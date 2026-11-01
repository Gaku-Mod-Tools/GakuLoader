#Requires -Version 5.1
# Desktop UI smoke suite (MSVC host): independent window, WARP pixel readback, UI thread
# lifetime, and the startup-failure cancellation path. Execution only - the binary comes
# from build\tests\<Configuration>\.
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

$binary = Get-GakuTestBinary $projectRoot 'desktop-ui-smoke.exe' $Configuration
$log = Join-Path $projectRoot 'build\desktop-ui-smoke.log'
& $binary 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Desktop UI WARP smoke failed: $LASTEXITCODE" }
& $binary --startup-failure 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Desktop UI startup-failure cancellation failed: $LASTEXITCODE" }
