param(
    [string]$SourceFile,
    [string]$WindhawkDirectory,
    [string]$LlvmToolsDirectory,
    [string[]]$Tests = @()
)
$ErrorActionPreference = 'Stop'
$runClock = [Diagnostics.Stopwatch]::StartNew()
$repository = Split-Path -Parent $PSScriptRoot
if (!$SourceFile) { $SourceFile = (Read-Host 'Path to the mod .cpp to test (you can drag the file here)').Trim().Trim('"') }
$output = Join-Path ([IO.Path]::GetTempPath()) ('gpu-mod-diagnostic-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $output
$console = Join-Path $output 'preparation-and-run.log'
$artifacts = Join-Path $output 'results'
$exitCode = 1
try {
    if (!$SourceFile) { throw 'No mod source was selected.' }
    if (![IO.Path]::IsPathRooted($SourceFile)) { $SourceFile = Join-Path $repository $SourceFile }
    $SourceFile = (Get-Item -LiteralPath $SourceFile -ErrorAction Stop).FullName
    if ([IO.Path]::GetExtension($SourceFile) -ine '.cpp') { throw 'Select a .cpp mod source.' }
    Write-Host 'Checking Windhawk and preparing test binaries. This can take a few minutes.'
    # Console output bypasses the redirected PowerShell streams below.
    $progress = { param($message) [Console]::WriteLine($message) }
    $arguments = @{ SourceFile=$SourceFile; OutputDirectory=$artifacts; Diagnostic=$true; Coverage=$true; ProgressAction=$progress }
    if ($WindhawkDirectory) { $arguments.WindhawkDirectory=$WindhawkDirectory }
    if ($LlvmToolsDirectory) { $arguments.LlvmToolsDirectory=$LlvmToolsDirectory }
    if ($Tests.Count) { $arguments.Tests=$Tests }
    & (Join-Path $repository 'tests/run-regressions.ps1') @arguments *> $console
    $exitCode = 0
} catch {
    $_ | Out-String | Add-Content -LiteralPath $console
}
$summary = Join-Path $artifacts 'regression-report.json'
if (Test-Path -LiteralPath $summary) {
    $report = Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
    Write-Host "Finished: $($report.Passed) passed, $($report.Failed) failed, $($report.Pending) pending."
    $report.Tests | Where-Object Status -ne Pass | Format-Table Name,Status,Reason -AutoSize | Out-Host
    if ($report.Status -ne 'Pass') { $exitCode = 1 }
} else {
    $runReport = Join-Path $artifacts 'test-run-report.json'
    if (Test-Path -LiteralPath $runReport) {
        $problem = Get-Content -LiteralPath $runReport -Raw | ConvertFrom-Json
        Write-Host "Tests could not finish: $($problem.Error)"
    } else {
        Write-Host 'Tests could not start. See preparation-and-run.log for the error.'
    }
}
Write-Host ('Running time: {0:N2}s' -f $runClock.Elapsed.TotalSeconds)
$coverageSummary = Join-Path $artifacts 'coverage\summary.txt'
$coverageHtml = Join-Path $artifacts 'coverage\html\index.html'
if (Test-Path -LiteralPath $coverageSummary) { Write-Host "Code coverage summary: $coverageSummary" }
if (Test-Path -LiteralPath $coverageHtml) { Write-Host "Code coverage report: $coverageHtml" }
Write-Host "Detailed output: $output"
exit $exitCode
