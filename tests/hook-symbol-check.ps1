# Checks, beside a regression run, that what the mod hooks or looks up in
# Windows still exists across Windows builds. Advisory only: it never changes
# a test or build result. The list of names is read from the mod source each
# run (no manifest to keep):
#   - Windhawk symbol hooks (WindhawkUtils::SYMBOL_HOOK) and their module,
#   - function hooks on exported APIs (WindhawkUtils::SetFunctionHook(Name, ...)),
#   - functions looked up by name (GetProcAddress(module, "Name")).
# Builds come from Winbindex (winbindex.m417z.com: every Windows 10/11 release
# and preview update; not Insider Canary/Dev), binaries and PDBs from the
# Microsoft symbol server. Hooks through COM interface tables are not named
# and are not checked. Cache: %LOCALAPPDATA%\gpu-overlay\symbol-cache.
param(
    [Parameter(Mandatory)][string]$SourceFile,
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$CacheDirectory = (Join-Path $env:LOCALAPPDATA 'gpu-overlay\symbol-cache'),
    # Network: each request is retried until this much time has passed.
    [int]$RetrySeconds = 120
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$reportPath = Join-Path $OutputDirectory 'hook-symbol-check.json'
$summaryPath = Join-Path $OutputDirectory 'hook-symbol-check.txt'
$lines = [Collections.Generic.List[string]]::new()
function Say([string]$text) { $lines.Add($text) }
function Finish([string]$status, $items = @(), $builds = @()) {
    [IO.File]::WriteAllText($summaryPath, ($lines -join "`r`n") + "`r`n")
    [pscustomobject]@{ Status=$status; Items=$items; Builds=$builds; Summary=@($lines) } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}

# ---- Network, with retries for an unreachable server or a blocked connection.
$script:offline = $null
function Get-Web([string]$Uri, [string]$OutFile) {
    # After one request ran out of retries, the rest are not tried.
    if ($script:offline) { throw $script:offline }
    $started = [Diagnostics.Stopwatch]::StartNew()
    $partial = "$OutFile.partial"
    while ($true) {
        try {
            Invoke-WebRequest -Uri $Uri -OutFile $partial -UseBasicParsing -TimeoutSec 60
            Move-Item -LiteralPath $partial -Destination $OutFile -Force
            return
        } catch {
            $status = $_.Exception.Response.StatusCode.value__
            if ($status -eq 404) { throw "not on the server (404): $Uri" }
            if ($started.Elapsed.TotalSeconds -ge $RetrySeconds) {
                $script:offline = "unreachable after $RetrySeconds s ($($_.Exception.Message)): $Uri"
                throw $script:offline
            }
            Start-Sleep -Seconds 10
        }
    }
}

# ---- PE files: exports and the PDB identity.
function Read-Pe([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    $sections = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
    $optional = $pe + 24
    if ([BitConverter]::ToUInt16($bytes, $optional) -ne 0x20b) { throw "not an x64 image: $Path" }
    $table = @()
    for ($s = 0; $s -lt $sections; $s++) {
        $h = $optional + $optionalSize + 40 * $s
        $table += [pscustomobject]@{ Rva=[BitConverter]::ToUInt32($bytes, $h + 12)
            Size=[Math]::Max([BitConverter]::ToUInt32($bytes, $h + 8), [BitConverter]::ToUInt32($bytes, $h + 16))
            Raw=[BitConverter]::ToUInt32($bytes, $h + 20) }
    }
    $offset = { param($rva) foreach ($t in $table) { if ($rva -ge $t.Rva -and $rva -lt $t.Rva + $t.Size) { return $t.Raw + $rva - $t.Rva } }; return $null }
    $ascii = { param($at) $end = $at; while ($bytes[$end] -ne 0) { $end++ }; [Text.Encoding]::ASCII.GetString($bytes, $at, $end - $at) }
    $exports = [Collections.Generic.HashSet[string]]::new()
    $exportRva = [BitConverter]::ToUInt32($bytes, $optional + 112)
    if ($exportRva) {
        $e = & $offset $exportRva
        $count = [BitConverter]::ToUInt32($bytes, $e + 24)
        $names = & $offset ([BitConverter]::ToUInt32($bytes, $e + 32))
        for ($i = 0; $i -lt $count; $i++) {
            [void]$exports.Add((& $ascii (& $offset ([BitConverter]::ToUInt32($bytes, $names + 4 * $i)))))
        }
    }
    $pdb = $null
    $debugRva = [BitConverter]::ToUInt32($bytes, $optional + 112 + 6 * 8)
    $debugSize = [BitConverter]::ToUInt32($bytes, $optional + 112 + 6 * 8 + 4)
    $d = if ($debugRva) { & $offset $debugRva } else { $null }
    if ($null -ne $d) {
        for ($entry = 0; $entry -lt $debugSize; $entry += 28) {
            if ([BitConverter]::ToUInt32($bytes, $d + $entry + 12) -ne 2) { continue }
            $record = [BitConverter]::ToUInt32($bytes, $d + $entry + 24)
            if ([BitConverter]::ToUInt32($bytes, $record) -ne 0x53445352) { continue }
            $guid = [guid]::new([byte[]]$bytes[($record + 4)..($record + 19)])
            $age = [BitConverter]::ToUInt32($bytes, $record + 20)
            $pdb = [pscustomobject]@{ Name=[IO.Path]::GetFileName((& $ascii ($record + 24)))
                Identity=$guid.ToString('N').ToUpperInvariant() + $age.ToString('X') }
            break
        }
    }
    [pscustomobject]@{ Exports=$exports; Pdb=$pdb }
}

try {
    $source = [IO.File]::ReadAllText($SourceFile)
    # ---- What the mod uses, read from its source.
    $moduleOf = {
        param([string]$variable, [int]$before)
        $assignments = [regex]::Matches($source.Substring(0, $before),
            '\b' + [regex]::Escape($variable) + '\s*=\s*(?:GetModuleHandleW|LoadLibraryExW|LoadLibraryW)\(\s*L"([^"]+)"')
        if ($assignments.Count) { $assignments[$assignments.Count - 1].Groups[1].Value } else { $null }
    }
    $items = [Collections.Generic.List[object]]::new()
    foreach ($array in [regex]::Matches($source, 'WindhawkUtils::SYMBOL_HOOK\s+(\w+)\s*\[\]\s*\{(.*?)\n\s*\};', 'Singleline')) {
        $call = [regex]::Match($source.Substring($array.Index), 'HookSymbols\(\s*(\w+)\s*,\s*' + [regex]::Escape($array.Groups[1].Value) + '\b')
        $module = if ($call.Success) { & $moduleOf $call.Groups[1].Value $array.Index } else { $null }
        foreach ($entry in [regex]::Matches($array.Groups[2].Value, '\{\{\s*((?:L"[^"]*"\s*,?\s*)+)\}\s*,[^{}]*?,\s*(true|false)\s*\}', 'Singleline')) {
            $names = @([regex]::Matches($entry.Groups[1].Value, 'L"([^"]*)"') | ForEach-Object { $_.Groups[1].Value })
            $items.Add([pscustomobject]@{ Kind='symbol'; Module=$module; Names=$names; Optional=($entry.Groups[2].Value -eq 'true') })
        }
    }
    $hooked = @([regex]::Matches($source, 'SetFunctionHook\(\s*([A-Za-z_]\w*)\s*,') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $vtableHooks = [regex]::Matches($source, 'SetFunctionHook\(\s*reinterpret_cast').Count
    foreach ($lookup in [regex]::Matches($source, 'GetProcAddress\(\s*(?:GetModuleHandleW\(\s*L"([^"]+)"\s*\)|(\w+))\s*,\s*"(\w+)"\s*\)')) {
        $module = if ($lookup.Groups[1].Success) { $lookup.Groups[1].Value } else { & $moduleOf $lookup.Groups[2].Value $lookup.Index }
        if ($module -and !($items | Where-Object { $_.Kind -eq 'lookup' -and $_.Module -eq $module -and $_.Names[0] -eq $lookup.Groups[3].Value })) {
            $items.Add([pscustomobject]@{ Kind='lookup'; Module=$module; Names=@($lookup.Groups[3].Value); Optional=$true })
        }
    }
    # Hooked APIs: the system DLL that exports each (this PC), among the DLLs
    # the source names and the usual Win32 ones.
    $candidates = @([regex]::Matches($source, 'L"([\w\.-]+\.dll)"') | ForEach-Object { $_.Groups[1].Value }) +
        @('user32.dll','kernel32.dll','kernelbase.dll','gdi32.dll','win32u.dll','ole32.dll','combase.dll','shell32.dll',
          'shlwapi.dll','comctl32.dll','uxtheme.dll','dwmapi.dll','advapi32.dll') | Sort-Object -Unique
    $localPe = @{}
    $local = { param($dll) if (!$localPe.ContainsKey($dll)) {
        $path = Join-Path ([Environment]::SystemDirectory) $dll
        $localPe[$dll] = if (Test-Path -LiteralPath $path) { Read-Pe $path } else { $null } }; $localPe[$dll] }
    foreach ($api in $hooked) {
        $owner = $candidates | Where-Object { $pe = & $local $_; $pe -and $pe.Exports.Contains($api) } | Select-Object -First 1
        if ($owner) { $items.Add([pscustomobject]@{ Kind='export'; Module=$owner; Names=@($api); Optional=$false }) }
        # A variable holding a looked-up function: checked as that lookup.
        elseif ($source -notmatch ('\b' + [regex]::Escape($api) + '\s*=[^;]*GetProcAddress')) {
            Say "NOTE: $api is hooked but no system DLL here exports it by that name (not checked)."
        }
    }
    foreach ($item in $items) { if (!$item.Module) { Say "NOTE: no module found in the source for $($item.Names[0]) (not checked)." } }
    $items = @($items | Where-Object Module)
    if (!$items.Count) { Say 'Hook check: nothing to check was found in the source.'; Finish 'Skipped'; return }
    Say ("Hook check: {0} symbol hooks, {1} hooked exports, {2} looked-up functions read from the source; {3} COM-table hooks are not named and not checked." -f
        @($items | Where-Object Kind -eq 'symbol').Count, @($items | Where-Object Kind -eq 'export').Count,
        @($items | Where-Object Kind -eq 'lookup').Count, $vtableHooks)

    # ---- The symbol reader (Windows' dbghelp), built once per source change.
    New-Item -ItemType Directory -Path $CacheDirectory -Force | Out-Null
    $readerSource = Join-Path $PSScriptRoot 'hook-symbol-reader.cpp'
    $reader = Join-Path $CacheDirectory ('hook-symbol-reader-' + (Get-FileHash -LiteralPath $readerSource).Hash.Substring(0, 12) + '.exe')
    if (!(Test-Path -LiteralPath $reader)) {
        & $Compiler -std=c++20 -O1 -target x86_64-w64-mingw32 -municode -static $readerSource -ldbghelp -o $reader 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'the symbol reader did not compile' }
    }
    $symbolCache = @{}
    $symbolsOf = { param([string]$dll, [string]$image)
        $pe = Read-Pe $image
        if (!$pe.Pdb) { return $null }
        $key = $pe.Pdb.Name + '\' + $pe.Pdb.Identity
        if ($symbolCache.ContainsKey($key)) { return $symbolCache[$key] }
        $directory = Join-Path $CacheDirectory ('pdb\' + $key)
        $pdbPath = Join-Path $directory $pe.Pdb.Name
        if (!(Test-Path -LiteralPath $pdbPath)) {
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
            Get-Web "https://msdl.microsoft.com/download/symbols/$($pe.Pdb.Name)/$($pe.Pdb.Identity)/$($pe.Pdb.Name)" $pdbPath
        }
        $names = [Collections.Generic.HashSet[string]]::new()
        foreach ($line in (& $reader $image $directory)) { [void]$names.Add(($line -split "`t")[0]) }
        if ($LASTEXITCODE -ne 0) { throw "the symbol reader failed on $dll (exit $LASTEXITCODE)" }
        $symbolCache[$key] = $names
        return $names
    }
    $present = { param($item, [string]$image)
        if ($item.Kind -eq 'symbol') {
            $names = & $symbolsOf $item.Module $image
            if ($null -eq $names) { return $null }
            return [bool]($item.Names | Where-Object { $names.Contains($_) })
        }
        return (Read-Pe $image).Exports.Contains($item.Names[0])
    }

    # ---- Self-check on this PC: the reader must find what the mod uses here.
    $selfOk = $true
    foreach ($item in $items | Where-Object Kind -ne 'lookup') {
        $image = Join-Path ([Environment]::SystemDirectory) $item.Module
        $found = if (Test-Path -LiteralPath $image) { & $present $item $image } else { $false }
        if (!$found) { $selfOk = $false; Say "THIS PC: $($item.Module) lacks $($item.Names[0])" }
    }
    if (!$selfOk) { Say 'Hook check: this PC already lacks the names above (they fail here today, or the source was misread).' }

    # ---- Builds: the first and the latest of every Windows 11 version and of
    # Windows 10 22H2, per DLL, from Winbindex.
    $indexDirectory = Join-Path $CacheDirectory 'winbindex'
    New-Item -ItemType Directory -Path $indexDirectory -Force | Out-Null
    $builds = [Collections.Generic.List[object]]::new()
    $results = @{}
    foreach ($dll in @($items | ForEach-Object Module | Sort-Object -Unique)) {
        $indexFile = Join-Path $indexDirectory "$dll.json.gz"
        if (!(Test-Path -LiteralPath $indexFile) -or (Get-Item -LiteralPath $indexFile).LastWriteTime -lt (Get-Date).AddHours(-12)) {
            Get-Web "https://winbindex.m417z.com/data/by_filename_compressed/$($dll.ToLowerInvariant()).json.gz" $indexFile
        }
        $stream = [IO.Compression.GZipStream]::new([IO.File]::OpenRead($indexFile), [IO.Compression.CompressionMode]::Decompress)
        try { $index = [IO.StreamReader]::new($stream).ReadToEnd() | ConvertFrom-Json } finally { $stream.Dispose() }
        $releases = foreach ($property in $index.PSObject.Properties) {
            $file = $property.Value.fileInfo
            if ($file.machineType -ne 34404 -or !$file.timestamp -or !$file.virtualSize) { continue }
            foreach ($version in $property.Value.windowsVersions.PSObject.Properties) {
                if ($version.Name -notlike '11-*' -and $version.Name -ne '22H2') { continue }
                foreach ($update in $version.Value.PSObject.Properties) {
                    $info = if ($update.Value.updateInfo) { $update.Value.updateInfo } else { $update.Value.windowsVersionInfo }
                    [pscustomobject]@{ Line=$version.Name; Build=if ($info.releaseVersion) { $info.releaseVersion } else { $update.Name }
                        Date=[string]$info.releaseDate; Timestamp=[uint32]$file.timestamp; VirtualSize=[uint32]$file.virtualSize
                        Size=if ($file.size) { [int64]$file.size } else { 0 } }
                }
            }
        }
        # A file the symbol server serves that is not the whole binary
        # (newest updates can be hotpatch images there): the
        # version's previous build stands in for it.
        $fetch = { param($pick)
            $id = '{0:X8}{1:x}' -f $pick.Timestamp, $pick.VirtualSize
            $image = Join-Path $CacheDirectory "bin\$dll\$id\$dll"
            if (!(Test-Path -LiteralPath $image)) {
                New-Item -ItemType Directory -Path (Split-Path $image) -Force | Out-Null
                Get-Web "https://msdl.microsoft.com/download/symbols/$dll/$id/$dll" $image
            }
            $length = (Get-Item -LiteralPath $image).Length
            # (The server keeps unsigned copies: a little smaller than the signed
            # file.) Hotpatch updates carry only the patched functions: an image
            # far smaller than this PC's full copy is not the whole binary.
            $full = Join-Path ([Environment]::SystemDirectory) $dll
            $reference = if (Test-Path -LiteralPath $full) { (Get-Item -LiteralPath $full).Length } else { 0 }
            if ($length -lt $pick.VirtualSize / 4 -or $length -lt $reference / 4) { return $null }
            return $image
        }
        foreach ($line in @($releases | Group-Object Line)) {
            $sorted = @($line.Group | Sort-Object Date, Build)
            foreach ($newest in $false, $true) {
                $candidates = if ($newest) { @($sorted[($sorted.Count - 1)..0]) } else { $sorted }
                $image = $null; $problem = $null; $pick = $null; $tried = 0
                foreach ($candidate in $candidates) {
                    if ($tried -ge 4) { break }
                    $tried++
                    try { $image = & $fetch $candidate } catch { $problem = "$_"; break }
                    if ($image) { $pick = $candidate; break }
                }
                if (!$pick -and !$problem) { $problem = 'the symbol server only had partial (hotpatch) images of the latest builds' }
                if ($pick -and @($builds | Where-Object { $_.Dll -eq $dll -and $_.Image -eq $image }).Count) { continue }
                $builds.Add([pscustomobject]@{ Dll=$dll; Line=$line.Name; Build=if ($pick) { $pick.Build } else { '?' }
                    Date=if ($pick) { $pick.Date } else { '' }; Image=$image; Error=$problem })
            }
        }
    }
    if (!@($builds | Where-Object { !$_.Error }).Count) {
        $reason = ($builds | Select-Object -First 1).Error
        Say "Hook check skipped: no Windows builds could be downloaded ($reason). Offline, or a firewall blocks winbindex.m417z.com / msdl.microsoft.com."
        Finish 'Skipped' $items; return
    }

    # ---- Each name in each build.
    $order = { param($line) if ($line -eq '22H2') { '10-22H2' } else { $line } }
    $findings = 0
    foreach ($item in $items) {
        $row = foreach ($build in $builds | Where-Object { $_.Dll -eq $item.Module -and !$_.Error } | Sort-Object Date) {
            $found = try { & $present $item $build.Image } catch { $null }
            [pscustomobject]@{ Line=& $order $build.Line; Build=$build.Build; Date=$build.Date; Found=$found }
        }
        $row = @($row)
        $item | Add-Member -NotePropertyName Builds -NotePropertyValue $row
        $known = @($row | Where-Object { $null -ne $_.Found })
        if (!$known.Count) { continue }
        $missing = @($known | Where-Object { !$_.Found })
        if (!$missing.Count) { continue }
        $findings++
        $label = "$($item.Module)!$($item.Names[0])" + $(if ($item.Optional) { ' (optional)' } else { '' })
        $latest = $known[-1]
        $first = $known | Where-Object Found | Select-Object -First 1
        if (!$latest.Found) {
            Say "CHANGED: $label is missing in the newest checked build $($latest.Line) $($latest.Build) ($($latest.Date)); the hook will not apply there."
        } elseif ($first -and $known.IndexOf($first) -gt 0) {
            Say "SINCE: $label exists from $($first.Line) $($first.Build) ($($first.Date)); missing in older checked builds."
        } else {
            Say ("GAP: $label is missing in " + (($missing | ForEach-Object { "$($_.Line) $($_.Build)" }) -join ', ') + '.')
        }
    }
    $checked = @($builds | Where-Object { !$_.Error } | ForEach-Object { "$($_.Line) $($_.Build)" } | Sort-Object -Unique).Count
    $failedDownloads = @($builds | Where-Object Error).Count
    if (!$findings) { Say "Hook check: all $($items.Count) names exist in every checked build ($checked builds, Windows 10 22H2 and every Windows 11 version, first and latest)." }
    else { Say "Hook check: $findings of $($items.Count) names are missing somewhere (above); $checked builds checked." }
    if ($failedDownloads) { Say "Hook check: $failedDownloads build downloads failed and were not checked." }
    Say 'Insider Canary/Dev and ARM64 builds are not in Winbindex and are not checked (ARM64 binaries export and decorate the same names).'
    Finish $(if ($findings -or !$selfOk) { 'Changed' } else { 'Valid' }) $items $builds
} catch {
    Say "Hook check could not finish: $_"
    Finish 'Error'
}
