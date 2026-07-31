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

# Loose rubble carries far more contrast than bedrock-smooth stone.
$cobblePalette = @('4F4F53', '646468', '78787D', '8E8E93', 'A6A6AB')
$cobbleWeights = @(3, 5, 5, 4, 3)

# Gravel mixes warm and cool greys rather than staying neutral.
$gravelPalette = @('575250', '6A625D', '7C736C', '8D847C', '9F958C')
$gravelWeights = @(3, 5, 5, 4, 3)

# Snow is almost flat with a faint blue cast; real contrast reads as dirty snow.
$snowPalette = @('DFE7EF', 'E9EFF5', 'F2F6FA', 'F9FBFD', 'FFFFFF')
$snowWeights = @(1, 2, 5, 7, 5)

$planksPalette = @('9A7C4B', 'A78855', 'B4945F', 'BE9F6A', 'C9AB76')
$planksWeights = @(3, 5, 6, 4, 2)
$plankSeamColor = '6A5330'

$brickPalette = @('6B3729', '7D4234', '8E4D3D', '9F5847', 'B0664F', 'C0765E')
$brickWeights = @(3, 5, 6, 5, 4, 2)

# Mortar is not a flat fill: it carries as much grain as the brick faces, which
# is most of what stops the texture reading as vector art.
$mortarPalette = @('978D86', 'A39992', 'AEA49C', 'B9AEA6', 'C4B8AF')
$mortarWeights = @(2, 4, 6, 4, 2)

$sandPalette  = @('CFC59A', 'D6CCA3', 'DCD2AB', 'E2D8B3', 'E8DEBB')
$sandWeights  = @(2, 4, 6, 4, 2)

# Warm and blotchy, weighted toward the brighter end so it reads as a light
# source even before anything is actually lit by it.
$glowPalette = @('8A6A2E', 'A88338', 'C39D45', 'D9B455', 'ECCB6B')
$glowWeights = @(2, 3, 5, 6, 5)
$glowCoreColor = 'FFF3B8'

# Narrow and deliberately low-contrast: water carries its look from being
# see-through and from what is under it, not from its own texture.
$waterPalette = @('2E6FA8', '3479B4', '3A83C0', '408DCB', '4796D6')
$waterWeights = @(2, 4, 6, 4, 2)

$barkPalette = @('3B2A19', '503B25', '634B2E', '775C39', '8F7145', '9E7E50')
$barkWeights = @(7, 14, 15, 42, 16, 5)

$logCorePalette = @('8A6C42', '9A7A4C', 'A78754', 'AF8E5B', 'B89862')
$logCoreWeights = @(2, 4, 6, 4, 2)

# Darker and more varied than grass: foliage reads as depth rather than as a
# flat surface, and the spread of tones is what suggests that.
$leafPalette = @('2F5220', '386127', '41702E', '4A7F35', '558E3E', '629C48')
$leafWeights = @(4, 5, 6, 5, 3, 2)
$leafGapColor = '1B3714'

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

# Rubble reads as lumps rather than grain, so the palette index is chosen per
# clump of pixels with only slight per-pixel jitter on top.
function New-ClumpedTexture {
    param([string]$Name, [string[]]$Palette, [int[]]$Weights, [int]$Salt, [int]$Clump)

    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $cx = [Math]::Floor($x / $Clump)
            $cy = [Math]::Floor($y / $Clump)
            $roll = Get-Hash01 -x $cx -y $cy -salt $Salt
            $jitter = (Get-Hash01 -x $x -y $y -salt ($Salt + 77)) * 0.22 - 0.11
            $roll = [Math]::Max(0.0, [Math]::Min(0.999, $roll + $jitter))
            $index = Get-WeightedIndex -Roll $roll -Weights $Weights
            $bitmap.SetPixel($x, $y, (ConvertTo-Color $Palette[$index]))
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name $Name
}

