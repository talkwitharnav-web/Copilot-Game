# Builds a temporary proof set: the reference's own block and item textures,
# staged beside each built game.exe so the game draws them instead of ours.
#
# Same arrangement and the same reason as the creature and HUD atlases - prove
# the thing works against pixels known to be right, then author ours. It refuses
# to write under assets/ so reference pixels cannot ship by mistake, and deleting
# build/*/bin/blocks-reference/ returns the game to our own art instantly.
#
# NOTHING IS EVER RESCALED. Every source is checked to be 16 wide and 16 tall
# after frame extraction, and the script stops if it is not - a texture array
# needs every layer the same size, so a silent resize here would not fail, it
# would blur one layer and leave the rest crisp.

param(
    [string[]]$OutputPath = @(
        "build\debug\bin\blocks-reference",
        "build\release\bin\blocks-reference"
    )
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$textures = Join-Path $root "reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures"

$assetsRoot = [System.IO.Path]::GetFullPath((Join-Path $root "assets"))
$resolvedOutputs = foreach ($candidate in $OutputPath) {
    $full = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $candidate))
    if ($full.StartsWith($assetsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Reference block textures must stay outside assets/: $full"
    }
    $full
}

if (-not (Test-Path $textures)) {
    throw "Missing the reference dump at $textures"
}

# The reference paints anything whose colour varies by biome in greyscale and
# multiplies a tint in at runtime. Copied raw they render grey, which looks like
# a bug rather than a missing feature - so the plains tints go on here.
$grassTint = @(145, 189, 89)
$foliageTint = @(119, 171, 47)
$waterTint = @(63, 118, 228)

# Our texture name -> where it comes from. `Overlay` composites a second,
# tinted image on top; `Tint` multiplies; `Frame` picks one 16-row frame out of
# an animation strip. Names with no entry keep our own art: `white.png` is a
# utility layer and `sun.png` has no counterpart.
$sources = @(
    @{ Name = 'stone.png';                 Path = 'block\stone' }
    @{ Name = 'dirt.png';                  Path = 'block\dirt' }
    @{ Name = 'grass_top.png';             Path = 'block\grass_block_top';    Tint = $grassTint }
    @{ Name = 'grass_side.png';            Path = 'block\grass_block_side';
       Overlay = 'block\grass_block_side_overlay'; OverlayTint = $grassTint }
    @{ Name = 'sand.png';                  Path = 'block\sand' }
    @{ Name = 'cobblestone.png';           Path = 'block\cobblestone' }
    @{ Name = 'gravel.png';                Path = 'block\gravel' }
    @{ Name = 'snow.png';                  Path = 'block\snow' }
    @{ Name = 'planks.png';                Path = 'block\oak_planks' }
    @{ Name = 'bricks.png';                Path = 'block\bricks' }
    @{ Name = 'glowstone.png';             Path = 'block\glowstone' }
    @{ Name = 'water.png';                 Path = 'block\water_still';        Tint = $waterTint; Frame = 0 }
    @{ Name = 'log_side.png';              Path = 'block\oak_log' }
    @{ Name = 'log_top.png';               Path = 'block\oak_log_top' }
    @{ Name = 'leaves.png';                Path = 'block\oak_leaves';         Tint = $foliageTint }
    @{ Name = 'tall_grass.png';            Path = 'block\short_grass';        Tint = $grassTint }
    @{ Name = 'crafting_table_top.png';    Path = 'block\crafting_table_top' }
    @{ Name = 'crafting_table_front.png';  Path = 'block\crafting_table_front' }
    @{ Name = 'crafting_table_side.png';   Path = 'block\crafting_table_side' }
    @{ Name = 'furnace_top.png';           Path = 'block\furnace_top' }
    @{ Name = 'furnace_side.png';          Path = 'block\furnace_side' }
    @{ Name = 'furnace_front.png';         Path = 'block\furnace_front' }
    @{ Name = 'furnace_front_on.png';      Path = 'block\furnace_front_on' }
    @{ Name = 'torch.png';                 Path = 'block\torch' }
    @{ Name = 'stick.png';                 Path = 'item\stick' }
    @{ Name = 'charcoal.png';              Path = 'item\charcoal' }
    @{ Name = 'wooden_pickaxe.png';        Path = 'item\wooden_pickaxe' }
    @{ Name = 'wooden_axe.png';            Path = 'item\wooden_axe' }
    @{ Name = 'wooden_shovel.png';         Path = 'item\wooden_shovel' }
    @{ Name = 'wooden_sword.png';          Path = 'item\wooden_sword' }
    @{ Name = 'wooden_hoe.png';            Path = 'item\wooden_hoe' }
    @{ Name = 'stone_pickaxe.png';         Path = 'item\stone_pickaxe' }
    @{ Name = 'stone_axe.png';             Path = 'item\stone_axe' }
    @{ Name = 'stone_shovel.png';          Path = 'item\stone_shovel' }
    @{ Name = 'stone_sword.png';           Path = 'item\stone_sword' }
    @{ Name = 'stone_hoe.png';             Path = 'item\stone_hoe' }
)

