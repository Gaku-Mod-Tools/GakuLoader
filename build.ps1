#Requires -Version 5.1
# GakuLoader build entry (thin shell over MSBuild).
#
# Toolchain policy:
#   Visual Studio 2022+ MSVC via MSBuild (GakuLoader.vcxproj, /std:c++20 /utf-8 /MT).
#
# Artifact contract:
#   build\version.dll -- x64, version.dll proxy exports + loader internals.
#
# NOTE: Keep this script ASCII-only. Windows PowerShell 5.1 parses .ps1 files
#       without a BOM using the ANSI code page, so non-ASCII text can break parsing.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$OutputDll = '',
    [ValidateSet('auto', 'v143', 'v145')]
    [string]$PlatformToolset = 'auto'
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot

$buildDir = Join-Path $projectRoot 'build'
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

$artifactDir = if ($Configuration -eq 'Release') { $buildDir } else { Join-Path $buildDir 'debug' }
$builtDll = Join-Path $artifactDir 'version.dll'
$builtPdb = Join-Path $artifactDir 'version.pdb'
$projectFile = Join-Path $projectRoot 'GakuLoader.vcxproj'

function Copy-Artifact {
    param([string]$Source, [string]$Destination)
    $src = [System.IO.Path]::GetFullPath($Source)
    $dst = [System.IO.Path]::GetFullPath($Destination)
    if ([string]::Equals($src, $dst, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
    return $true
}

# ---------------------------------------------------------------------------
# Locate MSBuild through vswhere
# ---------------------------------------------------------------------------
. (Join-Path $projectRoot 'tools\msbuild-env.ps1')
Clear-GakuPathDuplicate
$tools = Resolve-GakuBuildTools -PlatformToolset $PlatformToolset
$msbuild = $tools.MSBuild
foreach ($dependency in @('deps\imgui\imgui.cpp', 'deps\minhook\src\hook.c')) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $dependency))) {
        throw "Missing vendored source: $dependency. Restore this file from the frozen source snapshot; no external checkout is required."
    }
}

Write-Host "Building version.dll with MSVC/$Configuration..." -ForegroundColor Cyan

$msbuildArgs = @(
    $projectFile,
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    "/p:PlatformToolset=$($tools.PlatformToolset)",
    "/p:VCToolsVersion=$($tools.Version)",
    '/nologo',
    '/verbosity:minimal',
    '/m',
    '/nodeReuse:false'
)

& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path $builtDll)) {
    throw "Build reported success but $builtDll is missing."
}

$dllSize = (Get-Item $builtDll).Length
Write-Host "Successfully built: $builtDll ($dllSize bytes) via MSVC/$Configuration" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Candidate output copy if requested
# ---------------------------------------------------------------------------
if (-not [string]::IsNullOrWhiteSpace($OutputDll)) {
    $targetCandidate = [System.IO.Path]::GetFullPath($OutputDll)
    $candidateDir = Split-Path -Parent $targetCandidate
    New-Item -ItemType Directory -Path $candidateDir -Force | Out-Null
    if (-not (Copy-Artifact $builtDll $targetCandidate)) {
        Write-Host "Candidate already in place: $targetCandidate" -ForegroundColor Yellow
    }
    if (Test-Path $builtPdb) {
        Copy-Artifact $builtPdb (Join-Path $candidateDir 'version.pdb') | Out-Null
    }
    Write-Host "Candidate written to: $targetCandidate" -ForegroundColor Yellow
}
