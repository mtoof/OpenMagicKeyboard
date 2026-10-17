/* SPDX-License-Identifier: MIT. Original USB-interface lower filter. */
#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbioctl.h>
#include <usbdlib.h>
#include <bthdef.h>
#include <bthioctl.h>
#include <bthddi.h>
#include <initguid.h>
#include <devpkey.h>
#include <wdmsec.h>
#include <ntstrsafe.h>
#include "Protocol.h"
#include "ReportCore.h"
#include "NativeCore.h"

typedef struct DEVICE_CONTEXT {
    WDFSPINLOCK Lock;
    MK_LAYOUT Layout;
    MK_STATE TransformState;
    MK_MEDIA_EVENT MediaEvents[64];
    ULONG MediaCount, MediaSequence;
    ULONGLONG LastMediaPoll;
    MK_CONFIG Active, Pending;
    ULONGLONG LastInput; /* interrupt time of the last matching keyboard report */
    WCHAR InstanceId[256];
    USBD_PIPE_HANDLE InputPipe;
    ULONG FnDown, FnUp;
    ULONG Generation;
    ULONG SeenIoctl[16], SeenUrb[16], SeenIoctlCount, SeenUrbCount;
    ULONG DescriptorLength;
    UCHAR DescriptorBytes[MK_MAX_DESCRIPTOR]; /* static configuration, never input */
    BOOLEAN Ready;
    BOOLEAN FnLast, FnSeen, InCollection;
    BOOLEAN Bluetooth;
    BTH_ADDR BtAddress;
    L2CAP_CHANNEL_HANDLE BtInput;
    UCHAR NativeId, NativeHeld, NativeQueue[64];
    ULONG NativeCount;
    UCHAR NativeWorkspace[MK_MAX_DESCRIPTOR];
} DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceContext)
typedef struct REQUEST_CONTEXT {
    PURB Urb;
    ULONG Capacity;
    USHORT Function;
    USHORT Length;
    ULONG Generation;
    BOOLEAN Descriptor;
    PVOID DescriptorBuffer;
    PMDL DescriptorMdl;
    UCHAR DescriptorType;
    PBRB Brb;
    PVOID BtBuffer;
    PMDL BtMdl;
    L2CAP_CHANNEL_HANDLE BtChannel;
    BOOLEAN Sdp;
    WDFMEMORY Scratch;
} REQUEST_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, RequestContext)
static WDFCOLLECTION Devices;
static WDFWAITLOCK DevicesLock;
static WDFDEVICE Control;
static REGHANDLE TraceHandle;
static const GUID TraceProvider = { 0x7c3d46b1, 0xb7d7, 0x4987, {0x93,0xd4,0x1b,0x8e,0xac,0x2b,0x11,0x31} };
EVT_WDF_DRIVER_UNLOAD DriverUnload;
EVT_WDF_DEVICE_PREPARE_HARDWARE PrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE ReleaseHardware;
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD DeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP DeviceCleanup;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL InternalIoctl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE Completion;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ControlIoctl;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DeviceIoctl;
/* Fixed scalar diagnostic events. Never a report, usage array, or modifier state. */
static VOID Trace(USHORT id, ULONG value) {
    EVENT_DESCRIPTOR event = {0}; EVENT_DATA_DESCRIPTOR data;
    event.Id = id; event.Version = 1; event.Level = 4; event.Keyword = 1;
    if (!TraceHandle || !EtwEventEnabled(TraceHandle, &event)) return;
    EventDataDescCreate(&data, &value, sizeof(value));
    (void)EtwWrite(TraceHandle, &event, NULL, 1, &data);
}

/* Explicit ETW sessions can observe each request kind once per start. This
   records protocol identifiers/structure sizes, never transfer or key data. */
static BOOLEAN NoteTransport(DEVICE_CONTEXT *ctx, ULONG value, BOOLEAN urb) {
    EVENT_DESCRIPTOR event = {0}; ULONG i, *seen, *count; BOOLEAN fresh = FALSE;
    event.Id = urb ? 7 : 6; event.Version = 1; event.Level = 4; event.Keyword = 1;
    if (!TraceHandle || !EtwEventEnabled(TraceHandle, &event)) return FALSE;
    WdfSpinLockAcquire(ctx->Lock);
    seen = urb ? ctx->SeenUrb : ctx->SeenIoctl;
    count = urb ? &ctx->SeenUrbCount : &ctx->SeenIoctlCount;
    for (i = 0; i < *count; ++i) if (seen[i] == value) break;
    if (i == *count && *count < 16) { seen[(*count)++] = value; fresh = TRUE; }
    WdfSpinLockRelease(ctx->Lock);
    if (fresh) Trace(event.Id, value);
    return fresh;
}

