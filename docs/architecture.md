# Architecture

## Probe and GUI

The HID probe enumerates only the Apple VID/PID, confirms collection identity
and USB MI_01 ancestry, and reads parsed capabilities without input reads by
default. Explicit trace mode rejects keyboard collections and nested keyboard
usages, uses one bounded overlapped read, and drains cancellation before freeing
buffers. See the root README and hid-findings.md for its investigation limits.

The .NET Framework GUI preserves ten separate physical source mappings,
including Caps Lock and both Shift keys. Schema 2 adds F-key mode, optional
Fn combinations and ISO exchanged-key swapping; schema 1 loads migrate.
Status reads are nonadmin; Apply launches an elevated copy to save a finite
policy and restart only the exact USB MI_01 instance. No global hook, Scancode
Map, raw keyboard capture, network or startup service is used.

## Filter

OpenMagicKeyboardFilter is a KMDF 1.33 lower filter on only
USB\VID_05AC&PID_0320&MI_01, under Microsoft HidUsb. Independently authored INF
AddFilter registration uses FilterPosition=Lower and DIRID 13. Runtime hardware
ID checks provide a second target guard. MI_00, Bluetooth and other models are
excluded. Release is locally test-signed; initial loading and Microsoft rollback
succeeded. The actual descriptor, physical Fn and Windows F23 delivery are
confirmed. Expanded shortcuts/media and reconnect/power tests remain pending.

The driver observes completed IOCTL_INTERNAL_USB_SUBMIT_URB requests for report
descriptors, interface/pipe selection and interrupt-IN transfers. USB clients
retain buffer ownership. The filter validates structure lengths, completion
status, returned length versus requested capacity, resident buffers/MDL bounds,
the observed interrupt-IN pipe and a hardware start/stop generation counter.
The measured usbccgp path rewrites a descriptor URB into a standard control
transfer. Completion accepts this bounded conversion, validates the returned
structure and original descriptor buffer/MDL identity, and keeps the original
transfer capacity. A known select-configuration to select-interface conversion
is interpreted using the returned structure with interface/pipe bounds checks.
No feature/control request is generated and no descriptor is changed.
The latest completed report descriptor is retained in a fixed 4096-byte device
context buffer. A separate read-only, fixed-size METHOD_BUFFERED IOCTL exports
only those descriptor bytes when explicitly requested; it exposes no input
reports or arbitrary kernel memory. Descriptor cache length is cleared on
hardware stop/start and stale completions cannot restore it.
Other request types use automatic KMDF filter forwarding.

The shared portable ReportCore.c parser accepts a bounded supported subset of
HID short items, tracks globals/push/pop, local usages, collection nesting and
per-report bit positions. It derives all field locations. Unknown structures
fail open. Host tests compile this same source with AddressSanitizer; no captured
input or assumed hardware offsets appear in the tests.

## Mappings

Default policy is unchanged pass-through. Opt-in mappings combine simultaneous
owners of the same modifier using OR, preserve unmapped Shift keys and ordinary
array entries, and use only an empty descriptor-defined keyboard array slot for
F23. A full array, rollover/error, unknown ID, unsupported layout or short report
passes through unchanged. Releases are represented by the next complete physical
report rather than an injection queue. Disconnect/power release behavior must
still be verified on Windows hardware.

A single variable bit on HID page 0x00FF, usage 3 is an Fn candidate until tested.
Explicit diagnostic mode counts down/up transitions, and user confirmation
after a labeled experiment gates physical Fn remapping. The Fn/Globe output
target means F23 on Windows; it does not reproduce firmware Fn combinations.
The connected keyboard passed that physical confirmation. Optional Fn+Delete
and arrow shortcuts latch their chosen action until the source key is released.
Icon modes queue only finite F-key actions, with a bounded helper heartbeat;
without the helper they pass through standard F-keys. The normal-user helper
uses fixed Windows chords/media keys and supported WMI brightness methods.
Language switching and Bluetooth remain deferred.

Policy changes are validated and saved as one fixed binary value in the target
hardware registry key; they activate on its next prepare/start. Live held-key
policy changes are avoided. Hardware stop invalidates the layout and pipe, and
old request completions cannot resurrect them.

## Secured sideband protocol

A separate named KMDF control device exposes only fixed-size METHOD_BUFFERED
status and configuration IOCTLs. Its ACL grants SYSTEM/Administrators full
access and Interactive Users read-only access. Policy writes require
FILE_WRITE_DATA and administrator elevation. No pointer, memory range, arbitrary
USB request, filesystem/registry path or process operation is accepted.
The channel requires exactly one matching filter device, uses a protected
device collection, and is deleted when the last target is removed.

Status contains finite configuration/capabilities, the exact instance identity
and opt-in Fn counters; it has no ordinary key history. ETW scalar lifecycle
events require an enabled consumer, and Fn events additionally require explicit
diagnostic mode. No driver-created input log or telemetry exists.
