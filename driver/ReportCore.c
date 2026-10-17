/* SPDX-License-Identifier: MIT. No device offsets or input contents are assumed. */
#include "ReportCore.h"
typedef struct MK_GLOBAL { unsigned int Page, Size, Count, Id, Min, Max, MaxOffset, MaxSize; } MK_GLOBAL;
static void Zero(void *p, unsigned int n) { unsigned char *b = p; while (n--) *b++ = 0; }
static unsigned int Read(const unsigned char *p, unsigned int n) {
    unsigned int v = 0, i; for (i = 0; i < n; ++i) v |= (unsigned int)p[i] << (8u * i); return v;
}
static unsigned int Usage(unsigned int page, unsigned int value, unsigned int size) {
    return size == 4 ? value : (page << 16) | value;
}
static int ParseDescriptor(const unsigned char *b, unsigned int n, MK_LAYOUT *out, unsigned int fnPage) {
    MK_GLOBAL g, stack[8]; MK_LAYOUT l;
    unsigned int bits[256], usages[32], depths[16];
    unsigned int pos = 0, sp = 0, depth = 0, un = 0, lo = 0, hi = 0, range = 0;
    unsigned int keyboard = 0, report = 256, found = 0, i, hiOffset = 0, hiSize = 0;
    if (!out) return 0;
    Zero(out, sizeof(*out)); Zero(&l, sizeof(l)); Zero(&g, sizeof(g)); Zero(bits, sizeof(bits));
    for (i = 0; i < 8; ++i) l.Modifier[i] = MK_NONE;
    l.Fn = l.ArrayBit = MK_NONE;
    if (!b || !n || n > MK_MAX_DESCRIPTOR) return 0;
    while (pos < n) {
        unsigned int prefix = b[pos++], size = prefix & 3u, type = (prefix >> 2) & 3u, tag = prefix >> 4;
        unsigned int v, total;
        if (prefix == 0xfe || type == 3) return 0;
        if (size == 3) size = 4;
        if (size > n - pos) return 0;
        v = Read(b + pos, size); pos += size;
        if (type == 1) {
            switch (tag) {
            case 0: if (v > 0xffff) return 0; g.Page = v; break;
            case 1: g.Min = v; break;
            case 2: g.Max = v; g.MaxOffset = pos - size; g.MaxSize = size; break;
            case 7: g.Size = v; break;
            case 8: if (!v || v > 255 || bits[0]) return 0; g.Id = v; break;
            case 9: g.Count = v; break;
            case 10: if (size || sp == 8) return 0; stack[sp++] = g; break;
            case 11: if (size || !sp) return 0; g = stack[--sp]; break;
            case 3: case 4: case 5: case 6: break; /* physical/unit metadata */
            default: return 0;
            }
        } else if (type == 2) {
            if (tag == 0) { if (un == 32 || range) return 0; usages[un++] = Usage(g.Page, v, size); }
            else if (tag == 1) { if (un || range) return 0; lo = Usage(g.Page, v, size); range = 1; }
            else if (tag == 2) { if (range != 1) return 0; hi = Usage(g.Page, v, size); hiOffset = pos - size; hiSize = size; range = 2; }
            else return 0; /* delimiters/designators/string selectors: unsupported, fail open */
        } else {
            if (tag == 10) {
                if (depth == 16 || range == 1) return 0;
                depths[depth++] = keyboard;
                if (v == 1 && un == 1 && usages[0] == 0x00010006u) {
                    if (keyboard || found) return 0; keyboard = 1; found = 1;
                }
            } else if (tag == 12) {
                if (size || !depth) return 0; keyboard = depths[--depth];
            } else if (tag == 8 || tag == 9 || tag == 11) {
                if (!depth || !g.Size || !g.Count || g.Size > 32 || g.Count > 512 || range == 1) return 0;
                if ((v & ~0x1ffu) || (tag == 8 && (v & 0x80u))) return 0;
                if (range == 2 && (hi < lo || (hi >> 16) != (lo >> 16))) return 0;
                total = g.Size * g.Count;
                if (tag == 8) {
                    if (total > 512 || bits[g.Id] > 512 - total || (!g.Id && report != 256 && report != 0)) return 0;
                    if (keyboard) {
                        if (report == 256) report = g.Id;
                        if (report != g.Id) return 0;
                        if (!(v & 1u)) {
                            if ((v & 2u) && !(v & 0x104u) && g.Size == 1 && g.Min == 0 && g.Max == 1) {
                                for (i = 0; i < g.Count; ++i) {
                                    unsigned int u = un ? usages[i < un ? i : un - 1] : range == 2 ? lo + (i < hi - lo ? i : hi - lo) : 0;
                                    unsigned int bit = bits[g.Id] + i;
                                    if (u >= 0x000700e0 && u <= 0x000700e7) {
                                        if (l.Modifier[u - 0x000700e0] != MK_NONE) return 0;
                                        l.Modifier[u - 0x000700e0] = (unsigned short)bit;
                                    } else if (u == (fnPage << 16 | 3u)) {
                                        if (l.Fn != MK_NONE) return 0; l.Fn = (unsigned short)bit;
                                    }
                                }
                            } else if (!(v & 0x106u) && g.Size == 8 && g.Min == 0 && g.Max >= (fnPage == 0xff01 ? 0x65u : 0x72u) &&
                                       range == 2 && lo == 0x00070000 && hi >= (fnPage == 0xff01 ? 0x00070065u : 0x00070072u) && !(bits[g.Id] & 7u)) {
                                if (l.ArrayBit != MK_NONE || g.Count > 32) return 0;
                                l.ArrayBit = (unsigned short)bits[g.Id]; l.ArrayCount = (unsigned short)g.Count;
                                l.ArrayMax = (unsigned short)(g.Max < (hi & 0xffffu) ? g.Max : (hi & 0xffffu));
                                l.LogicalMaxOffset = (unsigned short)g.MaxOffset; l.LogicalMaxSize = (unsigned char)g.MaxSize;
                                l.UsageMaxOffset = (unsigned short)hiOffset; l.UsageMaxSize = (unsigned char)hiSize;
                            }
                        }
                    }
                    bits[g.Id] += total;
                }
            } else return 0;
            un = range = 0;
        }
    }
    if (sp || depth || !found || report == 256 || l.ArrayBit == MK_NONE) return 0;
    for (i = 0; i < 8; ++i) if (l.Modifier[i] == MK_NONE) return 0;
    l.ReportId = (unsigned char)report;
    l.Bytes = (unsigned short)((bits[report] + 7u) / 8u + (report ? 1u : 0u));
    if (!l.Bytes || l.Bytes > MK_MAX_REPORT) return 0;
    l.Valid = 1; *out = l; return 1;
}
int MkParseDescriptor(const unsigned char *b, unsigned int n, MK_LAYOUT *out) { return ParseDescriptor(b, n, out, 0xff); }
int MkParseBluetoothDescriptor(const unsigned char *b, unsigned int n, MK_LAYOUT *out) { return ParseDescriptor(b, n, out, 0xff01); }
static int Matches(const MK_LAYOUT *l, const unsigned char *b, unsigned int n) {
    return l && l->Valid && b && n == l->Bytes && (!l->ReportId || b[0] == l->ReportId);
}
static unsigned int Bit(const unsigned char *b, unsigned int bit) { return (b[bit / 8] >> (bit & 7u)) & 1u; }
static void Put(unsigned char *b, unsigned int bit, unsigned int value) {
    unsigned char mask = (unsigned char)(1u << (bit & 7u));
    b[bit / 8] = (unsigned char)((b[bit / 8] & ~mask) | (value ? mask : 0));
}
int MkFnPressed(const MK_LAYOUT *l, const unsigned char *b, unsigned int n) {
    if (!Matches(l, b, n) || l->Fn == MK_NONE) return -1;
    return (int)Bit(b + (l->ReportId ? 1 : 0), l->Fn);
}
int MkTransformState(const MK_LAYOUT *l, const MK_POLICY *p, MK_STATE *state,
                     MK_EFFECTS *effects, unsigned char *b, unsigned int n) {
    static const unsigned char source[6] = { 0, 4, 2, 6, 3, 7 };
    static const unsigned char target[6] = { 0, 4, 2, 6, 3, 7 };
    static const unsigned char comboSource[5] = { 0x2a, 0x50, 0x4f, 0x52, 0x51 };
    static const unsigned char comboTarget[5] = { 0x4c, 0x4a, 0x4d, 0x4b, 0x4e };
    unsigned char scratch[MK_MAX_REPORT], remapped[32]; MK_STATE next; MK_EFFECTS emitted;
    unsigned int down[10], result[8], i, j, f23 = 0, fn, logicalFn = 0, caps = 0, menu = 0;
    unsigned int comboDown = 0, fkeyDown = 0, icons, fnCaps;
    unsigned char *data, *array;
    if (effects) Zero(effects, sizeof(*effects));
    if (!p || !state || !p->Enabled || !Matches(l, b, n) || p->FKeyMode > 2) return 0;
    for (i = 0; i < 10; ++i) if (p->Map[i] > 13) return 0;
    if (!p->FnVerified && (p->Map[6] || p->FnCombinations || p->FKeyMode)) return 0;
    for (i = 0; i < n; ++i) scratch[i] = b[i];
    next = *state; Zero(&emitted, sizeof(emitted));
    Zero(remapped, sizeof(remapped));
    data = scratch + (l->ReportId ? 1 : 0); array = data + l->ArrayBit / 8;
    Zero(down, sizeof(down)); Zero(result, sizeof(result));
    for (i = 0; i < l->ArrayCount; ++i) {
        if (array[i] >= 1 && array[i] <= 3) return 0; /* HID rollover/error: unchanged */
        if (array[i] == 0x39) down[7] = 1;
        for (j = 0; j < 5; ++j) if (array[i] == comboSource[j]) comboDown |= 1u << j;
        if (array[i] >= 0x3a && array[i] <= 0x45) fkeyDown |= 1u << (array[i] - 0x3a);
    }
    for (i = 0; i < 6; ++i) down[i] = Bit(data, l->Modifier[source[i]]);
    fn = l->Fn == MK_NONE ? 0 : Bit(data, l->Fn);
    down[6] = fn; down[8] = Bit(data, l->Modifier[1]); down[9] = Bit(data, l->Modifier[5]);
    for (i = 0; i < 10; ++i) {
        unsigned int action = p->Map[i], active = down[i];
        if (!active) continue;
        if (action == 0) {
            if (i < 6) result[source[i]] = 1;
            else if (i == 6) logicalFn = 1;
            else if (i == 7) caps = 1;
            else result[i == 8 ? 1 : 5] = 1;
        }
        else if (action <= 6) result[target[action - 1]] = 1;
        else if (action == 7) logicalFn = f23 = 1;
        else if (action == 8) { f23 = 1; if (i == 6) logicalFn = 1; }
        else if (action == 10) caps = 1;
        else if (action == 11 || action == 12) result[action == 11 ? 1 : 5] = 1;
        else if (action == 13) menu = 1;
    }
    next.ComboHeld = p->FnCombinations ? (unsigned short)((next.ComboHeld & comboDown) |
        (logicalFn ? comboDown & ~next.ComboDown : 0)) : 0;
    next.ComboDown = (unsigned short)comboDown;
    icons = p->FKeyMode && (p->MediaReady || p->NativeReady) ? next.IconHeld & fkeyDown : 0;
    if (p->FKeyMode && (p->MediaReady || p->NativeReady) && (p->FKeyMode == 1 ? logicalFn : !logicalFn))
        icons |= fkeyDown & ~next.FKeyDown;
    if (!p->MediaReady) icons &= p->NativeReady ? 3u : 0u;
    for (i = 0; i < 12; ++i) {
        unsigned int mask = 1u << i;
        if ((next.IconHeld & mask) && !(icons & mask)) {
            emitted.Events[emitted.Count].Action = (unsigned char)(i + 1);
            emitted.Events[emitted.Count++].Down = 0;
        }
        if (!(next.IconHeld & mask) && (icons & mask)) {
            emitted.Events[emitted.Count].Action = (unsigned char)(i + 1);
            emitted.Events[emitted.Count++].Down = 1;
        }
    }
    next.IconHeld = (unsigned short)icons; next.FKeyDown = (unsigned short)fkeyDown;
    for (i = 0; i < l->ArrayCount; ++i) {
        if (array[i] == 0x39 && p->Map[7] != 0) array[i] = 0;
        for (j = 0; j < 5; ++j) if (array[i] == comboSource[j] && (next.ComboHeld & (1u << j))) {
            array[i] = comboTarget[j]; remapped[i] = 1; break;
        }
        if (array[i] >= 0x3a && array[i] <= 0x45 && (icons & (1u << (array[i] - 0x3a)))) array[i] = 0;
        if (p->SwapExchanged) {
            if (array[i] == 0x35) array[i] = 0x64;
            else if (array[i] == 0x64) array[i] = 0x35;
        }
    }
    /* Canonicalize collisions caused by configured mappings, preserving all
       unconfigured keys. Multiple owners therefore release their output only
       when the last owner releases. */
    for (i = 0; i < l->ArrayCount; ++i) if (array[i] && remapped[i]) {
        for (j = 0; j < l->ArrayCount; ++j) if (j != i && array[j] == array[i] && (!remapped[j] || j < i)) { array[i] = 0; break; }
    }
    for (j = 0; j < 3; ++j) {
        unsigned int needed = j == 0 ? f23 : j == 1 ? caps : menu;
        unsigned char key = (unsigned char)(j == 0 ? 0x72 : j == 1 ? 0x39 : 0x65);
        unsigned int present = 0, slot = 32;
        if (!needed) continue;
        if (key > l->ArrayMax) return 0;
        for (i = 0; i < l->ArrayCount; ++i) { if (array[i] == key) present = 1; if (!array[i] && slot == 32) slot = i; }
        if (!present) { if (slot == 32) return 0; array[slot] = key; }
    }
    fnCaps = p->FnVerified && l->Fn != MK_NONE && p->Map[6] != 0;
    for (i = 0; i < 8; ++i) Put(data, l->Modifier[i], result[i]);
    if (fnCaps) Put(data, l->Fn, 0);
    for (i = 0; i < n; ++i) b[i] = scratch[i];
    *state = next; if (effects) *effects = emitted;
    return 1;
}
int MkTransform(const MK_LAYOUT *l, const MK_POLICY *p, unsigned char *b, unsigned int n) {
    MK_STATE state; Zero(&state, sizeof(state));
    return MkTransformState(l, p, &state, 0, b, n);
}
