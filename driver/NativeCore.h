/* SPDX-License-Identifier: MIT. Bounded HID/SDP configuration transforms. */
#pragma once
#include "ReportCore.h"
#define MK_NATIVE_EXTRA 27u
#define MK_SDP_MAX 8192u
typedef struct MK_NATIVE_RESULT {
    MK_LAYOUT Layout;
    unsigned int Length, DescriptorOffset, DescriptorLength;
    unsigned char ReportId;
} MK_NATIVE_RESULT;
int MkNativeDescriptor(const unsigned char *input, unsigned int length, unsigned char *output,
    unsigned int capacity, int bluetooth, int expand, int brightness, MK_NATIVE_RESULT *result);
int MkSdpDescriptor(const unsigned char *input, unsigned int length, unsigned char *output,
    unsigned int capacity, int expand, int brightness, MK_NATIVE_RESULT *result);
