param([string]$WindhawkDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/repository.ps1')
$toolchain = Resolve-WindhawkDirectory $WindhawkDirectory
$output = Join-Path $PSScriptRoot '.build'
[void][IO.Directory]::CreateDirectory($output)
$binary = Join-Path $output 'rename-native-probe.exe'
& (Join-Path $toolchain 'Compiler/bin/clang++.exe') '-std=c++23' '-target' 'x86_64-w64-mingw32' `
    '-O2' '-municode' '-static' '-DUNICODE' '-D_UNICODE' (Join-Path $PSScriptRoot 'rename-native-probe.cpp') `
    '-o' $binary '-luser32' '-lgdi32' '-lcomctl32'
if ($LASTEXITCODE -ne 0) { throw "Native EDIT probe compilation failed: $LASTEXITCODE" }
& $binary (Join-Path $PSScriptRoot 'rename-native-controls.manifest')
if ($LASTEXITCODE -ne 0) { throw "Native EDIT probe failed: $LASTEXITCODE" }
