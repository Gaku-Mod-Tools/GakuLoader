#Requires -Version 5.1
[CmdletBinding()]
param([switch]$StaticOnly)
# F4 guard: the two guides that a new consumer reads must describe the current
# ABI, and the example they point at must actually compile and export the entry
# point. HOST_MIGRATION.md is intentionally excluded: it is the historical record
# of the first migration stage, not an onboarding document.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $repo 'tests\test-common.ps1')
$failures = 0
function Check([bool]$Condition, [string]$Message) {
    if ($Condition) { Write-Output "ok: $Message" }
    else { Write-Output "FAIL: $Message"; $script:failures++ }
}

# --- Guides must not teach the removed ABI -----------------------------------
$retired = @(
    'GmlPlugin', 'GmlHostApi', 'GmlPluginDescriptor', 'GmlStatus', 'GmlLogService',
    'GML_ABI_VERSION', 'GML_SERVICE_', 'GML_OK', 'GML_PLUGIN_EXPORT',
    'legacy_chainload', 'gml.log', 'GakumasModPlugin.h'
)
foreach ($doc in @('docs\PLUGIN_DEVELOPMENT.md', 'docs\QUICKSTART.md')) {
    $path = Join-Path $repo $doc
    Check (Test-Path -LiteralPath $path) "$doc exists"
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $text = Get-Content -LiteralPath $path -Raw
    foreach ($symbol in $retired) {
        Check (-not $text.Contains($symbol)) "$doc no longer references $symbol"
    }
    Check ($text.Contains('GakuPlugin.h')) "$doc points at GakuPlugin.h"
}

# --- README must identify the separate downstream plugins ----------------------
$readme = Get-Content -LiteralPath (Join-Path $repo 'README.md') -Raw
Check ($readme.Contains('GakuLocalify.dll')) 'README bundle list names GakuLocalify.dll'
Check ($readme.Contains('GakuMotion.dll')) 'README bundle list names GakuMotion.dll'

# --- The guide's code block must be the file that gets compiled ---------------
$guide = Get-Content -LiteralPath (Join-Path $repo 'docs\PLUGIN_DEVELOPMENT.md') -Raw
$match = [regex]::Match($guide, '(?s)```cpp\r?\n(.*?)\r?\n```')
Check $match.Success 'PLUGIN_DEVELOPMENT.md contains the minimal plugin block'
$examplePath = Join-Path $repo 'examples\minimal-plugin\MinimalPlugin.cpp'
$example = (Get-Content -LiteralPath $examplePath -Raw) -replace "`r`n", "`n"
if ($match.Success) {
    $documented = ($match.Groups[1].Value -replace "`r`n", "`n").TrimEnd("`n")
    Check ($documented -eq $example.TrimEnd("`n")) `
        'the documented plugin and examples/minimal-plugin/MinimalPlugin.cpp are identical'
}

# --- Error comments must match the implemented policy ------------------------
$diagnostics = Get-Content -LiteralPath (Join-Path $repo 'src\DiagnosticBoundary.cpp') -Raw
Check (-not $diagnostics.Contains('startup refused')) `
    'DiagnosticBoundary no longer claims startup is refused'
$quitGuard = Get-Content -LiteralPath (Join-Path $repo 'src\QuitGuard.cpp') -Raw
Check (-not $quitGuard.Contains('会让出插件')) 'QuitGuard no longer describes the legacy hand-off'

# --- Independently build and drive the documented example with MSVC -----------
# No MinGW fixtures or host test build is required for a first-time consumer.
if (-not $StaticOnly) {
    & (Join-Path $repo 'examples\minimal-plugin\verify.ps1')
    Check ($LASTEXITCODE -eq 0) 'MSVC sample negotiates ABI v1 and emits activation lifecycle logs'
    $dll = Join-Path $repo 'build\examples\minimal-plugin\Release\MinimalPlugin.dll'
    $dumpbin = Resolve-GakuDumpbin
    $exports = & $dumpbin /exports $dll | Out-String
    Check ($LASTEXITCODE -eq 0 -and $exports.Contains('GakuPlugin_Query')) 'the example exports GakuPlugin_Query'
} else {
    Write-Output 'SKIP: -StaticOnly excludes MSVC example build, activation and export verification.'
}

# Reject commands that survived the repository split in current onboarding docs.
foreach ($doc in @('README.md', 'README.en.md', 'README.ja.md', 'docs\QUICKSTART.md',
    'docs\PLUGIN_DEVELOPMENT.md', 'wiki\Building.md', 'wiki\Installation.md')) {
    $text = Get-Content -LiteralPath (Join-Path $repo $doc) -Raw -Encoding UTF8
    Check (-not ($text -match 'build\.ps1\s+-SkipDeploy')) "$doc uses supported build parameters"
    Check (-not $text.Contains('GakuModLoader.slnx')) "$doc uses the current solution name"
}

# Check repository-owned Markdown link targets without fetching remote pages.
$docs = @(Get-ChildItem -LiteralPath $repo -Filter '*.md' -File) +
    @(Get-ChildItem -LiteralPath (Join-Path $repo 'docs') -Filter '*.md' -File -Recurse) +
    @(Get-ChildItem -LiteralPath (Join-Path $repo 'wiki') -Filter '*.md' -File -Recurse)
foreach ($doc in $docs) {
    $text = Get-Content -LiteralPath $doc.FullName -Raw -Encoding UTF8
    $text = [regex]::Replace($text, '(?ms)^```[^\r\n]*\r?\n.*?^```[^\r\n]*', '')
    foreach ($link in [regex]::Matches($text, '\[[^\]]*\]\(([^\s)]+)\)')) {
        $target = $link.Groups[1].Value
        if ($target -match '^(?:[a-zA-Z][a-zA-Z0-9+.-]*:|#)') { continue }
        $target = [Uri]::UnescapeDataString(($target -split '#', 2)[0])
        Check (Test-Path -LiteralPath (Join-Path $doc.DirectoryName $target)) ("$($doc.Name) link target: $target")
    }
}

if ($failures) { throw "Documentation/ABI check failed: $failures item(s)" }
if ($StaticOnly) { Write-Output 'PASS: static documentation, source consistency and local link checks only.' }
else { Write-Output 'PASS: guides describe ABI v1; MSVC minimal plugin activation and export checks passed.' }
