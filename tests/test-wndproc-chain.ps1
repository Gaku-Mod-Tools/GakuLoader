#Requires -Version 5.1
# WndProc chain suite. Pure execution: the binary is the MSVC build of
# tests\WndProcChainTests.cpp against the production QuitGuard sources.
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

$outputDir = Join-Path $repo 'build\wndproc-chain-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$binary = Get-GakuTestBinary $repo 'wndproc-chain-tests.exe' $Configuration
$log = Join-Path $outputDir 'wndproc-chain-tests.log'
& $binary 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "WndProc chain test failed: $LASTEXITCODE" }
