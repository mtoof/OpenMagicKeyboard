# HID findings

## User-provided facts

- Apple Magic Keyboard 2024 USB-C without Touch ID; VID 05AC / PID 0320.
- MI_01 is the main HID interface; MI_00 is auxiliary.
- Keyboard, consumer-control, and vendor-defined HID children were observed.
- Ordinary typing works with Microsoft HID support. Fn/Globe visibility is missing.

## Initial vendor-filtered measurements (historical)

The first metadata-only run saved `observations/hid-inventory-initial.json`.
The PnP snapshot is `observations/device-baseline-initial.json`. No input-report
capture has been performed. Both files contain identifying device paths/IDs and
are excluded from source control.

**The initial stack was filtered by Magic Utilities.** MI_01 initially used
provider Magic Utilities Pty Ltd, package `oem110.inf`, version 3.1.5.3,
function service HidUsb, and lower/compound lower filter `MagicKeyboard`.
MI_00 also uses that package, with no functional service/filter returned in
the snapshot. All six target-related devnodes report OK/problem code zero.
The keyboard child uses Microsoft's keyboard.inf/kbdhid. The ordinary keyboard
class upper filter is kbdclass; no HID class filters were returned.

These observations cannot establish the unmodified hardware descriptor or
Microsoft-only report behavior. An existing filter may change capabilities or
traffic. These initial measurements are retained for comparison, not used as
unmodified hardware facts. The Microsoft rebind is documented below.

All three discoverable target HID collections descend from USB MI_01:

| Collection | Actual page / usage | Input max bytes / IDs | Output max bytes / IDs | Feature max bytes |
| --- | --- | --- | --- | --- |
| COL01 | 0x0001 / 0x0006 (keyboard) | 10 / 1 | 2 / 1 | 0 |
| COL02 | 0x000C / 0x0001 (consumer) | 3 / 3 | 0 / none | 0 |
| COL03 | 0x0001 / 0x0080 (system control) | 2 / 4 | 0 / none | 0 |

The Device Manager friendly name of COL03 says vendor-defined, but its parsed
top-level usage is system control. Probe classifications use capabilities rather
than friendly names. COL03 capture is deliberately refused by the initial tool;
COL02 is eligible and COL01 is refused. No MI_00 HID collection was returned.

COL01 input report ID 1 declares:

- Keyboard-page modifier usage range 0xE0-0xE7 and keyboard array range 0x00-0xFF.
- A consumer button usage page 0x000C / usage 0x00B8.
- A value usage page **0x00FF / usage 0x0003**, bit size 7, count 1,
  logical range 0-1.

The public [Linux hid-apple implementation](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-apple.c)
recognizes custom-page usage 3 as Fn. This supports identifying the advertised
value as an **Fn candidate**, not an experimentally confirmed key state. No
Linux source, offsets, or implementation logic is copied into the tool. Do not
infer a byte position or bit mask from this capability summary, especially
when interpreting the initial third-party-filtered view.

The candidate is advertised in the keyboard collection, which this user-mode
probe does not read. Consumer-only captures cannot directly establish the
candidate's down/up encoding. After a reviewed restoration of the standard
Microsoft stack and stable pass-through tests, an opt-in driver diagnostic may
need to parse only this verified usage, without logging keyboard arrays.

## Microsoft-only measurements on 2026-10-11

The MI_01 rebind selected Microsoft's preinstalled `input.inf`,
version 10.0.26100.9549, service HidUsb. A live PnP stack query returned
HidUsb -> usbccgp with no MagicKeyboard filter. Device and compound filters
were absent, problem code zero. MI_00 and class-filter configuration were
unchanged. The vendor `oem110.inf` package was retained, and a device-specific
reversal was successfully exercised before returning to Microsoft HID.
Ordinary typing was verified; Fn/language switching remains
missing, as expected before our filter/helper exists. Other manual input and
power/reconnection checks have not been confirmed.

Fresh metadata was saved as `observations/hid-inventory-microsoft.json`.
No input reports, feature reports, output reports, or ordinary typed text were
read. All collections still descend from MI_01 and have complete parsed metadata.
Report IDs below are hexadecimal; lengths include the ID byte and remain
per-type maxima, not experimental wire-length measurements.

| Collection | Page / usage on Microsoft HID | Input max bytes / ID | Output max bytes / ID | Feature max bytes / ID |
| --- | --- | --- | --- | --- |
| COL01 | 0x0001 / 0x0006 (keyboard) | 10 / 0x01 | 2 / 0x01 | 0 / none |
| COL02 | 0x000C / 0x0001 (consumer) | 2 / 0x52 | 0 / none | 4 / 0x09 |
| COL03 | 0xFF00 / 0x0006 (vendor-defined) | 65 / 0x3F | 0 / none | 0 / none |

The unfiltered COL01 input report ID 1 advertises the same modifier and keyboard
array ranges, plus button usages 0x000C/0x00B8, **0x00FF/0x0003**,
0x000C/0x0040 and 0x000C/0x019E. Its value-capability list is empty. Thus the
candidate Fn usage is a **button** on Microsoft HID, unlike the initial
vendor-filtered seven-bit value. Its live down/up behavior is not yet measured.
Do not use the old value-capability interpretation for the hardware baseline.