static BOOLEAN ConfigValid(const MK_CONFIG *c) {
    ULONG i;
    if (c->Size != sizeof(*c) || c->Version != MK_VERSION || (c->Flags & ~31u) || c->Reserved || c->FKeyMode > 2) return FALSE;
    for (i = 0; i < 10; ++i) {
        if (c->Map[i] > 13) return FALSE;
    }
    if ((c->Flags & MK_FLAG_MAP) && c->Map[6] != 0 && !(c->Flags & MK_FLAG_FN_VERIFIED)) return FALSE;
    if (((c->Flags & MK_FLAG_FN_COMBINATIONS) || c->FKeyMode) && !(c->Flags & MK_FLAG_FN_VERIFIED)) return FALSE;
    return TRUE;
}
static NTSTATUS ConfigRegistry(WDFDEVICE device, MK_CONFIG *c, BOOLEAN write) {
    WDFKEY key; NTSTATUS status; ULONG bytes = 0, type = 0;
    DECLARE_CONST_UNICODE_STRING(value, L"OpenMagicKeyboardPolicyV1");
    status = WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE, write ? KEY_SET_VALUE : KEY_QUERY_VALUE,
                                      WDF_NO_OBJECT_ATTRIBUTES, &key);
    if (!NT_SUCCESS(status)) return status;
    if (write) status = WdfRegistryAssignValue(key, &value, REG_BINARY, sizeof(*c), c);
    else {
        status = WdfRegistryQueryValue(key, &value, sizeof(*c), c, &bytes, &type);
        if (NT_SUCCESS(status) && type == REG_BINARY && bytes == 20 && c->Size == 20 && c->Version == 1) {
            ULONG i; BOOLEAN valid = !(c->Flags & ~7u) && !c->Map[7];
            for (i = 0; i < 7; ++i) if (c->Map[i] > 9) valid = FALSE;
            if (valid) { c->Size = sizeof(*c); c->Version = MK_VERSION; c->Map[7] = c->Map[8] = c->Map[9] = c->FKeyMode = c->Reserved = 0; bytes = sizeof(*c); }
        }
        if (NT_SUCCESS(status) && (bytes != sizeof(*c) || type != REG_BINARY || !ConfigValid(c))) status = STATUS_DATA_ERROR;
    }
    WdfRegistryClose(key); return status;
}
/* USB clients own resident transfer buffers. Never substitute or retain them. */
static PUCHAR TransferBuffer(PVOID buffer, PMDL mdl, ULONG length, ULONG capacity) {
    if (!length || length > capacity || length > MK_MAX_DESCRIPTOR) return NULL;
    if (buffer && mdl) return NULL;
    if (mdl) {
        if (length > MmGetMdlByteCount(mdl)) return NULL;
        return MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
    }
    return buffer;
}
static BOOLEAN SetupIsDescriptor(const UCHAR setup[8]) {
    return setup[0] == 0x81 && setup[1] == 6 && (setup[3] == 0x22 || setup[3] == 0x21) &&
           setup[4] == 1 && setup[5] == 0;
}
static BOOLEAN WantNative(DEVICE_CONTEXT *ctx) {
    return (ctx->Active.Flags & (MK_FLAG_MAP | MK_FLAG_FN_VERIFIED)) == (MK_FLAG_MAP | MK_FLAG_FN_VERIFIED) && ctx->Active.FKeyMode != 0;
}
static BOOLEAN WantExpand(DEVICE_CONTEXT *ctx) {
    ULONG i;
    if (!(ctx->Active.Flags & MK_FLAG_MAP)) return FALSE;
    for (i = 0; i < 10; ++i) if (ctx->Active.Map[i] == 7 || ctx->Active.Map[i] == 8) return TRUE;
    return FALSE;
}
/* Called under the per-device lock. Only finite brightness transitions are queued. */
static VOID TransformPacket(DEVICE_CONTEXT *ctx, PUCHAR bytes, ULONG length) {
    MK_POLICY policy; MK_EFFECTS effects; int fn; ULONG i; UCHAR previous;
    ctx->LastInput = KeQueryInterruptTime();
    fn = MkFnPressed(&ctx->Layout, bytes, length);
    if ((ctx->Active.Flags & MK_FLAG_DIAGNOSTIC) && fn >= 0) {
        if ((!ctx->FnSeen || ctx->FnLast != (BOOLEAN)fn) && fn) { ++ctx->FnDown; Trace(3, 1); }
        else if (ctx->FnSeen && ctx->FnLast && !fn) { ++ctx->FnUp; Trace(3, 0); }
        ctx->FnLast = (BOOLEAN)fn; ctx->FnSeen = TRUE;
    }
    RtlZeroMemory(&policy, sizeof(policy)); RtlCopyMemory(policy.Map, ctx->Active.Map, sizeof(policy.Map));
    policy.Enabled = (UCHAR)((ctx->Active.Flags & MK_FLAG_MAP) != 0);
    policy.FnVerified = (UCHAR)((ctx->Active.Flags & MK_FLAG_FN_VERIFIED) != 0);
    policy.FnCombinations = (UCHAR)((ctx->Active.Flags & MK_FLAG_FN_COMBINATIONS) != 0);
    policy.SwapExchanged = (UCHAR)((ctx->Active.Flags & MK_FLAG_SWAP_EXCHANGED) != 0);
    policy.FKeyMode = ctx->Active.FKeyMode;
    policy.MediaReady = (UCHAR)(ctx->LastMediaPoll && KeQueryInterruptTime() - ctx->LastMediaPoll < 20000000ull && ctx->MediaCount <= 40);
    policy.NativeReady = (UCHAR)(ctx->NativeId && ctx->NativeCount <= 40);
    if (!MkTransformState(&ctx->Layout, &policy, &ctx->TransformState, &effects, bytes, length)) return;
    previous = ctx->NativeHeld;
    for (i = 0; i < effects.Count; ++i) {
        if (ctx->NativeId && effects.Events[i].Action <= 2) {
            UCHAR bit = (UCHAR)(1u << (effects.Events[i].Action - 1));
            if (effects.Events[i].Down) ctx->NativeHeld |= bit; else ctx->NativeHeld &= (UCHAR)~bit;
        }
        if (ctx->MediaCount < 64) {
            MK_MEDIA_EVENT *event = &ctx->MediaEvents[ctx->MediaCount++];
            RtlZeroMemory(event, sizeof(*event)); event->Sequence = ++ctx->MediaSequence;
            event->Action = effects.Events[i].Action; event->Down = effects.Events[i].Down;
        }
    }
    if (ctx->NativeId && ((previous & 2) ? 0x6f : (previous & 1) ? 0x70 : 0) !=
        ((ctx->NativeHeld & 2) ? 0x6f : (ctx->NativeHeld & 1) ? 0x70 : 0) && ctx->NativeCount < 64)
        ctx->NativeQueue[ctx->NativeCount++] = (ctx->NativeHeld & 2) ? 0x6f : (ctx->NativeHeld & 1) ? 0x70 : 0;
}
static BOOLEAN NativePacket(DEVICE_CONTEXT *ctx, PUCHAR bytes, ULONG capacity, PULONG length) {
    ULONG header = ctx->Bluetooth ? 1u : 0u;
    if (!ctx->Ready || !ctx->NativeId || !ctx->NativeCount || capacity < 3 + header) return FALSE;
    if (header) bytes[0] = 0xa1;
    bytes[header] = ctx->NativeId; bytes[header + 1] = ctx->NativeQueue[0]; bytes[header + 2] = 0;
    --ctx->NativeCount; RtlMoveMemory(ctx->NativeQueue, ctx->NativeQueue + 1, ctx->NativeCount);
    *length = 3 + header; return TRUE;
}
static VOID SelectPipe(DEVICE_CONTEXT *ctx, PUSBD_INTERFACE_INFORMATION iface, ULONG available) {
    ULONG i, minimum = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);
    if (available < minimum || iface->Length < minimum || iface->Length > available || iface->InterfaceNumber != 1 ||
        iface->NumberOfPipes > (iface->Length - minimum) / sizeof(USBD_PIPE_INFORMATION)) return;
    for (i = 0; i < iface->NumberOfPipes; ++i) {
        PUSBD_PIPE_INFORMATION pipe = &iface->Pipes[i];
        if (pipe->PipeType == UsbdPipeTypeInterrupt && (pipe->EndpointAddress & 0x80)) {
            ctx->InputPipe = pipe->PipeHandle; Trace(18, 1); return;
        }
    }
}
_Use_decl_annotations_
VOID Completion(WDFREQUEST request, WDFIOTARGET target, PWDF_REQUEST_COMPLETION_PARAMS params, WDFCONTEXT context) {
    WDFDEVICE device = (WDFDEVICE)context;
    DEVICE_CONTEXT *ctx = DeviceContext(device);
    REQUEST_CONTEXT *rc = RequestContext(request);
    PURB u = rc->Urb; PUCHAR bytes = NULL; ULONG length = 0;
    UNREFERENCED_PARAMETER(target);
    if (rc->Sdp && NT_SUCCESS(params->IoStatus.Status)) {
        PBTH_SDP_STREAM_RESPONSE response; size_t actual = 0;
        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(request, 8, (PVOID *)&response, &actual)) && actual >= 8 &&
            params->IoStatus.Information >= 8 && params->IoStatus.Information <= rc->Capacity &&
            response->responseSize <= rc->Capacity - 8 && response->responseSize <= params->IoStatus.Information - 8) {
            BOOLEAN expand, native; MK_NATIVE_RESULT result;
            WdfSpinLockAcquire(ctx->Lock);
            if (!ctx->Ready || ctx->Generation != rc->Generation) { WdfSpinLockRelease(ctx->Lock); goto Finished; }
            expand = WantExpand(ctx); native = WantNative(ctx); WdfSpinLockRelease(ctx->Lock);
            Trace(20, rc->Capacity); Trace(21, (ULONG)params->IoStatus.Information);
            Trace(22, response->requiredSize); Trace(23, response->responseSize);
            Trace(25, (expand ? 1u : 0u) | (native ? 2u : 0u));
            /* Partial replies keep the lower driver's size contract intact.
               HidBth need not retry a complete query when a filter grows it. */
            if (response->responseSize == response->requiredSize && rc->Scratch) {
                PUCHAR scratch = WdfMemoryGetBuffer(rc->Scratch, NULL);
                if (MkSdpDescriptor(response->response, response->responseSize, scratch, MK_SDP_MAX, expand, native, &result)) {
                    Trace(24, result.Length);
                    if (result.Length > rc->Capacity - 8) {
                        /* Fail open with the original descriptor and layout;
                           never advertise unavailable native report fields. */
                        if (!MkSdpDescriptor(response->response, response->responseSize, scratch, MK_SDP_MAX, 0, 0, &result)) goto Finished;
                        Trace(27, 1);
                    }
                    RtlCopyMemory(response->response, scratch, result.Length);
                    response->requiredSize = response->responseSize = result.Length;
                    params->IoStatus.Information = 8 + result.Length;
                    WdfSpinLockAcquire(ctx->Lock);
                    if (ctx->Ready && ctx->Generation == rc->Generation) {
                        ctx->Layout = result.Layout; ctx->NativeId = result.ReportId;
                        ctx->DescriptorLength = result.DescriptorLength;
                        RtlCopyMemory(ctx->DescriptorBytes, scratch + result.DescriptorOffset, result.DescriptorLength);
                    }
                    WdfSpinLockRelease(ctx->Lock); Trace(2, result.Layout.Valid); Trace(26, result.ReportId);
                }
            }
        }
    } else if (rc->Brb && rc->Function == BRB_L2CA_CLOSE_CHANNEL && NT_SUCCESS(params->IoStatus.Status) && NT_SUCCESS(rc->Brb->BrbHeader.Status) &&
        rc->Brb->BrbHeader.Length == rc->Length && rc->Brb->BrbHeader.Type == BRB_L2CA_CLOSE_CHANNEL) {
        WdfSpinLockAcquire(ctx->Lock);
        if (ctx->Generation == rc->Generation && ctx->BtInput == rc->Brb->BrbL2caCloseChannel.ChannelHandle) {
            ctx->BtInput = NULL; ctx->NativeHeld = 0; ctx->NativeCount = 0; ctx->LastInput = 0;
            RtlZeroMemory(&ctx->TransformState, sizeof(ctx->TransformState)); ctx->FnSeen = ctx->FnLast = FALSE;
        }
        WdfSpinLockRelease(ctx->Lock);
    } else if (rc->Brb && NT_SUCCESS(params->IoStatus.Status) && NT_SUCCESS(rc->Brb->BrbHeader.Status) &&
        rc->Brb->BrbHeader.Length == rc->Length && rc->Brb->BrbHeader.Type == BRB_L2CA_ACL_TRANSFER &&
        rc->Brb->BrbL2caAclTransfer.ChannelHandle == rc->BtChannel &&
        rc->Brb->BrbL2caAclTransfer.Buffer == rc->BtBuffer && rc->Brb->BrbL2caAclTransfer.BufferMDL == rc->BtMdl &&
        (rc->Brb->BrbL2caAclTransfer.TransferFlags & ACL_TRANSFER_DIRECTION_IN)) {
        length = rc->Brb->BrbL2caAclTransfer.BufferSize;
        bytes = TransferBuffer(rc->BtBuffer, rc->BtMdl, length, rc->Capacity);
        if (bytes && length >= 2 && bytes[0] == 0xa1) {
            WdfSpinLockAcquire(ctx->Lock);
            if (ctx->Ready && ctx->Generation == rc->Generation && ctx->Layout.Valid && length - 1 == ctx->Layout.Bytes && bytes[1] == ctx->Layout.ReportId) {
                ctx->BtInput = rc->BtChannel; TransformPacket(ctx, bytes + 1, length - 1);
            }
            WdfSpinLockRelease(ctx->Lock);
        }
    }
    if (u && (rc->Function == URB_FUNCTION_SELECT_CONFIGURATION || rc->Function == URB_FUNCTION_SELECT_INTERFACE))
        Trace(19, ((ULONG)u->UrbHeader.Length << 16) | u->UrbHeader.Function);
    if (u && rc->Descriptor) {
        Trace(12, ((ULONG)u->UrbHeader.Length << 16) | u->UrbHeader.Function);
        Trace(13, (ULONG)params->IoStatus.Status); Trace(14, (ULONG)u->UrbHeader.Status);
        if (u->UrbHeader.Length >= sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST)) {
            Trace(15, u->UrbControlDescriptorRequest.TransferBufferLength);
            Trace(16, (u->UrbControlDescriptorRequest.TransferBuffer ? 1u : 0u) |
                      (u->UrbControlDescriptorRequest.TransferBufferMDL ? 2u : 0u));
        }
    }
    /* usbccgp rewrites a successful descriptor URB (0x28) into a standard
       control transfer (0x08). Accept only known returned structures and retain
       the original requested capacity/buffer identity. Selection URBs may also
       be normalized from select-configuration into select-interface. */
    if (u && NT_SUCCESS(params->IoStatus.Status) && USBD_SUCCESS(u->UrbHeader.Status) &&
        u->UrbHeader.Length <= rc->Length &&
        ((u->UrbHeader.Function == rc->Function && u->UrbHeader.Length == rc->Length) ||
         (rc->Descriptor &&
          ((u->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER && u->UrbHeader.Length == sizeof(struct _URB_CONTROL_TRANSFER)) ||
           (u->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER_EX && u->UrbHeader.Length == sizeof(struct _URB_CONTROL_TRANSFER_EX)))) ||
         (rc->Function == URB_FUNCTION_SELECT_CONFIGURATION && u->UrbHeader.Function == URB_FUNCTION_SELECT_INTERFACE &&
          u->UrbHeader.Length >= FIELD_OFFSET(struct _URB_SELECT_INTERFACE, Interface) + FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes)))) {
        if (rc->Descriptor) {
            PVOID returnedBuffer = NULL; PMDL returnedMdl = NULL;
            if (u->UrbHeader.Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE) {
                length = u->UrbControlDescriptorRequest.TransferBufferLength;
                returnedBuffer = u->UrbControlDescriptorRequest.TransferBuffer; returnedMdl = u->UrbControlDescriptorRequest.TransferBufferMDL;
            } else if (u->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER) {
                length = u->UrbControlTransfer.TransferBufferLength;
                returnedBuffer = u->UrbControlTransfer.TransferBuffer; returnedMdl = u->UrbControlTransfer.TransferBufferMDL;
            } else if (u->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER_EX) {
                length = u->UrbControlTransferEx.TransferBufferLength;
                returnedBuffer = u->UrbControlTransferEx.TransferBuffer; returnedMdl = u->UrbControlTransferEx.TransferBufferMDL;
            }
            if (returnedBuffer == rc->DescriptorBuffer && returnedMdl == rc->DescriptorMdl)
                bytes = TransferBuffer(returnedBuffer, returnedMdl, length, rc->Capacity);
            if (bytes) {
                MK_LAYOUT layout; MK_NATIVE_RESULT result; BOOLEAN native;
                WdfSpinLockAcquire(ctx->Lock); native = ctx->Ready && ctx->Generation == rc->Generation && WantNative(ctx); WdfSpinLockRelease(ctx->Lock);
                if (rc->DescriptorType == 0x21) {
                    if (native && length >= 9 && bytes[0] == 9 && bytes[1] == 0x21 && bytes[5] == 1 && bytes[6] == 0x22) {
                        ULONG original = bytes[7] | ((ULONG)bytes[8] << 8);
                        if (original && original <= MK_MAX_DESCRIPTOR - MK_NATIVE_EXTRA) {
                            original += MK_NATIVE_EXTRA; bytes[7] = (UCHAR)original; bytes[8] = (UCHAR)(original >> 8);
                        }
                    }
                    goto Finished;
                }
                WdfSpinLockAcquire(ctx->Lock);
                if (native && MkNativeDescriptor(bytes, length, ctx->NativeWorkspace,
                    MK_MAX_DESCRIPTOR, 0, 0, 1, &result) && result.Length <= rc->Capacity) {
                    RtlCopyMemory(bytes, ctx->NativeWorkspace, result.Length); length = result.Length;
                    if (u->UrbHeader.Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE) u->UrbControlDescriptorRequest.TransferBufferLength = length;
                    else if (u->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER) u->UrbControlTransfer.TransferBufferLength = length;
                    else u->UrbControlTransferEx.TransferBufferLength = length;
                } else { RtlZeroMemory(&result, sizeof(result)); }
                (void)MkParseDescriptor(bytes, length, &layout);
                if (ctx->Ready && ctx->Generation == rc->Generation) {
                    ctx->Layout = layout; ctx->DescriptorLength = length;
                    ctx->NativeId = result.ReportId;
                    RtlCopyMemory(ctx->DescriptorBytes, bytes, length);
                }
                WdfSpinLockRelease(ctx->Lock);
                Trace(2, layout.Valid);
            }
        } else if (u->UrbHeader.Function == URB_FUNCTION_SELECT_INTERFACE) {
            WdfSpinLockAcquire(ctx->Lock);
            if (ctx->Ready && ctx->Generation == rc->Generation)
                SelectPipe(ctx, &u->UrbSelectInterface.Interface, u->UrbHeader.Length - FIELD_OFFSET(struct _URB_SELECT_INTERFACE, Interface));
            WdfSpinLockRelease(ctx->Lock);
        } else if (u->UrbHeader.Function == URB_FUNCTION_SELECT_CONFIGURATION) {
            ULONG offset = FIELD_OFFSET(struct _URB_SELECT_CONFIGURATION, Interface);
            WdfSpinLockAcquire(ctx->Lock);
            if (ctx->Ready && ctx->Generation == rc->Generation) ctx->InputPipe = NULL;
            while (ctx->Ready && ctx->Generation == rc->Generation && offset < (ULONG)u->UrbHeader.Length) {
                PUSBD_INTERFACE_INFORMATION iface = (PUSBD_INTERFACE_INFORMATION)((PUCHAR)u + offset);
                ULONG remain = u->UrbHeader.Length - offset;
                if (remain < (ULONG)FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes) || !iface->Length || iface->Length > remain) break;
                SelectPipe(ctx, iface, remain); offset += iface->Length;
            }
            WdfSpinLockRelease(ctx->Lock);
        } else if (rc->Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER &&
                   (u->UrbBulkOrInterruptTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)) {
            length = u->UrbBulkOrInterruptTransfer.TransferBufferLength;
            if (length <= MK_MAX_REPORT) bytes = TransferBuffer(u->UrbBulkOrInterruptTransfer.TransferBuffer,
                u->UrbBulkOrInterruptTransfer.TransferBufferMDL, length, rc->Capacity);
            if (bytes) {
                WdfSpinLockAcquire(ctx->Lock);
                if (ctx->Ready && ctx->Generation == rc->Generation && ctx->InputPipe && ctx->InputPipe == u->UrbBulkOrInterruptTransfer.PipeHandle) {
                    TransformPacket(ctx, bytes, length);
                }
                WdfSpinLockRelease(ctx->Lock);
            }
        }
    }
