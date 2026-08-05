# Skins for the nine species on rows 192-767: chicken, cat, camel, horse,
# mule, llama, donkey, goat and rabbit.
#
# This is the SECOND STAGE of one pipeline. `make-creature-skins.ps1` owns rows
# 0-191 (sheep, cow, pig, Bramble), writes the sheet, and then calls this, which
# opens that sheet and paints the rest onto it. They stay separate files because
# both grew their own `Set-Solid` and `Set-Vignette` with different signatures -
# merging them verbatim would silently redraw four species that are already
# approved. Run the first script; it runs this one.
#
#   powershell -NoProfile -File tools\make-creature-skins.ps1
#
# Every coordinate in a species section is NET-LOCAL. `$script:rowBase` is set
# once per species and every drawing primitive adds it, so the numbers here read
# exactly like the net tables below and a species can be moved by changing one
# line. Mixing the two conventions writes the whole animal to row 0.
#
# ---------------------------------------------------------------------------
# What the references actually are, measured with tools/measure-skin.ps1
# ---------------------------------------------------------------------------
# Every one of the nine is the FLAT FIELD discipline - none is a creeper-style
# dither. Authoring these with per-pixel jitter would be the mistake TEXTURING.md
# calls the most expensive one on the project.
#
#   subject   colours  lum span   sat   character
#   chicken      14    198..255   0.12  near-white, red comb, gold beak
#   cat          13     65..234   0.41  brown tabby, cream belly, yellow eyes
#   rabbit       11     43..204   0.48  mid tan, soft mottling
#   goat         15     51..255   0.09  near-white fleece, grey horns
#   camel        19    136..202   0.68  six ochres, chevron shading, no markings
#   horse        25     19..48    0.88  very dark bay, black mane, white eye
#   mule         36     24..142   0.49  dark brown, mealy oatmeal muzzle
#   donkey       36     72..172   0.23  grey-brown, cream muzzle, dark cross
#   llama        34     84..227   0.65  russet fleece, cream face mask
#
# Detail therefore comes from STRUCTURE, not noise: one flat plane per world
# face, gradients that follow the form, and markings that are real anatomy
# (mackerel stripes, a dorsal cross, hoof bands, fur fringes).
#
# Ours deliberately take different coats so no colour is shared: a wheaten hen
# rather than white, a ginger tabby rather than brown, a chestnut horse with a
# flaxen mane rather than a black-maned bay, an ash donkey, an oatmeal goat, a
# sand camel, a cream llama with a chocolate saddle, a russet rabbit. Verified
# at zero shared RGB values against every reference.
#
# ---------------------------------------------------------------------------
# Net tables. game/src/world/Creature.cpp must agree with these exactly.
# ---------------------------------------------------------------------------
# For a box w wide, h tall, d deep with its net origin at (u, v):
#   top    (u+d,     v)    w x d      right (u,       v+d)  d x h
#   bottom (u+d+w,   v)    w x d      front (u+d,     v+d)  w x h
#                                     left  (u+d+w,   v+d)  d x h
#                                     back  (u+d+w+d, v+d)  w x h
#
# row 192 chicken (32 rows)
#   head (0,0) 4x6x3   beak (14,0) 4x2x2   wattle (14,4) 2x2x2
#   body (0,9) 6x8x6 LYING             wing (24,13) 1x4x6
#   leg strip (36,3) 1x5 plane         foot (32,0) 3x3 alpha plane
# row 224 cat (32 rows)
#   head (0,0) 5x4x5   body (20,0) 4x16x6 LYING   frontLeg (40,0) 2x10x2
#   hindLeg (8,13) 2x6x2   tail (0,15) and (4,15) 1x8x1
#   ear (0,10) and (6,10) 1x1x2        muzzle (0,24) 3x2x2, back face unused
# row 256 camel (128 rows)
#   body (0,25) 15x12x27   hump (74,0) 9x5x11   neck (21,0) 7x14x7
#   head (60,24) 7x8x19    muzzle (50,0) 5x5x6  ear (45,0) and (67,0) 3x1x2
#   legs (0,0) (0,26) (58,16) (94,16) 5x21x5    tail (122,0) 3x14 plane
# rows 384 horse / 448 mule / 576 donkey (64 rows each), one shared layout
#   body (0,32) 10x10x22   neck (0,35) 4x12x7   head (0,13) 6x5x7
#   muzzle (0,25) 4x5x5    leg (48,21) 4x11x4
#   mane (56,36) 2x16x2    tail (42,36) 3x14x4, bottom face unused
#   ear: horse short (19,16) 2x3x1; mule and donkey long (0,12) 2x7x1
# row 512 llama (64 rows)
#   head (0,0) 4x4x9   ear (17,0) 3x3x2   neck (0,14) 8x18x6
#   body (29,0) 12x18x10 LYING            leg (29,29) 4x14x4
# row 640 goat (64 rows)
#   body (1,1) 9x11x16     ruff (0,28) 11x14x11, bottom face transparent
#   head (34,46) 5x8x10    horn (12,55) 2x7x2   ear (2,61) 3x2x1
#   frontLeg (35,2) and (49,2) 3x10x3           hindLeg (36,29) and (49,29) 3x6x3
#   beard (23,56) 4x7x1, top and bottom faces unused
# row 704 rabbit (64 rows)
#   body (0,0) 8x6x10      head (0,16) 5x5x5    ear (26,0) and (32,0) 2x5x1
#   haunch (20,16) 4x4x4   frontLeg (36,18) and (44,18) 2x4x2
#   hindFoot (20,24) and (36,24) 2x1x6

param(
    [string]$SheetPath = "assets\textures\creatures.png"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$resolved = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\$SheetPath"))
if (-not (Test-Path $resolved)) {
    throw "No sheet at $resolved - run tools\make-creature-skins.ps1 first"
}

$script:rowBase = 0

# Loaded rather than created: rows 0-191 belong to the first stage and are
# carried through untouched. Cloning first because the file handle stays locked
# for the lifetime of a Bitmap opened straight from disk, and this saves back
# over the same path.
#
# The size comes from the file, never from a constant here. This script once
# carried its own copy of the sheet height, and that constructor is the
# *scaling* one - so growing the sheet in the first stage silently resampled it
# back down on save, and the new rows vanished with no error anywhere.
$loaded = [System.Drawing.Bitmap]::FromFile($resolved)
$sheetWidth = $loaded.Width
$sheetHeight = $loaded.Height
$sheet = New-Object System.Drawing.Bitmap $loaded, $sheetWidth, $sheetHeight
$loaded.Dispose()
$clear = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)

# --- helpers --------------------------------------------------------------

function ConvertTo-Color {
    param([string]$Hex)
    # Cheap, and it turns a malformed generated tone into an immediate stop
    # instead of a plausible-looking wrong colour spread over a whole animal.
    if ($Hex.Length -ne 6) { throw "Tone '$Hex' is not six hex digits" }
    [System.Drawing.Color]::FromArgb(255, [Convert]::ToInt32($Hex.Substring(0, 2), 16),
        [Convert]::ToInt32($Hex.Substring(2, 2), 16), [Convert]::ToInt32($Hex.Substring(4, 2), 16))
}

# Blends two tones into a third palette entry. This is not smoothing - the
# result is one more discrete value for a per-pixel choice, which is how the
# references reach 25-36 colours off a handful of anchors. Blending *across a
# surface* is what pixel art never does; blending a swatch is just mixing paint.
#
# The locals are prefixed because PowerShell variable names are CASE
# INSENSITIVE: a plain `$b` here assigns to the `[string]$B` parameter, silently
# coerces 186 to "186", and `{2:X2}` then emits it verbatim - a seven-character
# hex string that reads back as a bright yellow.
function Get-Mix {
    param([string]$A, [string]$B, [double]$T)
    $ca = ConvertTo-Color $A
    $cb = ConvertTo-Color $B
    $mixR = [int][Math]::Round($ca.R + ($cb.R - $ca.R) * $T)
    $mixG = [int][Math]::Round($ca.G + ($cb.G - $ca.G) * $T)
    $mixB = [int][Math]::Round($ca.B + ($cb.B - $ca.B) * $T)
    return "{0:X2}{1:X2}{2:X2}" -f $mixR, $mixG, $mixB
}

# Deterministic, so regenerating is byte-identical. Multipliers stay inside
# Int32 - PowerShell widens an overflow to Int64 and the [int] cast then throws
# once per pixel.
function Get-Hash01 {
    param([int]$x, [int]$y, [int]$salt)
    $n = ($x * 73856093) -bxor ($y * 19349663) -bxor ($salt * 83492791)
    $n = [Math]::Abs($n % 2147483647)
    $v = [Math]::Sin($n * 12.9898) * 43758.5453
    return $v - [Math]::Floor($v)
}

function Set-Pixel {
    param([int]$X, [int]$Y, [string]$Hex)
    $py = $Y + $script:rowBase
    if ($X -ge 0 -and $py -ge 0 -and $X -lt $sheetWidth -and $py -lt $sheetHeight) {
        $sheet.SetPixel($X, $py, (ConvertTo-Color $Hex))
    }
}

function Set-Solid {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string]$Hex)
    if ($W -le 0 -or $H -le 0) { return }
    $c = ConvertTo-Color $Hex
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $px = $X + $i; $py = $Y + $j + $script:rowBase
            if ($px -ge 0 -and $py -ge 0 -and $px -lt $sheetWidth -and $py -lt $sheetHeight) {
                $sheet.SetPixel($px, $py, $c)
            }
        }
    }
}

function Set-Clear {
    param([int]$X, [int]$Y, [int]$W, [int]$H)
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $px = $X + $i; $py = $Y + $j + $script:rowBase
            if ($px -ge 0 -and $py -ge 0 -and $px -lt $sheetWidth -and $py -lt $sheetHeight) {
                $sheet.SetPixel($px, $py, $clear)
            }
        }
    }
}

# A rectangle stepped through a tone list. `Axis` is 'V' top to bottom, 'H' left
# to right, or 'R' outward from the centre, which is what a rounded limb looks
# like. Steps, never a blend: pixel art does not blur.
function Set-Ramp {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string[]]$Tones, [string]$Axis = 'V')
    $last = $Tones.Count - 1
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            switch ($Axis) {
                'H' { $t = if ($W -le 1) { 0.0 } else { $i / [double]($W - 1) } }
                'R' {
                    $cx = ($W - 1) / 2.0
                    $t = if ($cx -le 0) { 0.0 } else { [Math]::Abs($i - $cx) / $cx }
                }
                default { $t = if ($H -le 1) { 0.0 } else { $j / [double]($H - 1) } }
            }
            $idx = [int][Math]::Floor($t * ($last + 0.999))
            if ($idx -gt $last) { $idx = $last }
            if ($idx -lt 0) { $idx = 0 }
            Set-Pixel ($X + $i) ($Y + $j) $Tones[$idx]
        }
    }
}

# Darker toward every edge, lighter in the middle. This is the shape the camel's
# reference uses on every limb, and it is what stops a flat plane reading as
# cardboard.
function Set-Vignette {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string[]]$Tones)
    $last = $Tones.Count - 1
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $dx = if ($W -le 1) { 0.0 } else { [Math]::Abs($i - ($W - 1) / 2.0) / (($W - 1) / 2.0) }
            $dy = if ($H -le 1) { 0.0 } else { [Math]::Abs($j - ($H - 1) / 2.0) / (($H - 1) / 2.0) }
            $t = [Math]::Max($dx, $dy * 0.75)
            $idx = [int][Math]::Floor($t * ($last + 0.999))
            if ($idx -gt $last) { $idx = $last }
            Set-Pixel ($X + $i) ($Y + $j) $Tones[$idx]
        }
    }
}

