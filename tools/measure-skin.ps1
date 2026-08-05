# Reports everything TEXTURING.md says to know before authoring a skin: how many
# colours the subject actually uses, where its luminance sits, how saturated it
# is, and the mean tone of any rectangles named on the command line.
#
# The colour count is the decision that matters most. Under ~20 means flat
# fields; hundreds means dense dithering. Getting it backwards is the single
# most expensive mistake recorded in TEXTURING.md.
#
#   powershell -NoProfile -File tools\measure-skin.ps1 -InputPath ref.png
#   & .\tools\measure-skin.ps1 -InputPath ref.png -Rects @("head=0,0,20,9","body=20,0,24,22")

param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [ValidateRange(1, 40)][int]$Top = 12,
    [string[]]$Rects = @()
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Get-Luminance {
    param([int]$R, [int]$G, [int]$B)
    return 0.299 * $R + 0.587 * $G + 0.114 * $B
}

$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $InputPath).Path)
try {
    $counts = @{}
    $total = 0
    $minLum = 255.0
    $maxLum = 0.0
    $sumLum = 0.0
    $sumSat = 0.0

    for ($y = 0; $y -lt $bitmap.Height; $y++) {
        for ($x = 0; $x -lt $bitmap.Width; $x++) {
            $pixel = $bitmap.GetPixel($x, $y)
            if ($pixel.A -lt 128) { continue }

            $key = "{0},{1},{2}" -f $pixel.R, $pixel.G, $pixel.B
            $counts[$key] = [int]$counts[$key] + 1
            $total++

            $lum = Get-Luminance $pixel.R $pixel.G $pixel.B
            if ($lum -lt $minLum) { $minLum = $lum }
            if ($lum -gt $maxLum) { $maxLum = $lum }
            $sumLum += $lum

            $high = [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B))
            $low = [Math]::Min($pixel.R, [Math]::Min($pixel.G, $pixel.B))
            if ($high -gt 0) { $sumSat += ($high - $low) / $high }
        }
    }

    if ($total -eq 0) { throw "No opaque pixels in $InputPath" }

    $discipline = if ($counts.Count -lt 20) { 'flat fields' }
                  elseif ($counts.Count -lt 60) { 'few tones' }
                  else { 'dense dither' }

    Write-Host ("{0}  {1}x{2}" -f (Split-Path -Leaf $InputPath), $bitmap.Width, $bitmap.Height)
    Write-Host ("  opaque {0}  colours {1}  -> {2}" -f $total, $counts.Count, $discipline)
    Write-Host ("  lum {0:N0}..{1:N0} (span {2:N0}) mean {3:N0}   sat {4:N2}" -f
                $minLum, $maxLum, ($maxLum - $minLum), ($sumLum / $total), ($sumSat / $total))

    Write-Host "  top colours:"
    $counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $Top | ForEach-Object {
        $parts = $_.Key -split ','
        $lum = Get-Luminance ([int]$parts[0]) ([int]$parts[1]) ([int]$parts[2])
        Write-Host ("    {0,7:N2}%  {1,-14} #{2:X2}{3:X2}{4:X2}  lum {5,3:N0}" -f
                    (100.0 * $_.Value / $total), $_.Key, [int]$parts[0], [int]$parts[1], [int]$parts[2], $lum)
    }

    foreach ($spec in $Rects) {
        $name, $box = $spec -split '='
        $values = $box -split ','
        $rx = [int]$values[0]; $ry = [int]$values[1]; $rw = [int]$values[2]; $rh = [int]$values[3]
        $sum = 0.0; $n = 0; $r = 0; $g = 0; $b = 0
        for ($y = $ry; $y -lt $ry + $rh; $y++) {
            for ($x = $rx; $x -lt $rx + $rw; $x++) {
                if ($x -lt 0 -or $y -lt 0 -or $x -ge $bitmap.Width -or $y -ge $bitmap.Height) { continue }
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.A -lt 128) { continue }
                $sum += Get-Luminance $pixel.R $pixel.G $pixel.B
                $r += $pixel.R; $g += $pixel.G; $b += $pixel.B
                $n++
            }
        }
        if ($n -eq 0) {
            Write-Host ("  {0,-16} EMPTY" -f $name)
        } else {
            Write-Host ("  {0,-16} rgb {1,3:N0} {2,3:N0} {3,3:N0}  lum {4,3:N0}  n={5}" -f
                        $name, ($r / $n), ($g / $n), ($b / $n), ($sum / $n), $n)
        }
    }
} finally {
    $bitmap.Dispose()
}
