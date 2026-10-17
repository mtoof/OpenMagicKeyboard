# Bluetooth implementation and validation

On 2026-10-13, with USB disconnected, ordinary Bluetooth typing was verified
with Microsoft HidBth/kbdhid. Version 0.3.0.0 was then bound to this Bluetooth device as `oem115.inf`. Version 0.3.0.1
is now bound as `oem119.inf`. The live stack
is HidBth / OpenMagicKeyboardFilter / BthEnum, with Problem 0 and no reboot request.
The control interface reports a valid report 1 / 10-byte layout and an available
Fn field. The initial pass-through snapshot had zero flags and F-key mode.
Manual input confirmation
was completed on version 0.3.0.0, as was a three-pair Fn diagnostic. The first
native activation failed startup; pass-through policy was restored and Bluetooth
re-paired. Corrected native activation remains pending. Current rollback journal:
`observations/install-9af46bb0f99f474b932de31293897fe8/package.json`.

The reference INF lists this exact compact keyboard's Bluetooth HID service:
`BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320`.
This matches the live device. Its vendor namespace differs from USB 05AC/0320.
We used the INF as device metadata; no vendor SYS or CAT is included in our
implementation. Our code remains independently authored and MIT licensed.

The metadata-only probe now recognizes this exact Bluetooth identity as well
as USB. Bluetooth input capture remains disabled. Observation:
`observations/bluetooth-hid-fd09f6c0249748c8b25904fcda5de235.json`.

The Bluetooth keyboard collection has report ID 1, maximum input length 10
bytes, modifiers E0-E7, and keyboard-array usages 0-101. Its variable Fn
candidate is page 0xFF01 / usage 3. Page 0xFF00 / usage 3 is a constant field;
it must not be mistaken for an Fn input. F23 usage 0x72 is outside this
Bluetooth array's advertised range. The second collection advertises battery
data. Parsed capabilities are not a raw report descriptor or measured Fn offset.
These findings rule out blindly copying the USB bit mapping or F23 synthesis.

The USB device remains bound to version 0.2. Installed Bluetooth version 0.3 adds a separate
Bluetooth lower-filter path beneath Microsoft HidBth. It observes bounded SDP
descriptor responses and inbound L2CAP ACL reports for this exact device and
Bluetooth address. HID report fields are derived from the observed descriptor;
unknown requests/reports pass through. Fn verification is separate for Bluetooth.
Pass-through and three physical Fn pairs were verified on version 0.3.0.0.
Expanded mapping, media/native brightness and package rollback validation remain
pending on Bluetooth.

The opt-in Bluetooth mapping policy extends the advertised keyboard array range
to 0x7F before generating F23. Native brightness mode adds a Consumer Control collection
with an unused report ID and emits matched 0x0070/0x006F press/release reports
through existing read requests. Only finite brightness usages are queued; no
ordinary key arrays are retained. Pass-through SDP bytes stay unchanged.
The USB path implements equivalent native brightness descriptor/report handling.

Version 0.3.0.1 removes identical established HID global declarations during
Bluetooth opt-in changes, preserving main/local items and global Push/Pop state.
The actual 189-byte descriptor compacts to 159 bytes; adding the native collection
produces 186 bytes. SDP text/container header widths remain unchanged when they
still fit. Partial-response sizes are untouched; oversized full transformations
fall back to the original descriptor rather than advertising unavailable bytes.
Static analysis and 175694 AddressSanitizer host checks passed, including actual
descriptor semantics, wire positions, buffer sizes and header-growth cases.

Separate USB and Bluetooth filter bindings follow the standard Windows HID
model (HidUsb and HidBth function drivers). No third-party binaries or installers
are used by this implementation.
Bluetooth mapping and native brightness-key reporting are installed but disabled
until input and Fn verification, followed by physical tests. A custom brightness popup was dropped
in favor of the native indicator. Ordinary Bluetooth typing and native volume indicators continue
to be provided by Windows.

Native display-brightness input uses consumer-control brightness usages
0x006F/0x0070 with matched press/release reports. Microsoft's documented path
targets mobile integrated displays; external screens still need DDC/CI.
See [Microsoft display brightness control](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/display-brightness-control).
No guessed shell messages or private flyout APIs were introduced.
