param([string]$Compiler, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\minify.ps1')
if (!('WindhawkSourceMinifier' -as [type])) {
    Add-Type -Path (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\SourceMinifier.cs')
}
# Compile actual C++ assertions in both forms. No manufactured Windows messages,
# user resources, clipboard or focus changes are involved.
$fixture = @'
#include <string_view>
#include <nested//minification-header.h>
// Standalone comment, including a fake "literal" and R"(raw)".
#define OBJECT (40 + 2)
#define ADD(x, y) ((x) + \
    (y)) // continued macro
#define TEXT(x) #x
#define MULTILINE /* a comment spanning
    physical lines stays within this directive */ 7
static_assert(OBJECT == 42 && ADD(3, 4) == 7 && MULTILINE == 7);
static_assert(std::string_view(TEXT(hello  + /* gap */ world)) == "hello + world");
static_assert(std::string_view(TEXT(( a, b ))) == "( a, b )");
static_assert(1'024 == 1024 && 0x1p+2 == 4.0);
static_assert(std::string_view("https://example.invalid/a/*b*/") == "https://example.invalid/a/*b*/");
static_assert('\'' == 39 && '\\' == 92);
constexpr auto raw = R"tag(raw // /* " '\
end)tag";
static_assert(std::string_view(raw) == "raw // /* \" '\\\nend");
constexpr auto trickyRaw = R"tag(first )tag\
" still raw
end)tag";
static_assert(std::string_view(trickyRaw) == "first )tag\\\n\" still raw\nend");
constexpr int joined = 4 /\
/ This is a comment formed by phase-two splicing.
;
constexpr int continued = 1; // continued comment \
this is still comment text, not C++;
cons\
texpr int separated = 1 +/**/+2;
#if !defined(MISSING)
static_assert(joined == 4 && continued == 1 && separated == 3);
#endif
int main() { return fixture_header_value == 23 ? 0 : 1; }
'@
$original = Join-Path $OutputDirectory 'minification-original.cpp'
$minified = Join-Path $OutputDirectory 'minification-compact.cpp'
$headerDirectory = Join-Path $OutputDirectory 'nested'
$header = Join-Path $headerDirectory 'minification-header.h'
New-Item -ItemType Directory -Path $headerDirectory -Force | Out-Null
[IO.File]::WriteAllText($header, 'constexpr int fixture_header_value = 23;')
$compact = [WindhawkSourceMinifier]::Minify($fixture)
if ($compact.Contains('Standalone comment') -or $compact.Contains('phase-two splicing')) {
    throw 'Minification retained ordinary comments'
}
if ($compact -cne [WindhawkSourceMinifier]::Minify($compact)) { throw 'Minification is not idempotent' }
foreach ($invalid in @('/* unterminated', '"unterminated', 'R"tag(unterminated')) {
    $rejected = $false
    try { [void][WindhawkSourceMinifier]::Minify($invalid) } catch { $rejected = $true }
    if (!$rejected) { throw 'Minifier accepted an unterminated token' }
}
[IO.File]::WriteAllText($original, $fixture, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($minified, $compact, [Text.UTF8Encoding]::new($false))
foreach ($path in @($original, $minified)) {
    $executable = [IO.Path]::ChangeExtension($path, '.exe')
    & $Compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-static' '-I' $OutputDirectory $path '-o' $executable
    if ($LASTEXITCODE -ne 0) { throw "Minification fixture compilation failed: $LASTEXITCODE" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "Minification fixture failed: $LASTEXITCODE" }
    Remove-Item -LiteralPath $executable -Force
}
Remove-Item -LiteralPath $original, $minified, $header
Remove-Item -LiteralPath $headerDirectory
Write-Output 'PASS: minification preserves macros, directives, token boundaries, digit separators, includes, escaped newlines and ordinary/raw literals'
