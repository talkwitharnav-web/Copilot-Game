# Builds a temporary proof atlas: our own hud.png with the reference's real UI
# crops laid over the regions whose art has not been authored yet.
#
# It exists to prove the *layout* - card widths, the fold between them, the tab
# pitch and the grid - against pixels that are known to be right, so that a tab
# looking wrong can only mean our geometry is wrong. Authoring our own art and
# judging the layout by it at the same time is the same mistake as debugging a
# box net and a skin together: two unknowns, one symptom.
#
# Everything else keeps OUR art. The panel frames come from the user's own
# concept mock-up and are finished; covering them would throw away working art
# to answer a question nobody asked.
#
# It lands beside each built game.exe as hud-reference.png, which the game loads
# in place of assets/textures/hud.png whenever it exists. Delete those files to
# go back to our own art everywhere. Like every other reference staging tool in
# this project, it refuses to write under assets/.

param(
    [string[]]$OutputPath = @(
        "build\debug\bin\hud-reference.png",
        "build\release\bin\hud-reference.png"
    )
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$sheetPath = Join-Path $root "assets\textures\hud.png"
$iconDir = Join-Path $root "reference\ui-icons"

$assetsRoot = [System.IO.Path]::GetFullPath((Join-Path $root "assets"))
$resolvedOutputs = foreach ($candidate in $OutputPath) {
    $full = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $candidate))
    if ($full.StartsWith($assetsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Reference HUD atlas must stay outside assets/: $full"
    }
    $full
}

if (-not (Test-Path $sheetPath)) {
    throw "Missing $sheetPath - run tools\make-hud-sheet.ps1 first."
}

# Must match the tab region make-hud-sheet.ps1 reports and the constants in
# game/src/hud/InventoryScreen.cpp. A wrong offset here does not fail, it
# silently mistextures the strip.
$tabsLeft = 0
$tabsTop = 721
$tabWidth = 22
$tabHeight = 25
$tabPitch = 24
$tabRowPitch = 27

# In CatalogueTab order.
$tabs = @('tab-construction', 'tab-equipment', 'tab-items', 'tab-nature', 'tab-search')

# The capture caught Construction *selected* and the other four unselected, so
# neither state exists for all five. Measured off the crops: a tab's interior
# fill is 198 grey when selected (the panel's own face colour, so it reads as
# part of the card) and 98 when not, over x 2..20, y 3..24. That single fill is
# the whole difference in the reference art - there is no glow and no accent -
# so the missing state is recovered by repainting the fill rather than by
# drawing anything.
$interiorLeft = 2
$interiorRight = 20
$interiorTop = 3
$interiorBottom = 24
$bottomEdgeTop = 23
$fillTolerance = 22
$selectedFill = 198
$unselectedFill = 98

function Test-NearGrey {
    param([System.Drawing.Color]$Color, [int]$Level, [int]$Tolerance)

    return ([Math]::Abs($Color.R - $Level) -le $Tolerance) -and
           ([Math]::Abs($Color.G - $Level) -le $Tolerance) -and
           ([Math]::Abs($Color.B - $Level) -le $Tolerance)
}

# Flooded inward from the interior's own edge rather than recoloured in bulk, so
# a grey pixel that happens to be part of the icon - a sword blade, a bucket -
# is never reached and never repainted.
function Convert-TabState {
    param([System.Drawing.Bitmap]$Crop, [int]$FromLevel, [int]$ToLevel,
          [System.Drawing.Bitmap]$BottomDonor)

    $out = New-Object System.Drawing.Bitmap -ArgumentList $Crop.Width, $Crop.Height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $Crop.Height; $y++) {
        for ($x = 0; $x -lt $Crop.Width; $x++) {
            $out.SetPixel($x, $y, $Crop.GetPixel($x, $y))
        }
    }

    $target = [System.Drawing.Color]::FromArgb(255, $ToLevel, $ToLevel, $ToLevel)
    $seen = New-Object 'bool[]' ($Crop.Width * $Crop.Height)
    $queue = New-Object System.Collections.Generic.Queue[object]
    for ($x = $interiorLeft; $x -le $interiorRight; $x++) {
        $queue.Enqueue(@($x, $interiorTop))
        $queue.Enqueue(@($x, $interiorBottom))
    }
    for ($y = $interiorTop; $y -le $interiorBottom; $y++) {
        $queue.Enqueue(@($interiorLeft, $y))
        $queue.Enqueue(@($interiorRight, $y))
    }

    while ($queue.Count -gt 0) {
        $point = $queue.Dequeue()
        $px = $point[0]
        $py = $point[1]
        if ($px -lt $interiorLeft -or $px -gt $interiorRight -or
            $py -lt $interiorTop -or $py -gt $interiorBottom) {
            continue
        }
        $index = $py * $Crop.Width + $px
        if ($seen[$index]) { continue }
        $seen[$index] = $true
        if (-not (Test-NearGrey -Color $out.GetPixel($px, $py) -Level $FromLevel -Tolerance $fillTolerance)) {
            continue
        }
        $out.SetPixel($px, $py, $target)
        $queue.Enqueue(@(($px - 1), $py))
        $queue.Enqueue(@(($px + 1), $py))
        $queue.Enqueue(@($px, ($py - 1)))
        $queue.Enqueue(@($px, ($py + 1)))
    }

    # The bottom edge belongs to the state, not to the tab: a selected one has
    # none so it can merge into the card below, an unselected one has a border.
    # Copied from a tab captured in the state we want.
    for ($y = $bottomEdgeTop; $y -le $interiorBottom; $y++) {
        for ($x = 0; $x -lt $Crop.Width; $x++) {
            $out.SetPixel($x, $y, $BottomDonor.GetPixel($x, $y))
        }
    }
    return $out
}