# Sparse accent pixels. Used to break a plane without turning it into static -
# the references carry a handful of these per face, never a wash.
function Set-Speckle {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string]$Hex, [double]$Rate, [int]$Salt)
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            if ((Get-Hash01 -x ($X + $i) -y ($Y + $j) -salt $Salt) -lt $Rate) {
                Set-Pixel ($X + $i) ($Y + $j) $Hex
            }
        }
    }
}

# Fine per-texel tone variation inside a field, weighted so most pixels keep the
# field's own tone and the extremes stay sparse.
#
# This is what separates a hide from coloured paper. Every reference here is
# 11-36 colours, and almost all of that count comes from a few tones a step
# either side of the base being sprinkled across each face - not from wide
# contrast. An *even* pick over the same tones reads as static instead, which is
# the mistake recorded against the first cow.
function Set-Grain {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string[]]$Tones, [double[]]$Weights, [int]$Salt,
          [double]$Rate = 1.0)
    if ($Tones.Count -eq 0) { return }
    $sum = 0.0
    foreach ($weight in $Weights) { $sum += $weight }
    if ($sum -le 0.0) { return }

    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            # Below full coverage the untouched pixels keep whatever a ramp or a
            # vignette left there. Replacing every texel destroys the modelled
            # shading underneath, which is what buried the camel's limbs.
            if ($Rate -lt 1.0 -and (Get-Hash01 -x ($X + $i) -y ($Y + $j) -salt ($Salt + 911)) -ge $Rate) {
                continue
            }
            $roll = (Get-Hash01 -x ($X + $i) -y ($Y + $j) -salt $Salt) * $sum
            $pick = $Tones.Count - 1
            $running = 0.0
            for ($k = 0; $k -lt $Tones.Count; $k++) {
                $running += $Weights[$k]
                if ($roll -lt $running) { $pick = $k; break }
            }
            Set-Pixel ($X + $i) ($Y + $j) $Tones[$pick]
        }
    }
}

# Hair strokes of uneven length, spacing and start row.
#
# `Set-Streaks` puts every dash on the same modulo column and the same baseline,
# which reads as corduroy - the goat came out looking combed. Real fur has no
# two neighbouring strokes alike, so column choice, start, length and tone are
# all rolled separately.
function Set-Hairs {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string[]]$Tones, [double]$Rate, [int]$Salt,
          [int]$MaxLen = 3)
    if ($H -lt 1 -or $Tones.Count -eq 0) { return }
    for ($i = 0; $i -lt $W; $i++) {
        $col = $X + $i
        if ((Get-Hash01 -x $col -y $Y -salt $Salt) -gt $Rate) { continue }
        $strokes = 1 + [int][Math]::Floor((Get-Hash01 -x $col -y ($Y + 7) -salt $Salt) * 1.999)
        for ($s = 0; $s -lt $strokes; $s++) {
            $start = [int][Math]::Floor((Get-Hash01 -x $col -y ($Y + 13 + $s * 5) -salt $Salt) * ($H - 0.001))
            $len = 1 + [int][Math]::Floor((Get-Hash01 -x $col -y ($Y + 29 + $s * 5) -salt $Salt) * ($MaxLen - 0.001))
            if ($start + $len -gt $H) { $len = $H - $start }
            if ($len -lt 1) { continue }
            $pick = [int][Math]::Floor((Get-Hash01 -x $col -y ($Y + 41 + $s * 5) -salt $Salt) * ($Tones.Count - 0.001))
            Set-Solid $col ($Y + $start) 1 $len $Tones[$pick]
        }
    }
}

# Grain applied to one named rectangle of a net. Kept separate from `Set-Grain`
# so a section can name a face and let each keep its own base tone - graining a
# whole net with one palette would flatten the anatomical shading that puts a
# light back and a dark belly on the same animal.
function Set-RectGrain {
    param([int[]]$Rect, [string[]]$Tones, [double[]]$Weights, [int]$Salt, [double]$Rate = 1.0)
    Set-Grain $Rect[0] $Rect[1] $Rect[2] $Rect[3] $Tones $Weights $Salt $Rate
}

# Short vertical dashes of uneven length rising from the bottom of a band, which
# is how the goat's shaggy coat and the llama's fleece are drawn. Column choice
# is deterministic, so the result reads as combed hair rather than noise.
function Set-Streaks {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [string]$Hex, [int]$Every, [int]$Salt, [int]$MinLen = 2)
    if ($H -lt $MinLen) { return }
    for ($i = 0; $i -lt $W; $i++) {
        if ((($X + $i) % $Every) -ne ($Salt % $Every)) { continue }
        $roll = Get-Hash01 -x ($X + $i) -y $Y -salt $Salt
        $len = $MinLen + [int][Math]::Floor($roll * ($H - $MinLen + 0.999))
        if ($len -gt $H) { $len = $H }
        Set-Solid ($X + $i) ($Y + $H - $len) 1 $len $Hex
    }
}

# Cuts the bottom rows of a band into hanging strands. The goat's ruff and beard
# are alpha-shaped this way in the reference, and a square hem reads as a
# blanket rather than fur.
function Set-Fringe {
    param([int]$X, [int]$Y, [int]$W, [int]$H, [int]$Salt)
    for ($i = 0; $i -lt $W; $i++) {
        $roll = Get-Hash01 -x ($X + $i) -y $Y -salt $Salt
        $keep = [int][Math]::Floor($roll * ($H + 0.999))
        if ($keep -gt $H) { $keep = $H }
        if ($keep -lt $H) { Set-Clear ($X + $i) ($Y + $keep) 1 ($H - $keep) }
    }
}

# Fills exactly the six rectangles a box net uses, named by the world face each
# one ends up on. `-Lying` applies the quarter turn the body boxes take, where
# the net's back rect becomes the world top and its front rect the underside -
# getting that backwards is what once painted a cow's belly across its spine.
function Set-Net {
    param(
        [int]$U, [int]$V, [int]$W, [int]$H, [int]$D,
        [string]$Top, [string]$Bottom, [string]$Front, [string]$Back, [string]$Side,
        [switch]$Lying
    )
    $netTop = if ($Lying) { $Front } else { $Top }
    $netBottom = if ($Lying) { $Back } else { $Bottom }
    $netFront = if ($Lying) { $Bottom } else { $Front }
    $netBack = if ($Lying) { $Top } else { $Back }

    Set-Solid ($U + $D) $V $W $D $netTop
    Set-Solid ($U + $D + $W) $V $W $D $netBottom
    Set-Solid $U ($V + $D) $D $H $Side
    Set-Solid ($U + $D) ($V + $D) $W $H $netFront
    Set-Solid ($U + $D + $W) ($V + $D) $D $H $Side
    Set-Solid ($U + $D + $W + $D) ($V + $D) $W $H $netBack
}

# Where each of a net's six rectangles lives, net-local, so a section can
# decorate one by name instead of recomputing offsets and getting them subtly
# wrong.
function Get-NetRects {
    param([int]$U, [int]$V, [int]$W, [int]$H, [int]$D, [switch]$Lying)
    # Every offset is parenthesised: PowerShell binds `,` tighter than `+`, so
    # @($U + $D, $V, ...) parses as $U + ($D, $V, ...) and throws op_Addition.
    $r = @{
        NetTop    = @(($U + $D), $V, $W, $D)
        NetBottom = @(($U + $D + $W), $V, $W, $D)
        Right     = @($U, ($V + $D), $D, $H)
        NetFront  = @(($U + $D), ($V + $D), $W, $H)
        Left      = @(($U + $D + $W), ($V + $D), $D, $H)
        NetBack   = @(($U + $D + $W + $D), ($V + $D), $W, $H)
    }
    if ($Lying) {
        $r.Top = $r.NetBack; $r.Bottom = $r.NetFront; $r.Front = $r.NetTop; $r.Back = $r.NetBottom
    } else {
        $r.Top = $r.NetTop; $r.Bottom = $r.NetBottom; $r.Front = $r.NetFront; $r.Back = $r.NetBack
    }
    return $r
}

# =========================================================================
# row 192 - chicken. A wheaten hen: warm buff plumage rather than the
# reference's near-white, a deep scarlet comb and wattle, a horn-amber beak
# and slate legs. Reference discipline: 14 colours, one flat plane per face.
# =========================================================================
$script:rowBase = 192
# Near-white with a warm cast rather than buff: the reference body measures
# luminance 215 and a wheaten bird read as dirty beside it.
$plumeHi = 'F8F4EC'
$plumeMid = 'EDE7DA'
$plumeSide = 'DFD8C8'
$plumeLow = 'CAC1AE'
$plumeDeep = 'B0A895'
$plumeEdge = '968D7B'
$combHi = 'C93B44'
$combLo = '9A2530'
$beakHi = 'DCAE55'
$beakLo = 'A87E31'
# Chickens have amber legs and feet, not grey ones - the reference runs
# 211,174,90 up to 252,229,112 and slate read as a wading bird.
$legHi = 'F4DC7A'
$legMid = 'E2C25C'
$legLo = 'CBA544'
$chickEye = '18140F'
$chickEyeRing = 'F8F2E4'
$plumeFleck = 'D9D0C0'
$plumeWarm = 'F2ECE0'

# Body, laid forward. The back catches the light, the belly is deepest.
Set-Net -U 0 -V 9 -W 6 -H 8 -D 6 -Lying `
        -Top $plumeHi -Bottom $plumeDeep -Front $plumeMid -Back $plumeLow -Side $plumeSide
$chBody = Get-NetRects -U 0 -V 9 -W 6 -H 8 -D 6 -Lying

# Plumage grain. Each face keeps its own tone as the majority and borrows a
# step either side, which is where the reference's colour count actually comes
# from - not from contrast, which at this scale reads as dirt.
Set-RectGrain $chBody.Top @($plumeHi, $plumeWarm, $plumeMid) @(0.62, 0.26, 0.12) 9101
Set-RectGrain $chBody.Right @($plumeSide, $plumeMid, $plumeFleck) @(0.60, 0.26, 0.14) 9103
Set-RectGrain $chBody.Left @($plumeSide, $plumeMid, $plumeFleck) @(0.60, 0.26, 0.14) 9105
Set-RectGrain $chBody.Back @($plumeLow, $plumeFleck, $plumeDeep) @(0.58, 0.28, 0.14) 9107

Set-Ramp $chBody.Front[0] $chBody.Front[1] $chBody.Front[2] $chBody.Front[3] @($plumeHi, $plumeMid) 'V'
Set-Ramp $chBody.Bottom[0] $chBody.Bottom[1] $chBody.Bottom[2] $chBody.Bottom[3] @($plumeLow, $plumeDeep) 'V'
# Tail coverts: three stepped feather bands across the rear.
Set-Solid $chBody.Back[0] ($chBody.Back[1] + 1) $chBody.Back[2] 1 $plumeDeep
Set-Solid ($chBody.Back[0] + 1) ($chBody.Back[1] + 3) ($chBody.Back[2] - 2) 1 $plumeEdge
Set-Solid $chBody.Back[0] ($chBody.Back[1] + 5) $chBody.Back[2] 1 $plumeDeep
# Flank feathering: staggered pairs of short dashes, offset row to row, which
# is how overlapping coverts sit. Straight rungs read as a striped jumper.
foreach ($flank in @($chBody.Right, $chBody.Left)) {
    Set-Solid ($flank[0] + 1) ($flank[1] + 2) 2 1 $plumeLow
    Set-Solid ($flank[0] + 4) ($flank[1] + 3) 2 1 $plumeLow
    Set-Solid ($flank[0] + 2) ($flank[1] + 4) 2 1 $plumeLow
    Set-Solid ($flank[0] + 5) ($flank[1] + 5) 1 1 $plumeEdge
    Set-Solid ($flank[0] + 1) ($flank[1] + 6) 2 1 $plumeEdge
    Set-Solid ($flank[0] + 4) ($flank[1] + 7) 2 1 $plumeEdge
}

# Head. The crown carries the comb, the throat the wattle.
Set-Net -U 0 -V 0 -W 4 -H 6 -D 3 `
        -Top $plumeHi -Bottom $plumeMid -Front $plumeMid -Back $plumeSide -Side $plumeSide
