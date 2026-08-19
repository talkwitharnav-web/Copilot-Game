# Checks every block's measurements against the reference's own block models.
#
# The single most expensive bug shape on this project is a rectangle or an
# extent guessed out of a picture instead of read from the model that states it.
# The reference ships `models/block/*.json` for every block it has, each naming
# its elements' `from` and `to` in sixteenths, every face's `uv` rect and which
# texture that face samples - so the true size of nearly every block we have is
# already sitting in the repo, and nothing was reading it.
#
#   powershell -NoProfile -File tools\check-models.ps1
#   powershell -NoProfile -File tools\check-models.ps1 -Verbose3
#   powershell -NoProfile -File tools\check-models.ps1 -ReuseDump
#
# It runs the game once with `block_probe=1`, which writes `block-shapes.txt`
# beside the exe and exits without opening a window, then compares three things
# against the reference. settings.cfg is backed up and restored in the same
# command, because a soak once left a setting behind and the user filed a bug
# against a working system. `-ReuseDump` reads the dump already beside the exe
# instead of running anything, and says how old it is - the whole check bar the
# probe run works offline, which is how it is developed on a busy machine.
#
# WHAT IT COMPARES, weakest first:
#
#   extent   the union box of what we draw against the union box the reference's
#            elements describe, rotations applied. **This is the check that let
#            two real bugs through**: a flower pot whose two X walls carried the
#            whole pot's lid rect, and a scaffolding missing its four top rails.
#            Neither moves a union extent by a texel, so neither was visible
#            here until the two checks below were added.
#
#   count    how many boxes we draw against how many elements the reference
#            has, for `Model` blocks only. A missing box that lies *inside* the
#            union - the scaffolding rails exactly - cannot hide from this.
#            Restricted to models whose elements are all real boxes: a zero
#            thickness quad or a rotated plane is something our box mesher
#            cannot express, so counting them would report a representation
#            difference as a fault.
#
#   uv       every face's rectangle, read out of `postModel` in Block.hpp and
#            compared against the `uv` the reference states for the element in
#            the same place - the flower pot's fault exactly. Only boxes whose
#            geometry already matches an element are compared, so the two sides
#            are always the same box seen twice. A `ModelBox` carries one rect
#            for its four sides and one for its lid where the reference may name
#            six different ones, so ours has to match *one of* the sides and
#            *one of* the lid pair; anything else is our representation being
#            narrower than theirs, not a mistake.
#
# HONESTY. A check that cries wolf is worse than no check, because the value of
# this file is that a disagreement is always real. Where the reference genuinely
# differs from us on purpose it is named in `$known`, `$knownCount` or
# `$knownUv` and reported in its own section - a *different* disagreement on the
# same block still shows.

param(
    [string]$Build = "release",
    # Difference in sixteenths before a block is called wrong. Half a texel is
    # below anything anyone can see and above every rounding artefact.
    [double]$Tolerance = 0.5,
    # Also list the blocks that match, and the ones with no reference model.
    [switch]$Verbose3,
    # Compare against the dump already beside the exe instead of running the
    # game. The dump is a pure function of Block.hpp, so this is exact whenever
    # the dump is newer than the source - which it says either way.
    [switch]$ReuseDump
)

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$binDir = Join-Path $root "build\$Build\bin"
$exe = Join-Path $binDir 'game.exe'
$config = Join-Path $binDir 'settings.cfg'
$dump = Join-Path $binDir 'block-shapes.txt'
$blockHeader = Join-Path $root 'game\src\world\Block.hpp'
$modelDir = Join-Path $root 'reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\models\block'

if (-not (Test-Path $modelDir)) { Write-Error "no reference models at $modelDir"; exit 1 }

# ---- 1. Ask the game what it draws. ----------------------------------------
$stale = $null
if ($ReuseDump) {
    if (-not (Test-Path $dump)) { Write-Error "-ReuseDump, but there is no dump at $dump"; exit 1 }
    $written = (Get-Item $dump).LastWriteTime
    Write-Output ("reusing the dump written {0:yyyy-MM-dd HH:mm} - nothing was run" -f $written)
    if ((Test-Path $blockHeader) -and (Get-Item $blockHeader).LastWriteTime -gt $written) {
        $stale = "Block.hpp has been edited since this dump was written, so every box count and extent below may be one build out of date."
    }
} else {
    if (-not (Test-Path $exe)) { Write-Error "no build at $exe"; exit 1 }
    $backup = $null
    if (Test-Path $config) { $backup = Get-Content $config -Raw -Encoding UTF8 }
    try {
        $text = if ($null -ne $backup) { $backup.TrimEnd() + "`n" } else { "" }
        # Appended, because settings.cfg on disk is often partial and the parser
        # takes the last occurrence of a key.
        $text += "block_probe=1`n"
        [System.IO.File]::WriteAllText($config, $text, (New-Object System.Text.UTF8Encoding($false)))
        Remove-Item $dump -ErrorAction SilentlyContinue
        $proc = Start-Process -FilePath $exe -WorkingDirectory $binDir -PassThru -Wait -WindowStyle Hidden
        if ($proc.ExitCode -ne 0) { Write-Error "block probe exited $($proc.ExitCode)"; exit 1 }
    }
    finally {
        if ($null -ne $backup) {
            [System.IO.File]::WriteAllText($config, $backup, (New-Object System.Text.UTF8Encoding($false)))
        }
    }
    if (-not (Test-Path $dump)) { Write-Error "the probe wrote no dump"; exit 1 }
}

# ---- 2. Read the reference models, following parents for elements. ---------
$cache = @{}
$modelCache = @{}

function Get-Model([string]$name) {
    if ($cache.ContainsKey($name)) { return $cache[$name] }
    $path = Join-Path $modelDir "$name.json"
    if (-not (Test-Path $path)) { $cache[$name] = $null; return $null }
    $json = Get-Content -Encoding UTF8 -Raw $path | ConvertFrom-Json
    $cache[$name] = $json
    return $json
}

# A model without elements of its own inherits its parent's, which is how every
# `cube_all` block resolves to a whole cell. The chain is short but real:
# red_bed_head -> template_bed_head, copper_grate -> cube_all -> cube.
function Get-Elements([string]$name, [int]$depth = 0) {
    if ($depth -gt 8) { return $null }
    $model = Get-Model $name
    if ($null -eq $model) { return $null }
    if ($null -ne $model.elements) { return $model.elements }
    if ($null -eq $model.parent) { return $null }
    $parent = ($model.parent -replace '^minecraft:', '') -replace '^block/', ''
    return Get-Elements $parent ($depth + 1)
}

