#Requires -Version 5.1
# Host/dialog concurrency suite: one MinGW fixture plugin, three real Shell modals, and the
# production frame/actor pump (MSVC host binary). Proves actor dispatches keep advancing
# while a modal blocks the desktop thread, with overlapCount = 0.
#
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $repo 'tests\test-common.ps1')

$testRoot = Initialize-GakuTestRoot (Join-Path $repo 'build\host-dialog-concurrency')
$plugins = Join-Path $testRoot 'GakuLoader\plugins'
# Exactly one plugin may be resident: a second DLL would make the "one plugin" contract
# non-deterministic, so anything else in this directory is reported instead of deleted.
Assert-GakuPluginDir $plugins @('fixture-a.dll') | Out-Null
# @(...) matters: a single-element result would otherwise unroll to a bare string and
# $fixture[0] would return the first character of the path.
$fixture = @(Copy-GakuFixtures $repo $plugins @('fixture-a.dll'))[0]

$executable = Get-GakuTestBinary $repo 'host-dialog-concurrency.exe' $Configuration
$report = Join-Path $testRoot 'report.json'
$stdout = Join-Path $testRoot 'test-output.log'
$stderr = Join-Path $testRoot 'test-errors.log'

$process = Start-Process -FilePath $executable -ArgumentList @('"' + $testRoot + '"', '"' + $report + '"') `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$processHandle = $process.Handle
if (-not $process.WaitForExit(95000)) {
    $process.Kill() # Only this fixture process; no game process is opened.
    throw 'Actual host/dialog concurrency fixture timed out.'
}
$process.WaitForExit()
Get-Content -LiteralPath $stdout -Encoding UTF8
if ((Get-Item -LiteralPath $stderr).Length -gt 0) { Get-Content -LiteralPath $stderr -Encoding UTF8 }
if ($process.ExitCode -ne 0) { throw "Actual host/dialog concurrency failed: $($process.ExitCode). Report: $report" }
if (-not (Test-Path -LiteralPath $report)) { throw 'Host/dialog concurrency JSON report missing.' }
$result = Get-Content -LiteralPath $report -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $result.passed) { throw "Host/dialog concurrency result failed: $($result.failure)" }

$result | Add-Member -NotePropertyName fixtureSha256 -NotePropertyValue (Get-GakuFileHash $fixture)
$result | Add-Member -NotePropertyName executableSha256 -NotePropertyValue (Get-GakuFileHash $executable)
$result | Add-Member -NotePropertyName configuration -NotePropertyValue $Configuration
$result | Add-Member -NotePropertyName completedUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding UTF8
Write-Output "Verified actual host A/B actor dispatches during $($result.cases.Count) real Shell modals. JSON: $report"
