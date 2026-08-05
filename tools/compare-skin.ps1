# Compares our creature skins against the reference art they were measured from.
#
# The goal is a texture that is *structurally* similar - same anatomy, same
# distribution of markings, same discipline - while being *pixel-wise* original.
# So this checks both directions:
#
#   things that should be HIGH        things that should be LOW
#   ---------------------------       -------------------------
#   net footprint agreement           exact colour overlap
#   block-luminance correlation
#   light/dark mask agreement
#   patch-size ratio
#
# The patch-size check is the one that earns its keep. A first attempt at the
# cow rolled a 3x3 cell grid independently, which gave scattered squares that
# read as static rather than hide - "this looks like a cyborg". Mean connected
# patch size was about a third of the reference's, and this would have said so
# before it ever reached the screen.
#
#   powershell -NoProfile -File tools\compare-skin.ps1
#   powershell -NoProfile -File tools\compare-skin.ps1 -Only Cow

param(
    [string]$Only = "",
    [string]$Ours = "assets\textures\creatures.png"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$refRoot = "reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures\entity"

# Each creature's region in our sheet, and the reference it was measured from.
# Rows must agree with the offsets in Creature.cpp.
$targets = @(
    @{ Name = 'Sheep';       Row = 0;  Height = 32; Ref = "$refRoot\sheep\sheep.png" }
    @{ Name = 'SheepFleece'; Row = 32; Height = 32; Ref = "$refRoot\sheep\sheep_wool.png" }
    @{ Name = 'Bramble';     Row = 64; Height = 32; Ref = "$refRoot\creeper\creeper.png" }
    @{ Name = 'Pig';         Row = 160; Height = 32; Ref = "$refRoot\pig\pig_temperate.png" }
    @{ Name = 'Cow';         Row = 96; Height = 64; Ref = "$refRoot\cow\cow_temperate.png" }
    @{ Name = 'Chicken';     Row = 192; Height = 32; Ref = "$refRoot\chicken\chicken_temperate.png" }
    @{ Name = 'Cat';         Row = 224; Height = 32; Ref = "$refRoot\cat\cat_tabby.png" }
    @{ Name = 'Camel';       Row = 256; Height = 128; Ref = "$refRoot\camel\camel.png" }
    @{ Name = 'Horse';       Row = 384; Height = 64; Ref = "$refRoot\horse\horse_brown.png" }
    @{ Name = 'Mule';        Row = 448; Height = 64; Ref = "$refRoot\horse\mule.png" }
    @{ Name = 'Llama';       Row = 512; Height = 64; Ref = "$refRoot\llama\llama_brown.png" }
    @{ Name = 'Donkey';      Row = 576; Height = 64; Ref = "$refRoot\horse\donkey.png" }
    @{ Name = 'Goat';        Row = 640; Height = 64; Ref = "$refRoot\goat\goat.png" }
    @{ Name = 'Rabbit';      Row = 704; Height = 64; Ref = "$refRoot\rabbit\rabbit_brown.png" }
)

function Get-Luminance {
    param($Pixel)
    return 0.299 * $Pixel.R + 0.587 * $Pixel.G + 0.114 * $Pixel.B
}

# How far a colour is from grey, 0 to 1. This is what "washed out" means: a
# muted pink and a bright coral can share a luminance exactly while looking
# nothing alike, so measuring brightness alone misses it entirely.
function Get-Saturation {
    param($Pixel)
    $mx = [Math]::Max($Pixel.R, [Math]::Max($Pixel.G, $Pixel.B))
    $mn = [Math]::Min($Pixel.R, [Math]::Min($Pixel.G, $Pixel.B))
    if ($mx -le 0) { return 0.0 }
    return ($mx - $mn) / [double]$mx
}

# The luminance split that best separates light markings from dark hide.
#
# Otsu's method: pick the threshold maximising variance *between* the two sides.
# A median split only works when they are evenly balanced - on our cow the
# markings cover about a third, so the median lands inside the hide and carves
# it up by its own faint tone variation, reporting dozens of phantom patches
# that are not visible in the texture at all.
function Get-OtsuThreshold {
    param([double[]]$Values)
    $hist = New-Object 'int[]' 256
    foreach ($v in $Values) {
        $hist[[int][Math]::Max(0, [Math]::Min(255, [Math]::Round($v)))]++
    }
    $total = $Values.Count
    $sum = 0.0
    for ($i = 0; $i -lt 256; $i++) { $sum += $i * $hist[$i] }
    $sumB = 0.0
    $wB = 0
    $best = -1.0
    $bestT = 128
    for ($t = 0; $t -lt 256; $t++) {
        $wB += $hist[$t]
        if ($wB -eq 0) { continue }
        $wF = $total - $wB
        if ($wF -eq 0) { break }
        $sumB += $t * $hist[$t]
        $mB = $sumB / $wB
        $mF = ($sum - $sumB) / $wF
        $between = [double]$wB * [double]$wF * ($mB - $mF) * ($mB - $mF)
        if ($between -gt $best) { $best = $between; $bestT = $t }
    }
    return [double]$bestT
}

# Number and mean size of connected light regions. Flood fill, four-connected -
# diagonal joins would merge separate speckles and hide the very failure this is
# looking for.
function Get-PatchStats {
    param([bool[]]$Mask, [int]$W, [int]$H)
    $seen = New-Object 'bool[]' ($W * $H)
    $sizes = @()
    for ($y = 0; $y -lt $H; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $start = $y * $W + $x
            if (-not $Mask[$start] -or $seen[$start]) { continue }
            $stack = New-Object System.Collections.Stack
            $stack.Push($start)
            $seen[$start] = $true
            $n = 0
            while ($stack.Count -gt 0) {
                $i = $stack.Pop()
                $n++
                $ix = $i % $W
                $iy = [int][Math]::Floor($i / $W)
                foreach ($d in @(@(1, 0), @(-1, 0), @(0, 1), @(0, -1))) {
                    $nx = $ix + $d[0]
                    $ny = $iy + $d[1]
                    if ($nx -lt 0 -or $ny -lt 0 -or $nx -ge $W -or $ny -ge $H) { continue }
                    $ni = $ny * $W + $nx
                    if ($Mask[$ni] -and -not $seen[$ni]) { $seen[$ni] = $true; $stack.Push($ni) }
                }
            }
            $sizes += $n
        }
    }
    if ($sizes.Count -eq 0) { return @{ Count = 0; Mean = 0.0 } }
    return @{ Count = $sizes.Count; Mean = ($sizes | Measure-Object -Average).Average }
}

