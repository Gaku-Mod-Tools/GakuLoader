#Requires -Version 5.1
# MSVC-only, offline plugin metadata and dependency scheduler suite. Uses a unique synthetic tree and no game.
# Keep ASCII-only so Windows PowerShell 5.1 can read it without a BOM.
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$binary = Get-GakuTestBinary $repo 'plugin-metadata-tests.exe' $Configuration
$outputDir = Join-Path $repo 'build\plugin-metadata-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$runDir = Join-Path $outputDir ([Guid]::NewGuid().ToString('N'))
$testRoot = Initialize-GakuTestRoot (Join-Path $runDir 'game-root')
$fixtureNames = @('zzz-base.dll', 'aaa-consumer.dll', 'broken-dep.dll', 'opt-cycle-a.dll', 'opt-cycle-b.dll', 'aux-marker.dll')
$pluginDir = Assert-GakuPluginDir (Join-Path $testRoot 'GakuLoader\plugins') $fixtureNames
$fixtures = @()
foreach ($name in $fixtureNames) {
    $source = Join-Path (Get-GakuTestDir $repo $Configuration) "metadata-fixtures\$name"
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing MSVC metadata fixture: $source. Run tests\build-tests.ps1." }
    Copy-Item -LiteralPath $source -Destination (Join-Path $pluginDir $name)
    $fixtures += [ordered]@{ name = $name; sha256 = Get-GakuFileHash $source }
}
$log = Join-Path $runDir 'plugin-metadata-tests.log'
& $binary $testRoot 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Plugin metadata test failed: $LASTEXITCODE" }
$report = [ordered]@{
    suite = 'plugin-metadata'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    root = $testRoot
    output = $log
    executable = $binary
    executableSha256 = Get-GakuFileHash $binary
    fixtures = $fixtures
    scope = 'Offline synthetic directories only; no deployment or game execution.'
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDir 'report.json') -Encoding UTF8
