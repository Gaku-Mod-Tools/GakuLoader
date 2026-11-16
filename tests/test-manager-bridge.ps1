#Requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$binary = Get-GakuTestBinary $repo 'manager-bridge-tests.exe' $Configuration
$outputDir = Join-Path $repo 'build\manager-bridge-tests'
$runDir = Join-Path $outputDir ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$log = Join-Path $runDir 'manager-bridge-tests.log'
& $binary 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Manager bridge contract failed: $LASTEXITCODE" }
[ordered]@{
    suite = 'manager-bridge'
    result = 'PASS'
    configuration = $Configuration
    completedUtc = [DateTime]::UtcNow.ToString('o')
    output = $log
    executable = $binary
    executableSha256 = Get-GakuFileHash $binary
    scope = 'Offline synthetic bridge core and backend contract only.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputDir 'report.json') -Encoding UTF8
