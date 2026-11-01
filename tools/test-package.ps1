#Requires -Version 5.1
[CmdletBinding()]
param([string]$Dll = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $repo 'tools\msbuild-env.ps1')
Clear-GakuPathDuplicate
if ([string]::IsNullOrWhiteSpace($Dll)) { $Dll = Join-Path $repo 'build\version.dll' }
if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) { throw 'Missing build/version.dll. Run build.ps1 first.' }
$bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Dll).Path)
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Artifact is not a PE DLL.' }
$offset = [BitConverter]::ToInt32($bytes, 0x3c)
if ($offset -lt 0 -or $offset -gt ($bytes.Length - 24) -or
    [BitConverter]::ToUInt32($bytes, $offset) -ne 0x00004550 -or
    [BitConverter]::ToUInt16($bytes, $offset + 4) -ne 0x8664 -or
    ([BitConverter]::ToUInt16($bytes, $offset + 22) -band 0x2000) -eq 0) {
    throw 'Artifact must be a Windows x64 DLL.'
}
$dumpbin = Resolve-GakuDumpbin
$exports = & $dumpbin /exports $Dll | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Cannot read DLL exports.' }
$source = Get-Content -LiteralPath (Join-Path $repo 'src\Proxy.cpp') -Raw
$expected = @([regex]::Matches($source, '__declspec\(dllexport\)\s+\w+\s+WINAPI\s+(\w+)\(') |
    ForEach-Object { $_.Groups[1].Value })
if ($expected.Count -lt 10) { throw 'Proxy source export scan is incomplete.' }
foreach ($name in $expected) {
    if ($exports -notmatch ('(?m)\s' + [regex]::Escape($name) + '\s*$')) { throw "Missing proxy export: $name" }
}
$dependents = & $dumpbin /dependents $Dll | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Cannot read DLL dependencies.' }
if ($dependents -match '(?i)\b(?:vcruntime|msvcp|ucrtbase|ucrtbased)[^\s]*\.dll') {
    throw "Dynamic CRT dependency violates the /MT package contract: $($Matches[0])"
}
Write-Output "PASS: x64 DLL; $($expected.Count) version proxy exports; no dynamic CRT dependency."
Write-Output ('SHA256: ' + (Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash)
Write-Output 'Local artifact check only; no deployment, package publication or game verification was performed.'
