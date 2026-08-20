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
    @{ Name = 'andesite.png';              Path = 'block\andesite' }
    @{ Name = 'diorite.png';               Path = 'block\diorite' }
    @{ Name = 'granite.png';               Path = 'block\granite' }
    @{ Name = 'smooth_stone.png';          Path = 'block\smooth_stone' }
    @{ Name = 'stone_bricks.png';          Path = 'block\stone_bricks' }
    @{ Name = 'mossy_cobblestone.png';     Path = 'block\mossy_cobblestone' }
    @{ Name = 'obsidian.png';              Path = 'block\obsidian' }
    @{ Name = 'clay.png';                  Path = 'block\clay' }
    @{ Name = 'sandstone_top.png';         Path = 'block\sandstone_top' }
    @{ Name = 'sandstone_side.png';        Path = 'block\sandstone' }
    @{ Name = 'sandstone_bottom.png';      Path = 'block\sandstone_bottom' }
    @{ Name = 'bookshelf.png';             Path = 'block\bookshelf' }
    @{ Name = 'glass.png';                 Path = 'block\glass' }
    @{ Name = 'dandelion.png';             Path = 'block\dandelion' }
    @{ Name = 'poppy.png';                 Path = 'block\poppy' }
    @{ Name = 'dead_bush.png';             Path = 'block\dead_bush' }
    @{ Name = 'coal_ore.png';              Path = 'block\coal_ore' }
    @{ Name = 'iron_ore.png';              Path = 'block\iron_ore' }
    @{ Name = 'copper_ore.png';            Path = 'block\copper_ore' }
    @{ Name = 'gold_ore.png';              Path = 'block\gold_ore' }
    @{ Name = 'redstone_ore.png';          Path = 'block\redstone_ore' }
    @{ Name = 'lapis_ore.png';             Path = 'block\lapis_ore' }
    @{ Name = 'diamond_ore.png';           Path = 'block\diamond_ore' }
    @{ Name = 'emerald_ore.png';           Path = 'block\emerald_ore' }
    @{ Name = 'deepslate_side.png';        Path = 'block\deepslate' }
    @{ Name = 'deepslate_top.png';         Path = 'block\deepslate_top' }
    @{ Name = 'bedrock.png';               Path = 'block\bedrock' }
    @{ Name = 'terracotta.png';            Path = 'block\terracotta' }
    @{ Name = 'packed_ice.png';            Path = 'block\packed_ice' }
    @{ Name = 'coal.png';                  Path = 'item\coal' }
    @{ Name = 'raw_iron.png';              Path = 'item\raw_iron' }
    @{ Name = 'iron_ingot.png';            Path = 'item\iron_ingot' }
    @{ Name = 'raw_gold.png';              Path = 'item\raw_gold' }
    @{ Name = 'gold_ingot.png';            Path = 'item\gold_ingot' }
    @{ Name = 'raw_copper.png';            Path = 'item\raw_copper' }
    @{ Name = 'copper_ingot.png';          Path = 'item\copper_ingot' }
    @{ Name = 'diamond.png';               Path = 'item\diamond' }
    @{ Name = 'emerald.png';               Path = 'item\emerald' }
    @{ Name = 'lapis_lazuli.png';          Path = 'item\lapis_lazuli' }
    @{ Name = 'redstone.png';              Path = 'item\redstone' }
    @{ Name = 'bucket.png';                Path = 'item\bucket' }
    @{ Name = 'water_bucket.png';          Path = 'item\water_bucket' }
    @{ Name = 'iron_pickaxe.png';          Path = 'item\iron_pickaxe' }
    @{ Name = 'iron_axe.png';              Path = 'item\iron_axe' }
    @{ Name = 'iron_shovel.png';           Path = 'item\iron_shovel' }
    @{ Name = 'iron_sword.png';            Path = 'item\iron_sword' }
    @{ Name = 'iron_hoe.png';              Path = 'item\iron_hoe' }
    @{ Name = 'diamond_pickaxe.png';       Path = 'item\diamond_pickaxe' }
    @{ Name = 'diamond_axe.png';           Path = 'item\diamond_axe' }
    @{ Name = 'diamond_shovel.png';        Path = 'item\diamond_shovel' }
    @{ Name = 'diamond_sword.png';         Path = 'item\diamond_sword' }
    @{ Name = 'diamond_hoe.png';           Path = 'item\diamond_hoe' }
    # Our name for the reference's dark alloy is Emberite; the files it is
    # staged from are still the reference's own, like every other placeholder.
    @{ Name = 'emberite_pickaxe.png';      Path = 'item\netherite_pickaxe' }
    @{ Name = 'emberite_axe.png';          Path = 'item\netherite_axe' }
    @{ Name = 'emberite_shovel.png';       Path = 'item\netherite_shovel' }
    @{ Name = 'emberite_sword.png';        Path = 'item\netherite_sword' }
    @{ Name = 'emberite_hoe.png';          Path = 'item\netherite_hoe' }
    @{ Name = 'emberite_scrap.png';        Path = 'item\netherite_scrap' }
    @{ Name = 'emberite_ingot.png';        Path = 'item\netherite_ingot' }
    @{ Name = 'apple.png';                 Path = 'item\apple' }
    @{ Name = 'porkchop_raw.png';          Path = 'item\porkchop' }
    @{ Name = 'porkchop_cooked.png';       Path = 'item\cooked_porkchop' }
    @{ Name = 'beef_raw.png';              Path = 'item\beef' }
    @{ Name = 'beef_cooked.png';           Path = 'item\cooked_beef' }
    @{ Name = 'chicken_raw.png';           Path = 'item\chicken' }
    @{ Name = 'chicken_cooked.png';        Path = 'item\cooked_chicken' }
    @{ Name = 'mutton_raw.png';            Path = 'item\mutton' }
    @{ Name = 'mutton_cooked.png';         Path = 'item\cooked_mutton' }
    @{ Name = 'cod_raw.png';               Path = 'item\cod' }
    @{ Name = 'cod_cooked.png';            Path = 'item\cooked_cod' }
    @{ Name = 'prismarine.png';            Path = 'block\prismarine' }
    @{ Name = 'sea_lantern.png';           Path = 'block\sea_lantern' }
    @{ Name = 'coarse_dirt.png';           Path = 'block\coarse_dirt' }

    # The table-driven run, in `kExtraBlocks` order. Anything greyscale in the
    # reference is tinted here for the same reason grass and oak leaves are:
    # copied raw it renders grey, which reads as a bug rather than as a missing
    # feature.
    @{ Name = 'cobbled_deepslate.png';     Path = 'block\cobbled_deepslate' }
    @{ Name = 'ice.png';                   Path = 'block\ice' }
    @{ Name = 'blue_ice.png';              Path = 'block\blue_ice' }
    @{ Name = 'coal_block.png';            Path = 'block\coal_block' }
    @{ Name = 'iron_block.png';            Path = 'block\iron_block' }
    @{ Name = 'gold_block.png';            Path = 'block\gold_block' }
    @{ Name = 'diamond_block.png';         Path = 'block\diamond_block' }
    @{ Name = 'emerald_block.png';         Path = 'block\emerald_block' }
    @{ Name = 'lapis_block.png';           Path = 'block\lapis_block' }
    @{ Name = 'redstone_block.png';        Path = 'block\redstone_block' }
    @{ Name = 'copper_block.png';          Path = 'block\copper_block' }
    @{ Name = 'polished_andesite.png';     Path = 'block\polished_andesite' }
    @{ Name = 'polished_diorite.png';      Path = 'block\polished_diorite' }
    @{ Name = 'polished_granite.png';      Path = 'block\polished_granite' }
    @{ Name = 'chiseled_stone_bricks.png'; Path = 'block\chiseled_stone_bricks' }
    @{ Name = 'mossy_stone_bricks.png';    Path = 'block\mossy_stone_bricks' }
    @{ Name = 'cracked_stone_bricks.png';  Path = 'block\cracked_stone_bricks' }
    @{ Name = 'polished_deepslate.png';    Path = 'block\polished_deepslate' }
    @{ Name = 'deepslate_bricks.png';      Path = 'block\deepslate_bricks' }
    @{ Name = 'deepslate_tiles.png';       Path = 'block\deepslate_tiles' }
    @{ Name = 'smooth_sandstone.png';      Path = 'block\sandstone_top' }
    @{ Name = 'cut_sandstone.png';         Path = 'block\cut_sandstone' }
    @{ Name = 'chiseled_sandstone.png';    Path = 'block\chiseled_sandstone' }
    @{ Name = 'tube_coral_block.png';      Path = 'block\tube_coral_block' }
    @{ Name = 'brain_coral_block.png';     Path = 'block\brain_coral_block' }
    @{ Name = 'bubble_coral_block.png';    Path = 'block\bubble_coral_block' }
    @{ Name = 'fire_coral_block.png';      Path = 'block\fire_coral_block' }
    @{ Name = 'horn_coral_block.png';      Path = 'block\horn_coral_block' }
    @{ Name = 'sponge.png';                Path = 'block\sponge' }
    @{ Name = 'wet_sponge.png';            Path = 'block\wet_sponge' }
    @{ Name = 'dark_prismarine.png';       Path = 'block\dark_prismarine' }
    @{ Name = 'prismarine_bricks.png';     Path = 'block\prismarine_bricks' }
    @{ Name = 'spruce_log.png';            Path = 'block\spruce_log' }
    @{ Name = 'spruce_log_top.png';        Path = 'block\spruce_log_top' }
    @{ Name = 'spruce_leaves.png';         Path = 'block\spruce_leaves';      Tint = @(97, 153, 97) }
    @{ Name = 'spruce_planks.png';         Path = 'block\spruce_planks' }
    @{ Name = 'birch_log.png';             Path = 'block\birch_log' }
    @{ Name = 'birch_log_top.png';         Path = 'block\birch_log_top' }
    @{ Name = 'birch_leaves.png';          Path = 'block\birch_leaves';       Tint = @(128, 167, 85) }
    @{ Name = 'birch_planks.png';          Path = 'block\birch_planks' }
    @{ Name = 'cornflower.png';            Path = 'block\cornflower' }
    @{ Name = 'oxeye_daisy.png';           Path = 'block\oxeye_daisy' }
    @{ Name = 'azure_bluet.png';           Path = 'block\azure_bluet' }
    @{ Name = 'allium.png';                Path = 'block\allium' }
    @{ Name = 'red_tulip.png';             Path = 'block\red_tulip' }
    @{ Name = 'orange_tulip.png';          Path = 'block\orange_tulip' }
    @{ Name = 'brown_mushroom.png';        Path = 'block\brown_mushroom' }
    @{ Name = 'red_mushroom.png';          Path = 'block\red_mushroom' }
    @{ Name = 'kelp.png';                  Path = 'block\kelp_plant' }
    @{ Name = 'seagrass.png';              Path = 'block\seagrass' }
    # Appended 2026-08-07, in `kExtraBlocks` layer order. Which of these ship
    # greyscale was measured rather than assumed: jungle, acacia and dark oak
    # leaves and the fern are colormapped and get a tint; cherry leaves and
    # sugar cane already carry their own colour and must not be touched.
    @{ Name = 'white_wool.png';            Path = 'block\white_wool' }
    @{ Name = 'orange_wool.png';           Path = 'block\orange_wool' }
    @{ Name = 'magenta_wool.png';          Path = 'block\magenta_wool' }
    @{ Name = 'light_blue_wool.png';       Path = 'block\light_blue_wool' }
    @{ Name = 'yellow_wool.png';           Path = 'block\yellow_wool' }
    @{ Name = 'lime_wool.png';             Path = 'block\lime_wool' }
    @{ Name = 'pink_wool.png';             Path = 'block\pink_wool' }
    @{ Name = 'gray_wool.png';             Path = 'block\gray_wool' }
    @{ Name = 'light_gray_wool.png';       Path = 'block\light_gray_wool' }
    @{ Name = 'cyan_wool.png';             Path = 'block\cyan_wool' }
    @{ Name = 'purple_wool.png';           Path = 'block\purple_wool' }
    @{ Name = 'blue_wool.png';             Path = 'block\blue_wool' }
    @{ Name = 'brown_wool.png';            Path = 'block\brown_wool' }
    @{ Name = 'green_wool.png';            Path = 'block\green_wool' }
    @{ Name = 'red_wool.png';              Path = 'block\red_wool' }
    @{ Name = 'black_wool.png';            Path = 'block\black_wool' }
    @{ Name = 'white_concrete.png';        Path = 'block\white_concrete' }
    @{ Name = 'orange_concrete.png';       Path = 'block\orange_concrete' }
    @{ Name = 'magenta_concrete.png';      Path = 'block\magenta_concrete' }
    @{ Name = 'light_blue_concrete.png';   Path = 'block\light_blue_concrete' }
    @{ Name = 'yellow_concrete.png';       Path = 'block\yellow_concrete' }
    @{ Name = 'lime_concrete.png';         Path = 'block\lime_concrete' }
    @{ Name = 'pink_concrete.png';         Path = 'block\pink_concrete' }
    @{ Name = 'gray_concrete.png';         Path = 'block\gray_concrete' }
    @{ Name = 'light_gray_concrete.png';   Path = 'block\light_gray_concrete' }
    @{ Name = 'cyan_concrete.png';         Path = 'block\cyan_concrete' }
    @{ Name = 'purple_concrete.png';       Path = 'block\purple_concrete' }
    @{ Name = 'blue_concrete.png';         Path = 'block\blue_concrete' }
    @{ Name = 'brown_concrete.png';        Path = 'block\brown_concrete' }
    @{ Name = 'green_concrete.png';        Path = 'block\green_concrete' }
    @{ Name = 'red_concrete.png';          Path = 'block\red_concrete' }
    @{ Name = 'black_concrete.png';        Path = 'block\black_concrete' }
    @{ Name = 'white_terracotta.png';      Path = 'block\white_terracotta' }
    @{ Name = 'orange_terracotta.png';     Path = 'block\orange_terracotta' }
    @{ Name = 'magenta_terracotta.png';    Path = 'block\magenta_terracotta' }
    @{ Name = 'light_blue_terracotta.png'; Path = 'block\light_blue_terracotta' }
    @{ Name = 'yellow_terracotta.png';     Path = 'block\yellow_terracotta' }
    @{ Name = 'lime_terracotta.png';       Path = 'block\lime_terracotta' }
    @{ Name = 'pink_terracotta.png';       Path = 'block\pink_terracotta' }
    @{ Name = 'gray_terracotta.png';       Path = 'block\gray_terracotta' }
    @{ Name = 'light_gray_terracotta.png'; Path = 'block\light_gray_terracotta' }
    @{ Name = 'cyan_terracotta.png';       Path = 'block\cyan_terracotta' }
    @{ Name = 'purple_terracotta.png';     Path = 'block\purple_terracotta' }
    @{ Name = 'blue_terracotta.png';       Path = 'block\blue_terracotta' }
    @{ Name = 'brown_terracotta.png';      Path = 'block\brown_terracotta' }
    @{ Name = 'green_terracotta.png';      Path = 'block\green_terracotta' }
    @{ Name = 'red_terracotta.png';        Path = 'block\red_terracotta' }
    @{ Name = 'black_terracotta.png';      Path = 'block\black_terracotta' }
    @{ Name = 'deepslate_coal_ore.png';    Path = 'block\deepslate_coal_ore' }
    @{ Name = 'deepslate_iron_ore.png';    Path = 'block\deepslate_iron_ore' }
    @{ Name = 'deepslate_copper_ore.png';  Path = 'block\deepslate_copper_ore' }
    @{ Name = 'deepslate_gold_ore.png';    Path = 'block\deepslate_gold_ore' }
    @{ Name = 'deepslate_redstone_ore.png'; Path = 'block\deepslate_redstone_ore' }
    @{ Name = 'deepslate_lapis_ore.png';   Path = 'block\deepslate_lapis_ore' }
    @{ Name = 'deepslate_diamond_ore.png'; Path = 'block\deepslate_diamond_ore' }
    @{ Name = 'deepslate_emerald_ore.png'; Path = 'block\deepslate_emerald_ore' }
    @{ Name = 'jungle_log.png';            Path = 'block\jungle_log' }
    @{ Name = 'jungle_log_top.png';        Path = 'block\jungle_log_top' }
    @{ Name = 'jungle_leaves.png';         Path = 'block\jungle_leaves';      Tint = $foliageTint }
    @{ Name = 'jungle_planks.png';         Path = 'block\jungle_planks' }
    @{ Name = 'acacia_log.png';            Path = 'block\acacia_log' }
    @{ Name = 'acacia_log_top.png';        Path = 'block\acacia_log_top' }
    @{ Name = 'acacia_leaves.png';         Path = 'block\acacia_leaves';      Tint = $foliageTint }
    @{ Name = 'acacia_planks.png';         Path = 'block\acacia_planks' }
    @{ Name = 'dark_oak_log.png';          Path = 'block\dark_oak_log' }
    @{ Name = 'dark_oak_log_top.png';      Path = 'block\dark_oak_log_top' }
    @{ Name = 'dark_oak_leaves.png';       Path = 'block\dark_oak_leaves';    Tint = $foliageTint }
    @{ Name = 'dark_oak_planks.png';       Path = 'block\dark_oak_planks' }
    @{ Name = 'cherry_log.png';            Path = 'block\cherry_log' }
    @{ Name = 'cherry_log_top.png';        Path = 'block\cherry_log_top' }
    @{ Name = 'cherry_leaves.png';         Path = 'block\cherry_leaves' }
    @{ Name = 'cherry_planks.png';         Path = 'block\cherry_planks' }
    @{ Name = 'stripped_oak_log.png';      Path = 'block\stripped_oak_log' }
    @{ Name = 'stripped_oak_log_top.png';  Path = 'block\stripped_oak_log_top' }
    @{ Name = 'stripped_spruce_log.png';   Path = 'block\stripped_spruce_log' }
    @{ Name = 'stripped_spruce_log_top.png'; Path = 'block\stripped_spruce_log_top' }
    @{ Name = 'stripped_birch_log.png';    Path = 'block\stripped_birch_log' }
    @{ Name = 'stripped_birch_log_top.png'; Path = 'block\stripped_birch_log_top' }
    @{ Name = 'stripped_jungle_log.png';   Path = 'block\stripped_jungle_log' }
    @{ Name = 'stripped_jungle_log_top.png'; Path = 'block\stripped_jungle_log_top' }
    @{ Name = 'stripped_acacia_log.png';   Path = 'block\stripped_acacia_log' }
    @{ Name = 'stripped_acacia_log_top.png'; Path = 'block\stripped_acacia_log_top' }
    @{ Name = 'stripped_dark_oak_log.png'; Path = 'block\stripped_dark_oak_log' }
    @{ Name = 'stripped_dark_oak_log_top.png'; Path = 'block\stripped_dark_oak_log_top' }
    @{ Name = 'tuff.png';                  Path = 'block\tuff' }
    @{ Name = 'calcite.png';               Path = 'block\calcite' }
    @{ Name = 'dripstone_block.png';       Path = 'block\dripstone_block' }
    @{ Name = 'moss_block.png';            Path = 'block\moss_block' }
    @{ Name = 'mud.png';                   Path = 'block\mud' }
    @{ Name = 'packed_mud.png';            Path = 'block\packed_mud' }
    @{ Name = 'mud_bricks.png';            Path = 'block\mud_bricks' }
    @{ Name = 'rooted_dirt.png';           Path = 'block\rooted_dirt' }
    @{ Name = 'amethyst_block.png';        Path = 'block\amethyst_block' }
    @{ Name = 'smooth_basalt.png';         Path = 'block\smooth_basalt' }
    @{ Name = 'basalt_side.png';           Path = 'block\basalt_side' }
    @{ Name = 'basalt_top.png';            Path = 'block\basalt_top' }
    @{ Name = 'magma.png';                 Path = 'block\magma' }
    @{ Name = 'honeycomb_block.png';       Path = 'block\honeycomb_block' }
    @{ Name = 'honey_block_side.png';      Path = 'block\honey_block_side' }
    @{ Name = 'honey_block_top.png';       Path = 'block\honey_block_top' }
    @{ Name = 'red_sandstone.png';         Path = 'block\red_sandstone' }
    @{ Name = 'red_sandstone_top.png';     Path = 'block\red_sandstone_top' }
    @{ Name = 'cut_red_sandstone.png';     Path = 'block\cut_red_sandstone' }
    @{ Name = 'chiseled_red_sandstone.png'; Path = 'block\chiseled_red_sandstone' }
    @{ Name = 'pumpkin_side.png';          Path = 'block\pumpkin_side' }
    @{ Name = 'pumpkin_top.png';           Path = 'block\pumpkin_top' }
    @{ Name = 'melon_side.png';            Path = 'block\melon_side' }
    @{ Name = 'melon_top.png';             Path = 'block\melon_top' }
    @{ Name = 'hay_block_side.png';        Path = 'block\hay_block_side' }
    @{ Name = 'hay_block_top.png';         Path = 'block\hay_block_top' }
    @{ Name = 'note_block.png';            Path = 'block\note_block' }
    @{ Name = 'jukebox_side.png';          Path = 'block\jukebox_side' }
    @{ Name = 'jukebox_top.png';           Path = 'block\jukebox_top' }
    @{ Name = 'blue_orchid.png';           Path = 'block\blue_orchid' }
    @{ Name = 'pink_tulip.png';            Path = 'block\pink_tulip' }
    @{ Name = 'white_tulip.png';           Path = 'block\white_tulip' }
    @{ Name = 'lily_of_the_valley.png';    Path = 'block\lily_of_the_valley' }
    @{ Name = 'oak_sapling.png';           Path = 'block\oak_sapling' }
    @{ Name = 'spruce_sapling.png';        Path = 'block\spruce_sapling' }
    @{ Name = 'birch_sapling.png';         Path = 'block\birch_sapling' }
    @{ Name = 'jungle_sapling.png';        Path = 'block\jungle_sapling' }
    @{ Name = 'acacia_sapling.png';        Path = 'block\acacia_sapling' }
    @{ Name = 'dark_oak_sapling.png';      Path = 'block\dark_oak_sapling' }
    @{ Name = 'fern.png';                  Path = 'block\fern';               Tint = $grassTint }
    @{ Name = 'sugar_cane.png';            Path = 'block\sugar_cane' }
    @{ Name = 'cobweb.png';                Path = 'block\cobweb' }
    # The second table-driven run, in `kExtraBlocks` layer order 176-248.
    @{ Name = 'netherrack.png';            Path = 'block\netherrack' }
    @{ Name = 'soul_sand.png';             Path = 'block\soul_sand' }
    @{ Name = 'soul_soil.png';             Path = 'block\soul_soil' }
    @{ Name = 'blackstone.png';            Path = 'block\blackstone' }
    @{ Name = 'blackstone_top.png';        Path = 'block\blackstone_top' }
    @{ Name = 'polished_blackstone.png';   Path = 'block\polished_blackstone' }
    @{ Name = 'polished_blackstone_bricks.png'; Path = 'block\polished_blackstone_bricks' }
    @{ Name = 'chiseled_polished_blackstone.png'; Path = 'block\chiseled_polished_blackstone' }
    @{ Name = 'cracked_polished_blackstone_bricks.png'; Path = 'block\cracked_polished_blackstone_bricks' }
    @{ Name = 'gilded_blackstone.png';     Path = 'block\gilded_blackstone' }
    @{ Name = 'nether_bricks.png';         Path = 'block\nether_bricks' }
    @{ Name = 'red_nether_bricks.png';     Path = 'block\red_nether_bricks' }
    @{ Name = 'cracked_nether_bricks.png'; Path = 'block\cracked_nether_bricks' }
    @{ Name = 'chiseled_nether_bricks.png'; Path = 'block\chiseled_nether_bricks' }
    @{ Name = 'nether_gold_ore.png';       Path = 'block\nether_gold_ore' }
    @{ Name = 'nether_quartz_ore.png';     Path = 'block\nether_quartz_ore' }
    @{ Name = 'quartz_block_side.png';     Path = 'block\quartz_block_side' }
    @{ Name = 'quartz_block_top.png';      Path = 'block\quartz_block_top' }
    @{ Name = 'smooth_quartz.png';         Path = 'block\quartz_block_bottom' }
    @{ Name = 'chiseled_quartz_block.png'; Path = 'block\chiseled_quartz_block' }
    @{ Name = 'chiseled_quartz_block_top.png'; Path = 'block\chiseled_quartz_block_top' }
    @{ Name = 'quartz_bricks.png';         Path = 'block\quartz_bricks' }
    @{ Name = 'end_stone.png';             Path = 'block\end_stone' }
    @{ Name = 'end_stone_bricks.png';      Path = 'block\end_stone_bricks' }
    @{ Name = 'purpur_block.png';          Path = 'block\purpur_block' }
    @{ Name = 'podzol_side.png';           Path = 'block\podzol_side' }
    @{ Name = 'podzol_top.png';            Path = 'block\podzol_top' }
    @{ Name = 'mycelium_side.png';         Path = 'block\mycelium_side' }
    @{ Name = 'mycelium_top.png';          Path = 'block\mycelium_top' }
    @{ Name = 'dried_kelp_side.png';       Path = 'block\dried_kelp_side' }
    @{ Name = 'dried_kelp_top.png';        Path = 'block\dried_kelp_top' }
    @{ Name = 'slime_block.png';           Path = 'block\slime_block' }
    @{ Name = 'sculk.png';                 Path = 'block\sculk' }
    @{ Name = 'budding_amethyst.png';      Path = 'block\budding_amethyst' }
    @{ Name = 'polished_tuff.png';         Path = 'block\polished_tuff' }
    @{ Name = 'tuff_bricks.png';           Path = 'block\tuff_bricks' }
    @{ Name = 'chiseled_tuff.png';         Path = 'block\chiseled_tuff' }
    @{ Name = 'polished_basalt_side.png';  Path = 'block\polished_basalt_side' }
    @{ Name = 'polished_basalt_top.png';   Path = 'block\polished_basalt_top' }
    @{ Name = 'raw_iron_block.png';        Path = 'block\raw_iron_block' }
    @{ Name = 'raw_gold_block.png';        Path = 'block\raw_gold_block' }
    @{ Name = 'raw_copper_block.png';      Path = 'block\raw_copper_block' }
    @{ Name = 'exposed_copper.png';        Path = 'block\exposed_copper' }
    @{ Name = 'weathered_copper.png';      Path = 'block\weathered_copper' }
    @{ Name = 'oxidized_copper.png';       Path = 'block\oxidized_copper' }
    @{ Name = 'cut_copper.png';            Path = 'block\cut_copper' }
    @{ Name = 'exposed_cut_copper.png';    Path = 'block\exposed_cut_copper' }
    @{ Name = 'weathered_cut_copper.png';  Path = 'block\weathered_cut_copper' }
    @{ Name = 'oxidized_cut_copper.png';   Path = 'block\oxidized_cut_copper' }
    @{ Name = 'chiseled_copper.png';       Path = 'block\chiseled_copper' }
    @{ Name = 'reinforced_deepslate_side.png'; Path = 'block\reinforced_deepslate_side' }
    @{ Name = 'reinforced_deepslate_top.png';  Path = 'block\reinforced_deepslate_top' }
    @{ Name = 'chiseled_deepslate.png';    Path = 'block\chiseled_deepslate' }
    @{ Name = 'cracked_deepslate_bricks.png'; Path = 'block\cracked_deepslate_bricks' }
    @{ Name = 'cracked_deepslate_tiles.png';  Path = 'block\cracked_deepslate_tiles' }
    @{ Name = 'smooth_red_sandstone.png';  Path = 'block\red_sandstone_top' }
    @{ Name = 'nether_wart_block.png';     Path = 'block\nether_wart_block' }
    @{ Name = 'white_concrete_powder.png';      Path = 'block\white_concrete_powder' }
    @{ Name = 'orange_concrete_powder.png';     Path = 'block\orange_concrete_powder' }
    @{ Name = 'magenta_concrete_powder.png';    Path = 'block\magenta_concrete_powder' }
    @{ Name = 'light_blue_concrete_powder.png'; Path = 'block\light_blue_concrete_powder' }
    @{ Name = 'yellow_concrete_powder.png';     Path = 'block\yellow_concrete_powder' }
    @{ Name = 'lime_concrete_powder.png';       Path = 'block\lime_concrete_powder' }
    @{ Name = 'pink_concrete_powder.png';       Path = 'block\pink_concrete_powder' }
    @{ Name = 'gray_concrete_powder.png';       Path = 'block\gray_concrete_powder' }
    @{ Name = 'light_gray_concrete_powder.png'; Path = 'block\light_gray_concrete_powder' }
    @{ Name = 'cyan_concrete_powder.png';       Path = 'block\cyan_concrete_powder' }
    @{ Name = 'purple_concrete_powder.png';     Path = 'block\purple_concrete_powder' }
    @{ Name = 'blue_concrete_powder.png';       Path = 'block\blue_concrete_powder' }
    @{ Name = 'brown_concrete_powder.png';      Path = 'block\brown_concrete_powder' }
    @{ Name = 'green_concrete_powder.png';      Path = 'block\green_concrete_powder' }
    @{ Name = 'red_concrete_powder.png';        Path = 'block\red_concrete_powder' }
    @{ Name = 'black_concrete_powder.png';      Path = 'block\black_concrete_powder' }
    # The third batch, layers 249-326.
    @{ Name = 'cactus_side.png';           Path = 'block\cactus_side' }
    @{ Name = 'cactus_top.png';            Path = 'block\cactus_top' }
    # **The whole sheet, uncropped.** Bamboo used to be staged as a three-column
    # slice re-centred on a transparent field, because it was drawn as a
    # full-cell cross and the full sheet came out as a solid square of bamboo
    # colour. It is drawn from `bamboo1_age0.json` now - a 2-wide post whose
    # sides take uv `[0,0,2,16]` and whose lids take `[13,0,15,2]` - so the
    # model's own rects pick the stalk out of the sheet, and a crop would leave
    # the sides sampling columns the crop had cleared.
    @{ Name = 'bamboo.png';                Path = 'block\bamboo_stalk' }
    @{ Name = 'sweet_berry_bush.png';      Path = 'block\sweet_berry_bush_stage3' }
    @{ Name = 'glow_lichen.png';           Path = 'block\glow_lichen' }
    @{ Name = 'pointed_dripstone.png';     Path = 'block\pointed_dripstone_up_tip' }
    @{ Name = 'sea_pickle.png';            Path = 'block\sea_pickle' }
    @{ Name = 'nether_sprouts.png';        Path = 'block\nether_sprouts' }
    @{ Name = 'crimson_roots.png';         Path = 'block\crimson_roots' }
    @{ Name = 'warped_roots.png';          Path = 'block\warped_roots' }
    @{ Name = 'crimson_fungus.png';        Path = 'block\crimson_fungus' }
    @{ Name = 'warped_fungus.png';         Path = 'block\warped_fungus' }
    @{ Name = 'twisting_vines.png';        Path = 'block\twisting_vines' }
    @{ Name = 'weeping_vines.png';         Path = 'block\weeping_vines' }
    @{ Name = 'hanging_roots.png';         Path = 'block\hanging_roots' }
    @{ Name = 'spore_blossom.png';         Path = 'block\spore_blossom' }
    @{ Name = 'amethyst_cluster.png';      Path = 'block\amethyst_cluster' }
    @{ Name = 'large_fern.png';            Path = 'block\large_fern_top';     Tint = $grassTint }
    @{ Name = 'lily_pad.png';              Path = 'block\lily_pad';           Tint = $grassTint }
    @{ Name = 'white_glazed_terracotta.png';      Path = 'block\white_glazed_terracotta' }
    @{ Name = 'orange_glazed_terracotta.png';     Path = 'block\orange_glazed_terracotta' }
    @{ Name = 'magenta_glazed_terracotta.png';    Path = 'block\magenta_glazed_terracotta' }
    @{ Name = 'light_blue_glazed_terracotta.png'; Path = 'block\light_blue_glazed_terracotta' }
    @{ Name = 'yellow_glazed_terracotta.png';     Path = 'block\yellow_glazed_terracotta' }
    @{ Name = 'lime_glazed_terracotta.png';       Path = 'block\lime_glazed_terracotta' }
    @{ Name = 'pink_glazed_terracotta.png';       Path = 'block\pink_glazed_terracotta' }
    @{ Name = 'gray_glazed_terracotta.png';       Path = 'block\gray_glazed_terracotta' }
    @{ Name = 'light_gray_glazed_terracotta.png'; Path = 'block\light_gray_glazed_terracotta' }
    @{ Name = 'cyan_glazed_terracotta.png';       Path = 'block\cyan_glazed_terracotta' }
    @{ Name = 'purple_glazed_terracotta.png';     Path = 'block\purple_glazed_terracotta' }
    @{ Name = 'blue_glazed_terracotta.png';       Path = 'block\blue_glazed_terracotta' }
    @{ Name = 'brown_glazed_terracotta.png';      Path = 'block\brown_glazed_terracotta' }
    @{ Name = 'green_glazed_terracotta.png';      Path = 'block\green_glazed_terracotta' }
    @{ Name = 'red_glazed_terracotta.png';        Path = 'block\red_glazed_terracotta' }
    @{ Name = 'black_glazed_terracotta.png';      Path = 'block\black_glazed_terracotta' }
    @{ Name = 'shroomlight.png';           Path = 'block\shroomlight' }
    @{ Name = 'ochre_froglight_side.png';  Path = 'block\ochre_froglight_side' }
    @{ Name = 'ochre_froglight_top.png';   Path = 'block\ochre_froglight_top' }
    @{ Name = 'verdant_froglight_side.png'; Path = 'block\verdant_froglight_side' }
    @{ Name = 'verdant_froglight_top.png';  Path = 'block\verdant_froglight_top' }
    @{ Name = 'pearlescent_froglight_side.png'; Path = 'block\pearlescent_froglight_side' }
    @{ Name = 'pearlescent_froglight_top.png';  Path = 'block\pearlescent_froglight_top' }
    @{ Name = 'crimson_nylium_side.png';   Path = 'block\crimson_nylium_side' }
    @{ Name = 'crimson_nylium_top.png';    Path = 'block\crimson_nylium' }
    @{ Name = 'warped_nylium_side.png';    Path = 'block\warped_nylium_side' }
    @{ Name = 'warped_nylium_top.png';     Path = 'block\warped_nylium' }
    @{ Name = 'crimson_stem_side.png';     Path = 'block\crimson_stem' }
    @{ Name = 'crimson_stem_top.png';      Path = 'block\crimson_stem_top' }
    @{ Name = 'warped_stem_side.png';      Path = 'block\warped_stem' }
    @{ Name = 'warped_stem_top.png';       Path = 'block\warped_stem_top' }
    @{ Name = 'crimson_planks.png';        Path = 'block\crimson_planks' }
    @{ Name = 'warped_planks.png';         Path = 'block\warped_planks' }
    @{ Name = 'warped_wart_block.png';     Path = 'block\warped_wart_block' }
    @{ Name = 'mangrove_log_side.png';     Path = 'block\mangrove_log' }
    @{ Name = 'mangrove_log_top.png';      Path = 'block\mangrove_log_top' }
    @{ Name = 'mangrove_planks.png';       Path = 'block\mangrove_planks' }
    @{ Name = 'mangrove_leaves.png';       Path = 'block\mangrove_leaves';    Tint = @(93, 158, 66) }
    @{ Name = 'muddy_mangrove_roots_side.png'; Path = 'block\muddy_mangrove_roots_side' }
    @{ Name = 'muddy_mangrove_roots_top.png';  Path = 'block\muddy_mangrove_roots_top' }
    @{ Name = 'bamboo_block_side.png';     Path = 'block\bamboo_block' }
    @{ Name = 'bamboo_block_top.png';      Path = 'block\bamboo_block_top' }
    @{ Name = 'stripped_cherry_log.png';       Path = 'block\stripped_cherry_log' }
    @{ Name = 'stripped_cherry_log_top.png';   Path = 'block\stripped_cherry_log_top' }
    @{ Name = 'stripped_mangrove_log.png';     Path = 'block\stripped_mangrove_log' }
    @{ Name = 'stripped_mangrove_log_top.png'; Path = 'block\stripped_mangrove_log_top' }
    @{ Name = 'stripped_crimson_stem.png';     Path = 'block\stripped_crimson_stem' }
    @{ Name = 'stripped_crimson_stem_top.png'; Path = 'block\stripped_crimson_stem_top' }
    @{ Name = 'stripped_warped_stem.png';      Path = 'block\stripped_warped_stem' }
    @{ Name = 'stripped_warped_stem_top.png';  Path = 'block\stripped_warped_stem_top' }
    @{ Name = 'stripped_bamboo_block.png';     Path = 'block\stripped_bamboo_block' }
    @{ Name = 'stripped_bamboo_block_top.png'; Path = 'block\stripped_bamboo_block_top' }
    @{ Name = 'bamboo_planks.png';         Path = 'block\bamboo_planks' }
    @{ Name = 'bamboo_mosaic.png';         Path = 'block\bamboo_mosaic' }
    @{ Name = 'bone_block_side.png';       Path = 'block\bone_block_side' }
    @{ Name = 'bone_block_top.png';        Path = 'block\bone_block_top' }
    @{ Name = 'quartz_pillar_side.png';    Path = 'block\quartz_pillar_side' }
    @{ Name = 'quartz_pillar_top.png';     Path = 'block\quartz_pillar_top' }
    @{ Name = 'purpur_pillar_side.png';    Path = 'block\purpur_pillar_side' }
    @{ Name = 'purpur_pillar_top.png';     Path = 'block\purpur_pillar_top' }
    @{ Name = 'target_side.png';           Path = 'block\target_side' }
    @{ Name = 'target_top.png';            Path = 'block\target_top' }
    @{ Name = 'snow_block.png';            Path = 'block\snow' }
    @{ Name = 'sculk_catalyst_side.png';   Path = 'block\sculk_catalyst_side' }
    @{ Name = 'sculk_catalyst_top.png';    Path = 'block\sculk_catalyst_top' }
    @{ Name = 'azalea_side.png';           Path = 'block\azalea_side' }
    @{ Name = 'azalea_top.png';            Path = 'block\azalea_top' }
    @{ Name = 'flowering_azalea_side.png'; Path = 'block\flowering_azalea_side' }
    @{ Name = 'flowering_azalea_top.png';  Path = 'block\flowering_azalea_top' }
    # The forty-nine appended items, in `ItemId` order: ten more foods, the
    # materials, and the sixteen dyes white through black.
    @{ Name = 'bread.png';                 Path = 'item\bread' }
    @{ Name = 'cookie.png';                Path = 'item\cookie' }
    @{ Name = 'melon_slice.png';           Path = 'item\melon_slice' }
    @{ Name = 'carrot.png';                Path = 'item\carrot' }
    @{ Name = 'potato.png';                Path = 'item\potato' }
    @{ Name = 'baked_potato.png';          Path = 'item\baked_potato' }
    @{ Name = 'beetroot.png';              Path = 'item\beetroot' }
    @{ Name = 'sweet_berries.png';         Path = 'item\sweet_berries' }
    @{ Name = 'golden_apple.png';          Path = 'item\golden_apple' }
    @{ Name = 'pumpkin_pie.png';           Path = 'item\pumpkin_pie' }
    @{ Name = 'string.png';                Path = 'item\string' }
    @{ Name = 'feather.png';               Path = 'item\feather' }
    @{ Name = 'leather.png';               Path = 'item\leather' }
    @{ Name = 'bone.png';                  Path = 'item\bone' }
    @{ Name = 'gunpowder.png';             Path = 'item\gunpowder' }
    @{ Name = 'slimeball.png';             Path = 'item\slime_ball' }
    @{ Name = 'ink_sac.png';               Path = 'item\ink_sac' }
    @{ Name = 'glow_ink_sac.png';          Path = 'item\glow_ink_sac' }
    @{ Name = 'clay_ball.png';             Path = 'item\clay_ball' }
    @{ Name = 'brick.png';                 Path = 'item\brick' }
    @{ Name = 'flint.png';                 Path = 'item\flint' }
    @{ Name = 'wheat.png';                 Path = 'item\wheat' }
    @{ Name = 'wheat_seeds.png';           Path = 'item\wheat_seeds' }
    @{ Name = 'sugar.png';                 Path = 'item\sugar' }
    @{ Name = 'paper.png';                 Path = 'item\paper' }
    @{ Name = 'book.png';                  Path = 'item\book' }
    @{ Name = 'glass_bottle.png';          Path = 'item\glass_bottle' }
    @{ Name = 'bowl.png';                  Path = 'item\bowl' }
    @{ Name = 'egg.png';                   Path = 'item\egg' }
    @{ Name = 'rotten_flesh.png';          Path = 'item\rotten_flesh' }
    @{ Name = 'spider_eye.png';            Path = 'item\spider_eye' }
    @{ Name = 'honeycomb.png';             Path = 'item\honeycomb' }
    @{ Name = 'honey_bottle.png';          Path = 'item\honey_bottle' }
    @{ Name = 'white_dye.png';             Path = 'item\white_dye' }
    @{ Name = 'orange_dye.png';            Path = 'item\orange_dye' }
    @{ Name = 'magenta_dye.png';           Path = 'item\magenta_dye' }
    @{ Name = 'light_blue_dye.png';        Path = 'item\light_blue_dye' }
    @{ Name = 'yellow_dye.png';            Path = 'item\yellow_dye' }
    @{ Name = 'lime_dye.png';              Path = 'item\lime_dye' }
    @{ Name = 'pink_dye.png';              Path = 'item\pink_dye' }
    @{ Name = 'gray_dye.png';              Path = 'item\gray_dye' }
    @{ Name = 'light_gray_dye.png';        Path = 'item\light_gray_dye' }
    @{ Name = 'cyan_dye.png';              Path = 'item\cyan_dye' }
    @{ Name = 'purple_dye.png';            Path = 'item\purple_dye' }
    @{ Name = 'blue_dye.png';              Path = 'item\blue_dye' }
    @{ Name = 'brown_dye.png';             Path = 'item\brown_dye' }
    @{ Name = 'green_dye.png';             Path = 'item\green_dye' }
    @{ Name = 'red_dye.png';               Path = 'item\red_dye' }
    @{ Name = 'black_dye.png';             Path = 'item\black_dye' }
    # The nine appended items.
    @{ Name = 'lava_bucket.png';           Path = 'item\lava_bucket' }
    @{ Name = 'milk_bucket.png';           Path = 'item\milk_bucket' }
    @{ Name = 'flint_and_steel.png';       Path = 'item\flint_and_steel' }
    @{ Name = 'amethyst_shard.png';        Path = 'item\amethyst_shard' }
    @{ Name = 'quartz.png';                Path = 'item\quartz' }
    @{ Name = 'nether_brick_item.png';     Path = 'item\nether_brick' }
    @{ Name = 'glowstone_dust.png';        Path = 'item\glowstone_dust' }
    @{ Name = 'dried_kelp.png';            Path = 'item\dried_kelp' }
    @{ Name = 'magma_cream.png';           Path = 'item\magma_cream' }
    # The twenty-seven appended items.
    @{ Name = 'glow_berries.png';          Path = 'item\glow_berries' }
    @{ Name = 'rabbit_raw.png';            Path = 'item\rabbit' }
    @{ Name = 'rabbit_cooked.png';         Path = 'item\cooked_rabbit' }
    @{ Name = 'salmon_raw.png';            Path = 'item\salmon' }
    @{ Name = 'salmon_cooked.png';         Path = 'item\cooked_salmon' }
    @{ Name = 'tropical_fish.png';         Path = 'item\tropical_fish' }
    @{ Name = 'pufferfish.png';            Path = 'item\pufferfish' }
    @{ Name = 'beetroot_seeds.png';        Path = 'item\beetroot_seeds' }
    @{ Name = 'melon_seeds.png';           Path = 'item\melon_seeds' }
    @{ Name = 'pumpkin_seeds.png';         Path = 'item\pumpkin_seeds' }
    @{ Name = 'bone_meal.png';             Path = 'item\bone_meal' }
    @{ Name = 'prismarine_shard.png';      Path = 'item\prismarine_shard' }
    @{ Name = 'prismarine_crystals.png';   Path = 'item\prismarine_crystals' }
    @{ Name = 'nautilus_shell.png';        Path = 'item\nautilus_shell' }
    @{ Name = 'heart_of_the_sea.png';      Path = 'item\heart_of_the_sea' }
    @{ Name = 'scute.png';                 Path = 'item\turtle_scute' }
    @{ Name = 'phantom_membrane.png';      Path = 'item\phantom_membrane' }
    @{ Name = 'cinder_rod.png';            Path = 'item\blaze_rod' }
    @{ Name = 'cinder_powder.png';         Path = 'item\blaze_powder' }
    @{ Name = 'drifter_tear.png';          Path = 'item\ghast_tear' }
    @{ Name = 'void_pearl.png';            Path = 'item\ender_pearl' }
    @{ Name = 'void_eye.png';              Path = 'item\ender_eye' }
    @{ Name = 'chorus_fruit.png';          Path = 'item\chorus_fruit' }
    @{ Name = 'popped_chorus_fruit.png';   Path = 'item\popped_chorus_fruit' }
    @{ Name = 'rabbit_hide.png';           Path = 'item\rabbit_hide' }
    @{ Name = 'rabbit_foot.png';           Path = 'item\rabbit_foot' }
    @{ Name = 'echo_shard.png';            Path = 'item\echo_shard' }
    @{ Name = 'water_bottle.png';          Path = 'item\potion' }
    @{ Name = 'bow.png';                   Path = 'item\bow' }
    @{ Name = 'arrow.png';                 Path = 'item\arrow' }
    @{ Name = 'shears.png';                Path = 'item\shears' }
    @{ Name = 'cocoa_beans.png';           Path = 'item\cocoa_beans' }
    @{ Name = 'beehive_front.png';         Path = 'block\beehive_front' }
    @{ Name = 'beehive_front_honey.png';   Path = 'block\beehive_front_honey' }
    @{ Name = 'beehive_side.png';          Path = 'block\beehive_side' }
    @{ Name = 'beehive_end.png';           Path = 'block\beehive_end' }
    # Lava, fire and TNT. Both fluids and fire are animation strips in the
    # reference; frame 0 is taken until the animation machinery covers more
    # than water.
    @{ Name = 'lava.png';                  Path = 'block\lava_still';         Frame = 0 }
    @{ Name = 'fire.png';                  Path = 'block\fire_0';             Frame = 0 }
    @{ Name = 'tnt_top.png';               Path = 'block\tnt_top' }
    @{ Name = 'tnt_bottom.png';            Path = 'block\tnt_bottom' }
    @{ Name = 'tnt_side.png';              Path = 'block\tnt_side' }
    @{ Name = 'ancient_debris_side.png';   Path = 'block\ancient_debris_side' }
    @{ Name = 'ancient_debris_top.png';    Path = 'block\ancient_debris_top' }
    @{ Name = 'emberite_block.png';        Path = 'block\netherite_block' }
    @{ Name = 'smoker_front.png';          Path = 'block\smoker_front' }
    @{ Name = 'smoker_front_on.png';       Path = 'block\smoker_front_on' }
    @{ Name = 'smoker_side.png';           Path = 'block\smoker_side' }
    @{ Name = 'smoker_top.png';            Path = 'block\smoker_top' }
    @{ Name = 'smithing_top.png';          Path = 'block\smithing_table_top' }
    @{ Name = 'smithing_front.png';        Path = 'block\smithing_table_front' }
    @{ Name = 'smithing_side.png';         Path = 'block\smithing_table_side' }
    @{ Name = 'smithing_bottom.png';       Path = 'block\smithing_table_bottom' }
    # The third table run: coloured glass, bars and the light sources.
    @{ Name = 'white_stained_glass.png';      Path = 'block\white_stained_glass' }
    @{ Name = 'orange_stained_glass.png';     Path = 'block\orange_stained_glass' }
    @{ Name = 'magenta_stained_glass.png';    Path = 'block\magenta_stained_glass' }
    @{ Name = 'light_blue_stained_glass.png'; Path = 'block\light_blue_stained_glass' }
    @{ Name = 'yellow_stained_glass.png';     Path = 'block\yellow_stained_glass' }
    @{ Name = 'lime_stained_glass.png';       Path = 'block\lime_stained_glass' }
    @{ Name = 'pink_stained_glass.png';       Path = 'block\pink_stained_glass' }
    @{ Name = 'gray_stained_glass.png';       Path = 'block\gray_stained_glass' }
    @{ Name = 'light_gray_stained_glass.png'; Path = 'block\light_gray_stained_glass' }
    @{ Name = 'cyan_stained_glass.png';       Path = 'block\cyan_stained_glass' }
    @{ Name = 'purple_stained_glass.png';     Path = 'block\purple_stained_glass' }
    @{ Name = 'blue_stained_glass.png';       Path = 'block\blue_stained_glass' }
    @{ Name = 'brown_stained_glass.png';      Path = 'block\brown_stained_glass' }
    @{ Name = 'green_stained_glass.png';      Path = 'block\green_stained_glass' }
    @{ Name = 'red_stained_glass.png';        Path = 'block\red_stained_glass' }
    @{ Name = 'black_stained_glass.png';      Path = 'block\black_stained_glass' }
    @{ Name = 'iron_bars.png';                Path = 'block\iron_bars' }
    # Both lanterns ship as a 16x48 sheet of model parts rather than a strip of
    # frames, and the body is the top square of it.
    @{ Name = 'lantern.png';                  Path = 'block\lantern';      Corner = $true }
    @{ Name = 'soul_lantern.png';             Path = 'block\soul_lantern'; Corner = $true }
    @{ Name = 'soul_torch.png';               Path = 'block\soul_torch' }
    @{ Name = 'redstone_torch.png';           Path = 'block\redstone_torch' }
    @{ Name = 'end_rod.png';                  Path = 'block\end_rod' }

    # ---- The fourth table run: the farm. ----
    @{ Name = 'farmland.png';                 Path = 'block\farmland' }
    @{ Name = 'farmland_moist.png';           Path = 'block\farmland_moist' }
    @{ Name = 'dirt_path_side.png';           Path = 'block\dirt_path_side' }
    @{ Name = 'dirt_path_top.png';            Path = 'block\dirt_path_top' }
    @{ Name = 'melon_stem.png';               Path = 'block\melon_stem' }
    @{ Name = 'attached_melon_stem.png';      Path = 'block\attached_melon_stem' }
    @{ Name = 'pumpkin_stem.png';             Path = 'block\pumpkin_stem' }
    @{ Name = 'attached_pumpkin_stem.png';    Path = 'block\attached_pumpkin_stem' }
    @{ Name = 'carved_pumpkin.png';           Path = 'block\carved_pumpkin' }
    @{ Name = 'jack_o_lantern.png';           Path = 'block\jack_o_lantern' }
    @{ Name = 'composter_top.png';            Path = 'block\composter_top' }
    @{ Name = 'composter_side.png';           Path = 'block\composter_side' }
    @{ Name = 'composter_ready.png';          Path = 'block\composter_ready' }
)

