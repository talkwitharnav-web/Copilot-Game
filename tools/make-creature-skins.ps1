# Builds assets/textures/creatures.png - the unwrapped skin sheet for creatures.
#
# The sheet is one column of nets, 64 wide. Creatures are not all the same
# height, so each names the row it starts at rather than an index:
#   row 0    bare sheep hide      (32 rows)
#   row 32   sheep fleece shell   (32 rows)
#   row 64   the Bramble          (32 rows)
#   row 96   the cow              (64 rows)
#   row 160  the pig              (32 rows)
#
# Rows 192-767 are drawn by tools\make-roster-skins.ps1, which this script calls
# at the end. Row offsets, which Creature.cpp must agree with:
#   192 chicken (32)   224 cat (32)      256 camel (128)   384 horse (64)
#   448 mule (64)      512 llama (64)    576 donkey (64)   640 goat (64)
#   704 rabbit (64)
#
# A box face reads one rectangle out of a net. For a box of width w, height h
# and depth d with its net origin at (u, v):
#
#   top    (u+d,       v)      w x d        right  (u,         v+d)  d x h
#   bottom (u+d+w,     v)      w x d        front  (u+d,       v+d)  w x h
#                                           left   (u+d+w,     v+d)  d x h
#                                           back   (u+d+w+d,   v+d)  w x h
#
# Sheep nets, which Creature.cpp must agree with exactly:
#   head  (0, 0)   6 x 6 x 8      fleece head  (0, 0)   6 x 6 x 6
#   leg   (0, 16)  4 x 12 x 4     fleece body  (28, 8)  8 x 16 x 6
#   body  (28, 8)  8 x 16 x 6
#
# Bramble nets (skin 2):
#   head  (0, 0)   8 x 8 x 8
#   body  (16, 16) 8 x 12 x 4
#   leg   (0, 16)  4 x 6 x 4
#
# Cow nets (row 96), on a 64-row net rather than 32:
#   head  (0, 0)   8 x 8 x 6
#   body  (18, 4)  12 x 18 x 10   (lying)
#   leg   (0, 16)  4 x 12 x 4
#   udder (0, 32)  6 x 3 x 2
#
# Palettes are ours. The *layout* is anatomy and is shared with any blocky
# quadruped; the colours and pixel choices are original work.
#
#   powershell -NoProfile -File tools\make-creature-skins.ps1

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$outputDir = Join-Path $PSScriptRoot "..\assets\textures"
if (-not (Test-Path $outputDir)) { New-Item -ItemType Directory -Path $outputDir | Out-Null }

$skinWidth = 128
$skinHeight = 32
# Must match kCreatureSheetHeight in game/src/world/Creature.hpp. The last 32
# rows are the charged Bramble's energy shell rather than a species.
$sheetHeight = 3552

function ConvertTo-Color {
    param([string]$Hex)
    [System.Drawing.Color]::FromArgb(255, [Convert]::ToInt32($Hex.Substring(0, 2), 16),
        [Convert]::ToInt32($Hex.Substring(2, 2), 16), [Convert]::ToInt32($Hex.Substring(4, 2), 16))
}

# Deterministic value hash, so a regenerated skin is byte-identical.
# The multiplies must stay inside Int32: PowerShell silently widens an overflow
# to Int64 and the [int] cast then throws once per pixel.
function Get-Hash01 {
    param([int]$x, [int]$y, [int]$salt)
    $n = ($x * 73856093) -bxor ($y * 19349663) -bxor ($salt * 83492791)
    $n = [Math]::Abs($n % 2147483647)
    $v = [Math]::Sin($n * 12.9898) * 43758.5453
    return $v - [Math]::Floor($v)
}

# [int] *rounds* in PowerShell rather than truncating, so [int](0.9 * 3) is 3 -
# one past the end of a three-colour palette. That yields $null, which coerces
# to an empty string and throws inside ConvertTo-Color once per pixel.
function Get-PaletteIndex {
    param([string[]]$Palette, [double]$Roll)
    $i = [int][Math]::Floor($Roll * $Palette.Count)
    if ($i -ge $Palette.Count) { $i = $Palette.Count - 1 }
    if ($i -lt 0) { $i = 0 }
    return $i
}