# The rectangle a face samples when it does not say. **The reference omits `uv`
# far more often than it writes one** - a scaffolding post states none of its
# five faces - and the rule it falls back to is the element's own footprint on
# the two axes that face does not span. Written out rather than summarised
# because the four sides do not agree on sign, and it is checked against
# `flower_pot.json`, which states every one of them explicitly: element
# `[6,0,5]-[10,6,6]` writes `down [6,10,10,11]` and `up [6,5,10,6]`, which is
# exactly what this returns for it.
function Get-DefaultUv([string]$face, $from, $to) {
    $x1 = $from[0]; $y1 = $from[1]; $z1 = $from[2]
    $x2 = $to[0];   $y2 = $to[1];   $z2 = $to[2]
    # Parenthesised, every one of them: a comma binds tighter than a minus, so
    # `@(16 - $x2, 16 - $y2)` hands the subtraction an array and throws.
    switch ($face) {
        'north' { return @((16 - $x2), (16 - $y2), (16 - $x1), (16 - $y1)) }
        'south' { return @($x1, (16 - $y2), $x2, (16 - $y1)) }
        'west'  { return @($z1, (16 - $y2), $z2, (16 - $y1)) }
        'east'  { return @((16 - $z2), (16 - $y2), (16 - $z1), (16 - $y1)) }
        'up'    { return @($x1, $z1, $x2, $z2) }
        'down'  { return @($x1, (16 - $z2), $x2, (16 - $z1)) }
    }
    return $null
}

# A rect with its corners the right way round. The reference mirrors a face by
# writing its `uv` backwards - `scaffolding_stable` states `[14,0,2,2]` on a rail
# - and a mirrored rect is the same rectangle, so both sides are normalised
# before they are compared and only the rectangle is judged.
function Get-Rect($uv) {
    return @([Math]::Min($uv[0], $uv[2]), [Math]::Min($uv[1], $uv[3]),
             [Math]::Max($uv[0], $uv[2]), [Math]::Max($uv[1], $uv[3]))
}

function Test-SameRect($a, $b) {
    for ($i = 0; $i -lt 4; $i++) {
        if ([Math]::Abs($a[$i] - $b[$i]) -gt 0.01) { return $false }
    }
    return $true
}

# Where an element ends up once its `rotation` is applied, which is where the
# player sees it. **Skipping every rotated model is what hid 467 rows** - a
# sculk sensor's five elements, a lectern's three, every coral fan - so the turn
# is done here rather than avoided: eight corners about the stated origin, and
# `rescale` widens by 1/cos, which is exactly how the reference makes a cross
# still fill its cell after a 45 degree turn.
function Get-ElementBounds($element) {
    $from = @([double]$element.from[0], [double]$element.from[1], [double]$element.from[2])
    $to = @([double]$element.to[0], [double]$element.to[1], [double]$element.to[2])
    if ($null -eq $element.rotation) {
        return [PSCustomObject]@{
            Lo = @([Math]::Min($from[0], $to[0]), [Math]::Min($from[1], $to[1]), [Math]::Min($from[2], $to[2]))
            Hi = @([Math]::Max($from[0], $to[0]), [Math]::Max($from[1], $to[1]), [Math]::Max($from[2], $to[2]))
        }
    }
    $axis = "$($element.rotation.axis)".ToLowerInvariant()
    $angle = [double]$element.rotation.angle
    $origin = @([double]$element.rotation.origin[0], [double]$element.rotation.origin[1], [double]$element.rotation.origin[2])
    $rescale = ($null -ne $element.rotation.rescale) -and [bool]$element.rotation.rescale
    $radians = $angle * [Math]::PI / 180.0
    $cos = [Math]::Cos($radians)
    $sin = [Math]::Sin($radians)
    $scale = 1.0
    if ($rescale -and [Math]::Abs($cos) -gt 0.0001) { $scale = 1.0 / [Math]::Abs($cos) }
    # Which two axes the turn moves, in the order the right-hand rule uses.
    $u = 0; $v = 2
    if ($axis -eq 'x') { $u = 1; $v = 2 }
    elseif ($axis -eq 'y') { $u = 2; $v = 0 }
    else { $u = 0; $v = 1 }
    $lo = @(99.0, 99.0, 99.0)
    $hi = @(-99.0, -99.0, -99.0)
    foreach ($cx in @($from[0], $to[0])) {
        foreach ($cy in @($from[1], $to[1])) {
            foreach ($cz in @($from[2], $to[2])) {
                $point = @([double]$cx, [double]$cy, [double]$cz)
                $a = $point[$u] - $origin[$u]
                $b = $point[$v] - $origin[$v]
                $point[$u] = $origin[$u] + ($a * $cos - $b * $sin) * $scale
                $point[$v] = $origin[$v] + ($a * $sin + $b * $cos) * $scale
                for ($k = 0; $k -lt 3; $k++) {
                    $lo[$k] = [Math]::Min($lo[$k], $point[$k])
                    $hi[$k] = [Math]::Max($hi[$k], $point[$k])
                }
            }
        }
    }
    return [PSCustomObject]@{ Lo = $lo; Hi = $hi }
}

