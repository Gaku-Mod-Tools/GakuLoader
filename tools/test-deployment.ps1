#Requires -Version 5.1
[CmdletBinding()]
param([string]$Dll = '')
# Compatibility entry only. Bundle deployment belongs to the integrating product.
Write-Warning 'test-deployment.ps1 is retired; forwarding to local test-package.ps1. No files are installed or restored.'
& (Join-Path $PSScriptRoot 'test-package.ps1') -Dll $Dll