function Compare-Skin {
    param([string]$Name, [int]$Row, [int]$Height, [string]$RefPath)

    if (-not (Test-Path $RefPath)) { Write-Host "$Name : reference missing - $RefPath"; return $null }

    $ourSheet = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Ours).Path)
    $ref = [System.Drawing.Bitmap]::FromFile((Resolve-Path $RefPath).Path)
    $w = [Math]::Min($ourSheet.Width, $ref.Width)
    $h = [Math]::Min($Height, $ref.Height)

    $ourLum = New-Object 'double[]' ($w * $h)
    $refLum = New-Object 'double[]' ($w * $h)
    $ourSat = New-Object 'double[]' ($w * $h)
    $refSat = New-Object 'double[]' ($w * $h)
    $ourOpaque = New-Object 'bool[]' ($w * $h)
    $refOpaque = New-Object 'bool[]' ($w * $h)
    $ourColours = @{}
    $refColours = @{}

    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $i = $y * $w + $x
            $op = $ourSheet.GetPixel($x, $Row + $y)
            $rp = $ref.GetPixel($x, $y)
            $ourOpaque[$i] = $op.A -ge 128
            $refOpaque[$i] = $rp.A -ge 128
            if ($ourOpaque[$i]) {
                $ourLum[$i] = Get-Luminance $op
                $ourSat[$i] = Get-Saturation $op
                $ourColours["$($op.R),$($op.G),$($op.B)"] = 1
            }
            if ($refOpaque[$i]) {
                $refLum[$i] = Get-Luminance $rp
                $refSat[$i] = Get-Saturation $rp
                $refColours["$($rp.R),$($rp.G),$($rp.B)"] = 1
            }
        }
    }
    $ourSheet.Dispose()
    $ref.Dispose()

    # --- net footprint: are we painting inside their nets? ----------------
    # One-sided on purpose. The reference may carry nets we deliberately do not
    # use - the sheep's leg wool, for one - and leaving those blank is a design
    # choice, not a misalignment. What must never happen is us painting where
    # the reference has nothing, because that means a net is in the wrong place.
    $outside = 0
    $ourCount = 0
    $both = New-Object 'bool[]' ($w * $h)
    for ($i = 0; $i -lt $w * $h; $i++) {
        if ($ourOpaque[$i]) {
            $ourCount++
            if (-not $refOpaque[$i]) { $outside++ }
        }
        $both[$i] = $ourOpaque[$i] -and $refOpaque[$i]
    }
    $footprint = if ($ourCount -gt 0) { 100.0 * (1.0 - $outside / $ourCount) } else { 0.0 }

    # --- structure: correlate 2x2 block means -----------------------------
    # Per-pixel correlation is meaningless on a dithered skin, where the noise
    # is uncorrelated by design. Averaging 2x2 blocks first washes the dither
    # out and leaves the actual layout.
    $ourBlocks = @()
    $refBlocks = @()
    for ($by = 0; $by -lt $h - 1; $by += 2) {
        for ($bx = 0; $bx -lt $w - 1; $bx += 2) {
            $os = 0.0; $rs = 0.0; $n = 0
            foreach ($d in @(@(0, 0), @(1, 0), @(0, 1), @(1, 1))) {
                $i = ($by + $d[1]) * $w + ($bx + $d[0])
                if ($both[$i]) { $os += $ourLum[$i]; $rs += $refLum[$i]; $n++ }
            }
            if ($n -eq 4) { $ourBlocks += ($os / 4); $refBlocks += ($rs / 4) }
        }
    }
    $correlation = 0.0
    if ($ourBlocks.Count -gt 2) {
        $om = ($ourBlocks | Measure-Object -Average).Average
        $rm = ($refBlocks | Measure-Object -Average).Average
        $num = 0.0; $od = 0.0; $rd = 0.0
        for ($i = 0; $i -lt $ourBlocks.Count; $i++) {
            $a = $ourBlocks[$i] - $om
            $b = $refBlocks[$i] - $rm
            $num += $a * $b; $od += $a * $a; $rd += $b * $b
        }
        if ($od -gt 0 -and $rd -gt 0) { $correlation = $num / [Math]::Sqrt($od * $rd) }
    }

    # --- light/dark masks, each thresholded at its own median --------------
    # Thresholding each against itself compares *where* the markings are, not
    # whether we happened to pick the same brightness.
    $ourVals = @(); $refVals = @()
    for ($i = 0; $i -lt $w * $h; $i++) { if ($both[$i]) { $ourVals += $ourLum[$i]; $refVals += $refLum[$i] } }
    $maskAgree = 0.0
    $ourDensity = 0.0
    $refDensity = 0.0
    $spread = 0.0
    if ($ourVals.Count -gt 0) {
        # How much tone the reference actually carries. On a near-uniform sheet
        # like fleece, a median threshold splits noise arbitrarily and the patch
        # numbers are meaningless, so they get skipped rather than believed.
        $rMean = ($refVals | Measure-Object -Average).Average
        $acc = 0.0
        foreach ($v in $refVals) { $acc += ($v - $rMean) * ($v - $rMean) }
        $spread = [Math]::Sqrt($acc / $refVals.Count)

        $ourMid = Get-OtsuThreshold -Values $ourVals
        $refMid = Get-OtsuThreshold -Values $refVals
        $ourMask = New-Object 'bool[]' ($w * $h)
        $refMask = New-Object 'bool[]' ($w * $h)
        $same = 0
        for ($i = 0; $i -lt $w * $h; $i++) {
            if (-not $both[$i]) { continue }
            $ourMask[$i] = $ourLum[$i] -ge $ourMid
            $refMask[$i] = $refLum[$i] -ge $refMid
            if ($ourMask[$i] -eq $refMask[$i]) { $same++ }
        }
        $maskAgree = 100.0 * $same / $ourVals.Count

        # Patch *density* rather than mean size: whether two blobs happen to
        # touch swings a mean wildly, while the count of separate markings per
        # unit area is what separates hide from static.
        $os = Get-PatchStats -Mask $ourMask -W $w -H $h
        $rs = Get-PatchStats -Mask $refMask -W $w -H $h
        $ourDensity = 100.0 * $os.Count / $ourVals.Count
        $refDensity = 100.0 * $rs.Count / $ourVals.Count
    }

    # --- detail: how much tone moves between neighbouring texels ----------
    # Mean absolute luminance step to the right and downward neighbour. This is
    # the one metric that catches a flat fill: a face painted at its own mean
    # scores near zero here while the reference vignettes from about 170 at the
    # centre to 126 at the rim. It exists because a pig authored from per-face
    # means scored 0.83 correlation - the best of any skin - and was a pink
    # slab. Correlation rewards exactly that mistake, so it cannot catch it.
    $ourStep = 0.0
    $refStep = 0.0
    $steps = 0
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w - 1; $x++) {
            $i = $y * $w + $x
            $j = $i + 1
            if ($both[$i] -and $both[$j]) {
                $ourStep += [Math]::Abs($ourLum[$i] - $ourLum[$j])
                $refStep += [Math]::Abs($refLum[$i] - $refLum[$j])
                $steps++
            }
        }
    }
    for ($y = 0; $y -lt $h - 1; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $i = $y * $w + $x
            $j = $i + $w
            if ($both[$i] -and $both[$j]) {
                $ourStep += [Math]::Abs($ourLum[$i] - $ourLum[$j])
                $refStep += [Math]::Abs($refLum[$i] - $refLum[$j])
                $steps++
            }
        }
    }
    $ourDetail = if ($steps -gt 0) { $ourStep / $steps } else { 0.0 }
    $refDetail = if ($steps -gt 0) { $refStep / $steps } else { 0.0 }
    $detailRatio = if ($refDetail -gt 0.5) { $ourDetail / $refDetail } else { 1.0 }

    # --- colour character: saturation and overall brightness --------------
    # Two textures can agree on structure and still look nothing alike. Our pig
    # matched the reference's luminance face by face and read as washed-out
    # against its coral, because every structural metric here is computed on
    # luminance and is blind to saturation by construction.
    $ourSatSum = 0.0
    $refSatSum = 0.0
    $ourLumSum = 0.0
    $refLumSum = 0.0
    $shared = 0
    for ($i = 0; $i -lt $w * $h; $i++) {
        if (-not $both[$i]) { continue }
        $ourSatSum += $ourSat[$i]
        $refSatSum += $refSat[$i]
        $ourLumSum += $ourLum[$i]
        $refLumSum += $refLum[$i]
        $shared++
    }
    $satRatio = if ($shared -gt 0 -and $refSatSum -gt 0) { $ourSatSum / $refSatSum } else { 1.0 }
    $lumRatio = if ($shared -gt 0 -and $refLumSum -gt 0) { $ourLumSum / $refLumSum } else { 1.0 }

    # --- originality: how many of our exact colours are theirs? ------------
    $sharedColours = 0
    foreach ($k in $ourColours.Keys) { if ($refColours.ContainsKey($k)) { $sharedColours++ } }
    $overlap = if ($ourColours.Count -gt 0) { 100.0 * $sharedColours / $ourColours.Count } else { 0.0 }

    $patchRatio = if ($refDensity -gt 0) { $ourDensity / $refDensity } else { 0.0 }

    return [pscustomobject]@{
        Name       = $Name
        Fit        = [Math]::Round($footprint, 1)
        Corr       = [Math]::Round($correlation, 2)
        Mask       = [Math]::Round($maskAgree, 1)
        OurP       = [Math]::Round($ourDensity, 1)
        RefP       = [Math]::Round($refDensity, 1)
        PatchRatio = [Math]::Round($patchRatio, 2)
        Detail     = [Math]::Round($detailRatio, 2)
        Sat        = [Math]::Round($satRatio, 2)
        Lum        = [Math]::Round($lumRatio, 2)
        Spread     = [Math]::Round($spread, 1)
        OurCol     = $ourColours.Count
        RefCol     = $refColours.Count
        Overlap    = [Math]::Round($overlap, 1)
    }
}

