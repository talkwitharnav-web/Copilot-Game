# Generates assets/textures/font.png: our own ASCII atlas for HUD text.
#
# **The layout is the reference's, deliberately.** 128x128, a 16x16 grid of 8x8
# cells, where the cell index *is* the codepoint - so `font-reference.png` is a
# drop-in replacement for this file and one set of constants in
# `HudPrimitives.cpp` serves both. Getting that wrong does not fail; it draws
# the wrong letters.
#
# Glyph advances are **not** written down anywhere. They are measured from
# whichever atlas actually loaded, at startup, by finding each cell's rightmost
# opaque column - which is how the reference does it, and the only way a
# variable-width font can have a single owner.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-font.ps1

Add-Type -AssemblyName System.Drawing

$cell = 8
$columns = 16
$rows = 16

$outputDir = Join-Path $PSScriptRoot '..\assets\textures'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$bitmap = New-Object System.Drawing.Bitmap ($cell * $columns), ($cell * $rows)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))

# SingleBitPerPixelGridFit gives hard-edged glyphs. Anything smoother reads as
# blurry once the HUD draws the atlas larger than 1:1.
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit

$font = New-Object System.Drawing.Font 'Consolas', 8, ([System.Drawing.FontStyle]::Regular),
    ([System.Drawing.GraphicsUnit]::Pixel)
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)

$format = New-Object System.Drawing.StringFormat
$format.FormatFlags = [System.Drawing.StringFormatFlags]::NoWrap
$format.Alignment = [System.Drawing.StringAlignment]::Near
$format.LineAlignment = [System.Drawing.StringAlignment]::Near

# Printable ASCII only. Everything else is a blank cell, which draws nothing and
# advances by the space width - the same thing the reference does with the cells
# it leaves empty.
for ($code = 32; $code -le 126; $code++) {
    $x = ($code % $columns) * $cell
    $y = [Math]::Floor($code / $columns) * $cell

    $rect = New-Object System.Drawing.RectangleF $x, $y, $cell, $cell
    $graphics.DrawString([string][char]$code, $font, $brush, $rect, $format)
}

$graphics.Dispose()
$path = Join-Path $outputDir 'font.png'
$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

Write-Host ("wrote font.png ({0}x{1}, {2}x{2} cells, cell index = codepoint)" -f
    ($cell * $columns), ($cell * $rows), $cell)
