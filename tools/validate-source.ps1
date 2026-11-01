#Requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('auto', 'v143', 'v145')][string]$PlatformToolset = 'auto')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'check-source-manifest.ps1')
$revision = (& git -C $repo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Commit the reviewed source before validation.' }
$dirty = (& git -C $repo status --porcelain | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Validation requires a clean committed source snapshot.' }
$logDir = Join-Path $repo 'build\validation'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$receipt = [ordered]@{
    schema = 'gakuloader.observed-source-build/1'
    source_commit = $revision
    source_manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $repo 'SOURCE_MANIFEST.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    started_utc = [DateTime]::UtcNow.ToString('o')
    result = 'RUNNING'
    steps = @()
    artifacts = @()
    excluded = @('MinGW heterogeneous fixtures', 'actual desktop UI/window suites', 'game loading and acceptance', 'remote CI')
    provenance_limit = 'Observed local clean-source build; unsigned and not a reproducibility or game-acceptance attestation.'
}
function Invoke-ValidationStep([string]$Name, [string]$Command, [scriptblock]$Body) {
    $log = Join-Path $logDir ($Name + '.log')
    $global:LASTEXITCODE = 0
    Write-Host "Validating: $Name" -ForegroundColor Cyan
    & $Body *> $log
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE; see $log" }
    $receipt.steps += [ordered]@{ name = $Name; command = $Command; result = 'PASS'; log = "build/validation/$Name.log"; log_sha256 = (Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash.ToLowerInvariant() }
}
Push-Location $repo
try {
    Invoke-ValidationStep 'release-build' 'build.ps1 -Configuration Release' { & (Join-Path $repo 'build.ps1') -Configuration Release -PlatformToolset $PlatformToolset }
    Invoke-ValidationStep 'debug-build' 'build.ps1 -Configuration Debug' { & (Join-Path $repo 'build.ps1') -Configuration Debug -PlatformToolset $PlatformToolset }
    Invoke-ValidationStep 'msvc-test-build' 'tests/build-tests.ps1 -SkipFixtures' { & (Join-Path $repo 'tests\build-tests.ps1') -SkipFixtures -PlatformToolset $PlatformToolset }
    Invoke-ValidationStep 'headless-suites' 'tests/run-all.ps1 -SkipBuild -SkipFixtures -Only docs,frame-pump' { & (Join-Path $repo 'tests\run-all.ps1') -SkipBuild -SkipFixtures -Only @('docs', 'frame-pump') }
    Invoke-ValidationStep 'c-abi' 'build/tests/Release/c-abi-test.exe' { & (Join-Path $repo 'build\tests\Release\c-abi-test.exe') }
    Invoke-ValidationStep 'minhook-isolation' 'build/tests/Release/minhook-isolation.exe' { & (Join-Path $repo 'build\tests\Release\minhook-isolation.exe') }
    Invoke-ValidationStep 'minimal-plugin' 'examples/minimal-plugin/verify.ps1' { & (Join-Path $repo 'examples\minimal-plugin\verify.ps1') -PlatformToolset $PlatformToolset }
    Invoke-ValidationStep 'package' 'tools/test-package.ps1' { & (Join-Path $repo 'tools\test-package.ps1') }
    & (Join-Path $PSScriptRoot 'check-source-manifest.ps1')
    $receipt.result = 'PASS'
} catch {
    $receipt.result = 'FAIL'
    $receipt.error = $_.Exception.Message
    throw
} finally {
    foreach ($name in @('build/version.dll', 'build/debug/version.dll', 'build/examples/minimal-plugin/Release/MinimalPlugin.dll')) {
        $path = Join-Path $repo $name
        if (Test-Path -LiteralPath $path) { $receipt.artifacts += [ordered]@{ path = $name; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() } }
    }
    $receipt.completed_utc = [DateTime]::UtcNow.ToString('o')
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $logDir 'receipt.json') -Encoding UTF8
    Pop-Location
}
Write-Output 'PASS: source-only validation; 8 test projects built, selected headless suites and standalone checks executed. No game or UI window was started.'