# The crops, one picture per growth stage. Written as loops rather than
# thirty-odd rows because the reference's own names are already `<crop>_stageN`
# - so the loop *is* the mapping, and there is no second list to drift from it.
$sources += 0..7 | ForEach-Object {
    @{ Name = ('wheat_stage{0}.png' -f $_); Path = ('block\wheat_stage{0}' -f $_) }
}
$sources += 0..3 | ForEach-Object {
    @{ Name = ('carrots_stage{0}.png' -f $_); Path = ('block\carrots_stage{0}' -f $_) }
}
$sources += 0..3 | ForEach-Object {
    @{ Name = ('potatoes_stage{0}.png' -f $_); Path = ('block\potatoes_stage{0}' -f $_) }
}
$sources += 0..3 | ForEach-Object {
    @{ Name = ('beetroots_stage{0}.png' -f $_); Path = ('block\beetroots_stage{0}' -f $_) }
}
$sources += 0..2 | ForEach-Object {
    @{ Name = ('nether_wart_stage{0}.png' -f $_); Path = ('block\nether_wart_stage{0}' -f $_) }
}

# ---- The fifth table run: mushroom blocks, coral, copper and decoratives. ----
# The twenty bark blocks are absent on purpose: they point at log sides this
# script already stages, so they need no row of their own.
$sources += @(
    @{ Name = 'brown_mushroom_block.png';     Path = 'block\brown_mushroom_block' }
    @{ Name = 'red_mushroom_block.png';       Path = 'block\red_mushroom_block' }
    @{ Name = 'mushroom_stem.png';            Path = 'block\mushroom_stem' }
    @{ Name = 'copper_grate.png';             Path = 'block\copper_grate' }
    @{ Name = 'exposed_copper_grate.png';     Path = 'block\exposed_copper_grate' }
    @{ Name = 'weathered_copper_grate.png';   Path = 'block\weathered_copper_grate' }
    @{ Name = 'oxidized_copper_grate.png';    Path = 'block\oxidized_copper_grate' }
    @{ Name = 'copper_bulb.png';              Path = 'block\copper_bulb' }
    @{ Name = 'copper_bulb_lit.png';          Path = 'block\copper_bulb_lit' }
    @{ Name = 'exposed_copper_bulb.png';      Path = 'block\exposed_copper_bulb' }
    @{ Name = 'exposed_copper_bulb_lit.png';  Path = 'block\exposed_copper_bulb_lit' }
    @{ Name = 'weathered_copper_bulb.png';    Path = 'block\weathered_copper_bulb' }
    @{ Name = 'weathered_copper_bulb_lit.png'; Path = 'block\weathered_copper_bulb_lit' }
    @{ Name = 'oxidized_copper_bulb.png';     Path = 'block\oxidized_copper_bulb' }
    @{ Name = 'oxidized_copper_bulb_lit.png'; Path = 'block\oxidized_copper_bulb_lit' }
    @{ Name = 'crying_obsidian.png';          Path = 'block\crying_obsidian' }
    @{ Name = 'powder_snow.png';              Path = 'block\powder_snow' }
    @{ Name = 'suspicious_sand.png';          Path = 'block\suspicious_sand_0' }
    @{ Name = 'suspicious_gravel.png';        Path = 'block\suspicious_gravel_0' }
    @{ Name = 'azalea_leaves.png';            Path = 'block\azalea_leaves' }
    @{ Name = 'flowering_azalea_leaves.png';  Path = 'block\flowering_azalea_leaves' }
    @{ Name = 'redstone_lamp.png';            Path = 'block\redstone_lamp' }
    @{ Name = 'redstone_lamp_on.png';         Path = 'block\redstone_lamp_on' }
    @{ Name = 'lodestone_side.png';           Path = 'block\lodestone_side' }
    @{ Name = 'lodestone_top.png';            Path = 'block\lodestone_top' }
    @{ Name = 'enchanting_table_side.png';    Path = 'block\enchanting_table_side' }
    @{ Name = 'enchanting_table_top.png';     Path = 'block\enchanting_table_top' }
    @{ Name = 'chiseled_bookshelf_side.png';  Path = 'block\chiseled_bookshelf_empty' }
    @{ Name = 'chiseled_bookshelf_top.png';   Path = 'block\chiseled_bookshelf_top' }
    @{ Name = 'cartography_table_side.png';   Path = 'block\cartography_table_side1' }
    @{ Name = 'cartography_table_top.png';    Path = 'block\cartography_table_top' }
    @{ Name = 'fletching_table_side.png';     Path = 'block\fletching_table_side' }
    @{ Name = 'fletching_table_top.png';      Path = 'block\fletching_table_top' }
    @{ Name = 'barrel_side.png';              Path = 'block\barrel_side' }
    @{ Name = 'barrel_top.png';               Path = 'block\barrel_top' }
    @{ Name = 'blast_furnace_side.png';       Path = 'block\blast_furnace_side' }
    @{ Name = 'blast_furnace_top.png';        Path = 'block\blast_furnace_top' }
    @{ Name = 'blast_furnace_front.png';      Path = 'block\blast_furnace_front' }
    @{ Name = 'blast_furnace_front_on.png';   Path = 'block\blast_furnace_front_on' }
    @{ Name = 'loom_side.png';                Path = 'block\loom_side' }
    @{ Name = 'loom_top.png';                 Path = 'block\loom_top' }
    @{ Name = 'stonecutter_side.png';         Path = 'block\stonecutter_side' }
    @{ Name = 'stonecutter_top.png';          Path = 'block\stonecutter_top' }
    @{ Name = 'grindstone_side.png';          Path = 'block\grindstone_side' }
    @{ Name = 'grindstone_round.png';         Path = 'block\grindstone_round' }
    @{ Name = 'lectern_sides.png';            Path = 'block\lectern_sides' }
    @{ Name = 'lectern_top.png';              Path = 'block\lectern_top' }
    # `lectern.json` names four textures and we carried two, so the plinth and the
    # post both wore the side art - the lectern has never had a front. These two
    # close that (finding 716). **They are inert until `Main.cpp` lists them and
    # `Block.hpp` names the layers**: this table is keyed by name, not by index
    # (see the `$staged` append below), so an extra row here stages a PNG nothing
    # reads and cannot shift any layer. The list in `Main.cpp` is what the index
    # comes from, and per its own note the three files must be *agreed first and
    # landed together* - append at the end of the run in all three or every layer
    # past the seam moves by one.
    @{ Name = 'lectern_front.png';            Path = 'block\lectern_front' }
    @{ Name = 'lectern_base.png';             Path = 'block\lectern_base' }
    @{ Name = 'bell_side.png';                Path = 'block\bell_side' }
    @{ Name = 'bell_top.png';                 Path = 'block\bell_top' }
    @{ Name = 'cauldron_side.png';            Path = 'block\cauldron_side' }
    @{ Name = 'cauldron_top.png';             Path = 'block\cauldron_top' }
    @{ Name = 'hopper_outside.png';           Path = 'block\hopper_outside' }
    @{ Name = 'hopper_top.png';               Path = 'block\hopper_top' }
    @{ Name = 'shulker_box.png';              Path = 'block\shulker_box' }
    @{ Name = 'white_shulker_box.png';        Path = 'block\white_shulker_box' }
    @{ Name = 'orange_shulker_box.png';       Path = 'block\orange_shulker_box' }
    @{ Name = 'magenta_shulker_box.png';      Path = 'block\magenta_shulker_box' }
    @{ Name = 'light_blue_shulker_box.png';   Path = 'block\light_blue_shulker_box' }
    @{ Name = 'yellow_shulker_box.png';       Path = 'block\yellow_shulker_box' }
    @{ Name = 'lime_shulker_box.png';         Path = 'block\lime_shulker_box' }
    @{ Name = 'pink_shulker_box.png';         Path = 'block\pink_shulker_box' }
    @{ Name = 'gray_shulker_box.png';         Path = 'block\gray_shulker_box' }
    @{ Name = 'light_gray_shulker_box.png';   Path = 'block\light_gray_shulker_box' }
    @{ Name = 'cyan_shulker_box.png';         Path = 'block\cyan_shulker_box' }
    @{ Name = 'purple_shulker_box.png';       Path = 'block\purple_shulker_box' }
    @{ Name = 'blue_shulker_box.png';         Path = 'block\blue_shulker_box' }
    @{ Name = 'brown_shulker_box.png';        Path = 'block\brown_shulker_box' }
    @{ Name = 'green_shulker_box.png';        Path = 'block\green_shulker_box' }
    @{ Name = 'red_shulker_box.png';          Path = 'block\red_shulker_box' }
    @{ Name = 'black_shulker_box.png';        Path = 'block\black_shulker_box' }
    @{ Name = 'brewing_stand_base.png';       Path = 'block\brewing_stand_base' }
    @{ Name = 'brewing_stand.png';            Path = 'block\brewing_stand' }
    @{ Name = 'anvil_base.png';               Path = 'block\anvil' }
    @{ Name = 'anvil_top.png';                Path = 'block\anvil_top' }
    @{ Name = 'chipped_anvil_top.png';        Path = 'block\chipped_anvil_top' }
    @{ Name = 'damaged_anvil_top.png';        Path = 'block\damaged_anvil_top' }
    @{ Name = 'scaffolding_side.png';         Path = 'block\scaffolding_side' }
    @{ Name = 'scaffolding_top.png';          Path = 'block\scaffolding_top' }
    @{ Name = 'flower_pot.png';               Path = 'block\flower_pot' }
    @{ Name = 'sculk_vein.png';               Path = 'block\sculk_vein' }
    @{ Name = 'sculk_sensor_side.png';        Path = 'block\sculk_sensor_side' }
    @{ Name = 'sculk_sensor_top.png';         Path = 'block\sculk_sensor_top' }
    @{ Name = 'sculk_shrieker_side.png';      Path = 'block\sculk_shrieker_side' }
    @{ Name = 'sculk_shrieker_top.png';       Path = 'block\sculk_shrieker_top' }
    @{ Name = 'small_amethyst_bud.png';       Path = 'block\small_amethyst_bud' }
    @{ Name = 'medium_amethyst_bud.png';      Path = 'block\medium_amethyst_bud' }
    @{ Name = 'large_amethyst_bud.png';       Path = 'block\large_amethyst_bud' }
    @{ Name = 'big_dripleaf_top.png';         Path = 'block\big_dripleaf_top' }
    @{ Name = 'small_dripleaf_top.png';       Path = 'block\small_dripleaf_top' }
    @{ Name = 'cave_vines.png';               Path = 'block\cave_vines' }
    @{ Name = 'cave_vines_lit.png';           Path = 'block\cave_vines_lit' }
    @{ Name = 'chorus_plant.png';             Path = 'block\chorus_plant' }
    @{ Name = 'chorus_flower.png';            Path = 'block\chorus_flower' }
    @{ Name = 'sunflower_bottom.png';         Path = 'block\sunflower_bottom' }
    @{ Name = 'sunflower_top.png';            Path = 'block\sunflower_top' }
    @{ Name = 'lilac_bottom.png';             Path = 'block\lilac_bottom' }
    @{ Name = 'lilac_top.png';                Path = 'block\lilac_top' }
    @{ Name = 'rose_bush_bottom.png';         Path = 'block\rose_bush_bottom' }
    @{ Name = 'rose_bush_top.png';            Path = 'block\rose_bush_top' }
    @{ Name = 'peony_bottom.png';             Path = 'block\peony_bottom' }
    @{ Name = 'peony_top.png';                Path = 'block\peony_top' }
    @{ Name = 'wither_rose.png';              Path = 'block\wither_rose' }
    @{ Name = 'campfire_log.png';             Path = 'block\campfire_log' }
    @{ Name = 'campfire_log_lit.png';         Path = 'block\campfire_log_lit' }
    # A replacement, not an addition - `Main.cpp` says why at the matching
    # name. This slot used to stage `soul_campfire_fire`, the animated flame
    # sheet, which `Block.hpp` then read as the soul campfire's ember log.
    # `soul_campfire_log_lit` is 16x64, exactly its ordinary twin above, so
    # it needs no `Frame` either.
    @{ Name = 'soul_campfire_log_lit.png';    Path = 'block\soul_campfire_log_lit' }
    @{ Name = 'respawn_anchor_side.png';      Path = 'block\respawn_anchor_side0' }
    @{ Name = 'respawn_anchor_top.png';       Path = 'block\respawn_anchor_top' }
)

