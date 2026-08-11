# Checks every block's measurements against the reference's own block models.
#
# The single most expensive bug shape on this project is a rectangle or an
# extent guessed out of a picture instead of read from the model that states it.
# The reference ships `models/block/*.json` for every block it has, each naming
# its elements' `from` and `to` in sixteenths - so the true size of nearly every
# block we have is already sitting in the repo, and nothing was reading it.
#
#   powershell -NoProfile -File tools\check-models.ps1
#   powershell -NoProfile -File tools\check-models.ps1 -Verbose3
#
# It runs the game once with `block_probe=1`, which writes `block-shapes.txt`
# beside the exe and exits without opening a window, then compares the union box
# of what we draw against the union box the reference's elements describe.
# settings.cfg is backed up and restored in the same command, because a soak
# once left a setting behind and the user filed a bug against a working system.

param(
    [string]$Build = "release",
    # Difference in sixteenths before a block is called wrong. Half a texel is
    # below anything anyone can see and above every rounding artefact.
    [double]$Tolerance = 0.5,
    # Also list the blocks that match, and the ones with no reference model.
    [switch]$Verbose3
)

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$binDir = Join-Path $root "build\$Build\bin"
$exe = Join-Path $binDir 'game.exe'
$config = Join-Path $binDir 'settings.cfg'
$dump = Join-Path $binDir 'block-shapes.txt'
$modelDir = Join-Path $root 'reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\models\block'

if (-not (Test-Path $exe)) { Write-Error "no build at $exe"; exit 1 }
if (-not (Test-Path $modelDir)) { Write-Error "no reference models at $modelDir"; exit 1 }

# ---- 1. Ask the game what it draws. ----------------------------------------
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

# ---- 2. Read the reference models, following parents for elements. ---------
$cache = @{}

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

