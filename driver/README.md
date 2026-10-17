# OpenMagicKeyboardFilter

Version 0.4.0.0. Install, repair and uninstall with `scripts\setup.ps1` (see the
[main README](../README.md)). 0.4 changes: the control channel serves the transport
with the most recent input (USB-C and Bluetooth devnodes are normally both present),
a policy write is stored for every transport, and Fn is accepted from the parsed
descriptor field (status capability bit 32) instead of requiring diagnostic taps.
See [Bluetooth details](../docs/bluetooth.md).

Original MIT-licensed KMDF 1.33 lower filter for the exact USB interface
`USB\VID_05AC&PID_0320&MI_01` and the documented Bluetooth HID identity.
Microsoft HidUsb/HidBth remain the function drivers. MI_00, other models and
keyboard class filters are excluded.

The SYS/INF/CAT package builds successfully. Release is locally test-signed after
explicit approval. Initial MI_01 loading and Microsoft HID rollback succeeded;
the latest build is installed on the connected MI_01. The real 207-byte
descriptor validates report ID 1 / 10 bytes, including Fn/Globe. An isolated
three-tap experiment confirmed Fn, and Windows received paired F23 events.
Version 0.2 adds optional Fn+Delete/arrows, Caps/Shift mapping and icon modes;
these expanded features still need physical validation. Test signing and
Memory Integrity are active. See the
[complete build, signing, install and rollback instructions](../README.md#build-review-and-install-the-driver).

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\scripts\build.ps1 -Target Driver -Analyze
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\scripts\build.ps1 -Target DriverTests -Analyze
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\scripts\test-driver.ps1
```

Run from the repository root. Builds produce an unsigned package under
`artifacts\Release\package`; symbols are under `artifacts\Release\driver`.
Standalone InfVerif and Inf2Cat are mandatory steps in `build.ps1`.
Rebuilding the prepared Release package requires signing it again.

`Driver.c` observes completed descriptor, interface selection, and interrupt-IN
URBs through documented USB structures. All other requests use KMDF forwarding.
It never sends a feature request, changes a descriptor, logs raw keyboard reports
or retains ordinary key arrays. A generation counter invalidates old completions
across hardware stop/start. Unrecognized descriptors, pipes, lengths, report IDs,
rollover/errors and failed transfers keep original input unchanged.

`ReportCore.c` is the same bounded HID descriptor parser/remapper compiled into
both kernel and AddressSanitizer host tests. No report ID, modifier byte, Fn bit
or key-array offset is hardcoded. Supported layouts have a single keyboard
collection/report, individual modifier bits and a byte-aligned 8-bit keyboard
array. Fn is a single variable button on page 0x00FF, usage 3. This usage is a
candidate until a labeled hardware diagnostic confirms it; that confirmation
has been completed for the connected keyboard.

Mappings combine simultaneous owners of the same output modifier and preserve
unmapped keys. Caps Lock and both Shift keys are configurable. Synthetic
Caps/Menu/F23 uses empty array slots; a full array fails open. Output Fn/Globe
means F23. Optional Fn+Backspace becomes forward Delete; Fn+arrows becomes
Home/End/Page Up/Page Down, retained until the source key is released.
F-key icon modes queue finite actions only while the normal-user helper is
responsive. Automatic language switching remains deferred. Fresh installation
has all transformations and Fn diagnostics disabled.

`Protocol.h` version 2 defines four fixed-size METHOD_BUFFERED IOCTLs: read
status (600 bytes), save policy (24 bytes), explicitly export the cached report
descriptor (4108 bytes), and drain up to 64 finite media events (528 bytes).
The descriptor is
device configuration; no input reports are cached or exported. The separate
control device uses SYSTEM/Administrators full access and
Interactive Users read-only access. Policy writes require FILE_WRITE_DATA and
administrator elevation, accept only finite actions, and persist one bounded
binary value in this device's hardware key. No arbitrary memory, USB request,
registry path, process or filesystem operation is accepted through the protocol.
Changes take effect on a target restart. The control channel refuses ambiguous
multiple-device configurations and is deleted when the last target is removed.

ETW provider `{7C3D46B1-B7D7-4987-93D4-1B8EAC2B1131}`, keyword 1, level 4,
version 1 emits scalar events only: 1 attach, 2 descriptor accepted (0/1),
3 Fn state (0/1), 4 remove, 5 hardware prepare/release (1/0). Fn events and
counters require explicit diagnostic policy. No session or log file is created
by the driver; ETW consumers must be enabled separately. No keystroke text,
ordinary key usages, modifiers, reports or kernel pointers appear in the payload.

Build/static analysis/host fuzzing do not establish kernel PnP, cancellation,
power or hardware behavior. Those tests and targeted rollback remain mandatory
before claiming this driver is finished or equivalent to the working vendor driver.
