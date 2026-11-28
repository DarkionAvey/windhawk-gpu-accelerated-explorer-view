# Live diagnostic (not a regression): how File Explorer chooses between its item
# and folder (empty-space) context menu.
#
#   1. Install tests/menu-decision-trace.wh.cpp in Windhawk as a separate mod and
#      enable its debug logging. Disable the GPU overlay mod for the run, so the
#      native list keeps its real geometry for the mouse requests.
#   2. powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-menu-decision-probe.ps1
#      An Explorer window opens on a private temporary folder; four menus open
#      and close by themselves. Do not use the mouse or keyboard meanwhile.
#   3. Save the Windhawk log, then:
#      powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-menu-decision-probe.ps1 -Symbolize <log>
#      This downloads the public symbols of the logged Windows modules into
#      tests/.symbol-cache and writes <log>.symbolized.txt.
param([string]$WindhawkDirectory, [string]$Symbolize)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/repository.ps1')
$toolchain = Resolve-WindhawkDirectory $WindhawkDirectory
$compiler = Join-Path $toolchain 'Compiler/bin/clang++.exe'
$output = Join-Path $PSScriptRoot '.build'
[void][IO.Directory]::CreateDirectory($output)

if ($Symbolize) {
    $log = (Resolve-Path -LiteralPath $Symbolize).Path
    # The trace names each module's path once per Explorer session, so a log
    # saved later may hold frames only. Those names are resolved to the paths
    # the running Explorer has loaded (the same builds as in the trace).
    $paths = @{}
    foreach ($match in (Select-String -LiteralPath $log -Pattern '\[MenuTrace\] module (.+)$')) {
        $path = $match.Matches[0].Groups[1].Value.Trim()
        $paths[[IO.Path]::GetFileName($path).ToLowerInvariant()] = $path
    }
    $names = @(Select-String -LiteralPath $log -Pattern 'stack\[\d+\] ([^ +]+)\+0x' -AllMatches |
        ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } | Sort-Object -Unique)
    $loaded = @{}
    foreach ($process in @(Get-Process explorer -ErrorAction SilentlyContinue)) {
        foreach ($module in @($process.Modules)) { $loaded[$module.ModuleName.ToLowerInvariant()] = $module.FileName }
    }
    foreach ($name in $names) { if (-not $paths.ContainsKey($name) -and $loaded.ContainsKey($name)) { $paths[$name] = $loaded[$name] } }
    $modules = @($paths.Values | Where-Object { $_ -like "$env:SystemRoot\*" -and (Test-Path -LiteralPath $_) } | Sort-Object -Unique)
    if (-not $modules.Count) { throw 'No Windows module of the trace could be located.' }
    $missing = @($names | Where-Object { -not $paths.ContainsKey($_) })
    if ($missing.Count) { Write-Warning ("Frames left as offsets (module not found): " + ($missing -join ', ')) }
    $source = Join-Path $output 'menu-trace-input.txt'
    [IO.File]::WriteAllLines($source, [string[]](@($modules | ForEach-Object { "[MenuTrace] module $_" }) +
        [IO.File]::ReadAllLines($log)))
    & (Join-Path $PSScriptRoot 'fetch-windows-symbols.ps1') -ModuleNames $modules
    $symbolizer = Join-Path $output 'menu-trace-symbolizer.exe'
    & $compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-O2' '-municode' '-static' `
        (Join-Path $PSScriptRoot 'menu-trace-symbolizer.cpp') '-o' $symbolizer '-ldbghelp'
    if ($LASTEXITCODE -ne 0) { throw "Symbolizer compilation failed: $LASTEXITCODE" }
    $result = "$log.symbolized.txt"
    & $symbolizer $source (Join-Path $PSScriptRoot '.symbol-cache') $result
    if ($LASTEXITCODE -ne 0) { throw "Symbolizing failed: $LASTEXITCODE" }
    Write-Output "Wrote $result"
    return
}

$probe = Join-Path $output 'menu-decision-probe.exe'
& $compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-O2' '-municode' '-static' '-DUNICODE' '-D_UNICODE' `
    (Join-Path $PSScriptRoot 'menu-decision-probe.cpp') '-o' $probe `
    '-lole32' '-loleaut32' '-luuid' '-lshell32' '-lshlwapi' '-luser32'
if ($LASTEXITCODE -ne 0) { throw "Probe compilation failed: $LASTEXITCODE" }
& $probe (Join-Path $output 'menu-probe-log.txt')
if ($LASTEXITCODE -ne 0) { throw "Probe failed: $LASTEXITCODE" }
