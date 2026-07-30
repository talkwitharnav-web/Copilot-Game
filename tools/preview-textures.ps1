# Magnifies textures with nearest-neighbour scaling so individual pixels are
# readable, and lays them out in a single labelled sheet.
#
# Used to study reference art and to review our own output, since a 16x16 image
# is far too small to judge at actual size.
#
#   powershell -ExecutionPolicy Bypass -File tools\preview-textures.ps1 -Files a.png,b.png -Output preview.png

param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [Parameter(Mandatory = $true)][string]$Output,
    [int]$Scale = 16
)

Add-Type -AssemblyName System.Drawing

if ($Files.Count -gt 20) {
    throw "Refusing to process more than 20 images at once (got $($Files.Count))."
}

$labelHeight = 18
$padding = 8

$sources = @()
foreach ($file in $Files) {
    if (-not (Test-Path $file)) { throw "Missing file: $file" }
    $sources += [System.Drawing.Bitmap]::FromFile((Resolve-Path $file).Path)
}

# Measure-Object returns doubles, which match no Bitmap constructor.
$tileWidth = [int](($sources | ForEach-Object { $_.Width } | Measure-Object -Maximum).Maximum) * $Scale
$tileHeight = [int](($sources | ForEach-Object { $_.Height } | Measure-Object -Maximum).Maximum) * $Scale

$sheetWidth = [int](($tileWidth + $padding) * $sources.Count + $padding)
$sheetHeight = [int]($tileHeight + $labelHeight + $padding * 2)

$sheet = New-Object System.Drawing.Bitmap $sheetWidth, $sheetHeight
$graphics = [System.Drawing.Graphics]::FromImage($sheet)
$graphics.Clear([System.Drawing.Color]::FromArgb(255, 32, 32, 36))

# Nearest neighbour: anything else blurs the very pixel grid being inspected.
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half

$font = New-Object System.Drawing.Font('Consolas', 9)
$brush = [System.Drawing.Brushes]::White

for ($i = 0; $i -lt $sources.Count; $i++) {
    $x = $padding + $i * ($tileWidth + $padding)
    $rect = New-Object System.Drawing.Rectangle $x, $padding, $tileWidth, $tileHeight
    $graphics.DrawImage($sources[$i], $rect)

    $name = [System.IO.Path]::GetFileNameWithoutExtension($Files[$i])
    $graphics.DrawString($name, $font, $brush, $x, ($padding + $tileHeight + 2))
}

$graphics.Dispose()
$sheet.Save((Join-Path (Get-Location) $Output), [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
$sources | ForEach-Object { $_.Dispose() }

Write-Host "wrote $Output"
