[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $repoRoot "artifacts\$Configuration\MagicKeyboardHidProbe.exe"
if (-not (Test-Path -LiteralPath $probe)) { throw 'Build the probe before running checks.' }
$testRoot = Join-Path $repoRoot ('artifacts\probe-checks-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
& $probe --self-test
if ($LASTEXITCODE -ne 0) { throw 'Native safety checks failed.' }
function Assert-Rejected([string[]]$Arguments, [string]$OutputFile) {
    # Expected native stderr must not terminate Windows PowerShell before
    # the executable's exit code can be checked.
    $errorFile = Join-Path $testRoot ([Guid]::NewGuid().ToString('N') + '.stderr.txt')
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $probe @Arguments 2> $errorFile | Out-Null
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 1) { throw ('Expected rejection: ' + ($Arguments -join ' ')) }
    if ($OutputFile -and (Test-Path -LiteralPath $OutputFile)) { throw 'Rejected capture created an output file.' }
}
$invalidFile = Join-Path $testRoot 'should-not-exist.jsonl'
Assert-Rejected @('--unknown') ''
Assert-Rejected @('--trace') ''
Assert-Rejected @('--output') ''
Assert-Rejected @('--seconds', '5') ''
Assert-Rejected @('--trace', '--collection', 'unrelated', '--step', 'password', '--output', $invalidFile) $invalidFile
Assert-Rejected @('--trace', '--collection', 'unrelated', '--step', 'baseline', '--seconds', '0', '--output', $invalidFile) $invalidFile
Assert-Rejected @('--trace', '--collection', 'unrelated', '--step', 'baseline', '--seconds', '31', '--output', $invalidFile) $invalidFile
Assert-Rejected @('--trace', '--collection', 'unrelated', '--step', 'baseline', '--seconds', '-1', '--output', $invalidFile) $invalidFile
Assert-Rejected @('--trace', '--collection', 'unrelated', '--step', 'baseline', '--seconds', '1.5', '--output', $invalidFile) $invalidFile
Assert-Rejected @('--trace', '--collection', 'unrelated', '--step', 'baseline', '--output', $invalidFile) $invalidFile
Assert-Rejected @('--help', '--trace') ''
Assert-Rejected @('--self-test', '--output', $invalidFile) $invalidFile
Assert-Rejected @('--trace', '--trace') ''

$inventoryFile = Join-Path $testRoot 'inventory.json'
& $probe --output $inventoryFile | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Metadata enumeration failed.' }
$inventory = Get-Content -LiteralPath $inventoryFile -Raw | ConvertFrom-Json
if ($inventory.schemaVersion -ne 1 -or $inventory.mode -ne 'metadata-only') { throw 'Unexpected inventory schema.' }
$originalHash = (Get-FileHash -LiteralPath $inventoryFile -Algorithm SHA256).Hash
Assert-Rejected @('--output', $inventoryFile) ''
if ((Get-FileHash -LiteralPath $inventoryFile -Algorithm SHA256).Hash -ne $originalHash) { throw 'Existing observation was modified.' }
$refusalChecks = 0
foreach ($collection in $inventory.collections) {
    if (-not $collection.traceEligible) {
        Assert-Rejected @('--trace', '--collection', $collection.instanceId, '--step', 'baseline', '--output', $invalidFile) $invalidFile
        ++$refusalChecks
    }
}
Write-Host "Probe CLI/JSON/output-preservation checks passed; $refusalChecks present ineligible collections refused."
Write-Host "Metadata-only check artifacts: $testRoot"