# Four boards, each with a hard dark seam beneath it and horizontal grain within.
function New-PlanksTexture {
    $boardHeight = 4
    $bitmap = New-Object System.Drawing.Bitmap $size, $size

    for ($y = 0; $y -lt $size; $y++) {
        $board = [Math]::Floor($y / $boardHeight)
        $rowInBoard = $y % $boardHeight

        $previous = 0
        for ($x = 0; $x -lt $size; $x++) {
            if ($rowInBoard -eq ($boardHeight - 1)) {
                $bitmap.SetPixel($x, $y, (ConvertTo-Color $plankSeamColor))
                continue
            }

            # Grain runs along the board, so a pixel usually repeats the one to
            # its left. Salting by board keeps each plank distinct.
            if ($x -gt 0 -and (Get-Hash01 -x $x -y $y -salt (600 + $board)) -lt 0.55) {
                $index = $previous
            } else {
                $roll = Get-Hash01 -x $x -y ($board * 4 + $rowInBoard) -salt 91
                $index = Get-WeightedIndex -Roll $roll -Weights $planksWeights
            }
            $previous = $index

            $color = ConvertTo-Color $planksPalette[$index]
            if ((Get-Hash01 -x $x -y $y -salt 313) -gt 0.965) {
                $color = ConvertTo-Color $plankSeamColor
            }
            $bitmap.SetPixel($x, $y, $color)
        }
    }

    Save-Bitmap -Bitmap $bitmap -Name 'planks'
}

# Running bond: every other row is offset by half a brick.
function New-BricksTexture {
    $rowHeight = 4
    $brickWidth = 8
    $bitmap = New-Object System.Drawing.Bitmap $size, $size

    for ($y = 0; $y -lt $size; $y++) {
        $row = [Math]::Floor($y / $rowHeight)
        $offset = if ($row % 2 -eq 0) { 0 } else { $brickWidth / 2 }

        for ($x = 0; $x -lt $size; $x++) {
            # Mortar occupies the last row of each course and the last column of
            # each brick.
            $shifted = ($x + $offset) % $size
            $isMortar = ($y % $rowHeight) -eq ($rowHeight - 1) -or ($shifted % $brickWidth) -eq ($brickWidth - 1)

            if ($isMortar) {
                $roll = Get-Hash01 -x $x -y $y -salt 157
                $index = Get-WeightedIndex -Roll $roll -Weights $mortarWeights
                $bitmap.SetPixel($x, $y, (ConvertTo-Color $mortarPalette[$index]))
                continue
            }

            # A base shade per brick, then strong per-pixel variation on top:
            # without the latter each brick reads as a flat rectangle.
            $brickX = [Math]::Floor($shifted / $brickWidth)
            $roll = Get-Hash01 -x $brickX -y $row -salt 131
            $jitter = (Get-Hash01 -x $x -y $y -salt 202) * 0.62 - 0.31
            $roll = [Math]::Max(0.0, [Math]::Min(0.999, $roll * 0.55 + 0.22 + $jitter))
            $index = Get-WeightedIndex -Roll $roll -Weights $brickWeights
            $bitmap.SetPixel($x, $y, (ConvertTo-Color $brickPalette[$index]))
        }
    }

    Save-Bitmap -Bitmap $bitmap -Name 'bricks'
}

# Blotchy warm mineral with a scattering of bright cores, so it reads as glowing
# rock rather than as a flat yellow tile.
function New-GlowstoneTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $cx = [Math]::Floor($x / 2)
            $cy = [Math]::Floor($y / 2)
            $roll = Get-Hash01 -x $cx -y $cy -salt 211
            $jitter = (Get-Hash01 -x $x -y $y -salt 233) * 0.3 - 0.15
            $roll = [Math]::Max(0.0, [Math]::Min(0.999, $roll + $jitter))
            $index = Get-WeightedIndex -Roll $roll -Weights $glowWeights
            $color = ConvertTo-Color $glowPalette[$index]

            # Sparse, so the cores stay individually visible at 16 pixels.
            if ((Get-Hash01 -x $x -y $y -salt 251) -gt 0.90) {
                $color = ConvertTo-Color $glowCoreColor
            }
            $bitmap.SetPixel($x, $y, $color)
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'glowstone'
}

