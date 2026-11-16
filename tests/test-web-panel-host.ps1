#Requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$binary = Get-GakuTestBinary $repo 'web-panel-host-tests.exe' $Configuration
$outputDir = Join-Path $repo 'build\web-panel-host-tests'
$runDir = Join-Path $outputDir ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$log = Join-Path $runDir 'web-panel-host-tests.log'
& $binary 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Web panel host contract failed: $LASTEXITCODE" }
[ordered]@{
    suite = 'web-panel-host'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    output = $log
    executable = $binary
    executableSha256 = Get-GakuFileHash $binary
    scope = 'Offline synthetic WebView2 panel host lifecycle seams only; no runtime or game.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputDir 'report.json') -Encoding UTF8
