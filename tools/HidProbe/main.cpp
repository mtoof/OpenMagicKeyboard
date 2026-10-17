// Independently written from documented Windows APIs. See LICENSE and docs/sources.md.
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr USHORT Vendor = 0x05AC, Product = 0x0320;
constexpr USHORT BluetoothVendor = 0x004C;
constexpr wchar_t BluetoothPrefix[] = L"HID\\{00001124-0000-1000-8000-00805F9B34FB}_VID&0001004C_PID&0320";
constexpr unsigned MaxSeconds = 30, MaxReports = 4096;

class Handle {
public:
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : h_(h) {}
    ~Handle() { if (valid()) CloseHandle(h_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return h_; }
    bool valid() const { return h_ != INVALID_HANDLE_VALUE && h_ != nullptr; }
private:
    HANDLE h_;
};
struct DeviceSet {
    HDEVINFO value;
    ~DeviceSet() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
struct Preparsed {
    PHIDP_PREPARSED_DATA value = nullptr;
    ~Preparsed() { if (value) HidD_FreePreparsedData(value); }
};

std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    if (s.size() > INT_MAX) throw std::runtime_error("String too long");
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
        static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid Unicode string");
    std::string out(static_cast<size_t>(n), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
        static_cast<int>(s.size()), out.data(), n, nullptr, nullptr))
        throw std::runtime_error("Unicode conversion failed");
    return out;
}
std::string Json(const std::string& s) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[7]{};
                sprintf_s(escaped, "\\u%04x", static_cast<unsigned>(c));
                out << escaped;
            } else out << static_cast<char>(c);
        }
    }
    return out.str() + '"';
}
std::wstring Upper(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towupper(c));
    return s;
}
bool HasIdPrefix(const std::wstring& id, const std::wstring& prefix) {
    const auto upper = Upper(id);
    return upper.compare(0, prefix.size(), prefix) == 0 &&
        upper.size() > prefix.size() &&
        (upper[prefix.size()] == L'&' || upper[prefix.size()] == L'\\');
}
bool IsMi01(const std::vector<std::wstring>& ancestry) {
    for (const auto& id : ancestry) {
        const auto upper = Upper(id);
        const std::wstring prefix = L"USB\\VID_05AC&PID_0320&MI_01\\";
        if (upper.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}
std::wstring DeviceId(DEVINST node) {
    ULONG n = 0;
    if (CM_Get_Device_ID_Size(&n, node, 0) != CR_SUCCESS || n > 4096) return {};
    std::vector<wchar_t> buffer(static_cast<size_t>(n) + 1);
    if (CM_Get_Device_IDW(node, buffer.data(), n + 1, 0) != CR_SUCCESS) return {};
    return buffer.data();
}
std::vector<std::wstring> Ancestors(DEVINST node) {
    std::vector<std::wstring> result;
    for (unsigned depth = 0; depth < 32; ++depth) {
        const auto id = DeviceId(node);
        if (id.empty()) break;
        result.push_back(id);
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    return result;
}
std::string UtcNow() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char buffer[40]{};
    sprintf_s(buffer, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buffer;
}

// Exclusive CREATE_NEW prevents accidental replacement of a prior observation.
class OutputFile {
public:
    explicit OutputFile(const std::wstring& path) : file_(CreateFileW(path.c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        if (!file_.valid()) throw std::runtime_error("Cannot create output file (Win32 " +
            std::to_string(GetLastError()) + "); choose a new filename in an existing directory");
    }
    void Write(const std::string& data) {
        size_t offset = 0;
        while (offset < data.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1U << 20));
            DWORD written = 0;
            if (!WriteFile(file_.get(), data.data() + offset, chunk, &written, nullptr) || !written)
                throw std::runtime_error("Output write failed");
            offset += written;
        }
    }
private:
    Handle file_;
};

struct ReportCaps {
    std::vector<HIDP_BUTTON_CAPS> buttons;
    std::vector<HIDP_VALUE_CAPS> values;
    std::set<unsigned> ids;
};
struct Collection {
    std::wstring path, instance;
    std::vector<std::wstring> ancestry;
    HIDD_ATTRIBUTES attributes{};
    HIDP_CAPS caps{};
    std::array<ReportCaps, 3> reports;
    std::vector<HIDP_LINK_COLLECTION_NODE> links;
    bool ready = false;
    std::string error;
};
void LoadCaps(HANDLE device, Collection& c) {
    c.ready = false;
    c.error.clear();
    c.attributes = {};
    c.attributes.Size = sizeof(c.attributes);
    if (!HidD_GetAttributes(device, &c.attributes)) {
        c.error = "HidD_GetAttributes failed: " + std::to_string(GetLastError()); return;
    }
    const USHORT expectedVendor = HasIdPrefix(c.instance, BluetoothPrefix) ? BluetoothVendor : Vendor;
    if (c.attributes.VendorID != expectedVendor || c.attributes.ProductID != Product) {
        c.error = "Device attributes do not match the exact supported USB/Bluetooth identity"; return;
    }
    Preparsed ppd;
    if (!HidD_GetPreparsedData(device, &ppd.value)) {
        c.error = "HidD_GetPreparsedData failed: " + std::to_string(GetLastError()); return;
    }
    if (HidP_GetCaps(ppd.value, &c.caps) != HIDP_STATUS_SUCCESS) {
        c.error = "HidP_GetCaps failed"; return;
    }
    const std::array<USHORT, 3> buttonCounts{ c.caps.NumberInputButtonCaps,
        c.caps.NumberOutputButtonCaps, c.caps.NumberFeatureButtonCaps };
    const std::array<USHORT, 3> valueCounts{ c.caps.NumberInputValueCaps,
        c.caps.NumberOutputValueCaps, c.caps.NumberFeatureValueCaps };
    for (size_t i = 0; i < c.reports.size(); ++i) {
        auto& r = c.reports[i];
        r = {};
        const auto type = static_cast<HIDP_REPORT_TYPE>(i);
        if (buttonCounts[i]) {
            r.buttons.resize(buttonCounts[i]);
            auto count = buttonCounts[i];
            if (HidP_GetButtonCaps(type, r.buttons.data(), &count, ppd.value) != HIDP_STATUS_SUCCESS ||
                count > r.buttons.size()) { c.error = "Invalid button capabilities"; return; }
            r.buttons.resize(count);
            for (const auto& b : r.buttons) r.ids.insert(b.ReportID);
        }
        if (valueCounts[i]) {
            r.values.resize(valueCounts[i]);
            auto count = valueCounts[i];
            if (HidP_GetValueCaps(type, r.values.data(), &count, ppd.value) != HIDP_STATUS_SUCCESS ||
                count > r.values.size()) { c.error = "Invalid value capabilities"; return; }
            r.values.resize(count);
            for (const auto& v : r.values) r.ids.insert(v.ReportID);
        }
    }
    c.links.clear();
    if (c.caps.NumberLinkCollectionNodes) {
        c.links.resize(c.caps.NumberLinkCollectionNodes);
        ULONG count = c.caps.NumberLinkCollectionNodes;
        if (HidP_GetLinkCollectionNodes(c.links.data(), &count, ppd.value) != HIDP_STATUS_SUCCESS ||
            count > c.links.size()) { c.error = "Invalid link collection capabilities"; return; }
        c.links.resize(count);
    }
    c.ready = true;
}
std::string Kind(const HIDP_CAPS& c) {
    if (c.UsagePage == 1 && (c.Usage == 6 || c.Usage == 7)) return "keyboard";
    if (c.UsagePage == 0x0C && c.Usage == 1) return "consumer-control";
    if (c.UsagePage >= 0xFF00) return "vendor-defined";
    return "other";
}
bool CanTrace(const Collection& c) {
    if (!c.ready || !IsMi01(c.ancestry) || c.caps.InputReportByteLength == 0 ||
        c.reports[0].ids.empty()) return false;
    if (Kind(c.caps) != "vendor-defined" && Kind(c.caps) != "consumer-control") return false;
    for (const auto& b : c.reports[0].buttons) if (b.UsagePage == 7) return false;
    for (const auto& v : c.reports[0].values) if (v.UsagePage == 7) return false;
    for (const auto& l : c.links) {
        if (l.LinkUsagePage == 7 || (l.LinkUsagePage == 1 &&
            (l.LinkUsage == 6 || l.LinkUsage == 7))) return false;
    }
    return true;
}
bool ValidReport(const Collection& c, const std::vector<unsigned char>& buffer, DWORD length) {
    return length != 0 && length == c.caps.InputReportByteLength &&
        length <= buffer.size() && c.reports[0].ids.count(buffer[0]) != 0;
}

std::vector<Collection> Enumerate() {
    GUID guid{};
    HidD_GetHidGuid(&guid);
    DeviceSet devices{ SetupDiGetClassDevsW(&guid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE) };
    if (devices.value == INVALID_HANDLE_VALUE) throw std::runtime_error("HID enumeration failed");
    std::vector<Collection> result;
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA iface{};
        iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(devices.value, nullptr, &guid, i, &iface)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            throw std::runtime_error("HID interface enumeration failed");
        }
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.value, &iface, nullptr, 0, &bytes, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes > 1U << 20)
            throw std::runtime_error("Invalid HID interface detail size");
        // Heap allocation has alignment suitable for the detail structure.
        std::vector<ULONG_PTR> storage((bytes + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
        auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.value, &iface, detail, bytes, nullptr, &info))
            throw std::runtime_error("HID interface detail query failed");
        Collection c;
        c.instance = DeviceId(info.DevInst);
        if (!HasIdPrefix(c.instance, L"HID\\VID_05AC&PID_0320") && !HasIdPrefix(c.instance, BluetoothPrefix)) continue;
        c.path = detail->DevicePath;
        c.ancestry = Ancestors(info.DevInst);
        Handle device(CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr));
        if (device.valid()) LoadCaps(device.get(), c);
        else c.error = "Metadata open denied/failed: " + std::to_string(GetLastError());
        result.push_back(std::move(c));
    }
    std::sort(result.begin(), result.end(), [](const Collection& a, const Collection& b) {
        return a.instance < b.instance;
    });
    return result;
}
template<class T> void WriteCapability(std::ostream& out, const T& c) {
    const USHORT low = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
    const USHORT high = c.IsRange ? c.Range.UsageMax : c.NotRange.Usage;
    out << "{\"reportId\":" << static_cast<unsigned>(c.ReportID)
        << ",\"usagePage\":" << c.UsagePage << ",\"usageMin\":" << low
        << ",\"usageMax\":" << high << ",\"linkCollection\":" << c.LinkCollection
        << ",\"isRange\":" << (c.IsRange ? "true" : "false")
        << ",\"isAlias\":" << (c.IsAlias ? "true" : "false")
        << ",\"bitField\":" << c.BitField;
}
void WriteCollection(std::ostream& out, const Collection& c) {
    out << "{\"instanceId\":" << Json(Utf8(c.instance)) << ",\"devicePath\":" << Json(Utf8(c.path))
        << ",\"mi01Ancestor\":" << (IsMi01(c.ancestry) ? "true" : "false")
        << ",\"metadataComplete\":" << (c.ready ? "true" : "false")
        << ",\"error\":" << Json(c.error) << ",\"ancestors\":[";
    for (size_t i = 0; i < c.ancestry.size(); ++i) {
        if (i) out << ',';
        out << Json(Utf8(c.ancestry[i]));
    }
    out << ']';
    if (c.ready) {
        out << ",\"vid\":" << c.attributes.VendorID << ",\"pid\":" << c.attributes.ProductID
            << ",\"deviceVersion\":" << c.attributes.VersionNumber
            << ",\"kind\":" << Json(Kind(c.caps)) << ",\"usagePage\":" << c.caps.UsagePage
            << ",\"usage\":" << c.caps.Usage
            << ",\"traceEligible\":" << (CanTrace(c) ? "true" : "false") << ",\"reports\":[";
        const std::array<const char*, 3> names{ "input", "output", "feature" };
        const std::array<USHORT, 3> lengths{ c.caps.InputReportByteLength,
            c.caps.OutputReportByteLength, c.caps.FeatureReportByteLength };
        for (size_t i = 0; i < 3; ++i) {
            if (i) out << ',';
            out << "{\"type\":" << Json(names[i]) << ",\"maxByteLengthIncludingId\":" << lengths[i]
                << ",\"idsFromCapabilities\":[";
            bool first = true;
            for (const auto id : c.reports[i].ids) { if (!first) out << ','; first = false; out << id; }
            out << "],\"buttons\":[";
            first = true;
            for (const auto& b : c.reports[i].buttons) {
                if (!first) out << ',';
                first = false; WriteCapability(out, b); out << '}';
            }
            out << "],\"values\":[";
            first = true;
            for (const auto& v : c.reports[i].values) {
                if (!first) out << ',';
                first = false; WriteCapability(out, v);
                out << ",\"bitSize\":" << v.BitSize << ",\"reportCount\":" << v.ReportCount
                    << ",\"logicalMin\":" << v.LogicalMin << ",\"logicalMax\":" << v.LogicalMax
                    << ",\"physicalMin\":" << v.PhysicalMin << ",\"physicalMax\":" << v.PhysicalMax
                    << ",\"units\":" << v.Units << ",\"unitsExp\":" << v.UnitsExp << '}';
            }
            out << "]}";
        }
        out << "],\"linkCollections\":[";
        for (size_t i = 0; i < c.links.size(); ++i) {
            if (i) out << ',';
            const auto& l = c.links[i];
            out << "{\"index\":" << i << ",\"usagePage\":" << l.LinkUsagePage
                << ",\"usage\":" << l.LinkUsage << ",\"parent\":" << l.Parent
                << ",\"numberOfChildren\":" << l.NumberOfChildren << '}';
        }
        out << ']';
    }
    out << '}';
}
std::string Inventory(const std::vector<Collection>& collections) {
    std::ostringstream out;
    out << "{\"schemaVersion\":1,\"mode\":\"metadata-only\",\"timestampUtc\":" << Json(UtcNow())
        << ",\"reportDescriptor\":{\"status\":\"not-available-via-documented-HidD-APIs\","
        "\"note\":\"Parsed capabilities are saved; opaque preparsed data is not a raw report descriptor. "
        "Report IDs from capabilities can omit constant-only reports. Lengths are per-type maxima, not per-ID lengths.\"},"
        "\"collections\":[";
    for (size_t i = 0; i < collections.size(); ++i) {
        if (i) out << ',';
        WriteCollection(out, collections[i]);
    }
    out << "]}\n";
    return out.str();
}

