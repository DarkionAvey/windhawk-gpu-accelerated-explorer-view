# Shared by the build, version starter and regression runner. Windows PowerShell 5.1+.
. (Join-Path $PSScriptRoot 'minify.ps1')
function Get-SourceHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Get-ModVersion([string]$SourcePath) {
    $matches = [regex]::Matches([IO.File]::ReadAllText($SourcePath),
        '(?m)^// @version[ \t]+([0-9]+\.[0-9]+\.[0-9]+)[ \t]*\r?$')
    if ($matches.Count -ne 1) { throw "Expected one Windhawk version header in $SourcePath" }
    return [version]$matches[0].Groups[1].Value
}

function Get-VersionSources([string]$Directory) {
    Get-ChildItem -LiteralPath $Directory -File | Where-Object {
        $_.Name -match '^\d+\.\d+\.\d+\.cpp$'
    } | Sort-Object { [version]$_.BaseName }
}

function New-ModVersion([string]$RepositoryRoot, [string]$Version) {
    $sources = @(Get-VersionSources $RepositoryRoot)
    $archive = Join-Path $RepositoryRoot 'previous-versions'
    if (Test-Path -LiteralPath $archive) { $sources += @(Get-VersionSources $archive) }
    $latest = Join-Path $RepositoryRoot 'latest.cpp'
    if (Test-Path -LiteralPath $latest) { $sources += Get-Item -LiteralPath $latest }
    if (!$sources.Count) { throw 'No existing source to copy for the new version' }
    $base = $sources | Sort-Object { Get-ModVersion $_.FullName } | Select-Object -Last 1
    $baseVersion = Get-ModVersion $base.FullName
    if (!$Version) {
        $Version = '{0}.{1}.{2}' -f $baseVersion.Major, $baseVersion.Minor, ($baseVersion.Build + 1)
    }
    if ($Version -notmatch '^\d+\.\d+\.\d+$' -or [version]$Version -le $baseVersion) {
        throw "New version must be newer than $baseVersion"
    }
    $destination = Join-Path $RepositoryRoot ($Version + '.cpp')
    if ((Test-Path -LiteralPath $destination) -or
        (Test-Path -LiteralPath (Join-Path $archive ($Version + '.cpp')))) {
        throw "Version $Version already exists; historical sources must not be overwritten"
    }
    Copy-Item -LiteralPath $base.FullName -Destination $destination
    $text = [IO.File]::ReadAllText($destination)
    $text = [regex]::Replace($text, '(?m)^(// @version[ \t]+)[0-9.]+', ('${1}' + $Version))
    [IO.File]::WriteAllText($destination, $text, [Text.UTF8Encoding]::new($false))
    return $destination
}

function Sync-ModReadme([string]$SourcePath, [string]$ReadmePath) {
    $source = [IO.File]::ReadAllText($SourcePath)
    $readme = [IO.File]::ReadAllText($ReadmePath)
    if ($readme.Contains('*/') -or $readme.Contains('// ==/WindhawkModReadme==')) {
        throw 'README.md contains a comment terminator or Windhawk delimiter; it cannot be embedded safely'
    }
    $blocks = [regex]::Matches($source,
        '(?ms)^// ==WindhawkModReadme==\r?\n/\*\r?\n.*?^\*/\r?\n// ==/WindhawkModReadme==[ \t]*(?=\r?$)')
    if ($blocks.Count -ne 1) { throw "Expected one Windhawk README block in $SourcePath" }
    $newline = "`n"
    if ($source.Contains("`r`n")) { $newline = "`r`n" }
    $readme = $readme.Replace("`r`n", "`n").TrimEnd([char]10).Replace("`n", $newline)
    $replacement = '// ==WindhawkModReadme==' + $newline + '/*' + $newline +
        $readme + $newline + '*/' + $newline + '// ==/WindhawkModReadme=='
    $block = $blocks[0]
    $updated = $source.Substring(0, $block.Index) + $replacement +
        $source.Substring($block.Index + $block.Length)
    if ($updated -cne $source) {
        [IO.File]::WriteAllText($SourcePath, $updated, [Text.UTF8Encoding]::new($false))
    }
}

function Resolve-WindhawkDirectory([string]$Directory) {
    # Explicit configuration is authoritative: report a bad path instead of
    # silently building against a different installation.
    if (!$Directory) { $Directory = $env:WINDHAWK_HOME }
    if ($Directory) {
        $resolved = (Resolve-Path -LiteralPath $Directory).ProviderPath
        if (!(Test-Path -LiteralPath (Join-Path $resolved 'Compiler\bin\clang++.exe'))) {
            throw "Windhawk compiler not found under $resolved"
        }
        return $resolved
    }
    $candidates = @()
    foreach ($key in @('HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
        $candidates += @(Get-ItemProperty -Path $key -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -like 'Windhawk*' -and $_.InstallLocation } |
            ForEach-Object { $_.InstallLocation })
    }
    $command = Get-Command windhawk.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += Split-Path -Parent $command.Source }
    foreach ($programDirectory in @($env:ProgramW6432, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($programDirectory) { $candidates += Join-Path $programDirectory 'Windhawk' }
    }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'Compiler\bin\clang++.exe')) {
            return (Resolve-Path -LiteralPath $candidate).ProviderPath
        }
    }
    throw 'Windhawk was not found. Install it, set WINDHAWK_HOME, or pass -WindhawkDirectory <installation directory>.'
}

