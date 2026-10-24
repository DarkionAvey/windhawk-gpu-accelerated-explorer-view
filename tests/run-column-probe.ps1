param([string]$SourceFile='latest.cpp', [string]$WindhawkDirectory)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\repository.ps1')
$installation=Resolve-WindhawkDirectory $WindhawkDirectory
if(![IO.Path]::IsPathRooted($SourceFile)) { $SourceFile=Join-Path (Split-Path -Parent $PSScriptRoot) $SourceFile }
$source=(Resolve-Path -LiteralPath $SourceFile).ProviderPath
$version=Get-ModVersion $source
$artifacts=Join-Path ([IO.Path]::GetTempPath()) ('gpu-column-probe-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $artifacts | Out-Null
$configuration=Join-Path $artifacts 'selected-source.h'
$text='#define GPU_OVERLAY_SOURCE "'+$source.Replace('\','/')+'"'+"`n"+'#define GPU_COLUMN_PROBE_VERSION L"'+$version+'"'+"`n"
[IO.File]::WriteAllText($configuration,$text,[Text.UTF8Encoding]::new($false))
$executable=Join-Path $artifacts 'column-state-probe.exe'
$compiler=Join-Path $installation 'Compiler\bin\clang++.exe'
& $compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-O0' '-municode' '-static' '-DUNICODE' '-D_UNICODE' '-DWINVER=0x0A00' '-D_WIN32_WINNT=0x0A00' '-D_WIN32_IE=0x0A00' '-DNTDDI_VERSION=0x0A000008' '-DWH_MOD' '-DWH_EDITING' '-include' $configuration (Join-Path $PSScriptRoot 'column-state-probe.cpp') '-o' $executable '-ldcomp' '-ld3d11' '-ldxgi' '-ld2d1' '-ldwrite' '-lshell32' '-lole32' '-loleaut32' '-luuid' '-lshlwapi' '-luser32' '-lgdi32' '-lwindowscodecs' '-ldwmapi' '-luxtheme' '-lpropsys' '-lcomctl32' '-lruntimeobject' '-lpsapi'
if($LASTEXITCODE) {throw "Column probe build failed: $LASTEXITCODE ($artifacts)"}
& $executable
if($LASTEXITCODE) {throw "No overlays with the selected source version were readable ($artifacts)"}
Remove-Item -LiteralPath $executable,$configuration
Remove-Item -LiteralPath $artifacts