# --- Palettes -------------------------------------------------------------
# Measured discipline from reference art: very few tones, and a tight luminance
# spread. Hide sits inside ~20 luminance across three tones; fleece inside ~40
# across four. Widening either is what makes pixel art look like noise.
$hide = @('9C7E70', 'A78A7B', 'B29687')      # lum ~132..152
$hideDark = '6B564B'
$hoof = '4A3D36'
$fleece = @('D4D2CB', 'E0DED7', 'EAE8E2', 'F4F2EE')  # lum ~210..242
$muzzle = @('CE9A9A', 'DFAEAE')
$eyeDark = '17130F'
$eyeGlint = 'F2F0EA'

# Bramble: mossy greens, densely dithered, with near-black sockets for eyes and
# mouth. Nothing warm on it - see the note by the face for why.
$moss = @('3A5B33', '456B3C', '507A45', '5C8A4F')
$mossDark = @('25401F', '2E4C27', '365A2E')
$socket = '141C12'
$ember = 'C8642A'

$sheet = New-Object System.Drawing.Bitmap $skinWidth, $sheetHeight

# Everything transparent first: a texel no face ever samples must stay empty so
# a mistake shows as a hole rather than as plausible-looking noise.
$clear = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)
for ($y = 0; $y -lt $sheetHeight; $y++) {
    for ($x = 0; $x -lt $skinWidth; $x++) { $sheet.SetPixel($x, $y, $clear) }
}

function Set-Rect {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string[]]$Palette, [int]$Salt)
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $r = Get-Hash01 -x ($X + $i) -y ($Y + $j) -salt $Salt
            $idx = Get-PaletteIndex -Palette $Palette -Roll $r
            $sheet.SetPixel($X + $i, $Y + $j, (ConvertTo-Color $Palette[$idx]))
        }
    }
}

function Set-Solid {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string]$Hex)
    $c = ConvertTo-Color $Hex
    for ($j = 0; $j -lt $H; $j++) { for ($i = 0; $i -lt $W; $i++) { $sheet.SetPixel($X + $i, $Y + $j, $c) } }
}

# Fills exactly the six rectangles a box net uses and nothing else, so any texel
# outside the net stays transparent and a mapping mistake shows as a hole.
function Clamp8 {
    param([double]$v)
    return [int][Math]::Max(0, [Math]::Min(255, [Math]::Round($v)))
}

# Per-pixel colour jitter, giving hundreds of distinct colours rather than a
# handful of tones.
#
# Which discipline a creature wants is a property of the creature, not a house
# style. Measured off the reference: a sheep is 13 colours and its fleece 5,
# because wool really is uniform; a creeper is 691 across 2048 pixels, nearly
# one per pixel, because lichen is not. Using a tight palette on something
# mottled makes it look like flat paint.
function Set-Dithered {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [int]$R, [int]$G, [int]$B, [int]$Spread, [int]$Salt)
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $px = $X + $i
            $py = $Y + $j
            # One correlated swing moves all three channels together, which
            # keeps the hue coherent while the value jumps. Small independent
            # nudges per channel then stop it banding into visible steps.
            $l = (Get-Hash01 -x $px -y $py -salt $Salt) - 0.5
            $a = (Get-Hash01 -x $px -y $py -salt ($Salt + 7)) - 0.5
            $c = (Get-Hash01 -x $px -y $py -salt ($Salt + 13)) - 0.5
            # The per-channel nudges scale with the spread. Fixed at +/-20 they
            # were tuned for dense dithering and turned a nearly flat hide into
            # vertical colour noise.
            $k = $Spread * 0.8
            $rr = Clamp8 ($R + $l * 2 * $Spread + $a * $k)
            $gg = Clamp8 ($G + $l * 2 * $Spread + $c * $k)
            $bb = Clamp8 ($B + $l * 2 * $Spread + ($a - $c) * $k * 0.8)
            $sheet.SetPixel($px, $py, [System.Drawing.Color]::FromArgb(255, $rr, $gg, $bb))
        }
    }
}

# The six rectangles of a box net, each dithered with its own salt so no two
# faces repeat the same noise.
function Set-DitheredNet {
    param([int]$U, [int]$V, [int]$W, [int]$H, [int]$D, [int]$R, [int]$G, [int]$B, [int]$Spread, [int]$Salt)
    Set-Dithered ($U + $D) $V $W $D $R $G $B $Spread $Salt
    Set-Dithered ($U + $D + $W) $V $W $D $R $G $B $Spread ($Salt + 21)
    Set-Dithered $U ($V + $D) $D $H $R $G $B $Spread ($Salt + 41)
    Set-Dithered ($U + $D) ($V + $D) $W $H $R $G $B $Spread ($Salt + 61)
    Set-Dithered ($U + $D + $W) ($V + $D) $D $H $R $G $B $Spread ($Salt + 81)
    Set-Dithered ($U + $D + $W + $D) ($V + $D) $W $H $R $G $B $Spread ($Salt + 101)
}