$chHead = Get-NetRects -U 0 -V 0 -W 4 -H 6 -D 3
Set-RectGrain $chHead.Right @($plumeSide, $plumeMid, $plumeWarm) @(0.58, 0.28, 0.14) 9109
Set-RectGrain $chHead.Left @($plumeSide, $plumeMid, $plumeWarm) @(0.58, 0.28, 0.14) 9111
Set-RectGrain $chHead.Back @($plumeSide, $plumeFleck, $plumeMid) @(0.60, 0.24, 0.16) 9113

# An eye on each cheek as well as on the face. The reference only carries the
# pair on its 4-wide front rect, which is invisible from every angle except
# dead ahead - and a bird with no eye reads as a bath toy.
foreach ($cheek in @($chHead.Right, $chHead.Left)) {
    Set-Solid ($cheek[0] + 1) ($cheek[1] + 1) 2 2 $chickEye
    Set-Pixel ($cheek[0] + 1) ($cheek[1] + 1) $chickEyeRing
    Set-Pixel ($cheek[0] + 2) ($cheek[1] + 3) $plumeEdge
}

Set-Solid ($chHead.Top[0] + 1) $chHead.Top[1] 2 1 $combLo
Set-Pixel ($chHead.Top[0] + 1) ($chHead.Top[1] + 1) $combHi
Set-Pixel ($chHead.Top[0] + 2) ($chHead.Top[1] + 1) $combHi
Set-Pixel ($chHead.Top[0] + 2) ($chHead.Top[1] + 2) $combLo
Set-Solid ($chHead.Bottom[0] + 1) ($chHead.Bottom[1] + 1) 2 2 $combHi
Set-Pixel ($chHead.Bottom[0] + 1) ($chHead.Bottom[1] + 2) $combLo

# Face: crown feathers, an eye with a pale ring on each outer column, and the
# comb's front lobe over the brow.
$fx = $chHead.Front[0]; $fy = $chHead.Front[1]
Set-Solid $fx $fy 4 1 $plumeHi
Set-Pixel ($fx + 1) $fy $combHi
Set-Pixel ($fx + 2) $fy $combLo
Set-Solid $fx ($fy + 1) 4 1 $plumeMid
Set-Pixel $fx ($fy + 2) $chickEye
Set-Pixel ($fx + 1) ($fy + 2) $chickEyeRing
Set-Pixel ($fx + 2) ($fy + 2) $chickEyeRing
Set-Pixel ($fx + 3) ($fy + 2) $chickEye
Set-Solid $fx ($fy + 3) 4 1 $plumeSide
Set-Solid ($fx + 1) ($fy + 4) 2 2 $combLo
Set-Pixel ($fx + 1) ($fy + 4) $combHi

# Beak: amber, upper mandible lighter than the lower.
Set-Net -U 14 -V 0 -W 4 -H 2 -D 2 `
        -Top $beakHi -Bottom $beakLo -Front $beakHi -Back $beakLo -Side $beakHi
$chBeak = Get-NetRects -U 14 -V 0 -W 4 -H 2 -D 2
Set-Solid $chBeak.Front[0] ($chBeak.Front[1] + 1) 4 1 $beakLo

# Wattle box under the beak.
Set-Net -U 14 -V 4 -W 2 -H 2 -D 2 `
        -Top $combLo -Bottom $combLo -Front $combHi -Back $combLo -Side $combHi
$chWattle = Get-NetRects -U 14 -V 4 -W 2 -H 2 -D 2
Set-Solid $chWattle.Front[0] ($chWattle.Front[1] + 1) 2 1 $combLo

# Wing: the broad faces carry three stepped flight feathers.
Set-Net -U 24 -V 13 -W 1 -H 4 -D 6 `
        -Top $plumeHi -Bottom $plumeDeep -Front $plumeSide -Back $plumeLow -Side $plumeMid
$chWing = Get-NetRects -U 24 -V 13 -W 1 -H 4 -D 6
foreach ($face in @($chWing.Right, $chWing.Left)) {
    Set-RectGrain $face @($plumeMid, $plumeSide, $plumeWarm) @(0.56, 0.28, 0.16) 9115
    Set-Solid $face[0] ($face[1] + 2) $face[2] 1 $plumeSide
    Set-Solid $face[0] ($face[1] + 3) $face[2] 1 $plumeLow
    Set-Pixel $face[0] ($face[1] + 3) $plumeEdge
    Set-Pixel ($face[0] + 2) ($face[1] + 3) $plumeEdge
    Set-Pixel ($face[0] + 4) ($face[1] + 3) $plumeEdge
}

# Legs are planes rather than boxes on the current reference model: a one-texel
# shank and a three-toed foot cut out of a 3x3.
Set-Solid 36 3 1 5 $legHi
Set-Solid 36 5 1 1 $legMid
Set-Solid 36 6 1 2 $legLo
Set-Solid 32 0 3 3 $legHi
Set-Clear 32 0 1 1
Set-Clear 34 0 1 1
Set-Solid 32 2 3 1 $legMid
Set-Pixel 33 1 $legMid

# =========================================================================
# row 224 - cat. A ginger tabby: warm marmalade field, rust broken stripes,
# cream chin, chest and paws, green eyes, dusky rose nose. The reference is a
# chocolate tabby of 13 colours, so the layout is shared and not one tone is.
# Ginger rather than a grey tabby deliberately - a silver coat measures around
# 0.1 saturation against the reference's 0.41 and reads as drab beside terrain.
# =========================================================================
$script:rowBase = 224
$furHi = 'D89A5C'
$furMid = 'C68A4E'
$furLow = 'B27A42'
$furDeep = '9C6937'
$stripe = '7A4E28'
$stripeSoft = '8A5B30'
$creamHi = 'F0E3C8'
$creamLo = 'D8C8A8'
$catEye = '7FA94F'
$catPupil = '1E1B15'
$catNose = 'C97F78'
$catInner = 'AE7263'
$furWarm = 'CF9455'
$furFleck = 'A87340'

Set-Net -U 20 -V 0 -W 4 -H 16 -D 6 -Lying `
        -Top $furLow -Bottom $creamLo -Front $furMid -Back $furMid -Side $furMid
$ctBody = Get-NetRects -U 20 -V 0 -W 4 -H 16 -D 6 -Lying

Set-Ramp $ctBody.Top[0] $ctBody.Top[1] $ctBody.Top[2] $ctBody.Top[3] @($furDeep, $furLow, $furMid, $furLow, $furDeep) 'H'
Set-Ramp $ctBody.Bottom[0] $ctBody.Bottom[1] $ctBody.Bottom[2] $ctBody.Bottom[3] @($creamHi, $creamLo) 'R'
# Mackerel bars: short vertical dashes at uneven spacing, never a repeating comb.
foreach ($flank in @($ctBody.Right, $ctBody.Left)) {
    $fx0 = $flank[0]; $fy0 = $flank[1]
    Set-Ramp $fx0 $fy0 $flank[2] $flank[3] @($furHi, $furMid, $furLow) 'V'
    # Grain first, bars second: a stripe drawn over noise stays crisp, a stripe
    # grained afterwards dissolves into it.
    Set-RectGrain $flank @($furMid, $furWarm, $furLow, $furFleck) @(0.46, 0.24, 0.20, 0.10) 9201
    foreach ($bar in @(@(1, 0, 3), @(2, 4, 4), @(1, 9, 3), @(2, 12, 3), @(0, 6, 2))) {
        Set-Solid ($fx0 + $bar[0]) ($fy0 + $bar[1]) 1 $bar[2] $stripe
        Set-Solid ($fx0 + $bar[0] + 1) ($fy0 + $bar[1] + 1) 1 ([Math]::Max(1, $bar[2] - 1)) $stripeSoft
    }
    Set-Solid ($fx0 + $flank[2] - 1) $fy0 1 $flank[3] $creamLo
    Set-Solid ($fx0 + $flank[2] - 2) ($fy0 + 3) 1 8 $creamLo
}
Set-Ramp $ctBody.Front[0] $ctBody.Front[1] $ctBody.Front[2] $ctBody.Front[3] @($creamHi, $creamLo, $furMid) 'V'

Set-Net -U 0 -V 0 -W 5 -H 4 -D 5 `
        -Top $furMid -Bottom $creamLo -Front $furMid -Back $furLow -Side $furMid
$ctHead = Get-NetRects -U 0 -V 0 -W 5 -H 4 -D 5
Set-RectGrain $ctHead.Right @($furMid, $furWarm, $furLow) @(0.54, 0.28, 0.18) 9203
Set-RectGrain $ctHead.Left @($furMid, $furWarm, $furLow) @(0.54, 0.28, 0.18) 9205
Set-RectGrain $ctHead.Back @($furLow, $furMid, $furFleck) @(0.54, 0.26, 0.20) 9207
Set-RectGrain $ctHead.Top @($furMid, $furHi, $furLow) @(0.52, 0.28, 0.20) 9209

# The tabby's brow: an M of dark bars over the forehead, which reads as "tabby"
# more than the flank stripes do.
$hx = $ctHead.Top[0]; $hy = $ctHead.Top[1]
Set-Solid ($hx + 1) ($hy + 1) 1 3 $stripe
Set-Solid ($hx + 3) ($hy + 1) 1 3 $stripe
Set-Pixel ($hx + 2) ($hy + 2) $stripeSoft
Set-Solid $hx ($hy + 3) 1 2 $stripeSoft
Set-Solid ($hx + 4) ($hy + 3) 1 2 $stripeSoft

# Face: green eyes with dark pupils outboard, cream muzzle wedge.
$fx = $ctHead.Front[0]; $fy = $ctHead.Front[1]
Set-Solid $fx $fy 5 1 $furLow
Set-Pixel ($fx + 1) $fy $stripe
Set-Pixel ($fx + 3) $fy $stripe
Set-Pixel $fx ($fy + 1) $catPupil
Set-Pixel ($fx + 1) ($fy + 1) $catEye
Set-Pixel ($fx + 3) ($fy + 1) $catEye
Set-Pixel ($fx + 4) ($fy + 1) $catPupil
Set-Solid ($fx + 1) ($fy + 2) 3 2 $creamHi
Set-Pixel $fx ($fy + 2) $furMid
Set-Pixel ($fx + 4) ($fy + 2) $furMid
Set-Pixel $fx ($fy + 3) $creamLo
Set-Pixel ($fx + 4) ($fy + 3) $creamLo
Set-Pixel ($fx + 2) ($fy + 2) $catNose
foreach ($cheek in @($ctHead.Right, $ctHead.Left)) {
    Set-Ramp $cheek[0] $cheek[1] $cheek[2] $cheek[3] @($furHi, $furMid, $furLow) 'V'
    Set-Solid ($cheek[0] + 1) ($cheek[1] + 1) 1 2 $stripeSoft
    Set-Solid ($cheek[0] + 3) ($cheek[1] + 2) 1 2 $stripeSoft
}

foreach ($ear in @(0, 6)) {
    Set-Net -U $ear -V 10 -W 1 -H 1 -D 2 `
            -Top $furLow -Bottom $catInner -Front $catInner -Back $furLow -Side $furMid
}

