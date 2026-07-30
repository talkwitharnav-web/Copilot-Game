# Generates the block textures under assets/textures/blocks/.
#
# Written against real block-texture reference. The rules that matter:
#
#   - NO interpolation, blending or gradients anywhere. Every pixel is one
#     palette entry chosen outright. Smooth noise, even when quantized, reads as
#     melted blobs rather than as pixel art.
#   - Tight value ranges. Stone in particular spans only a few near greys; wide
#     contrast makes terrain look like static.
#   - Weighted palette selection, so most pixels sit on the middle tones and the
#     extremes are sparse. That is what produces a mostly-even surface with
#     occasional speckles.
#   - Short horizontal runs, from occasionally repeating the pixel to the left.
#     Reference textures show runs of two or three, never large patches.
#   - Accent pixels, such as the grey pebbles scattered through soil, which are
#     what stop a brown texture reading as a brown blanket.
#
# Run from the repository root:
#   powershell -ExecutionPolicy Bypass -File tools\make-block-textures.ps1

Add-Type -AssemblyName System.Drawing

$size = 16
$outputDir = Join-Path $PSScriptRoot '..\assets\textures\blocks'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

function Get-Hash01 {
    param([int]$x, [int]$y, [int]$salt)
    $n = ($x * 73856093) -bxor ($y * 19349663) -bxor ($salt * 83492791)
    $n = [Math]::Abs($n % 2147483647)
    $v = [Math]::Sin($n * 12.9898) * 43758.5453
    return $v - [Math]::Floor($v)
}

function ConvertTo-Color {
    param([string]$Hex)
    return [System.Drawing.Color]::FromArgb(255,
        [Convert]::ToInt32($Hex.Substring(0, 2), 16),
        [Convert]::ToInt32($Hex.Substring(2, 2), 16),
        [Convert]::ToInt32($Hex.Substring(4, 2), 16))
}

# Picks a palette index from a weighted distribution. Weights are relative.
function Get-WeightedIndex {
    param([double]$Roll, [int[]]$Weights)
    $total = 0
    foreach ($w in $Weights) { $total += $w }
    $target = $Roll * $total
    $running = 0
    for ($i = 0; $i -lt $Weights.Count; $i++) {
        $running += $Weights[$i]
        if ($target -lt $running) { return $i }
    }
    return $Weights.Count - 1
}

# Fills a grid of palette indices: weighted per-pixel choice, with a chance of
# repeating the pixel to the left so short runs appear.
#
# Flat array with manual indexing on purpose. PowerShell's multidimensional
# element assignment ($a[$x, $y] = v) does not write where it appears to, which
# silently produced textures striped down each column.
function New-IndexGrid {
    param([int[]]$Weights, [int]$Salt, [double]$RunChance = 0.30)

    $grid = New-Object 'int[]' ($size * $size)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $i = $y * $size + $x
            if ($x -gt 0 -and (Get-Hash01 -x $x -y $y -salt ($Salt + 555)) -lt $RunChance) {
                $grid[$i] = $grid[$i - 1]
            } else {
                $roll = Get-Hash01 -x $x -y $y -salt $Salt
                $grid[$i] = Get-WeightedIndex -Roll $roll -Weights $Weights
            }
        }
    }
    return $grid
}

function Save-Bitmap {
    param([System.Drawing.Bitmap]$Bitmap, [string]$Name)
    $path = Join-Path $outputDir "$Name.png"
    $Bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $Bitmap.Dispose()
    Write-Host "wrote $Name.png"
}

# --- Palettes -------------------------------------------------------------
# Dark to light. Deliberately narrow: the reference spans far less range than
# instinct suggests.

$stonePalette = @('6E6E6E', '757575', '7C7C7C', '848484', '8C8C8C')
$stoneWeights = @(2, 4, 6, 4, 2)

$dirtPalette  = @('61452F', '6E4E36', '7A573C', '875F43', '936A4C')
$dirtWeights  = @(3, 5, 6, 4, 2)

$grassPalette = @('4F7F31', '5A8C38', '65993F', '70A646', '7BB34D')
$grassWeights = @(2, 4, 6, 4, 2)

