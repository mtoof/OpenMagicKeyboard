/* SPDX-License-Identifier: MIT. Fixed METHOD_BUFFERED ABI; no pointers. */
#pragma once
#define MK_VERSION 2u
#define MK_FLAG_MAP 1u
#define MK_FLAG_DIAGNOSTIC 2u
#define MK_FLAG_FN_VERIFIED 4u
#define MK_FLAG_FN_COMBINATIONS 8u
#define MK_FLAG_SWAP_EXCHANGED 16u
/* Status capability bits: 1|2|4|8 base controls, 16 native brightness,
   32 Fn accepted from the parsed descriptor field (no diagnostic taps). */
#define MK_CAP_NATIVE_BRIGHTNESS 16u
#define MK_CAP_DESCRIPTOR_FN 32u
#define MK_IOCTL_MEDIA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_READ_DATA)
#define MK_IOCTL_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define MK_IOCTL_CONFIG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)
#define MK_IOCTL_DESCRIPTOR CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)
typedef struct MK_DESCRIPTOR {
    ULONG Size, Version, Length;
    UCHAR Bytes[4096];
} MK_DESCRIPTOR;
C_ASSERT(sizeof(MK_DESCRIPTOR) == 4108);
typedef struct MK_CONFIG {
    ULONG Size, Version, Flags;
    UCHAR Map[10], FKeyMode, Reserved;
} MK_CONFIG;
typedef struct MK_STATUS {
    ULONG Size, Version, LayoutValid, ReportId, ReportBytes, FnAvailable;
    ULONG FnDown, FnUp;
    ULONG Capabilities, MediaReady;
    MK_CONFIG Active, Pending;
    WCHAR InstanceId[256];
} MK_STATUS;
C_ASSERT(sizeof(MK_CONFIG) == 24);
C_ASSERT(sizeof(MK_STATUS) == 600);
typedef struct MK_MEDIA_EVENT { ULONG Sequence; UCHAR Action, Down, Reserved[2]; } MK_MEDIA_EVENT;
typedef struct MK_MEDIA_BATCH { ULONG Size, Version, Generation, Count; MK_MEDIA_EVENT Events[64]; } MK_MEDIA_BATCH;
C_ASSERT(sizeof(MK_MEDIA_BATCH) == 528);
