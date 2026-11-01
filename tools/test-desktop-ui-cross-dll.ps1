#Requires -Version 5.1
# Cross-DLL desktop UI suite: an MSVC host loads a desktop presentation consumer that was
# built by MinGW.
#
# The direction matters. Before the MSVC migration this was the other way round (MinGW
# host, MSVC consumer), which after the migration would have degenerated into MSVC against
# itself and proved nothing. The consumer now lives in build\fixtures\ beside the other
# heterogeneous artifacts.
#
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $projectRoot 'tests\test-common.ps1')

# Baseline first: the plain smoke must pass on its own, otherwise a cross-DLL failure
# cannot be attributed to the toolchain boundary.
& (Join-Path $PSScriptRoot 'test-desktop-ui.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw "Standalone desktop UI smoke failed: $LASTEXITCODE" }

$smoke = Get-GakuTestBinary $projectRoot 'desktop-ui-smoke.exe' $Configuration
$consumer = Get-GakuFixture $projectRoot 'desktop-ui-consumer-mingw.dll'
$log = Join-Path $projectRoot 'build\desktop-ui-cross-dll-smoke.log'
& $smoke --consumer $consumer 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Cross-DLL desktop host smoke failed: $LASTEXITCODE" }

$report = [ordered]@{
    suite = 'desktop-ui-cross-dll'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    hostCompiler = 'MSVC'
    consumerCompiler = 'MinGW'
    hostSha256 = Get-GakuFileHash $smoke
    consumerSha256 = Get-GakuFileHash $consumer
    log = $log
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectRoot 'build\desktop-ui-cross-dll-report.json') -Encoding UTF8