$size = 16

function Get-Frame {
    param([System.Drawing.Bitmap]$Source, [int]$Index, [string]$Label)

    # An animated texture is a vertical strip of square frames, so the frame is
    # cropped out at its own size. Scaling the whole strip down to one tile is
    # the obvious wrong move and would smear thirty-two frames into one.
    if ($Source.Width -ne $size) {
        throw "$Label is $($Source.Width) wide, expected $size"
    }
    if ($Source.Height -ne $size -and ($Source.Height % $size) -ne 0) {
        throw "$Label is $($Source.Height) tall, which is not a whole number of ${size}px frames"
    }
    $frame = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $frame.SetPixel($x, $y, $Source.GetPixel($x, ($Index * $size + $y)))
        }
    }
    return $frame
}

function Set-Tint {
    param([System.Drawing.Bitmap]$Target, [int[]]$Tint)

    for ($y = 0; $y -lt $Target.Height; $y++) {
        for ($x = 0; $x -lt $Target.Width; $x++) {
            $pixel = $Target.GetPixel($x, $y)
            $Target.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(
                $pixel.A,
                [int]($pixel.R * $Tint[0] / 255),
                [int]($pixel.G * $Tint[1] / 255),
                [int]($pixel.B * $Tint[2] / 255)))
        }
    }
}

$staged = @()
foreach ($entry in $sources) {
    $sourcePath = Join-Path $textures "$($entry.Path).png"
    if (-not (Test-Path $sourcePath)) {
        Write-Warning "Missing $sourcePath - $($entry.Name) keeps our own art."
        continue
    }

    $loaded = [System.Drawing.Bitmap]::FromFile($sourcePath)
    $frameIndex = if ($entry.ContainsKey('Frame')) { $entry.Frame } else { 0 }
    $image = Get-Frame -Source $loaded -Index $frameIndex -Label $entry.Path
    $loaded.Dispose()

    if ($entry.ContainsKey('Tint')) {
        Set-Tint -Target $image -Tint $entry.Tint
    }

    if ($entry.ContainsKey('Overlay')) {
        # The grass block's green edge ships as a separate tintable layer over a
        # plain dirt side. Two images in the reference, one in our texture array.
        $overlayPath = Join-Path $textures "$($entry.Overlay).png"
        if (Test-Path $overlayPath) {
            $overlayLoaded = [System.Drawing.Bitmap]::FromFile($overlayPath)
            $overlay = Get-Frame -Source $overlayLoaded -Index 0 -Label $entry.Overlay
            $overlayLoaded.Dispose()
            Set-Tint -Target $overlay -Tint $entry.OverlayTint
            for ($y = 0; $y -lt $size; $y++) {
                for ($x = 0; $x -lt $size; $x++) {
                    $over = $overlay.GetPixel($x, $y)
                    if ($over.A -gt 127) {
                        $image.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $over.R, $over.G, $over.B))
                    }
                }
            }
            $overlay.Dispose()
        }
    }

    if ($image.Width -ne $size -or $image.Height -ne $size) {
        throw "$($entry.Name) came out $($image.Width)x$($image.Height), expected ${size}x${size}"
    }

    $staged += @{ Name = $entry.Name; Image = $image }
}

foreach ($output in $resolvedOutputs) {
    $parent = Split-Path -Parent $output
    if (-not (Test-Path $parent)) {
        Write-Warning "Skipping $output - $parent does not exist yet."
        continue
    }
    if (-not (Test-Path $output)) {
        New-Item -ItemType Directory -Path $output | Out-Null
    }
    foreach ($item in $staged) {
        $item.Image.Save((Join-Path $output $item.Name), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    Write-Host "wrote $output ($($staged.Count) textures at ${size}x${size}, none rescaled)"
}

foreach ($item in $staged) { $item.Image.Dispose() }
