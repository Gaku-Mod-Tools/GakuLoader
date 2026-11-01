#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('auto', 'v143', 'v145')][string]$PlatformToolset = 'auto'
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -PlatformToolset $PlatformToolset
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$output = Join-Path $repo "build\examples\minimal-plugin\$Configuration"
$verifier = Join-Path $output 'verify-minimal-plugin.exe'
$plugin = Join-Path $output 'MinimalPlugin.dll'
$log = Join-Path $output 'activation.log'
& $verifier $plugin 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Minimal plugin activation check failed: $LASTEXITCODE" }
Write-Output "PASS: standalone DLL negotiation and lifecycle; synthetic host log: $log"