# A soft-edged disc rather than a hard square: the sun is the one thing in the
# sky, so a visible staircase edge at 16 pixels would be the first thing anyone
# notices. Alpha does the shaping; the pipeline already blends.
function New-SunTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $centre = ($size - 1) / 2.0
    $radius = $size * 0.44

    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $dx = $x - $centre
            $dy = $y - $centre
            $distance = [Math]::Sqrt($dx * $dx + $dy * $dy)

            # Opaque core, one pixel of falloff, then nothing.
            $alpha = 1.0 - [Math]::Max(0.0, [Math]::Min(1.0, ($distance - ($radius - 1.2)) / 1.6))
            if ($alpha -le 0.0) {
                $bitmap.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0, 255, 255, 240))
                continue
            }

            # Faintly warmer toward the rim, which reads as glow rather than as a
            # flat sticker.
            $warmth = [Math]::Min(1.0, $distance / $radius)
            $r = 255
            $g = [int](255 - 14 * $warmth)
            $b = [int](236 - 60 * $warmth)
            $bitmap.SetPixel($x, $y, [System.Drawing.Color]::FromArgb([int](255 * $alpha), $r, $g, $b))
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'sun'
}

# Bark is a dense field of short vertical dashes, not stripes. Measured from
# reference art: vertical runs are overwhelmingly 1-3px, horizontally almost no
# two neighbouring pixels match, and one mid tone carries ~40% of the tile.
# Painting whole columns one tone reads as planks instead.
function New-BarkTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size

    for ($x = 0; $x -lt $size; $x++) {
        # Roughly half the columns lean dark. Drawn freely rather than alternated
        # by parity: a strict checker makes the 16-pixel repeat obvious once the
        # texture tiles up a trunk, and reference bark runs two light or two dark
        # columns together often enough to break that up.
        $dark = (Get-Hash01 -x $x -y 0 -salt 401) -lt 0.5

        # Each column leans on two adjacent tones, but both windows reach far
        # enough down to scatter dark flecks - the reference has them in light
        # columns too, and they are most of what makes bark look fine-grained.
        # The tile is an even number of columns wide, so the alternation meets
        # itself cleanly where the texture wraps.
        $base = if ($dark) { 0 } else { 1 }
        $windowWeights = if ($dark) { @(2, 7, 6, 3) } else { @(1, 3, 8, 6) }

        $y = 0
        $previous = -1
        while ($y -lt $size) {
            $lengthRoll = Get-Hash01 -x $x -y $y -salt 409
            $runLength = 1
            if ($lengthRoll -gt 0.34) { $runLength = 2 }
            if ($lengthRoll -gt 0.66) { $runLength = 3 }
            if ($lengthRoll -gt 0.85) { $runLength = 4 }
            if ($lengthRoll -gt 0.94) { $runLength = 5 }
            if ($lengthRoll -gt 0.98) { $runLength = 6 }

            $offset = Get-WeightedIndex -Roll (Get-Hash01 -x $x -y ($y + 32) -salt 419) -Weights $windowWeights
            $index = $base + $offset

            # Consecutive runs landing on the same tone merge into a chunky slab.
            # The step is drawn rather than fixed, because always nudging one way
            # drags every column toward that end of the palette.
            if ($index -eq $previous) {
                if ((Get-Hash01 -x $x -y ($y + 96) -salt 439) -lt 0.5) { $index++ } else { $index-- }
                if ($index -lt $base) { $index = $base + 1 }
                if ($index -gt $base + 3) { $index = $base + 2 }
            }
            $index = [Math]::Max(0, [Math]::Min($barkPalette.Count - 1, $index))
            $previous = $index
            $color = ConvertTo-Color $barkPalette[$index]

            for ($i = 0; $i -lt $runLength -and $y -lt $size; $i++) {
                $bitmap.SetPixel($x, $y, $color)
                $y++
            }
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'log_side'
}

# End grain: concentric *square* rings inside a bark border. Circular rings look
# wrong at 16 pixels and are not what the reference does.
function New-LogTopTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $centre = ($size - 1) / 2.0

    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            # Chebyshev distance is what makes the rings square.
            $ring = [Math]::Max([Math]::Abs($x - $centre), [Math]::Abs($y - $centre))

            # A single pixel of bark. Two reads as a thick frame and swallows
            # the end grain it is meant to surround.
            if ($ring -gt 7.0) {
                $roll = Get-Hash01 -x $x -y $y -salt 433
                $index = Get-WeightedIndex -Roll $roll -Weights $barkWeights
                $bitmap.SetPixel($x, $y, (ConvertTo-Color $barkPalette[$index]))
                continue
            }

            # Rings are thin dark lines on a light field, not thick bands. The
            # ring index is exact: distances land on half-integers.
            $r = [int][Math]::Round($ring - 0.5)
            $index = if ($r % 2 -eq 0) { 1 } else { 3 }
            if ($r -eq 0) { $index = 0 }

            if ((Get-Hash01 -x $x -y $y -salt 449) -gt 0.82) {
                $index = [Math]::Max(0, [Math]::Min($logCorePalette.Count - 1, $index + 1))
            }
            $bitmap.SetPixel($x, $y, (ConvertTo-Color $logCorePalette[$index]))
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'log_top'
}

