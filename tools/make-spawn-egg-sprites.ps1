# Stages the reference spawn egg sprites beside each built game.exe.
#
# Same arrangement as tools/make-reference-creature-atlas.ps1 and for the same
# reason: this is Mojang's art, used as a sanctioned placeholder. It refuses to
# write under assets/ so it cannot ship by mistake, and the folder it writes to
# is gitignored.
#
# The ORDER of $eggs is load-bearing - it must match the SpawnEgg run in
# TextureLayer (Block.hpp), the SpawnEgg run in ItemId (Item.hpp), and
# CreatureKind (Creature.hpp), all three of which are the same order. The game
# checks the count and falls back to a blank sprite for anything missing, so a
# gap degrades to an invisible icon rather than shifting every later layer.

param(
    [string[]]$OutputPath = @(
        "build\debug\bin\spawn-eggs",
        "build\release\bin\spawn-eggs"
    )
)

$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot "..\reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures\item"
$assetsRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\assets"))

# One per CreatureKind, in CreatureKind order. The three slimes share the one
# reference egg because the reference has one slime; they stay three entries
# because they are three species rows here.
$eggs = @(
    "sheep", "cow", "pig", "creeper", "chicken", "cat", "camel", "horse",
    "mule", "llama", "donkey", "goat", "rabbit", "wolf", "frog", "fox",
    "ocelot", "polar_bear", "panda", "slime", "slime", "slime", "spider",
    "cave_spider", "zombie", "skeleton", "villager", "husk", "silverfish",
    "wither_skeleton", "stray", "bogged", "zombie_villager", "witch",
    "wandering_trader", "piglin", "drowned", "cod", "salmon", "pufferfish",
    "squid", "glow_squid", "turtle", "dolphin", "axolotl", "tropical_fish",
    # Tier 1. The three magma cubes share one reference egg for the same reason
    # the slimes do.
    "mooshroom", "skeleton_horse", "zombie_horse", "trader_llama",
    "piglin_brute", "zombified_piglin", "endermite",
    "magma_cube", "magma_cube", "magma_cube"
)

$resolvedOutputs = foreach ($candidate in $OutputPath) {
    $full = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $candidate))
    if ($full.StartsWith($assetsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Spawn egg sprites must stay outside assets/: $full"
    }
    $full
}

foreach ($output in $resolvedOutputs) {
    $parent = Split-Path -Parent $output
    if (-not (Test-Path $parent)) { continue }
    if (-not (Test-Path $output)) { New-Item -ItemType Directory -Path $output | Out-Null }

    $index = 0
    foreach ($egg in $eggs) {
        $from = Join-Path $source "${egg}_spawn_egg.png"
        if (-not (Test-Path $from)) { throw "Missing reference sprite: $from" }
        # Numbered rather than named, because the load order is the layer order
        # and a name would invite someone to reorder them alphabetically.
        $to = Join-Path $output ("egg{0:d2}.png" -f $index)
        Copy-Item -Path $from -Destination $to -Force
        $index++
    }
    Write-Host "wrote $index spawn egg sprites to $output"
}
