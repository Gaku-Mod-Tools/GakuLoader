#Requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$binary = Get-GakuTestBinary $repo 'desktop-fonts-tests.exe' $Configuration
& $binary 2>&1 | Tee-Object -FilePath (Join-Path $repo 'build\desktop-fonts-tests.log')
if ($LASTEXITCODE -ne 0) { throw "Desktop fonts tests failed: $LASTEXITCODE" }
