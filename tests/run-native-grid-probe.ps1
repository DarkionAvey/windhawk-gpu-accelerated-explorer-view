param([string]$WindhawkDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/repository.ps1')
$compiler = Join-Path (Resolve-WindhawkDirectory $WindhawkDirectory) 'Compiler/bin/clang++.exe'
$output = Join-Path $PSScriptRoot '.build/native-grid'
[void][IO.Directory]::CreateDirectory($output)
$exe = Join-Path $output 'probe.exe'
& $compiler '-std=c++23' '-D_WIN32_WINNT=0x0A00' '-DWINVER=0x0A00' '-target' 'x86_64-w64-mingw32' '-municode' '-static' '-DUNICODE' '-D_UNICODE' (Join-Path $PSScriptRoot 'native-grid-probe.cpp') '-o' $exe '-lshell32' '-lole32' '-luuid' '-luser32' '-lgdi32'
if ($LASTEXITCODE) { throw 'Compilation failed' }
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('ExplorerGrid-' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($fixture)
try {
    Add-Type -AssemblyName System.Drawing
    foreach ($i in 1..4) {
        [void][IO.Directory]::CreateDirectory((Join-Path $fixture "HgFolder$i"))
        [IO.File]::WriteAllText((Join-Path $fixture "HgText$i.txt"),'Grid probe')
        $bitmap = New-Object Drawing.Bitmap 512,512
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $graphics.Clear([Drawing.Color]::Magenta)
        $bitmap.Save((Join-Path $fixture "HgImage$i.png"),[Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    & $exe $fixture $output
    if ($LASTEXITCODE) { throw 'Native grid probe failed' }
    Get-Content (Join-Path $output 'metrics.txt')
    & (Join-Path $PSScriptRoot 'measure-native-grid-pixels.ps1') $output
    foreach ($preset in 'small','medium','large','extralarge') {
        $capture = [Drawing.Bitmap]::FromFile((Join-Path $output "$preset.bmp"))
        try { $capture.Save((Join-Path $output "$preset.png"),[Drawing.Imaging.ImageFormat]::Png) }
        finally { $capture.Dispose() }
    }
    Write-Output $output
} finally {
    if ((Split-Path $fixture -Parent) -eq [IO.Path]::GetTempPath().TrimEnd('\') -and (Split-Path $fixture -Leaf) -match '^ExplorerGrid-[a-f0-9-]{36}$') {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}

