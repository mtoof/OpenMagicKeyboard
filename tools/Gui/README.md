# Magic Keyboard app

WinForms app (.NET Framework 4.8, no install needed) for the Apple Magic Keyboard
A3203. Runs as a normal user; only **Apply** and **Install driver** ask for
administrator approval.

```powershell
.\scripts\build.ps1 -Target Gui
.\artifacts\Release\MagicKeyboardGui.exe
```

- **Device info** — drawing of the keyboard (ANSI or ISO) with changed keys in green,
  connection (USB-C / Bluetooth) and battery level (read from the keyboard's
  battery report over Bluetooth).
- **F-Key Mappings** — standard F-keys, F-keys with fn for icons, or locked fn.
- **Other Key Mappings** — ISO § / ` swap, fn + delete / arrows navigation.
- **Modifier Key Mappings** — one selector per physical key, laid out like the
  bottom rows of the keyboard; quick Ctrl ⇄ Cmd and Alt ⇄ Cmd swaps.
- **Driver** — explains the driver state (not installed, blocked because test signing
  is off, Magic Utilities in use, active) with an Install / Repair button, and
  **Apply to keyboard**, which saves the profile and activates it on every transport.

The profile is saved to `%LOCALAPPDATA%\OpenMagicKeyboard\profile.json`.
Icon F-key modes register the media helper (`--media-helper`) to start with Windows.

Other options: `--driver-status`, `--self-test`, `--profile FILE`, `--help`.
Checks: `scripts\test-gui.ps1` and `scripts\test-gui-layout.ps1` (saves a preview image).
