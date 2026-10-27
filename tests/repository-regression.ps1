$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\repository.ps1')
function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw "Repository regression: $Message" }
}
function Reject([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert $rejected $Message
}
function Write-Utf8([string]$Path, [string]$Value) {
    [IO.File]::WriteAllText($Path, $Value, [Text.UTF8Encoding]::new($false))
}
$temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixture = Join-Path $temporary ('gpu-overlay-repository-' + [guid]::NewGuid().ToString('N') + ' space [fixture] ' + [char]0x062A)
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $readme = Join-Path $fixture 'README.md'
    $text = '# User documentation' + "`n`n" + 'Literal $1, ${name}, backticks ` and Unicode ' + [char]0x062A + "`n"
    Write-Utf8 $readme $text
    $original = "// @version         0.9.9`r`n// ==WindhawkModReadme==`r`n/*`r`nold`r`n*/`r`n// ==/WindhawkModReadme==`r`nint unchanged = 42;`r`n"
    $old = Join-Path $fixture '0.9.9.cpp'
    Write-Utf8 $old $original
    $oldHash = (Get-SourceHash $old)
    $legacy = Join-Path $fixture 'mod.cpp'
    Write-Utf8 $legacy ($original.Replace('0.9.9', '0.8.0'))
    $legacyHash = Get-SourceHash $legacy
    $new = New-ModVersion $fixture '0.9.10'
    Assert ((Get-ModVersion $new) -eq [version]'0.9.10') 'version header was not updated'
    Sync-ModReadme $new $readme
    $embedded = [regex]::Match([IO.File]::ReadAllText($new), '(?s)/\*\r?\n(.*?)\*/').Groups[1].Value.Replace("`r`n", "`n")
    Assert ($embedded -ceq $text) 'README content changed while embedding'
    Assert ([IO.File]::ReadAllText($new).EndsWith("int unchanged = 42;`r`n")) 'sync changed the C++ body'
    $newHash = (Get-SourceHash $new)
    Sync-ModReadme $new $readme
    Assert ((Get-SourceHash $new) -eq $newHash) 'README sync is not idempotent'
    Write-Utf8 $readme 'unsafe */ text'
    Reject { Sync-ModReadme $new $readme } 'unsafe README was accepted'
    Assert ((Get-SourceHash $new) -eq $newHash) 'rejected README modified the source'
    Write-Utf8 $readme $text
    Publish-ModRelease $fixture $new
    $latest = Join-Path $fixture 'latest.cpp'
    $export = Join-Path $fixture 'gpu-accelerated-explorer-view.wh.cpp'
    $archive = Join-Path $fixture 'previous-versions'
    Assert ((Get-SourceHash $latest) -eq $newHash) 'latest differs from the tested source'
    Assert ((Get-SourceHash $export) -eq $newHash) 'Windhawk export differs from the tested source'
    Assert ((Get-SourceHash (Join-Path $archive '0.9.9.cpp')) -eq $oldHash) 'historical bytes changed'
    Assert ((Get-SourceHash (Join-Path $archive '0.9.10.cpp')) -eq $newHash) 'archive differs from latest'
    Assert ((Get-SourceHash (Join-Path $archive 'mod.cpp')) -eq $legacyHash) 'legacy baseline changed below retention limit'
    Assert (@(Get-VersionSources $fixture).Count -eq 0) 'numbered sources remain at root'
    Copy-Item -LiteralPath (Join-Path $archive '0.9.10.cpp') -Destination $new
    Publish-ModRelease $fixture $new
    Assert ((Get-SourceHash $latest) -eq $newHash) 'identical archive retry failed'
    Assert ((Get-SourceHash $export) -eq $newHash) 'identical archive retry changed the Windhawk export'
    foreach ($patch in 1..18) {
        Write-Utf8 (Join-Path $archive ("0.8.$patch.cpp")) ($original.Replace('0.9.9', "0.8.$patch"))
    }
    Write-Utf8 $old ($original + 'conflict')
    Copy-Item -LiteralPath (Join-Path $archive '0.9.10.cpp') -Destination $new
    Reject { Publish-ModRelease $fixture $new } 'conflicting history was overwritten'
    Assert ((Test-Path -LiteralPath $new) -and (Test-Path -LiteralPath $old)) 'archive conflict moved working files'
    Assert ((Get-SourceHash $latest) -eq $newHash) 'archive conflict changed latest'
    Assert ((Get-SourceHash $export) -eq $newHash) 'archive conflict changed the Windhawk export'
    Assert (@(Get-VersionSources $archive).Count -eq 20) 'archive conflict pruned old versions'
    Assert ((Get-SourceHash (Join-Path $archive 'mod.cpp')) -eq $legacyHash) 'archive conflict pruned the baseline'
    Remove-Item -LiteralPath $old, $new
    Copy-Item -LiteralPath (Join-Path $archive '0.9.9.cpp') -Destination $old
    Reject { Publish-ModRelease $fixture $old } 'older release replaced latest'
    Remove-Item -LiteralPath $old
    Reject { New-ModVersion $fixture '0.9.9' } 'existing version was accepted'
    $next = New-ModVersion $fixture
    Assert ((Split-Path -Leaf $next) -eq '0.9.11.cpp') 'automatic version sorting/increment failed'
    Remove-Item -LiteralPath $next

    $notes = Join-Path $archive 'notes.txt'
    $unrelatedSource = Join-Path $archive 'reference.cpp'
    $nested = Join-Path $archive 'nested'
    New-Item -ItemType Directory -Path $nested | Out-Null
    $nestedSource = Join-Path $nested '99.0.0.cpp'
    Write-Utf8 $notes 'Unrelated archive notes'
    Write-Utf8 $unrelatedSource 'Unrelated C++ source'
    Write-Utf8 $nestedSource ($original.Replace('0.9.9', '99.0.0'))
    $nestedHash = Get-SourceHash $nestedSource
    $archiveHashes = @{}
    foreach ($file in (Get-ChildItem -LiteralPath $archive -File)) {
        $archiveHashes[$file.Name] = Get-SourceHash $file.FullName
    }

    # Exercise the real release orchestration with an isolated failing runner.
    # No installed mod, Explorer process or system clipboard is involved.
    $scripts = Join-Path $fixture 'scripts'
    $tests = Join-Path $fixture 'tests'
    $fakeWindhawk = Join-Path $fixture 'portable Windhawk'
    New-Item -ItemType Directory -Path $scripts, $tests, (Join-Path $fakeWindhawk 'Compiler\bin') -Force | Out-Null
    foreach ($name in @('repository.ps1', 'build.ps1')) {
        Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) ('scripts\' + $name)) -Destination (Join-Path $scripts $name)
    }
    Write-Utf8 (Join-Path $fakeWindhawk 'Compiler\bin\clang++.exe') ''
    Write-Utf8 (Join-Path $tests 'repository-regression.ps1') '# Isolated orchestration fixture.'
    $runner = Join-Path $tests 'run-regressions.ps1'
    Write-Utf8 $runner 'param($SourceFile, $WindhawkDirectory); throw "deliberate regression failure"'
    $build = Join-Path $scripts 'build.ps1'
    $outside = Get-Location
    try {
        Set-Location -LiteralPath $temporary
        Reject { & $build -WindhawkDirectory $fakeWindhawk } 'failed regression published a release'
        Assert ((Get-SourceHash $latest) -eq $newHash) 'failed tests changed latest'
        Assert ((Get-SourceHash $export) -eq $newHash) 'failed tests changed the Windhawk export'
        Assert (Test-Path -LiteralPath $next) 'failed tests discarded the working source'
        Assert (!(Test-Path -LiteralPath (Join-Path $archive '0.9.11.cpp'))) 'failed tests archived the candidate'
        foreach ($name in $archiveHashes.Keys) {
            Assert ((Get-SourceHash (Join-Path $archive $name)) -eq $archiveHashes[$name]) 'failed tests changed or pruned an archive'
        }
        Write-Utf8 $runner 'param($SourceFile, $WindhawkDirectory); [IO.File]::AppendAllText($SourceFile, "changed during testing")'
        Reject { & $build -WindhawkDirectory $fakeWindhawk } 'source changed during testing was published'
        Assert ((Get-SourceHash $export) -eq $newHash) 'source-change guard published the Windhawk export'
        foreach ($name in $archiveHashes.Keys) {
            Assert ((Get-SourceHash (Join-Path $archive $name)) -eq $archiveHashes[$name]) 'source-change guard changed or pruned an archive'
        }
        Write-Utf8 $runner 'param($SourceFile, $WindhawkDirectory)'
        & $build -WindhawkDirectory $fakeWindhawk
        Assert ((Get-ModVersion $latest) -eq [version]'0.9.11') 'successful run did not finalize'
        Assert ((Get-SourceHash $latest) -eq
            (Get-SourceHash (Join-Path $archive '0.9.11.cpp'))) 'final published/archive copies differ'
        Assert ((Get-SourceHash $export) -eq (Get-SourceHash $latest)) 'final Windhawk export differs from latest'
        $retained = @(Get-VersionSources $archive)
        $expectedNames = @(7..18 | ForEach-Object { "0.8.$_.cpp" }) + @('0.9.9.cpp', '0.9.10.cpp', '0.9.11.cpp')
        Assert (($retained.Name -join ',') -eq ($expectedNames -join ',')) 'retention did not keep the 15 numerically newest versions'
        Assert (!(Test-Path -LiteralPath (Join-Path $archive 'mod.cpp'))) 'obsolete legacy baseline was retained'
        foreach ($file in ($retained | Select-Object -First 14)) {
            Assert ((Get-SourceHash $file.FullName) -eq $archiveHashes[$file.Name]) 'retention changed historical bytes'
        }
        foreach ($name in @('notes.txt', 'reference.cpp')) {
            Assert ((Get-SourceHash (Join-Path $archive $name)) -eq $archiveHashes[$name]) 'retention changed an unrelated file'
        }
        Assert ((Get-SourceHash $nestedSource) -eq $nestedHash) 'retention changed a nested source'
        Copy-Item -LiteralPath (Join-Path $archive '0.9.11.cpp') -Destination $next
        & $build -SourceFile '0.9.11.cpp' -WindhawkDirectory $fakeWindhawk
        Assert ((@(Get-VersionSources $archive).Name -join ',') -eq ($retained.Name -join ',')) 'retention is not idempotent at 15 versions'
        Assert ((Split-Path -Leaf (New-ModVersion $fixture)) -eq '0.9.12.cpp') 'version creation failed after pruning'
    } finally { Set-Location -LiteralPath $outside.Path }
    $previousWindhawk = $env:WINDHAWK_HOME
    try {
        $env:WINDHAWK_HOME = $fakeWindhawk
        Assert ((Resolve-WindhawkDirectory) -eq $fakeWindhawk) 'WINDHAWK_HOME was not used'
        Reject { Resolve-WindhawkDirectory (Join-Path $fixture 'missing installation') } 'bad explicit toolchain silently fell back'
    } finally { $env:WINDHAWK_HOME = $previousWindhawk }
    Write-Output 'PASS: README sync, version preservation, 15-version retention, archive conflicts, failed-test guard, source-change guard, portable paths and release publication'
} finally {
    # Recursive cleanup is restricted to this uniquely named private fixture.
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolved) -ne $temporary -or
        (Split-Path -Leaf $resolved) -notlike 'gpu-overlay-repository-*') {
        throw 'Refusing cleanup outside the repository-test fixture'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