$sheet = [System.Drawing.Bitmap]::FromFile($sheetPath)
$atlas = New-Object System.Drawing.Bitmap -ArgumentList $sheet.Width, $sheet.Height,
    ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($atlas)
$graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$graphics.InterpolationMode = 'NearestNeighbor'
$graphics.PixelOffsetMode = 'Half'
$graphics.DrawImage($sheet, 0, 0, $sheet.Width, $sheet.Height)

$replaced = 0
$crops = @{}
foreach ($name in $tabs) {
    $cropPath = Join-Path $iconDir "$name.png"
    if (-not (Test-Path $cropPath)) {
        Write-Warning "Missing $cropPath - that tab keeps our own art."
        continue
    }
    $crop = [System.Drawing.Bitmap]::FromFile($cropPath)

    # Never resized. The crops measure exactly 22x25, which is the check that
    # the capture really is vanilla's layout at 3x; anything else means the
    # extraction drifted and compositing it would hide that behind a rescale.
    if ($crop.Width -ne $tabWidth -or $crop.Height -ne $tabHeight) {
        $crop.Dispose()
        throw "$name is $($crop.Width)x$($crop.Height), expected ${tabWidth}x${tabHeight}"
    }
    $crops[$name] = $crop
}

if ($crops.Count -eq $tabs.Count) {
    $selectedDonor = $crops['tab-construction']
    $unselectedDonor = $crops['tab-nature']

    for ($index = 0; $index -lt $tabs.Count; $index++) {
        $crop = $crops[$tabs[$index]]
        # Construction is the one the capture caught selected.
        $sourceLevel = if ($index -eq 0) { $selectedFill } else { $unselectedFill }

        for ($row = 0; $row -lt 2; $row++) {
            $selected = $row -eq 1
            $targetLevel = if ($selected) { $selectedFill } else { $unselectedFill }
            $donor = if ($selected) { $selectedDonor } else { $unselectedDonor }
            $state = Convert-TabState -Crop $crop -FromLevel $sourceLevel -ToLevel $targetLevel -BottomDonor $donor

            $destX = $tabsLeft + $index * $tabPitch
            $destY = $tabsTop + $row * $tabRowPitch
            $graphics.SetClip((New-Object System.Drawing.Rectangle $destX, $destY, $tabWidth, $tabHeight))
            $graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
            $graphics.DrawImage($state, $destX, $destY, $tabWidth, $tabHeight)
            $graphics.ResetClip()
            $state.Dispose()
        }
        $replaced++
    }
}
foreach ($crop in $crops.Values) { $crop.Dispose() }

# The ten survival status icons, straight out of the reference's own 9x9 HUD
# sprites. Unlike the tabs these need no state recovery - each one ships as its
# own file, at exactly the size we draw it, so this is a copy rather than a
# derivation. Must match `New-StatusStrip` in make-hud-sheet.ps1 and the
# `hud::StatusIcon` order in code.
#
# `absorbing_full`/`absorbing_half` are the gold hearts absorption draws with -
# the same 9x9 as every other heart, and `absorbing_half` is opaque over its
# left four columns exactly as `half` is, which is what says it is meant to be
# drawn over a container rather than on its own.
$statusTop = 1345
$statusPitch = 10
$statusSize = 9
$hudSprites = Join-Path $root "reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures\gui\sprites\hud"
$statusFiles = @(
    'heart\container.png', 'heart\full.png', 'heart\half.png',
    'food_empty.png', 'food_full.png', 'food_half.png',
    'air.png', 'air_bursting.png',
    'heart\absorbing_full.png', 'heart\absorbing_half.png'
)

$statusReplaced = 0
for ($i = 0; $i -lt $statusFiles.Count; $i++) {
    $path = Join-Path $hudSprites $statusFiles[$i]
    if (-not (Test-Path $path)) {
        Write-Warning "Missing status sprite $($statusFiles[$i]) - keeping ours."
        continue
    }
    $icon = [System.Drawing.Image]::FromFile($path)
    # Never resize a reference crop. The size *is* the evidence the layout is
    # right, and a silent rescale hides a drifted extraction behind a plausible
    # result - the same rule the tab crops are held to.
    if ($icon.Width -ne $statusSize -or $icon.Height -ne $statusSize) {
        $icon.Dispose()
        throw "$($statusFiles[$i]) is $($icon.Width)x$($icon.Height), expected ${statusSize}x${statusSize}"
    }
    $destX = $i * $statusPitch
    $graphics.SetClip((New-Object System.Drawing.Rectangle $destX, $statusTop, $statusSize, $statusSize))
    $graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    $graphics.DrawImage($icon, $destX, $statusTop, $statusSize, $statusSize)
    $graphics.ResetClip()
    $icon.Dispose()
    $statusReplaced++
}

$graphics.Dispose()
$sheet.Dispose()

foreach ($output in $resolvedOutputs) {
    $directory = Split-Path -Parent $output
    if (-not (Test-Path $directory)) {
        Write-Warning "Skipping $output - $directory does not exist yet."
        continue
    }
    $atlas.Save($output, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host "wrote $output ($($atlas.Width) x $($atlas.Height), $replaced tabs and $statusReplaced status icons from reference)"
}
$atlas.Dispose()