# A box whose form comes from face direction, with only restrained texture
# inside each plane. Large clean-skinned animals need this; rolling a palette
# independently per pixel turns a camel's flank into gravel.
function Set-ShadedNet {
    param([int]$U, [int]$V, [int]$W, [int]$H, [int]$D, [int]$R, [int]$G, [int]$B,
        [int]$Spread, [int]$Salt)
    Set-Dithered ($U + $D) $V $W $D (Clamp8 ($R * 1.08)) (Clamp8 ($G * 1.08)) (Clamp8 ($B * 1.08)) $Spread $Salt
    Set-Dithered ($U + $D + $W) $V $W $D (Clamp8 ($R * 0.72)) (Clamp8 ($G * 0.72)) (Clamp8 ($B * 0.72)) $Spread ($Salt + 21)
    Set-Dithered $U ($V + $D) $D $H (Clamp8 ($R * 0.88)) (Clamp8 ($G * 0.88)) (Clamp8 ($B * 0.88)) $Spread ($Salt + 41)
    Set-Dithered ($U + $D) ($V + $D) $W $H $R $G $B $Spread ($Salt + 61)
    Set-Dithered ($U + $D + $W) ($V + $D) $D $H (Clamp8 ($R * 0.82)) (Clamp8 ($G * 0.82)) (Clamp8 ($B * 0.82)) $Spread ($Salt + 81)
    Set-Dithered ($U + $D + $W + $D) ($V + $D) $W $H (Clamp8 ($R * 0.93)) (Clamp8 ($G * 0.93)) (Clamp8 ($B * 0.93)) $Spread ($Salt + 101)
}

function Set-BoxNet {    param([int]$U, [int]$V, [int]$W, [int]$H, [int]$D, [string[]]$Palette, [int]$Salt)
    Set-Rect ($U + $D) $V $W $D $Palette $Salt
    Set-Rect ($U + $D + $W) $V $W $D $Palette ($Salt + 1)
    Set-Rect $U ($V + $D) (2 * ($W + $D)) $H $Palette ($Salt + 2)
}

# Coarse blotches over an already-filled rect, for markings that come in slabs
# rather than speckles - a cow's patches are several texels across, so the noise
# is sampled on a coarse grid and not per pixel.
function Set-Blotches {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string[]]$Palette, [int]$Salt, [double]$Coverage = 0.42,
        [int]$Cell = 3)
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $cx = [int][Math]::Floor(($X + $i) / $Cell)
            $cy = [int][Math]::Floor(($Y + $j) / $Cell)
            if ((Get-Hash01 -x $cx -y $cy -salt $Salt) -gt (1.0 - $Coverage)) {
                $r = Get-Hash01 -x ($X + $i) -y ($Y + $j) -salt ($Salt + 3)
                $sheet.SetPixel($X + $i, $Y + $j, (ConvertTo-Color $Palette[(Get-PaletteIndex -Palette $Palette -Roll $r)]))
            }
        }
    }
}

# Fill a rect with a base tone that falls off toward its edges.
#
# This is what stops a face reading as a flat slab. The reference pig has no
# markings whatsoever - every bit of its form comes from this vignette, centre
# around luminance 170 dropping to 126 at the rim. Filling each face with its
# mean instead gives correct numbers and a pink brick.
function Set-Vignette {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [int]$R, [int]$G, [int]$B, [double]$EdgeDrop = 0.26,
        [double]$Falloff = 2.5, [int]$Salt = 0)
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $d = [Math]::Min([Math]::Min($i, $W - 1 - $i), [Math]::Min($j, $H - 1 - $j))
            $t = [Math]::Min(1.0, $d / $Falloff)
            $f = 1.0 - $EdgeDrop * (1.0 - $t)
            # A single texel of jitter keeps the falloff from banding into rings
            # without turning the plane into speckle.
            $n = if ($Salt -ne 0) { ((Get-Hash01 -x ($X + $i) -y ($Y + $j) -salt $Salt) - 0.5) * 5 } else { 0 }
            $sheet.SetPixel($X + $i, $Y + $j, [System.Drawing.Color]::FromArgb(255,
                    (Clamp8 ($R * $f + $n)), (Clamp8 ($G * $f + $n)), (Clamp8 ($B * $f + $n))))
        }
    }
}

