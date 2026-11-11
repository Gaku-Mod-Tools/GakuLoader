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
$pluginDir = Join-Path $testRoot 'GakuLoader\plugins'
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
New-Item -ItemType Directory -Force -Path (Join-Path $testRoot 'GakuLoader\Logs') | Out-Null
[IO.File]::WriteAllText((Join-Path $testRoot 'GakuLoader\Logs\gakuloader.log'), '')
$hostTests = Get-GakuTestBinary $repo 'host-tests.exe' $Configuration
& $hostTests $testRoot | Tee-Object -FilePath $outputPath
if ($LASTEXITCODE -ne 0) { throw "Host contract test failed: $LASTEXITCODE" }
$redirectedOutput = Get-Content -LiteralPath $outputPath -Raw
if ($redirectedOutput -match '\[GakuLoader|\[Plugin|literal %s %n fixture log') { throw 'Loader diagnostics leaked into redirected stdout' }
$privateOutput = Get-Content -LiteralPath (Join-Path $testRoot 'GakuLoader\Logs\gakuloader.log') -Raw
if (-not $privateOutput.Contains('literal %s %n fixture log')) { throw 'Private log did not receive the literal plugin diagnostic' }
foreach ($marker in @('[Shutdown] begin plugin=Fixture A', '[Shutdown] end plugin=Fixture A callback_ms=',
        '[Shutdown] begin plugin=Fixture B', 'Rejected legacy-fixture.dll',
        '[Hotkeys] ambiguous legacy override a.b.c ignored', 'no partial overrides loaded')) {
    if (-not $privateOutput.Contains($marker)) { throw "Log marker missing: $marker" }
}

# Timestamped log contract: every record carries exactly one local-time prefix,
# one severity column and one source tag, and one host run writes one session
# header. A torn interleaved line would not match the prefix and fails here.
# The severity column is fixed width, so the tag starts at the same offset for
# DEBUG/INFO/WARN/ERROR; the regex only requires whitespace between them.
$timestampPrefix = '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}[+-]\d{2}:\d{2} (DEBUG|INFO|WARN|ERROR)\s+\['
$logLines = @(Get-Content -LiteralPath (Join-Path $testRoot 'GakuLoader\Logs\gakuloader.log') |
    Where-Object { $_ -ne '' -and $_ -notmatch '^=+$' })
foreach ($line in $logLines) {
    if ($line -notmatch $timestampPrefix) { throw "Log line missing timestamp/level prefix: $line" }
}
$sessions = @($logLines | Where-Object { $_ -match '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}[+-]\d{2}:\d{2} INFO\s+\[GakuLoader\] Session started$' })
if ($sessions.Count -ne 1) { throw "Expected exactly one Session started header after one host run, got $($sessions.Count)" }

# A disabled filename must be skipped before LoadLibrary, so the pre-reset DLL
# never runs its DllMain and no legacy entry point is probed.
$filteredRoot = Join-Path $testRoot 'filtered'
$filteredPlugins = Join-Path $filteredRoot 'GakuLoader\plugins'
Assert-GakuPluginDir $filteredPlugins @('fixture-a.dll', 'fixture-b.dll', 'legacy-fixture.dll', 'GakuBurstBridge.dll', 'GakuMotion.VmdImporter.Native.dll') | Out-Null
Copy-GakuFixtures $repo $filteredPlugins @('fixture-a.dll', 'fixture-b.dll', 'legacy-fixture.dll') | Out-Null
# The legacy fixture writes a marker in DllMain. Auxiliary names must be skipped
# before LoadLibrary, while ordinary incompatible plugins remain diagnosed.
Copy-Item -LiteralPath (Join-Path $filteredPlugins 'legacy-fixture.dll') -Destination (Join-Path $filteredPlugins 'GakuBurstBridge.dll') -Force
Copy-Item -LiteralPath (Join-Path $filteredPlugins 'legacy-fixture.dll') -Destination (Join-Path $filteredPlugins 'GakuMotion.VmdImporter.Native.dll') -Force
New-Item -ItemType Directory -Force -Path (Join-Path $filteredRoot 'GakuLoader\Configs') | Out-Null
[IO.File]::WriteAllText((Join-Path $filteredRoot 'GakuLoader\Configs\gakuloader.ini'), "[plugins]`r`nLEGACY-FIXTURE.DLL=FaLsE`r`n")
& $hostTests $filteredRoot 'filtered'
if ($LASTEXITCODE -ne 0) { throw 'Plugin filename filter failed' }

# A runtime callback can wait for native work while holding the lifecycle gate.
# Cancellation stays readable and prevents later runtime callbacks from starting.
& $hostTests $testRoot 'cancel-runtime'
if ($LASTEXITCODE -ne 0) { throw 'Runtime initialization cancellation failed' }

# The cancel-runtime run appends to the same log as a second process: exactly two
# session headers prove append mode survived and each process marks its session
# once and only once.
$sessionsTotal = @(Get-Content -LiteralPath (Join-Path $testRoot 'GakuLoader\Logs\gakuloader.log') |
    Where-Object { $_ -match '\[GakuLoader\] Session started$' })
if ($sessionsTotal.Count -ne 2) { throw "Expected two Session started headers after two host runs on one log, got $($sessionsTotal.Count)" }

$report = [ordered]@{
    suite = 'host'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    output = $outputPath
    privateLog = Join-Path $testRoot 'GakuLoader\Logs\gakuloader.log'
    executable = $hostTests
    executableSha256 = Get-GakuFileHash $hostTests
    fixtureSha256 = Get-GakuFileHash (Join-Path $pluginDir 'fixture-a.dll')
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testRoot 'report.json') -Encoding UTF8