# The five corals, in the one order every coral family uses. Written as loops so
# the order **is** the mapping and there is no second list to drift from it.
$coralSpecies = @('tube', 'brain', 'bubble', 'fire', 'horn')
foreach ($species in $coralSpecies) {
    $sources += @{ Name = "dead_${species}_coral_block.png"; Path = "block\dead_${species}_coral_block" }
}
foreach ($species in $coralSpecies) {
    $sources += @{ Name = "${species}_coral.png"; Path = "block\${species}_coral" }
}
foreach ($species in $coralSpecies) {
    $sources += @{ Name = "dead_${species}_coral.png"; Path = "block\dead_${species}_coral" }
}
foreach ($species in $coralSpecies) {
    $sources += @{ Name = "${species}_coral_fan.png"; Path = "block\${species}_coral_fan" }
}
foreach ($species in $coralSpecies) {
    $sources += @{ Name = "dead_${species}_coral_fan.png"; Path = "block\dead_${species}_coral_fan" }
}

# The candles, plain then the sixteen dyes. **Staged from the *lit* art**,
# because ours is always burning - a candle that can be out is four counts times
# two states times seventeen colours, and this run can be widened later without
# moving anything.
$sources += @{ Name = 'candle.png'; Path = 'block\candle_lit' }
$sources += @{ Name = 'candle_unlit.png'; Path = 'block\candle' }
$candleColours = @('white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray',
                   'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black')