# Broad form shading for clean hides. Unlike Set-Dithered this does not create
# per-pixel static; unlike a flat face fill it gives large limbs and flanks a
# readable centre and edge.
function Set-ModelledNet {
    param([int]$U, [int]$V, [int]$W, [int]$H, [int]$D, [int]$R, [int]$G, [int]$B,
        [double]$EdgeDrop = 0.14, [double]$Falloff = 4.0)
    Set-Vignette ($U + $D) $V $W $D (Clamp8 ($R * 1.08)) (Clamp8 ($G * 1.08)) (Clamp8 ($B * 1.08)) $EdgeDrop $Falloff
    Set-Vignette ($U + $D + $W) $V $W $D (Clamp8 ($R * 0.72)) (Clamp8 ($G * 0.72)) (Clamp8 ($B * 0.72)) $EdgeDrop $Falloff
    Set-Vignette $U ($V + $D) $D $H (Clamp8 ($R * 0.88)) (Clamp8 ($G * 0.88)) (Clamp8 ($B * 0.88)) $EdgeDrop $Falloff
    Set-Vignette ($U + $D) ($V + $D) $W $H $R $G $B $EdgeDrop $Falloff
    Set-Vignette ($U + $D + $W) ($V + $D) $D $H (Clamp8 ($R * 0.82)) (Clamp8 ($G * 0.82)) (Clamp8 ($B * 0.82)) $EdgeDrop $Falloff
    Set-Vignette ($U + $D + $W + $D) ($V + $D) $W $H (Clamp8 ($R * 0.93)) (Clamp8 ($G * 0.93)) (Clamp8 ($B * 0.93)) $EdgeDrop $Falloff
}

# Adds restrained one-pixel texture without repainting transparent texels or
# erasing the larger form shading already present.
function Add-FineDetail {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [int]$Strength, [int]$Salt,
        [double]$Density = 0.35)
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $pixelX = $X + $i
            $pixelY = $Y + $j
            $pixel = $sheet.GetPixel($pixelX, $pixelY)
            if ($pixel.A -lt 128 -or (Get-Hash01 -x $pixelX -y $pixelY -salt $Salt) -gt $Density) {
                continue
            }
            $direction = if ((Get-Hash01 -x $pixelX -y $pixelY -salt ($Salt + 17)) -lt 0.5) { -1 } else { 1 }
            $shift = $direction * $Strength
            $sheet.SetPixel($pixelX, $pixelY, [System.Drawing.Color]::FromArgb(255,
                    (Clamp8 ($pixel.R + $shift)), (Clamp8 ($pixel.G + $shift)),
                    (Clamp8 ($pixel.B + $shift))))
        }
    }
}

function Set-DetailedNet {
    param([int]$U, [int]$V, [int]$W, [int]$H, [int]$D, [int]$R, [int]$G, [int]$B,
        [double]$EdgeDrop, [double]$Falloff, [int]$Detail, [int]$Salt, [double]$Density = 0.28)
    Set-ModelledNet $U $V $W $H $D $R $G $B $EdgeDrop $Falloff
    Add-FineDetail $U $V (2 * ($W + $D)) ($D + $H) $Detail $Salt $Density
}

function Set-DetailedRect {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [int]$R, [int]$G, [int]$B,
        [double]$EdgeDrop, [double]$Falloff, [int]$Detail, [int]$Salt, [double]$Density = 0.28)
    Set-Vignette $X $Y $W $H $R $G $B $EdgeDrop $Falloff
    Add-FineDetail $X $Y $W $H $Detail $Salt $Density
}