$results = @()
foreach ($t in $targets) {
    if ($Only -ne "" -and $t.Name -ne $Only) { continue }
    $r = Compare-Skin -Name $t.Name -Row $t.Row -Height $t.Height -RefPath $t.Ref
    if ($null -ne $r) { $results += $r }
}

$results | Format-Table -AutoSize Name, Fit, Corr, Mask, PatchRatio, Detail, Sat, Lum, OurCol, RefCol, Overlap

Write-Host "  Fit  = our opaque texels inside their nets (%)   Corr = 2x2 block luminance correlation"
Write-Host "  Mask = light/dark agreement after Otsu (%)      PatchRatio = markings per area vs theirs"
Write-Host "  Detail = texel-to-texel tone movement vs theirs - near 0 means we painted flat fills"
Write-Host "  Sat/Lum = our mean saturation and brightness vs theirs - Sat below 1 is washed out"
Write-Host "  Overlap = our exact colours also in theirs (%) - this one must stay LOW"
Write-Host ""
Write-Host "  These are a guide, not a verdict. A flat pink slab once scored the highest"
Write-Host "  correlation of any skin. Look at the thing before believing the table."
Write-Host ""
$failed = $false
foreach ($r in $results) {
    $notes = @()
    $advice = @()

    # Geometry must agree: painting outside the reference's nets means a net is
    # in the wrong place, which is a bug rather than a style.
    if ($r.Fit -lt 92) { $notes += "footprint $($r.Fit)% - painting outside their nets" }

    # Patch analysis only means something when the reference carries real tonal
    # structure. On a near-uniform sheet it splits noise, and on a densely
    # dithered one every pixel is its own island - neither says anything about
    # whether the markings are right.
    $patchMeaningful = $r.Spread -ge 18 -and $r.RefCol -lt 200
    if ($patchMeaningful) {
        if ($r.PatchRatio -gt 3.0) {
            $notes += "patches $($r.PatchRatio)x - markings scattered where the reference has slabs"
        } elseif ($r.PatchRatio -lt 0.15) {
            $advice += "patches $($r.PatchRatio)x - flatter than the reference, which may be fine"
        }
    }

    # Sharing exact colours is the legal risk, not the aesthetic one.
    if ($r.Overlap -gt 25) { $notes += "colour overlap $($r.Overlap)% - too close to the reference" }

    # Washed out, or the wrong overall brightness. Structure can be perfect and
    # the thing still look wrong if the colour has no life in it.
    if ($r.Sat -lt 0.75) { $notes += "saturation $($r.Sat)x - washed out against the reference" }
    elseif ($r.Sat -gt 1.35) { $advice += "saturation $($r.Sat)x - louder than the reference" }
    if ($r.Lum -lt 0.8 -or $r.Lum -gt 1.2) { $advice += "brightness $($r.Lum)x of the reference" }

    # Flat where the reference is modelled. Measured against a deliberately
    # flattened pig: it scored 0.57 here against 1.14 for the modelled one - so
    # the metric discriminates, but no single threshold separates "acceptably
    # clean" from "slab" across every creature. Our sheep sits at 0.55 and is
    # fine. Hence a hard failure only for the egregious case and a note
    # otherwise, with the judgement left to whoever is looking.
    if ($r.Detail -lt 0.35) {
        $notes += "detail $($r.Detail)x - flat fills where the reference has shading"
    } elseif ($r.Detail -lt 0.7) {
        $advice += "detail $($r.Detail)x - flatter than the reference; check it reads as modelled"
    } elseif ($r.Detail -gt 2.5) {
        $advice += "detail $($r.Detail)x - noisier than the reference"
    }

    if ($notes.Count -eq 0) {
        $tail = if ($advice.Count -gt 0) { "  (note: $($advice -join '; '))" } else { "" }
        Write-Host "PASS  $($r.Name)$tail"
    } else {
        $failed = $true
        Write-Host "CHECK $($r.Name): $($notes -join '; ')"
    }
}
if ($failed) { Write-Host "`nSome skins want another look." } else { Write-Host "`nAll skins within tolerance." }
