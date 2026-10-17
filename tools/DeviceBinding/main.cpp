// SPDX-License-Identifier: MIT
// Original device-specific configuration utility. No HID input/report reads.
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <devguid.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr wchar_t HardwareId[] = L"USB\\VID_05AC&PID_0320&MI_01";
constexpr wchar_t BluetoothId[] = L"BTHENUM\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320";
// Published oemNN.inf names are reassigned by Windows; match the vendor by provider.
constexpr wchar_t VendorProvider[] = L"Magic Utilities Pty Ltd";
bool Equal(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
bool Target(const std::wstring& instance, const std::vector<std::wstring>& ids) {
    const auto& hardware = instance.size() > 8 && Equal(instance.substr(0, 8), L"BTHENUM\\") ? BluetoothId : HardwareId;
    const std::wstring prefix = std::wstring(hardware) + L"\\";
    return instance.size() > prefix.size() &&
        Equal(instance.substr(0, prefix.size()), prefix) &&
        std::any_of(ids.begin(), ids.end(), [&](const auto& id) { return Equal(id, hardware); });
}
[[noreturn]] void Fail(const char* operation, DWORD error = GetLastError()) {
    throw std::runtime_error(std::string(operation) + ": Windows error " + std::to_string(error));
}
struct DeviceSet {
    HDEVINFO value = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    DeviceSet() { if (value == INVALID_HANDLE_VALUE) Fail("SetupDiCreateDeviceInfoList"); }
    ~DeviceSet() { SetupDiDestroyDeviceInfoList(value); }
    DeviceSet(const DeviceSet&) = delete;
    DeviceSet& operator=(const DeviceSet&) = delete;
};
struct Audit {
    HANDLE file = INVALID_HANDLE_VALUE;
    ~Audit() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    void Open(const wchar_t* path) {
        file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) Fail("Create new audit file (refuses overwrite)");
    }
    void Line(const std::wstring& line) {
        std::wcout << line << L'\n';
        if (file == INVALID_HANDLE_VALUE) return;
        const std::wstring source = line + L"\r\n";
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.c_str(),
            -1, nullptr, 0, nullptr, nullptr);
        if (count <= 0) Fail("Encode audit");
        std::vector<char> bytes(static_cast<size_t>(count));
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.c_str(), -1,
            bytes.data(), count, nullptr, nullptr)) Fail("Encode audit");
        DWORD written = 0;
        const DWORD length = static_cast<DWORD>(count - 1);
        if (!WriteFile(file, bytes.data(), length, &written, nullptr) || written != length)
            Fail("Write audit");
        if (!FlushFileBuffers(file)) Fail("Flush audit");
    }
};
std::vector<std::wstring> Property(HDEVINFO set, SP_DEVINFO_DATA& device,
    const DEVPROPKEY& key, bool optional = false) {
    DWORD bytes = 0;
    DEVPROPTYPE type = 0;
    if (SetupDiGetDevicePropertyW(set, &device, &key, &type, nullptr, 0, &bytes, 0))
        throw std::runtime_error("Unexpected empty property");
    const DWORD error = GetLastError();
    if (optional && error == ERROR_NOT_FOUND) return {};
    if (error != ERROR_INSUFFICIENT_BUFFER) Fail("Query property size", error);
    if (bytes < sizeof(wchar_t) || bytes > 1024 * 1024 || bytes % sizeof(wchar_t))
        throw std::runtime_error("Invalid property length");
    std::vector<wchar_t> data(bytes / sizeof(wchar_t));
    if (!SetupDiGetDevicePropertyW(set, &device, &key, &type,
        reinterpret_cast<PBYTE>(data.data()), bytes, &bytes, 0)) Fail("Read property");
    if (bytes > data.size() * sizeof(wchar_t) || bytes % sizeof(wchar_t) || bytes == 0 ||
        (type != DEVPROP_TYPE_STRING && type != DEVPROP_TYPE_STRING_LIST))
        throw std::runtime_error("Unexpected property type or length");
    data.resize(bytes / sizeof(wchar_t));
    if (data.back() != L'\0') throw std::runtime_error("Unterminated property");
    std::vector<std::wstring> result;
    for (size_t i = 0; i < data.size() && data[i];) {
        const auto begin = data.begin() + static_cast<ptrdiff_t>(i);
        const auto end = std::find(begin, data.end(), L'\0');
        if (end == data.end()) throw std::runtime_error("Unterminated property member");
        result.emplace_back(begin, end);
        i += result.back().size() + 1;
        if (type == DEVPROP_TYPE_STRING) break;
    }
    return result;
}
std::wstring Text(HDEVINFO set, SP_DEVINFO_DATA& device, const DEVPROPKEY& key) {
    const auto values = Property(set, device, key);
    if (values.size() != 1) throw std::runtime_error("Required string property missing");
    return values.front();
}
void Snapshot(Audit& audit, HDEVINFO set, SP_DEVINFO_DATA& device) {
    audit.Line(L"DriverInf=" + Text(set, device, DEVPKEY_Device_DriverInfPath));
    audit.Line(L"Provider=" + Text(set, device, DEVPKEY_Device_DriverProvider));
    audit.Line(L"Version=" + Text(set, device, DEVPKEY_Device_DriverVersion));
    audit.Line(L"Service=" + Text(set, device, DEVPKEY_Device_Service));
    const std::pair<const wchar_t*, const DEVPROPKEY*> filters[] = {
        { L"UpperFilters", &DEVPKEY_Device_UpperFilters },
        { L"LowerFilters", &DEVPKEY_Device_LowerFilters }
    };
    for (const auto& filter : filters) {
        auto line = std::wstring(filter.first) + L"=";
        for (const auto& value : Property(set, device, *filter.second, true)) line += value + L";";
        audit.Line(line);
    }
    ULONG status = 0, problem = 0;
    const auto cr = CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0);
    if (cr != CR_SUCCESS) throw std::runtime_error("Cannot read present device status");
    audit.Line(L"DevNodeStatus=" + std::to_wstring(status) + L"; Problem=" + std::to_wstring(problem));
}
bool Elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) Fail("OpenProcessToken");
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes);
    const DWORD error = GetLastError();
    CloseHandle(token);
    if (!ok) Fail("GetTokenInformation", error);
    return elevation.TokenIsElevated != 0;
}
SP_DRVINFO_DATA_W Candidate(HDEVINFO set, SP_DEVINFO_DATA& device,
    const std::wstring& infName, const std::wstring& provider, Audit& audit, bool project = false) {
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH) Fail("GetWindowsDirectory");
    const bool anyInf = infName.empty(); // any staged INF from the expected provider
    const std::wstring path = project || anyInf ? infName : std::wstring(directory) + L"\\INF\\" + infName;
    if (project && (path.size() < 4 || path[1] != L':' || path[2] != L'\\' ||
        !Equal(path.substr(path.find_last_of(L"\\") + 1), L"OpenMagicKeyboardFilter.inf")))
        throw std::runtime_error("Project requires an absolute local OpenMagicKeyboardFilter.inf path");
    SP_DEVINSTALL_PARAMS_W params{};
    params.cbSize = sizeof(params);
    if (!SetupDiGetDeviceInstallParamsW(set, &device, &params)) Fail("Get install parameters");
    if (!anyInf) {
        params.Flags |= DI_ENUMSINGLEINF;
        if (wcscpy_s(params.DriverPath, path.c_str())) throw std::runtime_error("INF path too long");
    }
    params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    // These parameters affect this in-memory device information set only.
    if (!SetupDiSetDeviceInstallParamsW(set, &device, &params)) Fail("Set enumeration parameters");
    if (!SetupDiBuildDriverInfoList(set, &device, SPDIT_COMPATDRIVER)) Fail("Build compatible driver list");
    for (DWORD index = 0; index < 256; ++index) {
        SP_DRVINFO_DATA_W driver{};
        driver.cbSize = sizeof(driver);
        if (!SetupDiEnumDriverInfoW(set, &device, SPDIT_COMPATDRIVER, index, &driver)) {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_ITEMS) break;
            Fail("Enumerate compatible drivers", error);
        }
        DWORD required = 0;
        if (SetupDiGetDriverInfoDetailW(set, &device, &driver, nullptr, 0, &required) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            required < sizeof(SP_DRVINFO_DETAIL_DATA_W) || required > 1024 * 1024)
            throw std::runtime_error("Invalid driver detail size");
        std::vector<ULONG_PTR> buffer((required + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
        auto* detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDriverInfoDetailW(set, &device, &driver, detail, required, nullptr))
            Fail("Read compatible driver detail");
        if ((!anyInf && !Equal(detail->InfFileName, path)) || !Equal(driver.ProviderName, provider)) continue;
        audit.Line(L"CandidateInf=" + std::wstring(detail->InfFileName));
        audit.Line(L"CandidateProvider=" + std::wstring(driver.ProviderName));
        audit.Line(L"CandidateSection=" + std::wstring(detail->SectionName));
        audit.Line(L"CandidateDescription=" + std::wstring(driver.Description));
        return driver;
    }
    throw std::runtime_error("Expected preinstalled compatible INF/provider not found; no change made");
}
int SelfTest() {
    const std::wstring target = std::wstring(HardwareId) + L"\\test";
    if (!Target(target, { HardwareId }) ||
        Target(L"USB\\VID_05AC&PID_0320&MI_00\\test", { HardwareId }) ||
        Target(L"HID\\VID_05AC&PID_0320&MI_01\\test", { HardwareId }) ||
        Target(target, { L"USB\\VID_05AC&PID_0321&MI_01" }) ||
        Target(std::wstring(HardwareId) + L"EXTRA\\test", { HardwareId }) ||
        Target(HardwareId, { HardwareId }) || Target(target, {}))
        throw std::runtime_error("Device target guard failed");
    std::wcout << L"7 target guard checks passed; no device opened.\n";
    const std::wstring bt = std::wstring(BluetoothId) + L"\\test";
    if (!Target(bt,{BluetoothId}) || Target(bt,{HardwareId}) || Target(std::wstring(BluetoothId)+L"EXTRA\\test",{BluetoothId}) || Target(BluetoothId,{BluetoothId}))
        throw std::runtime_error("Bluetooth target guard failed");
    std::wcout << L"4 Bluetooth target guard checks passed; no device opened.\n";
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    Audit audit;
    try {
        if (argc == 2 && Equal(argv[1], L"--self-test")) return SelfTest();
        if (argc == 1 || (argc == 2 && Equal(argv[1], L"--help"))) {
            std::wcout << L"Device-specific MI_01 binding utility, x64.\n"
                L"--plan --instance EXACT_USB_INSTANCE\n"
                L"--plan-vendor --instance EXACT_USB_INSTANCE\n"
                L"--apply-microsoft --instance EXACT_USB_INSTANCE --audit NEW_FILE\n"
                L"--restore-vendor --instance EXACT_USB_INSTANCE --audit NEW_FILE\n"
                L"--plan-project --instance EXACT_USB_INSTANCE --inf ABSOLUTE_PROJECT_INF\n"
                L"--apply-project --instance EXACT_USB_INSTANCE --inf ABSOLUTE_PROJECT_INF --audit NEW_FILE\n"
                L"--self-test\nPlans never install. Apply/restore require an elevated token.\n"
                L"Vendor commands select the staged Magic Utilities package by provider.\n";
            return 0;
        }
        const bool microsoft = Equal(argv[1], L"--apply-microsoft");
        const bool restore = Equal(argv[1], L"--restore-vendor");
        const bool planVendor = Equal(argv[1], L"--plan-vendor");
        const bool updateProject = Equal(argv[1], L"--update-project") || Equal(argv[1], L"--plan-update-project");
        const bool applyProject = Equal(argv[1], L"--apply-project") || Equal(argv[1], L"--update-project");
        const bool planProject = Equal(argv[1], L"--plan-project") || Equal(argv[1], L"--plan-update-project");
        const bool project = applyProject || planProject;
        const bool apply = microsoft || restore || applyProject;
        const int auditIndex = project ? 6 : 4;
        if ((!apply && !Equal(argv[1], L"--plan") && !planVendor && !planProject) ||
            argc != (project ? (apply ? 8 : 6) : (apply ? 6 : 4)) || !Equal(argv[2], L"--instance") ||
            (project && (!Equal(argv[4], L"--inf") || !argv[5][0])) ||
            (apply && (!Equal(argv[auditIndex], L"--audit") || !argv[auditIndex + 1][0])))
            throw std::runtime_error("Invalid arguments; use --help. No change made.");
        if (apply && !Elevated()) throw std::runtime_error("Administrator elevation required; no change made");
        DeviceSet set;
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiOpenDeviceInfoW(set.value, argv[3], nullptr, 0, &device)) Fail("Open exact device");
        if (!IsEqualGUID(device.ClassGuid, GUID_DEVCLASS_HIDCLASS) ||
            !Target(argv[3], Property(set.value, device, DEVPKEY_Device_HardwareIds)))
            throw std::runtime_error("Refusing device: requires exact USB VID05AC/PID0320/MI_01 and HIDClass");
        DEVINST present = 0;
        std::wstring instance(argv[3]);
        if (CM_Locate_DevNodeW(&present, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS ||
            present != device.DevInst)
            throw std::runtime_error("Target must be present; no phantom-device changes");
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0) != CR_SUCCESS)
            throw std::runtime_error("Cannot read target status");
        // Recovery must remain possible if a failed binding stops the devnode.
        const auto currentInf = Text(set.value, device, DEVPKEY_Device_DriverInfPath);
        const std::wstring baseline = Target(argv[3], {BluetoothId}) ? L"hidbth.inf" : L"input.inf";
        const auto currentProvider = Text(set.value, device, DEVPKEY_Device_DriverProvider);
        const bool ours = Equal(currentProvider, L"OpenMagicKeyboard Project") && currentInf.size() > 7 &&
            Equal(currentInf.substr(0, 3), L"oem") && Equal(currentInf.substr(currentInf.size() - 4), L".inf");
        // Project install/update may start from Microsoft, the vendor package or an
        // existing project binding, including a failed one: a stale project package
        // whose service binary vanished (Problem 39) must be repairable in place.
        if (!restore && !planVendor && !project && !ours && (!(status & DN_STARTED) || problem))
            throw std::runtime_error("Initial switch requires a started, problem-free target");
        const bool vendorBound = Equal(currentProvider, VendorProvider) && currentInf.size() > 7 &&
            Equal(currentInf.substr(0, 3), L"oem") && Equal(currentInf.substr(currentInf.size() - 4), L".inf");
        if (!(vendorBound || (Equal(currentInf, baseline) && Equal(currentProvider, L"Microsoft")) || ours))
            throw std::runtime_error("Unexpected current binding; refresh rollback evidence before changing it");
        (void)updateProject;
        if (apply) audit.Open(argv[auditIndex + 1]);
        audit.Line(L"Instance=" + std::wstring(argv[3]));
        audit.Line(L"Before:");
        Snapshot(audit, set.value, device);
        const bool vendor = restore || planVendor;
        auto driver = Candidate(set.value, device, project ? argv[5] : vendor ? L"" : baseline,
            project ? L"OpenMagicKeyboard Project" : vendor ? VendorProvider : L"Microsoft", audit, project);
        if (!apply) { audit.Line(L"PLAN ONLY. No device binding changed."); return 0; }
        audit.Line(L"BEGIN DiInstallDevice: explicit driver, one devnode, Flags=0.");
        BOOL reboot = FALSE;
        // Never pass a null driver (would pick the best-ranked vendor package).
        const BOOL installed = DiInstallDevice(nullptr, set.value, &device, &driver, 0, &reboot);
        const DWORD error = installed ? ERROR_SUCCESS : GetLastError();
        audit.Line(L"DiInstallDeviceSuccess=" + std::to_wstring(installed != FALSE) +
            L"; WindowsError=" + std::to_wstring(error) + L"; NeedReboot=" + std::to_wstring(reboot != FALSE));
        if (!installed) Fail("DiInstallDevice (inspect post-state; failures may have partial effects)", error);
        audit.Line(L"After (properties only; verify live stack separately):");
        Snapshot(audit, set.value, device);
        audit.Line(reboot ? L"Restart required. Save work, then restart manually. No automatic restart."
            : L"Installation returned success without a restart request.");
        return reboot ? 3010 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            const std::string message(error.what());
            audit.Line(L"ERROR: " + std::wstring(message.begin(), message.end()));
        } catch (...) { /* Preserve original failure when audit itself fails. */ }
        return 1;
    }
}