COL02 input buttons advertise consumer usages 0x00CD, 0x00B3, 0x00B4,
0x00B5 and 0x00B6 on report 0x52. Its feature report 0x09 advertises
page 0xFF01 / usage 0x000B, bit size 8/count 1, logical range 0-1.
No feature query/write was issued and the field's purpose is not established.

COL03 input report 0x3F advertises a buffered-byte value, page 0xFF00 /
usage 0x0006, bit size 8/count 64, logical range 0-255. Its contents and
relationship to Fn are unknown. Eligibility for explicit capture is not proof
that its payload cannot contain keyboard-related information.

The Microsoft and vendor views have different collection semantics, reports
and Fn capability types. Restoring the vendor binding reproduced the initial
parsed view. This establishes that the visible capabilities depend on the
active stack; it does not establish how the vendor implements its changes.
COL01 remains ineligible for raw capture; COL02 and COL03 are eligible on
Microsoft HID. Always refresh metadata after a rebind and select capabilities,
not friendly names or report IDs from an older snapshot.

## What the probe can establish

Collection usage pages/usages; report IDs found in button/value capabilities;
maximum input/output/feature lengths; declared usage ranges and link hierarchy;
and the USB parent chain. IDs found in capabilities can omit constant-only
reports. Type maximum lengths do not prove exact wire lengths for each ID.

Windows provides `HidD_GetPreparsedData`, not a documented HidD raw-report-
descriptor retrieval function. Opaque preparsed data is not serialized as a
descriptor. Raw descriptor retrieval through the HID transport's internal
request path belongs to a separately reviewed driver milestone or another
documented acquisition method, not an undocumented user-mode IOCTL guess.

Keyboard collections are exclusive to Windows. A vendor usage advertised in
the keyboard collection may therefore be visible as metadata but not readable
by this probe. Eligible vendor/consumer collections may still deny read access
or produce no reports. Neither result establishes that the keyboard lacks Fn.

Third-party package declarations do not establish HID report behavior; only
the keyboard's own descriptor and measured reports do.

## Evidence needed before remapping

On successful installation after reconnect, descriptor-request metadata showed
type 0x22, index 0 and interface index 1. The initial index-zero restriction was
unnecessary but was not the measured cause. Completion tracing identified the
cause: usbccgp returned URB_FUNCTION_CONTROL_TRANSFER (0x0008) for the original
GET_DESCRIPTOR_FROM_INTERFACE (0x0028), both 136 bytes, with NTSTATUS/USBD status
success, returned descriptor length 207 and a resident buffer without an MDL.
The old unchanged-function check skipped that successful response. The fix
accepts only known returned structures within original URB capacity and checks
original descriptor buffer/MDL identity and returned length before parsing.
Report offsets and Fn transitions still come from the actual descriptor/device.

The corrected build is installed and exported the actual 207-byte descriptor
to observations/install-3eaf308f1cdb44ebac58c23a23418ca6/report-descriptor.bin.
The shared AddressSanitizer host parser and live driver agree on keyboard
report ID 1, 10 wire bytes, a six-entry array at data bit 16, and candidate
Fn page 0x00FF / usage 3 at data bit 65. Data offsets exclude the report ID:
the Fn field therefore occupies wire byte 9, mask 0x02. These offsets are
derived from this descriptor and are not constants in the driver. Selection
completion validated interface 1's interrupt input pipe. Layout validity
establishes the declared field; a labeled physical down/up test must still
confirm that it corresponds to Fn/Globe before transformation is enabled.

Initial project-filter installation and rollback were exercised. Scalar-only
ETW on reinstall recorded attach/start events but no descriptor-parse event;
control status showed LayoutValid=false, ReportBytes=0 and mapping flags 0.
This does not establish a parser rejection: no descriptor observation has been
confirmed. USB reconnect investigation is next. Current HID package inventory
contains the project package as oem110.inf, with no staged Magic Utilities HID
package; older vendor package-number observations below are historical.

After a physical reconnect of the keyboard, ETW recorded remove/attach/start,
still with no descriptor parse event. A subsequent instrumented build confirmed
kernel-mode IOCTL_INTERNAL_USB_SUBMIT_URB requests and URB function/length pairs
0x000B/136, 0x0000/88, 0x001B/136, 0x0028/136, 0x0009/128 and 0x0032/136.
These are request metadata, not HID report contents or measured Fn encoding.
The next build removed the unjustified descriptor-index-zero restriction and
added bounded, explicit report-descriptor metadata export. That build passed
analysis/INF/signing and 40 GUI protocol checks, but its binding attempt stopped
because USB disconnected before the native install call. Keep the keyboard
connected during installation; actual descriptor and Fn validation remain pending.

The Microsoft-only baseline was obtained through a device-specific
reconfiguration. The vendor package covers MI_00 and other Apple devices, so it
is never deleted automatically; binding changes target one devnode at a time.

Record labeled baseline, Globe down/up, Left, Globe+Left, F1, and Globe+F1
observations. Repeat controlled runs; identify the collection and usage;
establish stable down/up encoding and whether media events already arrive.
If Fn is on the exclusive keyboard collection, plan opt-in driver diagnostics
that observe only the validated Fn field after pass-through tests. Do not log
keyboard array contents or arbitrary interrupt report bytes to find it.
