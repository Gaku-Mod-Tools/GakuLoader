#Requires -Version 5.1
# Frame pump suite. Pure execution: the binary is the MSVC build of
# tests\FramePumpTests.cpp against the production host sources.
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

$outputDir = Join-Path $repo 'build\frame-pump-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$binary = Get-GakuTestBinary $repo 'frame-pump-tests.exe' $Configuration
$log = Join-Path $outputDir 'frame-pump-tests.log'
& $binary 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Frame pump test failed: $LASTEXITCODE" }