Finished:
    if (rc->Scratch) { WdfObjectDelete(rc->Scratch); rc->Scratch = NULL; }
    WdfRequestCompleteWithInformation(request, params->IoStatus.Status, params->IoStatus.Information);
}
_Use_decl_annotations_
VOID InternalIoctl(WDFQUEUE queue, WDFREQUEST request, size_t outLength, size_t inLength, ULONG code) {
    WDFDEVICE device = WdfIoQueueGetDevice(queue);
    DEVICE_CONTEXT *ctx = DeviceContext(device);
    REQUEST_CONTEXT *rc = RequestContext(request);
    PURB u = NULL; BOOLEAN observe = FALSE;
    UNREFERENCED_PARAMETER(outLength); UNREFERENCED_PARAMETER(inLength);
    RtlZeroMemory(rc, sizeof(*rc));
    if (NoteTransport(ctx, code, FALSE)) Trace(8, WdfRequestGetRequestorMode(request));
    if (ctx->Bluetooth && code == IOCTL_INTERNAL_BTH_SUBMIT_BRB && WdfRequestGetRequestorMode(request) == KernelMode) {
        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(request));
        PBRB brb = (PBRB)stack->Parameters.Others.Argument1;
        if (brb && brb->BrbHeader.Type == BRB_L2CA_CLOSE_CHANNEL && brb->BrbHeader.Length == sizeof(struct _BRB_L2CA_CLOSE_CHANNEL) &&
            ctx->BtAddress && brb->BrbL2caCloseChannel.BtAddress == ctx->BtAddress) {
            rc->Brb = brb; rc->Function = BRB_L2CA_CLOSE_CHANNEL; rc->Length = (USHORT)brb->BrbHeader.Length; observe = TRUE;
        }
        if (brb && brb->BrbHeader.Type == BRB_L2CA_ACL_TRANSFER && brb->BrbHeader.Length == sizeof(struct _BRB_L2CA_ACL_TRANSFER) &&
            ctx->BtAddress && brb->BrbL2caAclTransfer.BtAddress == ctx->BtAddress &&
            (brb->BrbL2caAclTransfer.TransferFlags & ACL_TRANSFER_DIRECTION_IN)) {
            ULONG capacity = brb->BrbL2caAclTransfer.BufferSize, nativeLength = 0; BOOLEAN supplied = FALSE;
            PUCHAR buffer = TransferBuffer(brb->BrbL2caAclTransfer.Buffer, brb->BrbL2caAclTransfer.BufferMDL, capacity, capacity);
            WdfSpinLockAcquire(ctx->Lock);
            if (buffer && ctx->BtInput && ctx->BtInput == brb->BrbL2caAclTransfer.ChannelHandle)
                supplied = NativePacket(ctx, buffer, capacity, &nativeLength);
            WdfSpinLockRelease(ctx->Lock);
            if (supplied) { brb->BrbL2caAclTransfer.BufferSize = nativeLength; brb->BrbHeader.Status = STATUS_SUCCESS; brb->BrbHeader.BtStatus = 0; WdfRequestComplete(request, STATUS_SUCCESS); return; }
            rc->Brb = brb; rc->Length = (USHORT)brb->BrbHeader.Length; rc->Capacity = capacity;
            rc->BtBuffer = brb->BrbL2caAclTransfer.Buffer; rc->BtMdl = brb->BrbL2caAclTransfer.BufferMDL;
            rc->BtChannel = brb->BrbL2caAclTransfer.ChannelHandle; observe = TRUE;
        }
    }
    if (code == IOCTL_INTERNAL_USB_SUBMIT_URB && WdfRequestGetRequestorMode(request) == KernelMode) {
        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(request));
        u = (PURB)stack->Parameters.Others.Argument1;
        if (u && u->UrbHeader.Length >= sizeof(struct _URB_HEADER)) {
            USHORT f = u->UrbHeader.Function;
            (void)NoteTransport(ctx, ((ULONG)u->UrbHeader.Length << 16) | f, TRUE);
            if (f == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE && u->UrbHeader.Length == sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST))
                Trace(9, ((ULONG)u->UrbControlDescriptorRequest.DescriptorType << 24) |
                         ((ULONG)u->UrbControlDescriptorRequest.Index << 16) | u->UrbControlDescriptorRequest.LanguageId);
            if (f == URB_FUNCTION_CONTROL_TRANSFER_EX && u->UrbHeader.Length == sizeof(struct _URB_CONTROL_TRANSFER_EX) &&
                u->UrbControlTransferEx.SetupPacket[1] == 6 && u->UrbControlTransferEx.SetupPacket[3] == 0x22) {
                ULONG first, second;
                RtlCopyMemory(&first, u->UrbControlTransferEx.SetupPacket, sizeof(first));
                RtlCopyMemory(&second, u->UrbControlTransferEx.SetupPacket + 4, sizeof(second));
                Trace(10, first); Trace(11, second);
            }
            if (f == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE && u->UrbHeader.Length == sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST) &&
                (u->UrbControlDescriptorRequest.DescriptorType == 0x22 || u->UrbControlDescriptorRequest.DescriptorType == 0x21)) {
                rc->DescriptorType = u->UrbControlDescriptorRequest.DescriptorType;
                rc->Descriptor = TRUE; rc->Capacity = u->UrbControlDescriptorRequest.TransferBufferLength; observe = TRUE;
            } else if (f == URB_FUNCTION_CONTROL_TRANSFER && u->UrbHeader.Length == sizeof(struct _URB_CONTROL_TRANSFER) && SetupIsDescriptor(u->UrbControlTransfer.SetupPacket)) {
                rc->DescriptorType = u->UrbControlTransfer.SetupPacket[3];
                rc->Descriptor = TRUE; rc->Capacity = u->UrbControlTransfer.TransferBufferLength; observe = TRUE;
            } else if (f == URB_FUNCTION_CONTROL_TRANSFER_EX && u->UrbHeader.Length == sizeof(struct _URB_CONTROL_TRANSFER_EX) && SetupIsDescriptor(u->UrbControlTransferEx.SetupPacket)) {
                rc->DescriptorType = u->UrbControlTransferEx.SetupPacket[3];
                rc->Descriptor = TRUE; rc->Capacity = u->UrbControlTransferEx.TransferBufferLength; observe = TRUE;
            } else if (f == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER && u->UrbHeader.Length == sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)) {
                rc->Capacity = u->UrbBulkOrInterruptTransfer.TransferBufferLength; observe = TRUE;
                if (u->UrbBulkOrInterruptTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN) {
                    ULONG nativeLength = 0; BOOLEAN supplied = FALSE;
                    PUCHAR buffer = TransferBuffer(u->UrbBulkOrInterruptTransfer.TransferBuffer, u->UrbBulkOrInterruptTransfer.TransferBufferMDL, rc->Capacity, rc->Capacity);
                    WdfSpinLockAcquire(ctx->Lock);
                    if (buffer && ctx->InputPipe && ctx->InputPipe == u->UrbBulkOrInterruptTransfer.PipeHandle) supplied = NativePacket(ctx, buffer, rc->Capacity, &nativeLength);
                    WdfSpinLockRelease(ctx->Lock);
                    if (supplied) { u->UrbBulkOrInterruptTransfer.TransferBufferLength = nativeLength; u->UrbHeader.Status = USBD_STATUS_SUCCESS; WdfRequestComplete(request, STATUS_SUCCESS); return; }
                }
            } else if (f == URB_FUNCTION_SELECT_INTERFACE && u->UrbHeader.Length >= FIELD_OFFSET(struct _URB_SELECT_INTERFACE, Interface) + FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes)) observe = TRUE;
            else if (f == URB_FUNCTION_SELECT_CONFIGURATION && u->UrbHeader.Length >= FIELD_OFFSET(struct _URB_SELECT_CONFIGURATION, Interface)) observe = TRUE;
            if (observe) {
                rc->Urb = u; rc->Function = f; rc->Length = u->UrbHeader.Length;
                if (rc->Descriptor) {
                    if (f == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE) {
                        rc->DescriptorBuffer = u->UrbControlDescriptorRequest.TransferBuffer; rc->DescriptorMdl = u->UrbControlDescriptorRequest.TransferBufferMDL;
                    } else if (f == URB_FUNCTION_CONTROL_TRANSFER) {
                        rc->DescriptorBuffer = u->UrbControlTransfer.TransferBuffer; rc->DescriptorMdl = u->UrbControlTransfer.TransferBufferMDL;
                    } else {
                        rc->DescriptorBuffer = u->UrbControlTransferEx.TransferBuffer; rc->DescriptorMdl = u->UrbControlTransferEx.TransferBufferMDL;
                    }
                }
            }
        }
    }
    WdfRequestFormatRequestUsingCurrentType(request);
    if (observe) {
        WdfSpinLockAcquire(ctx->Lock); rc->Generation = ctx->Generation; WdfSpinLockRelease(ctx->Lock);
        WdfRequestSetCompletionRoutine(request, Completion, device);
        if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), WDF_NO_SEND_OPTIONS)) WdfRequestComplete(request, WdfRequestGetStatus(request));
    } else {
        WDF_REQUEST_SEND_OPTIONS options;
        WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
        if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), &options)) WdfRequestComplete(request, WdfRequestGetStatus(request));
    }
}
_Use_decl_annotations_
VOID DeviceIoctl(WDFQUEUE queue, WDFREQUEST request, size_t outLength, size_t inLength, ULONG code) {
    WDFDEVICE device = WdfIoQueueGetDevice(queue); DEVICE_CONTEXT *ctx = DeviceContext(device);
    REQUEST_CONTEXT *rc = RequestContext(request); BOOLEAN observe = FALSE;
    UNREFERENCED_PARAMETER(inLength);
    RtlZeroMemory(rc, sizeof(*rc));
    (void)NoteTransport(ctx, code, FALSE);
    if (ctx->Bluetooth && WdfRequestGetRequestorMode(request) == KernelMode &&
        (code == IOCTL_BTH_SDP_SERVICE_ATTRIBUTE_SEARCH || code == IOCTL_BTH_SDP_ATTRIBUTE_SEARCH) && outLength >= 8 && outLength <= MK_SDP_MAX + 8) {
        WDF_OBJECT_ATTRIBUTES attributes; WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ParentObject = request;
        rc->Sdp = TRUE; rc->Capacity = (ULONG)outLength;
        (void)WdfMemoryCreate(&attributes, NonPagedPoolNx, 'sKMO', MK_SDP_MAX, &rc->Scratch, NULL);
        WdfSpinLockAcquire(ctx->Lock); rc->Generation = ctx->Generation; WdfSpinLockRelease(ctx->Lock); observe = TRUE;
    }
    WdfRequestFormatRequestUsingCurrentType(request);
    if (observe) {
        WdfRequestSetCompletionRoutine(request, Completion, device);
        if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), WDF_NO_SEND_OPTIONS)) { if (rc->Scratch) WdfObjectDelete(rc->Scratch); WdfRequestComplete(request, WdfRequestGetStatus(request)); }
    } else {
        WDF_REQUEST_SEND_OPTIONS options; WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
        if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), &options)) WdfRequestComplete(request, WdfRequestGetStatus(request));
    }
}
/* The USB and Bluetooth devnodes of the same keyboard are usually both present
   (a paired keyboard stays enumerated while cabled). Report on the transport
   that produced the most recent keyboard report instead of refusing service.
   Called with DevicesLock held. */
