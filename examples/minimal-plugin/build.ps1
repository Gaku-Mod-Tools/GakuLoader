#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('auto', 'v143', 'v145')][string]$PlatformToolset = 'auto'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools\msbuild-env.ps1')
Clear-GakuPathDuplicate
$tools = Resolve-GakuBuildTools -PlatformToolset $PlatformToolset
foreach ($name in @('MinimalPlugin', 'VerifyMinimalPlugin')) {
    & $tools.MSBuild (Join-Path $PSScriptRoot "$name.vcxproj") `
        "/p:Configuration=$Configuration" '/p:Platform=x64' "/p:PlatformToolset=$($tools.PlatformToolset)" "/p:VCToolsVersion=$($tools.Version)" `
        '/nologo' '/verbosity:minimal' '/m' '/nodeReuse:false'
    if ($LASTEXITCODE -ne 0) { throw "$name build failed: $LASTEXITCODE" }
}
Write-Output "Built build\examples\minimal-plugin\$Configuration\MinimalPlugin.dll (MSVC $($tools.PlatformToolset)). No game files were changed."