struct Options {
    bool help = false, selfTest = false, trace = false;
    std::wstring output, collection, step;
    unsigned seconds = 5;
};
bool KnownStep(const std::wstring& s) {
    const std::array<const wchar_t*, 7> steps{ L"baseline", L"globe-down", L"globe-up",
        L"left", L"globe-left", L"f1", L"globe-f1" };
    return std::any_of(steps.begin(), steps.end(), [&s](const wchar_t* step) { return s == step; });
}
Options Parse(int argc, wchar_t** argv) {
    Options o;
    std::set<std::wstring> seen;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (!seen.insert(arg).second) throw std::runtime_error("Duplicate option");
        if (arg == L"--help") o.help = true;
        else if (arg == L"--self-test") o.selfTest = true;
        else if (arg == L"--trace") o.trace = true;
        else if (arg == L"--output" || arg == L"--collection" || arg == L"--step" || arg == L"--seconds") {
            if (++i >= argc || !*argv[i]) throw std::runtime_error("Missing option value");
            const std::wstring value = argv[i];
            if (arg == L"--output") o.output = value;
            else if (arg == L"--collection") o.collection = value;
            else if (arg == L"--step") o.step = value;
            else {
                if (value.size() > 2 || value.find_first_not_of(L"0123456789") != std::wstring::npos)
                    throw std::runtime_error("Seconds must be an integer from 1 through 30");
                o.seconds = static_cast<unsigned>(std::stoul(value));
                if (!o.seconds || o.seconds > MaxSeconds) throw std::runtime_error("Seconds must be 1 through 30");
            }
        } else throw std::runtime_error("Unknown option; use --help");
    }
    if ((o.help || o.selfTest) && argc != 2) throw std::runtime_error("Use --help or --self-test alone");
    if (o.trace && (o.output.empty() || o.collection.empty() || !KnownStep(o.step)))
        throw std::runtime_error("Trace needs --output, exact --collection instance ID, and a documented --step");
    if (!o.trace && (!o.collection.empty() || !o.step.empty() || seen.count(L"--seconds")))
        throw std::runtime_error("Capture options require explicit --trace");
    return o;
}
void DrainRead(HANDLE device, OVERLAPPED& pending) {
    CancelIoEx(device, &pending);
    DWORD ignored = 0;
    // OVERLAPPED and its buffer must live until cancellation/completion is observed.
    GetOverlappedResult(device, &pending, &ignored, TRUE);
}
void Trace(const Options& o, const std::vector<Collection>& collections) {
    const auto it = std::find_if(collections.begin(), collections.end(), [&o](const Collection& c) {
        return Upper(c.instance) == Upper(o.collection);
    });
    if (it == collections.end()) throw std::runtime_error("Collection is not a present target keyboard collection");
    if (!CanTrace(*it)) throw std::runtime_error("Capture refused: only MI_01 vendor/consumer collections without keyboard usages are eligible");
    Collection c = *it;
    Handle device(CreateFileW(c.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (!device.valid()) throw std::runtime_error("Collection read access failed: " + std::to_string(GetLastError()));
    LoadCaps(device.get(), c); // Revalidate the handle actually used for reads.
    if (!CanTrace(c)) throw std::runtime_error("Capture refused after revalidating collection capabilities");
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) throw std::runtime_error("Cannot create read completion event");
    OutputFile output(o.output);
    const bool fnIntent = o.step == L"globe-down" || o.step == L"globe-left" || o.step == L"globe-f1";
    std::ostringstream header;
    header << "{\"kind\":\"capture-start\",\"timestampUtc\":" << Json(UtcNow())
        << ",\"step\":" << Json(Utf8(o.step)) << ",\"fnIntentionallyHeld\":" << (fnIntent ? "true" : "false")
        << ",\"intentSource\":\"operator-label-not-measured-state\",\"seconds\":" << o.seconds
        << ",\"collection\":";
    WriteCollection(header, c);
    header << "}\n";
    output.Write(header.str());
    std::cerr << "Capture begins in 3 seconds. Do not type text. Perform only the labeled step.\n";
    Sleep(3000);
    const ULONGLONG start = GetTickCount64();
    const ULONGLONG deadline = start + static_cast<ULONGLONG>(o.seconds) * 1000;
    std::vector<unsigned char> buffer(c.caps.InputReportByteLength);
    unsigned reports = 0;
    std::string stopReason = "duration";
    while (reports < MaxReports && GetTickCount64() < deadline) {
        std::fill(buffer.begin(), buffer.end(), static_cast<unsigned char>(0));
        ResetEvent(event.get());
        OVERLAPPED pending{};
        pending.hEvent = event.get();
        DWORD length = 0;
        if (!ReadFile(device.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &length, &pending)) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) { stopReason = "read-error-" + std::to_string(error); break; }
            const auto now = GetTickCount64();
            const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
            const DWORD wait = WaitForSingleObject(event.get(), remaining);
            if (wait != WAIT_OBJECT_0) {
                DrainRead(device.get(), pending);
                stopReason = wait == WAIT_TIMEOUT ? "duration" : "wait-error";
                break;
            }
            if (!GetOverlappedResult(device.get(), &pending, &length, FALSE)) {
                stopReason = "completion-error-" + std::to_string(GetLastError()); break;
            }
        }
        ++reports;
        std::ostringstream row;
        row << "{\"kind\":" << Json(ValidReport(c, buffer, length) ? "report" : "rejected-report")
            << ",\"timestampUtc\":" << Json(UtcNow())
            << ",\"elapsedMs\":" << GetTickCount64() - start
            << ",\"step\":" << Json(Utf8(o.step))
            << ",\"fnIntentionallyHeld\":" << (fnIntent ? "true" : "false")
            << ",\"length\":" << length;
        if (ValidReport(c, buffer, length)) {
            row << ",\"reportId\":" << static_cast<unsigned>(buffer[0]) << ",\"hex\":\"";
            constexpr char digits[] = "0123456789abcdef";
            for (DWORD i = 0; i < length; ++i) row << digits[buffer[i] >> 4] << digits[buffer[i] & 15];
            row << '"';
        } else row << ",\"reason\":\"unexpected-length-or-report-id; raw bytes omitted\"";
        row << "}\n";
        output.Write(row.str());
    }
    if (reports == MaxReports) stopReason = "report-limit";
    output.Write("{\"kind\":\"capture-end\",\"timestampUtc\":" + Json(UtcNow()) +
        ",\"reports\":" + std::to_string(reports) + ",\"reason\":" + Json(stopReason) + "}\n");
    if (stopReason != "duration" && stopReason != "report-limit") throw std::runtime_error(stopReason);
}

