#Requires -Version 5.1
# Production manager UI driven by synthetic ImGui events, with hidden WARP
# backbuffer screenshots. Execution only; build-tests.ps1 produces the binary.
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'test-common.ps1')
$outputDir = Join-Path $repo 'build\manager-ui-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$binary = Get-GakuTestBinary $repo 'manager-ui-tests.exe' $Configuration
$log = Join-Path $outputDir 'manager-ui-tests.log'
& $binary $outputDir 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Manager UI WARP test failed: $LASTEXITCODE" }