# Muzzle: cream, rose nose, dark mouth. Its back face is buried in the head, so
# the band measures 7 wide rather than the 10 the formula gives.
Set-Net -U 0 -V 24 -W 3 -H 2 -D 2 `
        -Top $creamHi -Bottom $creamLo -Front $creamHi -Back $creamLo -Side $creamHi
Set-Clear 7 26 3 2
$ctNose = Get-NetRects -U 0 -V 24 -W 3 -H 2 -D 2
Set-Pixel ($ctNose.Front[0] + 1) $ctNose.Front[1] $catNose
Set-Pixel ($ctNose.Front[0] + 1) ($ctNose.Front[1] + 1) $catPupil
Set-Pixel $ctNose.Front[0] ($ctNose.Front[1] + 1) $creamLo
Set-Pixel ($ctNose.Front[0] + 2) ($ctNose.Front[1] + 1) $creamLo

# Legs: the front pair long, the rear pair short, both ending in cream socks.
Set-Net -U 40 -V 0 -W 2 -H 10 -D 2 `
        -Top $furMid -Bottom $creamHi -Front $furMid -Back $furLow -Side $furMid
$ctFront = Get-NetRects -U 40 -V 0 -W 2 -H 10 -D 2
foreach ($face in @($ctFront.Right, $ctFront.Front, $ctFront.Left, $ctFront.Back)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($furMid, $furLow) 'R'
    Set-Solid $face[0] ($face[1] + 8) $face[2] 2 $creamLo
    Set-Solid $face[0] ($face[1] + 9) $face[2] 1 $creamHi
    Set-Pixel $face[0] ($face[1] + 3) $stripeSoft
}
Set-Net -U 8 -V 13 -W 2 -H 6 -D 2 `
        -Top $furMid -Bottom $creamHi -Front $furMid -Back $furLow -Side $furMid
$ctHind = Get-NetRects -U 8 -V 13 -W 2 -H 6 -D 2
foreach ($face in @($ctHind.Right, $ctHind.Front, $ctHind.Left, $ctHind.Back)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($furMid, $furLow) 'R'
    Set-Solid $face[0] ($face[1] + 4) $face[2] 2 $creamLo
    Set-Solid $face[0] ($face[1] + 5) $face[2] 1 $creamHi
}

# Tail: two segments, ringed, the upper one ending dark.
foreach ($seg in @(@(0, 0), @(4, 1))) {
    $tu = $seg[0]
    Set-Net -U $tu -V 15 -W 1 -H 8 -D 1 `
            -Top $furLow -Bottom $furLow -Front $furMid -Back $furMid -Side $furMid
    $rects = Get-NetRects -U $tu -V 15 -W 1 -H 8 -D 1
    foreach ($face in @($rects.Right, $rects.Front, $rects.Left, $rects.Back)) {
        Set-Solid $face[0] ($face[1] + 1) $face[2] 1 $stripe
        Set-Solid $face[0] ($face[1] + 4) $face[2] 1 $stripe
        if ($seg[1] -eq 1) { Set-Solid $face[0] ($face[1] + 6) $face[2] 2 $stripe }
    }
}

# =========================================================================
# row 256 - camel. Six tones of cool dun instead of the reference's saturated
# ochre, with the same technique: no markings at all, every face modelled by a
# vignette that follows the limb, and dark toe caps.
# =========================================================================
$script:rowBase = 256
# Warm sand rather than the cool dun of the first pass, which measured 0.39
# saturation against a reference at 0.68 and looked bleached in daylight.
$dunHi = 'EACE8E'
$dun2 = 'DBBC79'
$dun3 = 'C8A765'
$dun4 = 'B39155'
$dun5 = '9E7C47'
$dunLo = '8A683A'
$dunLimb = @($dun2, $dun3, $dun4, $dun5, $dunLo)
$camelToe = '55504A'
$camelEye = '221E1A'
$camelLip = '8B7660'
$camelInner = '9C7A6C'
$dunWarm = Get-Mix $dunHi $dun2 0.5
$dunA = Get-Mix $dun2 $dun3 0.5
$dunAsh = Get-Mix $dun3 $dun4 0.5
$dunB = Get-Mix $dun4 $dun5 0.5
$dunC = Get-Mix $dun5 $dunLo 0.5

Set-Net -U 0 -V 25 -W 15 -H 12 -D 27 `
        -Top $dunHi -Bottom $dun5 -Front $dun3 -Back $dun4 -Side $dun3
$cmBody = Get-NetRects -U 0 -V 25 -W 15 -H 12 -D 27
Set-Ramp $cmBody.Top[0] $cmBody.Top[1] $cmBody.Top[2] $cmBody.Top[3] @($dun3, $dun2, $dunHi, $dun2, $dun3) 'H'
Set-RectGrain $cmBody.Top @($dun2, $dunWarm, $dunHi, $dunA, $dun3) @(0.26, 0.22, 0.20, 0.18, 0.14) 9301 0.45
foreach ($flank in @($cmBody.Right, $cmBody.Left)) {
    Set-Vignette $flank[0] $flank[1] $flank[2] $flank[3] @($dun2, $dun3, $dun4, $dun5)
    Set-Grain $flank[0] $flank[1] $flank[2] $flank[3] @($dun3, $dunA, $dunAsh, $dun2, $dun4) `
              @(0.26, 0.22, 0.20, 0.18, 0.14) 9303 0.40
    # Loose hair against the coat, heavier along the shoulder than the flank.
    Set-Hairs $flank[0] ($flank[1] + 2) $flank[2] ($flank[3] - 4) @($dun4, $dunB, $dun5, $dunWarm) 0.28 9305 3
}
Set-Ramp $cmBody.Bottom[0] $cmBody.Bottom[1] $cmBody.Bottom[2] $cmBody.Bottom[3] @($dun5, $dunLo) 'R'
Set-RectGrain $cmBody.Bottom @($dun5, $dunC, $dunLo, $dunB) @(0.30, 0.26, 0.24, 0.20) 9307 0.45

Set-Net -U 74 -V 0 -W 9 -H 5 -D 11 `
        -Top $dunHi -Bottom $dun5 -Front $dun2 -Back $dun3 -Side $dun2
# The hump's underside sits inside the back and the neck's inside the chest;
# the reference leaves both transparent and so do we, so a mapping mistake
# shows as a hole rather than as plausible-looking colour.
Set-Clear 94 0 9 11
$cmHump = Get-NetRects -U 74 -V 0 -W 9 -H 5 -D 11
Set-Vignette $cmHump.Top[0] $cmHump.Top[1] $cmHump.Top[2] $cmHump.Top[3] @($dunHi, $dun2, $dun3)
Set-RectGrain $cmHump.Top @($dun2, $dunWarm, $dunHi, $dunA) @(0.30, 0.26, 0.24, 0.20) 9309 0.45
Set-Hairs $cmHump.Top[0] $cmHump.Top[1] $cmHump.Top[2] $cmHump.Top[3] @($dun4, $dunB, $dun5) 0.30 9311 3
foreach ($face in @($cmHump.Right, $cmHump.Left, $cmHump.Front, $cmHump.Back)) {
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($dun2, $dunA, $dun3, $dunWarm) `
              @(0.32, 0.26, 0.22, 0.20) 9313 0.45
}

Set-Net -U 21 -V 0 -W 7 -H 14 -D 7 `
        -Top $dun2 -Bottom $dun4 -Front $dun2 -Back $dun4 -Side $dun3
Set-Clear 35 0 7 7
$cmNeck = Get-NetRects -U 21 -V 0 -W 7 -H 14 -D 7
Set-Ramp $cmNeck.Front[0] $cmNeck.Front[1] $cmNeck.Front[2] $cmNeck.Front[3] @($dunHi, $dun2, $dun3) 'R'
Set-RectGrain $cmNeck.Front @($dun2, $dunWarm, $dunA, $dun3) @(0.32, 0.26, 0.22, 0.20) 9315 0.45
foreach ($face in @($cmNeck.Right, $cmNeck.Left)) {
    Set-Vignette $face[0] $face[1] $face[2] $face[3] @($dun2, $dun3, $dun4)
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($dun3, $dunA, $dunAsh, $dun2) `
              @(0.30, 0.26, 0.24, 0.20) 9317 0.40
    Set-Hairs $face[0] ($face[1] + 1) $face[2] ($face[3] - 2) @($dun4, $dunB, $dun5) 0.34 9319 3
}

Set-Net -U 60 -V 24 -W 7 -H 8 -D 19 `
        -Top $dun2 -Bottom $dun5 -Front $dun3 -Back $dun4 -Side $dun3
$cmHead = Get-NetRects -U 60 -V 24 -W 7 -H 8 -D 19
foreach ($face in @($cmHead.Right, $cmHead.Left)) {
    Set-Vignette $face[0] $face[1] $face[2] $face[3] @($dun2, $dun3, $dun4)
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($dun3, $dunA, $dunAsh, $dun2) `
              @(0.30, 0.26, 0.24, 0.20) 9321 0.40
    Set-Solid ($face[0] + 2) ($face[1] + 1) 3 1 $dun4
    Set-Pixel ($face[0] + 3) ($face[1] + 2) $camelEye
    Set-Pixel ($face[0] + 4) ($face[1] + 2) $camelEye
}
Set-RectGrain $cmHead.Top @($dun2, $dunWarm, $dunA, $dun3) @(0.32, 0.26, 0.22, 0.20) 9323 0.45

# Muzzle: nostrils and a lip line. Its back face is unused, so the band is 17
# wide rather than the 22 the formula predicts.
Set-Net -U 50 -V 0 -W 5 -H 5 -D 6 `
        -Top $dun3 -Bottom $dunLo -Front $dun2 -Back $dun4 -Side $dun3
Set-Clear 67 6 5 5
$cmMuzzle = Get-NetRects -U 50 -V 0 -W 5 -H 5 -D 6
$mx = $cmMuzzle.Front[0]; $my = $cmMuzzle.Front[1]
Set-Ramp $mx $my 5 5 @($dun2, $dun3, $dun4) 'V'
Set-Pixel ($mx + 1) ($my + 1) $camelEye
Set-Pixel ($mx + 3) ($my + 1) $camelEye
Set-Solid ($mx + 1) ($my + 3) 3 1 $camelLip

foreach ($eu in @(45, 67)) {
    Set-Net -U $eu -V 0 -W 3 -H 1 -D 2 `
            -Top $dun3 -Bottom $camelInner -Front $camelInner -Back $dun4 -Side $dun3
}

