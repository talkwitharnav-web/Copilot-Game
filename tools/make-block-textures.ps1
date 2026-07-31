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
#
# Every ramp below is tuned against measured reference statistics - luminance
# range, mean, standard deviation and run length - rather than against how it
# looks in isolation. Run `tools\compare-texture.ps1 <ours> <reference>` after
# any change here; a number moving is the only reliable signal.

# 4 tones, not 6: reference stone is more banded than instinct suggests, and
# its horizontal runs reach 9 pixels where ours reached 4.
$stonePalette = @('686868', '747474', '808080', '8C8C8C')
$stoneWeights = @(3, 6, 6, 3)

# Lighter and higher contrast than before: the reference sits at mean 102 with
# a spread of 23, ours was 92 and 16, which read as flat mud.
$dirtPalette  = @('5E4430', '6C4E38', '7A5840', '886248', '966C50', 'A47758', 'B08260')
$dirtWeights  = @(3, 5, 7, 7, 5, 3, 2)

# Green is baked in, not applied at draw time.
#
# A greyscale texture multiplied by a per-biome colour was tried and reverted:
# it washed every plant out to a pale sage, because a mid-grey times a mid-green
# is a much duller colour than either. Matching a reference statistic is not the
# goal; the goal is that it looks right.
$grassPalette = @('44722B', '4A7A2F', '508233', '558A37', '5A923B', '5F9A3F', '64A143', '69A947',
                  '6EB14B', '73B94F', '78C153', '7DC957')
$grassWeights = @(1, 2, 4, 6, 8, 10, 10, 8, 6, 4, 2, 1)

$grassTopPalette = $grassPalette
$grassTopWeights = $grassWeights


# Loose rubble carries far more contrast than bedrock-smooth stone.
# Near neutral, and wider than before. Reference cobblestone measures 0.4%
# saturation against our 3.7%, and spans 82-181 against our 79-166.
$cobblePalette = @('525252', '686868', '7E7E7E', '949494', 'AAAAAA', 'B5B5B5')
$cobbleWeights = @(3, 5, 6, 5, 4, 2)

# Reference gravel is almost colourless - 4.3% saturation, where ours reached
# 11.8% and read as brown rubble rather than stone chips.
$gravelPalette = @('5D5D5B', '696966', '747471', '80807C', '8B8B87', '979792', 'A2A29D', 'AEAEA8')
$gravelWeights = @(2, 4, 5, 6, 6, 5, 4, 2)

# Three tones spanning five luminance levels. Reference snow is very nearly
# pure white; ours spanned 25 levels and read as dirty.
$snowPalette = @('FAFAFA', 'FCFCFC', 'FFFFFF')
$snowWeights = @(3, 5, 6)

# Reference planks top out at 161 and their longest identical run is 8 pixels -
# half the tile. Ours reached 174 and ran the full 16, which is what made whole
# rows read as painted bands.
$planksPalette = @('9A7C4B', 'A58653', 'B0905B', 'BA9A63', 'C3A46B')
$planksWeights = @(3, 5, 6, 5, 3)
$plankSeamColor = '6E5635'
$plankSeamDark = '5E4A2D'

$brickPalette = @('6F3B2D', '7F4638', '8F5143', '9F5C4D', 'AB6755', 'B37160')
$brickWeights = @(3, 5, 6, 5, 4, 2)

# Mortar is not a flat fill: it carries as much grain as the brick faces, which
# is most of what stops the texture reading as vector art. Darker than it was -
# the reference spread is 22 where ours reached 42, because pale mortar against
# dark brick is most of a texture's contrast all by itself.
$mortarPalette = @('80756E', '8A7F77', '948980', '9E9389', 'A69B91')
$mortarWeights = @(2, 4, 6, 4, 2)

# Nearly twice the spread it had: reference sand covers 187-233 where ours
# covered 196-222 and read as flat card.
$sandPalette  = @('C6BC92', 'CFC59B', 'D8CEA4', 'E1D7AD', 'EAE0B6', 'F0E6BC')
$sandWeights  = @(2, 4, 6, 6, 4, 2)

