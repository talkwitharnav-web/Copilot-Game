# Builds a temporary proof atlas: our own creatures.png with Mojang's reference
# skins laid over the rows whose art has not been authored yet. This exists only
# to prove a box net maps to the right faces, and refuses to write under assets/
# so reference pixels cannot ship by mistake.
#
# Sheep, cow, pig and the Bramble keep OUR skins - they are finished, and
# covering them would throw away working art to answer a question already
# settled. Only rows 192 and beyond are replaced.
#
# It lands beside each built game.exe as creatures-reference.png, which the game
# loads in place of the generated sheet whenever it exists. Delete those files
# to go back to our own skins everywhere.

param(
    [string[]]$OutputPath = @(
        "build\debug\bin\creatures-reference.png",
        "build\release\bin\creatures-reference.png"
    )
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Join-Path $PSScriptRoot "..\reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures\entity"
$assetsRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\assets"))
$resolvedOutputs = foreach ($candidate in $OutputPath) {
    $full = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $candidate))
    if ($full.StartsWith($assetsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Reference atlas must stay outside assets/: $full"
    }
    $full
}

# Row offsets must match the constants in game/src/world/Creature.cpp.
$targets = @(
    @{ Row = 192; Path = "chicken\chicken_temperate.png" }
    @{ Row = 224; Path = "cat\cat_tabby.png" }
    @{ Row = 256; Path = "camel\camel.png" }
    @{ Row = 384; Path = "horse\horse_brown.png" }
    @{ Row = 448; Path = "horse\mule.png" }
    @{ Row = 512; Path = "llama\llama_brown.png" }
    @{ Row = 576; Path = "horse\donkey.png" }
    @{ Row = 640; Path = "goat\goat.png" }
    @{ Row = 704; Path = "rabbit\rabbit_brown.png" }
    @{ Row = 768; Path = "wolf\wolf.png" }
    # The angry wolf is the same net with red eyes and a dropped brow - forty
    # pixels apart. It fits inside the wolf's own 64-row slot, so no resize.
    @{ Row = 800; Path = "wolf\wolf_angry.png" }
    @{ Row = 832; Path = "frog\frog_temperate.png" }
    @{ Row = 896; Path = "fox\fox.png" }
    @{ Row = 928; Path = "cat\ocelot.png" }
    @{ Row = 960; Path = "bear\polarbear.png" }
    @{ Row = 1024; Path = "panda\panda.png" }
    @{ Row = 1088; Path = "slime\slime.png" }
    @{ Row = 1120; Path = "spider\spider.png" }
    @{ Row = 1152; Path = "zombie\zombie.png" }
    @{ Row = 1216; Path = "skeleton\skeleton.png" }
    @{ Row = 1248; Path = "spider\cave_spider.png" }
    @{ Row = 1280; Path = "villager\villager.png" }
    # The base villager skin is bare - the robe is a separate biome overlay with
    # transparent gaps for the face and hands, so it has to blend over the base
    # rather than replace it.
    @{ Row = 1280; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 1344; Path = "zombie\husk.png" }
    @{ Row = 1408; Path = "silverfish\silverfish.png" }
    @{ Row = 1440; Path = "skeleton\wither_skeleton.png" }
    @{ Row = 1472; Path = "skeleton\stray.png" }
    @{ Row = 1504; Path = "skeleton\bogged.png" }
    @{ Row = 1536; Path = "zombie_villager\zombie_villager.png" }
    # Same arrangement as the villager: the base skin is bare and the robe is a
    # separate overlay with transparent gaps, so it blends rather than replaces.
    @{ Row = 1536; Path = "zombie_villager\type\plains.png"; Over = $true }
    @{ Row = 1600; Path = "witch\witch.png" }
    @{ Row = 1728; Path = "wandering_trader\wandering_trader.png" }
    @{ Row = 1792; Path = "piglin\piglin.png" }
    # Not a species: the charged Bramble's energy shell, on the same net as the
    # creeper itself so the same box UVs read it. Mostly transparent, which is
    # the point - only the blue survives the cutout test.
    @{ Row = 1856; Path = "creeper\creeper_armor.png" }
)

$ours = Join-Path $PSScriptRoot "..\assets\textures\creatures.png"
$base = [System.Drawing.Bitmap]::FromFile((Resolve-Path $ours).Path)
# The size comes from our own sheet, never from a constant here. A second copy
# of it is exactly how a stale atlas once served mangled art through a rebuild
# and a verification pass.
$atlas = New-Object System.Drawing.Bitmap $base.Width, $base.Height
$graphics = [System.Drawing.Graphics]::FromImage($atlas)
try {
    $graphics.Clear([System.Drawing.Color]::Transparent)
    # SourceCopy throughout, because these skins are alpha-tested cutouts: a
    # blended edge would survive the discard as a half-transparent texel.
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy

    try {
        $graphics.DrawImageUnscaled($base, 0, 0)
    } finally {
        $base.Dispose()
    }

    foreach ($target in $targets) {
        $path = Join-Path $root $target.Path
        $source = [System.Drawing.Bitmap]::FromFile((Resolve-Path $path).Path)
        try {
            $graphics.CompositingMode = if ($target.Over) {
                [System.Drawing.Drawing2D.CompositingMode]::SourceOver
            } else {
                [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            }
            $graphics.DrawImageUnscaled($source, 0, $target.Row)
        } finally {
            $source.Dispose()
        }
    }
} finally {
    $graphics.Dispose()
}

foreach ($output in $resolvedOutputs) {
    $directory = Split-Path -Parent $output
    if (-not (Test-Path $directory)) { continue }
    $atlas.Save($output, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host "wrote $output ($($atlas.Width)x$($atlas.Height))"
}
$atlas.Dispose()