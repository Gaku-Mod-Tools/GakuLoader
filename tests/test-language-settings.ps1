#Requires -Version 5.1
# Offline language preference contract. Keep this runner ASCII-only.
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$binary = Get-GakuTestBinary $repo 'language-settings-tests.exe' $Configuration
$outputDir = Join-Path $repo 'build\language-settings-tests'
$runDir = Join-Path $outputDir ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$log = Join-Path $runDir 'language-settings-tests.log'
& $binary $runDir 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Language preference contract failed: $LASTEXITCODE" }
[ordered]@{
    suite = 'language-settings'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    output = $log
    executable = $binary
    executableSha256 = Get-GakuFileHash $binary
    scope = 'Offline synthetic configuration and service contract only.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputDir 'report.json') -Encoding UTF8
