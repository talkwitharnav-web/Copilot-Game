# Magnifies one pixel-art texture and overlays coordinate lines. Used when a
# model sheet changed layout and box-net boundaries need to be read visually.

param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [ValidateRange(2, 32)][int]$Scale = 8,
    [ValidateRange(1, 32)][int]$Grid = 4
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$source = [System.Drawing.Bitmap]::FromFile((Resolve-Path $InputPath).Path)
try {
    $margin = 34
    $width = $margin + $source.Width * $Scale + 1
    $height = $margin + $source.Height * $Scale + 1
    $sheet = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(255, 28, 28, 32))
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $destination = New-Object System.Drawing.Rectangle $margin, $margin, ($source.Width * $Scale), ($source.Height * $Scale)
        $graphics.DrawImage($source, $destination)

        $gridPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(110, 40, 220, 220)), 1
        $axisPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(190, 255, 210, 60)), 1
        $font = New-Object System.Drawing.Font 'Consolas', 7
        $brush = [System.Drawing.Brushes]::White
        try {
            for ($x = 0; $x -le $source.Width; $x += $Grid) {
                $screenX = $margin + $x * $Scale
                $pen = if ($x % 16 -eq 0) { $axisPen } else { $gridPen }
                $graphics.DrawLine($pen, $screenX, $margin, $screenX, ($height - 1))
                if ($x -lt $source.Width) { $graphics.DrawString($x.ToString(), $font, $brush, $screenX, 12) }
            }
            for ($y = 0; $y -le $source.Height; $y += $Grid) {
                $screenY = $margin + $y * $Scale
                $pen = if ($y % 16 -eq 0) { $axisPen } else { $gridPen }
                $graphics.DrawLine($pen, $margin, $screenY, ($width - 1), $screenY)
                if ($y -lt $source.Height) { $graphics.DrawString($y.ToString(), $font, $brush, 2, $screenY) }
            }
        } finally {
            $gridPen.Dispose()
            $axisPen.Dispose()
            $font.Dispose()
        }
    } finally {
        $graphics.Dispose()
    }

    $sheet.Save((Join-Path (Get-Location) $OutputPath), [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    Write-Host "wrote $OutputPath ($width x $height)"
} finally {
    $source.Dispose()
}