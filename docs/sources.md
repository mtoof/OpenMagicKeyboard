# Source provenance

Code is independently written against documented Windows APIs. No third-party
driver source, disassembly, extracted implementation, or binary dependency is
included. The Windows SDK/WDK and system HID libraries are development/platform
dependencies. Linux hid-apple was consulted only for the behavioral fact that
custom HID page usage 3 maps to Fn; no GPL code or implementation was copied.

The following official references informed the probe:

- [Opening HID collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/opening-hid-collections): SetupDi enumeration, CreateFile, HidD/HidP.
- [Top-level collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/top-level-collections): separate collection PDOs.
- [Preparsed data](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/preparsed-data): opaque parser input, distinct from a raw report descriptor.
- [HIDP_CAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/ns-hidpi-_hidp_caps): report-type maximum lengths include the ID byte; ID zero when unused.
- [HidP_GetButtonCaps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/nf-hidpi-hidp_getbuttoncaps) and [HidP_GetValueCaps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/nf-hidpi-hidp_getvaluecaps): capability allocation and returned count validation.
- [System-use collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/top-level-collections-opened-by-windows-for-system-use): Windows opens keyboard collections exclusively.
- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex): cancellation does not imply completion; retain buffer/OVERLAPPED until drained.

Installation uses Microsoft's [AddFilter directive](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addfilter-directive)
and [device filter ordering](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/device-filter-driver-ordering).
These support a device-specific lower filter on Windows 11. Driver Store
placement will follow [running from the Driver Store](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/run-from-driver-store)
and current INF validation, not the vendor's DIRID conventions.

Kernel implementation references: [KMDF filter objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/creating-device-objects-in-a-filter-driver),
[WdfFdoInitSetFilter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdffdo/nf-wdffdo-wdffdoinitsetfilter),
[URB descriptor requests](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usb/ns-usb-_urb_control_descriptor_request),
[device registry access](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nf-wdfdevice-wdfdeviceopenregistrykey),
[KMDF version history](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/kmdf-version-history),
[USB HID 1.11 specification](https://www.usb.org/document-library/device-class-definition-hid-111),
and the official [sideband filter sample](https://github.com/microsoft/Windows-driver-samples/blob/main/general/toaster/toastDrv/kmdf/filter/sideband/filter.c)
for framework object lifetime behavior. Code and bounded parser are original;
no sample function or proprietary algorithm was imported.

The public hardware IDs of this keyboard (USB and Bluetooth) were taken from
the device itself. No third-party package content is included.

Behavioral reference: [Linux hid-apple](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-apple.c)
and its [HID constants](https://github.com/torvalds/linux/blob/master/include/linux/hid.h),
reviewed on 2026-10-11. Both remain external references; their GPL licensing is
not represented as MIT and no source files from them are redistributed here.
