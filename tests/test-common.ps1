#Requires -Version 5.1
# Shared artifact lookup for every test runner.
#
# After the MSVC migration no test script compiles anything: tests\build-tests.ps1 is the
# only place that produces binaries, and this file is how the runners find them. Keeping
# that rule in one place is the reason the scripts no longer carry toolchain paths.
#
# NOTE: Keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as ANSI).

# Some shells hand PowerShell an environment block where Path / PATH / path coexist. Any
# child-process launch (Start-Process, MSBuild) then dies with "dictionary key Path
# conflicts with PATH", so normalise it once here instead of in every runner.
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\msbuild-env.ps1')
Clear-GakuPathDuplicate

function Get-GakuTestDir {
    param([string]$Repo, [string]$Configuration = 'Release')
    $dir = Join-Path $Repo "build\tests\$Configuration"
    if (-not (Test-Path -LiteralPath $dir)) {
        throw "No MSVC test binaries at $dir. Run tests\build-tests.ps1 -Configuration $Configuration first."
    }
    return $dir
}

function Get-GakuSourceStamp {
    <#
      Newest write time across the inputs that feed the test binaries. Used to refuse a
      stale artifact: a PASS from a binary older than its sources is worse than no result.
    #>
    param([string]$Repo)
    $roots = @('src', 'tests', 'include', 'deps\imgui', 'deps\minhook')
    $newest = [DateTime]::MinValue
    foreach ($root in $roots) {
        $dir = Join-Path $Repo $root
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        $files = Get-ChildItem -LiteralPath $dir -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -in @('.c', '.cpp', '.h', '.hpp', '.cxx') }
        foreach ($file in $files) {
            if ($file.LastWriteTime -gt $newest) { $newest = $file.LastWriteTime }
        }
    }
    return $newest
}

function Get-GakuBuildStamp {
    <#
      Timestamp of the last successful tests\build-tests.ps1 run for this configuration.

      Comparing a binary against "every source file in the repo" would flag untouched
      projects (editing DesktopUiSmoke.cpp is not a reason to call c-abi-test.exe stale,
      because MSBuild correctly did not relink it). The stamp answers the question that
      actually matters: has any source changed since the build that produced these
      binaries?
    #>
    param([string]$Repo, [string]$Configuration = 'Release')
    $stamp = Join-Path $Repo "build\tests\$Configuration\build-stamp.txt"
    if (-not (Test-Path -LiteralPath $stamp)) {
        throw ("No build stamp at $stamp. Run tests\build-tests.ps1 -Configuration $Configuration " +
            'so the suite runs binaries it can vouch for.')
    }
    return (Get-Item -LiteralPath $stamp).LastWriteTime
}

function Get-GakuTestBinary {
    param(
        [string]$Repo,
        [string]$Name,
        [string]$Configuration = 'Release',
        [switch]$AllowStale
    )
    $path = Join-Path (Get-GakuTestDir $Repo $Configuration) $Name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing test binary: $path. Run tests\build-tests.ps1 -Configuration $Configuration."
    }
    if (-not $AllowStale) {
        $sourceStamp = Get-GakuSourceStamp $Repo
        $buildStamp = Get-GakuBuildStamp $Repo $Configuration
        if ($sourceStamp -gt $buildStamp) {
            throw ("Sources changed after the last build (newest source $($sourceStamp.ToString('s')), " +
                "build $($buildStamp.ToString('s'))), so $Name may not reflect them. " +
                'Re-run tests\build-tests.ps1.')
        }
    }
    return $path
}

function Get-GakuFixture {
    param([string]$Repo, [string]$Name)
    $path = Join-Path $Repo "build\fixtures\$Name"
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing MinGW fixture: $path. Run tests\build-fixtures.ps1."
    }
    return $path
}

function Initialize-GakuTestRoot {
    <#
      Creates (or reuses) the runtime root for one suite.

      Deliberately does not delete the tree: only files this suite owns are removed, and
      the tree itself is left alone. Anything unexpected is reported by
      Assert-GakuPluginDir instead of being silently wiped, because a stray plugin DLL is
      exactly the kind of leftover that changes what a fixture is proving.
    #>
    param([string]$Path)
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    return $Path
}

function Reset-GakuLog {
    <#
      Truncates a log instead of deleting it: the host appends, so an empty file is the
      deterministic starting state, and truncation cannot collide with the workspace's
      delete guards.
    #>
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        [IO.File]::WriteAllText($Path, '')
    }
}

function Assert-GakuPluginDir {
    <#
      Ensures a plugin directory contains exactly the expected fixtures. Copy-Item -Force
      replaces the ones we name; anything else is a leftover from an earlier run (or a
      real deployment) and would make "exactly one plugin is resident" non-deterministic,
      so the suite refuses to run instead of quietly deleting it.
    #>
    param([string]$Path, [string[]]$Expected)
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    # Only DLLs count: the fixture also writes evidence files (legacy-dllmain.txt) into
    # this directory, and those are the fixture's own output, not a stale plugin.
    $present = @(Get-ChildItem -LiteralPath $Path -File -Filter '*.dll' -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Name)
    $unexpected = @($present | Where-Object { $Expected -notcontains $_ })
    if ($unexpected.Count -gt 0) {
        throw ("Unexpected files in $Path : $($unexpected -join ', '). " +
            'Remove them so the fixture sees exactly the plugins it expects.')
    }
    return $Path
}

function Copy-GakuFixtures {
    param([string]$Repo, [string]$Destination, [string[]]$Names)
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $copied = @()
    foreach ($name in $Names) {
        $source = Get-GakuFixture $Repo $name
        Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $name) -Force
        $copied += (Join-Path $Destination $name)
    }
    return $copied
}

function Get-GakuFileHash {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
