#Requires -Version 5.1
# Shared MSBuild discovery for build.ps1 and tests\build-tests.ps1.
# Dot-source this file; it defines shared functions and executes nothing on its own.
#
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).

function Clear-GakuPathDuplicate {
    <#
      MSBuild's ToolTask stores the child environment in a case-sensitive dictionary and
      aborts with MSB6001 ("key already added: Path") when the process block carries more
      than one casing of Path (Path + PATH + path). Collapse them before launching anything.
    #>
    $variants = @([System.Environment]::GetEnvironmentVariables('Process').Keys |
        Where-Object { $_ -ieq 'Path' })
    if ($variants.Count -gt 1) {
        $value = $env:Path
        foreach ($name in $variants) {
            [System.Environment]::SetEnvironmentVariable($name, $null)
        }
        [System.Environment]::SetEnvironmentVariable('Path', $value)
    }
    # Empty entries survive as duplicate separators and confuse the compiler tool lookup.
    if ($env:Path -match ';;') {
        $env:Path = ($env:Path -split ';' | Where-Object { $_ } | Select-Object -Unique) -join ';'
    }
}

function Resolve-GakuBuildTools {
    param(
        [ValidateSet('auto', 'v143', 'v145')]
        [string]$PlatformToolset = 'auto'
    )
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2022+ with Desktop development with C++ and a Windows SDK."
    }
    $installations = @(& $vswhere -all -sort -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio discovery failed.' }
    $requested = if ($PlatformToolset -eq 'auto') { @('v145', 'v143') } else { @($PlatformToolset) }
    foreach ($vsPath in $installations) {
        $msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
        if (-not (Test-Path -LiteralPath $msbuild)) { continue }
        foreach ($toolset in $requested) {
            $vcRoots = @(Get-ChildItem -LiteralPath (Join-Path $vsPath 'MSBuild\Microsoft\VC') -Directory -ErrorAction SilentlyContinue)
            $hasTargets = @($vcRoots | Where-Object {
                Test-Path -LiteralPath (Join-Path $_.FullName "Platforms\x64\PlatformToolsets\$toolset\Toolset.props")
            }).Count -gt 0
            if (-not $hasTargets) { continue }
            $versionsRoot = Join-Path $vsPath 'VC\Tools\MSVC'
            $versions = @(Get-ChildItem -LiteralPath $versionsRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^14\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending)
            foreach ($installedVersion in $versions) {
                $version = $installedVersion.Name
                $compatible = if ($toolset -eq 'v145') { $version -match '^14\.5[0-9]\.' } else { $version -match '^14\.[34][0-9]\.' }
                if (-not $compatible) { continue }
                $compiler = Join-Path $installedVersion.FullName 'bin\Hostx64\x64\cl.exe'
                if (-not (Test-Path -LiteralPath $compiler)) { continue }
                return [pscustomobject]@{
                    MSBuild = $msbuild
                    PlatformToolset = $toolset
                    Compiler = $compiler
                    Version = $version
                }
            }
        }
    }
    throw "No installed MSVC x64 toolset satisfies '$PlatformToolset'. Install v143 (VS2022) or v145 (VS2026) with a Windows SDK, or select -PlatformToolset explicitly."
}

function Resolve-GakuMsBuild {
    return (Resolve-GakuBuildTools).MSBuild
}

function Resolve-GakuDumpbin {
    <#
      Returns dumpbin.exe from the newest MSVC toolset of the same Visual Studio install.
      Used to assert exported symbols without depending on a MinGW binutils install.
    #>
    $msbuild = Resolve-GakuMsBuild
    # <VS>\MSBuild\Current\Bin\MSBuild.exe -> <VS> is four levels up, not three.
    $vsPath = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $msbuild)))
    $toolsRoot = Join-Path $vsPath 'VC\Tools\MSVC'
    if (-not (Test-Path $toolsRoot)) {
        throw "MSVC tools directory not found under: $vsPath"
    }
    $toolset = Get-ChildItem -LiteralPath $toolsRoot -Directory |
        Sort-Object { [version]($_.Name -replace '[^0-9.]', '') } -Descending |
        Select-Object -First 1
    $dumpbin = Join-Path $toolset.FullName 'bin\Hostx64\x64\dumpbin.exe'
    if (-not (Test-Path $dumpbin)) {
        throw "dumpbin.exe not found under: $($toolset.FullName)"
    }
    return $dumpbin
}