# Warm and blotchy, weighted toward the brighter end so it reads as a light
# source even before anything is actually lit by it. The reference spans 75-255
# against our 108-241: a glowing block needs genuinely dark pits for its bright
# cores to register as bright.
$glowPalette = @('6B4F1F', '85632A', 'A07935', 'BA8F42', 'D3A754', 'E4BC68')
$glowWeights = @(3, 5, 6, 6, 5, 3)
$glowCoreColor = 'FFF6C8'

# Narrow and deliberately low-contrast: water carries its look from being
# see-through and from what is under it, not from its own texture.
$waterPalette = @('2E6FA8', '3479B4', '3A83C0', '408DCB', '4796D6')
$waterWeights = @(2, 4, 6, 4, 2)

$barkPalette = @('3B2A19', '503B25', '634B2E', '775C39', '8F7145', '9E7E50')
$barkWeights = @(7, 14, 15, 42, 16, 5)

$logCorePalette = @('8A6C42', '9A7A4C', 'A78754', 'AF8E5B', 'B89862')
$logCoreWeights = @(2, 4, 6, 4, 2)

# Green is baked in for the same reason ground cover is. Four tones and roughly
# a third of the tile missing, so a canopy reads as leaves rather than a cube.
$leafPalette = @('3B6328', '47762F', '5D9A3D', '74BC4B')
$leafWeights = @(34, 36, 57, 45)

# Lighter and yellower than canopy leaves: plants read brighter because they are
# lit from every side.
$tallGrassPalette = @('4A7A2E', '55892F', '629B36', '6FAD3E', '7EC048', '8FD456')
$tallGrassWeights = @(8, 20, 28, 25, 40, 19)

# Pebbles in soil, and the darkest crumbs. Sparse by design.
$pebbleColor = ConvertTo-Color '82817C'
$crumbColor  = ConvertTo-Color '4E3625'

function New-FlatTexture {
    param([string]$Name, [string[]]$Palette, [int[]]$Weights, [int]$Salt, [double]$RunChance = 0.30)

    $grid = New-IndexGrid -Weights $Weights -Salt $Salt -RunChance $RunChance
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

# Stone: the grey ramp plus a few darker flecks so it reads as rock. A high run
# chance because reference stone repeats up to 9 pixels across, where ours
# managed 4 and came out speckled.
function New-StoneTexture {
    $grid = New-IndexGrid -Weights $stoneWeights -Salt 11 -RunChance 0.55
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
    $grassGrid = New-IndexGrid -Weights $grassWeights -Salt 31 -RunChance 0.10
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
                # Not one flat colour across the tile. A uniform seam row is a
                # 16-pixel run, and the reference's longest is 8 - a solid line
                # is what made these read as painted stripes rather than wood.
                $shade = if ((Get-Hash01 -x $x -y $y -salt 411) -gt 0.42) { $plankSeamColor } else { $plankSeamDark }
                $bitmap.SetPixel($x, $y, (ConvertTo-Color $shade))
                continue
            }

            # Grain runs along the board, so a pixel usually repeats the one to
            # its left. Salting by board keeps each plank distinct.
            if ($x -gt 0 -and (Get-Hash01 -x $x -y $y -salt (600 + $board)) -lt 0.38) {
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

# Foliage is clumped and high-contrast, and roughly a third of it is nothing at
# all. Authored greyscale and tinted per biome at draw time, so this file is
# tones rather than colours.
function New-LeavesTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $hole = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)

    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $cx = [Math]::Floor($x / 2)
            $cy = [Math]::Floor($y / 2)

            # Per pixel, nudged by its 2x2 clump rather than decided by it.
            # Letting the clump decide paints obvious 2x2 squares, and doing the
            # colour and the holes on the same grid doubles the effect.
            $roll = (Get-Hash01 -x $x -y $y -salt 163) + ((Get-Hash01 -x $cx -y $cy -salt 151) - 0.5) * 0.35
            $roll = [Math]::Max(0.0, [Math]::Min(0.999, $roll))
            $index = Get-WeightedIndex -Roll $roll -Weights $leafWeights
            $color = ConvertTo-Color $leafPalette[$index]

            # Holes make a canopy read as leaves rather than a solid cube, but
            # too many turn a tree to lace. Tuned by eye, not to a statistic.
            $gx = [Math]::Floor(($x + 1) / 2)
            $gy = [Math]::Floor(($y + 1) / 2)
            $gap = (Get-Hash01 -x $x -y $y -salt 191) * 0.65 + (Get-Hash01 -x $gx -y $gy -salt 179) * 0.35
            if ($gap -gt 0.70) {
                $color = $hole
            }
            $bitmap.SetPixel($x, $y, $color)
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'leaves'
}

