param([string]$Compiler, [string]$LlvmToolsDirectory, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
. (Join-Path $PSScriptRoot 'coverage.ps1')
. (Join-Path $PSScriptRoot 'parallel-runner.ps1')
$tools = Resolve-CoverageTools $Compiler $LlvmToolsDirectory
$fixture = Join-Path $OutputDirectory ('coverage selftest [private] ' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$source = Join-Path $fixture 'covered.c'
[IO.File]::WriteAllText($source, @'
static int covered(int choice) {
    if (choice) return 31;
    return 47;
}
static int uncovered(void) {
    return 99;
}
__declspec(dllexport) int invoke(int choice) { return choice == 2 ? uncovered() : covered(choice); }
'@)
$hostSource = Join-Path $fixture 'host.c'
[IO.File]::WriteAllText($hostSource, @'
#include <windows.h>
#include <stdlib.h>
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    HMODULE image = LoadLibraryW(argv[1]);
    HMODULE unused = LoadLibraryW(argv[2]);
    if (!image || !unused) return 3;
    int (*call)(int) = (int(*)(int))GetProcAddress(image, "invoke");
    int choice = _wtoi(argv[3]);
    int result = call ? call(choice) : 0;
    if (!FreeLibrary(unused) || !FreeLibrary(image)) return 4;
    return result == (choice ? 31 : 47) ? 0 : 5;
}
'@)
$coverage = New-RegressionCoverage $source $fixture $tools
$dll = Join-Path $fixture 'probe.dll'
$unusedDll = Join-Path $fixture 'unused.dll'
$hostExe = Join-Path $fixture 'probe.exe'
foreach ($variant in @(@('used.c',$dll),@('unused.c',$unusedDll))) {
    $wrapper = Join-Path $fixture $variant[0]
    [IO.File]::WriteAllText($wrapper, '#include "covered.c"')
    & $Compiler -x c -target x86_64-w64-mingw32 -nostdlib++ -shared @($coverage.Flags) $wrapper -o $variant[1]
    if ($LASTEXITCODE) { throw 'Coverage selftest DLL compilation failed' }
}
& $Compiler -x c -target x86_64-w64-mingw32 -nostdlib++ -municode -static $hostSource -o $hostExe
if ($LASTEXITCODE) { throw 'Coverage selftest host compilation failed' }
$savedProfile = $env:LLVM_PROFILE_FILE
try {
    $env:LLVM_PROFILE_FILE = $coverage.Pattern
    $tasks = @(0,1 | ForEach-Object { [pscustomobject]@{
        Name="coverage-branch-$_"; FilePath=$hostExe; Arguments=@($dll,$unusedDll,"$_"); Serial=$false
    } })
    Invoke-RegressionTasks -Tasks $tasks -OutputDirectory $fixture -Jobs 16 -Phase regression
} finally { $env:LLVM_PROFILE_FILE = $savedProfile }
Write-RegressionCoverage $coverage @($dll,$unusedDll) (Join-Path $fixture 'regression-results.csv') $true
$report = Get-Content -LiteralPath (Join-Path $coverage.Directory 'coverage.json') -Raw | ConvertFrom-Json
$functions = @($report.data | ForEach-Object { $_.functions })
if (@($functions | Where-Object { $_.name -match '(^|:)covered$' -and $_.count -eq 2 }).Count -ne 1 -or
    @($functions | Where-Object { $_.name -match '(^|:)covered$' -and $_.count -eq 0 }).Count -ne 1) {
    throw 'Coverage did not preserve executed and uncovered functions across processes and DLL unload'
}
$uncovered = @(Get-Content -LiteralPath (Join-Path $coverage.Directory 'uncovered-functions.json') -Raw | ConvertFrom-Json)
if ($uncovered.Count -ne 1 -or $uncovered[0].Line -ne 5 -or $uncovered[0].Functions.Count -ne 2) {
    throw 'Coverage confused an unexecuted binary variant with an uncovered source definition'
}
$branches = @($report.data | ForEach-Object { $_.files } | ForEach-Object { $_.branches } | Where-Object { $_[0] -eq 2 })
if (!$branches.Count -or $branches[0][4] -eq 0 -or $branches[0][5] -eq 0) {
    throw 'Coverage lost one side of the real branch'
}
$missed = Get-Content -LiteralPath (Join-Path $coverage.Directory 'uncovered-lines.json') -Raw | ConvertFrom-Json
if ($missed -notcontains 6) { throw 'Coverage omitted the uncovered return line' }

# Publication moves the numbered source. Regeneration must read the verified
# snapshot, and still produce a working index plus real source/count markup.
Move-Item -LiteralPath $source -Destination (Join-Path $fixture 'archived-covered.c')
Write-RegressionCoverage $coverage @($dll,$unusedDll) (Join-Path $fixture 'regression-results.csv') $true
$htmlDirectory = Join-Path $coverage.Directory 'html'
$indexPath = Join-Path $htmlDirectory 'index.html'
$index = [IO.File]::ReadAllText($indexPath)
foreach ($expected in @('(2/3)','(5/8)','(7/9)','(3/4)')) {
    if (!$index.Contains($expected)) { throw "HTML omitted independent fixture expectation $expected" }
}
$page = Assert-CoverageHtml $htmlDirectory $report.data[0].files[0].summary
$sourceHtml = [IO.File]::ReadAllText($page)
if ($sourceHtml -notmatch "name='L2'.*?</td><td class='covered-line'><pre>2</pre>" -or
    $sourceHtml -notmatch "name='L6'.*?</td><td class='uncovered-line'><pre>0</pre>" -or
    !$sourceHtml.Contains('return 99;')) { throw 'HTML omitted actual source or covered/uncovered line counts' }
try {
    [IO.File]::WriteAllText($indexPath, [regex]::Replace($index,'\(\d+/\d+\)','(0/0)'))
    $rejected = $false
    try { $null = Assert-CoverageHtml $htmlDirectory $report.data[0].files[0].summary }
    catch { $rejected = $true }
    if (!$rejected) { throw 'Coverage accepted an empty HTML report' }
} finally { [IO.File]::WriteAllText($indexPath,$index) }
try {
    Remove-Item -LiteralPath $page
    $rejected = $false
    try { $null = Assert-CoverageHtml $htmlDirectory $report.data[0].files[0].summary }
    catch { $rejected = $true }
    if (!$rejected) { throw 'Coverage accepted a broken source-page link' }
} finally { [IO.File]::WriteAllText($page,$sourceHtml) }
Write-Output 'PASS: compiler coverage, branch/line/function expectations, binary variant aggregation, parallel profiles, DLL unload, actual HTML totals and annotations, rejected empty/broken HTML, and regeneration after source archival'
