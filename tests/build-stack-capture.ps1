param([string]$WindhawkDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\repository.ps1')
$WindhawkDirectory = Resolve-WindhawkDirectory $WindhawkDirectory
$compiler = Join-Path $WindhawkDirectory 'Compiler\bin\clang++.exe'
$package = Join-Path $PSScriptRoot '.build\explorer-ui-stack-capture'
New-Item -ItemType Directory -Path $package -Force | Out-Null
& $compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-O2' '-municode' '-static' '-Wall' '-Wextra' `
    (Join-Path $PSScriptRoot 'live-stall-sampler.cpp') '-o' (Join-Path $package 'live-stall-sampler.exe') '-ldbghelp'
if ($LASTEXITCODE -ne 0) { throw "Stack sampler build failed: $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'capture-explorer-ui-stacks.ps1') -Destination $package
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'capture-explorer-ui-stacks.cmd') -Destination (Join-Path $package 'Capture.cmd')
@'
Explorer UI stack capture - Windows x64

1. Extract every file in this ZIP to one folder on the PC where the lag occurs.
2. Enable the mod's logging. Navigate once in the affected File Explorer window,
   then save the current Windhawk log to a text file.
3. Double-click Capture.cmd. Select that Explorer window. Drag the saved log into
   the capture window at its prompt and press Enter. The launcher finds the
   renderer belonging to that window's overlay; no thread number is needed.
4. Keep that window/tab open. Press Enter to begin, switch to it and repeat
   Back/Forward swipes during capture. Keep Windhawk logging enabled.
5. Return to the capture window. Send its generated Explorer-UI-stacks-*.zip
   (containing BOTH UI and renderer stacks), plus a NEW Windhawk log saved after
   these swipes. The earlier log was used only to select the renderer.

No installation or symbol download is required. This samples the selected UI
and renderer stacks with a brief pause for each copy; it does not dump all Explorer memory,
inject input or change focus or clipboard. The log records module paths, build
identities, all captured stacks, QPC sample timestamps and the selected window
title for later analysis alongside Windhawk's QPC pipeline records.
Ctrl+C stops collection after the current thread snapshot has resumed.

If capture reports access denied, right-click Capture.cmd and run as administrator.
Optional duration: Capture.cmd -Seconds 20
Optional log selection: Capture.cmd -WindhawkLog "C:\path\to\current-log.txt"
Alternatively enter the current renderer thread number at the prompt, or use
-AdditionalThreadId followed by it. Use tid from a frame.begin pipeline record,
not the UI thread or PipelineWriterProc number. Thread numbers change when
Explorer or its tab renderer restarts. Up to three additional threads can be
captured. Intentional UI-only diagnostics require -UiOnly.
'@ | Set-Content -LiteralPath (Join-Path $package 'README.txt') -Encoding UTF8
$archive = $package+'.zip'
Compress-Archive -LiteralPath @(Get-ChildItem -LiteralPath $package -File | ForEach-Object FullName) -DestinationPath $archive -Force
Write-Output "Created $archive"