# Four legs, each vignetted so the shin reads as round, with a knee band and two
# toe caps. Repeating one flat rectangle four times is what makes a model look
# stamped, so each gets its own speckle salt.
foreach ($leg in @(@(0, 0, 4101), @(0, 26, 4103), @(58, 16, 4107), @(94, 16, 4109))) {
    $lu = $leg[0]; $lv = $leg[1]
    Set-Net -U $lu -V $lv -W 5 -H 21 -D 5 `
            -Top $dun3 -Bottom $camelToe -Front $dun3 -Back $dun4 -Side $dun3
    $r = Get-NetRects -U $lu -V $lv -W 5 -H 21 -D 5
    foreach ($face in @($r.Right, $r.Front, $r.Left, $r.Back)) {
        # A vignette rather than a horizontal ramp: ramping only across the
        # width puts the same bar on all four faces and the leg reads as
        # corduroy.
        Set-Vignette $face[0] $face[1] $face[2] $face[3] $dunLimb
        Set-Grain $face[0] $face[1] $face[2] 9 @($dun3, $dunA, $dunAsh, $dun4) `
                  @(0.30, 0.26, 0.24, 0.20) ($leg[2] + 1) 0.35
        Set-Solid $face[0] ($face[1] + 9) $face[2] 1 $dun5
        Set-Vignette $face[0] ($face[1] + 10) $face[2] 6 @($dun4, $dun5, $dunLo)
        Set-Grain $face[0] ($face[1] + 10) $face[2] 6 @($dun4, $dunB, $dun5, $dunC) `
                  @(0.30, 0.26, 0.24, 0.20) ($leg[2] + 2) 0.35
        Set-Solid $face[0] ($face[1] + 16) $face[2] 3 $dun5
        Set-Solid $face[0] ($face[1] + 19) $face[2] 2 $camelToe
        Set-Pixel ($face[0] + 1) ($face[1] + 19) $dunLo
        if ($face[2] -ge 5) { Set-Pixel ($face[0] + 3) ($face[1] + 19) $dunLo }
    }
    Set-Speckle $r.Right[0] ($r.Right[1] + 2) $r.Right[2] 6 $dun5 0.10 $leg[2]
}

# Tail plane: a thin switch with a dark tuft.
Set-Solid 123 0 1 10 $dun4
Set-Solid 122 10 3 3 $camelLip
Set-Pixel 123 13 $camelLip

# =========================================================================
# The horse family. One net layout, three coats.
#   horse  - chestnut with a flaxen mane and tail, and a white blaze
#   mule   - seal brown with a mealy oatmeal muzzle and belly
#   donkey - ash grey with a cream muzzle and the dark shoulder cross
# The reference bay is 19..48 luminance with a black mane; none of these shares
# a tone with it.
# =========================================================================
function Build-Equine {
    param(
        [int]$Base,
        [string[]]$Coat,        # five tones, lightest first
        [string]$Mane,
        [string]$ManeLo,
        [string]$Muzzle,
        [string]$MuzzleLo,
        [string]$Hoof,
        [string]$EyeWhite,
        [string]$Pupil,
        [string]$Nostril,
        [switch]$LongEars,
        [string]$Cross = '',
        [string]$Blaze = '',
        [int]$Salt = 9000
    )
    $script:rowBase = $Base
    $hi = $Coat[0]; $c2 = $Coat[1]; $c3 = $Coat[2]; $c4 = $Coat[3]; $lo = $Coat[4]

    # Two steps between each pair of anchors rather than one. Five flat tones
    # read as painted card next to a reference that spends most of its 25-36
    # colours on values a single step apart.
    $q1 = Get-Mix $hi $c2 0.34; $m1 = Get-Mix $hi $c2 0.67
    $q2 = Get-Mix $c2 $c3 0.34; $m2 = Get-Mix $c2 $c3 0.67
    $q3 = Get-Mix $c3 $c4 0.34; $m3 = Get-Mix $c3 $c4 0.67
    $q4 = Get-Mix $c4 $lo 0.34; $m4 = Get-Mix $c4 $lo 0.67
    $maneMid = Get-Mix $Mane $ManeLo 0.5
    $maneWarm = Get-Mix $Mane $ManeLo 0.25

    # Body: light along the spine, deepest under the belly.
    Set-Net -U 0 -V 32 -W 10 -H 10 -D 22 `
            -Top $hi -Bottom $lo -Front $c3 -Back $c3 -Side $c2
    $body = Get-NetRects -U 0 -V 32 -W 10 -H 10 -D 22
    Set-Ramp $body.Top[0] $body.Top[1] $body.Top[2] $body.Top[3] @($c2, $hi, $c2) 'H'
    Set-RectGrain $body.Top @($q1, $m1, $hi, $c2, $q2) @(0.28, 0.22, 0.20, 0.18, 0.12) ($Salt + 20)
    foreach ($flank in @($body.Right, $body.Left)) {
        Set-Ramp $flank[0] $flank[1] $flank[2] $flank[3] @($c2, $c3, $c4) 'V'
        Set-Grain $flank[0] $flank[1] $flank[2] $flank[3] @($q2, $m2, $c3, $c2, $q3, $m1) `
                  @(0.24, 0.20, 0.18, 0.16, 0.12, 0.10) ($Salt + 21)
        # Coat hair: short strokes that vary in length and never share a column
        # baseline. A regular comb of dashes reads as corduroy.
        Set-Hairs $flank[0] ($flank[1] + 1) $flank[2] ($flank[3] - 2) @($m3, $c4, $q3) 0.34 ($Salt + 22) 3
        # Shoulder and haunch swell.
        Set-Solid ($flank[0] + 2) ($flank[1] + 2) 3 2 $m1
        Set-Solid ($flank[0] + 16) ($flank[1] + 2) 4 3 $m1
        Set-Grain ($flank[0] + 2) ($flank[1] + 2) 3 2 @($m1, $q1, $c2) @(0.44, 0.32, 0.24) ($Salt + 23)
        Set-Grain ($flank[0] + 16) ($flank[1] + 2) 4 3 @($m1, $q1, $c2) @(0.44, 0.32, 0.24) ($Salt + 24)
    }
    Set-Ramp $body.Bottom[0] $body.Bottom[1] $body.Bottom[2] $body.Bottom[3] @($c4, $lo) 'R'
    Set-RectGrain $body.Bottom @($q4, $m4, $c4, $lo) @(0.30, 0.26, 0.24, 0.20) ($Salt + 25)
    Set-RectGrain $body.Front @($c3, $q2, $m2, $q3) @(0.34, 0.24, 0.22, 0.20) ($Salt + 26)
    Set-RectGrain $body.Back @($c3, $m3, $q3, $c4) @(0.32, 0.26, 0.22, 0.20) ($Salt + 27)
    if ($Cross) {
        # A donkey's shoulder cross: a dorsal line down the spine and a bar
        # across the withers. Real markings, not decoration - so these stay
        # crisp, with only enough grain to sit in the coat rather than on it.
        $crossMid = Get-Mix $Cross $c4 0.4
        Set-Solid ($body.Top[0] + 4) $body.Top[1] 2 $body.Top[3] $Cross
        Set-Grain ($body.Top[0] + 4) $body.Top[1] 2 $body.Top[3] @($Cross, $crossMid) @(0.7, 0.3) ($Salt + 28) 0.5
        foreach ($flank in @($body.Right, $body.Left)) {
            Set-Solid ($flank[0] + 3) $flank[1] 1 4 $Cross
            Set-Grain ($flank[0] + 3) $flank[1] 1 4 @($Cross, $crossMid) @(0.7, 0.3) ($Salt + 29) 0.5
        }
    }
    if ($Muzzle -and -not $Cross) {
        # Mealy pale belly, which the mule carries. Drawn as broken coverage
        # rather than two rectangles: a square patch of a second colour reads as
        # a sticker, and the pixels grain skips keep the coat underneath, which
        # is what makes the edge ragged for free.
        $mealyMid = Get-Mix $Muzzle $MuzzleLo 0.5
        Set-Grain ($body.Bottom[0] + 2) ($body.Bottom[1] + 6) 6 10 @($MuzzleLo, $mealyMid) `
                  @(0.6, 0.4) ($Salt + 45) 0.80
        Set-Grain ($body.Bottom[0] + 3) ($body.Bottom[1] + 8) 4 6 @($Muzzle, $mealyMid) `
                  @(0.62, 0.38) ($Salt + 46) 0.88
    }

    # Neck, with the mane crest along its top.
    Set-Net -U 0 -V 35 -W 4 -H 12 -D 7 `
            -Top $Mane -Bottom $c4 -Front $c2 -Back $ManeLo -Side $c2
    $neck = Get-NetRects -U 0 -V 35 -W 4 -H 12 -D 7
    foreach ($face in @($neck.Right, $neck.Left)) {
        Set-Ramp $face[0] $face[1] $face[2] $face[3] @($c2, $c3) 'V'
        Set-Grain $face[0] $face[1] $face[2] $face[3] @($c2, $q2, $m2, $q1, $c3) `
                  @(0.28, 0.22, 0.20, 0.16, 0.14) ($Salt + 30)
        Set-Hairs $face[0] ($face[1] + 1) $face[2] ($face[3] - 1) @($m2, $q3, $c3) 0.36 ($Salt + 31) 3
        Set-Solid $face[0] $face[1] $face[2] 1 $ManeLo
        Set-Grain $face[0] $face[1] $face[2] 1 @($ManeLo, $maneMid, $maneWarm) @(0.46, 0.32, 0.22) ($Salt + 32)
    }
    Set-Ramp $neck.Front[0] $neck.Front[1] $neck.Front[2] $neck.Front[3] @($hi, $c2, $c3) 'R'
    Set-RectGrain $neck.Front @($c2, $q1, $m1, $q2) @(0.32, 0.26, 0.22, 0.20) ($Salt + 33)
    Set-RectGrain $neck.Top @($Mane, $maneWarm, $maneMid, $ManeLo) @(0.32, 0.26, 0.22, 0.20) ($Salt + 34)

    # Head: an eye on each side rect with a brow ridge above it.
    Set-Net -U 0 -V 13 -W 6 -H 5 -D 7 `
            -Top $c2 -Bottom $c4 -Front $c2 -Back $c3 -Side $c2
    $head = Get-NetRects -U 0 -V 13 -W 6 -H 5 -D 7
    foreach ($face in @($head.Right, $head.Left)) {
        Set-Ramp $face[0] $face[1] $face[2] $face[3] @($c2, $c3, $c4) 'V'
        Set-Grain $face[0] $face[1] $face[2] $face[3] @($c2, $q2, $m2, $q1) `
                  @(0.30, 0.26, 0.24, 0.20) ($Salt + 35)
        # Cheekbone and jaw: the two planes that stop a head reading as a slab.
        Set-Solid ($face[0] + 1) ($face[1] + 3) 4 1 $m3
        Set-Solid ($face[0] + 3) ($face[1] + 1) 3 1 $c4
        Set-Pixel ($face[0] + 4) ($face[1] + 2) $Pupil
        Set-Pixel ($face[0] + 5) ($face[1] + 2) $EyeWhite
    }
    Set-Solid $head.Top[0] $head.Top[1] $head.Top[2] 2 $ManeLo
    Set-Grain $head.Top[0] $head.Top[1] $head.Top[2] 2 @($ManeLo, $maneMid, $maneWarm) `
              @(0.46, 0.32, 0.22) ($Salt + 36)
    Set-Grain $head.Top[0] ($head.Top[1] + 2) $head.Top[2] ($head.Top[3] - 2) @($c2, $q1, $m1, $q2) `
              @(0.34, 0.26, 0.22, 0.18) ($Salt + 37)
    if ($Blaze) {
        # A blaze is a stripe of unpigmented hair, so its edges wander a texel
        # either side rather than ruling straight down the face.
        $blazeMid = Get-Mix $Blaze $c2 0.35
        Set-Solid ($head.Front[0] + 2) $head.Front[1] 2 $head.Front[3] $Blaze
        Set-Grain ($head.Front[0] + 2) $head.Front[1] 2 $head.Front[3] @($Blaze, $blazeMid) `
                  @(0.68, 0.32) ($Salt + 47) 0.55
        Set-Grain ($head.Front[0] + 1) ($head.Front[1] + 1) 1 3 @($blazeMid, $Blaze) `
                  @(0.6, 0.4) ($Salt + 48) 0.45
        Set-Grain ($head.Front[0] + 4) ($head.Front[1] + 2) 1 3 @($blazeMid, $Blaze) `
                  @(0.6, 0.4) ($Salt + 49) 0.40
    }

    # Muzzle: pale on the mule and donkey, coat-coloured on the horse, with
    # paired nostrils and a lower lip.
    $muzzleFace = if ($Muzzle) { $Muzzle } else { $c3 }
    $muzzleDeep = if ($Muzzle) { $MuzzleLo } else { $c4 }
    Set-Net -U 0 -V 25 -W 4 -H 5 -D 5 `
            -Top $muzzleDeep -Bottom $muzzleDeep -Front $muzzleFace -Back $c4 -Side $muzzleFace
    $mz = Get-NetRects -U 0 -V 25 -W 4 -H 5 -D 5
    $muzzleMid = Get-Mix $muzzleFace $muzzleDeep 0.5
    $muzzleWarm = Get-Mix $muzzleFace $muzzleDeep 0.25
    foreach ($face in @($mz.Right, $mz.Left)) {
        Set-Ramp $face[0] $face[1] $face[2] $face[3] @($muzzleFace, $muzzleDeep) 'V'
        Set-Grain $face[0] $face[1] $face[2] $face[3] @($muzzleMid, $muzzleWarm, $muzzleFace, $muzzleDeep) `
                  @(0.30, 0.26, 0.24, 0.20) ($Salt + 38)
    }
    $fx = $mz.Front[0]; $fy = $mz.Front[1]
    if ($Blaze) { Set-Solid ($fx + 1) $fy 2 2 $Blaze }
    Set-Pixel $fx ($fy + 2) $Nostril
    Set-Pixel ($fx + 3) ($fy + 2) $Nostril
    Set-Solid ($fx + 1) ($fy + 4) 2 1 $muzzleDeep

    # Ear. The horse's is short at (19,16); the long pair sits at (0,12).
    if ($LongEars) {
        Set-Net -U 0 -V 12 -W 2 -H 7 -D 1 `
                -Top $ManeLo -Bottom $c4 -Front $c3 -Back $ManeLo -Side $c2
        $ear = Get-NetRects -U 0 -V 12 -W 2 -H 7 -D 1
        Set-Solid $ear.Front[0] ($ear.Front[1] + 1) 2 4 $Nostril
        Set-Solid $ear.Back[0] $ear.Back[1] 2 2 $ManeLo
    } else {
        Set-Net -U 19 -V 16 -W 2 -H 3 -D 1 `
                -Top $ManeLo -Bottom $c4 -Front $c3 -Back $ManeLo -Side $c2
        $ear = Get-NetRects -U 19 -V 16 -W 2 -H 3 -D 1
        Set-Solid $ear.Front[0] ($ear.Front[1] + 1) 2 2 $Nostril
    }

    # Leg: rounded shank, a darker cannon, a pale coronet and a grey hoof.
    Set-Net -U 48 -V 21 -W 4 -H 11 -D 4 `
            -Top $c3 -Bottom $Hoof -Front $c2 -Back $c3 -Side $c2
    $leg = Get-NetRects -U 48 -V 21 -W 4 -H 11 -D 4
    foreach ($face in @($leg.Right, $leg.Front, $leg.Left, $leg.Back)) {
        Set-Ramp $face[0] $face[1] $face[2] $face[3] @($c2, $c3, $c4) 'R'
        Set-Grain $face[0] $face[1] $face[2] 5 @($c3, $q2, $m2, $c2, $q3) `
                  @(0.28, 0.22, 0.20, 0.16, 0.14) ($Salt + 39)
        Set-Solid $face[0] ($face[1] + 5) $face[2] 3 $c4
        Set-Grain $face[0] ($face[1] + 5) $face[2] 3 @($c4, $m3, $q4, $m4) `
                  @(0.32, 0.26, 0.22, 0.20) ($Salt + 40)
        Set-Solid $face[0] ($face[1] + 8) $face[2] 1 $lo
        Set-Solid $face[0] ($face[1] + 9) $face[2] 2 $Hoof
        Set-Pixel $face[0] ($face[1] + 9) $lo
    }

    # Mane and tail, strand-shaded rather than flat slabs.
    Set-Net -U 56 -V 36 -W 2 -H 16 -D 2 `
            -Top $Mane -Bottom $ManeLo -Front $Mane -Back $ManeLo -Side $Mane
    $maneRects = Get-NetRects -U 56 -V 36 -W 2 -H 16 -D 2
    foreach ($face in @($maneRects.Right, $maneRects.Front, $maneRects.Left, $maneRects.Back)) {
        Set-Grain $face[0] $face[1] $face[2] $face[3] @($Mane, $maneWarm, $maneMid, $ManeLo) `
                  @(0.32, 0.26, 0.22, 0.20) ($Salt + 41)
        Set-Hairs $face[0] $face[1] $face[2] $face[3] @($ManeLo, $maneMid) 0.55 ($Salt + 42) 4
    }

    Set-Net -U 42 -V 36 -W 3 -H 14 -D 4 `
            -Top $Mane -Bottom $ManeLo -Front $Mane -Back $ManeLo -Side $Mane
    Set-Clear 49 36 3 4
    $tailRects = Get-NetRects -U 42 -V 36 -W 3 -H 14 -D 4
    foreach ($face in @($tailRects.Right, $tailRects.Front, $tailRects.Left, $tailRects.Back)) {
        Set-Grain $face[0] $face[1] $face[2] $face[3] @($Mane, $maneWarm, $maneMid, $ManeLo) `
                  @(0.30, 0.26, 0.24, 0.20) ($Salt + 43)
        Set-Hairs $face[0] $face[1] $face[2] $face[3] @($ManeLo, $maneMid) 0.60 ($Salt + 44) 5
        Set-Solid $face[0] ($face[1] + 12) $face[2] 2 $ManeLo
        # A tail ends in strands, not a straight hem.
        Set-Fringe $face[0] ($face[1] + 12) $face[2] 2 ($Salt + 6)
    }
}

Build-Equine -Base 384 `
    -Coat @('A9663A', '995A32', '89502B', '774524', '653A1D') `
    -Mane 'D9BE87' -ManeLo 'B49965' `
    -Muzzle '' -MuzzleLo '' `
    -Hoof '4C4744' -EyeWhite 'F3EEE3' -Pupil '17120E' -Nostril '4A2E1C' `
    -Blaze 'EFE7D6' -Salt 9100

Build-Equine -Base 448 `
    -Coat @('6D5A4A', '604E40', '534336', '46382C', '3A2E24') `
    -Mane '271F18' -ManeLo '3A2F26' `
    -Muzzle 'CDBEA6' -MuzzleLo 'AB9C86' `
    -Hoof '46413D' -EyeWhite 'EDE6D8' -Pupil '15110E' -Nostril '35281F' `
    -LongEars -Salt 9200

Build-Equine -Base 576 `
    -Coat @('9E988F', '8F8980', '807A72', '716C64', '625E57') `
    -Mane '3C3831' -ManeLo '4E4941' `
    -Muzzle 'E5DCCC' -MuzzleLo 'C8BEAB' `
    -Hoof '4B4743' -EyeWhite 'EFE9DC' -Pupil '16120F' -Nostril '3A352F' `
    -LongEars -Cross '3C3831' -Salt 9300

# =========================================================================
# row 512 - llama. Cream fleece with a chocolate face, ears, saddle and
# stockings - a real llama marking, and the inverse of the reference's russet
# body with a cream mask, so nothing about it is theirs. A solid taupe measured
# 0.17 saturation and read as concrete; the contrast is what carries this one.
# =========================================================================
$script:rowBase = 512
# Warm cream rather than neutral. A fleece mixed toward grey measures around
# 0.15 saturation and reads as concrete beside terrain - the same failure that
# once shipped a washed-out pig. These carry enough yellow to stay wool.
$fleeceHi = 'F3E6C6'
$fleece2 = 'E4D3AA'
$fleece3 = 'CDB88C'
$fleeceLo = 'B39C72'
$saddle = '7A5E45'
$llCocoa = '6B5340'
$llCocoaLo = '55402F'
$llEye = '1E1712'
$llMuzzle = '8B6E55'
$fleeceWarm = Get-Mix $fleeceHi $fleece2 0.5
$fleeceMid = Get-Mix $fleece2 $fleece3 0.5
$fleeceDusk = Get-Mix $fleece3 $fleeceLo 0.5
$fleeceA = Get-Mix $fleeceHi $fleece2 0.25
$fleeceB = Get-Mix $fleece2 $fleece3 0.75
$fleeceC = Get-Mix $fleeceLo $saddle 0.35
$saddleLo = Get-Mix $saddle $llCocoaLo 0.5
$saddleWarm = Get-Mix $saddle $fleeceLo 0.3
$llCocoaMid = Get-Mix $llCocoa $llCocoaLo 0.5

Set-Net -U 29 -V 0 -W 12 -H 18 -D 10 -Lying `
        -Top $saddle -Bottom $fleece3 -Front $fleece2 -Back $fleece3 -Side $fleece2
$llBody = Get-NetRects -U 29 -V 0 -W 12 -H 18 -D 10 -Lying
Set-Ramp $llBody.Top[0] $llBody.Top[1] $llBody.Top[2] $llBody.Top[3] @($fleece3, $saddle, $fleece3) 'H'
Set-RectGrain $llBody.Top @($saddle, $saddleWarm, $saddleLo, $llCocoa, $fleeceC) @(0.28, 0.22, 0.20, 0.16, 0.14) 5211
Set-Hairs $llBody.Top[0] $llBody.Top[1] $llBody.Top[2] $llBody.Top[3] @($saddleLo, $llCocoa, $llCocoaMid) 0.30 5213 3
foreach ($flank in @($llBody.Right, $llBody.Left)) {
    Set-Ramp $flank[0] $flank[1] $flank[2] $flank[3] @($fleece2, $fleece3, $fleeceLo) 'V'
    Set-Grain $flank[0] $flank[1] $flank[2] $flank[3] @($fleece3, $fleeceMid, $fleece2, $fleeceB, $fleeceDusk) `
              @(0.26, 0.22, 0.20, 0.18, 0.14) 5215
    # Fleece clumps: uneven length and spacing. The first pass combed them on a
    # fixed modulo and the wool came out as corduroy.
    Set-Hairs $flank[0] ($flank[1] + 1) $flank[2] 8 @($fleeceWarm, $fleeceHi, $fleeceA) 0.34 5201 4
    Set-Hairs $flank[0] ($flank[1] + 8) $flank[2] 9 @($fleeceLo, $fleeceDusk, $fleeceC) 0.40 5203 4
}
Set-Ramp $llBody.Bottom[0] $llBody.Bottom[1] $llBody.Bottom[2] $llBody.Bottom[3] @($fleece3, $fleeceLo) 'R'
Set-RectGrain $llBody.Bottom @($fleeceLo, $fleeceDusk, $fleece3, $fleeceC) @(0.30, 0.26, 0.24, 0.20) 5217
Set-RectGrain $llBody.Front @($fleece2, $fleeceMid, $fleeceWarm, $fleeceA) @(0.30, 0.26, 0.24, 0.20) 5219
Set-RectGrain $llBody.Back @($fleece3, $fleeceMid, $fleeceDusk, $fleeceB) @(0.30, 0.26, 0.24, 0.20) 5221

Set-Net -U 0 -V 14 -W 8 -H 18 -D 6 `
        -Top $fleece2 -Bottom $fleece3 -Front $fleeceHi -Back $fleece3 -Side $fleece2
$llNeck = Get-NetRects -U 0 -V 14 -W 8 -H 18 -D 6
Set-Ramp $llNeck.Front[0] $llNeck.Front[1] $llNeck.Front[2] $llNeck.Front[3] @($fleeceHi, $fleece2, $fleece3) 'V'
foreach ($face in @($llNeck.Right, $llNeck.Left)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($fleeceHi, $fleece2, $fleece3) 'V'
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($fleece2, $fleeceWarm, $fleece3, $fleeceMid, $fleeceA) `
              @(0.26, 0.22, 0.20, 0.18, 0.14) 5223
    Set-Hairs $face[0] ($face[1] + 3) $face[2] ($face[3] - 4) @($fleeceLo, $fleeceDusk, $fleeceC) 0.38 5205 4
}
Set-RectGrain $llNeck.Back @($fleece3, $fleeceMid, $fleeceDusk, $fleeceB) @(0.30, 0.26, 0.24, 0.20) 5225
Set-RectGrain $llNeck.Top @($fleece2, $fleeceMid, $fleeceWarm, $fleeceA) @(0.30, 0.26, 0.24, 0.20) 5227
# The chocolate mask runs up the throat and over the poll, which is what makes
# the head read from a distance.
Set-Solid $llNeck.Front[0] $llNeck.Front[1] $llNeck.Front[2] 5 $llCocoa
Set-Solid ($llNeck.Front[0] + 1) ($llNeck.Front[1] + 5) ($llNeck.Front[2] - 2) 2 $saddle

# Head: chocolate mask, dark eyes, pale lower muzzle.
Set-Net -U 0 -V 0 -W 4 -H 4 -D 9 `
        -Top $llCocoaLo -Bottom $llMuzzle -Front $llCocoa -Back $fleece3 -Side $llCocoa
$llHead = Get-NetRects -U 0 -V 0 -W 4 -H 4 -D 9
$hx = $llHead.Front[0]; $hy = $llHead.Front[1]
Set-Ramp $hx $hy 4 4 @($llCocoa, $llCocoaLo) 'V'
Set-Grain $hx $hy 4 4 @($llCocoa, $llCocoaMid, $saddleLo, $llCocoaLo) @(0.32, 0.26, 0.22, 0.20) 5229
Set-Pixel $hx ($hy + 1) $llEye
Set-Pixel ($hx + 3) ($hy + 1) $llEye
Set-Solid ($hx + 1) ($hy + 2) 2 2 $llMuzzle
Set-Solid ($hx + 1) ($hy + 3) 2 1 $fleece3
foreach ($face in @($llHead.Right, $llHead.Left)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($fleece2, $saddle, $llCocoa) 'H'
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($saddle, $saddleWarm, $saddleLo, $fleeceC) `
              @(0.30, 0.26, 0.24, 0.20) 5231
    Set-Pixel ($face[0] + 7) ($face[1] + 1) $llEye
}

Set-Net -U 17 -V 0 -W 3 -H 3 -D 2 `
        -Top $llCocoa -Bottom $llMuzzle -Front $llCocoaLo -Back $llCocoa -Side $llCocoa
$llEar = Get-NetRects -U 17 -V 0 -W 3 -H 3 -D 2
Set-Solid ($llEar.Front[0] + 1) $llEar.Front[1] 1 3 $llMuzzle

# Legs: fleece above, chocolate stockings below.
Set-Net -U 29 -V 29 -W 4 -H 14 -D 4 `
        -Top $fleece2 -Bottom $llCocoaLo -Front $fleece2 -Back $fleece3 -Side $fleece2
$llLeg = Get-NetRects -U 29 -V 29 -W 4 -H 14 -D 4
foreach ($face in @($llLeg.Right, $llLeg.Front, $llLeg.Left, $llLeg.Back)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($fleeceHi, $fleece2, $fleece3) 'R'
    Set-Grain $face[0] $face[1] $face[2] 9 @($fleece2, $fleeceWarm, $fleece3, $fleeceA) `
              @(0.30, 0.26, 0.24, 0.20) 5233
    Set-Hairs $face[0] $face[1] $face[2] 8 @($fleeceLo, $fleeceDusk, $fleeceB) 0.34 5207 3
    Set-Solid $face[0] ($face[1] + 9) $face[2] 2 $saddle
    Set-Solid $face[0] ($face[1] + 11) $face[2] 3 $llCocoa
    Set-Grain $face[0] ($face[1] + 9) $face[2] 4 @($saddle, $saddleLo, $llCocoa, $llCocoaMid) `
              @(0.28, 0.24, 0.26, 0.22) 5235
    Set-Solid $face[0] ($face[1] + 13) $face[2] 1 $llCocoaLo
}

# =========================================================================
# row 640 - goat. Oatmeal fleece with a cocoa dorsal band and belly, charcoal
# ridged horns, dark hooves and a tan muzzle. The reference is near-white at 15
# colours; the shaggy coat and the hanging fringe are technique, the tones are
# not.
# =========================================================================
$script:rowBase = 640
$woolHi = 'E4DAC4'
$wool2 = 'D4C8AF'
$wool3 = 'C1B499'
$woolLo = 'AC9E82'
$cocoa = '7C6852'
$cocoaLo = '65543F'
$hornHi = '6F6B63'
$hornLo = '4F4C46'
$goatHoof = '39352F'
$goatMuzzle = 'C2B095'
$goatEye = '1A1715'
$woolWarm = Get-Mix $woolHi $wool2 0.5
$woolMid = Get-Mix $wool2 $wool3 0.5
$woolDusk = Get-Mix $wool3 $woolLo 0.5
$cocoaMid = Get-Mix $cocoa $cocoaLo 0.5

Set-Net -U 1 -V 1 -W 9 -H 11 -D 16 `
        -Top $woolHi -Bottom $cocoa -Front $wool2 -Back $wool3 -Side $wool2
$gtBody = Get-NetRects -U 1 -V 1 -W 9 -H 11 -D 16
Set-Ramp $gtBody.Top[0] $gtBody.Top[1] $gtBody.Top[2] $gtBody.Top[3] @($wool2, $woolHi, $wool2) 'H'
Set-RectGrain $gtBody.Top @($woolHi, $woolWarm, $wool2, $woolMid) @(0.34, 0.26, 0.24, 0.16) 6411
Set-Hairs $gtBody.Top[0] $gtBody.Top[1] $gtBody.Top[2] $gtBody.Top[3] @($woolMid, $wool3) 0.32 6413 3
Set-Solid ($gtBody.Top[0] + 4) $gtBody.Top[1] 1 $gtBody.Top[3] $wool3
Set-Ramp $gtBody.Bottom[0] $gtBody.Bottom[1] $gtBody.Bottom[2] $gtBody.Bottom[3] @($cocoa, $cocoaLo) 'R'
Set-RectGrain $gtBody.Bottom @($cocoa, $cocoaMid, $cocoaLo) @(0.42, 0.32, 0.26) 6415
foreach ($flank in @($gtBody.Right, $gtBody.Left)) {
    Set-Ramp $flank[0] $flank[1] $flank[2] $flank[3] @($woolHi, $wool2, $wool3) 'V'
    Set-Grain $flank[0] $flank[1] $flank[2] $flank[3] @($wool2, $woolWarm, $woolMid, $wool3) `
              @(0.32, 0.26, 0.24, 0.18) 6417
    # Shaggy coat. Every stroke rolls its own column, start row and length: a
    # fixed modulo puts them all on one baseline and the goat comes out combed.
    Set-Hairs $flank[0] ($flank[1] + 1) $flank[2] ($flank[3] - 2) @($woolDusk, $woolLo) 0.42 6401 4
    Set-Hairs $flank[0] ($flank[1] + 4) $flank[2] ($flank[3] - 5) @($cocoa, $cocoaMid) 0.16 6403 3
}
Set-RectGrain $gtBody.Front @($wool2, $woolMid, $woolWarm) @(0.46, 0.30, 0.24) 6419
Set-RectGrain $gtBody.Back @($wool3, $woolMid, $woolDusk) @(0.44, 0.32, 0.24) 6421