foreach ($colour in $candleColours) {
    $sources += @{ Name = "${colour}_candle.png"; Path = "block\${colour}_candle_lit" }
}
foreach ($colour in $candleColours) {
    $sources += @{ Name = "${colour}_candle_unlit.png"; Path = "block\${colour}_candle" }
}
$sources += @(
    @{ Name = 'tinted_glass.png';             Path = 'block\tinted_glass' }
    @{ Name = 'beacon.png';                   Path = 'block\beacon' }
    @{ Name = 'conduit.png';                  Path = 'block\conduit' }
    @{ Name = 'dragon_egg.png';               Path = 'block\dragon_egg' }
    @{ Name = 'end_portal_frame_side.png';    Path = 'block\end_portal_frame_side' }
    @{ Name = 'end_portal_frame_top.png';     Path = 'block\end_portal_frame_top' }
    @{ Name = 'spawner.png';                  Path = 'block\spawner' }
)

# Doors and trapdoors, in the same order as `kDoorFamilies` and
# `kTrapdoorFamilies` - the loop **is** the mapping, so there is no second list
# to drift from it. Oak's files are unprefixed in the reference dump.
$openingWoods = @('oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak', 'cherry', 'mangrove',
                  'crimson', 'warped', 'bamboo', 'iron')
