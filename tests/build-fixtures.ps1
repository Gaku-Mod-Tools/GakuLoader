#Requires -Version 5.1
# MinGW compatibility fixtures. This is the ONLY place MinGW still appears.
#
# Everything produced here is deliberately built by a toolchain that is NOT the one
# shipping the host. That asymmetry is the test: an MSVC host must load, describe and
# drive plugins compiled by MinGW, under the versioned C ABI in include\GakuPlugin.h.
#
# Output: build\fixtures\
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).
[CmdletBinding()]
param(
    [string]$MinGwBin = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# MinGW is optional for MSVC-only development, required when this script is called.
# Resolve an explicit argument, a task-local environment variable or the caller's PATH.
if ([string]::IsNullOrWhiteSpace($MinGwBin)) { $MinGwBin = $env:GAKU_MINGW_BIN }
if ([string]::IsNullOrWhiteSpace($MinGwBin)) {
    $gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($gccCmd) { $MinGwBin = Split-Path -Parent $gccCmd.Source }
}
if ([string]::IsNullOrWhiteSpace($MinGwBin) -or
    -not (Test-Path -LiteralPath (Join-Path $MinGwBin 'gcc.exe')) -or
    -not (Test-Path -LiteralPath (Join-Path $MinGwBin 'g++.exe'))) {
    throw 'MinGW fixtures require gcc.exe and g++.exe. Supply -MinGwBin or GAKU_MINGW_BIN; for MSVC-only tests use build-tests.ps1 -SkipFixtures and run-all.ps1 -SkipFixtures.'
}

$gxx = Join-Path $MinGwBin 'g++.exe'
$gcc = Join-Path $MinGwBin 'gcc.exe'

# Prepend the bin dir so the toolchain is self-sufficient
if (($env:Path -split ';') -notcontains $MinGwBin) {
    $env:Path = "$MinGwBin;$env:Path"
}

$out = Join-Path $repo 'build\fixtures'
New-Item -ItemType Directory -Path $out -Force | Out-Null

$common = @(
    '-std=c++20', '-O1', '-static',
    "-I$(Join-Path $repo 'include')",
    "-I$(Join-Path $repo 'src')",
    "-I$(Join-Path $repo 'tests')",
    "-I$(Join-Path $repo 'deps\minhook\include')",
    "-I$(Join-Path $repo 'deps\minhook\src')",
    "-I$(Join-Path $repo 'deps\imgui')"
)

function Invoke-FixtureBuild {
    param([string]$Tool, [string[]]$Arguments, [string]$What)
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE" }
}

Write-Host "Building MinGW compatibility fixtures ($MinGwBin)..." -ForegroundColor Cyan

# Two variants of the same fixture source: the host must treat them as distinct owners.
$fixtureSrc = Join-Path $repo 'tests\GakuHostFixture.cpp'
Invoke-FixtureBuild $gxx ($common + @('-DFIXTURE_VARIANT=1', '-shared', $fixtureSrc,
        '-o', (Join-Path $out 'fixture-a.dll'))) 'fixture-a'
Invoke-FixtureBuild $gxx ($common + @('-DFIXTURE_VARIANT=2', '-shared', $fixtureSrc,
        '-o', (Join-Path $out 'fixture-b.dll'))) 'fixture-b'

# Legacy entry point fixture: must be rejected with a rebuild diagnostic, never loaded.
Invoke-FixtureBuild $gxx ($common + @('-shared', (Join-Path $repo 'tests\GakuLegacyFixture.cpp'),
        '-o', (Join-Path $out 'legacy-fixture.dll'))) 'legacy-fixture'

# Stands in for GameAssembly.dll so the diagnostic boundary hook has a real target.
Invoke-FixtureBuild $gxx ($common + @('-DGAKU_DIAGNOSTIC_FIXTURE_DLL=1', '-shared',
        (Join-Path $repo 'tests\test_diagnostic_boundary.cpp'),
        '-o', (Join-Path $out 'GameAssembly.dll'))) 'GameAssembly fixture'

# Desktop UI consumer, built by MinGW on purpose: the cross-DLL smoke pairs it with the
# MSVC smoke host, which is the direction that actually ships.
$imgui = Join-Path $repo 'deps\imgui'
$consumerArgs = $common + @('-shared') +
    @((Join-Path $repo 'tests\DesktopUiConsumer.cpp')) +
    @(@('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp') |
        ForEach-Object { Join-Path $imgui $_ }) +
    @('-o', (Join-Path $out 'desktop-ui-consumer-mingw.dll'),
        '-ld3d11', '-ldxgi', '-ld3dcompiler', '-limm32', '-ldwmapi', '-lgdi32',
        '-lole32', '-luuid', '-lshell32')
Invoke-FixtureBuild $gxx $consumerArgs 'desktop-ui-consumer'

# The documented minimal plugin must keep compiling with the non-shipping toolchain.
$minimalArgs = @('-std=c++17', '-shared', "-I$(Join-Path $repo 'include')",
    (Join-Path $repo 'examples\minimal-plugin\MinimalPlugin.cpp'),
    '-o', (Join-Path $out 'MinimalPlugin.dll'))
Invoke-FixtureBuild $gxx $minimalArgs 'minimal plugin'

# The C ABI layout test exists twice on purpose: the same header must describe the same
# struct under both compilers. tests\CAbiTest.vcxproj covers the MSVC half.
$abiArgs = @('-std=c11', "-I$(Join-Path $repo 'include')",
    (Join-Path $repo 'tests\CAbiTest.c'), '-o', (Join-Path $out 'c-abi-test-mingw.exe'))
Invoke-FixtureBuild $gcc $abiArgs 'c-abi-test (MinGW)'

& (Join-Path $out 'c-abi-test-mingw.exe')
if ($LASTEXITCODE -ne 0) { throw "c-abi-test (MinGW) failed with exit code $LASTEXITCODE" }

Write-Host "MinGW fixtures built successfully in $out" -ForegroundColor Green