static WDFDEVICE SelectControlTarget(VOID) {
    WDFDEVICE best = NULL, ready = NULL; ULONGLONG bestTime = 0; ULONG i, count = WdfCollectionGetCount(Devices);
    for (i = 0; i < count; ++i) {
        WDFDEVICE candidate = (WDFDEVICE)WdfCollectionGetItem(Devices, i); DEVICE_CONTEXT *c = DeviceContext(candidate);
        BOOLEAN isReady, valid; ULONGLONG last;
        WdfSpinLockAcquire(c->Lock);
        isReady = c->Ready; valid = c->Ready && c->Layout.Valid; last = c->LastInput;
        WdfSpinLockRelease(c->Lock);
        if (isReady && !ready) ready = candidate;
        if (valid && (!best || last > bestTime)) { best = candidate; bestTime = last; }
    }
    if (best) return best;
    if (ready) return ready;
    return count ? (WDFDEVICE)WdfCollectionGetItem(Devices, 0) : NULL;
}
_Use_decl_annotations_
VOID ControlIoctl(WDFQUEUE queue, WDFREQUEST request, size_t outLength, size_t inLength, ULONG code) {
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST; size_t information = 0;
    WDFDEVICE device = NULL; DEVICE_CONTEXT *ctx; ULONG i;
    UNREFERENCED_PARAMETER(queue);
    WdfWaitLockAcquire(DevicesLock, NULL);
    device = SelectControlTarget();
    if (!device) { status = STATUS_DEVICE_NOT_READY; goto Done; }
    ctx = DeviceContext(device);
    if (code == MK_IOCTL_STATUS && !inLength && outLength == sizeof(MK_STATUS)) {
        MK_STATUS *s;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*s), (PVOID *)&s, NULL);
        if (NT_SUCCESS(status)) {
            RtlZeroMemory(s, sizeof(*s)); s->Size = sizeof(*s); s->Version = MK_VERSION;
            WdfSpinLockAcquire(ctx->Lock);
            s->LayoutValid = ctx->Layout.Valid; s->ReportId = ctx->Layout.ReportId; s->ReportBytes = ctx->Layout.Bytes;
            s->FnAvailable = ctx->Layout.Valid && ctx->Layout.Fn != MK_NONE;
            s->FnDown = ctx->FnDown; s->FnUp = ctx->FnUp; s->Active = ctx->Active; s->Pending = ctx->Pending;
            s->Capabilities = 15 | MK_CAP_DESCRIPTOR_FN | (ctx->NativeId ? MK_CAP_NATIVE_BRIGHTNESS : 0u);
            s->MediaReady = ctx->LastMediaPoll && KeQueryInterruptTime() - ctx->LastMediaPoll < 20000000ull;
            RtlCopyMemory(s->InstanceId, ctx->InstanceId, sizeof(s->InstanceId));
            WdfSpinLockRelease(ctx->Lock); information = sizeof(*s);
        }
    } else if (code == MK_IOCTL_MEDIA && !inLength && outLength == sizeof(MK_MEDIA_BATCH)) {
        MK_MEDIA_BATCH *batch;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*batch), (PVOID *)&batch, NULL);
        if (NT_SUCCESS(status)) {
            RtlZeroMemory(batch, sizeof(*batch)); batch->Size = sizeof(*batch); batch->Version = MK_VERSION;
            WdfSpinLockAcquire(ctx->Lock);
            if (!ctx->Ready) status = STATUS_DEVICE_NOT_READY;
            else {
                batch->Generation = ctx->Generation; batch->Count = ctx->MediaCount;
                RtlCopyMemory(batch->Events, ctx->MediaEvents, ctx->MediaCount * sizeof(MK_MEDIA_EVENT));
                ctx->MediaCount = 0; ctx->LastMediaPoll = KeQueryInterruptTime(); information = sizeof(*batch);
            }
            WdfSpinLockRelease(ctx->Lock);
        }
    } else if (code == MK_IOCTL_DESCRIPTOR && !inLength && outLength == sizeof(MK_DESCRIPTOR)) {
        MK_DESCRIPTOR *d;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(*d), (PVOID *)&d, NULL);
        if (NT_SUCCESS(status)) {
            RtlZeroMemory(d, sizeof(*d)); d->Size = sizeof(*d); d->Version = MK_VERSION;
            WdfSpinLockAcquire(ctx->Lock);
            if (ctx->DescriptorLength <= sizeof(d->Bytes)) {
                d->Length = ctx->DescriptorLength;
                RtlCopyMemory(d->Bytes, ctx->DescriptorBytes, d->Length);
            }
            WdfSpinLockRelease(ctx->Lock); information = sizeof(*d);
        }
    } else if (code == MK_IOCTL_CONFIG && inLength == sizeof(MK_CONFIG) && !outLength) {
        MK_CONFIG *input, config;
        status = WdfRequestRetrieveInputBuffer(request, sizeof(config), (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status)) goto Done;
        config = *input;
        if (!ConfigValid(&config)) { status = STATUS_INVALID_PARAMETER; goto Done; }
        WdfSpinLockAcquire(ctx->Lock);
        /* The driver binds only to this exact Apple model, and the Fn bit is
           taken from its own report descriptor (page 0xFF/0xFF01, usage 3),
           so a descriptor-advertised Fn field is accepted without the old
           three-tap diagnostic. Diagnostic counters remain a valid source. */
        if ((config.Flags & MK_FLAG_FN_VERIFIED) && !(ctx->Active.Flags & MK_FLAG_FN_VERIFIED) && (!ctx->FnDown || !ctx->FnUp) &&
            !(ctx->Layout.Valid && ctx->Layout.Fn != MK_NONE)) status = STATUS_DEVICE_NOT_READY;
        else if ((config.Flags & MK_FLAG_MAP) && (!ctx->Layout.Valid || ((config.Map[6] != 0 || config.FKeyMode || (config.Flags & MK_FLAG_FN_COMBINATIONS)) && ctx->Layout.Fn == MK_NONE))) status = STATUS_NOT_SUPPORTED;
        else status = STATUS_SUCCESS;
        WdfSpinLockRelease(ctx->Lock);
        if (!NT_SUCCESS(status)) goto Done;
        status = ConfigRegistry(device, &config, TRUE);
        if (NT_SUCCESS(status)) { WdfSpinLockAcquire(ctx->Lock); ctx->Pending = config; WdfSpinLockRelease(ctx->Lock); }
        /* One physical keyboard: keep the other transport's policy identical so
           switching between cable and Bluetooth keeps the same mappings. Each
           transport still applies it only when its own layout supports it. */
        for (i = 0; NT_SUCCESS(status) && i < WdfCollectionGetCount(Devices); ++i) {
            WDFDEVICE other = (WDFDEVICE)WdfCollectionGetItem(Devices, i); DEVICE_CONTEXT *otherCtx = DeviceContext(other);
            if (other == device || !NT_SUCCESS(ConfigRegistry(other, &config, TRUE))) continue;
            WdfSpinLockAcquire(otherCtx->Lock); otherCtx->Pending = config; WdfSpinLockRelease(otherCtx->Lock);
        }
    }