# Foliage is clumped and high-contrast, with dark gaps standing in for the holes
# the reference gets from transparency. Alpha-tested leaves arrive at M17.
function New-LeavesTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size

    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $cx = [Math]::Floor($x / 2)
            $cy = [Math]::Floor($y / 2)
            $roll = Get-Hash01 -x $cx -y $cy -salt 151
            $jitter = (Get-Hash01 -x $x -y $y -salt 163) * 0.45 - 0.225
            $roll = [Math]::Max(0.0, [Math]::Min(0.999, $roll + $jitter))
            $index = Get-WeightedIndex -Roll $roll -Weights $leafWeights
            $color = ConvertTo-Color $leafPalette[$index]

            # Gaps you would see sky through, until transparency exists.
            if ((Get-Hash01 -x $x -y $y -salt 179) -gt 0.86) {
                $color = ConvertTo-Color $leafGapColor
            }
            $bitmap.SetPixel($x, $y, $color)
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'leaves'
}

New-StoneTexture
New-DirtTexture
New-FlatTexture -Name 'grass_top' -Palette $grassPalette -Weights $grassWeights -Salt 31
New-GrassSideTexture
New-FlatTexture -Name 'sand' -Palette $sandPalette -Weights $sandWeights -Salt 53
New-ClumpedTexture -Name 'cobblestone' -Palette $cobblePalette -Weights $cobbleWeights -Salt 71 -Clump 2
New-ClumpedTexture -Name 'gravel' -Palette $gravelPalette -Weights $gravelWeights -Salt 83 -Clump 1
New-FlatTexture -Name 'snow' -Palette $snowPalette -Weights $snowWeights -Salt 97
New-PlanksTexture
New-BricksTexture
New-GlowstoneTexture
New-SunTexture
New-FlatTexture -Name 'water' -Palette $waterPalette -Weights $waterWeights -Salt 137
New-BarkTexture
New-LogTopTexture
New-LeavesTexture

# Flat white, for geometry that supplies its own colour: the targeting cage, the
# crosshair, and anything else that must not pick up a material.
$white = New-Object System.Drawing.Bitmap $size, $size
for ($y = 0; $y -lt $size; $y++) {
    for ($x = 0; $x -lt $size; $x++) {
        $white.SetPixel($x, $y, [System.Drawing.Color]::White)
    }
}
Save-Bitmap -Bitmap $white -Name 'white'
