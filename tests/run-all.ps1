#Requires -Version 5.1
# Formal test runner: builds once, runs every suite, writes one machine-readable report.
#
#   .\tests\run-all.ps1                    # Release: build + all suites
#   .\tests\run-all.ps1 -SkipBuild         # reuse the binaries already in build\tests\Release
#   .\tests\run-all.ps1 -Only host,frame-pump
#
# Compilation happens exactly once, in build-tests.ps1. Every suite here is execution
# plus assertions against the artifacts that call produced, which is what makes a PASS
# traceable to a specific binary (each entry records the SHA256 it ran).
#
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [switch]$SkipFixtures,
    [string[]]$Only = @()
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')

$logDir = Join-Path $repo 'build\tests'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

# Cheap and deterministic first; the windowed UI suites last so an interactive failure
# does not hide the headless results.
$suites = @(
    [ordered]@{ name = 'language-settings'; script = 'tests\test-language-settings.ps1'; evidence = @() }
    [ordered]@{ name = 'desktop-fonts'; script = 'tests\test-desktop-fonts.ps1'; evidence = @('build\desktop-fonts-tests.log') }
    [ordered]@{ name = 'manager-ui'; script = 'tests\test-manager-ui.ps1'; evidence = @('build\manager-ui-tests\manager-ui-tests.log') }
    [ordered]@{ name = 'docs'; script = 'tools\test-docs.ps1'; evidence = @() }
    [ordered]@{ name = 'desktop-appearance'; script = 'tools\test-desktop-appearance.ps1'; evidence = @('build\desktop-appearance-tests.log') }
    [ordered]@{ name = 'host'; script = 'tests\test-host.ps1'; evidence = @('build\host-contract-tests\report.json') }
    [ordered]@{ name = 'frame-pump'; script = 'tests\test-frame-pump.ps1'; evidence = @('build\frame-pump-tests\frame-pump-tests.log') }
    [ordered]@{ name = 'paths'; script = 'tests\test-paths.ps1'; evidence = @('build\paths-tests\report.json') }
    [ordered]@{ name = 'diagnostic-boundary'; script = 'tests\run_diagnostic_boundary.ps1'; evidence = @() }
    [ordered]@{ name = 'desktop-dialogs'; script = 'tools\test-desktop-dialogs.ps1'; evidence = @('build\desktop-dialogs-tests\report.json') }
    [ordered]@{ name = 'desktop-file-drop'; script = 'tests\test-desktop-file-drop.ps1'; evidence = @('build\desktop-file-drop-tests\report.json') }
    [ordered]@{ name = 'host-dialog-concurrency'; script = 'tools\test-host-dialog-concurrency.ps1'; evidence = @('build\host-dialog-concurrency\report.json') }
    [ordered]@{ name = 'desktop-ui'; script = 'tools\test-desktop-ui.ps1'; evidence = @('build\desktop-ui-smoke.log') }
    [ordered]@{ name = 'desktop-ui-cross-dll'; script = 'tools\test-desktop-ui-cross-dll.ps1'; evidence = @('build\desktop-ui-cross-dll-report.json') }
)

if ($Only.Count -gt 0) {
    $selected = @()
    foreach ($name in $Only) {
        $match = $suites | Where-Object { $_.name -eq $name }
        if (-not $match) { throw "Unknown suite: $name. Known: $(($suites | ForEach-Object { $_.name }) -join ', ')" }
        $selected += $match
    }
    $suites = $selected
}

$startedUtc = [DateTime]::UtcNow
if ($SkipBuild) {
    Write-Host 'Skipping the build step; running existing artifacts.' -ForegroundColor Yellow
} else {
    & (Join-Path $PSScriptRoot 'build-tests.ps1') -Configuration $Configuration -SkipFixtures:$SkipFixtures
    if ($LASTEXITCODE -ne 0) { throw "Build step failed with exit code $LASTEXITCODE" }
}

$results = @()
$failed = 0
$skipped = 0
$fixtureSuites = @('host', 'diagnostic-boundary', 'host-dialog-concurrency', 'desktop-ui-cross-dll')
foreach ($suite in $suites) {
    $scriptPath = Join-Path $repo $suite.script
    $logPath = Join-Path $logDir ("{0}.log" -f $suite.name)
    $suiteStarted = [DateTime]::UtcNow
    Write-Host ''
    Write-Host ("=== {0} ===" -f $suite.name) -ForegroundColor Cyan

    $status = 'PASS'
    $errorText = $null
    # $LASTEXITCODE is process-wide, so a suite that never launches a native process would
    # otherwise inherit the previous suite's value and report a false PASS.
    $global:LASTEXITCODE = 0
    # Not every suite takes -Configuration (test-docs.ps1 only reads files), so only pass
    # the parameter when the script actually declares it.
    $invokeArgs = @{}
    if ((Get-Command $scriptPath).Parameters.ContainsKey('Configuration')) {
        $invokeArgs['Configuration'] = $Configuration
    }
    try {
        if ($SkipFixtures -and $suite.name -in $fixtureSuites) {
            $status = 'SKIP_DEPENDENCY'
            $skipped++
            $skipReason = 'SKIP_DEPENDENCY: optional MinGW fixtures explicitly excluded.'
            Set-Content -LiteralPath $logPath -Value $skipReason -Encoding ASCII
            Write-Host $skipReason -ForegroundColor Yellow
        } else {
        & $scriptPath @invokeArgs 2>&1 | Tee-Object -FilePath $logPath
        if ($LASTEXITCODE -ne 0) { throw "Suite exited with code $LASTEXITCODE" }
        }
    } catch {
        $status = 'FAIL'
        $errorText = $_.Exception.Message
        $failed++
        Write-Host ("FAIL: {0}" -f $errorText) -ForegroundColor Red
    }

    $entry = [ordered]@{
        name = $suite.name
        result = $status
        script = $suite.script
        log = $logPath
        durationSeconds = [math]::Round(([DateTime]::UtcNow - $suiteStarted).TotalSeconds, 1)
    }
    if ($errorText) { $entry.error = $errorText }
    $evidence = @()
    foreach ($relative in $suite.evidence) {
        $full = Join-Path $repo $relative
        if ($status -eq 'PASS' -and (Test-Path -LiteralPath $full) -and
            (Get-Item -LiteralPath $full).LastWriteTimeUtc -ge $suiteStarted) {
            $evidence += [ordered]@{ path = $relative; sha256 = Get-GakuFileHash $full }
        }
    }
    if ($evidence.Count -gt 0) { $entry.evidence = $evidence }
    $results += $entry
}

$report = [ordered]@{
    result = if ($failed) { 'FAIL' } elseif ($skipped) { 'PASS_WITH_SKIPS' } else { 'PASS' }
    configuration = $Configuration
    startedUtc = $startedUtc.ToString('o')
    completedUtc = [DateTime]::UtcNow.ToString('o')
    suites = $results
}
$reportPath = Join-Path $logDir 'report.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8

Write-Host ''
if ($failed -eq 0) {
    Write-Host ("TEST RUN COMPLETE ({0} passed, {1} skipped). Report: {2}" -f ($results.Count - $skipped), $skipped, $reportPath) -ForegroundColor Green
} else {
    throw ("{0} of {1} suites failed. Report: {2}" -f $failed, $results.Count, $reportPath)
}
