#Requires -Version 5.1
# Desktop dialog suite: ten real Shell/service scenarios driven through the production
# dialog broker (MSVC host binary).
#
# Every path passed to the fixture is explicit and lives under build\: the binary defaults
# to paths relative to the current directory, which used to drop the fixture tree and the
# JSON report into the repository root.
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

$testRoot = Initialize-GakuTestRoot (Join-Path $projectRoot 'build\desktop-dialogs-tests')
$binary = Get-GakuTestBinary $projectRoot 'desktop-dialogs-tests.exe' $Configuration
$report = Join-Path $testRoot 'report.json'
$stdout = Join-Path $testRoot 'test-output.log'
$stderr = Join-Path $testRoot 'test-errors.log'
$fixtures = Join-Path $testRoot 'fixtures'

# Only the fixture process is subject to this timeout. No game process is opened.
$process = Start-Process -FilePath $binary -ArgumentList @('"' + $report + '"', '"' + $fixtures + '"') `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$processHandle = $process.Handle # Retain the handle so Windows PowerShell exposes ExitCode after waiting.
if (-not $process.WaitForExit(195000)) {
    $process.Kill()
    throw 'Desktop dialog native fixture exceeded its timeout; only the test executable was terminated.'
}
$process.WaitForExit()
Get-Content -LiteralPath $stdout -Encoding UTF8
if ((Get-Item -LiteralPath $stderr).Length -gt 0) { Get-Content -LiteralPath $stderr -Encoding UTF8 }
if ($process.ExitCode -ne 0) {
    if (-not (Test-Path -LiteralPath $report)) {
        @{ passed = $false; scope = 'Real IFileOpenDialog fixture'; cases = @();
           failure = "Native fixture exited before reporting: $($process.ExitCode)" } |
            ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $report -Encoding UTF8
    }
    throw "Desktop dialog native test failed: $($process.ExitCode). Evidence: $report"
}
if (-not (Test-Path -LiteralPath $report)) { throw 'Desktop dialog fixture produced no JSON report.' }
$result = Get-Content -LiteralPath $report -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $result.passed) { throw "Desktop dialog native test failed: $($result.failure)" }

$result | Add-Member -NotePropertyName executableSha256 -NotePropertyValue (Get-GakuFileHash $binary)
$result | Add-Member -NotePropertyName configuration -NotePropertyValue $Configuration
$result | Add-Member -NotePropertyName completedUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding UTF8
Write-Output "Verified $($result.cases.Count) real Shell/service cases. JSON: $report"