# Everything one reference model has to say, read once and kept: its union box,
# how many elements it is built from, each element's box and each face's rect.
# `Why` is what to say when there is nothing to compare against, and it is the
# difference between "the reference has no geometry for this" and "we looked up
# the wrong name" - two numbers that used to be added together and reported as
# one.
function Get-ReferenceModel([string]$name) {
    if ($modelCache.ContainsKey($name)) { return $modelCache[$name] }
    $result = $null
    $elements = Get-Elements $name
    if ($null -eq $elements) {
        $why = if ($null -eq (Get-Model $name)) { 'nofile' } else { 'nogeometry' }
        $result = [PSCustomObject]@{ Name = $name; Box = $null; Why = $why }
    } else {
        $lo = @(99.0, 99.0, 99.0)
        $hi = @(-99.0, -99.0, -99.0)
        $boxes = New-Object System.Collections.ArrayList
        $rotated = $false
        $thin = $false
        foreach ($element in $elements) {
            if ($null -eq $element.from -or $null -eq $element.to) { continue }
            $bounds = Get-ElementBounds $element
            for ($a = 0; $a -lt 3; $a++) {
                $lo[$a] = [Math]::Min($lo[$a], $bounds.Lo[$a])
                $hi[$a] = [Math]::Max($hi[$a], $bounds.Hi[$a])
            }
            if ($null -ne $element.rotation) { $rotated = $true }
            $flat = $false
            for ($a = 0; $a -lt 3; $a++) {
                if ([Math]::Abs([double]$element.to[$a] - [double]$element.from[$a]) -lt 0.001) { $flat = $true }
            }
            if ($flat) { $thin = $true }
            $faces = @{}
            if ($null -ne $element.faces) {
                foreach ($face in @('north', 'south', 'east', 'west', 'up', 'down')) {
                    $declared = $element.faces.$face
                    if ($null -eq $declared) { continue }
                    $uv = $declared.uv
                    if ($null -eq $uv) {
                        $uv = Get-DefaultUv $face @([double]$element.from[0], [double]$element.from[1], [double]$element.from[2]) `
                                                  @([double]$element.to[0], [double]$element.to[1], [double]$element.to[2])
                    } else {
                        $uv = @([double]$uv[0], [double]$uv[1], [double]$uv[2], [double]$uv[3])
                    }
                    $faces[$face] = Get-Rect $uv
                }
            }
            [void]$boxes.Add([PSCustomObject]@{
                From = @([double]$element.from[0], [double]$element.from[1], [double]$element.from[2])
                To = @([double]$element.to[0], [double]$element.to[1], [double]$element.to[2])
                Rotated = ($null -ne $element.rotation)
                Flat = $flat
                Faces = $faces
            })
        }
        if ($boxes.Count -eq 0) {
            $result = [PSCustomObject]@{ Name = $name; Box = $null; Why = 'nogeometry' }
        } else {
            # A model with no thickness at all is a *face* template -
            # `template_single_face` and its kin - which the blockstate places
            # six ways to build a mushroom block or a patch of lichen. Its
            # written extent is one of those faces, not the block, so there is
            # nothing here to compare against.
            $degenerate = $false
            for ($a = 0; $a -lt 3; $a++) {
                if ($hi[$a] - $lo[$a] -lt 0.001) { $degenerate = $true }
            }
            if ($degenerate) {
                $result = [PSCustomObject]@{ Name = $name; Box = $null; Why = 'facetemplate' }
            } else {
                # Every element a rotated plane is the cross template and its
                # kin - two sheets swung 45 degrees and widened back out. Ours
                # draws a crossed pair corner to corner, which is the same thing
                # said without a rotation, and the 0.8 texel inset between them
                # is recorded below rather than reported 279 times.
                $allPlanes = $true
                foreach ($box in $boxes) {
                    if (-not ($box.Rotated -and $box.Flat)) { $allPlanes = $false }
                }
                # **The part of this model a box mesher could express**: the
                # elements that are neither turned nor paper-thin. A campfire's
                # flames, a cocoa pod's string, a sculk sensor's tendrils and a
                # torch's glow quads are all one of those two, and measuring our
                # boxes against a union that includes them reports the shape of
                # our mesher rather than a mistake. Kept beside the full union
                # rather than instead of it, so a difference can be *attributed*
                # to those parts instead of excused by them.
                $solidLo = @(99.0, 99.0, 99.0)
                $solidHi = @(-99.0, -99.0, -99.0)
                $solidCount = 0
                foreach ($box in $boxes) {
                    if ($box.Rotated -or $box.Flat) { continue }
                    for ($a = 0; $a -lt 3; $a++) {
                        $solidLo[$a] = [Math]::Min($solidLo[$a], [Math]::Min($box.From[$a], $box.To[$a]))
                        $solidHi[$a] = [Math]::Max($solidHi[$a], [Math]::Max($box.From[$a], $box.To[$a]))
                    }
                    $solidCount++
                }
                $solidBox = $null
                if ($solidCount -gt 0) { $solidBox = [PSCustomObject]@{ Lo = $solidLo; Hi = $solidHi } }
                $result = [PSCustomObject]@{
                    Name = $name
                    Box = [PSCustomObject]@{ Lo = $lo; Hi = $hi }
                    SolidBox = $solidBox
                    SolidCount = $solidCount
                    Boxes = $boxes
                    Count = $boxes.Count
                    Rotated = $rotated
                    Thin = $thin
                    CrossLike = $allPlanes
                    Why = ''
                }
            }
        }
    }
    $modelCache[$name] = $result
    return $result
}

# How far apart two boxes are, in texels, on their worst measurement.
#
# **Height is compared where it sits; width and depth are compared only as
# sizes.** A blockstate turns one model four ways - the reference ships a single
# `*_trapdoor_open` on the north face and rotates it - so comparing horizontal
# *positions* reports every east-facing anything as thirteen texels wrong. What
# cannot hide from that is the number the mistakes are actually made in: how
# tall the thing is.
function Get-Worst($ours, $reference) {
    # **A block that mounts on any of six faces is the same shape turned onto
    # another axis**, and the reference expresses that turn in its blockstate
    # rather than in the model - so its JSON only ever describes the upright
    # one. A lightning rod on a wall is four by four by sixteen lying down
    # against the reference's four by sixteen by four standing up, and comparing
    # height where it sits reports twelve texels of nothing.
    #
    # So: if the three extents match as a **set**, this is a rotation and not a
    # disagreement. A genuinely wrong size changes the set, so nothing that
    # matters can hide behind this.
    $oursAll = @(($ours.Hi[0] - $ours.Lo[0]), ($ours.Hi[1] - $ours.Lo[1]),
                 ($ours.Hi[2] - $ours.Lo[2])) | Sort-Object
    $refAll = @(($reference.Hi[0] - $reference.Lo[0]), ($reference.Hi[1] - $reference.Lo[1]),
                ($reference.Hi[2] - $reference.Lo[2])) | Sort-Object
    $turned = $true
    for ($i = 0; $i -lt 3; $i++) {
        if ([Math]::Abs($oursAll[$i] - $refAll[$i]) -gt 0.001) { $turned = $false }
    }
    if ($turned) {
        return [PSCustomObject]@{ Worst = 0.0; Detail = 'same shape, turned onto another axis' }
    }

    $worst = [Math]::Abs($ours.Lo[1] - $reference.Lo[1])
    $axis = "ymin ours $([Math]::Round($ours.Lo[1],2)) ref $([Math]::Round($reference.Lo[1],2))"
    $d = [Math]::Abs($ours.Hi[1] - $reference.Hi[1])
    if ($d -gt $worst) { $worst = $d; $axis = "ymax ours $([Math]::Round($ours.Hi[1],2)) ref $([Math]::Round($reference.Hi[1],2))" }

    $oursFlat = @(($ours.Hi[0] - $ours.Lo[0]), ($ours.Hi[2] - $ours.Lo[2])) | Sort-Object
    $refFlat = @(($reference.Hi[0] - $reference.Lo[0]), ($reference.Hi[2] - $reference.Lo[2])) | Sort-Object
    for ($i = 0; $i -lt 2; $i++) {
        $d = [Math]::Abs($oursFlat[$i] - $refFlat[$i])
        if ($d -gt $worst) {
            $worst = $d
            $axis = "across ours $([Math]::Round($oursFlat[$i],2)) ref $([Math]::Round($refFlat[$i],2))"
        }
    }
    return [PSCustomObject]@{ Worst = $worst; Detail = $axis }
}

# ---- 3. Our display names are not the reference's file names. --------------
# Ordinary words map by lowercasing and swapping spaces for underscores. The
# coined ones are ours by design and are listed rather than guessed at, and a
# handful of families are named differently on each side.
#
# **The two tables are separate because one substring loop corrupted names.**
# `grass` -> `short_grass` applied inside a longer name turned Tall Grass into
# `tall_short_grass` and Seagrass into `seashort_grass`, neither of which
# exists, so both landed in the unchecked pile instead of being measured against
# `tall_grass_top.json` and `seagrass.json`. A rename is either a whole *name*
# or a whole *word*, and never a run of letters inside one.
$renameExact = @{
    'grass'                 = 'short_grass'
    # Two blocks, two models, and one of them used to take the other's: `Snow`
    # is the full cube `snow_block.json` and `Top Snow` is the layer, which the
    # reference ships seven of by height.
    'snow'                  = 'snow_block'
    'top_snow'              = 'snow'
    'monster_spawner'       = 'spawner'
    'redstone_repeater'     = 'repeater_1tick'
    'redstone_comparator'   = 'comparator'
    'wheat_crop'            = 'wheat'
    'potato_crop'           = 'potatoes'
    'carrot_crop'           = 'carrots'
    'beetroot_crop'         = 'beetroots'
    'hay_bale'              = 'hay_block'
    'planks'                = 'oak_planks'
    'log'                   = 'oak_log'
    'block_of_stripped_bamboo' = 'stripped_bamboo_block'
    'waxed_block_of_copper' = 'copper_block'
    'block_of_iron'         = 'iron_block'
    'block_of_gold'         = 'gold_block'
    'block_of_diamond'      = 'diamond_block'
    'block_of_coal'         = 'coal_block'
    'block_of_copper'       = 'copper_block'
    'block_of_emerald'      = 'emerald_block'
    'block_of_redstone'     = 'redstone_block'
    'block_of_lapis_lazuli' = 'lapis_block'
    'block_of_emberite'     = 'netherite_block'
    'block_of_raw_iron'     = 'raw_iron_block'
    'block_of_raw_gold'     = 'raw_gold_block'
    'block_of_raw_copper'   = 'raw_copper_block'
    'block_of_amethyst'     = 'amethyst_block'
    'block_of_quartz'       = 'quartz_block'
    'block_of_bamboo'       = 'bamboo_block'
    'glow_berries'          = 'cave_vines_lit'
    'bamboo'                = 'bamboo1_age0'
    'fire'                  = 'fire_floor0'
    'soul_fire'             = 'soul_fire_floor0'
    'tripwire'              = 'tripwire_n'
    'snow_layer'            = 'snow_height2'
}
# Whole words inside a longer name, which is what our coined families need:
# every Stowbox is a shulker box and every Emberite thing is netherite.
$renameWord = @{
    'emberite'   = 'netherite'
    'stowbox'    = 'shulker_box'
    'bramble'    = 'creeper'
    'void_pearl' = 'ender_pearl'
}

function Get-ModelName([string]$display) {
    $key = $display.ToLowerInvariant() -replace "[^a-z0-9]+", '_'
    $key = $key.Trim('_')
    if ($renameExact.ContainsKey($key)) { return $renameExact[$key] }
    $words = $key -split '_'
    $changed = $false
    for ($i = 0; $i -lt $words.Count; $i++) {
        if ($renameWord.ContainsKey($words[$i])) { $words[$i] = $renameWord[$words[$i]]; $changed = $true }
    }
    if ($changed) { return ($words -join '_') }
    return $key
}

# The variants one block is shipped as. **This is where most of the unchecked
# 1414 went**: a door has no `oak_door.json` at all, it has four of them, and
# looking up the bare name found nothing and called it a block entity. Every
# pattern below was checked with Test-Path against the reference folder before
# it was written down.
$genericSuffixes = @('', '_post', '_top', '_bottom', '_open', '_wall', '_wall_open', '_inner', '_outer',
                     '_side', '_floor', '_ceiling', '_pressed', '_down', '_on', '_lit', '_rot_0',
                     '_stage0', '_stage1', '_stage2', '_stage3', '_stage4', '_stage5', '_stage6', '_stage7',
                     '_age0', '_age1', '_age2', '_age3',
                     '_height2', '_height4', '_height6', '_height8', '_height10', '_height12', '_height14',
                     '_0', '_1', '_stable', '_unstable', '_n', '_empty', '_honey', '_subtract',
                     '_head', '_foot', '_one_candle', '_two_candles', '_three_candles', '_four_candles',
                     '_bottom_left', '_bottom_right', '_top_left', '_top_right', '_inventory', '_full')
$candidateCache = @{}

function Get-Candidates([string]$key) {
    if ($candidateCache.ContainsKey($key)) { return $candidateCache[$key] }
    $names = New-Object System.Collections.ArrayList
    foreach ($suffix in $genericSuffixes) { [void]$names.Add($key + $suffix) }
    # Waxed copper reuses the unwaxed model - the reference paints the wax on
    # nothing, it only stops the block ageing.
    if ($key -like 'waxed_*') {
        $bare = $key -replace '^waxed_', ''
        foreach ($suffix in $genericSuffixes) { [void]$names.Add($bare + $suffix) }
    }
    # A candle's model names its own count, and one candle is the state we dump.
    if ($key -like '*candle*') {
        [void]$names.Add($key + '_one_candle')
        [void]$names.Add(($key -replace '_candle', '_one_candle'))
    }
    # A repeater's *delay* is in its file name, so there is no `repeater.json`
    # at all - four delays, each powered or not.
    if ($key -like 'repeater*') {
        for ($i = 1; $i -le 4; $i++) {
            [void]$names.Add("repeater_${i}tick")
            [void]$names.Add("repeater_${i}tick_on")
            [void]$names.Add("repeater_${i}tick_locked")
        }
    }
    # A banner has a `banner.json` holding nothing but a particle texture,
    # because the cloth is a block entity. Worth finding: it is the difference
    # between "the reference has no geometry" and "we looked up the wrong name".
    if ($key -like '*_banner') { [void]$names.Add('banner') }
    # A respawn anchor's charge is in the file name, and only `_0` is the state
    # a freshly placed one is in.
    if ($key -eq 'respawn_anchor') { for ($i = 0; $i -lt 5; $i++) { [void]$names.Add("respawn_anchor_$i") } }
    # A bed is a head model and a foot model and never one file.
    if ($key -like '*_bed') { [void]$names.Add($key + '_head'); [void]$names.Add($key + '_foot') }
    # **One display name, two entirely different models.** The probe dumps a
    # name and an extent, not a blockstate, so "Oak Sign" arrives eight times -
    # four standing rotations and four wall facings - and the wall one is a
    # plate on the side of a block that shares nothing with the post-and-board
    # standing model. Offering both and letting the ranking choose is the whole
    # reason candidates are a list: without `oak_wall_sign` here, 88 wall signs
    # were measured against the standing model and reported five texels wrong
    # when they match their own model to the texel.
    if ($key -like '*_hanging_sign') {
        [void]$names.Add(($key -replace '_hanging_sign$', '_wall_hanging_sign'))
        for ($i = 0; $i -lt 4; $i++) { [void]$names.Add($key + "_attached_rot_$i") }
    } elseif ($key -like '*_sign') {
        [void]$names.Add(($key -replace '_sign$', '_wall_sign'))
    }
    $result = @($names | Select-Object -Unique)
    $candidateCache[$key] = $result
    return $result
}

# ---- 4. What *we* draw, box for box, straight out of Block.hpp. ------------
#
# `block-shapes.txt` carries a union extent and a box count, which is all a
# probe can say in ten columns - the rectangles are in the source. **This reads
# `postModel` rather than trusting it**, and reads it conservatively: a box is
# only taken when it sits directly inside a branch that names its blocks, when
# its index is a plain number, and when all fourteen numbers resolve to
# arithmetic on `t`. A box behind a `switch`, a loop or a variable is skipped
# and counted as unread rather than guessed at.
$ourModelCache = $null

function Get-Number([string]$expr) {
    $e = $expr.Trim()
    $e = $e -replace '([0-9.])[fF]\b', '$1'
    $e = $e -replace '\bt\b', '(1.0/16.0)'
    if ($e -match '[A-Za-z_]') { return $null }
    if ($e.Length -eq 0) { return $null }
    # Every integer made a real number, because Compute divides 1/16 as integers
    # and hands back 0.
    $e = [regex]::Replace($e, '(?<![\d.])(\d+)(?![\d.])', '$1.0')
    try {
        $table = New-Object System.Data.DataTable
        $value = $table.Compute($e, '')
        return [double]$value
    } catch {
        return $null
    }
}

function Split-Fields([string]$text) {
    $fields = New-Object System.Collections.ArrayList
    $depth = 0
    $current = New-Object System.Text.StringBuilder
    foreach ($c in $text.ToCharArray()) {
        if ($c -eq '{' -or $c -eq '(') { $depth++ }
        elseif ($c -eq '}' -or $c -eq ')') { $depth-- }
        if ($c -eq ',' -and $depth -eq 0) {
            [void]$fields.Add($current.ToString())
            [void]$current.Clear()
            continue
        }
        [void]$current.Append($c)
    }
    if ($current.ToString().Trim().Length -gt 0) { [void]$fields.Add($current.ToString()) }
    return $fields
}

# The branches whose condition is a family predicate rather than a list of ids,
# and the one reference model each of them is. Written out rather than guessed,
# because `isHopper` covers six facings and only one file is the shape we dump.
$familyModels = @{
    'isAnvil(id)'            = 'anvil'
    'isHopper(id)'           = 'hopper'
    'isTorchBlock(id)'       = 'torch'
    'isLightningRod(id)'     = 'lightning_rod'
    'isTripwireHook(id)'     = 'tripwire_hook'
    'isDaylightDetector(id)' = 'daylight_detector'
    'isPistonHead(id)'       = 'piston_head'
}

function Get-OurModels {
    if ($null -ne $script:ourModelCache) { return $script:ourModelCache }
    $models = @{}
    if (-not (Test-Path $blockHeader)) { $script:ourModelCache = $models; return $models }
    $source = [System.IO.File]::ReadAllText($blockHeader)
    # Comments stripped, so a brace in prose cannot move the depth.
    $source = [regex]::Replace($source, '/\*.*?\*/', '', 'Singleline')
    $source = [regex]::Replace($source, '//[^\r\n]*', '')
    $start = [regex]::Match($source, 'constexpr ModelBoxes postModel\(BlockId id\)\s*\{')
    if (-not $start.Success) { $script:ourModelCache = $models; return $models }

    $i = $start.Index + $start.Length
    $depth = 1
    $frames = New-Object System.Collections.ArrayList
    $buffer = New-Object System.Text.StringBuilder
    $statements = New-Object System.Collections.ArrayList
    while ($i -lt $source.Length -and $depth -gt 0) {
        $c = $source[$i]
        if ($c -eq '{') {
            $head = $buffer.ToString()
            if ($head -match 'result\.boxes\[(\d+)\]\s*=\s*$') {
                $index = [int]$Matches[1]
                $d = 0
                $close = -1
                for ($k = $i; $k -lt $source.Length; $k++) {
                    if ($source[$k] -eq '{') { $d++ }
                    elseif ($source[$k] -eq '}') { $d--; if ($d -eq 0) { $close = $k; break } }
                }
                if ($close -lt 0) { break }
                $frame = if ($frames.Count -gt 0) { $frames[$frames.Count - 1] } else { '' }
                [void]$statements.Add([PSCustomObject]@{
                    Index = $index
                    Init = $source.Substring($i + 1, $close - $i - 1)
                    Frame = $frame
                })
                $i = $close + 1
                [void]$buffer.Clear()
                continue
            }
            [void]$frames.Add($head.Trim())
            $depth++
            [void]$buffer.Clear()
            $i++
            continue
        }
        if ($c -eq '}') {
            $depth--
            if ($frames.Count -gt 0) { $frames.RemoveAt($frames.Count - 1) }
            [void]$buffer.Clear()
            $i++
            continue
        }
        if ($c -eq ';') { [void]$buffer.Clear(); $i++; continue }
        [void]$buffer.Append($c)
        $i++
    }

    $unread = 0
    foreach ($statement in $statements) {
        $frame = ($statement.Frame -replace '\s+', ' ').Trim()
        $label = $null
        $modelKey = $null
        if ($frame -match '^if \(\s*id == BlockId::(\w+)\s*\)$') {
            $label = $Matches[1]
            $modelKey = $Matches[1]
        } elseif ($frame -match '^if \(\s*id == BlockId::\w+(\s*\|\|\s*id == BlockId::\w+)+\s*\)$') {
            # Several ids sharing one model. The first names the file; the
            # others are the same geometry under another texture, which is what
            # a soul campfire is.
            $ids = [regex]::Matches($frame, 'BlockId::(\w+)')
            $label = ($ids | ForEach-Object { $_.Groups[1].Value }) -join '/'
            $modelKey = $ids[0].Groups[1].Value
        } else {
            $predicate = $frame -replace '^if \(\s*', ''
            $predicate = ($predicate -replace '\s*\)$', '').Trim()
            if ($familyModels.ContainsKey($predicate)) {
                $label = $predicate -replace '^is', ''
                $label = $label -replace '\(id\)$', ''
                $modelKey = $familyModels[$predicate]
            } else {
                $unread++
                continue
            }
        }
        $fields = Split-Fields $statement.Init
        $numbers = New-Object System.Collections.ArrayList
        if ($fields.Count -gt 0 -and $fields[0].Trim().StartsWith('{')) {
            $inner = $fields[0].Trim()
            $inner = $inner.Substring(1, $inner.Length - 2)
            foreach ($field in (Split-Fields $inner)) { [void]$numbers.Add($field) }
            for ($k = 1; $k -lt $fields.Count; $k++) { [void]$numbers.Add($fields[$k]) }
        } else {
            foreach ($field in $fields) { [void]$numbers.Add($field) }
        }
        if ($numbers.Count -lt 14) { $unread++; continue }
        $values = New-Object System.Collections.ArrayList
        $ok = $true
        for ($k = 0; $k -lt 14; $k++) {
            $value = Get-Number $numbers[$k]
            if ($null -eq $value) { $ok = $false; break }
            [void]$values.Add($value * 16.0)
        }
        if (-not $ok) { $unread++; continue }
        if (-not $models.ContainsKey($label)) {
            $models[$label] = [PSCustomObject]@{
                Label = $label
                Key = $modelKey
                Boxes = @{}
                Dropped = $false
            }
        }
        $entry = $models[$label]
        # **Two writes to one index is a branch, and a branch means the box we
        # read is only one of the shapes this block can be.** Rather than pick,
        # the id is dropped: an honest gap is worth more than a comparison that
        # might be against the wrong state.
        if ($entry.Boxes.ContainsKey($statement.Index)) { $entry.Dropped = $true; continue }
        $entry.Boxes[$statement.Index] = [PSCustomObject]@{
            Box = @($values[0], $values[1], $values[2], $values[3], $values[4], $values[5])
            Side = Get-Rect @($values[6], $values[7], $values[8], $values[9])
            Lid = Get-Rect @($values[10], $values[11], $values[12], $values[13])
        }
    }
    $script:ourModelCache = [PSCustomObject]@{ Models = $models; Unread = $unread }
    return $script:ourModelCache
}

# Blocks that disagree with the reference on purpose. Named rather than filtered
# by a wider tolerance, because a *different* disagreement on one of these is
# still a fault worth seeing.
$known = @{
    'Bell'           = 'our model includes the bell itself, which the reference draws as a block entity and leaves out of bell_floor.json'
    'TNT'            = 'primed TNT is lifted one texel on purpose, and a static_assert holds it there'
    'Piston Head'    = 'the reference runs its arm four texels outside its own cell, back into the piston that pushed it; ours stops at the cell wall, which is the short head the reference also ships'
    'Redstone Torch' = 'the reference adds zero-thickness glow quads round the head; ours paints the head on the stick'
    # The named divergence postModel states for its whole first family: the box
    # mesher has no rotation, so where the reference turns a handle we slide it.
    'Lever'          = 'the reference turns the handle 45 degrees; ours slides it, which is the documented no-tilt divergence covering this whole family'
    'Tripwire Hook'  = 'the reference hangs the hook at 45 degrees; ours is the same hook square to the wall, and the box mesher has no rotation'
    # The same no-tilt divergence, three more times, and each one's whole
    # difference is a turned element the `unexpressible` list below cannot
    # attribute because the *rest* of the model stops short of where we draw.
    'Lectern'        = 'the reference tilts the desk 22.5 degrees about x, which lifts its far edge to 18.45 - out of the top of the cell (lectern.json); ours lays the same 12-to-16 desk flat'
    'Spore Blossom'  = 'four of spore_blossom.json''s five elements are paper-thin planes turned 22.5 degrees that run eight texels outside the cell on every side; ours is the crossed pair, which has neither the tilt nor the overhang'
    'Small Dripleaf' = 'every element of small_dripleaf_top.json is a paper-thin plane and the tallest is a stem swung 45 degrees to y14; ours is the crossed pair, which spans the cell'
    'Big Dripleaf'   = 'big_dripleaf.json is a paper-thin leaf at y15 over two stems swung 45 degrees; ours is the crossed pair, which spans the cell'
}
# **The standing sign, once per wood**, because `$known` is keyed by the display
# name and a sign and a hanging sign are two of them. Written as a loop rather
# than eleven identical rows: the reason is one reason, and eleven copies of it
# is eleven places for it to rot. The wall form has no overhang and matches the
# reference exactly, so only the standing board is named here.
foreach ($wood in 'Oak', 'Spruce', 'Birch', 'Jungle', 'Acacia', 'Dark Oak', 'Mangrove', 'Cherry',
                  'Bamboo', 'Crimson', 'Warped') {
    $known["$wood Sign"] = 'template_sign_rot_0''s board reaches y17.33, 1.33 texels out of the top of its own cell; ours is the same board clipped to the cell wall, because a Sign box takes its texture from where it sits in the cell and the block sampler repeats'
}
# The same idea for the two new checks. A box count and a rectangle are
# different claims, so a block deliberately built from a different number of
# boxes is not thereby allowed a wrong rectangle.
$knownCount = @{
    'Bell'          = 'the reference splits the bell across bell_floor.json and a block entity; ours draws the whole thing'
    'Brewing Stand' = 'the three bottle arms are one box each in the reference and are documented in-source as omitted'
    'Cauldron'      = 'four corner squares where the reference has eight L-shaped feet, because ModelBoxes holds ten'
    'Composter'     = 'four corner squares where the reference has eight L-shaped feet, because ModelBoxes holds ten'
    'Piston Head'   = 'ours is the short head, which the reference ships as its own model with a different element count'
    'Tripwire Hook' = 'the reference builds the hook itself from six turned and paper-thin pieces; ours is two boxes, which is the same no-tilt divergence its extent is named for'
}
$knownUv = @{
    'Grindstone' = 'two texture divergences documented in-source: our pivot and leg art is not split the way the reference splits it'
    'Bell'       = 'measured from bell_side.png, because bell_floor.json omits the bell body entirely'
}

# ---- 5. Compare. ------------------------------------------------------------
$rows = @()
$noModel = 0
$matched = 0
$bad = @()
$expected = @()
$unfound = @{}
$noGeometry = @{}
$faceTemplate = @{}
$crossFamily = 0
$unexpressible = @{}
$countChecked = 0
$countBad = @()
$countKnown = 0

foreach ($line in Get-Content -Encoding UTF8 $dump) {
    if ($line.StartsWith('#')) { continue }
    $parts = $line -split "`t"
    if ($parts.Count -lt 10) { continue }
    # Parenthesised casts: `[double]$parts[4] * 16` binds the cast to the whole
    # expression and hands the multiply an array.
    $lo = @((([double]$parts[4]) * 16), (([double]$parts[5]) * 16), (([double]$parts[6]) * 16))
    $hi = @((([double]$parts[7]) * 16), (([double]$parts[8]) * 16), (([double]$parts[9]) * 16))
    $row = [PSCustomObject]@{
        Id    = [int]$parts[0]
        Name  = $parts[1]
        Shape = $parts[2]
        Boxes = [int]$parts[3]
        Lo    = $lo
        Hi    = $hi
    }
    $rows += $row

    $modelName = Get-ModelName $row.Name
    # The reference ships several models per block where we ship several ids: a
    # trapdoor has a bottom, a top and an open one, a fence gate has an open and
    # a wall-mounted pair, a stair has inner and outer corners. Our display name
    # is the same for all of them, so the closest of the set is the one this id
    # actually is. Anything with no variants falls straight through the loop.
    $best = $null
    $bestRank = 0.0
    $bestName = $modelName
    $bestModel = $null
    $sawFile = $false
    $sawGeometry = $false
    $sawFlat = $false
    foreach ($candidate in (Get-Candidates $modelName)) {
        $reference = Get-ReferenceModel $candidate
        if ($null -eq $reference.Box) {
            if ($reference.Why -eq 'nogeometry') { $sawFile = $true; $sawGeometry = $true }
            elseif ($reference.Why -eq 'facetemplate') { $sawFile = $true; $sawFlat = $true }
            continue
        }
        $sawFile = $true
        $score = Get-Worst $row $reference.Box
        # **Which variant this id is, judged both ways.** A cocoa pod hangs off
        # a zero-thickness stem the mesher cannot draw, so every one of its
        # three ages scores the same four texels against the full union and the
        # tie handed all twelve rows to `_stage0`. Ranking on whichever of the
        # two unions fits better picks the age the row actually is.
        $rank = $score.Worst
        if ($null -ne $reference.SolidBox) {
            $solid = Get-Worst $row $reference.SolidBox
            if ($solid.Worst -lt $rank) { $rank = $solid.Worst }
        }
        if ($null -eq $best -or $rank -lt $bestRank) {
            $best = $score
            $bestRank = $rank
            $bestName = $candidate
            $bestModel = $reference
        }
    }
    if ($null -eq $best) {
        $noModel++
        # **Three reasons, and they used to be one number.** A chest has a model
        # file with nothing in it because the reference draws it as a block
        # entity; a lichen's model is a single face the blockstate places six
        # ways; and a name we simply failed to find is a hole in the table above
        # and is the only one of the three anybody can fix.
        $bucket = if ($sawGeometry) { $noGeometry } elseif ($sawFlat) { $faceTemplate } else { $unfound }
        if (-not $bucket.ContainsKey($modelName)) { $bucket[$modelName] = 0 }
        $bucket[$modelName]++
        if ($Verbose3) { Write-Host ("no model  {0,-34} tried {1}" -f $row.Name, $modelName) -ForegroundColor DarkGray }
        continue
    }

    if ($best.Worst -gt $Tolerance) {
        if ($known.ContainsKey($row.Name)) {
            $expected += [PSCustomObject]@{ Name = $row.Name; Worst = $best.Worst; Detail = $best.Detail; Why = $known[$row.Name] }
        } elseif ($bestModel.CrossLike) {
            $crossFamily++
        } else {
            # Does the whole difference lie in the parts a box mesher cannot
            # express? Measured, not assumed: if our extent agrees with the
            # model's solid unrotated elements, then what we are missing is
            # exactly its turned and paper-thin ones, which is a fact about the
            # mesher and is reported as such rather than as a fault.
            $attributed = $false
            if ($null -ne $bestModel.SolidBox -and ($bestModel.Rotated -or $bestModel.Thin)) {
                $solidScore = Get-Worst $row $bestModel.SolidBox
                if ($solidScore.Worst -le $Tolerance) {
                    $attributed = $true
                    if (-not $unexpressible.ContainsKey($row.Name)) {
                        $unexpressible[$row.Name] = [PSCustomObject]@{
                            Rows = 0
                            Model = $bestName
                            Missing = ($bestModel.Count - $bestModel.SolidCount)
                        }
                    }
                    $unexpressible[$row.Name].Rows++
                }
            }
            if (-not $attributed) {
                $bad += [PSCustomObject]@{ Name = $row.Name; Shape = $row.Shape; Model = $bestName; Worst = $best.Worst; Detail = $best.Detail }
            }
        }
    } else {
        $matched++
        if ($Verbose3) { Write-Host ("ok        {0,-34} {1}" -f $row.Name, $bestName) -ForegroundColor DarkGreen }
    }

    # ---- how many boxes, not just how big. ----
    # Only where both sides mean the same thing: a `Model` block is drawn from
    # the boxes `postModel` names, one for one. Every other shape is meshed from
    # a rule and its box count is a collision count, which the reference does
    # not state at all.
    #
    # A model holding turned or paper-thin elements is counted against **both**
    # its totals - all of them, and only the ones a box could be - and disagrees
    # only when it matches neither. That is what lets a scaffolding's four
    # missing rails be seen while a campfire's two flame quads are not called a
    # fault for being absent.
    if ($row.Shape -eq 'Model' -and $bestModel.SolidCount -gt 0) {
        $countChecked++
        if ($row.Boxes -ne $bestModel.Count -and $row.Boxes -ne $bestModel.SolidCount) {
            if ($knownCount.ContainsKey($row.Name)) {
                $countKnown++
            } else {
                $countBad += [PSCustomObject]@{
                    Name = $row.Name; Ours = $row.Boxes; Reference = $bestModel.Count
                    Solid = $bestModel.SolidCount; Model = $bestName
                }
            }
        }
    }
}

# ---- 6. Rectangles, which is where the two that got through were hiding. ----
$ours = Get-OurModels
$uvChecked = 0
$uvBad = @()
$uvKnown = 0
$uvSkipped = 0
if ($null -ne $ours) {
    foreach ($label in ($ours.Models.Keys | Sort-Object)) {
        $entry = $ours.Models[$label]
        if ($entry.Dropped) { $uvSkipped += $entry.Boxes.Count; continue }
        $key = Get-ModelName ($entry.Key -creplace '(?<!^)([A-Z])', '_$1')
        # The variant whose elements our boxes actually sit in. A hopper has six
        # models and only one of them is the shape we drew.
        $bestName = $null
        $bestHits = -1
        $bestReference = $null
        foreach ($candidate in (Get-Candidates $key)) {
            $reference = Get-ReferenceModel $candidate
            if ($null -eq $reference.Box) { continue }
            $hits = 0
            foreach ($index in $entry.Boxes.Keys) {
                $box = $entry.Boxes[$index].Box
                foreach ($element in $reference.Boxes) {
                    $same = $true
                    for ($a = 0; $a -lt 3; $a++) {
                        if ([Math]::Abs($box[$a] - $element.From[$a]) -gt 0.05) { $same = $false }
                        if ([Math]::Abs($box[$a + 3] - $element.To[$a]) -gt 0.05) { $same = $false }
                    }
                    if ($same) { $hits++; break }
                }
            }
            if ($hits -gt $bestHits) { $bestHits = $hits; $bestName = $candidate; $bestReference = $reference }
        }
        if ($null -eq $bestReference -or $bestHits -le 0) { $uvSkipped += $entry.Boxes.Count; continue }

        foreach ($index in ($entry.Boxes.Keys | Sort-Object)) {
            $box = $entry.Boxes[$index]
            $element = $null
            foreach ($candidate in $bestReference.Boxes) {
                if ($candidate.Rotated) { continue }
                $same = $true
                for ($a = 0; $a -lt 3; $a++) {
                    if ([Math]::Abs($box.Box[$a] - $candidate.From[$a]) -gt 0.05) { $same = $false }
                    if ([Math]::Abs($box.Box[$a + 3] - $candidate.To[$a]) -gt 0.05) { $same = $false }
                }
                if ($same) { $element = $candidate; break }
            }
            # No element in the same place is not a fault: it is our box being
            # somewhere the reference's is not, which the extent check above
            # already had its say about.
            if ($null -eq $element) { $uvSkipped++; continue }

            $sides = @()
            foreach ($face in @('north', 'south', 'east', 'west')) {
                if ($element.Faces.ContainsKey($face)) { $sides += , $element.Faces[$face] }
            }
            $lids = @()
            foreach ($face in @('up', 'down')) {
                if ($element.Faces.ContainsKey($face)) { $lids += , $element.Faces[$face] }
            }
            $problems = @()
            if ($sides.Count -gt 0) {
                $uvChecked++
                $hit = $false
                foreach ($rect in $sides) { if (Test-SameRect $box.Side $rect) { $hit = $true } }
                if (-not $hit) {
                    $problems += ("side ours [{0}] ref {1}" -f (($box.Side | ForEach-Object { [Math]::Round($_, 2) }) -join ','),
                                  (($sides | ForEach-Object { '[' + (($_ | ForEach-Object { [Math]::Round($_, 2) }) -join ',') + ']' }) -join ' '))
                }
            }
            if ($lids.Count -gt 0) {
                $uvChecked++
                $hit = $false
                foreach ($rect in $lids) { if (Test-SameRect $box.Lid $rect) { $hit = $true } }
                if (-not $hit) {
                    $problems += ("lid ours [{0}] ref {1}" -f (($box.Lid | ForEach-Object { [Math]::Round($_, 2) }) -join ','),
                                  (($lids | ForEach-Object { '[' + (($_ | ForEach-Object { [Math]::Round($_, 2) }) -join ',') + ']' }) -join ' '))
                }
            }
            foreach ($problem in $problems) {
                if ($knownUv.ContainsKey($label)) {
                    $uvKnown++
                } else {
                    $uvBad += [PSCustomObject]@{
                        Name = $label; Index = $index; Model = $bestName; Detail = $problem
                    }
                }
            }
        }
    }
}

Write-Output ""
Write-Output ("{0} blocks drawn, {1} matched a reference model and agree, {2} disagree, {3} have no model to check against" -f $rows.Count, $matched, $bad.Count, $noModel)
$unfoundRows = 0
$unfound.Values | ForEach-Object { $unfoundRows += $_ }
$noGeometryRows = 0
$noGeometry.Values | ForEach-Object { $noGeometryRows += $_ }
$faceTemplateRows = 0
$faceTemplate.Values | ForEach-Object { $faceTemplateRows += $_ }
Write-Output ("of those {0}: {1} the reference draws as a block entity and ships no geometry for, {2} it ships only as a single face template, {3} we failed to find a model for" -f $noModel, $noGeometryRows, $faceTemplateRows, $unfoundRows)
Write-Output ("{0} Model blocks compared box for box, {1} disagree on how many; {2} rectangles read out of Block.hpp and compared, {3} disagree" -f $countChecked, $countBad.Count, $uvChecked, $uvBad.Count)
if ($null -ne $stale) {
    Write-Output ""
    Write-Output ("STALE: " + $stale)
}

if ($expected.Count -gt 0 -or $crossFamily -gt 0 -or $countKnown -gt 0 -or $uvKnown -gt 0) {
    Write-Output ""
    Write-Output "Deliberate divergences, which is what these numbers are meant to look like:"
    # One line per block, not per state. A lever has sixteen rows and one
    # reason, and printing the reason sixteen times buries the three lines
    # underneath it that are about something else.
    $expected | Group-Object Name | Sort-Object Name | ForEach-Object {
        $worst = ($_.Group | Measure-Object -Property Worst -Maximum).Maximum
        Write-Output ("  {0,5:N2} texels  {1,-18} {2,4} rows  {3}" -f $worst, $_.Name, $_.Count, $_.Group[0].Why)
    }
    if ($crossFamily -gt 0) {
        Write-Output ("  {0,12}  {1,-18} {2,4} rows  {3}" -f '', 'crossed pair', $crossFamily,
                      'the reference swings two planes 45 degrees and rescales them to an 0.8 texel inset; ours draws the pair corner to corner')
    }
    if ($countKnown -gt 0) {
        Write-Output ("  {0,12}  {1,-18} {2,4} rows  {3}" -f '', 'box count', $countKnown, 'named in $knownCount and listed there with the reason')
    }
    if ($uvKnown -gt 0) {
        Write-Output ("  {0,12}  {1,-18} {2,4} rects {3}" -f '', 'rectangle', $uvKnown, 'named in $knownUv and listed there with the reason')
    }
}

if ($bad.Count -gt 0) {
    Write-Output ""
    Write-Output "Disagreements, worst first. A shape family listed many times over is one fault, not many:"
    # **Collapsed to one line per fault, because that is what a fault is.** The
    # eleven woods times eight states are one wrong number in one branch, and
    # eighty-eight identical lines only make the two blocks underneath them
    # harder to see. `-Verbose3` prints every row.
    $bad | Group-Object { $_.Shape + '|' + $_.Detail } |
        Sort-Object -Property @{ Expression = { ($_.Group | Measure-Object -Property Worst -Maximum).Maximum } } -Descending |
        ForEach-Object {
            $first = $_.Group[0]
            $names = @($_.Group | ForEach-Object { $_.Name } | Select-Object -Unique)
            $label = $names[0]
            if ($names.Count -gt 1) { $label = "$($names[0]) +$($names.Count - 1) more" }
            $worst = ($_.Group | Measure-Object -Property Worst -Maximum).Maximum
            Write-Output ("  {0,5:N2} texels  {1,-34} [{2}]  {3,4} rows  {4}  vs {5}" -f $worst, $label, $first.Shape, $_.Count, $first.Detail, $first.Model)
        }
    if ($Verbose3) {
        Write-Output ""
        Write-Output "Every disagreeing row:"
        $bad | Sort-Object -Property Worst -Descending | ForEach-Object {
            Write-Output ("  {0,5:N2} texels  {1,-30} [{2}]  {3}  vs {4}" -f $_.Worst, $_.Name, $_.Shape, $_.Detail, $_.Model)
        }
    }
    Write-Output ""
    Write-Output "By shape:"
    $bad | Group-Object Shape | Sort-Object Count -Descending | ForEach-Object {
        Write-Output ("  {0,5} x {1}" -f $_.Count, $_.Name)
    }
}

if ($countBad.Count -gt 0) {
    Write-Output ""
    Write-Output "Box counts that disagree. A box missing inside the union is invisible to every other check here:"
    $countBad | Sort-Object -Property Name -Unique | ForEach-Object {
        Write-Output ("  {0,-30} we draw {1}, {2}.json has {3} ({4} of them a plain box)" -f $_.Name, $_.Ours, $_.Model, $_.Reference, $_.Solid)
    }
}

if ($unexpressible.Count -gt 0) {
    Write-Output ""
    Write-Output "Extents whose whole difference is the reference's turned or paper-thin elements, which a box mesher cannot express. Ours agrees with the rest of the model exactly:"
    $unexpressible.GetEnumerator() | Sort-Object -Property { $_.Value.Rows } -Descending | ForEach-Object {
        Write-Output ("  {0,5} rows  {1,-28} {2} of {3}.json's elements are turned or paper-thin" -f $_.Value.Rows, $_.Key, $_.Value.Missing, $_.Value.Model)
    }
}

if ($uvBad.Count -gt 0) {
    Write-Output ""
    Write-Output "Rectangles that disagree, box by box. Ours has one rect per four sides and one per lid, so this only fires when it matches none of them:"
    $uvBad | ForEach-Object {
        Write-Output ("  {0,-22} box {1}  {2}  vs {3}" -f $_.Name, $_.Index, $_.Detail, $_.Model)
    }
}

if ($unfound.Count -gt 0) {
    Write-Output ""
    Write-Output "Names we looked up and did not find, worst first. Each is either a missing rename above or a block the reference has not got:"
    $unfound.GetEnumerator() | Sort-Object -Property Value -Descending | Select-Object -First 30 | ForEach-Object {
        Write-Output ("  {0,5} rows  {1}" -f $_.Value, $_.Key)
    }
    if ($unfound.Count -gt 30) {
        Write-Output ("  ... and {0} more names" -f ($unfound.Count - 30))
    }
}

if ($Verbose3 -and $noGeometry.Count -gt 0) {
    Write-Output ""
    Write-Output "Block entities the reference ships no geometry for, which is correct and unfixable here:"
    $noGeometry.GetEnumerator() | Sort-Object -Property Value -Descending | ForEach-Object {
        Write-Output ("  {0,5} rows  {1}" -f $_.Value, $_.Key)
    }
}

if ($null -ne $ours -and $ours.Unread -gt 0) {
    Write-Output ""
    Write-Output ("{0} boxes in postModel could not be read statically - behind a switch, a loop or a variable - and {1} more had no element in the same place to compare against. Neither is counted as agreeing." -f $ours.Unread, $uvSkipped)
}

exit 0
