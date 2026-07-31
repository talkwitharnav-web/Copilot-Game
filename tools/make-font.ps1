# Generates assets/textures/font.png: a monospace ASCII atlas for HUD text.
#
# Printable ASCII (32..127) laid out in a 16x6 grid of fixed-size cells, white on
# transparent. Grid-fitted rather than antialiased, so glyphs stay crisp instead
# of turning into grey mush when the HUD scales them.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-font.ps1

Add-Type -AssemblyName System.Drawing

$cellWidth = 8
$cellHeight = 14
$columns = 16
$rows = 6
$firstChar = 32

$outputDir = Join-Path $PSScriptRoot '..\assets\textures'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$bitmap = New-Object System.Drawing.Bitmap ($cellWidth * $columns), ($cellHeight * $rows)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))

# SingleBitPerPixelGridFit gives hard-edged glyphs. Anything smoother reads as
# blurry once the HUD draws the atlas larger than 1:1.
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit

$font = New-Object System.Drawing.Font 'Consolas', 10, ([System.Drawing.FontStyle]::Regular),
    ([System.Drawing.GraphicsUnit]::Pixel)
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)

$format = New-Object System.Drawing.StringFormat
$format.FormatFlags = [System.Drawing.StringFormatFlags]::NoWrap
$format.Alignment = [System.Drawing.StringAlignment]::Center
$format.LineAlignment = [System.Drawing.StringAlignment]::Center

for ($i = 0; $i -lt ($columns * $rows); $i++) {
    $code = $firstChar + $i
    if ($code -gt 126) { break }

    $char = [char]$code
    $x = ($i % $columns) * $cellWidth
    $y = [Math]::Floor($i / $columns) * $cellHeight

    $rect = New-Object System.Drawing.RectangleF $x, $y, $cellWidth, $cellHeight
    $graphics.DrawString($char, $font, $brush, $rect, $format)
}

$graphics.Dispose()
$path = Join-Path $outputDir 'font.png'
$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

Write-Host ("wrote font.png ({0}x{1}, {2}x{3} cells)" -f ($cellWidth * $columns), ($cellHeight * $rows), $cellWidth, $cellHeight)
