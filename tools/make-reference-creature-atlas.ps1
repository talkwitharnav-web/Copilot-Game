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
    @{ Row = 1888; Path = "zombie\drowned.png" }
    # Not a species either: the drowned's clothing, on the same net as the body
    # under it, drawn as a second shell a quarter of a texel larger.
    @{ Row = 1952; Path = "zombie\drowned_outer_layer.png" }
    @{ Row = 2016; Path = "fish\cod.png" }
    @{ Row = 2048; Path = "fish\salmon.png" }
    @{ Row = 2080; Path = "fish\pufferfish.png" }
    @{ Row = 2112; Path = "squid\squid.png" }
    @{ Row = 2144; Path = "squid\glow_squid.png" }
    @{ Row = 2176; Path = "turtle\turtle.png" }
    @{ Row = 2240; Path = "dolphin\dolphin.png" }
    # Five axolotl liveries, 64 rows each, in the order `CreatureKind` variants
    # are rolled.
    @{ Row = 2304; Path = "axolotl\axolotl_lucy.png" }
    @{ Row = 2368; Path = "axolotl\axolotl_cyan.png" }
    @{ Row = 2432; Path = "axolotl\axolotl_gold.png" }
    @{ Row = 2496; Path = "axolotl\axolotl_wild.png" }
    @{ Row = 2560; Path = "axolotl\axolotl_blue.png" }
    # Each tropical body shape, then its six pattern overlays. The overlay is
    # drawn as a second cutout shell rather than tinted onto the base.
    @{ Row = 2624; Path = "fish\tropical_a.png" }
    @{ Row = 2656; Path = "fish\tropical_a_pattern_1.png" }
    @{ Row = 2688; Path = "fish\tropical_a_pattern_2.png" }
    @{ Row = 2720; Path = "fish\tropical_a_pattern_3.png" }
    @{ Row = 2752; Path = "fish\tropical_a_pattern_4.png" }
    @{ Row = 2784; Path = "fish\tropical_a_pattern_5.png" }
    @{ Row = 2816; Path = "fish\tropical_a_pattern_6.png" }
    @{ Row = 2848; Path = "fish\tropical_b.png" }
    @{ Row = 2880; Path = "fish\tropical_b_pattern_1.png" }
    @{ Row = 2912; Path = "fish\tropical_b_pattern_2.png" }
    @{ Row = 2944; Path = "fish\tropical_b_pattern_3.png" }
    @{ Row = 2976; Path = "fish\tropical_b_pattern_4.png" }
    @{ Row = 3008; Path = "fish\tropical_b_pattern_5.png" }
    @{ Row = 3040; Path = "fish\tropical_b_pattern_6.png" }
    # Tier 1. The first four are a skin apiece over a rig that already exists;
    # the trader llama contributes only its pack, because the reference draws
    # that over an ordinary llama rather than replacing it.
    @{ Row = 3072; Path = "cow\mooshroom_red.png" }
    @{ Row = 3136; Path = "slime\magmacube.png" }
    @{ Row = 3200; Path = "horse\horse_skeleton.png" }
    @{ Row = 3264; Path = "horse\horse_zombie.png" }
    @{ Row = 3328; Path = "equipment\llama_body\trader_llama.png" }
    @{ Row = 3392; Path = "endermite\endermite.png" }
    @{ Row = 3424; Path = "piglin\piglin_brute.png" }
    @{ Row = 3488; Path = "piglin\zombified_piglin.png" }
    # Not a skin: the mushroom the Mushroom Cow grows on its back. The reference
    # puts real mushroom BLOCKS there rather than painting them on the hide, so
    # this is the block texture stamped into an empty corner of that species'
    # own rows - measured at 0% coverage before it was chosen. Putting it here
    # rather than in the block texture array is what avoids moving
    # `TextureLayer::SpawnEggFirst` and sliding every spawn egg sprite.
    @{ Row = 3072; X = 32; Y = 48; Path = "..\block\red_mushroom.png" }
    @{ Row = 3552; Path = "bee\bee.png" }
    @{ Row = 3616; Path = "bee\bee_angry.png" }
    # The iron golem, 128 rows: the largest single species allocation the sheet
    # has, and the only one as tall as it is wide.
    @{ Row = 3680; Path = "iron_golem\iron_golem.png" }
    # The fourteen villager outfits, sixty-four rows each, in the same order as
    # `professionForJobSite` numbers them. Each is three layers, exactly as the
    # reference renders one: the bare body, the biome robe over it, and the
    # trade's apron over that. **Unemployed is not in this list** - it wears the
    # plain villager already staged at row 1280, which is what makes taking a
    # job visibly change a villager.
    @{ Row = 3808; Path = "villager\villager.png" }
    @{ Row = 3808; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 3808; Path = "villager\profession\farmer.png"; Over = $true }
    @{ Row = 3872; Path = "villager\villager.png" }
    @{ Row = 3872; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 3872; Path = "villager\profession\fisherman.png"; Over = $true }
    @{ Row = 3936; Path = "villager\villager.png" }
    @{ Row = 3936; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 3936; Path = "villager\profession\fletcher.png"; Over = $true }
    @{ Row = 4000; Path = "villager\villager.png" }
    @{ Row = 4000; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4000; Path = "villager\profession\shepherd.png"; Over = $true }
    @{ Row = 4064; Path = "villager\villager.png" }
    @{ Row = 4064; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4064; Path = "villager\profession\cartographer.png"; Over = $true }
    @{ Row = 4128; Path = "villager\villager.png" }
    @{ Row = 4128; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4128; Path = "villager\profession\librarian.png"; Over = $true }
    @{ Row = 4192; Path = "villager\villager.png" }
    @{ Row = 4192; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4192; Path = "villager\profession\mason.png"; Over = $true }
    @{ Row = 4256; Path = "villager\villager.png" }
    @{ Row = 4256; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4256; Path = "villager\profession\toolsmith.png"; Over = $true }
    @{ Row = 4320; Path = "villager\villager.png" }
    @{ Row = 4320; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4320; Path = "villager\profession\weaponsmith.png"; Over = $true }
    @{ Row = 4384; Path = "villager\villager.png" }
    @{ Row = 4384; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4384; Path = "villager\profession\armorer.png"; Over = $true }
    @{ Row = 4448; Path = "villager\villager.png" }
    @{ Row = 4448; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4448; Path = "villager\profession\butcher.png"; Over = $true }
    @{ Row = 4512; Path = "villager\villager.png" }
    @{ Row = 4512; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4512; Path = "villager\profession\leatherworker.png"; Over = $true }
    @{ Row = 4576; Path = "villager\villager.png" }
    @{ Row = 4576; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4576; Path = "villager\profession\cleric.png"; Over = $true }
    @{ Row = 4640; Path = "villager\villager.png" }
    @{ Row = 4640; Path = "villager\type\plains.png"; Over = $true }
    @{ Row = 4640; Path = "villager\profession\nitwit.png"; Over = $true }
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
            $offsetX = if ($null -ne $target.X) { $target.X } else { 0 }
            $offsetY = if ($null -ne $target.Y) { $target.Y } else { 0 }
            $graphics.DrawImageUnscaled($source, $offsetX, $target.Row + $offsetY)
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