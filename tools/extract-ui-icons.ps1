# Extracts the UI icons and widget art from the reference crafting-screen capture
# so we have something to author our own from.
#
# The capture is a 1280x720 console screenshot of the crafting-table screen and
# it is **vanilla's layout at exactly 3x** - the slot pitch measures 54 px,
# which is 18 x 3 - so every crop divides back to whole units. Each region is
# written twice: `<name>@3x.png` at the capture's own resolution, which is what
# you look at, and `<name>.png` divided by three, which is the native pixel grid
# and what you measure.
#
# **This writes into `reference/` and refuses to write under `assets/`.**
# Reference art may be measured from, must never live under `assets/`, and must
# never ship. Same rule and same mechanical guard as
# `tools/make-reference-creature-atlas.ps1`.
#
#   powershell -File tools\extract-ui-icons.ps1
#
# Region coordinates were measured off the capture, not estimated:
#   tab strip   5 tabs, 66 px wide (22 units), 75 px pitch, search pushed right
#   toggle      76 x 48 px = 26 x 16 units, matching the shipped craft_toggle
#   slot pitch  54 px = 18 units, the same in the catalogue and the inventory

param(
    [string]$Source = "reference\crafting-ui.png",
    [string]$OutDir = "reference\ui-icons"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = (Get-Location).Path
$full = [System.IO.Path]::GetFullPath((Join-Path $root $OutDir))
if ($full -like "*\assets\*" -or $full -like "*\assets") {
    throw "Refusing to write reference art under assets/. Reference art is measured from, never shipped."
}

# The capture arrives as AVIF, which System.Drawing cannot open; convert-image
# reads it through WIC.
if (-not (Test-Path $Source)) {
    $avif = [System.IO.Path]::ChangeExtension($Source, ".avif")
    if (-not (Test-Path $avif)) { throw "No source at $Source or $avif" }
    Write-Host "converting $avif"
    & (Join-Path $PSScriptRoot "convert-image.ps1") -InputPath $avif -OutputPath $Source | Out-Null
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$src = New-Object System.Drawing.Bitmap((Resolve-Path $Source).Path)

# name, x, y, w, h  -- all in capture pixels, so divide by 3 for units.
$regions = @(
    # The five category tabs. Each crop is the whole tab, not just its icon:
    # the tab art is needed as much as the glyph, and the selected tab is
    # visibly taller and paler than the rest.
    @("tab-construction",      154,  42,  66,  74),
    @("tab-equipment",         229,  42,  66,  74),
    @("tab-items",             304,  42,  66,  74),
    @("tab-nature",            379,  42,  66,  74),
    @("tab-search",            517,  42,  66,  74),

    # The layout switch, top right. Left = recipe book AND inventory, right =
    # inventory only. On console these are the ZL and ZR shoulder buttons,
    # which is why the bumper glyphs sit either side of the strip.
    @("layout-book-inventory", 838,  50,  88,  58),
    @("layout-inventory-only", 925,  50,  82,  58),
    @("button-help",          1003,  56,  46,  44),
    @("button-close",         1048,  56,  46,  44),
    @("bumper-zl",             766,  48,  66,  58),
    @("bumper-zr",            1128,  48,  66,  58),
    @("toolbar-strip",         830,  44, 290,  68),

    # The craftable filter, right of the tab name. Two cells side by side, not
    # a sliding knob: the active one carries a crafting-grid glyph and a bright
    # green border. 76 x 48 px is 26 x 16 units, which is the shipped sprite's
    # size exactly.
    @("toggle-craftable",      488, 118,  84,  56),

    # Widget and panel art worth having beside the icons.
    @("scrollbar",             545, 175,  45, 400),
    @("slot-empty",            673, 369,  54,  54),
    @("slot-red-uncraftable",  172, 184,  54,  54),
    @("slot-output-red",      1046, 186,  60,  60),
    @("panel-corner",          598, 108,  66,  66),
    @("armour-slot-glyphs",    616, 130,  56, 220),
    @("card-left-full",        148,  38, 446, 578),
    @("card-right-full",       598, 108, 534, 506)
)

$made = 0
foreach ($r in $regions) {
    $name, $x, $y, $w, $h = $r
    $rect = New-Object System.Drawing.Rectangle $x, $y, $w, $h
    $crop = $src.Clone($rect, $src.PixelFormat)

    $bigPath = Join-Path $OutDir "$name@3x.png"
    $crop.Save((Join-Path $root $bigPath), [System.Drawing.Imaging.ImageFormat]::Png)

    # Nearest neighbour, because the capture is an integer upscale of pixel art
    # and anything smoother destroys the grid being recovered. The source is
    # lossy AVIF, so treat the 1x as indicative of the grid rather than as a
    # clean rip.
    $nw = [int][math]::Round($w / 3.0)
    $nh = [int][math]::Round($h / 3.0)
    $small = New-Object System.Drawing.Bitmap $nw, $nh
    $g = [System.Drawing.Graphics]::FromImage($small)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.DrawImage($crop, (New-Object System.Drawing.Rectangle 0, 0, $nw, $nh))
    $g.Dispose()

    $smallPath = Join-Path $OutDir "$name.png"
    $small.Save((Join-Path $root $smallPath), [System.Drawing.Imaging.ImageFormat]::Png)

    Write-Host ("  {0,-24} {1,3}x{2,-3} px  ->  {3,3}x{4,-3} units" -f $name, $w, $h, $nw, $nh)
    $small.Dispose()
    $crop.Dispose()
    $made++
}

$src.Dispose()
Write-Host ""
Write-Host ("wrote $made regions x2 into $OutDir  (@3x = as captured, plain = divided by 3)")
