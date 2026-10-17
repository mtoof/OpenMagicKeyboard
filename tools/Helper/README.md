# Optional media helper

The media helper is included in `MagicKeyboardGui.exe`. It runs as a normal
user and handles only finite F-key icon actions from the driver. It does not
switch languages, record ordinary input or use a global keyboard hook.

From the project directory:

```powershell
.\artifacts\Release\MagicKeyboardGui.exe --media-helper
```

Choose an icon mode in the GUI and Apply; Apply starts the helper automatically.
The Start media helper button also starts it. One instance runs per session.
Exit through its tray menu. There is no automatic startup entry; start it again
after signing in. Without a responsive helper, standard F-keys pass through.
Fn+Delete/arrows and modifier mappings work without this helper.

| Keys | Icon action |
| --- | --- |
| F1 / F2 | Brightness down / up on laptop WMI and supported external DDC/CI displays |
| F3 / F4 / F5 | Task View / Windows Search / dictation |
| F6 | Sleep: pressing this icon can put the PC to sleep |
| F7 / F8 / F9 | Previous track / play-pause / next track |
| F10 / F11 / F12 | Mute / volume down / volume up |

Each physical press produces one action; holding volume does not repeat.
Volume uses Windows' built-in indicator. Version 0.3 adds native HID brightness
reports for Windows' laptop brightness handling and indicator, pending physical
validation. The helper adjusts external screens using DDC/CI; it skips WMI writes
when native brightness is active to avoid changing the laptop twice.
Version 0.2 uses WMI/DDC/CI directly and does not trigger the native indicator.
There is no custom brightness popup.
Actions use documented Windows APIs, only in the active local session on the
normal input desktop. Elevated apps may reject injected keys. Each supported
screen changes by 5% of its own brightness range. DDC/CI must be enabled in
external monitor menus; unsupported or inaccessible screens are left unchanged.
Inactive display registrations are ignored. External displays that reject the
standard brightness API use the fixed DDC/CI brightness control (VCP 0x10).
Brightness work runs separately so slow monitors do not stall media polling.
Read available screens and brightness without changing settings:

```powershell
.\artifacts\Release\MagicKeyboardGui.exe --brightness-status | Out-Host
```

Hardware icon-mode behavior,
power and reconnect checks remain pending.
