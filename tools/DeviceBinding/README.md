# Device binding utility

Native x64 tool used by `scripts\setup.ps1` and the boot-time recovery task. It binds
an explicitly chosen, already staged driver to exactly one present Apple Magic
Keyboard devnode (`USB\VID_05AC&PID_0320&MI_01` or the Bluetooth HID identity)
with `DiInstallDevice`. It never reads key input, deletes packages or reboots.

```powershell
$id = (Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'USB\VID_05AC&PID_0320&MI_01\*').InstanceId
.\artifacts\Release\MagicKeyboardDeviceBinding.exe --plan --instance $id          # Microsoft driver plan
.\artifacts\Release\MagicKeyboardDeviceBinding.exe --plan-vendor --instance $id   # Magic Utilities plan
.\artifacts\Release\MagicKeyboardDeviceBinding.exe --plan-project --instance $id --inf "$PWD\artifacts\Release\package\OpenMagicKeyboardFilter.inf"
# Elevated, with a new audit file:
#   --apply-microsoft | --restore-vendor | --apply-project ... --audit NEW_FILE
```

The vendor package is found by provider (`Magic Utilities Pty Ltd`) rather than a
fixed `oemNN.inf` name, because Windows reassigns published names. The project
driver can be bound from the Microsoft driver, the vendor driver, or a failed
project binding, so a broken install is repairable in place.

Exit codes: 0 done, 3010 restart required, 1 error (see the audit file).