foreach ($wood in $openingWoods) {
    $sources += @{ Name = "${wood}_door_bottom.png"; Path = "block\${wood}_door_bottom" }
    $sources += @{ Name = "${wood}_door_top.png";    Path = "block\${wood}_door_top" }
}
foreach ($wood in $openingWoods) {
    $trapdoorPath = if ($wood -eq 'oak') { 'block\oak_trapdoor' } else { "block\${wood}_trapdoor" }
    $sources += @{ Name = "${wood}_trapdoor.png"; Path = $trapdoorPath }
}

# The sixteen beds, four faces each in the order `bedFamilyAt` reads them:
# foot top, foot side, head top, head side.
foreach ($colour in $candleColours) {
    $sources += @{ Name = "${colour}_bed_foot_top.png";  Path = "block\${colour}_bed_foot_up" }
    $sources += @{ Name = "${colour}_bed_foot_side.png"; Path = "block\${colour}_bed_foot_east" }
    $sources += @{ Name = "${colour}_bed_head_top.png";  Path = "block\${colour}_bed_head_up" }
    $sources += @{ Name = "${colour}_bed_head_side.png"; Path = "block\${colour}_bed_head_east" }
}

# ---- The appended items. ----
# Leather armour ships as **two** images: a greyscale layer the game dyes, and
# an undyed overlay carrying the buckles and trim. The dyeable layer alone has
# real holes in it where the overlay is meant to show through, so staging it on
# its own produced a set of rags with gaps punched in them.
$leatherTint = @(167, 105, 67)
$sources += @(
    @{ Name = 'nether_wart.png';              Path = 'item\nether_wart' }
    @{ Name = 'leather_helmet.png';           Path = 'item\leather_helmet';     Tint = $leatherTint;
       Overlay = 'item\leather_helmet_overlay' }
    @{ Name = 'leather_chestplate.png';       Path = 'item\leather_chestplate'; Tint = $leatherTint;
       Overlay = 'item\leather_chestplate_overlay' }
    @{ Name = 'leather_leggings.png';         Path = 'item\leather_leggings';   Tint = $leatherTint;
       Overlay = 'item\leather_leggings_overlay' }
    @{ Name = 'leather_boots.png';            Path = 'item\leather_boots';      Tint = $leatherTint;
       Overlay = 'item\leather_boots_overlay' }
    @{ Name = 'chainmail_helmet.png';         Path = 'item\chainmail_helmet' }
    @{ Name = 'chainmail_chestplate.png';     Path = 'item\chainmail_chestplate' }
    @{ Name = 'chainmail_leggings.png';       Path = 'item\chainmail_leggings' }
    @{ Name = 'chainmail_boots.png';          Path = 'item\chainmail_boots' }
    @{ Name = 'iron_helmet.png';              Path = 'item\iron_helmet' }
    @{ Name = 'iron_chestplate.png';          Path = 'item\iron_chestplate' }
    @{ Name = 'iron_leggings.png';            Path = 'item\iron_leggings' }
    @{ Name = 'iron_boots.png';               Path = 'item\iron_boots' }
    @{ Name = 'golden_helmet.png';            Path = 'item\golden_helmet' }
    @{ Name = 'golden_chestplate.png';        Path = 'item\golden_chestplate' }
    @{ Name = 'golden_leggings.png';          Path = 'item\golden_leggings' }
    @{ Name = 'golden_boots.png';             Path = 'item\golden_boots' }
    @{ Name = 'diamond_helmet.png';           Path = 'item\diamond_helmet' }
    @{ Name = 'diamond_chestplate.png';       Path = 'item\diamond_chestplate' }
    @{ Name = 'diamond_leggings.png';         Path = 'item\diamond_leggings' }
    @{ Name = 'diamond_boots.png';            Path = 'item\diamond_boots' }
    # Emberite is our name for the reference's dark alloy, so its art is that
    # alloy's - the same arrangement as every other staged placeholder.
    @{ Name = 'emberite_helmet.png';          Path = 'item\netherite_helmet' }
    @{ Name = 'emberite_chestplate.png';      Path = 'item\netherite_chestplate' }
    @{ Name = 'emberite_leggings.png';        Path = 'item\netherite_leggings' }
    @{ Name = 'emberite_boots.png';           Path = 'item\netherite_boots' }
    @{ Name = 'turtle_helmet.png';            Path = 'item\turtle_helmet' }
    # The shield has no item sprite of its own - it is drawn from a 64x64 entity
    # net - so the interface's own placeholder is what stands in for the icon.
    @{ Name = 'shield.png';                   Path = 'gui\sprites\container\slot\shield' }
    # **The fifteen `music_disc_*` rows that stood here are gone**, with the
    # fifteen matching sprite-list rows in `Main.cpp`. They were a second,
    # hand-written disc run duplicating ids the real one already owns - the
    # twenty-two-entry `$discs` run further down, which stages the same source
    # art plus seven more. Nothing here is orphaned by the cut.
    @{ Name = 'saddle.png';                   Path = 'item\saddle' }
    @{ Name = 'name_tag.png';                 Path = 'item\name_tag' }
    @{ Name = 'lead.png';                     Path = 'item\lead' }
    @{ Name = 'elytra.png';                   Path = 'item\elytra' }
    @{ Name = 'totem_of_undying.png';         Path = 'item\totem_of_undying' }
    @{ Name = 'spyglass.png';                 Path = 'item\spyglass' }
    @{ Name = 'brush.png';                    Path = 'item\brush' }
    @{ Name = 'trident.png';                  Path = 'item\trident' }
    @{ Name = 'crossbow.png';                 Path = 'item\crossbow_standby' }
    @{ Name = 'fishing_rod.png';              Path = 'item\fishing_rod' }
    # Both dials ship as sixteen and sixty-four frame sets; frame 0 is the one
    # that reads as a compass rather than a smear.
    @{ Name = 'compass.png';                  Path = 'item\compass_00' }
    @{ Name = 'clock.png';                    Path = 'item\clock_00' }
    @{ Name = 'empty_map.png';                Path = 'item\map' }
    @{ Name = 'filled_map.png';               Path = 'item\filled_map' }
    @{ Name = 'recovery_compass.png';         Path = 'item\recovery_compass_00' }
    @{ Name = 'firework_rocket.png';          Path = 'item\firework_rocket' }
    @{ Name = 'writable_book.png';            Path = 'item\writable_book' }
    @{ Name = 'written_book.png';             Path = 'item\written_book' }
    @{ Name = 'mushroom_stew.png';            Path = 'item\mushroom_stew' }
    @{ Name = 'beetroot_soup.png';            Path = 'item\beetroot_soup' }
    @{ Name = 'rabbit_stew.png';              Path = 'item\rabbit_stew' }
    @{ Name = 'suspicious_stew.png';          Path = 'item\suspicious_stew' }
    # The enchanted apple has no sprite of its own; the reference draws the
    # ordinary one with a glint over it, and we have no glint.
    @{ Name = 'enchanted_golden_apple.png';   Path = 'item\golden_apple' }
    @{ Name = 'poisonous_potato.png';         Path = 'item\poisonous_potato' }
    @{ Name = 'golden_carrot.png';            Path = 'item\golden_carrot' }
    @{ Name = 'glistering_melon_slice.png';   Path = 'item\glistering_melon_slice' }
    @{ Name = 'powder_snow_bucket.png';       Path = 'item\powder_snow_bucket' }
    @{ Name = 'cod_bucket.png';               Path = 'item\cod_bucket' }
    @{ Name = 'salmon_bucket.png';            Path = 'item\salmon_bucket' }
    @{ Name = 'tropical_fish_bucket.png';     Path = 'item\tropical_fish_bucket' }
    @{ Name = 'pufferfish_bucket.png';        Path = 'item\pufferfish_bucket' }
    @{ Name = 'axolotl_bucket.png';           Path = 'item\axolotl_bucket' }
    @{ Name = 'iron_nugget.png';              Path = 'item\iron_nugget' }
    @{ Name = 'gold_nugget.png';              Path = 'item\gold_nugget' }
)