int SelfTest() {
    unsigned checks = 0;
    const auto require = [&checks](bool ok) { ++checks; if (!ok) throw std::runtime_error("Safety self-test failed"); };
    require(Json("a\n\\\"") == "\"a\\n\\\\\\\"\"");
    require(!HasIdPrefix(L"HID\\VID_05AC&PID_03201&MI_01", L"HID\\VID_05AC&PID_0320"));
    require(HasIdPrefix(L"hid\\vid_05ac&pid_0320&mi_01", L"HID\\VID_05AC&PID_0320"));
    require(HasIdPrefix(L"HID\\{00001124-0000-1000-8000-00805F9B34FB}_VID&0001004C_PID&0320&COL01", BluetoothPrefix));
    require(!HasIdPrefix(L"HID\\{00001124-0000-1000-8000-00805F9B34FB}_VID&0001004C_PID&03201&COL01", BluetoothPrefix));
    Collection c;
    c.ready = true; c.caps.UsagePage = 0xFF00; c.caps.Usage = 1;
    c.caps.InputReportByteLength = 3; c.reports[0].ids.insert(4);
    c.ancestry = { L"USB\\VID_05AC&PID_0320&MI_01\\test" };
    require(CanTrace(c));
    require(ValidReport(c, { 4, 0, 1 }, 3));
    require(!ValidReport(c, {}, 0));
    require(!ValidReport(c, { 4 }, 3));
    require(!ValidReport(c, { 4, 0 }, 2));
    require(!ValidReport(c, { 5, 0, 1 }, 3));
    require(!ValidReport(c, { 4, 0, 1, 2 }, 4));
    c.ancestry = { L"USB\\VID_05AC&PID_0320&MI_00\\test" }; require(!CanTrace(c));
    c.ancestry = { L"USB\\VID_05AC&PID_0320&MI_010\\test" }; require(!CanTrace(c));
    c.ancestry = { L"BTHENUM\\VID_05AC&PID_0320" }; require(!CanTrace(c));
    c.ancestry = { L"USB\\VID_05AC&PID_0320&MI_01\\test" };
    c.caps.UsagePage = 1; c.caps.Usage = 6; require(!CanTrace(c));
    c.caps.UsagePage = 0xFF00;
    HIDP_BUTTON_CAPS button{}; button.UsagePage = 7;
    c.reports[0].buttons.push_back(button); require(!CanTrace(c)); c.reports[0].buttons.clear();
    HIDP_VALUE_CAPS value{}; value.UsagePage = 7;
    c.reports[0].values.push_back(value); require(!CanTrace(c)); c.reports[0].values.clear();
    HIDP_LINK_COLLECTION_NODE link{}; link.LinkUsagePage = 1; link.LinkUsage = 6;
    c.links.push_back(link); require(!CanTrace(c)); c.links.clear();
    c.ready = false; require(!CanTrace(c)); c.ready = true;
    c.reports[0].ids.clear(); require(!CanTrace(c));
    std::cout << checks << " safety checks passed; no device opened.\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const auto options = Parse(argc, argv);
        if (options.help) {
            std::cout << "MagicKeyboardHidProbe (USB investigation; no writes to devices)\n"
                "Default: enumerate exact USB 05AC/0320 and Bluetooth 004C/0320 HID metadata; no report reads or saved logs.\n"
                "  --output NEW.json        Save metadata (existing files are refused).\n"
                "  --self-test              Run safety checks without accessing devices.\n"
                "Capture ONLY an eligible MI_01 vendor/consumer collection:\n"
                "  --trace --collection EXACT_HID_INSTANCE_ID --output NEW.jsonl\n"
                "  --step baseline|globe-down|globe-up|left|globe-left|f1|globe-f1\n"
                "  [--seconds 1..30]         Default 5; maximum 4096 reports.\n"
                "A step labels operator intent, not detected Fn state. Do not type text\n"
                "during captures. Keyboard collections/keyboard usages are refused.\n"
                "MI_00 is metadata-only; Bluetooth capture is refused.\n";
            return 0;
        }
        if (options.selfTest) return SelfTest();
        const auto collections = Enumerate();
        if (options.trace) Trace(options, collections);
        else {
            const auto json = Inventory(collections);
            if (!options.output.empty()) { OutputFile output(options.output); output.Write(json); }
            std::cout << json;
            if (collections.empty()) std::cerr << "No present target HID collections found. Connect the keyboard by USB-C.\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
