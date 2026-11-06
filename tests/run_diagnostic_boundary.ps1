#Requires -Version 5.1
# Diagnostic boundary suite: eight native scenarios against the MSVC host.
#
# The GameAssembly.dll stand-in is a MinGW build (build\fixtures\GameAssembly.dll): the
# hook has to relocate a probe compiled by the other toolchain, which is the real risk.
#
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')

# Each scenario owns <root>\<scenario>\GakuLoader. The fixture appends to the log, so
# every scenario's log is truncated first: a re-run then sees exactly the same budgets as
# the first run.
$testRoot = Initialize-GakuTestRoot (Join-Path $repo 'build\diagnostic-boundary-tests')
Copy-GakuFixtures $repo $testRoot @('GameAssembly.dll') | Out-Null
$binary = Get-GakuTestBinary $repo 'diagnostic-boundary-tests.exe' $Configuration

foreach ($scenario in @('success', 'missing', 'parameter', 'return', 'instance', 'rollback', 'aliased', 'collision')) {
    Reset-GakuLog (Join-Path $testRoot "$scenario\GakuLoader\Logs\gakuloader.log")
    & $binary $testRoot $scenario
    if ($LASTEXITCODE -ne 0) { throw "Diagnostic boundary scenario failed: $scenario ($LASTEXITCODE)" }
}
Write-Host 'Diagnostic boundary: 8 native scenarios passed.' -ForegroundColor Green