$sandPalette  = @('CFC59A', 'D6CCA3', 'DCD2AB', 'E2D8B3', 'E8DEBB')
$sandWeights  = @(2, 4, 6, 4, 2)

# Pebbles in soil, and the darkest crumbs. Sparse by design.
$pebbleColor = ConvertTo-Color '82817C'
$crumbColor  = ConvertTo-Color '4E3625'

function New-FlatTexture {
    param([string]$Name, [string[]]$Palette, [int[]]$Weights, [int]$Salt)

    $grid = New-IndexGrid -Weights $Weights -Salt $Salt
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $bitmap.SetPixel($x, $y, (ConvertTo-Color $Palette[$grid[$y * $size + $x]]))
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name $Name
}

# Soil: the brown ramp plus sparse grey pebbles and dark crumbs.
function Set-DirtPixel {
    param([System.Drawing.Bitmap]$Bitmap, [int]$x, [int]$y, $Grid, [int]$AccentSalt)
    $accent = Get-Hash01 -x $x -y $y -salt $AccentSalt
    if ($accent -gt 0.955) {
        $Bitmap.SetPixel($x, $y, $pebbleColor)
    } elseif ($accent -lt 0.06) {
        $Bitmap.SetPixel($x, $y, $crumbColor)
    } else {
        $Bitmap.SetPixel($x, $y, (ConvertTo-Color $dirtPalette[$Grid[$y * $size + $x]]))
    }
}

function New-DirtTexture {
    $grid = New-IndexGrid -Weights $dirtWeights -Salt 23
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            Set-DirtPixel -Bitmap $bitmap -x $x -y $y -Grid $grid -AccentSalt 717
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'dirt'
}

# Stone: the grey ramp plus a few darker flecks so it reads as rock.
function New-StoneTexture {
    $grid = New-IndexGrid -Weights $stoneWeights -Salt 11
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            if ((Get-Hash01 -x $x -y $y -salt 909) -gt 0.96) {
                $bitmap.SetPixel($x, $y, (ConvertTo-Color '626262'))
            } else {
                $bitmap.SetPixel($x, $y, (ConvertTo-Color $stonePalette[$grid[$y * $size + $x]]))
            }
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'stone'
}

# The side face is soil with grass hanging over the top edge. Blade depth varies
# per column and some columns dangle a further pixel or two, which is what makes
# the boundary read as growth rather than as a painted stripe.
function New-GrassSideTexture {
    $dirtGrid = New-IndexGrid -Weights $dirtWeights -Salt 23
    $grassGrid = New-IndexGrid -Weights $grassWeights -Salt 31
    $bitmap = New-Object System.Drawing.Bitmap $size, $size

    $depths = @()
    for ($x = 0; $x -lt $size; $x++) {
        $d = 3
        if ((Get-Hash01 -x $x -y 0 -salt 61) -gt 0.5) { $d = 4 }
        if ((Get-Hash01 -x $x -y 1 -salt 62) -gt 0.80) { $d += 1 }
        if ((Get-Hash01 -x $x -y 2 -salt 63) -gt 0.93) { $d += 1 }
        $depths += $d
    }

    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            if ($y -lt $depths[$x]) {
                $bitmap.SetPixel($x, $y, (ConvertTo-Color $grassPalette[$grassGrid[$y * $size + $x]]))
            } else {
                Set-DirtPixel -Bitmap $bitmap -x $x -y $y -Grid $dirtGrid -AccentSalt 717
            }
        }
    }

    Save-Bitmap -Bitmap $bitmap -Name 'grass_side'
}

New-StoneTexture
New-DirtTexture
New-FlatTexture -Name 'grass_top' -Palette $grassPalette -Weights $grassWeights -Salt 31
New-GrassSideTexture
New-FlatTexture -Name 'sand' -Palette $sandPalette -Weights $sandWeights -Salt 53

# Flat white, for geometry that supplies its own colour: the targeting cage, the
# crosshair, and anything else that must not pick up a material.
$white = New-Object System.Drawing.Bitmap $size, $size
for ($y = 0; $y -lt $size; $y++) {
    for ($x = 0; $x -lt $size; $x++) {
        $white.SetPixel($x, $y, [System.Drawing.Color]::White)
    }
}
Save-Bitmap -Bitmap $white -Name 'white'