# The water surface is a thirty-two frame strip and the game plays all of it, so
# every frame is staged as its own layer. `water.png` above stays as frame 0 for
# anything that wants a single still image.
$sources += 0..31 | ForEach-Object {
    @{ Name = ('water{0:d2}.png' -f $_); Path = 'block\water_still'; Tint = $waterTint; Frame = $_ }
}

# Fire's own strip, on the same arrangement.
$sources += 0..31 | ForEach-Object {
    @{ Name = ('fire{0:d2}.png' -f $_); Path = 'block\fire_0'; Frame = $_ }
}

# The drawn bow, and the arrow entity's sheet. That sheet is 32x32 and both
# rects the arrow's model samples sit in its top-left corner, so `Corner` crops
# rather than resizes - a resize would blur one layer of an array whose layers
# all have to be the same size.
$sources += @(
    @{ Name = 'bow_pulling_0.png';         Path = 'item\bow_pulling_0' }
    @{ Name = 'bow_pulling_1.png';         Path = 'item\bow_pulling_1' }
    @{ Name = 'bow_pulling_2.png';         Path = 'item\bow_pulling_2' }
    @{ Name = 'arrow_entity.png';          Path = 'entity\projectiles\arrow'; Corner = $true }
    @{ Name = 'ladder.png';                Path = 'block\ladder' }
    # A vine is greyscale and tinted at run time from the **foliage** table, the
    # same one the leaves use - not the grass table.
    @{ Name = 'vine.png';                  Path = 'block\vine';               Tint = $foliageTint }
    @{ Name = 'cocoa_stage0.png';          Path = 'block\cocoa_stage0' }
    @{ Name = 'cocoa_stage1.png';          Path = 'block\cocoa_stage1' }
    @{ Name = 'cocoa_stage2.png';          Path = 'block\cocoa_stage2' }
)

# The sky. Both bodies ship at 32x32 with the body itself in the middle eight
# columns, so `Centre` crops the middle 16x16 - see the note where it is
# implemented. `kSunRadius` in Sky.hpp is scaled to match that proportion.
#
# **The eight phases are eight files, in the order the moon runs through them**,
# and 26.2 splits them into their own folder rather than the single strip earlier
# versions shipped. `kMoonPhaseFirst` in Block.hpp counts in the same order.
$sources += @(
    @{ Name = 'sun.png';                   Path = 'environment\celestial\sun';                    Centre = $true }
    @{ Name = 'moon_full.png';             Path = 'environment\celestial\moon\full_moon';         Centre = $true }
    @{ Name = 'moon_waning_gibbous.png';   Path = 'environment\celestial\moon\waning_gibbous';    Centre = $true }
    @{ Name = 'moon_third_quarter.png';    Path = 'environment\celestial\moon\third_quarter';     Centre = $true }
    @{ Name = 'moon_waning_crescent.png';  Path = 'environment\celestial\moon\waning_crescent';   Centre = $true }
    @{ Name = 'moon_new.png';              Path = 'environment\celestial\moon\new_moon';          Centre = $true }
    @{ Name = 'moon_waxing_crescent.png';  Path = 'environment\celestial\moon\waxing_crescent';   Centre = $true }
    @{ Name = 'moon_first_quarter.png';    Path = 'environment\celestial\moon\first_quarter';     Centre = $true }
    @{ Name = 'moon_waxing_gibbous.png';   Path = 'environment\celestial\moon\waxing_gibbous';    Centre = $true }
    # The stonecutter's saw blade, which is a plane standing out of the bench
    # rather than one of its faces - so it needs its own layer.
    @{ Name = 'stonecutter_saw.png';       Path = 'block\stonecutter_saw' }
    # The end face of a bed's head, which is the pillow's own end and is white
    # all the way across. It carries no colour in its name because the reference
    # shares one image across all sixteen beds, and the side texture cannot
    # stand in for it: that one is half pillow and half blanket, so the
    # headboard came out half red.
    @{ Name = 'bed_head_north.png';        Path = 'block\bed_head_north' }
    # The compost inside a composter. `composter_top` is a rim with a
    # transparent middle, which is the very rectangle the contents plate
    # samples, so the tub had to have this one of its own.
    @{ Name = 'composter_compost.png';     Path = 'block\composter_compost' }
)

# ---- Redstone. ----
# Everything here is appended after every existing run, which is the rule for
# adding layers: anything inserted in the middle slides every layer behind it.
$sources += @(
    @{ Name = 'redstone_torch_off.png';    Path = 'block\redstone_torch_off' }
    @{ Name = 'lever.png';                 Path = 'block\lever' }
    @{ Name = 'repeater.png';              Path = 'block\repeater' }
    @{ Name = 'repeater_on.png';           Path = 'block\repeater_on' }
    @{ Name = 'comparator.png';            Path = 'block\comparator' }
    @{ Name = 'comparator_on.png';         Path = 'block\comparator_on' }
    # The bench a repeater and a comparator stand on. Staged again under its own
    # name so a model box can name it as a plain layer number rather than having
    # to look up where smooth stone landed in the table run.
    @{ Name = 'redstone_slab.png';         Path = 'block\smooth_stone' }
    @{ Name = 'observer_front.png';        Path = 'block\observer_front' }
    @{ Name = 'observer_back.png';         Path = 'block\observer_back' }
    @{ Name = 'observer_back_on.png';      Path = 'block\observer_back_on' }
    @{ Name = 'observer_side.png';         Path = 'block\observer_side' }
    @{ Name = 'observer_top.png';          Path = 'block\observer_top' }
    @{ Name = 'piston_top.png';            Path = 'block\piston_top' }
    @{ Name = 'piston_top_sticky.png';     Path = 'block\piston_top_sticky' }
    @{ Name = 'piston_side.png';           Path = 'block\piston_side' }
    @{ Name = 'piston_bottom.png';         Path = 'block\piston_bottom' }
    @{ Name = 'piston_inner.png';          Path = 'block\piston_inner' }
    @{ Name = 'dispenser_front.png';          Path = 'block\dispenser_front' }
    @{ Name = 'dispenser_front_vertical.png'; Path = 'block\dispenser_front_vertical' }
    @{ Name = 'dropper_front.png';            Path = 'block\dropper_front' }
    @{ Name = 'dropper_front_vertical.png';   Path = 'block\dropper_front_vertical' }
    # A dispenser and a dropper wear a furnace's sides and top in the reference
    # too. Staged again for the same reason the bench is: these are reached from
    # a model box, not from a `TextureLayer` enumerator.
    @{ Name = 'machine_side.png';          Path = 'block\furnace_side' }
    @{ Name = 'machine_top.png';           Path = 'block\furnace_top' }
    @{ Name = 'daylight_detector_side.png';         Path = 'block\daylight_detector_side' }
    @{ Name = 'daylight_detector_top.png';          Path = 'block\daylight_detector_top' }
    @{ Name = 'daylight_detector_inverted_top.png'; Path = 'block\daylight_detector_inverted_top' }
    @{ Name = 'lightning_rod.png';         Path = 'block\lightning_rod' }
    @{ Name = 'lightning_rod_on.png';      Path = 'block\lightning_rod_on' }
    @{ Name = 'tripwire_hook.png';         Path = 'block\tripwire_hook' }
    @{ Name = 'tripwire.png';              Path = 'block\tripwire' }
    @{ Name = 'rail.png';                  Path = 'block\rail' }
    @{ Name = 'rail_corner.png';           Path = 'block\rail_corner' }
    @{ Name = 'powered_rail.png';          Path = 'block\powered_rail' }
    @{ Name = 'powered_rail_on.png';       Path = 'block\powered_rail_on' }
    @{ Name = 'detector_rail.png';         Path = 'block\detector_rail' }
    @{ Name = 'detector_rail_on.png';      Path = 'block\detector_rail_on' }
    @{ Name = 'activator_rail.png';        Path = 'block\activator_rail' }
    @{ Name = 'activator_rail_on.png';     Path = 'block\activator_rail_on' }
    @{ Name = 'redstone_lamp_on.png';      Path = 'block\redstone_lamp_on' }
)

