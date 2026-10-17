/* SPDX-License-Identifier: MIT. Original bounded serializer; no proprietary implementation. */
#include "NativeCore.h"
static void Copy(unsigned char *a, const unsigned char *b, unsigned int n) { while (n--) *a++ = *b++; }
static void Zero(void *p, unsigned int n) { unsigned char *b = p; while (n--) *b++ = 0; }
static void PutLE(unsigned char *p, unsigned int size, unsigned int value) { unsigned int i; for (i = 0; i < size; ++i) p[i] = (unsigned char)(value >> (8u * i)); }
typedef struct GLOBALS { unsigned int Value[10], Known; unsigned char Width[10]; } GLOBALS;
/* Remove only exact redeclarations of established globals. Main/local items,
   first declarations and global stack operations remain byte-for-byte intact. */
static int CompactGlobals(unsigned char *bytes, unsigned int n, unsigned int *length) {
    GLOBALS state, stack[8]; unsigned int source = 0, dest = 0, depth = 0;
    Zero(&state, sizeof(state));
    while (source < n) {
        unsigned int prefix = bytes[source], size = prefix & 3u, type = (prefix >> 2) & 3u;
        unsigned int tag = prefix >> 4, value = 0, i, total; int omit = 0;
        if (prefix == 0xfe || type == 3) return 0;
        if (size == 3) size = 4;
        if (size > n - source - 1) return 0;
        total = size + 1;
        for (i = 0; i < size; ++i) value |= (unsigned int)bytes[source + 1 + i] << (8u * i);
        if (type == 1) {
            if (tag < 10) {
                omit = (state.Known & (1u << tag)) && state.Width[tag] == size && state.Value[tag] == value;
                state.Known |= 1u << tag; state.Width[tag] = (unsigned char)size; state.Value[tag] = value;
            } else if (tag == 10) {
                if (size || depth == 8) return 0; stack[depth++] = state;
            } else if (tag == 11) {
                if (size || !depth) return 0; state = stack[--depth];
            } else return 0;
        }
        if (!omit) { Copy(bytes + dest, bytes + source, total); dest += total; }
        source += total;
    }
    if (depth) return 0; *length = dest; return 1;
}
int MkNativeDescriptor(const unsigned char *in, unsigned int n, unsigned char *out, unsigned int cap,
    int bluetooth, int expand, int brightness, MK_NATIVE_RESULT *r) {
    /* Push/pop isolate the extra Consumer Control collection's globals. */
    static const unsigned char consumer[] = { 0xa4,0x05,0x0c,0x09,1,0xa1,1,0x85,0,
        0x15,0,0x26,0xff,3,0x19,0,0x2a,0xff,3,0x75,16,0x95,1,0x81,0,0xc0,0xb4 };
    unsigned char used[256]; unsigned int pos = 0, id = 0, i; MK_LAYOUT layout;
    if (!r) return 0; Zero(r, sizeof(*r)); Zero(used, sizeof(used));
    if (!in || !out || !n || n > MK_MAX_DESCRIPTOR || n > cap ||
        !(bluetooth ? MkParseBluetoothDescriptor(in, n, &layout) : MkParseDescriptor(in, n, &layout))) return 0;
    while (pos < n) {
        unsigned int tag = in[pos++], size = tag & 3u;
        if (tag == 0xfe) return 0; if (size == 3) size = 4;
        if (size > n - pos) return 0;
        if ((tag & 0xfcu) == 0x84 && size) {
            unsigned int value = 0;
            for (i = 0; i < size; ++i) value |= (unsigned int)in[pos + i] << (8u * i);
            if (value > 255) return 0; used[value] = 1;
        }
        pos += size;
    }
    if (brightness) { for (i = 1; i < 256; ++i) if (!used[i]) { id = i; break; } if (!id) return 0; }
    Copy(out, in, n);
    if (bluetooth && expand && layout.ArrayMax < 0x7f) {
        if (!layout.LogicalMaxSize || !layout.UsageMaxSize || (unsigned int)layout.LogicalMaxOffset + layout.LogicalMaxSize > n ||
            (unsigned int)layout.UsageMaxOffset + layout.UsageMaxSize > n) return 0;
        PutLE(out + layout.LogicalMaxOffset, layout.LogicalMaxSize, 0x7f);
        PutLE(out + layout.UsageMaxOffset, layout.UsageMaxSize, layout.UsageMaxSize == 4 ? 0x0007007fu : 0x7fu);
    }
    if (bluetooth && (expand || brightness) && !CompactGlobals(out, n, &n)) return 0;
    if (brightness && (sizeof(consumer) > cap - n || sizeof(consumer) > MK_MAX_DESCRIPTOR - n)) return 0;
    if (brightness) { Copy(out + n, consumer, sizeof(consumer)); out[n + 8] = (unsigned char)id; n += sizeof(consumer); }
    if (!(bluetooth ? MkParseBluetoothDescriptor(out, n, &r->Layout) : MkParseDescriptor(out, n, &r->Layout))) return 0;
    r->Length = r->DescriptorLength = n; r->ReportId = (unsigned char)id; return 1;
}
typedef struct NODE { unsigned int Type, Header, Size, Total; } NODE;
static int Node(const unsigned char *p, unsigned int n, NODE *v) {
    unsigned int d, i, bytes, value = 0;
    if (!n) return 0; v->Type = p[0] >> 3; d = p[0] & 7u; v->Header = 1;
    if (v->Type > 8) return 0;
    if (v->Type == 0) { if (d) return 0; v->Size = 0; }
    else if (d <= 4) v->Size = 1u << d;
    else {
        bytes = 1u << (d - 5); if (bytes > n - 1) return 0;
        for (i = 0; i < bytes; ++i) value = (value << 8) | p[1 + i];
        v->Header += bytes; v->Size = value;
    }
    if (v->Header > n || v->Size > n - v->Header) return 0;
    v->Total = v->Header + v->Size; return 1;
}
typedef struct WALK { unsigned int Nodes, Found; int Expand, Brightness; MK_NATIVE_RESULT Native; } WALK;
static int Serialize(const unsigned char *p, unsigned int n, unsigned char *out, unsigned int cap,
    unsigned int depth, int list, WALK *w, unsigned int *written) {
    NODE node; unsigned int source, dest, lastAttribute = 0, length, head, foundAtEntry = w->Found;
    if (depth > 12 || ++w->Nodes > 512 || !Node(p, n, &node) || cap < node.Header) return 0;
    dest = node.Header;
    if (list && node.Type == 6) {
        NODE first, second;
        const unsigned char *data = p + node.Header;
        if (Node(data, node.Size, &first) && first.Type == 1 && first.Size == 1 && data[first.Header] == 0x22 &&
            Node(data + first.Total, node.Size - first.Total, &second) && second.Type == 4 && first.Total + second.Total == node.Size) {
            MK_NATIVE_RESULT native; unsigned int textHead = second.Header, descriptorStart;
            if (!w->Expand && !w->Brightness) {
                if (w->Found || node.Total > cap || !MkParseBluetoothDescriptor(data + first.Total + second.Header, second.Size, &w->Native.Layout)) return 0;
                Copy(out, p, node.Total); w->Native.DescriptorOffset = node.Header + first.Total + second.Header;
                w->Native.DescriptorLength = second.Size; w->Found = 1; *written = node.Total; return 1;
            }
            if (textHead != 2 && textHead != 3 && textHead != 5) return 0;
            descriptorStart = node.Header + first.Total + textHead;
            if (w->Found || descriptorStart > cap || !MkNativeDescriptor(data + first.Total + second.Header, second.Size,
                out + descriptorStart, cap - descriptorStart, 1, w->Expand, w->Brightness, &native)) return 0;
            if (textHead == 2 && native.Length > 255) {
                unsigned int i;
                if (native.Length >= cap - descriptorStart) return 0;
                for (i = native.Length; i; --i) out[descriptorStart + i] = out[descriptorStart + i - 1];
                ++descriptorStart; textHead = 3;
            }
            Copy(out + node.Header, data, first.Total);
            out[node.Header + first.Total] = (unsigned char)(0x20 | (textHead == 2 ? 5 : textHead == 3 ? 6 : 7));
            if (textHead == 2) out[node.Header + first.Total + 1] = (unsigned char)native.Length;
            else if (textHead == 3) {
                out[node.Header + first.Total + 1] = (unsigned char)(native.Length >> 8);
                out[node.Header + first.Total + 2] = (unsigned char)native.Length;
            } else {
                out[node.Header + first.Total + 1] = out[node.Header + first.Total + 2] = 0;
                out[node.Header + first.Total + 3] = (unsigned char)(native.Length >> 8);
                out[node.Header + first.Total + 4] = (unsigned char)native.Length;
            }
            dest = descriptorStart + native.Length; w->Native = native;
            w->Native.DescriptorOffset = descriptorStart; w->Found = 1;
            goto Container;
        }
    }
    if (node.Type != 6 && node.Type != 7) {
        if (node.Total > cap) return 0; Copy(out, p, node.Total); *written = node.Total; return 1;
    }
    source = node.Header;
    while (source < node.Total) {
        NODE child; unsigned int childWritten, oldFound = w->Found;
        if (!Node(p + source, node.Total - source, &child) ||
            !Serialize(p + source, child.Total, out + dest, cap - dest, depth + 1,
                list || lastAttribute == 0x0206, w, &childWritten)) return 0;
        if (!oldFound && w->Found) w->Native.DescriptorOffset += dest;
        lastAttribute = child.Type == 1 && child.Size == 2 ?
            ((unsigned int)p[source + child.Header] << 8) | p[source + child.Header + 1] : 0;
        source += child.Total; dest += childWritten;
    }
Container:
    length = dest - node.Header; head = node.Header;
    if (head != 2 && head != 3 && head != 5) return 0;
    if (head == 2 && length > 255) head = 3;
    if (length > 65535) return 0;
    if (head > node.Header) {
        unsigned int i;
        if (head - node.Header > cap - dest) return 0;
        for (i = length; i; --i) out[head + i - 1] = out[node.Header + i - 1];
    }
    out[0] = (unsigned char)((node.Type << 3) | (head == 2 ? 5 : head == 3 ? 6 : 7));
    if (head == 2) out[1] = (unsigned char)length;
    else if (head == 3) { out[1] = (unsigned char)(length >> 8); out[2] = (unsigned char)length; }
    else { out[1] = out[2] = 0; out[3] = (unsigned char)(length >> 8); out[4] = (unsigned char)length; }
    if (!foundAtEntry && w->Found) w->Native.DescriptorOffset += head - node.Header;
    *written = length + head; return 1;
}
int MkSdpDescriptor(const unsigned char *in, unsigned int n, unsigned char *out, unsigned int cap,
    int expand, int brightness, MK_NATIVE_RESULT *r) {
    WALK walk; unsigned int written; NODE root;
    if (!r) return 0; Zero(r, sizeof(*r)); Zero(&walk, sizeof(walk));
    if (!in || !out || in == out || !n || n > MK_SDP_MAX || cap > MK_SDP_MAX ||
        !Node(in, n, &root) || root.Total != n || root.Type != 6) return 0;
    walk.Expand = expand; walk.Brightness = brightness;
    if (!Serialize(in, n, out, cap, 0, 0, &walk, &written) || !walk.Found ||
        walk.Native.DescriptorOffset > written || walk.Native.DescriptorLength > written - walk.Native.DescriptorOffset) return 0;
    *r = walk.Native; r->Length = written; return 1;
}