# Neck ruff: its bottom rect is buried in the body and stays transparent, and
# the side rects hang in strands.
Set-Net -U 0 -V 28 -W 11 -H 14 -D 11 `
        -Top $woolHi -Bottom $wool3 -Front $wool2 -Back $cocoa -Side $wool2
Set-Clear 22 28 11 11
$gtRuff = Get-NetRects -U 0 -V 28 -W 11 -H 14 -D 11
foreach ($face in @($gtRuff.Right, $gtRuff.Front, $gtRuff.Left)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($woolHi, $wool2, $wool3, $woolLo) 'V'
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($wool2, $woolWarm, $woolMid, $wool3) `
              @(0.32, 0.26, 0.24, 0.18) 6423
    Set-Hairs $face[0] ($face[1] + 2) $face[2] 7 @($woolMid, $wool3) 0.46 6405 4
    Set-Hairs $face[0] ($face[1] + 6) $face[2] 7 @($woolDusk, $woolLo) 0.44 6407 5
    Set-Fringe $face[0] ($face[1] + 9) $face[2] 5 6409
}
Set-Ramp $gtRuff.Top[0] $gtRuff.Top[1] $gtRuff.Top[2] $gtRuff.Top[3] @($wool2, $woolHi, $wool2) 'R'
Set-RectGrain $gtRuff.Top @($woolHi, $woolWarm, $wool2) @(0.44, 0.32, 0.24) 6425
# The ruff's back rect is buried in the body, but a dead flat slab still shows
# on the one edge that clears the shoulder.
Set-RectGrain $gtRuff.Back @($cocoa, $cocoaMid, $cocoaLo) @(0.42, 0.32, 0.26) 6431

