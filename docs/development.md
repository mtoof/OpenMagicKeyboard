# Development and verification

Environment verified on 2026-10-11:

| Component | Installed |
| --- | --- |
| Visual Studio Community 2026 | 18.10.3, `D:\Program Files\Microsoft Visual Studio\18\Community` |
| Desktop C++ / x64 compiler | MSVC 19.51.36260; toolset v145 |
| Spectre libraries | x64 runtime, ATL, and MFC components |
| SDK | 10.0.28000.2957; also 26100 installed |
| WDK | 10.0.28000.2526, x64 kernel toolset and VS integration |
| MSBuild | 18.10.1.42706 |
| InfVerif / Inf2Cat / SignTool | Present and help commands execute |
| WinDbg x64 | 10.0.28000.2957 |
| KMDF development headers/libraries | Including 1.35; minimum runtime remains a later target decision |

The SDK/WDK share build 28000; servicing revisions need not match according to
[Microsoft's setup guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk#kit-versioning).
Build scripts locate Visual Studio via vswhere and choose the latest installed
matching SDK/WDK build. The project default is overridable. No downloads or
software installations occur during builds.

Run `scripts/build.ps1 -Configuration Release -Analyze` using the process-local
PowerShell invocation in the README. It enables MSVC static analysis, `/W4`,
warnings as errors, SDL checks, CFG, ASLR, and DEP. The static C++ runtime avoids
requiring a separate redistributable installation for the probe. MSBuild may
need filesystem access outside an agent sandbox; that is not a driver install.

Build both Release (analysis enabled) and Debug; run `--self-test` plus
`scripts/test-probe.ps1`. These are probe checks, not proof of driver/PnP/power
behavior. No driver INF exists yet, so InfVerif/Inf2Cat package validation is
not applicable at this milestone. See testing.md for required later checks.

No persistent execution-policy, Secure Boot, Defender, signature-enforcement,
Verifier, registry, service, or device-stack changes are made by these scripts.

## Completed checks for the probe milestone

- Release x64 builds with MSVC native code analysis; no warnings or reported defects.
- Debug x64 builds with CFG and `/Zi` (Edit and Continue `/ZI` is intentionally not used).
- Both builds pass 19 native safety checks and the CLI/JSON/output-preservation checks.
- Invalid capture arguments and both present ineligible collections are refused
  before creating capture logs or reading device input.
- All PowerShell scripts parse successfully.
- Metadata-only enumeration succeeds on the attached keyboard; no raw capture,
  driver/INF validation, input modification, or kernel/PnP/power testing occurred.
