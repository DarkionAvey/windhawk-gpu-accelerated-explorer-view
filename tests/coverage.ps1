# Compiler-measured coverage; dot-sourced by the regression runner.
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\repository.ps1')
function Resolve-CoverageTools([string]$Compiler, [string]$Directory) {
    if ($Directory -and ![IO.Path]::IsPathRooted($Directory)) {
        $Directory = Join-Path (Split-Path -Parent $PSScriptRoot) $Directory
    }
    $compilerVersion = (& $Compiler --version | Out-String)
    if ($compilerVersion -notmatch 'clang version (\d+)') { throw 'Cannot identify the coverage compiler' }
    $major = $Matches[1]
    $candidates = @()
    if ($Directory) { $candidates = @($Directory) }
    else {
        $candidates += Join-Path $PSScriptRoot ".build\llvm-coverage-tools-$major"
        $candidates += Split-Path -Parent $Compiler
        if ($env:LLVM_HOME) { $candidates += (Join-Path $env:LLVM_HOME 'bin'), $env:LLVM_HOME }
        $onPath = Get-Command llvm-cov.exe -ErrorAction SilentlyContinue
        if ($onPath) { $candidates += Split-Path -Parent $onPath.Source }
        foreach ($programDirectory in @($env:ProgramW6432, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
            if ($programDirectory) { $candidates += Join-Path $programDirectory 'LLVM\bin' }
        }
        foreach ($key in @('HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
            $candidates += @(Get-ItemProperty -Path $key -ErrorAction SilentlyContinue |
                Where-Object { $_.DisplayName -match '^LLVM(\s|$)' -and $_.InstallLocation } |
                ForEach-Object { Join-Path $_.InstallLocation 'bin' })
        }
    }
    foreach ($candidate in $candidates) {
        $cov = Join-Path $candidate 'llvm-cov.exe'
        $prof = Join-Path $candidate 'llvm-profdata.exe'
        if (!(Test-Path -LiteralPath $cov) -or !(Test-Path -LiteralPath $prof)) { continue }
        $versions = @((& $cov --version | Out-String), (& $prof --version | Out-String))
        if (@($versions | Where-Object { $_ -notmatch "LLVM version $major\." }).Count) { continue }
        return [pscustomobject]@{Cov=(Resolve-Path -LiteralPath $cov).Path; Profdata=(Resolve-Path -LiteralPath $prof).Path; Major=$major}
    }
    throw "Coverage needs llvm-cov.exe and llvm-profdata.exe from LLVM $major. Install matching LLVM tools or pass -LlvmToolsDirectory."
}

function New-RegressionCoverage([string]$Source, [string]$OutputDirectory, $Tools) {
    $directory = Join-Path $OutputDirectory 'coverage'
    New-Item -ItemType Directory -Path (Join-Path $directory 'raw') | Out-Null
    $filter = Join-Path $directory 'instrumentation.txt'
    # Instrument the mod, excluding fixture code, SDK and standard headers.
    # Clang sees native and slash-normalized include paths on Windows. The
    # selected numbered source has a unique basename in each translation unit.
    $pattern = '*' + [IO.Path]::GetFileName($Source).Replace('[','\[').Replace(']','\]')
    [IO.File]::WriteAllText($filter, "[clang]`nsrc:" + $pattern + "`n", [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{
        Directory=$directory; Tools=$Tools; Source=$Source; SourceHash=(Get-SourceHash $Source)
        Flags=@('-fprofile-instr-generate','-fcoverage-mapping','-fprofile-update=atomic',"-fprofile-list=$filter")
        Pattern=(Join-Path $directory 'raw\%p-%m.profraw')
    }
}

function Invoke-CoverageOutput([string]$Tool, [string[]]$Arguments, [string]$Destination) {
    & $Tool @Arguments | Out-File -LiteralPath $Destination -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Coverage tool failed ($LASTEXITCODE): $Tool" }
}

function Assert-CoverageHtml([string]$Directory, $Summary) {
    $index = [IO.File]::ReadAllText((Join-Path $Directory 'index.html'))
    $rows = [regex]::Matches($index, "<tr class='light-row'>(.*?)</tr>", 'Singleline')
    $totals = [regex]::Matches($index, "<tr class='light-row-bold'>(.*?)</tr>", 'Singleline')
    if ($rows.Count -ne 1 -or $totals.Count -ne 1) { throw 'Coverage HTML omitted the selected source or totals row' }
    foreach ($row in @($rows[0].Value, $totals[0].Value)) {
        $counts = [regex]::Matches($row, '\((\d+)/(\d+)\)')
        $expected = @($Summary.functions, $Summary.lines, $Summary.regions, $Summary.branches)
        if ($counts.Count -ne $expected.Count) { throw 'Coverage HTML omitted its coverage counts' }
        for ($i=0; $i -lt $expected.Count; ++$i) {
            if ([long]$counts[$i].Groups[1].Value -ne $expected[$i].covered -or
                [long]$counts[$i].Groups[2].Value -ne $expected[$i].count) {
                throw 'Coverage HTML counts disagree with measured coverage'
            }
        }
    }
    $link = [regex]::Match($rows[0].Value, "<a href='([^']+\.html)'>")
    if (!$link.Success) { throw 'Coverage HTML omitted the annotated source link' }
    $page = [IO.Path]::GetFullPath((Join-Path $Directory ([Net.WebUtility]::HtmlDecode($link.Groups[1].Value))))
    $root = [IO.Path]::GetFullPath($Directory).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (!$page.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $page)) {
        throw 'Coverage HTML annotated source link is broken'
    }
    $source = [IO.File]::ReadAllText($page)
    if (!$source.Contains("class='code'") -or !$source.Contains("name='L1'") -or
        ($Summary.lines.covered -gt 0 -and !$source.Contains("class='covered-line'"))) {
        throw 'Coverage HTML contains no annotated source code'
    }
    return $page
}

function Write-RegressionCoverage($Coverage, [string[]]$Objects, [string]$ResultsPath, [bool]$Passed) {
    $directory = $Coverage.Directory
    $snapshot = Join-Path $directory 'source.cpp'
    if (Test-Path -LiteralPath $Coverage.Source) {
        if ((Get-SourceHash $Coverage.Source) -ne $Coverage.SourceHash) { throw 'Coverage source changed after measurement' }
        Copy-Item -LiteralPath $Coverage.Source -Destination $snapshot
    } elseif (!(Test-Path -LiteralPath $snapshot) -or (Get-SourceHash $snapshot) -ne $Coverage.SourceHash) {
        throw 'Coverage source and its verified snapshot are unavailable'
    }
    $raw = @(Get-ChildItem -LiteralPath (Join-Path $directory 'raw') -Filter '*.profraw' -File | Where-Object Length -gt 0)
    if (!$raw.Count) { throw 'Coverage produced no profiles; no coverage result can be reported' }
    $inputs = Join-Path $directory 'profiles.txt'
    [IO.File]::WriteAllLines($inputs, [string[]]$raw.FullName, [Text.UTF8Encoding]::new($false))
    $mergedProfile = Join-Path $directory 'merged.profdata'
    & $Coverage.Tools.Profdata merge -sparse -f $inputs -o $mergedProfile
    if ($LASTEXITCODE -ne 0) { throw 'Coverage profile merge failed' }
    $objectsArguments = @($Objects[0])
    foreach ($object in @($Objects | Select-Object -Skip 1)) { $objectsArguments += '-object', $object }
    $baseArguments = @($objectsArguments) + @("-instr-profile=$mergedProfile")
    # Export metadata without filesystem-based positional filters: archived
    # sources need not exist at their original compile-time path.
    $common = @($baseArguments)
    Invoke-CoverageOutput $Coverage.Tools.Cov (@('export')+$common) (Join-Path $directory 'coverage.json')
    $data = Get-Content -LiteralPath (Join-Path $directory 'coverage.json') -Raw | ConvertFrom-Json
    $sourceName = $Coverage.Source.Replace('\','/')
    # Macro expansions may pull fixture files into LLVM's positional source
    # filter. Exclude those explicitly so they cannot alter the denominator.
    $others = @($data.data | ForEach-Object { $_.files } | Where-Object { $_.filename.Replace('\','/') -ne $sourceName } |
        Select-Object -ExpandProperty filename -Unique)
    if ($others.Count) {
        $exclude = @($others | ForEach-Object { [regex]::Escape($_.Replace('\','/')).Replace('/','[/\\]') })
        $common += '-ignore-filename-regex=^(' + ($exclude -join '|') + ')$'
        Invoke-CoverageOutput $Coverage.Tools.Cov (@('export')+$common) (Join-Path $directory 'coverage.json')
        $data = Get-Content -LiteralPath (Join-Path $directory 'coverage.json') -Raw | ConvertFrom-Json
    }
    $measured = @($data.data | ForEach-Object { $_.files } | Where-Object { $_.filename.Replace('\','/') -eq $sourceName })
    if ($measured.Count -ne 1 -or !$measured[0].summary.lines.count) { throw 'Coverage omitted the selected source' }
    Invoke-CoverageOutput $Coverage.Tools.Cov (@('report')+$common) (Join-Path $directory 'summary.txt')
    Invoke-CoverageOutput $Coverage.Tools.Cov (@('export','-format=lcov')+$common) (Join-Path $directory 'coverage.lcov')
    # show requires --sources explicitly. Positional source arguments can
    # silently produce a successful, empty (0/0) HTML report on Windows.
    # Remap a directory (LLVM does not support file-to-file equivalence), so
    # reports can be regenerated after publication moves the numbered source.
    $sourceDirectory = Split-Path -Parent $Coverage.Source
    $htmlSourceDirectory = Join-Path $directory 'sources'
    New-Item -ItemType Directory -Path $htmlSourceDirectory -Force | Out-Null
    $htmlSource = Join-Path $htmlSourceDirectory ([IO.Path]::GetFileName($Coverage.Source))
    Copy-Item -LiteralPath $snapshot -Destination $htmlSource
    $showArguments = @($baseArguments) + @("--sources=$htmlSource", "-path-equivalence=$sourceDirectory,$htmlSourceDirectory")
    if ($others.Count) {
        $prefix = $sourceDirectory.Replace('\','/').TrimEnd('/') + '/'
        $showExcluded = @(foreach ($file in $others) {
            $file
            $normal = $file.Replace('\','/')
            if ($normal.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                Join-Path $htmlSourceDirectory $normal.Substring($prefix.Length)
            }
        })
        $showRegex = @($showExcluded | Select-Object -Unique | ForEach-Object {
            [regex]::Escape($_.Replace('\','/')).Replace('/','[/\\]')
        })
        $showArguments += '-ignore-filename-regex=^(' + ($showRegex -join '|') + ')$'
    }
    $htmlDirectory = Join-Path $directory 'html'
    $stagedHtml = Join-Path $directory ('html-pending-' + [guid]::NewGuid().ToString('N'))
    & $Coverage.Tools.Cov show @showArguments -format=html `
        "-output-dir=$stagedHtml" -show-branches=count -show-line-counts-or-regions
    if ($LASTEXITCODE -ne 0) { throw 'Coverage HTML generation failed' }
    $null = Assert-CoverageHtml $stagedHtml $measured[0].summary
    # Validate fresh pages before replacing an earlier report; stale source
    # pages must never make a failed regeneration appear successful.
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($htmlDirectory)) -ne [IO.Path]::GetFullPath($directory).TrimEnd('\','/')) {
        throw 'Coverage HTML replacement escaped its report directory'
    }
    if (Test-Path -LiteralPath $htmlDirectory) { Remove-Item -LiteralPath $htmlDirectory -Recurse -Force }
    Move-Item -LiteralPath $stagedHtml -Destination $htmlDirectory
    # Static symbols differ between the standalone and DLL translation units;
    # template instantiations also share one source definition. A zero-count
    # variant must not label a definition missed when another variant ran.
    $definitions = @(foreach ($unit in $data.data) {
        foreach ($function in $unit.functions) {
            $region = $function.regions | Select-Object -First 1
            $file = $function.filenames[$region[5]]
            if ($file.Replace('\','/') -eq $sourceName) {
                [pscustomobject]@{Key=($region[0..3] -join ':'); Function=$function.name
                    File=$file; Line=$region[0]; Count=$function.count}
            }
        }
    })
    $functions = @($definitions | Group-Object Key | Where-Object {
        !($_.Group | Where-Object Count -gt 0)
    } | ForEach-Object {
        [pscustomobject]@{File=$_.Group[0].File; Line=$_.Group[0].Line
            Functions=@($_.Group.Function | Select-Object -Unique)}
    })
    $expectedMissed = $measured[0].summary.functions.count - $measured[0].summary.functions.covered
    if ($functions.Count -ne $expectedMissed) { throw 'Uncovered source definitions disagree with LLVM function totals' }
    ConvertTo-Json -InputObject $functions -Depth 8 | Out-File -LiteralPath (Join-Path $directory 'uncovered-functions.json') -Encoding utf8
    $selected = $false
    $lines = @(foreach ($line in [IO.File]::ReadLines((Join-Path $directory 'coverage.lcov'))) {
        if ($line.StartsWith('SF:')) { $selected = $line.Substring(3).Replace('\','/') -eq $sourceName }
        if ($selected -and $line -match '^DA:(\d+),0(?:,|$)') { [int]$Matches[1] }
    })
    ConvertTo-Json -InputObject $lines | Out-File -LiteralPath (Join-Path $directory 'uncovered-lines.json') -Encoding utf8
    $results = if (Test-Path -LiteralPath $ResultsPath) { @(Import-Csv -LiteralPath $ResultsPath) } else { @() }
    $metadata = [ordered]@{
        source=$Coverage.Source; sha256=$Coverage.SourceHash; compilerMajor=$Coverage.Tools.Major
        suitePassed=$Passed; profiles=$raw.Count; objects=$Objects; tests=$results
        scope='Selected mod source in standalone tests and the unload DLL; excluded fixture/SDK code. No live Explorer or displayed-frame coverage.'
        limitations='Execution coverage is not assertion coverage. Crashed or forcibly terminated processes may leave no profile.'
    }
    $metadata | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $directory 'run.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $directory 'summary.txt') | Write-Output
    Write-Output "Coverage HTML: $(Join-Path $directory 'html\index.html')"
    Write-Output "Uncovered functions/lines and run status: $directory"
}