# Large connected markings, stamped as overlapping discs with a ragged rim.
#
# Rolling a cell grid independently - which Set-Blotches does - gives scattered
# squares that read as static, not as a hide. The reference cow's patches are
# single blobs three to eight texels across with irregular edges, and the only
# way to get that is to place centres and grow outward.
function Set-Patches {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string[]]$Palette, [int]$Salt, [int]$Count = 7,
        [double]$MinR = 1.8, [double]$MaxR = 3.8, [double]$Ragged = 1.7)
    $cx = @(); $cy = @(); $cr = @()
    for ($k = 0; $k -lt $Count; $k++) {
        $cx += $X + (Get-Hash01 -x $k -y 0 -salt $Salt) * $W
        $cy += $Y + (Get-Hash01 -x $k -y 1 -salt $Salt) * $H
        $cr += $MinR + (Get-Hash01 -x $k -y 2 -salt $Salt) * ($MaxR - $MinR)
    }
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $px = $X + $i
            $py = $Y + $j
            $hit = $false
            for ($k = 0; $k -lt $Count; $k++) {
                $dx = $px + 0.5 - $cx[$k]
                $dy = $py + 0.5 - $cy[$k]
                # The radius wobbles per pixel, so an edge is ragged rather than
                # a visible circle.
                $rr = $cr[$k] + ((Get-Hash01 -x $px -y $py -salt ($Salt + $k + 11)) - 0.5) * $Ragged
                if (($dx * $dx + $dy * $dy) -lt ($rr * $rr)) { $hit = $true; break }
            }
            if ($hit) {
                $r = Get-Hash01 -x $px -y $py -salt ($Salt + 7)
                $sheet.SetPixel($px, $py, (ConvertTo-Color $Palette[(Get-PaletteIndex -Palette $Palette -Roll $r)]))
            }
        }
    }
}

# =========================================================================
# skin 0 - bare hide
# =========================================================================

# --- head net (0,0) 6 wide, 6 tall, 8 deep -------------------------------
# Most of the skull is already woolly; only the face and the ears are hide.
Set-Rect 8 0 6 8 $fleece 5101      # top of skull
Set-Rect 14 0 6 8 $hide 5103       # underside of the jaw
Set-Rect 0 8 8 6 $fleece 5107      # right cheek
Set-Rect 14 8 8 6 $fleece 5109     # left cheek
Set-Rect 22 8 6 6 $fleece 5111     # back of skull

# Ears: a hide patch on each cheek, 1-3 texels back from the front edge. The
# two rects unwrap in opposite directions, so the offsets mirror.
Set-Rect 4 9 3 2 $hide 5113
Set-Rect 15 9 3 2 $hide 5115

# --- the face, (8,8) 6x6 -------------------------------------------------
# Row 0 is a fleece fringe over the brow, rows 1-3 the hide face with the eyes
# on row 2, rows 4-5 the muzzle with fleece left in the outer corners.
Set-Rect 8 8 6 1 $fleece 5117
Set-Rect 8 9 6 3 $hide 5119
for ($i = 0; $i -lt 6; $i++) {
    $sheet.SetPixel(8 + $i, 12, (ConvertTo-Color $fleece[(Get-PaletteIndex -Palette $fleece -Roll (Get-Hash01 -x $i -y 12 -salt 5121))]))
    $sheet.SetPixel(8 + $i, 13, (ConvertTo-Color $fleece[(Get-PaletteIndex -Palette $fleece -Roll (Get-Hash01 -x $i -y 13 -salt 5123))]))
}
# Muzzle: a two-wide pink block with hide either side, sat in the lower middle.
Set-Solid 9 12 1 2 $hideDark
Set-Solid 12 12 1 2 $hideDark
Set-Solid 10 12 2 1 $muzzle[1]
Set-Solid 10 13 2 1 $muzzle[0]
# Eyes on row 10: dark outboard, a glint inboard, the way the reference sets a
# prey animal's eyes wide.
$sheet.SetPixel(8, 10, (ConvertTo-Color $eyeDark))
$sheet.SetPixel(9, 10, (ConvertTo-Color $eyeGlint))
$sheet.SetPixel(12, 10, (ConvertTo-Color $eyeGlint))
$sheet.SetPixel(13, 10, (ConvertTo-Color $eyeDark))

# --- leg net (0,16) 4 wide, 12 tall, 4 deep ------------------------------
# Sides run fleece at the top where the leg meets the body, hide below, and a
# dark hoof on the last row.
Set-Rect 0 20 16 4 $fleece 5131    # upper leg, all four sides
Set-Rect 0 24 16 7 $hide 5133      # lower leg
Set-Solid 0 31 16 1 $hoof
Set-Rect 4 16 4 4 $fleece 5135     # top cap
Set-Solid 8 16 4 4 $hoof           # underside of the hoof

# --- body net (28,8) 8 wide, 16 tall, 6 deep -----------------------------
# Bare skin, which is what shows once the fleece is shorn off.
Set-Rect 28 8 28 22 $hide 5141

# =========================================================================
# skin 1 - the fleece shell
# =========================================================================
$o = $skinHeight

# Fleece head is 6 x 6 x 6, a shallower box than the skull beneath it.
Set-Rect 6 ($o + 0) 12 6 $fleece 5201
Set-Rect 0 ($o + 6) 24 6 $fleece 5203

