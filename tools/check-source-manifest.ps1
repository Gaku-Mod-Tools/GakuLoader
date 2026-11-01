#Requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $repo 'SOURCE_MANIFEST.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$prefix = [IO.Path]::GetFullPath($repo).TrimEnd('\') + '\'
$expected = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($file in $manifest.files) {
    if (-not $expected.Add([string]$file.path)) { throw "Duplicate manifest path: $($file.path)" }
    $path = [IO.Path]::GetFullPath((Join-Path $repo $file.path))
    if (-not $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes repository.' }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing frozen source: $($file.path)" }
    if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Source links are not allowed.' }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {
        throw "Frozen source hash mismatch: $($file.path)"
    }
}
function Check-SourceDirectory([string]$Directory) {
    foreach ($item in (Get-ChildItem -LiteralPath $Directory -Force)) {
        $relative = $item.FullName.Substring($prefix.Length).Replace('\', '/')
        if ($relative -in @('.git', 'build')) { continue }
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Unexpected source link: $relative" }
        if ($item.PSIsContainer) { Check-SourceDirectory $item.FullName; continue }
        if ($relative -in @('SOURCE_MANIFEST.json', 'VALIDATION.md')) { continue }
        if (-not $expected.Contains($relative)) { throw "Unreviewed file outside build/: $relative" }
    }
}
Check-SourceDirectory $repo
Write-Output "PASS: $($manifest.files.Count) frozen source/document/vendor files match SOURCE_MANIFEST.json; no unreviewed payload."