Done:
    WdfWaitLockRelease(DevicesLock);
    WdfRequestCompleteWithInformation(request, status, information);
}
static NTSTATUS CreateControl(WDFDRIVER driver) {
    PWDFDEVICE_INIT init; WDFDEVICE device = NULL; NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queue; WDF_OBJECT_ATTRIBUTES attributes;
    DECLARE_CONST_UNICODE_STRING(name, L"\\Device\\OpenMagicKeyboardControl");
    DECLARE_CONST_UNICODE_STRING(link, L"\\DosDevices\\Global\\OpenMagicKeyboardControl");
    DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;IU)");
    if (Control) return STATUS_SUCCESS;
    init = WdfControlDeviceInitAllocate(driver, &sddl);
    if (!init) return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetExclusive(init, FALSE);
    status = WdfDeviceInitAssignName(init, &name);
    if (!NT_SUCCESS(status)) goto Fail;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&init, &attributes, &device);
    if (!NT_SUCCESS(status)) goto Fail;
    status = WdfDeviceCreateSymbolicLink(device, &link);
    if (!NT_SUCCESS(status)) goto Fail;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue, WdfIoQueueDispatchSequential);
    queue.PowerManaged = WdfFalse; queue.EvtIoDeviceControl = ControlIoctl;
    status = WdfIoQueueCreate(device, &queue, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) goto Fail;
    Control = device; WdfControlFinishInitializing(device); return STATUS_SUCCESS;
