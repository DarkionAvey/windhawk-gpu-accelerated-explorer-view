param(
    [string]$WindhawkDirectory,
    [string]$SourceFile = 'latest.cpp',
    [string[]]$TestArguments = @(),
    # Manifest names (background-regression.exe --list-tests) and/or 'unload'.
    # Selected tests keep their Parallel/Serial scheduling.
    [string[]]$Tests = @(),
    [string]$MinifiedSourceFile,
    [ValidateRange(1,32)][int]$Jobs = [Math]::Min(4,[Environment]::ProcessorCount),
    # A hard ceiling: may be lowered for diagnosis, never raised. See tests/README.md.
    [ValidateRange(1,120)][int]$TestTimeoutSeconds = 120,
    [switch]$KeepArtifacts,
    [switch]$Coverage,
    [string]$LlvmToolsDirectory,
    [string]$OutputDirectory
)
if ($Tests.Count -and $TestArguments.Count) { throw 'Use either -Tests or -TestArguments' }
# Comma-separated names arrive as one string through powershell -File.
$Tests = @($Tests | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$compileUnload = (!$Tests.Count -and !$TestArguments.Count) -or $TestArguments -contains '--unload' -or $Tests -contains 'unload'
$compilePipeline = $TestArguments -notcontains '--unload' -and !($Tests.Count -and !@($Tests | Where-Object { $_ -ne 'unload' }).Count)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\repository.ps1')
. (Join-Path $PSScriptRoot 'parallel-runner.ps1')
. (Join-Path $PSScriptRoot 'coverage.ps1')
$totalClock = [Diagnostics.Stopwatch]::StartNew()
if ($OutputDirectory -and ![IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) $OutputDirectory
}
$outputDirectory = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) }
    else { Join-Path ([IO.Path]::GetTempPath()) ('gpu-overlay-tests-' + [guid]::NewGuid().ToString('N')) }
