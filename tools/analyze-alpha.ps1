# Lists the largest connected opaque regions in a texture. This identifies
# unwrapped model islands without dumping an entire bitmap into the terminal.

param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [ValidateRange(1, 100)][int]$Limit = 20
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $InputPath).Path)
try {
    $width = $bitmap.Width
    $height = $bitmap.Height
    $seen = New-Object 'bool[]' ($width * $height)
    $parts = @()

    for ($y = 0; $y -lt $height; $y++) {
        for ($x = 0; $x -lt $width; $x++) {
            $start = $y * $width + $x
            if ($seen[$start] -or $bitmap.GetPixel($x, $y).A -lt 128) { continue }

            $queue = New-Object 'System.Collections.Generic.Queue[int]'
            $queue.Enqueue($start)
            $seen[$start] = $true
            $minX = $x
            $maxX = $x
            $minY = $y
            $maxY = $y
            $count = 0

            while ($queue.Count -gt 0) {
                $index = $queue.Dequeue()
                $pixelX = $index % $width
                $pixelY = [int][Math]::Floor($index / $width)
                $count++
                $minX = [Math]::Min($minX, $pixelX)
                $maxX = [Math]::Max($maxX, $pixelX)
                $minY = [Math]::Min($minY, $pixelY)
                $maxY = [Math]::Max($maxY, $pixelY)

                for ($direction = 0; $direction -lt 4; $direction++) {
                    $nextX = $pixelX
                    $nextY = $pixelY
                    if ($direction -eq 0) { $nextX-- }
                    elseif ($direction -eq 1) { $nextX++ }
                    elseif ($direction -eq 2) { $nextY-- }
                    else { $nextY++ }

                    if ($nextX -lt 0 -or $nextX -ge $width -or $nextY -lt 0 -or $nextY -ge $height) {
                        continue
                    }
                    $next = $nextY * $width + $nextX
                    if (-not $seen[$next] -and $bitmap.GetPixel($nextX, $nextY).A -ge 128) {
                        $seen[$next] = $true
                        $queue.Enqueue($next)
                    }
                }
            }

            $parts += [pscustomobject]@{
                Pixels = $count
                Box = "$minX,$minY-$maxX,$maxY"
                Size = "$(($maxX - $minX) + 1)x$(($maxY - $minY) + 1)"
            }
        }
    }

    $parts | Sort-Object Pixels -Descending | Select-Object -First $Limit |
        Format-Table -AutoSize
} finally {
    $bitmap.Dispose()
}