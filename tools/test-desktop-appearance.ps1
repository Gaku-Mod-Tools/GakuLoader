#Requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $projectRoot 'tests\test-common.ps1')
$binary = Get-GakuTestBinary $projectRoot 'desktop-appearance-tests.exe' $Configuration
& $binary 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot 'build\desktop-appearance-tests.log')
if ($LASTEXITCODE -ne 0) { throw "Desktop appearance tests failed: $LASTEXITCODE" }
