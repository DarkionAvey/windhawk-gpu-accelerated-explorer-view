param(
    [string]$WindhawkDirectory,
    [string]$SourceFile = 'latest.cpp',
    [string[]]$TestArguments = @()
)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\repository.ps1')
$WindhawkDirectory = Resolve-WindhawkDirectory $WindhawkDirectory
$compilerDirectory = Join-Path $WindhawkDirectory 'Compiler'
$compiler = Join-Path $compilerDirectory 'bin\clang++.exe'
$testDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if (![IO.Path]::IsPathRooted($SourceFile)) { $SourceFile = Join-Path (Split-Path -Parent $testDirectory) $SourceFile }
$sourcePath = (Resolve-Path -LiteralPath $SourceFile).ProviderPath
$sourceVersion = Get-ModVersion $sourcePath
Write-Output "Testing $sourcePath (version $sourceVersion)"
$outputDirectory = Join-Path ([IO.Path]::GetTempPath()) ('gpu-overlay-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDirectory | Out-Null
Write-Output "Build artifacts: $outputDirectory (retained on failure)"
$executable = Join-Path $outputDirectory 'background-regression.exe'
$flags = @(
    '-std=c++23', '-target', 'x86_64-w64-mingw32', '-O2', '-municode', '-static',
    '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0',
    '-DWH_MOD', '-DWH_EDITING',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
    '-Wno-cast-function-type-mismatch'
)
$libraries = @('-ldcomp', '-ld3d11', '-ldxgi', '-ld2d1', '-ldwrite', '-lshell32',
    '-lole32', '-loleaut32', '-luuid', '-lshlwapi', '-luser32', '-lgdi32',
    '-lwindowscodecs', '-ldwmapi', '-luxtheme', '-lpropsys', '-lcomctl32', '-lruntimeobject', '-lninput')
# A forced header avoids native command-line quote stripping in Windows
# PowerShell 5.1, including repository paths containing spaces or Unicode.
$configuration = Join-Path $outputDirectory 'selected-source.h'
$configurationText = '#define GPU_OVERLAY_SOURCE "' + $sourcePath.Replace('\', '/') + '"' + "`n" +
    '#define WH_MOD_VERSION L"' + $sourceVersion + '"' + "`n"
[IO.File]::WriteAllText($configuration, $configurationText, [Text.UTF8Encoding]::new($false))
$flags += '-include', $configuration, '-include', 'windhawk_api.h'
$modDll = Join-Path $outputDirectory 'mod-build.dll'
$hostLibrary = Get-ChildItem -LiteralPath (Join-Path $WindhawkDirectory 'Engine') -Directory |
    Sort-Object -Property @{
        Expression = { $v = [version]'0.0'; [void][version]::TryParse(($_.Name -split '_')[0], [ref]$v); $v }
    }, @{
        Expression = { if ($_.Name -match '_(\d+)$') { [int]$Matches[1] } else { 0 } }
    } -Descending |
    ForEach-Object { Join-Path $_.FullName '64\windhawk.lib' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $hostLibrary) { throw 'Installed Windhawk x64 host import library not found' }
Write-Output "Windhawk host library: $hostLibrary"
$modFlags = $flags | Where-Object { $_ -notin @('-DWH_EDITING', '-static', '-municode') }
$modFlags += '-fstack-usage'
& $compiler @modFlags '-shared' (Join-Path $testDirectory 'mod-build.cpp') '-o' $modDll $hostLibrary @libraries
if ($LASTEXITCODE -ne 0) { throw "Windhawk host DLL build failed: $LASTEXITCODE" }
Write-Output "PASS: linked $SourceFile against installed Windhawk host API"
$stackReports = @(Get-ChildItem -LiteralPath $outputDirectory -Filter '*.su' -File)
if (!$stackReports.Count) { throw 'Mod compiler did not produce stack-usage diagnostics' }
$callbackStacks = foreach ($stackReport in $stackReports) {
    foreach ($stackLine in [IO.File]::ReadLines($stackReport.FullName)) {
        $parts = $stackLine -split "`t"
        if ($parts.Count -ge 3 -and $parts[0] -match 'DispatchHook|OverlayProc|TranslateAcceleratorHook|NativeHostSubclass|ColdAttachGetMessageHook|OnNavigationPending|OnNavigationComplete') {
            [pscustomobject]@{ Function=$parts[0]; Bytes=[int]$parts[1] }
        }
    }
}
if (!$callbackStacks) { throw 'Stack report omitted critical message/COM callbacks' }
$oversized = @($callbackStacks | Where-Object Bytes -gt 8192)
if ($oversized.Count) { $oversized | Format-Table -AutoSize; throw 'Message/COM callback exceeds the 8 KiB stack budget' }
Write-Output ('PASS: compiled message/COM callback stack frames stay within 8 KiB (maximum ' +
    ($callbackStacks | Measure-Object Bytes -Maximum).Maximum + ' bytes)')
if ($TestArguments -contains '--unload' -or !$TestArguments.Count) {
    $unloadDll = Join-Path $outputDirectory 'unload-mod.dll'
    $unloadExe = Join-Path $outputDirectory 'unload-regression.exe'
    $unloadFlags = $flags | Where-Object { $_ -notin @('-static', '-municode') }
    & $compiler @unloadFlags '-shared' (Join-Path $testDirectory 'unload-mod.cpp') '-o' $unloadDll @libraries
    if ($LASTEXITCODE -ne 0) { throw "Unload fixture DLL build failed: $LASTEXITCODE" }
    & $compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-O2' '-municode' (Join-Path $testDirectory 'unload-regression.cpp') '-o' $unloadExe '-lole32'
    if ($LASTEXITCODE -ne 0) { throw "Unload fixture host build failed: $LASTEXITCODE" }
    # Windhawk's import libraries name the private runtimes .whl. The isolated
    # Windows loader needs matching files, without injecting Windhawk's loader.
    Copy-Item -LiteralPath (Join-Path $compilerDirectory 'x86_64-w64-mingw32\bin\libc++.dll') -Destination (Join-Path $outputDirectory 'libc++.whl')
    Copy-Item -LiteralPath (Join-Path $compilerDirectory 'x86_64-w64-mingw32\bin\libunwind.dll') -Destination (Join-Path $outputDirectory 'libunwind.whl')
}
if ($TestArguments -notcontains '--unload') {
& $compiler @flags (Join-Path $testDirectory 'background-regression.cpp') '-o' $executable @libraries
if ($LASTEXITCODE -ne 0) { throw "Regression build failed: $LASTEXITCODE" }
}
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $compilerDirectory 'x86_64-w64-mingw32\bin') + ';' +
        (Join-Path $compilerDirectory 'bin') + ';' + $previousPath
    if ($unloadExe) {
        & $unloadExe $unloadDll
        if ($LASTEXITCODE -ne 0) { throw "Unload regressions failed: $LASTEXITCODE (binaries retained at $outputDirectory)" }
    }
    if ($TestArguments -notcontains '--unload') {
        & $executable @TestArguments
        if ($LASTEXITCODE -ne 0) { throw "Regression tests failed: $LASTEXITCODE (binary retained at $executable)" }
    }
    $testsPassed = $true
} finally {
    $env:PATH = $previousPath
    # Delete only this script's known generated binary; no recursive deletion.
    if ($testsPassed) {
        if (Test-Path -LiteralPath $executable) { Remove-Item -LiteralPath $executable -Force }
        if (Test-Path -LiteralPath $modDll) { Remove-Item -LiteralPath $modDll -Force }
        foreach ($stackReport in $stackReports) { Remove-Item -LiteralPath $stackReport.FullName }
        Remove-Item -LiteralPath $configuration
        if ($unloadExe -and (Test-Path -LiteralPath $unloadExe)) { Remove-Item -LiteralPath $unloadExe -Force }
        if ($unloadDll -and (Test-Path -LiteralPath $unloadDll)) { Remove-Item -LiteralPath $unloadDll -Force }
        if ($unloadExe) {
            Remove-Item -LiteralPath (Join-Path $outputDirectory 'libc++.whl'),(Join-Path $outputDirectory 'libunwind.whl') -Force
        }
        Remove-Item -LiteralPath $outputDirectory
    }
}