function Get-ReferenceBox([string]$name) {
    $elements = Get-Elements $name
    if ($null -eq $elements) { return $null }
    $lo = @(99.0, 99.0, 99.0)
    $hi = @(-99.0, -99.0, -99.0)
    $any = $false
    foreach ($element in $elements) {
        if ($null -eq $element.from -or $null -eq $element.to) { continue }
        # An element carrying a `rotation` is placed by turning it, so its
        # written extent is not where it ends up - a coral fan and a mushroom
        # block are both built from flat planes swung into position. Comparing
        # those numbers would report every one of them as wrong.
        if ($null -ne $element.rotation) { return $null }
        for ($a = 0; $a -lt 3; $a++) {
            $f = [double]$element.from[$a]
            $t = [double]$element.to[$a]
            $lo[$a] = [Math]::Min($lo[$a], [Math]::Min($f, $t))
            $hi[$a] = [Math]::Max($hi[$a], [Math]::Max($f, $t))
        }
        $any = $true
    }
    if (-not $any) { return $null }
    # A model with no thickness at all is a *face* template - `template_single_face`
    # and its kin - which the blockstate places six ways to build a mushroom
    # block or a patch of lichen. Its written extent is one of those faces, not
    # the block, so there is nothing here to compare against.
    for ($a = 0; $a -lt 3; $a++) {
        if ($hi[$a] - $lo[$a] -lt 0.001) { return $null }
    }
    return [PSCustomObject]@{ Lo = $lo; Hi = $hi }
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
$rename = @{
    'emberite'       = 'netherite'
    'stowbox'        = 'shulker_box'
    'bramble'        = 'creeper'
    'void_pearl'     = 'ender_pearl'
    'monster_spawner' = 'spawner'
    'top_snow'       = 'snow'
    'grass'          = 'short_grass'
    'block_of_iron'  = 'iron_block'
    'block_of_gold'  = 'gold_block'
    'block_of_diamond' = 'diamond_block'
    'block_of_coal'  = 'coal_block'
    'block_of_copper' = 'copper_block'
    'block_of_emerald' = 'emerald_block'
    'block_of_redstone' = 'redstone_block'
    'block_of_lapis_lazuli' = 'lapis_block'
    'block_of_emberite' = 'netherite_block'
}

function Get-ModelName([string]$display) {
    $key = $display.ToLowerInvariant() -replace "[^a-z0-9]+", '_'
    $key = $key.Trim('_')
    if ($rename.ContainsKey($key)) { return $rename[$key] }
    foreach ($from in $rename.Keys) {
        if ($key -like "*$from*") { return ($key -replace [regex]::Escape($from), $rename[$from]) }
    }
    return $key
}

# Blocks that disagree with the reference on purpose. Named rather than filtered
# by a wider tolerance, because a *different* disagreement on one of these is
# still a fault worth seeing.
$known = @{
    'Bell'           = 'our model includes the bell itself, which the reference draws as a block entity and leaves out of bell_floor.json'
    'TNT'            = 'primed TNT is lifted one texel on purpose, and a static_assert holds it there'
    'Piston Head'    = 'the reference runs its arm four texels outside its own cell, back into the piston that pushed it; ours stops at the cell wall, which is the short head the reference also ships'
    'Redstone Torch' = 'the reference adds zero-thickness glow quads round the head; ours paints the head on the stick'
}

# ---- 4. Compare. ------------------------------------------------------------
$rows = @()
$noModel = 0
$matched = 0
$bad = @()
$expected = @()

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
    # A block often ships several models and only one of them is the state we
    # dumped. `_pressed` and `_down` matter as much as the rest: without them a
    # pushed button is measured against an unpushed one and reports a texel of
    # difference that is the whole point of the state.
    $suffixes = @('', '_post', '_top', '_bottom', '_open', '_wall', '_wall_open', '_inner', '_outer',
                  '_side', '_floor', '_ceiling', '_pressed', '_down', '_on', '_lit', '_rot_0')
    $best = $null
    $bestName = $modelName
    foreach ($suffix in $suffixes) {
        $candidate = $modelName + $suffix
        $reference = Get-ReferenceBox $candidate
        if ($null -eq $reference) { continue }
        $score = Get-Worst $row $reference
        if ($null -eq $best -or $score.Worst -lt $best.Worst) { $best = $score; $bestName = $candidate }
    }
    if ($null -eq $best) { $noModel++; if ($Verbose3) { Write-Host ("no model  {0,-34} tried {1}" -f $row.Name, $modelName) -ForegroundColor DarkGray }; continue }

    if ($best.Worst -gt $Tolerance) {
        if ($known.ContainsKey($row.Name)) {
            $expected += [PSCustomObject]@{ Name = $row.Name; Worst = $best.Worst; Detail = $best.Detail; Why = $known[$row.Name] }
        } else {
            $bad += [PSCustomObject]@{ Name = $row.Name; Shape = $row.Shape; Model = $bestName; Worst = $best.Worst; Detail = $best.Detail }
        }
    } else {
        $matched++
        if ($Verbose3) { Write-Host ("ok        {0,-34} {1}" -f $row.Name, $bestName) -ForegroundColor DarkGreen }
    }
}

Write-Output ""
Write-Output ("{0} blocks drawn, {1} matched a reference model and agree, {2} disagree, {3} have no model to check against" -f $rows.Count, $matched, $bad.Count, $noModel)

if ($expected.Count -gt 0) {
    Write-Output ""
    Write-Output "Deliberate divergences, which is what these numbers are meant to look like:"
    $expected | ForEach-Object {
        Write-Output ("  {0,5:N2} texels  {1,-18} {2}" -f $_.Worst, $_.Name, $_.Why)
    }
}

if ($bad.Count -gt 0) {
    Write-Output ""
    Write-Output "Disagreements, worst first. A shape family listed many times over is one fault, not many:"
    $bad | Sort-Object -Property Worst -Descending | ForEach-Object {
        Write-Output ("  {0,5:N2} texels  {1,-30} [{2}]  {3}  vs {4}" -f $_.Worst, $_.Name, $_.Shape, $_.Detail, $_.Model)
    }
    Write-Output ""
    Write-Output "By shape:"
    $bad | Group-Object Shape | Sort-Object Count -Descending | ForEach-Object {
        Write-Output ("  {0,5} x {1}" -f $_.Count, $_.Name)
    }
}

exit 0
