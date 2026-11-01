#Requires -Version 5.1
# Host contract suite: the MSVC host drives MinGW-built plugins.
#
# Nothing here compiles. The host binary comes from build\tests\<Configuration>\ (MSVC,
# because that is the artifact that ships) and the plugins come from build\fixtures\
# (MinGW, because the toolchain asymmetry is the point of the test).
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

$testRoot = Initialize-GakuTestRoot (Join-Path $repo 'build\host-contract-tests')
$pluginDir = Join-Path $testRoot 'gakumas-local\plugins'
Assert-GakuPluginDir $pluginDir @('fixture-a.dll', 'fixture-b.dll', 'legacy-fixture.dll') | Out-Null

# Both halves of the C ABI check: the same header must describe the same layout under
# MSVC and under MinGW. A mismatch here invalidates every other suite's assumptions.
$msvcAbi = Get-GakuTestBinary $repo 'c-abi-test.exe' $Configuration
& $msvcAbi
if ($LASTEXITCODE -ne 0) { throw 'C ABI layout test (MSVC) failed' }
$mingwAbi = Get-GakuFixture $repo 'c-abi-test-mingw.exe'
& $mingwAbi
if ($LASTEXITCODE -ne 0) { throw 'C ABI layout test (MinGW) failed' }

Copy-GakuFixtures $repo $pluginDir @('fixture-a.dll', 'fixture-b.dll', 'legacy-fixture.dll') | Out-Null

$isolation = Get-GakuTestBinary $repo 'minhook-isolation.exe' $Configuration
& $isolation
if ($LASTEXITCODE -ne 0) { throw "MinHook batch isolation failed: $LASTEXITCODE" }

$outputPath = Join-Path $testRoot 'test-output.txt'
[IO.File]::WriteAllText((Join-Path $testRoot 'gakumas-local\gakuloader.log'), '')
$hostTests = Get-GakuTestBinary $repo 'host-tests.exe' $Configuration
& $hostTests $testRoot | Tee-Object -FilePath $outputPath
if ($LASTEXITCODE -ne 0) { throw "Host contract test failed: $LASTEXITCODE" }
$redirectedOutput = Get-Content -LiteralPath $outputPath -Raw
if ($redirectedOutput -match '\[GakuLoader|\[Plugin|literal %s %n fixture log') { throw 'Loader diagnostics leaked into redirected stdout' }
$privateOutput = Get-Content -LiteralPath (Join-Path $testRoot 'gakumas-local\gakuloader.log') -Raw
if (-not $privateOutput.Contains('literal %s %n fixture log')) { throw 'Private log did not receive the literal plugin diagnostic' }
foreach ($marker in @('[Shutdown] begin plugin=Fixture A', '[Shutdown] end plugin=Fixture A callback_ms=',
        '[Shutdown] begin plugin=Fixture B', 'Rejected legacy-fixture.dll')) {
    if (-not $privateOutput.Contains($marker)) { throw "Log marker missing: $marker" }
}

# A disabled filename must be skipped before LoadLibrary, so the pre-reset DLL
# never runs its DllMain and no legacy entry point is probed.
$filteredRoot = Join-Path $testRoot 'filtered'
$filteredPlugins = Join-Path $filteredRoot 'gakumas-local\plugins'
Assert-GakuPluginDir $filteredPlugins @('fixture-a.dll', 'fixture-b.dll', 'legacy-fixture.dll') | Out-Null
Copy-GakuFixtures $repo $filteredPlugins @('fixture-a.dll', 'fixture-b.dll', 'legacy-fixture.dll') | Out-Null
[IO.File]::WriteAllText((Join-Path $filteredRoot 'gakuloader.ini'), "[plugins]`r`nLEGACY-FIXTURE.DLL=FaLsE`r`n")
& $hostTests $filteredRoot 'filtered'
if ($LASTEXITCODE -ne 0) { throw 'Plugin filename filter failed' }

$report = [ordered]@{
    suite = 'host'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    output = $outputPath
    privateLog = Join-Path $testRoot 'gakumas-local\gakuloader.log'
    executable = $hostTests
    executableSha256 = Get-GakuFileHash $hostTests
    fixtureSha256 = Get-GakuFileHash (Join-Path $pluginDir 'fixture-a.dll')
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testRoot 'report.json') -Encoding UTF8