New-RegressionResultDirectory $outputDirectory
Write-Output "Build artifacts and results: $outputDirectory"
try {
    Assert-RegressionExecutionContext
    Assert-NoModuleInjection # Before compiling: an injected engine invalidates every result.
    $WindhawkDirectory = Resolve-WindhawkDirectory $WindhawkDirectory
    $compilerDirectory = Join-Path $WindhawkDirectory 'Compiler'
    $compiler = Join-Path $compilerDirectory 'bin\clang++.exe'
    $coverageTools = if ($Coverage) { Resolve-CoverageTools $compiler $LlvmToolsDirectory } else { $null }
    $testDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    if (![IO.Path]::IsPathRooted($SourceFile)) { $SourceFile = Join-Path (Split-Path -Parent $testDirectory) $SourceFile }
    $sourcePath = (Resolve-Path -LiteralPath $SourceFile).ProviderPath
    $sourceVersion = Get-ModVersion $sourcePath
    Write-Output "Testing $sourcePath (version $sourceVersion)"
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
    $arm64HostLibrary = Join-Path (Split-Path -Parent (Split-Path -Parent $hostLibrary)) 'arm64\windhawk.lib'
    if (!(Test-Path -LiteralPath $arm64HostLibrary)) {
        throw 'Matching Windhawk ARM64 host import library not found; install a Windhawk distribution with ARM64 support'
    }
    Write-Output "Windhawk ARM64 host library: $arm64HostLibrary"
    $modFlags = $flags | Where-Object { $_ -notin @('-DWH_EDITING', '-static', '-municode') }
    $modFlags += '-fstack-usage'
    $arm64ModFlags = @($modFlags | ForEach-Object {
        if ($_ -eq 'x86_64-w64-mingw32') { 'aarch64-w64-mingw32' } else { $_ }
    })
    $arm64ModDll = Join-Path $outputDirectory 'arm64-mod-build.dll'
    $arm64CompileTasks = @([pscustomobject]@{Name='compile-arm64-mod'; FilePath=$compiler; Serial=$false
        Arguments=$arm64ModFlags+@('-shared',(Join-Path $testDirectory 'mod-build.cpp'),'-o',$arm64ModDll,$arm64HostLibrary)+$libraries})
    $coverageRun = if ($Coverage) { New-RegressionCoverage $sourcePath $outputDirectory $coverageTools } else { $null }
    if ($Coverage) {
        & (Join-Path $testDirectory 'coverage-regression.ps1') -Compiler $compiler `
            -LlvmToolsDirectory (Split-Path -Parent $coverageTools.Cov) -OutputDirectory $outputDirectory
    }
    $testFlags = if ($Coverage) { @($flags)+$coverageRun.Flags } else { $flags }
    $compileTasks = @([pscustomobject]@{Name='compile-mod'; FilePath=$compiler; Serial=$false
        Arguments=@($modFlags)+@('-shared',(Join-Path $testDirectory 'mod-build.cpp'),'-o',$modDll,$hostLibrary)+$libraries})
    if ($MinifiedSourceFile) {
        if (![IO.Path]::IsPathRooted($MinifiedSourceFile)) {
            $MinifiedSourceFile = Join-Path (Split-Path -Parent $testDirectory) $MinifiedSourceFile
        }
        $MinifiedSourceFile = (Resolve-Path -LiteralPath $MinifiedSourceFile).ProviderPath
        if ((Get-ModVersion $MinifiedSourceFile) -ne $sourceVersion) { throw 'Minified version differs from the selected source' }
        $minifiedConfiguration = Join-Path $outputDirectory 'minified-selected-source.h'
        [IO.File]::WriteAllText($minifiedConfiguration,
            $configurationText.Replace($sourcePath.Replace('\', '/'), $MinifiedSourceFile.Replace('\', '/')),
            [Text.UTF8Encoding]::new($false))
        $minifiedFlags = @($modFlags | ForEach-Object { if ($_ -eq $configuration) { $minifiedConfiguration } else { $_ } })
        $minifiedDll = Join-Path $outputDirectory 'minified-mod-build.dll'
        $compileTasks += [pscustomobject]@{Name='compile-minified'; FilePath=$compiler; Serial=$false
            Arguments=@($minifiedFlags)+@('-shared',(Join-Path $testDirectory 'mod-build.cpp'),'-o',$minifiedDll,$hostLibrary)+$libraries}
        $arm64MinifiedFlags = @($minifiedFlags | ForEach-Object {
            if ($_ -eq 'x86_64-w64-mingw32') { 'aarch64-w64-mingw32' } else { $_ }
        })
        $arm64MinifiedDll = Join-Path $outputDirectory 'arm64-minified-mod-build.dll'
        $arm64CompileTasks += [pscustomobject]@{Name='compile-arm64-minified'; FilePath=$compiler; Serial=$false
            Arguments=$arm64MinifiedFlags+@('-shared',(Join-Path $testDirectory 'mod-build.cpp'),'-o',$arm64MinifiedDll,$arm64HostLibrary)+$libraries}
    }
    if ($compileUnload) {
        $unloadDll = Join-Path $outputDirectory 'unload-mod.dll'
        $unloadExe = Join-Path $outputDirectory 'unload-regression.exe'
        $unloadFlags = $testFlags | Where-Object { $_ -notin @('-static', '-municode') }
        $compileTasks += [pscustomobject]@{Name='compile-unload-mod'; FilePath=$compiler; Serial=$false
            Arguments=@($unloadFlags)+@('-shared',(Join-Path $testDirectory 'unload-mod.cpp'),'-o',$unloadDll)+$libraries}
        $compileTasks += [pscustomobject]@{Name='compile-unload-host'; FilePath=$compiler; Serial=$false
            Arguments=@('-std=c++23','-target','x86_64-w64-mingw32','-O2','-municode',
                (Join-Path $testDirectory 'unload-regression.cpp'),'-o',$unloadExe,'-lole32')}
        # Windhawk's import libraries name the private runtimes .whl. The isolated
        # Windows loader needs matching files, without injecting Windhawk's loader.
        Copy-Item -LiteralPath (Join-Path $compilerDirectory 'x86_64-w64-mingw32\bin\libc++.dll') -Destination (Join-Path $outputDirectory 'libc++.whl')
        Copy-Item -LiteralPath (Join-Path $compilerDirectory 'x86_64-w64-mingw32\bin\libunwind.dll') -Destination (Join-Path $outputDirectory 'libunwind.whl')
    }
    if ($compilePipeline) {
        $compileTasks += [pscustomobject]@{Name='compile-pipeline'; FilePath=$compiler; Serial=$false
            Arguments=@($testFlags)+@((Join-Path $testDirectory 'background-regression.cpp'),'-o',$executable)+$libraries}
    }
    # The large translation units are memory intensive; cap concurrent compilers
    # at two while allowing the separately isolated test suites to use -Jobs.
    # ARM64 is a mandatory compile/link gate, never executed on this x64 host.
    # Fail before compiling/running the x64 suite or allowing release publication.
    Invoke-RegressionTasks -Tasks $arm64CompileTasks -OutputDirectory $outputDirectory -Jobs ([Math]::Min(2,$Jobs)) -Phase compile-arm64
    Invoke-RegressionTasks -Tasks $compileTasks -OutputDirectory $outputDirectory -Jobs ([Math]::Min(2,$Jobs)) -Phase compile
    if ($MinifiedSourceFile) {
        & (Join-Path $testDirectory 'minification-regression.ps1') -Compiler $compiler -OutputDirectory $outputDirectory
    }
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
    $previousPath = $env:PATH
    $previousProfile = $env:LLVM_PROFILE_FILE
    $testsPassed = $false
    try {
        if ($Coverage) { $env:LLVM_PROFILE_FILE = $coverageRun.Pattern }
        $env:PATH = (Join-Path $compilerDirectory 'x86_64-w64-mingw32\bin') + ';' +
            (Join-Path $compilerDirectory 'bin') + ';' + $previousPath
        $testTasks = @()
        if ($unloadExe) { $testTasks += [pscustomobject]@{
            Name='unload'; FilePath=$unloadExe; Arguments=@($unloadDll); Serial=$false} }
        if ($compilePipeline) {
            if ($TestArguments.Count) {
                $testTasks += [pscustomobject]@{Name='pipeline'; FilePath=$executable
                    Arguments=$TestArguments; Serial=$false}
            } else {
                # One process per manifest entry. Parallel entries run first, then
                # each Serial entry alone (see tests/README.md "Writing a regression").
                $manifest = @(& $executable --list-tests)
                if ($LASTEXITCODE -ne 0 -or !$manifest.Count) { throw 'Cannot read the regression manifest' }
                $known = @{}
                foreach ($line in $manifest) {
                    if ($line -notmatch '^([a-z0-9-]+)\t(parallel|serial)\t(release|focused)\t([a-z,-]*)$') {
                        throw "Invalid regression manifest line: $line"
                    }
                    $known[$Matches[1]] = $true
                    $selected = if ($Tests.Count) { $Tests -contains $Matches[1] } else { $Matches[3] -eq 'release' }
                    if ($selected) {
                        $testTasks += [pscustomobject]@{Name=$Matches[1]; FilePath=$executable
                            Arguments=@('--test',$Matches[1]); Serial=($Matches[2] -eq 'serial')}
                    }
                }
                $unknown = @($Tests | Where-Object { $_ -ne 'unload' -and !$known.ContainsKey($_) })
                if ($unknown.Count) { throw "Unknown regression(s): $($unknown -join ', ')" }
            }
        }
        Invoke-RegressionGate -Tasks $testTasks -OutputDirectory $outputDirectory -Jobs $Jobs -TimeoutSeconds $TestTimeoutSeconds -ObserveInterference
        $testsPassed = $true
        Write-Output ('PASS: complete regression gate in {0:N2}s (compile and execution)' -f $totalClock.Elapsed.TotalSeconds)
        if ($KeepArtifacts) { Write-Output "Artifacts retained at $outputDirectory" }
    } finally {
        $env:LLVM_PROFILE_FILE = $previousProfile
        try {
            if ($Coverage) {
                $objects = @()
                if ($compilePipeline) { $objects += $executable }
                if ($compileUnload) { $objects += $unloadDll }
                try { Write-RegressionCoverage $coverageRun $objects (Join-Path $outputDirectory 'regression-results.csv') $testsPassed }
                catch {
                    if ($testsPassed) { throw }
                    Write-Warning "Coverage report failed after test failure: $_"
                }
            }
        } finally { $env:PATH = $previousPath }
        # Delete only this script's known generated binary; no recursive deletion.
        $retried = Test-Path -LiteralPath (Join-Path $outputDirectory 'isolated-1')
        if ($testsPassed -and !$KeepArtifacts -and !$Coverage -and !$retried) {
            if (Test-Path -LiteralPath $executable) { Remove-Item -LiteralPath $executable -Force }
            if (Test-Path -LiteralPath $modDll) { Remove-Item -LiteralPath $modDll -Force }
            Remove-Item -LiteralPath $arm64ModDll -Force
            if ($arm64MinifiedDll) { Remove-Item -LiteralPath $arm64MinifiedDll -Force }
            if ($minifiedDll) { Remove-Item -LiteralPath $minifiedDll, $minifiedConfiguration -Force }
            foreach ($stackReport in $stackReports) { Remove-Item -LiteralPath $stackReport.FullName }
            Remove-Item -LiteralPath $configuration
            if ($unloadExe -and (Test-Path -LiteralPath $unloadExe)) { Remove-Item -LiteralPath $unloadExe -Force }
            if ($unloadDll -and (Test-Path -LiteralPath $unloadDll)) { Remove-Item -LiteralPath $unloadDll -Force }
            if ($unloadExe) {
                Remove-Item -LiteralPath (Join-Path $outputDirectory 'libc++.whl'),(Join-Path $outputDirectory 'libunwind.whl') -Force
            }
            # Receipts/logs remain available even after a clean successful run.
        }
    }
    [pscustomobject]@{Status='Pass'; Source=$sourcePath; Seconds=$totalClock.Elapsed.TotalSeconds} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputDirectory 'test-run-report.json') -Encoding UTF8
} catch {
    $status = if ($_.Exception.Message -match '^PENDING:') { 'Pending' } else { 'Fail' }
    [pscustomobject]@{Status=$status; Source=$sourcePath; Error=$_.Exception.Message; Seconds=$totalClock.Elapsed.TotalSeconds} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputDirectory 'test-run-report.json') -Encoding UTF8
    Write-Host "RESULT: $status. Reports retained at $outputDirectory"
    throw
}
