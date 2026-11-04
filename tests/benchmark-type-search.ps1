param(
    [Parameter(Mandatory=$true)][string]$SourceFile,
    [string]$WindhawkDirectory,
    [int]$Rows=20000,
    [switch]$IncrementalOnly
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scripts/repository.ps1')
$root=Split-Path -Parent $PSScriptRoot
if(![IO.Path]::IsPathRooted($SourceFile)) { $SourceFile=Join-Path $root $SourceFile }
$sourcePath=(Resolve-Path -LiteralPath $SourceFile).ProviderPath
$source=[IO.File]::ReadAllText($sourcePath)
function Slice-Source([string]$Start,[string]$End) {
    $a=$source.IndexOf($Start)
    if($a -lt 0) {throw "Missing source marker: $Start"}
    $b=$source.IndexOf($End,$a)
    if($b -lt 0) {throw "Missing source marker: $End"}
    return $source.Substring($a,$b-$a)
}
$outputDirectory=Join-Path $PSScriptRoot ('.build/type-search-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDirectory | Out-Null
$header=@'
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <atomic>
#include <type_traits>
#include <algorithm>
#include <vector>
#include <bit>
'@
$header+="`n"+(Slice-Source 'namespace WindowsIcuAbi {' 'struct ItemTipRelativeOffset')
$header+="`n"+(Slice-Source 'struct TypeSearchCandidates {' 'struct ActiveFrame {')
$header+="`nnamespace TypeSearchCandidate {`n"
$header+=Slice-Source 'size_t TypeSearchCharacterLength(' '// Search the current display order'
$header+=Slice-Source 'UINT32 TypeSearchPrefixLength(const std::wstring& name,const std::wstring& prefix) {' 'bool TypeSearchActive('
$header+="}`n"
[IO.File]::WriteAllText((Join-Path $outputDirectory 'type-search-source.h'),$header,[Text.UTF8Encoding]::new($false))
$compiler=Join-Path (Resolve-WindhawkDirectory $WindhawkDirectory) 'Compiler/bin/clang++.exe'
$executable=Join-Path $outputDirectory 'type-search-benchmark.exe'
$arguments=@('-std=c++23','-target','x86_64-w64-mingw32','-O2','-static','-municode',
    '-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-Wall','-Wextra',
    '-I',$outputDirectory,(Join-Path $PSScriptRoot 'type-search-benchmark.cpp'),'-o',$executable)
$previousTemp=$env:TEMP; $previousTmp=$env:TMP
try {
    $env:TEMP=$outputDirectory; $env:TMP=$outputDirectory
    & $compiler @arguments
    if($LASTEXITCODE -ne 0) {throw 'Standalone matcher benchmark compilation failed'}
} finally { $env:TEMP=$previousTemp; $env:TMP=$previousTmp }
$report=Join-Path $outputDirectory 'results.csv'
$benchmarkArguments=@($Rows)
if($IncrementalOnly) { $benchmarkArguments+='--incremental-only' }
& $executable @benchmarkArguments | Tee-Object -FilePath $report
if($LASTEXITCODE -ne 0) {throw "Matcher benchmark/check failed; artifacts: $outputDirectory"}
Write-Output "Source: $sourcePath"
Write-Output "Source SHA256: $(Get-SourceHash $sourcePath)"
$cpu=(Get-ItemProperty -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -ErrorAction SilentlyContinue).ProcessorNameString
if(!$cpu) { $cpu=$env:PROCESSOR_IDENTIFIER }
Write-Output "CPU: $cpu"
$metadata=[ordered]@{Source=$sourcePath;SourceSHA256=(Get-SourceHash $sourcePath);
    BaselineSHA256=(Get-SourceHash (Join-Path $PSScriptRoot 'type-search-baseline.h'));
    CPU=$cpu;Rows=$Rows;Compiler=$compiler;Arguments=$arguments;OS=[Environment]::OSVersion.VersionString}
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputDirectory 'metadata.json') -Encoding UTF8
Write-Output "Artifacts: $outputDirectory"
