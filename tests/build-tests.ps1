#Requires -Version 5.1
# Single build entry for the whole test suite.
#
#   .\tests\build-tests.ps1                 -> MSVC projects + MinGW fixtures
#   .\tests\build-tests.ps1 -SkipFixtures    -> MSVC only
#   .\tests\build-tests.ps1 -Configuration Debug
#
# Host-side test binaries are always MSVC: host-tests.exe compiles the production
# PluginHost/ActorEntry/Trace/CrashTrace/Hotkeys translation units, so building it with
# MinGW would test a host that is never shipped.
#
# MinGW only produces the heterogeneous fixtures (build\fixtures\). Those are the whole
# point of the compatibility gate: they prove an MSVC host can drive a plugin that was
# compiled by a different toolchain, and vice versa.
#
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipFixtures,
    [ValidateSet('auto', 'v143', 'v145')]
    [string]$PlatformToolset = 'auto'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

. (Join-Path $repo 'tools\msbuild-env.ps1')
Clear-GakuPathDuplicate

$tools = Resolve-GakuBuildTools -PlatformToolset $PlatformToolset
$msbuild = $tools.MSBuild
$logDir = Join-Path $repo 'build\tests'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$buildLog = Join-Path $logDir 'msvc-build.log'

# Direct project entry keeps older VS2022 installations usable without .slnx support.
$projects = @((Join-Path $repo 'GakuLoader.vcxproj')) +
    @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.vcxproj' -File | ForEach-Object { $_.FullName })
Set-Content -LiteralPath $buildLog -Value '' -Encoding ASCII
foreach ($project in $projects) {
    Write-Host "Building $([IO.Path]::GetFileName($project)) (MSVC/$Configuration)..." -ForegroundColor Cyan
    & $msbuild $project "/p:Configuration=$Configuration" '/p:Platform=x64' `
        "/p:PlatformToolset=$($tools.PlatformToolset)" "/p:VCToolsVersion=$($tools.Version)" `
        '/nologo' '/verbosity:minimal' '/m' '/nodeReuse:false' 2>&1 |
        Tee-Object -FilePath $buildLog -Append
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC build failed with exit code $LASTEXITCODE (log: $buildLog)"
    }
}

$testDir = Join-Path $repo "build\tests\$Configuration"
$expected = @('host-tests.exe', 'minhook-isolation.exe', 'frame-pump-tests.exe',
    'diagnostic-boundary-tests.exe', 'c-abi-test.exe', 'desktop-ui-smoke.exe',
    'desktop-dialogs-tests.exe', 'host-dialog-concurrency.exe')
foreach ($name in $expected) {
    if (-not (Test-Path (Join-Path $testDir $name))) {
        throw "Expected test binary missing: $(Join-Path $testDir $name)"
    }
}
Write-Host "Host-side test binaries: $testDir" -ForegroundColor Green

# Stamp the build so the runners can refuse binaries that predate a source edit. Without
# it, a PASS could come from an artifact that no longer matches the tree it claims to test.
$stamp = Join-Path $testDir 'build-stamp.txt'
"$Configuration built $([DateTime]::UtcNow.ToString('o'))" |
    Set-Content -LiteralPath $stamp -Encoding ASCII

if (-not $SkipFixtures) {
    & (Join-Path $PSScriptRoot 'build-fixtures.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "Fixture build failed with exit code $LASTEXITCODE"
    }
}
