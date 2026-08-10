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
    @{ Name = 'bamboo.png';                Path = 'block\bamboo_stalk'; StalkColumns = @(13, 3) }
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
    # **A box's UP face is at `u+d+w`, not `u+d` — that one is DOWN.** Taking the
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

    # A stalk is drawn from a **slice** of its texture in the reference - bamboo's
    # model samples uv 13-16 across a 2/16 wide post - and we draw cross blades
    # that span the whole cell, so the full sheet comes out as a solid square of
    # bamboo colour. Cropping the slice onto a transparent field is what makes it
    # read as a stalk.
    if ($entry.ContainsKey('StalkColumns')) {
        $from = $entry.StalkColumns[0]
        $wide = $entry.StalkColumns[1]
        $slice = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
            ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $at = [int](($size - $wide) / 2)
        for ($y = 0; $y -lt $size; $y++) {
            for ($x = 0; $x -lt $wide; $x++) {
                $slice.SetPixel(($at + $x), $y, $image.GetPixel(($from + $x), $y))
            }
        }
        $image.Dispose()
        $image = $slice
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
foreach ($entry in $staged) {
    # Water goes through the blended pass for real. The sun and moon are named
    # here too, and they are the one case the shape test genuinely cannot judge:
    # a sky body's halo is partly clear *everywhere*, which is exactly the
    # signature this rule reads as translucent block art. Flattening them turns
    # the moon back into an opaque tile with a picture on it.
    if ($entry.Name -like 'water*' -or $entry.Name -eq 'sun.png' -or $entry.Name -like 'moon_*') {
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
    foreach ($item in $staged) {
        $item.Image.Save((Join-Path $output $item.Name), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    Write-Host "wrote $output ($($staged.Count) textures at ${size}x${size}, none rescaled)"
}

foreach ($item in $staged) { $item.Image.Dispose() }
