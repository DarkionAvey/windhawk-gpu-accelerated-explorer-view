param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) ('gpu-tooltip-live-' + [guid]::NewGuid().ToString('N'))
}
$directory = New-Item -ItemType Directory -Path $OutputDirectory -ErrorAction Stop
# A real uncompressed BMP: Windows' property handler supplies dimensions,
# bit depth and resolution. This does not load or compile the mod or inject input.
$width = 160
$height = 96
$stride = $width * 3
$imageBytes = $stride * $height
$stream = [IO.File]::Create((Join-Path $directory.FullName 'hover-image.bmp'))
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write([byte[]](0x42, 0x4D))
    $writer.Write([int](54 + $imageBytes))
    $writer.Write([int]0)
    $writer.Write([int]54)
    $writer.Write([int]40)
    $writer.Write([int]$width)
    $writer.Write([int]$height)
    $writer.Write([int16]1)
    $writer.Write([int16]24)
    $writer.Write([int]0)
    $writer.Write([int]$imageBytes)
    $writer.Write([int]3780)
    $writer.Write([int]3780)
    $writer.Write([int]0)
    $writer.Write([int]0)
    for ($y = 0; $y -lt $height; $y++) {
        for ($x = 0; $x -lt $width; $x++) {
            $writer.Write([byte]64)
            $writer.Write([byte]([int]($y * 255 / ($height - 1))))
            $writer.Write([byte]([int]($x * 255 / ($width - 1))))
        }
    }
} finally { $writer.Dispose() }
[IO.File]::WriteAllText((Join-Path $directory.FullName 'hover-note.txt'), 'Temporary tooltip hover fixture.')
Write-Output $directory.FullName