# Fleece body matches the body net exactly.
Set-Rect 34 ($o + 8) 16 6 $fleece 5205
Set-Rect 28 ($o + 14) 28 16 $fleece 5207

# =========================================================================
# skin 2 - the Bramble
# =========================================================================
# Our own creature: a mossy, upright thing that hunts at night. Densely
# dithered rather than painted in tones, because a lichen-covered hide is
# mottled - see Set-Dithered for why this differs from the sheep.
#
# Ours is a colder, bluer green than the reference's, and the face is our own
# arrangement: sunken sockets and a jagged seam
# instead of a grin.
$b = $skinHeight * 2

Set-DitheredNet 0 $b 8 8 8 74 152 90 58 5301          # head
Set-DitheredNet 16 ($b + 16) 8 12 4 74 152 90 58 5401 # body
Set-DitheredNet 0 ($b + 16) 4 6 4 66 136 80 52 5501   # leg, one net for all four

# Deeper growth down the lower body, so it reads as rooted rather than floating.
Set-Dithered 16 ($b + 26) 24 6 52 112 66 46 5601

# --- the face, on the head's front rect at (8, 8) ------------------------
$fx = 8
$fy = $b + 8

# Sockets are near-black but still dithered - a flat black hole on a mottled
# creature reads as a missing texture.
foreach ($ex in @(1, 5)) {
    Set-Dithered ($fx + $ex) ($fy + 2) 2 2 22 30 24 14 5701
}
# Mouth: a narrow notch that widens, then two fangs left in the lower corners.
Set-Dithered ($fx + 3) ($fy + 4) 2 1 22 30 24 14 5711
Set-Dithered ($fx + 2) ($fy + 5) 4 2 22 30 24 14 5721
Set-Dithered ($fx + 2) ($fy + 7) 1 1 22 30 24 14 5731
Set-Dithered ($fx + 5) ($fy + 7) 1 1 22 30 24 14 5741

# One ember per socket is reserved for a nightmare difficulty, not the default
# creature - a glowing eye reads as "this one is special", so spending it on the
# ordinary version leaves nothing to escalate to. Difficulties are a later
# milestone; when they arrive, paint 'C4552A' at (fx+2, fy+2) and (fx+5, fy+2).

# =========================================================================
# row 96 - the cow, on a 64-row net
# =========================================================================
# Flat fields rather than dithering: the reference is 28 colours, because a hide
# is broad slabs of tone. Markings are large connected blobs - independently
# rolled cells read as static, which is what made the first attempt look
# machine-made rather than animal.
#
# Structure follows the reference closely because that is anatomy: patched hide,
# a blaze from the crown down the forehead, eye whites with the pupil set at the
# outer corner, and a muzzle only slightly lighter than the hide - the reference
# lifts it from luminance 56 to 82, not to white. Every colour value is ours.
$cowHide = @('3A2E24', '43342A', '4C3D31')
$cowPatch = @('BEB6A6', 'CFC7B7', 'DED7C7')
$cowMuzzle = @('6B5847', '75604E')
$cowUdder = @('C89494', 'D8A4A4')
$cowEar = @('BC8686', 'CA9494')
$cowDark = '15110E'
$cowEye = 'F2EFE7'
$cowHorn = @('C0B8A4', 'CFC7B2')
$cw = 96

# --- body net (18,4) 12 x 18 x 10 ---------------------------------------
Set-BoxNet 18 ($cw + 4) 12 18 10 $cowHide 6101
# Patches over the whole net. The two bands are stamped separately so a blob
# cannot straddle the seam between them, which would tear across the model.
# Few and large on purpose: the reference's markings merge into a handful of big
# irregular shapes, and lots of small discs read as spots rather than hide.
Set-Patches 28 ($cw + 4) 24 10 $cowPatch 6111 3 2.6 5.0 2.0
Set-Patches 18 ($cw + 14) 44 18 $cowPatch 6113 6 3.0 6.2 2.2
# The belly is the net's front rect, which the quarter turn puts underneath. It
# stays dark except where the udder meets it.
Set-Rect 28 ($cw + 14) 12 18 $cowHide 6115
Set-Rect 31 ($cw + 26) 6 6 $cowUdder 6117