# Redstone dust, sixteen times, one per signal strength.
#
# **The reference ships this art grey and multiplies a colour in at draw time**,
# from `power/15`, so a copied-raw wire would be a white cross. We have no
# per-block tint, so the sixteen colours are baked into sixteen layers and the
# wire's own id picks one - which is the same trade the grass block already
# makes, only sixteen times over.
#
# The formula is the reference's own: red climbs from 0.3 at rest to 1.0 at full
# strength while green and blue stay at nothing until the signal is strong, which
# is what makes a long run fade from orange-red to near-black rather than dimming
# evenly.
#
# `line0` and `line1` are the two halves of a cross - one running each way - so
# staging one over the other is what gives the unconnected shape the reference
# itself draws when a wire has no neighbours.
$sources += 0..15 | ForEach-Object {
    $f = $_ / 15.0
    $r = if ($_ -eq 0) { 0.3 } else { $f * 0.6 + 0.4 }
    $g = [Math]::Max(0.0, [Math]::Min(1.0, $f * $f * 0.7 - 0.5))
    $b = [Math]::Max(0.0, [Math]::Min(1.0, $f * $f * 0.6 - 0.7))
    $tint = @([int]($r * 255), [int]($g * 255), [int]($b * 255))
    @{ Name = ('redstone_dust_{0:d2}.png' -f $_); Path = 'block\redstone_dust_line0';
       Tint = $tint; Overlay = 'block\redstone_dust_line1'; OverlayTint = $tint }
}

# ---- Brewing. ----
# The two reagents that had no item yet. Magma cream, the phantom membrane and
# the rabbit's foot were already staged with the equipment.
#
# **Three rows shorter than it was.** `blaze_rod`, `blaze_powder` and
# `ghast_tear` were Mojang's names for ids already staged above under the
# project's own coined ones - `cinder_rod.png`, `cinder_powder.png` and
# `drifter_tear.png`, which point at these very same three source paths. Two
# staged pictures for one id is two chances to draw the wrong one, and only the
# coined names may ship.
$sources += @(
    @{ Name = 'fermented_spider_eye.png';  Path = 'item\fermented_spider_eye' }
    @{ Name = 'dragon_breath.png';         Path = 'item\dragon_breath' }
)

# Every potion, three ways.
#
# **The reference draws a potion as two layers**: `potion_overlay` is the liquid
# and is tinted from the effect's own colour, and `potion` is the glass over the
# top of it with a see-through belly. We have no per-item tint, so each of the
# forty-one colours is composited once here - the same trade the redstone wire
# makes, and the reason a potion of swiftness comes out blue without the game
# knowing anything about it.
#
# **The colours are the effect colours from `Effects.hpp`**, and they are the
# reference's own post-1.19.80 values. The four at the top carry no effect and
# take the washed blue the reference gives water, mundane, thick and awkward.
# Keep this list in `kPotions` order; the game indexes straight across.
$potionTints = @(
    0x385DC6, 0x385DC6, 0x385DC6, 0x385DC6,
    0xC2FF66, 0xC2FF66,
    0xF6F6F6, 0xF6F6F6,
    0xFDFF84, 0xFDFF84, 0xFDFF84,
    0xFF9900, 0xFF9900,
    0x33EBFF, 0x33EBFF, 0x33EBFF,
    0x8BAFE0, 0x8BAFE0, 0x8BAFE0,
    0x98DAC0, 0x98DAC0,
    0xF82423, 0xF82423,
    0xA9656A, 0xA9656A,
    0x87A363, 0x87A363, 0x87A363,
    0xCD5CAB, 0xCD5CAB, 0xCD5CAB,
    0xFFC700, 0xFFC700, 0xFFC700,
    0x484D48, 0x484D48,
    0x9146F0, 0x9146F0, 0x9146F0,
    0xF3CFB9, 0xF3CFB9
)
if ($potionTints.Count -ne 41) {
    throw "the potion tint list is $($potionTints.Count) long; kPotionTypes says 41"
}

$sources += 0..40 | ForEach-Object {
    $c = $potionTints[$_]
    $tint = @((($c -shr 16) -band 0xFF), (($c -shr 8) -band 0xFF), ($c -band 0xFF))
    @{ Name = ('potion_{0:d2}.png' -f $_); Path = 'item\potion_overlay'; Tint = $tint;
       Overlay = 'item\potion' }
}
$sources += 0..40 | ForEach-Object {
    $c = $potionTints[$_]
    $tint = @((($c -shr 16) -band 0xFF), (($c -shr 8) -band 0xFF), ($c -band 0xFF))
    @{ Name = ('splash_potion_{0:d2}.png' -f $_); Path = 'item\potion_overlay'; Tint = $tint;
       Overlay = 'item\splash_potion' }
}
# Tipped arrows start at the first potion that carries an effect - there is
# nothing to tip an arrow with in a water bottle.
$sources += 4..40 | ForEach-Object {
    $c = $potionTints[$_]
    $tint = @((($c -shr 16) -band 0xFF), (($c -shr 8) -band 0xFF), ($c -band 0xFF))
    @{ Name = ('tipped_arrow_{0:d2}.png' -f $_); Path = 'item\tipped_arrow_base';
       Overlay = 'item\tipped_arrow_head'; OverlayTint = $tint }
}
$sources += 0..40 | ForEach-Object {
    $c = $potionTints[$_]
    $tint = @((($c -shr 16) -band 0xFF), (($c -shr 8) -band 0xFF), ($c -band 0xFF))
    @{ Name = ('lingering_potion_{0:d2}.png' -f $_); Path = 'item\potion_overlay'; Tint = $tint;
       Overlay = 'item\lingering_potion' }
}

# ---- Collectibles. ----# Twenty-three sherds, one horn and twenty-two discs. The names here are the
# reference's file names; what the game calls each one is its own business and
# lives in Item.hpp.
$sherds = @('angler', 'archer', 'arms_up', 'blade', 'brewer', 'burn', 'danger', 'explorer',
            'flow', 'friend', 'guster', 'heart', 'heartbreak', 'howl', 'miner', 'mourner',
            'plenty', 'prize', 'scrape', 'sheaf', 'shelter', 'skull', 'snort')
$sources += $sherds | ForEach-Object {
    @{ Name = ('sherd_{0}.png' -f $_); Path = ('item\{0}_pottery_sherd' -f $_) }
}
$sources += @( @{ Name = 'goat_horn.png'; Path = 'item\goat_horn' } )
# **In the order Item.hpp lists them, not alphabetically.** The two runs are
# matched index for index, so a different order here quietly hands every disc
# the wrong picture.
$discs = @('13', 'cat', 'blocks', 'chirp', 'far', 'mall', 'mellohi', 'stal', 'strad', 'ward',
           '11', 'wait', 'otherside', 'pigstep', 'relic', '5', 'creator',
           'creator_music_box', 'precipice', 'tears', 'lava_chicken', 'bounce')
$sources += 0..($discs.Count - 1) | ForEach-Object {
    @{ Name = ('music_disc_{0:d2}.png' -f $_); Path = ('item\music_disc_{0}' -f $discs[$_]) }
}

# ---- Firework stars. ----
# **Two layers, like the potion**: the star is tinted by the dye and the overlay
# is the sparkle drawn over it untinted. Sixteen composites here rather than a
# per-item tint the renderer does not have.
$dyeColours = @(0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA, 0x474F52,
                0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21)
$sources += 0..15 | ForEach-Object {
    $c = $dyeColours[$_]
    $tint = @((($c -shr 16) -band 0xFF), (($c -shr 8) -band 0xFF), ($c -band 0xFF))
    @{ Name = ('firework_star_{0:d2}.png' -f $_); Path = 'item\firework_star'; Tint = $tint;
       Overlay = 'item\firework_star_overlay' }
}

# ---- The ten breaking stages, drawn over whatever is being mined. ----
# Measured before staging them: each is 16x16 with **no fully clear texel and no
# partial one worth the name** - the crack lines are alpha 255 and the field
# between them is alpha 1. So they are effectively a cutout already, which is
# what lets them go through the ordinary alpha-tested path with no blending, no
# second pipeline and no depth trouble at all.
$sources += 0..9 | ForEach-Object {
    @{ Name = ('destroy_stage_{0}.png' -f $_); Path = ('block\destroy_stage_{0}' -f $_) }
}

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

# The chest is the one block whose art is not a block texture at all: the
# reference draws it as a *model* and paints it from `entity/chest/normal.png`,
# a 64x64 box net. So its three faces are cut out of that net and padded, never
# rescaled - the lid's rect stacked on the base's, which is exactly what a chest
# looks like from the side.
#
# A chest is also 14 texels wide rather than 16, so the crop is short in both
# axes. The margin replicates the outermost row and column rather than being
# left clear: we draw a full cube, and a transparent border would show the world
# through the corner of every chest.
# `Align` and `Mirror` exist for the two halves of a double chest and default to
# what a single chest already did, so the three faces above are byte-identical.
function New-ChestFace {
    param([System.Drawing.Bitmap]$Net, [int[]]$Upper, [int[]]$Lower, [int[]]$Decal, [int[]]$DecalAt,
          [switch]$Mirror, [ValidateSet('Centre', 'Left', 'Right')][string]$Align = 'Centre')

    $face = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $parts = @()
    $parts += ,$Upper
    if ($Lower) { $parts += ,$Lower }

    $height = 0
    foreach ($part in $parts) { $height += $part[3] }
    $left = switch ($Align) {
        'Left'  { 0 }
        'Right' { $size - $Upper[2] }
        default { [int](($size - $Upper[2]) / 2) }
    }
    $top = [int](($size - $height) / 2)

    $rowY = $top
    foreach ($part in $parts) {
        for ($y = 0; $y -lt $part[3]; $y++) {
            for ($x = 0; $x -lt $part[2]; $x++) {
                $srcX = if ($Mirror) { $part[0] + $part[2] - 1 - $x } else { $part[0] + $x }
                $face.SetPixel(($left + $x), ($rowY + $y), $Net.GetPixel($srcX, ($part[1] + $y)))
            }
        }
        $rowY += $part[3]
    }

    # Replicate outward into the margin, columns first then rows, so a corner
    # picks up the nearest real pixel rather than staying empty.
    for ($y = $top; $y -lt ($top + $height); $y++) {
        for ($x = 0; $x -lt $left; $x++) { $face.SetPixel($x, $y, $face.GetPixel($left, $y)) }
        for ($x = ($left + $Upper[2]); $x -lt $size; $x++) {
            $face.SetPixel($x, $y, $face.GetPixel(($left + $Upper[2] - 1), $y))
        }
    }
    for ($x = 0; $x -lt $size; $x++) {
        for ($y = 0; $y -lt $top; $y++) { $face.SetPixel($x, $y, $face.GetPixel($x, $top)) }
        for ($y = ($top + $height); $y -lt $size; $y++) {
            $face.SetPixel($x, $y, $face.GetPixel($x, ($top + $height - 1)))
        }
    }

    # The lock is a separate box in the model, not part of any face, so a plain
    # crop of the front comes out as blank wood. Painted on last so it survives
    # the margin replication above.
    if ($Decal) {
        for ($y = 0; $y -lt $Decal[3]; $y++) {
            for ($x = 0; $x -lt $Decal[2]; $x++) {
                $px = $Net.GetPixel(($Decal[0] + $x), ($Decal[1] + $y))
                if ($px.A -lt 128) { continue }
                $face.SetPixel(($DecalAt[0] + $x), ($DecalAt[1] + $y), $px)
            }
        }
    }
    return $face
}

