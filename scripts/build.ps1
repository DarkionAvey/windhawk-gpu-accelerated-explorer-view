param(
    [string]$SourceFile,
    [string]$WindhawkDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repository.ps1')
$root = Split-Path -Parent $PSScriptRoot
# Check the toolchain before creating or changing a working release.
$WindhawkDirectory = Resolve-WindhawkDirectory $WindhawkDirectory
if (!$SourceFile) {
    $working = Get-VersionSources $root | Select-Object -Last 1
    if ($working) { $SourceFile = $working.FullName }
    else { $SourceFile = New-ModVersion $root }
}
if (![IO.Path]::IsPathRooted($SourceFile)) { $SourceFile = Join-Path $root $SourceFile }
$source = (Resolve-Path -LiteralPath $SourceFile).ProviderPath
Assert-ReleaseSources $root $source
$readme = Join-Path $root 'README.md'
Sync-ModReadme $source $readme
Assert-ReleaseSources $root $source
$sourceHash = (Get-SourceHash $source)
$readmeHash = (Get-SourceHash $readme)
Write-Output "Building release $(Get-ModVersion $source) from $source"
& (Join-Path $root 'tests\repository-regression.ps1')
# A release always runs the complete C++ and DLL-unload suites. Focused test
# arguments belong to run-regressions.ps1 and cannot accidentally publish.
& (Join-Path $root 'tests\run-regressions.ps1') -SourceFile $source -WindhawkDirectory $WindhawkDirectory
if ((Get-SourceHash $source) -ne $sourceHash -or
    (Get-SourceHash $readme) -ne $readmeHash) {
    throw 'Source or README changed during testing; run the build again before publishing'
}
Publish-ModRelease $root $source
Write-Output "PASS: release finalized at $(Join-Path $root 'latest.cpp') and gpu-accelerated-explorer-view.wh.cpp; numbered sources preserved in previous-versions."