function Assert-ReleaseSources([string]$RepositoryRoot, [string]$SourcePath) {
    $version = Get-ModVersion $SourcePath
    if ((Split-Path -Leaf $SourcePath) -ne ($version.ToString() + '.cpp') -or
        (Split-Path -Parent $SourcePath) -ne $RepositoryRoot) {
        throw 'Release source must be a numbered working file in the repository root. Use the regression runner to test archived sources.'
    }
    $latest = Join-Path $RepositoryRoot 'latest.cpp'
    if ((Test-Path -LiteralPath $latest) -and (Get-ModVersion $latest) -gt $version) {
        throw 'Refusing to replace latest.cpp with an older release'
    }
    $archive = Join-Path $RepositoryRoot 'previous-versions'
    $sources = @(Get-VersionSources $RepositoryRoot)
    $legacy = Join-Path $RepositoryRoot 'mod.cpp'
    if (Test-Path -LiteralPath $legacy) { $sources += Get-Item -LiteralPath $legacy }
    foreach ($file in $sources) {
        if ((Get-ModVersion $file.FullName) -gt $version) { throw "A newer working source exists: $($file.Name)" }
        $preserved = Join-Path $archive $file.Name
        if ((Test-Path -LiteralPath $preserved) -and
            (Get-SourceHash $preserved) -ne (Get-SourceHash $file.FullName)) {
            throw "Archive conflict for $($file.Name); historical sources will not be overwritten"
        }
    }
    if ((Test-Path -LiteralPath $archive) -and
        @(Get-VersionSources $archive | Where-Object { [version]$_.BaseName -gt $version }).Count) {
        throw 'A newer archived release exists; refusing to downgrade latest.cpp'
    }
}

function Publish-ModRelease([string]$RepositoryRoot, [string]$SourcePath, [string]$MinifiedPath) {
    Assert-ReleaseSources $RepositoryRoot $SourcePath
    $archive = Join-Path $RepositoryRoot 'previous-versions'
    New-Item -ItemType Directory -Path $archive -Force | Out-Null
    $staged = Join-Path $RepositoryRoot ('latest.' + [guid]::NewGuid().ToString('N') + '.tmp')
    $stagedExport = Join-Path $RepositoryRoot ('latest.export.' + [guid]::NewGuid().ToString('N') + '.tmp')
    $stagedMinified = Join-Path $RepositoryRoot ('latest.minified.' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Item -LiteralPath $SourcePath -Destination $staged
        Copy-Item -LiteralPath $staged -Destination $stagedExport
        $minifiedText = ConvertTo-MinifiedMod $SourcePath
        if ($MinifiedPath) {
            if ([IO.File]::ReadAllText($MinifiedPath) -cne $minifiedText) {
                throw 'Minified source changed after validation; run the build again'
            }
            Copy-Item -LiteralPath $MinifiedPath -Destination $stagedMinified
        } else {
            [IO.File]::WriteAllText($stagedMinified, $minifiedText, [Text.UTF8Encoding]::new($false))
        }
        $sources = @(Get-VersionSources $RepositoryRoot)
        $legacy = Join-Path $RepositoryRoot 'mod.cpp'
        if (Test-Path -LiteralPath $legacy) { $sources += Get-Item -LiteralPath $legacy }
        foreach ($file in $sources) {
            $destination = Join-Path $archive $file.Name
            if (Test-Path -LiteralPath $destination) {
                # Assert-ReleaseSources has verified this is an identical copy.
                Remove-Item -LiteralPath $file.FullName
            } else {
                Move-Item -LiteralPath $file.FullName -Destination $destination
            }
        }
        $latest = Join-Path $RepositoryRoot 'latest.cpp'
        if (Test-Path -LiteralPath $latest) {
            [IO.File]::Replace($staged, $latest, [NullString]::Value)
        } else {
            Move-Item -LiteralPath $staged -Destination $latest
        }
        $export = Join-Path $RepositoryRoot 'gpu-accelerated-explorer-view.wh.cpp'
        if (Test-Path -LiteralPath $export) {
            [IO.File]::Replace($stagedExport, $export, [NullString]::Value)
        } else {
            Move-Item -LiteralPath $stagedExport -Destination $export
        }
        $minified = Join-Path $RepositoryRoot 'gpu-accelerated-explorer-view.minified.wh.cpp'
        if (Test-Path -LiteralPath $minified) {
            $attributes = [IO.File]::GetAttributes($minified)
            try {
                [IO.File]::SetAttributes($minified, ($attributes -band (-bnot [IO.FileAttributes]::ReadOnly)))
                [IO.File]::Replace($stagedMinified, $minified, [NullString]::Value)
            } finally {
                [IO.File]::SetAttributes($minified, ($attributes -bor [IO.FileAttributes]::ReadOnly))
            }
        } else {
            Move-Item -LiteralPath $stagedMinified -Destination $minified
            [IO.File]::SetAttributes($minified, ([IO.File]::GetAttributes($minified) -bor [IO.FileAttributes]::ReadOnly))
        }
        # Prune only after all published copies are in place. Include the
        # legacy baseline, but leave unrelated files and subdirectories alone.
        $history = @(Get-VersionSources $archive)
        $archivedLegacy = Join-Path $archive 'mod.cpp'
        if (Test-Path -LiteralPath $archivedLegacy -PathType Leaf) {
            $history += Get-Item -LiteralPath $archivedLegacy
        }
        foreach ($obsolete in ($history | Sort-Object { Get-ModVersion $_.FullName } -Descending |
            Select-Object -Skip 15)) {
            Remove-Item -LiteralPath $obsolete.FullName
        }
    } finally {
        if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged }
        if (Test-Path -LiteralPath $stagedExport) { Remove-Item -LiteralPath $stagedExport }
        if (Test-Path -LiteralPath $stagedMinified) { Remove-Item -LiteralPath $stagedMinified -Force }
    }
}