Set-Net -U 34 -V 46 -W 5 -H 8 -D 10 `
        -Top $wool2 -Bottom $goatMuzzle -Front $goatMuzzle -Back $wool3 -Side $wool2
$gtHead = Get-NetRects -U 34 -V 46 -W 5 -H 8 -D 10
foreach ($face in @($gtHead.Right, $gtHead.Left)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($woolHi, $wool2, $wool3) 'V'
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($wool2, $woolWarm, $woolMid) @(0.46, 0.30, 0.24) 6427
    Set-Solid ($face[0] + 6) ($face[1] + 4) 4 4 $goatMuzzle
    Set-Solid ($face[0] + 2) ($face[1] + 1) 3 1 $woolLo
    Set-Pixel ($face[0] + 3) ($face[1] + 2) $goatEye
}
$gx = $gtHead.Front[0]; $gy = $gtHead.Front[1]
Set-Ramp $gx $gy 5 8 @($wool2, $goatMuzzle) 'V'
Set-Pixel ($gx + 1) ($gy + 5) $cocoaLo
Set-Pixel ($gx + 3) ($gy + 5) $cocoaLo
Set-Solid ($gx + 1) ($gy + 7) 3 1 $cocoa

# Horns: charcoal with growth ridges, which is what makes them read as horn
# rather than as grey sticks.
Set-Net -U 12 -V 55 -W 2 -H 7 -D 2 `
        -Top $hornHi -Bottom $hornLo -Front $hornHi -Back $hornLo -Side $hornHi
