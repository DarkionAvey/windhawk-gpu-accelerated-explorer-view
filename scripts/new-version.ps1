param([string]$Version)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repository.ps1')
$root = Split-Path -Parent $PSScriptRoot
$source = New-ModVersion $root $Version
Write-Output "Created $source. Edit this working copy, then run build.bat."
