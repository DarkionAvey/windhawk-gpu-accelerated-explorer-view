param([string]$WindhawkDirectory,[switch]$Integrated,[switch]$Visuals)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scripts/repository.ps1')
$outputDirectory=Join-Path $PSScriptRoot ('.build/gradient-dither-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDirectory | Out-Null
$compiler=Join-Path (Resolve-WindhawkDirectory $WindhawkDirectory) 'Compiler/bin/clang++.exe'
$source=Join-Path $PSScriptRoot 'gradient-dither-benchmark.cpp'
$executable=Join-Path $outputDirectory 'gradient-dither-benchmark.exe'
$arguments=@('-std=c++23','-target','x86_64-w64-mingw32','-O2','-static',
    '-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-Wall','-Wextra',
    $source,'-o',$executable,'-ld2d1','-ld3d11','-ldxgi','-ldwrite','-lwindowscodecs','-lole32','-luuid')
$previousTemp=$env:TEMP; $previousTmp=$env:TMP
try {
    $env:TEMP=$outputDirectory; $env:TMP=$outputDirectory
    & $compiler @arguments
    if($LASTEXITCODE -ne 0) {throw 'Gradient benchmark compilation failed'}
} finally { $env:TEMP=$previousTemp; $env:TMP=$previousTmp }
$cpu=(Get-ItemProperty -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0').ProcessorNameString
[ordered]@{CPU=$cpu;OS=[Environment]::OSVersion.VersionString;Compiler=$compiler;
    Arguments=$arguments;BenchmarkSHA256=(Get-SourceHash $source);
    ModSourceSHA256=(Get-SourceHash (Join-Path $PSScriptRoot '../1.0.10.cpp'));Integrated=[bool]$Integrated;
    Note='Standalone equivalent Direct2D draw workload, not the mod executable or live Explorer.'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputDirectory 'metadata.json') -Encoding UTF8
$benchmarkArguments=@()
if($Integrated) { $benchmarkArguments+='--integrated' }
if($Visuals) { $benchmarkArguments+=@('--visuals',$outputDirectory) }
& $executable @benchmarkArguments | Tee-Object -FilePath (Join-Path $outputDirectory 'results.csv')
if($LASTEXITCODE -ne 0) {throw "Gradient benchmark failed; artifacts: $outputDirectory"}
Write-Output "Artifacts: $outputDirectory"