# Tall grass: vertical blades of differing height, sparse at the top and solid
# at the base. Measured from the reference sprite, which is 45% hole and whose
# per-row coverage climbs 0, 0, 4, 4, 6, 7, 8, 7, 10, 10, 11, 12, 15, 15, 15, 16.
# One blade per column is what produces that profile.
function New-TallGrassTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $hole = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)

    for ($x = 0; $x -lt $size; $x++) {
        # Neighbouring blades are pushed apart in height deliberately. Drawing
        # each start independently lets adjacent columns land together and merge
        # into wide clumps, where the reference is almost all 1px strands.
        $base = 4 + [int][Math]::Floor((Get-Hash01 -x $x -y 0 -salt 211) * 7)
        $lean = if ($x % 2 -eq 0) { -2 } else { 2 }
        $start = [Math]::Max(2, [Math]::Min(13, $base + $lean))

        for ($y = 0; $y -lt $size; $y++) {
            $color = $hole
            if ($y -ge $start -and (Get-Hash01 -x $x -y $y -salt 223) -gt 0.04) {
                $index = Get-WeightedIndex -Roll (Get-Hash01 -x $x -y $y -salt 227) -Weights $tallGrassWeights
                $color = ConvertTo-Color $tallGrassPalette[$index]
            }
            $bitmap.SetPixel($x, $y, $color)
        }
    }
    Save-Bitmap -Bitmap $bitmap -Name 'tall_grass'
}

# A whittled stick: a three-pixel diagonal running corner to corner, lit along
# its upper-left edge and shadowed along its lower-right so it reads as round
# rather than as a painted line. Item sprites live in the same array as block
# faces, since that array is really "every 16x16 sprite we own".
function New-StickTexture {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $clear = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $bitmap.SetPixel($x, $y, $clear)
        }
    }

    $light = ConvertTo-Color '8C7038'
    $mid = ConvertTo-Color '6B532A'
    $dark = ConvertTo-Color '4C3A1B'
    $edge = ConvertTo-Color '2E2110'

    # A three-wide band, drawn a row at a time. Stepping one pixel diagonally
    # per iteration instead leaves each step overwriting the last, which comes
    # out two pixels wide and reads as a scratch rather than a stick.
    for ($y = 2; $y -le 14; $y++) {
        $xStart = 15 - $y
        for ($k = 0; $k -lt 3; $k++) {
            $x = $xStart + $k
            if ($x -lt 0 -or $x -ge $size) { continue }
            # Lit along the upper-left edge, shadowed along the lower-right.
            $color = if ($k -eq 0) { $light } elseif ($k -eq 1) { $mid } else { $edge }
            if ($k -eq 1 -and (Get-Hash01 -x $x -y $y -salt 811) -gt 0.62) {
                $color = $dark
            }
            $bitmap.SetPixel($x, $y, $color)
        }
    }

    Save-Bitmap -Bitmap $bitmap -Name 'stick'
}

New-StoneTexture
New-DirtTexture
New-FlatTexture -Name 'grass_top' -Palette $grassTopPalette -Weights $grassTopWeights -Salt 31 -RunChance 0.10
New-GrassSideTexture
New-FlatTexture -Name 'sand' -Palette $sandPalette -Weights $sandWeights -Salt 53 -RunChance 0.08
# Clump 1: reference cobblestone is per-pixel noise, 82% of its horizontal runs
# being a single pixel against our 52%.
New-ClumpedTexture -Name 'cobblestone' -Palette $cobblePalette -Weights $cobbleWeights -Salt 71 -Clump 1
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
New-TallGrassTexture
New-StickTexture

# Flat white, for geometry that supplies its own colour: the targeting cage, the
# crosshair, and anything else that must not pick up a material.
$white = New-Object System.Drawing.Bitmap $size, $size
for ($y = 0; $y -lt $size; $y++) {
    for ($x = 0; $x -lt $size; $x++) {
        $white.SetPixel($x, $y, [System.Drawing.Color]::White)
    }
}
Save-Bitmap -Bitmap $white -Name 'white'