$chestNetPath = Join-Path $textures "entity\chest\normal.png"
if (Test-Path $chestNetPath) {
    $chestNet = [System.Drawing.Bitmap]::FromFile($chestNetPath)
    # Net layout, both boxes 14 wide and 14 deep: the lid is 5 tall at uv (0,0)
    # and the base 10 tall at uv (0,19).
    #
    # **A box's UP face is at `u+d+w`, not `u+d` - that one is DOWN.** Taking the
    # lid from x14 gave its *underside*, which is plain dark wood, so every chest
    # in the world wore a black square with a border on its lid.
    #
    # The lock is its own 2x4x1 box at uv (0,0) and belongs to no face at all, so
    # the front is given it as a decal, straddling the lid/base seam the way the
    # model puts it.
    $chestFaces = @{
        'chest_top.png'   = @{ Upper = @(28, 0, 14, 14); Lower = $null }
        'chest_front.png' = @{ Upper = @(14, 14, 14, 5); Lower = @(14, 33, 14, 10)
                               Decal = @(1, 1, 2, 4);    DecalAt = @(7, 3) }
        'chest_side.png'  = @{ Upper = @(0, 14, 14, 5);  Lower = @(0, 33, 14, 10) }
    }
    foreach ($name in $chestFaces.Keys) {
        $spec = $chestFaces[$name]
        $face = New-ChestFace -Net $chestNet -Upper $spec.Upper -Lower $spec.Lower `
            -Decal $spec.Decal -DecalAt $spec.DecalAt
        $staged += @{ Name = $name; Image = $face }
    }
    $chestNet.Dispose()

    # The trapped chest is the same net with a red latch, so it reuses the very
    # same crop rectangles - only the source file differs.
    $trappedNetPath = Join-Path $textures "entity\chest\trapped.png"
    if (Test-Path $trappedNetPath) {
        $trappedNet = [System.Drawing.Bitmap]::FromFile($trappedNetPath)
        $trappedFaces = @{
            'trapped_chest_top.png'   = @{ Upper = @(28, 0, 14, 14); Lower = $null }
            'trapped_chest_side.png'  = @{ Upper = @(0, 14, 14, 5);  Lower = @(0, 33, 14, 10) }
            'trapped_chest_front.png' = @{ Upper = @(14, 14, 14, 5); Lower = @(14, 33, 14, 10)
                                           Decal = @(1, 1, 2, 4);    DecalAt = @(7, 3) }
        }
        foreach ($name in $trappedFaces.Keys) {
            $spec = $trappedFaces[$name]
            $face = New-ChestFace -Net $trappedNet -Upper $spec.Upper -Lower $spec.Lower `
                -Decal $spec.Decal -DecalAt $spec.DecalAt
            $staged += @{ Name = $name; Image = $face }
        }
        $trappedNet.Dispose()
    }

    # The ender chest, from its own net with the very same crop rectangles.
    $enderNetPath = Join-Path $textures "entity\chest\ender.png"
    if (Test-Path $enderNetPath) {
        $enderNet = [System.Drawing.Bitmap]::FromFile($enderNetPath)
        $enderFaces = @{
            'ender_chest_top.png'  = @{ Upper = @(28, 0, 14, 14); Lower = $null }
            'ender_chest_side.png' = @{ Upper = @(0, 14, 14, 5);  Lower = @(0, 33, 14, 10) }
        }
        foreach ($name in $enderFaces.Keys) {
            $spec = $enderFaces[$name]
            $face = New-ChestFace -Net $enderNet -Upper $spec.Upper -Lower $spec.Lower `
                -Decal $spec.Decal -DecalAt $spec.DecalAt
            $staged += @{ Name = $name; Image = $face }
        }
        $enderNet.Dispose()
    }

    # The two halves of a double chest. Both nets are 15 wide and 14 deep - one
    # texel wider than a single chest, because the pair overlaps at the seam -
    # with the lid at v0 and the base at v19, exactly as above.
    #
    # **Mojang's file names are the opposite way round from what they read as.**
    # `normal_right.png` is the half a player sees on the LEFT. That is measured,
    # not assumed: each half paints its dark border on its OUTER vertical edge
    # only, and the seam edge is left as plain wood. Per-column luminance over
    # the front rect gives 36 at the first column of `normal_right` and 59 at the
    # last column of `normal_left`, and our own mesher runs u along -X on the
    # front face - so the first column is the viewer's left.
    #
    # Each face is therefore cropped, aligned so its border lands on the outer
    # edge, and the spare margin column replicated into the seam.
    #
    # **The lid is not mirrored, and that was measured rather than reasoned.**
    # It used to be, on the theory that a box's UP rect unwraps with u running
    # the other way from the front's - which a single chest cannot disprove,
    # because its lid is symmetric. Per-column luminance over the paired lids
    # settles it: the dark border sits at column 0 of `normal_right`'s UP rect
    # and column 14 of `normal_left`'s, exactly where each front's does. Mirror
    # them and both borders end up against the seam, which is the dark line that
    # ran down the middle of every double chest's lid.
    #
    # The outer SIDE faces are pixel-identical to a single chest's - verified,
    # zero differing texels - so they reuse `chest_side.png` rather than costing
    # two more layers. The buried faces need no art at all: two chests are solid
    # opaque cubes, so the mesher culls the face between them.
    # `Back` is not a typo for `Outer`: the back face's u runs the opposite way
    # from the front's, so the same physical outer edge lands at the other end
    # of the texture.
    $chestHalves = @(
        @{ File = 'normal_right.png'; Prefix = 'chest_left'; Outer = 'Left'; Back = 'Right' }
        @{ File = 'normal_left.png';  Prefix = 'chest_right'; Outer = 'Right'; Back = 'Left' }
    )
    foreach ($half in $chestHalves) {
        $halfPath = Join-Path $textures "entity\chest\$($half.File)"
        if (-not (Test-Path $halfPath)) {
            Write-Warning "Missing $halfPath - the double chest keeps our own art."
            continue
        }
        $halfNet = [System.Drawing.Bitmap]::FromFile($halfPath)
        # The lock is its own 1x4x1 box per half, so together the pair make the
        # one latch that straddles the seam. Its front rect is a single column.
        # `,` binds tighter than arithmetic, so the subtraction needs its own
        # brackets or this parses as `$size - (1, 3)`.
        $lockAt = if ($half.Outer -eq 'Left') { @(($size - 1), 3) } else { @(0, 3) }

        $top = New-ChestFace -Net $halfNet -Upper @(29, 0, 15, 14) -Align $half.Outer
        $front = New-ChestFace -Net $halfNet -Upper @(14, 14, 15, 5) -Lower @(14, 33, 15, 10) `
            -Decal @(1, 1, 1, 4) -DecalAt $lockAt -Align $half.Outer
        $back = New-ChestFace -Net $halfNet -Upper @(43, 14, 15, 5) -Lower @(43, 33, 15, 10) `
            -Align $half.Back

        $staged += @{ Name = "$($half.Prefix)_top.png"; Image = $top }
        $staged += @{ Name = "$($half.Prefix)_front.png"; Image = $front }
        $staged += @{ Name = "$($half.Prefix)_back.png"; Image = $back }
        $halfNet.Dispose()
    }
} else {
    Write-Warning "Missing $chestNetPath - the chest keeps our own art."
}

foreach ($entry in $sources) {
    $sourcePath = Join-Path $textures "$($entry.Path).png"
    if (-not (Test-Path $sourcePath)) {
        Write-Warning "Missing $sourcePath - $($entry.Name) keeps our own art."
        continue
    }

    $loaded = [System.Drawing.Bitmap]::FromFile($sourcePath)
    # `Corner` takes the top-left 16x16 of a larger sheet rather than a frame out
    # of a strip. The arrow's entity texture is 32x32 and both rects its model
    # samples live in that corner, so a crop keeps every pixel at its own size -
    # a resize would blur exactly one layer of an array that needs them equal.
    if ($entry.ContainsKey('Corner')) {
        $image = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
                 ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        for ($y = 0; $y -lt $size; $y++) {
            for ($x = 0; $x -lt $size; $x++) {
                $image.SetPixel($x, $y, $loaded.GetPixel($x, $y))
            }
        }
    } elseif ($entry.ContainsKey('Centre')) {
        # The sun and the moon are 32x32, and **the body itself is only the
        # middle eight columns** - everything around it is a very dim halo. So
        # this takes the centre 16x16 at its own size rather than resizing the
        # whole thing, which keeps the disc's pixels exact and keeps the halo
        # that makes it bloom.
        if ($loaded.Width -ne ($size * 2) -or $loaded.Height -ne ($size * 2)) {
            throw "$($entry.Path) is $($loaded.Width)x$($loaded.Height), expected $($size * 2) square"
        }
        $image = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
                 ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $offset = [int]($size / 2)
        for ($y = 0; $y -lt $size; $y++) {
            for ($x = 0; $x -lt $size; $x++) {
                $p = $loaded.GetPixel(($x + $offset), ($y + $offset))
                # **Alpha comes from brightness, because these are drawn
                # additively in the reference and we have no additive pass.**
                # Both files are fully opaque squares with a black surround; used
                # as they are, the moon would be a black tile with a shape in it.
                # Treating black as "nothing here" is what an additive blend
                # does, and it gives the phases their crescent for free.
                $a = [Math]::Max($p.R, [Math]::Max($p.G, $p.B))
                # **A floor and a ceiling, not a snap to zero.** The halo never
                # reaches black - inside the centre crop it bottoms out around
                # 14 - so a threshold of 10 left every single texel *slightly*
                # opaque, and the whole 16x16 tile showed as a faint square of
                # dark blue sitting behind the moon. Anything at or under the
                # floor becomes truly nothing, and what survives is rescaled so
                # the body itself reaches full opacity instead of the 204 the
                # reference's own brightness gives it.
                $a = [int](255 * [Math]::Max(0.0, [Math]::Min(1.0, ($a - 40) / 160.0)))
                $image.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, $p.R, $p.G, $p.B))
            }
        }
    } else {
        $frameIndex = if ($entry.ContainsKey('Frame')) { $entry.Frame } else { 0 }
        $image = Get-Frame -Source $loaded -Index $frameIndex -Label $entry.Path
    }
    $loaded.Dispose()

    if ($entry.ContainsKey('Tint')) {
        Set-Tint -Target $image -Tint $entry.Tint
    }

    if ($entry.ContainsKey('Overlay')) {
        # Two images in the reference, one in our texture array. The grass
        # block's green edge is a tintable layer over a plain dirt side; leather
        # armour's buckles and trim are an *untinted* layer over the dyeable
        # hide. So the tint is applied only where the entry asks for one - it
        # used to be unconditional, which threw on any overlay without it.
        $overlayPath = Join-Path $textures "$($entry.Overlay).png"
        if (Test-Path $overlayPath) {
            $overlayLoaded = [System.Drawing.Bitmap]::FromFile($overlayPath)
            $overlay = Get-Frame -Source $overlayLoaded -Index 0 -Label $entry.Overlay
            $overlayLoaded.Dispose()
            if ($entry.ContainsKey('OverlayTint')) {
                Set-Tint -Target $overlay -Tint $entry.OverlayTint
            }
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

# Blocks the reference draws with its `translucent` render type - ice, honey,
# slime - carry a uniform partial alpha over the whole 16x16. We have exactly one
# blended pass and water owns it, so these arrive in the **opaque** pass, where a
# partial alpha blends against whatever happened to be drawn first and the block
# comes out washed out and see-through in the wrong places. Flattening the alpha
# is the honest answer until the M23 renderer can sort translucent geometry.
#
# The test is the shape of the alpha, not a list: **no fully clear texel** rules
# out glass, leaves and every plant, and **most texels partly clear** rules out
# ordinary art. Water is named because it is the one thing that really does go
# through the blended pass.
#
# **The stained and tinted glass used to be flattened here and are not any
# more.** Their whole appearance is in the alpha and nowhere else: a frame at
# 200, a diagonal highlight streak at 155 and a centre panel at 110, measured
# off `tinted_glass.png` and `white_stained_glass.png`. Flattening replaced all
# three with one number and `translucentAlpha` then painted the whole face with
# the average of them, which the user reported as "it looks like a ghost".
# **The cutout alpha test was the reason it had to be flattened, and the
# blended pass is now exempt from that test** - see `kDrawFlagBlended`.
foreach ($entry in $staged) {
    # Water goes through the blended pass for real. The sun and moon are named
    # here too, and they are the one case the shape test genuinely cannot judge:
    # a sky body's halo is partly clear *everywhere*, which is exactly the
    # signature this rule reads as translucent block art. Flattening them turns
    # the moon back into an opaque tile with a picture on it.
    #
    # Glass is named for the same reason as water and is the reason this list
    # exists at all now: it is blended, so its own alpha is what draws it.
    #
    # The breaking stages are named for the opposite reason: they are almost
    # entirely alpha 1 with the crack lines at 255, which is exactly the
    # signature this rule reads as translucent block art. Flattened, every
    # stage becomes an opaque grey tile and mining paints the block solid grey.
    if ($entry.Name -like 'water*' -or $entry.Name -eq 'sun.png' -or $entry.Name -like 'moon_*' -or
        $entry.Name -eq 'tinted_glass.png' -or $entry.Name -like '*_stained_glass.png' -or
        $entry.Name -like 'destroy_stage_*') {
        continue
    }
    $image = $entry.Image
    $clear = 0
    $partial = 0
    for ($y = 0; $y -lt $image.Height; $y++) {
        for ($x = 0; $x -lt $image.Width; $x++) {
            $a = $image.GetPixel($x, $y).A
            if ($a -eq 0) { $clear++ }
            elseif ($a -lt 250) { $partial++ }
        }
    }
    if ($clear -gt 0 -or $partial -lt ($image.Width * $image.Height / 2)) {
        continue
    }
    for ($y = 0; $y -lt $image.Height; $y++) {
        for ($x = 0; $x -lt $image.Width; $x++) {
            $p = $image.GetPixel($x, $y)
            $image.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $p.R, $p.G, $p.B))
        }
    }
    Write-Host "flattened alpha on $($entry.Name)"
}

# Lit TNT. The reference has no texture for it - it flashes the whole entity
# white in a shader - so every face is derived here instead. **Pure white, not a
# wash toward it**: a partial mix leaves the lettering legible and the block
# reads as dull grey rather than as a flash. `Set-Tint` multiplies and therefore
# can only darken, which is why this is its own pass.
foreach ($face in @('top', 'bottom', 'side')) {
    $src = ($staged | Where-Object { $_.Name -eq "tnt_$face.png" } | Select-Object -First 1)
    if ($null -eq $src) {
        throw "tnt_$face.png must be staged before the primed variant can be derived from it"
    }
    $primed = New-Object System.Drawing.Bitmap $size, $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $p = $src.Image.GetPixel($x, $y)
            $primed.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($p.A, 255, 255, 255))
        }
    }
    $staged += @{ Name = "tnt_primed_$face.png"; Image = $primed }
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

    # The manifest is what turns run.ps1's guard from a TRIGGER into a CHECK,
    # and the order of these three steps is the whole point.
    #
    # It is deleted FIRST and written LAST, so it is a receipt for a run that
    # finished rather than one that started. An interrupted stage - a full disk,
    # an antivirus lock, a cancelled build - leaves the folder half full and NO
    # manifest, and run.ps1 stages again. Written first, it would vouch for a
    # folder it never saw completed.
    #
    # ADDING A ROW TO $sources NEEDS NOTHING HERE. That is deliberate, and it is
    # why this is a manifest and not a number: the count is only knowable at
    # runtime, because most of the 1,340 names are generated by the family loops
    # above rather than written out - 851 appear literally in this file. Any
    # count restated by hand would be a fiction that happened to be right on the
    # day it was written.
    $manifestPath = Join-Path $output "manifest.txt"
    if (Test-Path $manifestPath) { Remove-Item $manifestPath -Force }

    foreach ($item in $staged) {
        $item.Image.Save((Join-Path $output $item.Name), [System.Drawing.Imaging.ImageFormat]::Png)
    }

    $manifestNames = @($staged | ForEach-Object { $_.Name } | Sort-Object -Unique)
    $manifestLines = @(
        "# Written by tools/make-reference-blocks.ps1 - do not hand-edit.",
        "# One texture name per line. run.ps1 compares this set against the .png",
        "# files beside it; a name here with no file is a block face that would",
        "# be drawn with our own placeholder art, which is the thing to catch.",
        "# $($manifestNames.Count) distinct names from $($staged.Count) staged images."
    ) + $manifestNames
    [System.IO.File]::WriteAllText($manifestPath, (($manifestLines -join "`r`n") + "`r`n"),
                                   [System.Text.UTF8Encoding]::new($false))

    Write-Host "wrote $output ($($staged.Count) textures at ${size}x${size}, none rescaled)"
}

foreach ($item in $staged) { $item.Image.Dispose() }
