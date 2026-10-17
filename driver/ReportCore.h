/* SPDX-License-Identifier: MIT. Shared with the host-side adversarial tests. */
#pragma once
#define MK_MAX_REPORT 65u
#define MK_MAX_DESCRIPTOR 4096u
#define MK_NONE 0xffffu
typedef struct MK_LAYOUT {
    unsigned short Modifier[8], Fn, ArrayBit, ArrayCount, Bytes;
    unsigned short ArrayMax, LogicalMaxOffset, UsageMaxOffset;
    unsigned char LogicalMaxSize, UsageMaxSize;
    unsigned char ReportId, Valid;
} MK_LAYOUT;
typedef struct MK_POLICY {
    unsigned char Map[10]; /* Original seven, Caps Lock, left Shift, right Shift. */
    unsigned char Enabled, FnVerified, FnCombinations, SwapExchanged, FKeyMode, MediaReady, NativeReady;
} MK_POLICY;
typedef struct MK_STATE {
    unsigned short ComboDown, ComboHeld, FKeyDown, IconHeld;
} MK_STATE;
typedef struct MK_EFFECTS {
    unsigned char Count;
    struct { unsigned char Action, Down; } Events[24]; /* F1..F12 actions only. */
} MK_EFFECTS;
int MkParseDescriptor(const unsigned char *bytes, unsigned int length, MK_LAYOUT *out);
int MkParseBluetoothDescriptor(const unsigned char *bytes, unsigned int length, MK_LAYOUT *out);
int MkTransform(const MK_LAYOUT *layout, const MK_POLICY *policy,
                unsigned char *report, unsigned int length);
int MkFnPressed(const MK_LAYOUT *layout, const unsigned char *report, unsigned int length);
int MkTransformState(const MK_LAYOUT *layout, const MK_POLICY *policy,
                     MK_STATE *state, MK_EFFECTS *effects,
                     unsigned char *report, unsigned int length);
