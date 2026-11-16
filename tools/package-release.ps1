#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('auto', 'v143', 'v145')]
    [string]$PlatformToolset = 'auto',
    [switch]$BuildNative,
    [switch]$BuildFrontend,
    [ValidateSet('Ci', 'Install', 'Skip')]
    [string]$FrontendDependencies = 'Ci',
    [string]$OutputDir = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$repoRoot = [System.IO.Path]::GetFullPath($repo)
$releaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build\release'))

function Get-CanonicalPath {
    param([string]$Base, [string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'Path must not be empty.'
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Assert-PathUnder {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPrefix = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must stay under $rootPrefix; got $fullPath"
    }
    return $fullPath
}

function Assert-SafeOutputChain {
    param([string]$Root, [string]$Target)
    $current = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $targetPath = [System.IO.Path]::GetFullPath($Target)
    $chain = @($current)
    $relative = $targetPath.Substring($current.Length).TrimStart('\')
    foreach ($segment in $relative.Split('\')) {
        if ([string]::IsNullOrEmpty($segment)) { continue }
        $current = Join-Path $current $segment
        $chain += $current
    }
    foreach ($path in $chain) {
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to package through a reparse point: $path"
        }
        if (-not $item.PSIsContainer -and $path -ne $targetPath) {
            throw "Output parent is a file, not a directory: $path"
        }
    }
}

function Assert-RequiredFile {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing $Label`: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to package a reparse-point file: $Path"
    }
}

function Assert-NoReparseEntries {
    param([string]$Directory)
    $rootItem = Get-Item -LiteralPath $Directory -Force
    if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to package through a reparse point: $Directory"
    }
    foreach ($entry in @(Get-ChildItem -LiteralPath $Directory -Recurse -Force)) {
        if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Package input contains a reparse point: $($entry.FullName)"
        }
    }
}

function Assert-FrontendAssets {
    param([string]$Directory)
    Assert-NoReparseEntries -Directory $Directory
    $index = Join-Path $Directory 'index.html'
    Assert-RequiredFile -Path $index -Label 'frontend entry'

    $entries = @(Get-ChildItem -LiteralPath $Directory -Recurse -File -Force)
    foreach ($entry in $entries) {
        if ($entry.Extension -ieq '.map' -or $entry.Extension -ieq '.tsbuildinfo') {
            throw "Frontend package must not contain source maps or build info: $($entry.FullName)"
        }
        if ($entry.FullName -match '[\\/]node_modules[\\/]') {
            throw "Frontend package must not contain node_modules: $($entry.FullName)"
        }
    }
    if (-not @($entries | Where-Object { $_.Extension -ieq '.js' }).Count) {
        throw 'Frontend package contains no JavaScript asset.'
    }
    if (-not @($entries | Where-Object { $_.Extension -ieq '.css' }).Count) {
        throw 'Frontend package contains no CSS asset.'
    }

    $indexText = Get-Content -LiteralPath $index -Raw -Encoding UTF8
    foreach ($match in [regex]::Matches($indexText, '(?i)\b(?:src|href)\s*=\s*(?:"([^"]*)"|''([^'']*)'')')) {
        $value = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
        if ([string]::IsNullOrWhiteSpace($value) -or $value.StartsWith('#') -or $value.StartsWith('data:')) {
            continue
        }
        if ($value.StartsWith('/') -or $value.StartsWith('\') -or $value -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') {
            throw "Frontend entry must use relative local assets, found: $value"
        }
        $localPath = ($value -split '[?#]', 2)[0]
        $target = [System.IO.Path]::GetFullPath((Join-Path $Directory ([Uri]::UnescapeDataString($localPath))))
        $target = Assert-PathUnder -Path $target -Root $Directory -Label 'Frontend asset reference'
        Assert-RequiredFile -Path $target -Label "frontend asset referenced by index.html ($value)"
    }

    foreach ($cssFile in @($entries | Where-Object { $_.Extension -ieq '.css' })) {
        $cssText = Get-Content -LiteralPath $cssFile.FullName -Raw -Encoding UTF8
        $patterns = @(
            '(?i)url\(\s*(?:"([^"]*)"|''([^'']*)''|([^)''"\s]+))\s*\)',
            '(?i)@import\s+(?:"([^"]*)"|''([^'']*)'')'
        )
        foreach ($pattern in $patterns) {
            foreach ($match in [regex]::Matches($cssText, $pattern)) {
                $value = $null
                foreach ($groupIndex in 1..3) {
                    if ($match.Groups.Count -gt $groupIndex -and $match.Groups[$groupIndex].Success) {
                        $value = $match.Groups[$groupIndex].Value
                        break
                    }
                }
                if ([string]::IsNullOrWhiteSpace($value) -or $value.StartsWith('#') -or $value.StartsWith('data:')) {
                    continue
                }
                if ($value.StartsWith('/') -or $value.StartsWith('\') -or $value -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') {
                    throw "Frontend CSS must use relative local assets, found: $value"
                }
                $localPath = ($value -split '[?#]', 2)[0]
                $target = [System.IO.Path]::GetFullPath((Join-Path $cssFile.DirectoryName ([Uri]::UnescapeDataString($localPath))))
                $target = Assert-PathUnder -Path $target -Root $Directory -Label 'Frontend CSS asset reference'
                Assert-RequiredFile -Path $target -Label "frontend asset referenced by $($cssFile.Name) ($value)"
            }
        }
    }
}

function Assert-X64Dll {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Artifact is not a PE DLL: $Path"
    }
    $offset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 0 -or $offset -gt ($bytes.Length - 26)) {
        throw "Invalid PE header offset: $Path"
    }
    if ([BitConverter]::ToUInt32($bytes, $offset) -ne 0x00004550 -or
        [BitConverter]::ToUInt16($bytes, $offset + 4) -ne 0x8664 -or
        ([BitConverter]::ToUInt16($bytes, $offset + 22) -band 0x2000) -eq 0 -or
        [BitConverter]::ToUInt16($bytes, $offset + 24) -ne 0x020b) {
        throw "Artifact must be a Windows x64 PE32+ DLL: $Path"
    }
}

function Get-RepoRelativePath {
    param([string]$Path)
    $fullPath = Assert-PathUnder -Path $Path -Root $repoRoot -Label 'Repository input'
    return $fullPath.Substring($repoRoot.TrimEnd('\').Length + 1).Replace('\', '/')
}

function Copy-PayloadFile {
    param([string]$Source, [string]$Destination)
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
        throw "Copied payload hash mismatch: $Destination"
    }
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $packageName = 'GakuLoader-WebView2-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + ([guid]::NewGuid().ToString('N').Substring(0, 8))
    $outputPath = Join-Path $releaseRoot $packageName
}
else {
    $outputPath = Get-CanonicalPath -Base $repoRoot -Path $OutputDir
}
if ([string]::Equals($outputPath.TrimEnd('\'), $releaseRoot.TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDir must be a new child directory under build/release, not build/release itself.'
}
$outputPath = Assert-PathUnder -Path $outputPath -Root $releaseRoot -Label 'OutputDir'
if (Test-Path -LiteralPath $outputPath) {
    throw "OutputDir already exists; existing directories are never overwritten: $outputPath"
}
Assert-SafeOutputChain -Root $repoRoot -Target $outputPath

$loaderSource = Join-Path $repoRoot 'deps\webview2\x64\WebView2Loader.dll'
$nativeRelative = if ($Configuration -eq 'Release') { 'build\version.dll' } else { 'build\debug\version.dll' }
$nativeDll = Join-Path $repoRoot $nativeRelative
$frontendRoot = Join-Path $repoRoot 'frontend'
$frontendDist = Join-Path $frontendRoot 'dist'
$frontendBuildScript = Join-Path $frontendRoot 'build.ps1'
$nativeBuildScript = Join-Path $repoRoot 'build.ps1'
$packageLock = Join-Path $frontendRoot 'package-lock.json'
$viteConfig = Join-Path $frontendRoot 'vite.config.ts'

Assert-RequiredFile -Path $loaderSource -Label 'vendored WebView2Loader.dll'
Assert-RequiredFile -Path $packageLock -Label 'frontend package-lock.json'
Assert-RequiredFile -Path $viteConfig -Label 'frontend vite.config.ts'
if ($BuildNative) {
    Assert-RequiredFile -Path $nativeBuildScript -Label 'native build script'
}
if ($BuildFrontend) {
    Assert-RequiredFile -Path $frontendBuildScript -Label 'frontend build script'
}

$legalPayload = [ordered]@{
    'THIRD_PARTY_NOTICES.md' = (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md')
    'LICENSE' = (Join-Path $repoRoot 'LICENSE')
    'LICENSES/GakuLoader-Plugin-Exception.txt' = (Join-Path $repoRoot 'LICENSES\GakuLoader-Plugin-Exception.txt')
    'deps/webview2/LICENSE.txt' = (Join-Path $repoRoot 'deps\webview2\LICENSE.txt')
    'deps/webview2/NOTICE.txt' = (Join-Path $repoRoot 'deps\webview2\NOTICE.txt')
    'deps/webview2/PROVENANCE.md' = (Join-Path $repoRoot 'deps\webview2\PROVENANCE.md')
    'deps/nlohmann/LICENSE.MIT' = (Join-Path $repoRoot 'deps\nlohmann\LICENSE.MIT')
    'deps/nlohmann/PROVENANCE.md' = (Join-Path $repoRoot 'deps\nlohmann\PROVENANCE.md')
    'deps/imgui/LICENSE.txt' = (Join-Path $repoRoot 'deps\imgui\LICENSE.txt')
    'deps/imgui/PROVENANCE.md' = (Join-Path $repoRoot 'deps\imgui\PROVENANCE.md')
    'deps/minhook/LICENSE.txt' = (Join-Path $repoRoot 'deps\minhook\LICENSE.txt')
    'deps/minhook/PROVENANCE.md' = (Join-Path $repoRoot 'deps\minhook\PROVENANCE.md')
}
foreach ($relative in $legalPayload.Keys) {
    Assert-RequiredFile -Path $legalPayload[$relative] -Label "license/provenance payload $relative"
}

if ($BuildNative) {
    & $nativeBuildScript -Configuration $Configuration -PlatformToolset $PlatformToolset
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed with exit code $LASTEXITCODE"
    }
}
if ($BuildFrontend) {
    & $frontendBuildScript -Dependencies $FrontendDependencies
    if ($LASTEXITCODE -ne 0) {
        throw "Frontend build failed with exit code $LASTEXITCODE"
    }
}

Assert-RequiredFile -Path $nativeDll -Label "$Configuration version.dll"
Assert-X64Dll -Path $nativeDll
Assert-X64Dll -Path $loaderSource
Assert-FrontendAssets -Directory $frontendDist

. (Join-Path $repoRoot 'tools\msbuild-env.ps1')
Clear-GakuPathDuplicate
$buildTools = Resolve-GakuBuildTools -PlatformToolset $PlatformToolset
$dumpbin = Join-Path (Split-Path -Parent $buildTools.Compiler) 'dumpbin.exe'
Assert-RequiredFile -Path $dumpbin -Label 'dumpbin.exe'

$global:LASTEXITCODE = 0
& (Join-Path $repoRoot 'tools\test-package.ps1') -Dll $nativeDll
if ($LASTEXITCODE -ne 0) {
    throw "Native package contract check failed with exit code $LASTEXITCODE"
}

$global:LASTEXITCODE = 0
$loaderExports = & $dumpbin /exports $loaderSource | Out-String
if ($LASTEXITCODE -ne 0) {
    throw 'Cannot read WebView2Loader.dll exports.'
}
foreach ($exportName in @('GetAvailableCoreWebView2BrowserVersionString', 'CreateCoreWebView2EnvironmentWithOptions')) {
    if ($loaderExports -notmatch ('\b' + [regex]::Escape($exportName) + '\b')) {
        throw "WebView2Loader.dll is missing required export: $exportName"
    }
}

$revision = (& git -C $repoRoot rev-parse HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
    throw 'Cannot resolve source HEAD for the package manifest.'
}
$statusLines = @(& git -C $repoRoot status --porcelain --untracked-files=all 2>$null)
$sourceDirty = $statusLines.Count -gt 0

if (-not (Test-Path -LiteralPath $releaseRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
}
New-Item -ItemType Directory -Path $outputPath | Out-Null

Copy-PayloadFile -Source $nativeDll -Destination (Join-Path $outputPath 'version.dll')
Copy-PayloadFile -Source $loaderSource -Destination (Join-Path $outputPath 'WebView2Loader.dll')
foreach ($relative in $legalPayload.Keys) {
    $destination = Join-Path $outputPath $relative.Replace('/', '\')
    Copy-PayloadFile -Source $legalPayload[$relative] -Destination $destination
}

$assetRoot = Join-Path $outputPath 'GakuMotion.UI'
$distPrefix = [System.IO.Path]::GetFullPath($frontendDist).TrimEnd('\')
foreach ($asset in @(Get-ChildItem -LiteralPath $frontendDist -Recurse -File -Force)) {
    $relative = $asset.FullName.Substring($distPrefix.Length + 1)
    Copy-PayloadFile -Source $asset.FullName -Destination (Join-Path $assetRoot $relative)
}
Assert-FrontendAssets -Directory $assetRoot

$installText = @'
GakuLoader + GakuMotion WebView2 local staging package

This package contains the host proxy, the app-local WebView2 SDK loader, and complete offline
GakuMotion.UI assets. It contains no game assets, plugins, personal configuration, node_modules,
source maps, or PDB files. It is unsigned local staging output, not a published release.

The GakuLoader Manager remains ImGui. GakuMotion.UI is rendered only inside the GakuMotion plugin
content area of the independent Manager window. Legacy ImGui plugins are not routed to the game
window by this package.

Manual install layout (the packaging script never deploys):
  <game root>/version.dll
  <game root>/WebView2Loader.dll
  <game root>/GakuMotion.UI/index.html
  <game root>/GakuMotion.UI/assets/...
  <game root>/GakuLoader/Configs/gakuloader.ini   optional host settings
  <game root>/GakuLoader/WebView2/                browser user data, created at runtime

Close the game and independently back up any existing version.dll, WebView2Loader.dll, and
GakuMotion.UI directory before replacing them. Keep the two DLLs and GakuMotion.UI together beside
gakumas.exe. Existing plugins and user data are not included or migrated by this package.

The target machine needs the Microsoft Edge WebView2 Evergreen Runtime installed. The bundled
WebView2Loader.dll is only the SDK loader. [host] ui=imgui disables plugin web panels and keeps the
legacy ImGui plugin panels. Missing runtime, loader, or UI assets produce a warning and an ImGui
fallback inside the Manager content area. The Motion UI is served offline from the local virtual
host https://gakumotion.invalid/index.html; no CDN or game-network access is required by these assets.

package-manifest.json records the payload SHA256 hashes, source HEAD, dirty-worktree state, and
whether this invocation rebuilt the native/frontend inputs. Verify hashes before manual installation.
'@
[System.IO.File]::WriteAllText((Join-Path $outputPath 'INSTALL.txt'), $installText, [System.Text.Encoding]::ASCII)

$payloadFiles = @(Get-ChildItem -LiteralPath $outputPath -Recurse -File -Force | Sort-Object FullName)
$fileRecords = foreach ($file in $payloadFiles) {
    [ordered]@{
        path = $file.FullName.Substring($outputPath.TrimEnd('\').Length + 1).Replace('\', '/')
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$manifest = [ordered]@{
    schema = 'gakuloader.webview2-release/1'
    package = Split-Path -Leaf $outputPath
    created_utc = [DateTime]::UtcNow.ToString('o')
    source_commit = $revision
    source_dirty = $sourceDirty
    configuration = $Configuration
    build = [ordered]@{
        native_invoked = [bool]$BuildNative
        frontend_invoked = [bool]$BuildFrontend
        frontend_dependencies = if ($BuildFrontend) { $FrontendDependencies } else { 'not invoked' }
        platform_toolset_requested = $PlatformToolset
        platform_toolset_resolved = $buildTools.PlatformToolset
        msvc_version = $buildTools.Version
    }
    inputs = [ordered]@{
        native_dll = [ordered]@{ path = Get-RepoRelativePath -Path $nativeDll; sha256 = (Get-FileHash -LiteralPath $nativeDll -Algorithm SHA256).Hash.ToLowerInvariant() }
        webview2_loader = [ordered]@{ path = Get-RepoRelativePath -Path $loaderSource; sha256 = (Get-FileHash -LiteralPath $loaderSource -Algorithm SHA256).Hash.ToLowerInvariant() }
        frontend_index = [ordered]@{ path = Get-RepoRelativePath -Path (Join-Path $frontendDist 'index.html'); sha256 = (Get-FileHash -LiteralPath (Join-Path $frontendDist 'index.html') -Algorithm SHA256).Hash.ToLowerInvariant() }
        frontend_package_lock = [ordered]@{ path = Get-RepoRelativePath -Path $packageLock; sha256 = (Get-FileHash -LiteralPath $packageLock -Algorithm SHA256).Hash.ToLowerInvariant() }
        vite_config = [ordered]@{ path = Get-RepoRelativePath -Path $viteConfig; sha256 = (Get-FileHash -LiteralPath $viteConfig -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    runtime = [ordered]@{
        module_directory_assets = 'GakuMotion.UI'
        entry_url = 'https://gakumotion.invalid/index.html'
        loader = 'WebView2Loader.dll'
        prerequisite = 'Microsoft Edge WebView2 Evergreen Runtime installed on the target machine'
        fallback = '[host] ui=imgui disables plugin web panels; Manager remains ImGui'
        browser_user_data = 'GakuLoader/WebView2'
    }
    checks = @(
        'version.dll: x64 PE32+ DLL, version proxy exports, and no dynamic CRT dependency via tools/test-package.ps1',
        'WebView2Loader.dll: x64 PE32+ DLL and required dynamic-load exports',
        'GakuMotion.UI: index.html, JavaScript/CSS assets, relative local HTML references, no node_modules/source maps/build info',
        'Every copied payload file matched its source SHA256'
    )
    excluded = @('node_modules', 'source maps', 'tsbuildinfo', 'PDB', 'game assets', 'plugins', 'personal configurations', 'WebView2 Evergreen Runtime')
    limits = 'Unsigned local staging package. Hashes and structural checks are not game loading, rendered UI, listening, publication, or bitwise reproducibility attestation. When native_invoked/frontend_invoked is false, the corresponding input is a supplied local build artifact, not rebuilt by this invocation.'
    files = @($fileRecords)
}
$manifestPath = Join-Path $outputPath 'package-manifest.json'
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Packaged GakuLoader + GakuMotion UI release: $outputPath" -ForegroundColor Green
Write-Host ("Payload files: $($payloadFiles.Count); manifest: $manifestPath") -ForegroundColor Green
Write-Host ('Manifest SHA256: ' + (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant())
Write-Host 'Local staging only; no deployment, game launch, or runtime UI acceptance was performed.' -ForegroundColor Yellow