$gtHorn = Get-NetRects -U 12 -V 55 -W 2 -H 7 -D 2
foreach ($face in @($gtHorn.Right, $gtHorn.Front, $gtHorn.Left, $gtHorn.Back)) {
    Set-Solid $face[0] ($face[1] + 1) $face[2] 1 $hornLo
    Set-Solid $face[0] ($face[1] + 3) $face[2] 1 $hornLo
    Set-Solid $face[0] ($face[1] + 5) $face[2] 1 $hornLo
    Set-Solid $face[0] ($face[1] + 6) $face[2] 1 $goatHoof
}

Set-Net -U 2 -V 61 -W 3 -H 2 -D 1 `
        -Top $wool2 -Bottom $goatMuzzle -Front $goatMuzzle -Back $wool3 -Side $wool2

# Beard: a tuft whose own top and bottom rects are unused, cut into strands.
Set-Net -U 23 -V 56 -W 4 -H 7 -D 1 `
        -Top $wool2 -Bottom $wool2 -Front $woolHi -Back $wool2 -Side $wool2
Set-Clear 24 56 4 1
Set-Clear 28 56 4 1
$gtBeard = Get-NetRects -U 23 -V 56 -W 4 -H 7 -D 1
foreach ($face in @($gtBeard.Right, $gtBeard.Front, $gtBeard.Left, $gtBeard.Back)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($woolHi, $wool2, $wool3) 'V'
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($wool2, $woolWarm, $woolMid) @(0.44, 0.32, 0.24) 6429
    Set-Fringe $face[0] ($face[1] + 3) $face[2] 4 6411
}

# Legs. The front pair is ten texels, the hind six, each on its own net, and
# every one ends in a dark cloven hoof.
foreach ($leg in @(@(35, 2, 10, 6421), @(49, 2, 10, 6423), @(36, 29, 6, 6425), @(49, 29, 6, 6427))) {
    $lu = $leg[0]; $lv = $leg[1]; $lh = $leg[2]
    Set-Net -U $lu -V $lv -W 3 -H $lh -D 3 `
            -Top $woolLo -Bottom $goatHoof -Front $wool2 -Back $wool3 -Side $wool2
    $r = Get-NetRects -U $lu -V $lv -W 3 -H $lh -D 3
    foreach ($face in @($r.Right, $r.Front, $r.Left, $r.Back)) {
        Set-Ramp $face[0] $face[1] $face[2] $face[3] @($woolHi, $wool2, $wool3) 'R'
        Set-Grain $face[0] $face[1] $face[2] ($lh - 3) @($wool2, $woolWarm, $woolMid) `
                  @(0.44, 0.32, 0.24) ($leg[3] + 1)
        Set-Hairs $face[0] $face[1] $face[2] ($lh - 3) @($woolDusk, $woolLo) 0.34 $leg[3] 3
        Set-Solid $face[0] ($face[1] + $lh - 3) $face[2] 1 $cocoa
        Set-Solid $face[0] ($face[1] + $lh - 2) $face[2] 2 $goatHoof
        Set-Pixel ($face[0] + 1) ($face[1] + $lh - 1) $hornLo
    }
}

# =========================================================================
# row 704 - rabbit. Russet back grading to a sand belly, dark ear rims, an
# amber eye and a rose nose. The reference is a flat mid-tan of 11 colours.
# =========================================================================
$script:rowBase = 704
$rustHi = 'AF7845'
$rust2 = '9E6B3C'
$rust3 = '8C5E34'
$rustLo = '78502C'
$rustDeep = '5C3C20'
$sandHi = 'DFCCAB'
$sandLo = 'C3AE8E'
$rbEye = 'C08C3C'
$rbPupil = '1B1510'
$rbNose = 'B67A80'
$rbInner = '8A5A50'
$rustWarm = Get-Mix $rustHi $rust2 0.5
$rustMid = Get-Mix $rust2 $rust3 0.5
$rustDusk = Get-Mix $rust3 $rustLo 0.5
$sandMid = Get-Mix $sandHi $sandLo 0.5

Set-Net -U 0 -V 0 -W 8 -H 6 -D 10 `
        -Top $rustHi -Bottom $sandLo -Front $rust2 -Back $rust3 -Side $rust2
$rbBody = Get-NetRects -U 0 -V 0 -W 8 -H 6 -D 10
Set-Ramp $rbBody.Top[0] $rbBody.Top[1] $rbBody.Top[2] $rbBody.Top[3] @($rust2, $rustHi, $rust2) 'H'
# The reference's coat is mottled almost texel by texel; a ramp plus a sprinkle
# of one accent leaves a flat band down the spine.
Set-RectGrain $rbBody.Top @($rustHi, $rustWarm, $rust2, $rustMid) @(0.32, 0.26, 0.24, 0.18) 7101
Set-Ramp $rbBody.Bottom[0] $rbBody.Bottom[1] $rbBody.Bottom[2] $rbBody.Bottom[3] @($sandHi, $sandLo) 'R'
Set-RectGrain $rbBody.Bottom @($sandMid, $sandHi, $sandLo) @(0.40, 0.32, 0.28) 7105
foreach ($flank in @($rbBody.Right, $rbBody.Left)) {
    Set-Ramp $flank[0] $flank[1] $flank[2] $flank[3] @($rust2, $rust3, $rustLo) 'V'
    Set-Grain $flank[0] $flank[1] $flank[2] $flank[3] @($rust2, $rustMid, $rust3, $rustWarm) `
              @(0.32, 0.26, 0.24, 0.18) 7103
    Set-Hairs $flank[0] ($flank[1] + 1) $flank[2] ($flank[3] - 2) @($rustDusk, $rustLo) 0.30 7107 3
    Set-Solid $flank[0] ($flank[1] + $flank[3] - 1) $flank[2] 1 $sandLo
}
Set-Ramp $rbBody.Back[0] $rbBody.Back[1] $rbBody.Back[2] $rbBody.Back[3] @($rust3, $rustLo) 'V'
Set-RectGrain $rbBody.Back @($rust3, $rustDusk, $rustMid) @(0.44, 0.32, 0.24) 7109
Set-RectGrain $rbBody.Front @($rust2, $rustMid, $rustWarm) @(0.46, 0.30, 0.24) 7111
Set-Solid ($rbBody.Back[0] + 2) ($rbBody.Back[1] + 3) 4 3 $sandHi
Set-Grain ($rbBody.Back[0] + 2) ($rbBody.Back[1] + 3) 4 3 @($sandHi, $sandMid) @(0.6, 0.4) 7113

Set-Net -U 0 -V 16 -W 5 -H 5 -D 5 `
        -Top $rust2 -Bottom $sandHi -Front $rust2 -Back $rust3 -Side $rust2
$rbHead = Get-NetRects -U 0 -V 16 -W 5 -H 5 -D 5
foreach ($face in @($rbHead.Right, $rbHead.Left)) {
    Set-Ramp $face[0] $face[1] $face[2] $face[3] @($rustHi, $rust2, $rust3) 'V'
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($rust2, $rustWarm, $rustMid) @(0.44, 0.32, 0.24) 7115
    Set-Solid ($face[0] + 3) ($face[1] + 3) 2 2 $sandLo
}
Set-RectGrain $rbHead.Top @($rust2, $rustWarm, $rustMid) @(0.44, 0.32, 0.24) 7117
Set-RectGrain $rbHead.Back @($rust3, $rustMid, $rustDusk) @(0.44, 0.32, 0.24) 7119
$hx = $rbHead.Front[0]; $hy = $rbHead.Front[1]
# Face, following the reference's own arrangement. The outer columns stay coat
# coloured, each eye is a two-row dark block, and the nose is ONE pink texel
# above a clean pale muzzle. Splitting that muzzle with a dark pixel leaves a
# pale block either side of it, which reads as a pair of fangs.
Set-Ramp $hx $hy 5 5 @($rust2, $rust3) 'V'
Set-Solid ($hx + 1) ($hy + 1) 1 2 $rbPupil
Set-Solid ($hx + 3) ($hy + 1) 1 2 $rbPupil
Set-Pixel ($hx + 1) ($hy + 1) $rbEye
Set-Pixel ($hx + 3) ($hy + 1) $rbEye
Set-Solid ($hx + 1) ($hy + 3) 3 1 $sandLo
Set-Pixel ($hx + 2) ($hy + 3) $rbNose
Set-Solid ($hx + 1) ($hy + 4) 3 1 $sandHi
Set-Pixel ($hx + 1) ($hy + 4) $sandLo
Set-Pixel ($hx + 3) ($hy + 4) $sandLo

foreach ($eu in @(26, 32)) {
    Set-Net -U $eu -V 0 -W 2 -H 5 -D 1 `
            -Top $rustDeep -Bottom $rbInner -Front $rust3 -Back $rust3 -Side $rust3
    $ear = Get-NetRects -U $eu -V 0 -W 2 -H 5 -D 1
    # Coat outside, a pink channel down the middle, dark at the tip. A ramp of
    # pale bands across the whole face made the pair read as a helmet.
    Set-Ramp $ear.Front[0] $ear.Front[1] 2 5 @($rust3, $rust2) 'V'
    Set-Solid $ear.Front[0] ($ear.Front[1] + 1) 2 3 $rbInner
    Set-Pixel $ear.Front[0] ($ear.Front[1] + 4) $rust2
    Set-Solid $ear.Front[0] $ear.Front[1] 2 1 $rustDeep
    Set-Solid $ear.Back[0] $ear.Back[1] 2 2 $rustDeep
}

Set-Net -U 20 -V 16 -W 4 -H 4 -D 4 `
        -Top $rustHi -Bottom $sandLo -Front $rust2 -Back $rust3 -Side $rust2
$rbHaunch = Get-NetRects -U 20 -V 16 -W 4 -H 4 -D 4
foreach ($face in @($rbHaunch.Right, $rbHaunch.Left)) {
    Set-Vignette $face[0] $face[1] $face[2] $face[3] @($rustHi, $rust2, $rust3)
    Set-Grain $face[0] $face[1] $face[2] $face[3] @($rust2, $rustWarm, $rustMid) @(0.44, 0.32, 0.24) 7121
}
Set-RectGrain $rbHaunch.Top @($rustHi, $rustWarm, $rust2) @(0.42, 0.32, 0.26) 7123

foreach ($lu in @(36, 44)) {
    Set-Net -U $lu -V 18 -W 2 -H 4 -D 2 `
            -Top $rust3 -Bottom $rustDeep -Front $rust2 -Back $rust3 -Side $rust2
    $r = Get-NetRects -U $lu -V 18 -W 2 -H 4 -D 2
    foreach ($face in @($r.Right, $r.Front, $r.Left, $r.Back)) {
        Set-Ramp $face[0] $face[1] $face[2] $face[3] @($rust2, $rust3, $rustLo) 'V'
        Set-Solid $face[0] ($face[1] + 3) $face[2] 1 $rustDeep
    }
}

foreach ($fu in @(20, 36)) {
    Set-Net -U $fu -V 24 -W 2 -H 1 -D 6 `
            -Top $sandHi -Bottom $rustDeep -Front $rust3 -Back $rust3 -Side $rust2
    $r = Get-NetRects -U $fu -V 24 -W 2 -H 1 -D 6
    Set-Ramp $r.Top[0] $r.Top[1] $r.Top[2] $r.Top[3] @($sandHi, $sandLo) 'V'
    Set-Solid $r.Bottom[0] ($r.Bottom[1] + 4) $r.Bottom[2] 2 $rbInner
}

# --- save -----------------------------------------------------------------

$script:rowBase = 0
$sheet.Save($resolved, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "wrote rows 192-767 of $resolved"