# --- head net (0,0) 8 x 8 x 6 -------------------------------------------
Set-BoxNet 0 ($cw + 0) 8 8 6 $cowHide 6201
# Blaze: narrow at the crown, widening as it comes down the skull, then across
# the top of the face.
Set-Rect 10 ($cw + 0) 2 3 $cowPatch 6203
Set-Rect 9 ($cw + 3) 4 1 $cowPatch 6205
Set-Rect 8 ($cw + 4) 6 2 $cowPatch 6207
Set-Rect 9 ($cw + 6) 3 2 $cowPatch 6209
# Eye whites are 2x2 at the outer corners, pupil on the outer edge of each.
Set-Rect 6 ($cw + 8) 2 2 @($cowEye) 6211
Set-Rect 12 ($cw + 8) 2 2 @($cowEye) 6213
$sheet.SetPixel(6, $cw + 9, (ConvertTo-Color $cowDark))
$sheet.SetPixel(13, $cw + 9, (ConvertTo-Color $cowDark))
# Muzzle: barely lighter than the hide, across the lower middle only.
Set-Rect 8 ($cw + 12) 4 1 $cowMuzzle 6215
Set-Rect 7 ($cw + 13) 6 1 $cowMuzzle 6217
$sheet.SetPixel(8, $cw + 13, (ConvertTo-Color $cowDark))
$sheet.SetPixel(11, $cw + 13, (ConvertTo-Color $cowDark))
# Pink inner ear on each cheek. This is the pink you actually see on a standing
# animal - the udder is underneath and the muzzle is grey-brown, not pink.
Set-Rect 2 ($cw + 8) 2 2 $cowEar 6219
Set-Rect 16 ($cw + 8) 2 2 $cowEar 6221

# --- horns (22,0) 1 x 3 x 1 ---------------------------------------------
Set-BoxNet 22 ($cw + 0) 1 3 1 $cowHorn 6301

# --- legs (0,16) 4 x 12 x 4 ---------------------------------------------
Set-BoxNet 0 ($cw + 16) 4 12 4 $cowHide 6401
Set-Patches 0 ($cw + 20) 16 7 $cowPatch 6403 4 1.6 3.0
# Dark hooves on the last two rows and the underside cap.
Set-Rect 0 ($cw + 30) 16 2 @($cowDark, '1C1714') 6405
Set-Solid 8 ($cw + 16) 4 4 $cowDark

# --- udder (52,0) 4 x 6 x 1 ---------------------------------------------
Set-BoxNet 52 ($cw + 0) 4 6 1 $cowUdder 6501

# =========================================================================
# row 160 - the pig, on a 32-row net
# =========================================================================
# Flat fields, very tight: the reference is 12 colours spanning luminance
# 113-187, nearly all of it one pink.
#
# The shading is *per face*, baked to a top-down light - not a palette rolled
# per pixel, and not a gradient inside each rect. Measured off the reference:
# body top 181, sides 166, front/back 152, belly 139; head top 184, sides 159,
# underside 126. A first attempt rolled three tones at random and scored a
# correlation of -0.03 against the reference, which is no agreement at all -
# speckle where the original has clean planes.
#
# Ours is a dustier, less saturated pink at the same luminances.
$pigNostril = '7C4240'
$pigEye = 'F2EEEA'
$pigDark = '0A0708'
$pigCurl = '90655F'
$pigCurlDeep = '744F4B'
$pigSeam = 'C4837E'
$pigEar = 'A87571'
$pigEarDeep = '8A5F5B'
$pg = 160

# --- body net (28,8) 10 x 16 x 8 ----------------------------------------
# Flat per-face tone with barely any jitter. No edge falloff: darkening each
# rect's rim frames every face, and on the model those frames meet as dark
# seams - the animal ends up looking tiled. The reference's shading follows the
# form, never the rectangle.
Set-Dithered 36 ($pg + 8) 10 8 208 129 125 5 7101   # net top    -> world front
Set-Dithered 46 ($pg + 8) 10 8 208 129 125 5 7103   # net bottom -> world back
Set-Dithered 28 ($pg + 16) 8 16 227 141 136 5 7105  # right side
Set-Dithered 36 ($pg + 16) 10 16 190 118 114 5 7107 # net front  -> world belly
Set-Dithered 46 ($pg + 16) 8 16 227 141 136 5 7109  # left side
Set-Dithered 54 ($pg + 16) 10 16 248 154 149 5 7111 # net back   -> world top

