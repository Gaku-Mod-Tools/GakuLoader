#Requires -Version 5.1
# MSVC-only, offline paths contract suite. Uses a unique synthetic tree and no game.
# Keep ASCII-only so Windows PowerShell 5.1 can read it without a BOM.
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$binary = Get-GakuTestBinary $repo 'paths-tests.exe' $Configuration
$outputDir = Join-Path $repo 'build\paths-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
# Construct Unicode text without introducing non-ASCII script bytes.
$unicodeName = 'game ' + [char]0x5b66 + [char]0x5712 + ' ' + [char]::ConvertFromUtf32(0x1f3b5)
$runDir = Join-Path $outputDir ([Guid]::NewGuid().ToString('N'))
$testRoot = Initialize-GakuTestRoot (Join-Path $runDir $unicodeName)
$pluginDir = Assert-GakuPluginDir (Join-Path $testRoot 'GakuLoader\plugins') @('paths-alpha.dll', 'paths-beta.dll', 'paths-uppercase.dll')
$fixtures = @()
foreach ($name in @('paths-alpha.dll', 'paths-beta.dll', 'paths-uppercase.dll')) {
    $source = Join-Path (Get-GakuTestDir $repo $Configuration) "paths-fixtures\$name"
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing MSVC paths fixture: $source. Run tests\build-tests.ps1." }
    Copy-Item -LiteralPath $source -Destination (Join-Path $pluginDir $name)
    $fixtures += [ordered]@{ name = $name; sha256 = Get-GakuFileHash $source }
}
$log = Join-Path $runDir 'paths-tests.log'
& $binary $testRoot 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Paths contract test failed: $LASTEXITCODE" }
$report = [ordered]@{
    suite = 'paths'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    root = $testRoot
    output = $log
    executable = $binary
    executableSha256 = Get-GakuFileHash $binary
    fixtures = $fixtures
    scope = 'Offline synthetic directories only; no deployment, real migration or game execution.'
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDir 'report.json') -Encoding UTF8
