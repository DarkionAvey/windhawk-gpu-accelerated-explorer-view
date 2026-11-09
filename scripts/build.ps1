param(
    [string]$SourceFile,
    [string]$WindhawkDirectory,
    [ValidateRange(1,32)][int]$Jobs = [Math]::Min(4,[Environment]::ProcessorCount),
    [switch]$KeepArtifacts,
    [switch]$Coverage,
    [string]$LlvmToolsDirectory,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repository.ps1')
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'tests\parallel-runner.ps1')
if ($OutputDirectory -and ![IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $root $OutputDirectory }
$resultDirectory = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) }
    else { Join-Path ([IO.Path]::GetTempPath()) ('gpu-overlay-build-' + [guid]::NewGuid().ToString('N')) }
New-RegressionResultDirectory $resultDirectory
Write-Output "Build results: $resultDirectory"
$published = $false
try {
    Assert-RegressionExecutionContext
    Assert-NoModuleInjection # Before the repository checks, compilers and tests.
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
    $minified = Join-Path $root ('latest.minified.' + [guid]::NewGuid().ToString('N') + '.tmp')
    [IO.File]::WriteAllText($minified, (ConvertTo-MinifiedMod $source), [Text.UTF8Encoding]::new($false))
    # A release always runs the complete C++ and DLL-unload suites. Focused test
    # arguments belong to run-regressions.ps1 and cannot accidentally publish.
    try {
        & (Join-Path $root 'tests\run-regressions.ps1') -SourceFile $source -WindhawkDirectory $WindhawkDirectory -MinifiedSourceFile $minified -Jobs $Jobs -KeepArtifacts:$KeepArtifacts -Coverage:$Coverage -LlvmToolsDirectory $LlvmToolsDirectory -OutputDirectory $resultDirectory
        if ((Get-SourceHash $source) -ne $sourceHash -or
            (Get-SourceHash $readme) -ne $readmeHash) {
            throw 'Source or README changed during testing; run the build again before publishing'
        }
        Publish-ModRelease $root $source $minified
        $published = $true
    } finally {
        Remove-Item -LiteralPath $minified -Force
    }
    Write-Output "PASS: release finalized at $(Join-Path $root 'latest.cpp'), gpu-accelerated-explorer-view.wh.cpp and read-only gpu-accelerated-explorer-view.minified.wh.cpp; previous-versions retains only the 15 newest source versions."
    [pscustomobject]@{Status='Pass'; Published=$true; Source=$source} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $resultDirectory 'build-report.json') -Encoding UTF8
} catch {
    $status = if ($_.Exception.Message -match '^PENDING:') { 'Pending' } else { 'Fail' }
    [pscustomobject]@{Status=$status; Published=$published; Source=$source; Error=$_.Exception.Message} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $resultDirectory 'build-report.json') -Encoding UTF8
    $publication = if ($published) { 'release published; subsequent reporting/cleanup failed' } else { 'publication blocked' }
    Write-Host "BUILD $($status.ToUpperInvariant()): $publication. Results: $resultDirectory"
    throw
}