Fail:
    if (init) WdfDeviceInitFree(init);
    if (device) WdfObjectDelete(device);
    return status;
}
_Use_decl_annotations_
NTSTATUS PrepareHardware(WDFDEVICE device, WDFCMRESLIST raw, WDFCMRESLIST translated) {
    DEVICE_CONTEXT *ctx = DeviceContext(device); MK_CONFIG config;
    UNREFERENCED_PARAMETER(raw); UNREFERENCED_PARAMETER(translated);
    WdfWaitLockAcquire(DevicesLock, NULL);
    RtlZeroMemory(&config, sizeof(config)); config.Size = sizeof(config); config.Version = MK_VERSION;
    if (!NT_SUCCESS(ConfigRegistry(device, &config, FALSE))) { RtlZeroMemory(&config, sizeof(config)); config.Size = sizeof(config); config.Version = MK_VERSION; }
    WdfSpinLockAcquire(ctx->Lock);
    ctx->Active = ctx->Pending = config; RtlZeroMemory(&ctx->Layout, sizeof(ctx->Layout)); ctx->InputPipe = NULL;
    ++ctx->Generation; ctx->Ready = TRUE;
    ctx->SeenIoctlCount = ctx->SeenUrbCount = 0;
    ctx->DescriptorLength = 0;
    ctx->NativeId = ctx->NativeHeld = 0; ctx->NativeCount = 0; ctx->BtInput = NULL;
    RtlZeroMemory(&ctx->TransformState, sizeof(ctx->TransformState));
    ctx->MediaCount = ctx->MediaSequence = 0; ctx->LastMediaPoll = 0;
    ctx->FnSeen = ctx->FnLast = FALSE; ctx->FnDown = ctx->FnUp = 0; ctx->LastInput = 0;
    WdfSpinLockRelease(ctx->Lock); WdfWaitLockRelease(DevicesLock);
    Trace(5, 1); return STATUS_SUCCESS;
}
_Use_decl_annotations_
NTSTATUS ReleaseHardware(WDFDEVICE device, WDFCMRESLIST translated) {
    DEVICE_CONTEXT *ctx = DeviceContext(device);
    UNREFERENCED_PARAMETER(translated);
    WdfSpinLockAcquire(ctx->Lock);
    ++ctx->Generation; ctx->Ready = FALSE;
    ctx->MediaCount = 0; ctx->LastMediaPoll = 0; RtlZeroMemory(&ctx->TransformState, sizeof(ctx->TransformState));
    ctx->DescriptorLength = 0;
    ctx->NativeId = ctx->NativeHeld = 0; ctx->NativeCount = 0; ctx->BtInput = NULL;
    RtlZeroMemory(&ctx->Layout, sizeof(ctx->Layout)); ctx->InputPipe = NULL; ctx->FnSeen = ctx->FnLast = FALSE;
    ctx->LastInput = 0;
    WdfSpinLockRelease(ctx->Lock); Trace(5, 0); return STATUS_SUCCESS;
}
_Use_decl_annotations_
VOID DeviceCleanup(WDFOBJECT object) {
    DEVICE_CONTEXT *ctx = DeviceContext(object);
    WdfWaitLockAcquire(DevicesLock, NULL);
    if (ctx->InCollection) {
        WdfCollectionRemove(Devices, object);
        if (!WdfCollectionGetCount(Devices) && Control) { WdfObjectDelete(Control); Control = NULL; }
    }
    WdfWaitLockRelease(DevicesLock);
    Trace(4, 0);
}
_Use_decl_annotations_
NTSTATUS DeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT init) {
    WCHAR ids[512]; ULONG bytes = 0; NTSTATUS status; BOOLEAN match = FALSE;
    WDFDEVICE device; DEVICE_CONTEXT *ctx; WDF_OBJECT_ATTRIBUTES attributes; WDF_IO_QUEUE_CONFIG queue;
    DEVPROPTYPE type; WDF_DEVICE_PROPERTY_DATA property; MK_CONFIG config;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    ULONG offset; BOOLEAN bluetooth = FALSE;
    status = WdfFdoInitQueryProperty(init, DevicePropertyHardwareID, sizeof(ids), ids, &bytes);
    if (!NT_SUCCESS(status) || bytes < 2 * sizeof(WCHAR) || bytes > sizeof(ids) || bytes % sizeof(WCHAR)) return STATUS_SUCCESS;
    for (offset = 0; offset < bytes / sizeof(WCHAR);) {
        ULONG end = offset;
        while (end < bytes / sizeof(WCHAR) && ids[end]) ++end;
        if (end == bytes / sizeof(WCHAR)) return STATUS_SUCCESS;
        if (!ids[offset]) break;
        if (_wcsicmp(ids + offset, L"USB\\VID_05AC&PID_0320&MI_01") == 0) match = TRUE;
        if (_wcsicmp(ids + offset, L"BTHENUM\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320") == 0) { match = TRUE; bluetooth = TRUE; }
        offset = end + 1;
    }
    if (!match) return STATUS_SUCCESS;
    WdfFdoInitSetFilter(init);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = PrepareHardware; pnp.EvtDeviceReleaseHardware = ReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(init, &pnp);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, REQUEST_CONTEXT);
    WdfDeviceInitSetRequestAttributes(init, &attributes);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = DeviceCleanup;
    status = WdfDeviceCreate(&init, &attributes, &device); if (!NT_SUCCESS(status)) return status;
    ctx = DeviceContext(device);
    ctx->Bluetooth = bluetooth;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes, &ctx->Lock); if (!NT_SUCCESS(status)) return status;
    RtlZeroMemory(&config, sizeof(config)); config.Size = sizeof(config); config.Version = MK_VERSION;
    if (!NT_SUCCESS(ConfigRegistry(device, &config, FALSE))) { RtlZeroMemory(&config, sizeof(config)); config.Size = sizeof(config); config.Version = MK_VERSION; }
    ctx->Active = ctx->Pending = config;
    WDF_DEVICE_PROPERTY_DATA_INIT(&property, &DEVPKEY_Device_InstanceId);
    status = WdfDeviceQueryPropertyEx(device, &property, sizeof(ctx->InstanceId), ctx->InstanceId, &bytes, &type);
    if (!NT_SUCCESS(status) || type != DEVPROP_TYPE_STRING || bytes > sizeof(ctx->InstanceId) || bytes < sizeof(WCHAR)) ctx->InstanceId[0] = 0;
    ctx->InstanceId[RTL_NUMBER_OF(ctx->InstanceId) - 1] = 0;
    if (bluetooth) {
        WCHAR *marker = wcsstr(ctx->InstanceId, L"_C");
        if (!marker) marker = wcsstr(ctx->InstanceId, L"_c");
        if (marker && marker - ctx->InstanceId >= 12) {
            BTH_ADDR address = 0; ULONG i;
            for (i = 0; i < 12; ++i) {
                WCHAR c = (marker - 12)[i]; ULONG digit;
                if (c >= L'0' && c <= L'9') digit = c - L'0';
                else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
                else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
                else break;
                address = (address << 4) | digit;
            }
            if (i == 12) ctx->BtAddress = address;
        }
    }
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue, WdfIoQueueDispatchParallel);
    queue.PowerManaged = WdfFalse; queue.EvtIoInternalDeviceControl = InternalIoctl; queue.EvtIoDeviceControl = DeviceIoctl;
    status = WdfIoQueueCreate(device, &queue, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE); if (!NT_SUCCESS(status)) return status;
    WdfWaitLockAcquire(DevicesLock, NULL);
    status = WdfCollectionAdd(Devices, device);
    if (NT_SUCCESS(status)) { ctx->InCollection = TRUE; (void)CreateControl(driver); }
    WdfWaitLockRelease(DevicesLock);
    Trace(1, 1);
    return STATUS_SUCCESS; /* optional control-channel failure must not break typing */
}
_Use_decl_annotations_
VOID DriverUnload(WDFDRIVER driver) {
    UNREFERENCED_PARAMETER(driver);
    if (TraceHandle) { (void)EtwUnregister(TraceHandle); TraceHandle = 0; }
}
_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT object, PUNICODE_STRING path) {
    WDF_DRIVER_CONFIG config; NTSTATUS status;
    WDF_DRIVER_CONFIG_INIT(&config, DeviceAdd);
    config.EvtDriverUnload = DriverUnload;
    status = WdfDriverCreate(object, path, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) return status;
    status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &Devices); if (!NT_SUCCESS(status)) return status;
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &DevicesLock);
    if (NT_SUCCESS(status)) (void)EtwRegister(&TraceProvider, NULL, NULL, &TraceHandle);
    return status;
}