# The tail: a small curl on the rear face, painted rather than modelled.
$sheet.SetPixel(50, $pg + 10, (ConvertTo-Color $pigCurl))
$sheet.SetPixel(51, $pg + 10, (ConvertTo-Color $pigCurl))
$sheet.SetPixel(49, $pg + 11, (ConvertTo-Color $pigCurl))
$sheet.SetPixel(52, $pg + 11, (ConvertTo-Color $pigCurlDeep))
$sheet.SetPixel(49, $pg + 12, (ConvertTo-Color $pigCurl))
$sheet.SetPixel(51, $pg + 12, (ConvertTo-Color $pigCurlDeep))

# A soft seam down the belly. This goes on the net's front rect, which the
# quarter turn puts underneath the animal - putting it on the back rect, where
# it is the one face always in view, reads as a zip up the pig's spine.
for ($i = 3; $i -lt 13; $i++) {
    $sheet.SetPixel(41, $pg + 16 + $i, (ConvertTo-Color $pigSeam))
}

# --- head net (0,0) 8 x 8 x 8 -------------------------------------------
Set-Dithered 8 ($pg + 0) 8 8 252 156 151 5 7201     # crown
Set-Dithered 16 ($pg + 0) 8 8 172 107 103 5 7203    # underside of the jaw
Set-Dithered 0 ($pg + 8) 8 8 218 135 131 5 7205     # right cheek
Set-Dithered 16 ($pg + 8) 8 8 218 135 131 5 7207    # left cheek
Set-Dithered 24 ($pg + 8) 8 8 218 135 131 5 7209    # back of skull
# The face falls from brow to jaw. Deliberately not centred: a radial highlight
# puts a halo in the middle of the animal's face.
for ($j = 0; $j -lt 8; $j++) {
    $t = $j / 7.0
    for ($i = 0; $i -lt 8; $i++) {
        $n = ((Get-Hash01 -x (8 + $i) -y ($pg + 8 + $j) -salt 7211) - 0.5) * 10
        $sheet.SetPixel(8 + $i, $pg + 8 + $j, [System.Drawing.Color]::FromArgb(255,
                (Clamp8 (250 - 30 * $t + $n)), (Clamp8 (155 - 20 * $t + $n)),
                (Clamp8 (150 - 19 * $t + $n))))
    }
}
# Ears, on both cheeks 1-3 texels back from the front edge. The two side rects
# unwrap in opposite directions, so the offsets mirror.
Set-Solid 4 ($pg + 10) 3 3 $pigEar
Set-Solid 17 ($pg + 10) 3 3 $pigEar
$sheet.SetPixel(5, $pg + 11, (ConvertTo-Color $pigEarDeep))
$sheet.SetPixel(18, $pg + 11, (ConvertTo-Color $pigEarDeep))
# Eyes on the fourth row: pupil outboard, white just inboard.
$sheet.SetPixel(8, $pg + 11, (ConvertTo-Color $pigDark))
$sheet.SetPixel(9, $pg + 11, (ConvertTo-Color $pigEye))
$sheet.SetPixel(14, $pg + 11, (ConvertTo-Color $pigEye))
$sheet.SetPixel(15, $pg + 11, (ConvertTo-Color $pigDark))

# --- legs (0,16) 4 x 6 x 4 ----------------------------------------------
Set-Dithered 4 ($pg + 16) 4 4 238 148 143 5 7301    # top of the leg
Set-Dithered 8 ($pg + 16) 4 4 164 102 98 5 7303     # underfoot
Set-Dithered 0 ($pg + 20) 16 6 228 141 137 5 7305   # all four sides
Set-Solid 0 ($pg + 25) 16 1 'B4706C'                # the trotter

# --- snout (16,16) 4 x 3 x 1 --------------------------------------------
# Filled by hand rather than through a net helper: the back rect is never
# sampled and is transparent in the reference, so filling it would paint outside
# the net and the similarity check would rightly object.
Set-Solid 17 ($pg + 16) 4 1 'FCA49E'    # top of the snout
Set-Solid 21 ($pg + 16) 4 1 'BC7470'    # under it
Set-Dithered 16 ($pg + 17) 6 3 212 131 127 5 7401
$sheet.SetPixel(17, $pg + 18, (ConvertTo-Color $pigNostril))
$sheet.SetPixel(20, $pg + 18, (ConvertTo-Color $pigNostril))

$path = Join-Path $outputDir "creatures.png"
$sheet.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "wrote creatures.png rows 0-191 ($skinWidth x $sheetHeight)"

# Second stage. Kept a separate file because both scripts grew a `Set-Solid` and
# a `Set-Vignette` with different signatures, and merging them verbatim would
# silently redraw the four species above.
& (Join-Path $PSScriptRoot "make-roster-skins.ps1")
