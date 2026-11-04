#Requires -Version 5.1
# Offline broker contract suite. Does not start a desktop window or a game.
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$binary = Get-GakuTestBinary $repo 'desktop-file-drop-tests.exe' $Configuration
$outputDir = Join-Path $repo 'build\desktop-file-drop-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$log = Join-Path $outputDir 'desktop-file-drop-tests.log'
& $binary 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Desktop file-drop contract test failed: $LASTEXITCODE" }
$report = [ordered]@{
    suite = 'desktop-file-drop'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    executable = $binary
    executableSha256 = Get-GakuFileHash $binary
    output = $log
    scope = 'Offline owner/bounds/buffer/queue contract; no Explorer drag, deployment, game or visual acceptance.'
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputDir 'report.json') -Encoding UTF8
