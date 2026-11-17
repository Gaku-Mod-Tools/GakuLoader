#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Ci', 'Install', 'Skip')]
    [string]$Dependencies = 'Ci'
)

$ErrorActionPreference = 'Stop'
$frontendRoot = $PSScriptRoot
$packageJson = Join-Path $frontendRoot 'package.json'
$packageLock = Join-Path $frontendRoot 'package-lock.json'
$viteConfig = Join-Path $frontendRoot 'vite.config.ts'
$distDir = Join-Path $frontendRoot 'dist'

foreach ($required in @($packageJson, $packageLock, $viteConfig)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing frontend input: $required"
    }
}

$frontendItem = Get-Item -LiteralPath $frontendRoot -Force
if (($frontendItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Refusing to build through a reparse point: $frontendRoot"
}

$npmCommand = Get-Command npm.cmd -ErrorAction SilentlyContinue
if (-not $npmCommand) {
    throw 'npm.cmd was not found. Install Node.js/npm and make npm.cmd available on PATH.'
}

$viteText = Get-Content -LiteralPath $viteConfig -Raw
if ($viteText -notmatch "base\s*:\s*['""]\.\/['""]") {
    throw "vite.config.ts must keep base './' for offline virtual-host assets."
}

function Assert-ProductionAssets {
    param([string]$Directory)

    $index = Join-Path $Directory 'index.html'
    if (-not (Test-Path -LiteralPath $index -PathType Leaf)) {
        throw "Production build is missing index.html: $index"
    }

    $entries = @(Get-ChildItem -LiteralPath $Directory -Recurse -File -Force)
    foreach ($entry in $entries) {
        if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Production assets contain a reparse point: $($entry.FullName)"
        }
        if ($entry.Extension -ieq '.map' -or $entry.Extension -ieq '.tsbuildinfo') {
            throw "Production assets must not contain source maps or build info: $($entry.FullName)"
        }
        if ($entry.FullName -match '[\\/]node_modules[\\/]') {
            throw "Production assets must not contain node_modules: $($entry.FullName)"
        }
    }
    if (-not @($entries | Where-Object { $_.Extension -ieq '.js' }).Count) {
        throw 'Production build produced no JavaScript asset.'
    }
    if (-not @($entries | Where-Object { $_.Extension -ieq '.css' }).Count) {
        throw 'Production build produced no CSS asset.'
    }

    $indexText = Get-Content -LiteralPath $index -Raw -Encoding UTF8
    foreach ($match in [regex]::Matches($indexText, '(?i)\b(?:src|href)\s*=\s*(?:"([^"]*)"|''([^'']*)'')')) {
        $value = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
        if ([string]::IsNullOrWhiteSpace($value) -or $value.StartsWith('#') -or $value.StartsWith('data:')) {
            continue
        }
        if ($value.StartsWith('/') -or $value.StartsWith('\') -or $value -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') {
            throw "Production index.html must use relative local assets, found: $value"
        }
        $localPath = ($value -split '[?#]', 2)[0]
        $target = [System.IO.Path]::GetFullPath((Join-Path $Directory ([Uri]::UnescapeDataString($localPath))))
        $rootPrefix = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
        if (-not $target.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Production asset reference escapes the output directory: $value"
        }
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "Production index.html references a missing asset: $value"
        }
    }
}

$savedEnvironment = @{}
foreach ($name in @('CI', 'NPM_CONFIG_UPDATE_NOTIFIER', 'NPM_CONFIG_AUDIT', 'NPM_CONFIG_FUND')) {
    $savedEnvironment[$name] = [System.Environment]::GetEnvironmentVariable($name, 'Process')
}

Push-Location -LiteralPath $frontendRoot
try {
    [System.Environment]::SetEnvironmentVariable('CI', '1', 'Process')
    [System.Environment]::SetEnvironmentVariable('NPM_CONFIG_UPDATE_NOTIFIER', 'false', 'Process')
    [System.Environment]::SetEnvironmentVariable('NPM_CONFIG_AUDIT', 'false', 'Process')
    [System.Environment]::SetEnvironmentVariable('NPM_CONFIG_FUND', 'false', 'Process')

    if ($Dependencies -ne 'Skip') {
        $npmVerb = if ($Dependencies -eq 'Ci') { 'ci' } else { 'install' }
        Write-Host "Installing frontend dependencies with npm $npmVerb (locked inputs, lifecycle scripts disabled)..." -ForegroundColor Cyan
        $global:LASTEXITCODE = 0
        & $npmCommand.Source $npmVerb '--ignore-scripts' '--no-audit' '--no-fund'
        if ($LASTEXITCODE -ne 0) {
            throw "npm $npmVerb failed with exit code $LASTEXITCODE"
        }
    }

    foreach ($tool in @('node_modules\.bin\tsc.cmd', 'node_modules\.bin\vite.cmd')) {
        $toolPath = Join-Path $frontendRoot $tool
        if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
            throw "Missing frontend tool: $toolPath. Run frontend/build.ps1 with -Dependencies Ci."
        }
    }

    Write-Host 'Building production frontend (tsc -b && vite build)...' -ForegroundColor Cyan
    $global:LASTEXITCODE = 0
    & $npmCommand.Source run build
    if ($LASTEXITCODE -ne 0) {
        throw "npm run build failed with exit code $LASTEXITCODE"
    }

    Assert-ProductionAssets -Directory $distDir
    $assetCount = @(Get-ChildItem -LiteralPath $distDir -Recurse -File).Count
    Write-Host "Successfully built offline frontend: $distDir ($assetCount files)" -ForegroundColor Green
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        [System.Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    Pop-Location
}
