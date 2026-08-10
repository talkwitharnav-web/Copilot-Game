# Stages the reference's own sound effects and music beside each built game.exe.
#
# Same arrangement as every other reference drop in this project - the creature
# atlas, the spawn eggs, the HUD sheet, the block textures and the font. The art
# may be **measured from and used as a placeholder**, must **never live under
# assets/**, and must **never ship in a release**. This script refuses to write
# under assets/ so that boundary is mechanical rather than remembered.
#
# The user asked for this directly on 2026-08-08: "just use whatever sound has
# been placed inside reference, use mojang's noises and music and all. i'll
# download some kind of music editor later and make custom tracks later."
#
# Nothing here is converted, resampled or re-encoded. The files are copied
# verbatim, because a silent re-encode is exactly the kind of change that makes
# a difference nobody can trace.

param(
    [string[]]$OutputDir = @(
        "build\debug\bin\sounds-reference",
        "build\release\bin\sounds-reference"
    )
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$soundRoot = Join-Path $root "reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\sounds"

if (-not (Test-Path $soundRoot)) {
    throw "Missing the reference sound dump at $soundRoot"
}

$assetsRoot = [System.IO.Path]::GetFullPath((Join-Path $root "assets"))
$resolvedOutputs = foreach ($candidate in $OutputDir) {
    $full = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $candidate))
    if ($full.StartsWith($assetsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Reference sounds must stay outside assets/: $full"
    }
    $full
}

# --- World events. Our event name -> the reference files that serve it.
#
# **Grouped by material rather than by block**, which is how the reference does
# it too: a sound belongs to what a thing is made of, so a new stone block
# inherits the stone sounds without a line being added here.
$events = [ordered]@{
    'dig_stone'   = @('dig\stone1.ogg', 'dig\stone2.ogg', 'dig\stone3.ogg', 'dig\stone4.ogg')
    'dig_wood'    = @('dig\wood1.ogg', 'dig\wood2.ogg', 'dig\wood3.ogg', 'dig\wood4.ogg')
    'dig_grass'   = @('dig\grass1.ogg', 'dig\grass2.ogg', 'dig\grass3.ogg', 'dig\grass4.ogg')
    'dig_gravel'  = @('dig\gravel1.ogg', 'dig\gravel2.ogg', 'dig\gravel3.ogg', 'dig\gravel4.ogg')
    'dig_sand'    = @('dig\sand1.ogg', 'dig\sand2.ogg', 'dig\sand3.ogg', 'dig\sand4.ogg')
    'dig_cloth'   = @('dig\cloth1.ogg', 'dig\cloth2.ogg', 'dig\cloth3.ogg', 'dig\cloth4.ogg')
    'dig_snow'    = @('dig\snow1.ogg', 'dig\snow2.ogg', 'dig\snow3.ogg', 'dig\snow4.ogg')
    'dig_glass'   = @('random\glass1.ogg', 'random\glass2.ogg', 'random\glass3.ogg')
    'dig_coral'   = @('dig\coral1.ogg', 'dig\coral2.ogg', 'dig\coral3.ogg', 'dig\coral4.ogg')
    'dig_wet'     = @('dig\wet_grass1.ogg', 'dig\wet_grass2.ogg', 'dig\wet_grass3.ogg', 'dig\wet_grass4.ogg')

    'step_stone'  = @('step\stone1.ogg', 'step\stone2.ogg', 'step\stone3.ogg', 'step\stone4.ogg', 'step\stone5.ogg', 'step\stone6.ogg')
    'step_wood'   = @('step\wood1.ogg', 'step\wood2.ogg', 'step\wood3.ogg', 'step\wood4.ogg', 'step\wood5.ogg', 'step\wood6.ogg')
    'step_grass'  = @('step\grass1.ogg', 'step\grass2.ogg', 'step\grass3.ogg', 'step\grass4.ogg', 'step\grass5.ogg', 'step\grass6.ogg')
    'step_gravel' = @('step\gravel1.ogg', 'step\gravel2.ogg', 'step\gravel3.ogg', 'step\gravel4.ogg')
    'step_sand'   = @('step\sand1.ogg', 'step\sand2.ogg', 'step\sand3.ogg', 'step\sand4.ogg', 'step\sand5.ogg')
    'step_cloth'  = @('step\cloth1.ogg', 'step\cloth2.ogg', 'step\cloth3.ogg', 'step\cloth4.ogg')
    'step_snow'   = @('step\snow1.ogg', 'step\snow2.ogg', 'step\snow3.ogg', 'step\snow4.ogg')
    'step_ladder' = @('step\ladder1.ogg', 'step\ladder2.ogg', 'step\ladder3.ogg', 'step\ladder4.ogg', 'step\ladder5.ogg')
    'step_coral'  = @('step\coral1.ogg', 'step\coral2.ogg', 'step\coral3.ogg', 'step\coral4.ogg', 'step\coral5.ogg', 'step\coral6.ogg')
    'step_wet'    = @('step\wet_grass1.ogg', 'step\wet_grass2.ogg', 'step\wet_grass3.ogg', 'step\wet_grass4.ogg', 'step\wet_grass5.ogg', 'step\wet_grass6.ogg')

    'hurt'        = @('damage\hit1.ogg', 'damage\hit2.ogg', 'damage\hit3.ogg')
    'fall_big'    = @('damage\fallbig.ogg')
    'fall_small'  = @('damage\fallsmall.ogg')
    'breath'      = @('random\breath.ogg')

    'bow'         = @('random\bow.ogg')
    'bow_hit'     = @('random\bowhit1.ogg', 'random\bowhit2.ogg', 'random\bowhit3.ogg', 'random\bowhit4.ogg')
    'hit_land'    = @('random\successful_hit.ogg')
    'explode'     = @('random\explode1.ogg', 'random\explode2.ogg', 'random\explode3.ogg', 'random\explode4.ogg')
    'fuse'        = @('random\fuse.ogg')

    'eat'         = @('random\eat1.ogg', 'random\eat2.ogg', 'random\eat3.ogg')
    'burp'        = @('random\burp.ogg')
    'pop'         = @('random\pop.ogg')
    'orb'         = @('random\orb.ogg')
    'click'       = @('random\click.ogg')
    'wood_click'  = @('random\wood_click.ogg')
    'item_break'  = @('random\break.ogg')
    'fizz'        = @('random\fizz.ogg')
    'splash'      = @('liquid\splash.ogg', 'liquid\splash2.ogg')
    'splash_big'  = @('liquid\heavy_splash.ogg')
    'swim'        = @('liquid\swim1.ogg', 'liquid\swim2.ogg', 'liquid\swim3.ogg', 'liquid\swim4.ogg', 'liquid\swim5.ogg', 'liquid\swim6.ogg')
    'drink'       = @('random\drink.ogg')
    'levelup'     = @('random\levelup.ogg')
    'chest_open'  = @('random\chestopen.ogg')
    'chest_close' = @('random\chestclosed.ogg')
    'door_open'   = @('random\door_open.ogg')
    'door_close'  = @('random\door_close.ogg')

    'bucket_fill'       = @('item\bucket\fill1.ogg', 'item\bucket\fill2.ogg', 'item\bucket\fill3.ogg')
    'bucket_empty'      = @('item\bucket\empty1.ogg', 'item\bucket\empty2.ogg', 'item\bucket\empty3.ogg')
    'bucket_fill_lava'  = @('item\bucket\fill_lava1.ogg', 'item\bucket\fill_lava2.ogg', 'item\bucket\fill_lava3.ogg')
    'bucket_empty_lava' = @('item\bucket\empty_lava1.ogg', 'item\bucket\empty_lava2.ogg', 'item\bucket\empty_lava3.ogg')

    'fire'        = @('fire\fire.ogg')
    'ignite'      = @('fire\ignite.ogg')
    'lava'        = @('liquid\lava.ogg')
    'lava_pop'    = @('liquid\lavapop.ogg')
    'water'       = @('liquid\water.ogg')

    # The cave ambience is the reference's own unnerving background, and there
    # are twenty-three of them precisely so it never repeats within a session.
    'cave'        = @('ambient\cave\cave1.ogg', 'ambient\cave\cave2.ogg', 'ambient\cave\cave3.ogg',
                      'ambient\cave\cave4.ogg', 'ambient\cave\cave5.ogg', 'ambient\cave\cave6.ogg',
                      'ambient\cave\cave7.ogg', 'ambient\cave\cave8.ogg')

    # Music. Eight tracks rather than four, now the bus is proved.
    'music'       = @('music\game\an_ordinary_day.ogg', 'music\game\ancestry.ogg',
                      'music\game\a_familiar_room.ogg', 'music\game\below_and_above.ogg',
                      'music\game\broken_clocks.ogg', 'music\game\comforting_memories.ogg',
                      'music\game\crescent_dunes.ogg', 'music\game\danny.ogg')
    # Weather. The rain loop is retriggered rather than looped, because the
    # mixer has no loop point and eight recordings are enough that the seam
    # never lands in the same place twice.
    'rain'        = @('ambient\weather\rain1.ogg', 'ambient\weather\rain2.ogg',
                      'ambient\weather\rain3.ogg', 'ambient\weather\rain4.ogg',
                      'ambient\weather\rain5.ogg', 'ambient\weather\rain6.ogg',
                      'ambient\weather\rain7.ogg', 'ambient\weather\rain8.ogg')
    'thunder'     = @('ambient\weather\thunder1.ogg', 'ambient\weather\thunder2.ogg',
                      'ambient\weather\thunder3.ogg')
}

# --- Creature voices, three states per family: idle, hurt, death.
#
# **A family rather than a species**, which is the same reasoning the block
# materials follow: fifty-seven species share far fewer voices, and a new one
# inherits an existing family by naming it rather than by needing recordings of
# its own. `voiceFamilyFor` in `game/src/core/Sounds.cpp` is the other half of
# this table and the two must name the same families.
$voices = [ordered]@{
    'sheep'     = @{ idle = 'mob\sheep\say';            hurt = 'mob\sheep\say';            death = 'mob\sheep\say' }
    'cow'       = @{ idle = 'mob\cow\say';              hurt = 'mob\cow\hurt';             death = 'mob\cow\hurt' }
    'pig'       = @{ idle = 'mob\pig\say';              hurt = 'mob\pig\say';              death = 'mob\pig\death' }
    'chicken'   = @{ idle = 'mob\chicken\say';          hurt = 'mob\chicken\hurt';         death = 'mob\chicken\hurt' }
    'horse'     = @{ idle = 'mob\horse\idle';           hurt = 'mob\horse\hit';            death = 'mob\horse\death' }
    'llama'     = @{ idle = 'mob\llama\idle';           hurt = 'mob\llama\hurt';           death = 'mob\llama\death' }
    'cat'       = @{ idle = 'mob\cat\meow';             hurt = 'mob\cat\hitt';             death = 'mob\cat\hitt' }
    'wolf'      = @{ idle = 'mob\wolf\classic\bark';    hurt = 'mob\wolf\classic\hurt';    death = 'mob\wolf\classic\death' }
    'fox'       = @{ idle = 'mob\fox\idle';             hurt = 'mob\fox\hurt';             death = 'mob\fox\death' }
    'panda'     = @{ idle = 'mob\panda\idle';           hurt = 'mob\panda\hurt';           death = 'mob\panda\death' }
    'bear'      = @{ idle = 'mob\polarbear\idle';       hurt = 'mob\polarbear\hurt';       death = 'mob\polarbear\death' }
    'rabbit'    = @{ idle = 'mob\rabbit\idle';          hurt = 'mob\rabbit\hurt';          death = 'mob\rabbit\bunnymurder' }
    'goat'      = @{ idle = 'mob\goat\idle';            hurt = 'mob\goat\hurt';            death = 'mob\goat\death' }
    'bee'       = @{ idle = 'mob\bee\loop';             hurt = 'mob\bee\hurt';             death = 'mob\bee\death' }
    'turtle'    = @{ idle = 'mob\turtle\idle';          hurt = 'mob\turtle\hurt';          death = 'mob\turtle\death' }
    'dolphin'   = @{ idle = 'mob\dolphin\idle';         hurt = 'mob\dolphin\hurt';         death = 'mob\dolphin\death' }
    'squid'     = @{ idle = 'mob\squid\ambient';        hurt = 'mob\squid\hurt';           death = 'mob\squid\death' }
    'villager'  = @{ idle = 'mob\villager\idle';        hurt = 'mob\villager\hit';         death = 'mob\villager\death' }
    'trader'    = @{ idle = 'mob\wandering_trader\idle';hurt = 'mob\wandering_trader\hurt';death = 'mob\wandering_trader\death' }
    'zombie'    = @{ idle = 'mob\zombie\say';           hurt = 'mob\zombie\hurt';          death = 'mob\zombie\death' }
    'husk'      = @{ idle = 'mob\husk\idle';            hurt = 'mob\husk\hurt';            death = 'mob\husk\death' }
    'drowned'   = @{ idle = 'mob\drowned\idle';         hurt = 'mob\drowned\hurt';         death = 'mob\drowned\death' }
    'zvillager' = @{ idle = 'mob\zombie_villager\say';  hurt = 'mob\zombie_villager\hurt'; death = 'mob\zombie_villager\death' }
    'skeleton'  = @{ idle = 'mob\skeleton\say';         hurt = 'mob\skeleton\hurt';        death = 'mob\skeleton\death' }
    'stray'     = @{ idle = 'mob\stray\idle';           hurt = 'mob\stray\hurt';           death = 'mob\stray\death' }
    'bogged'    = @{ idle = 'mob\bogged\ambient';       hurt = 'mob\bogged\hurt';          death = 'mob\bogged\death' }
    'blackbone' = @{ idle = 'mob\wither_skeleton\idle'; hurt = 'mob\wither_skeleton\hurt'; death = 'mob\wither_skeleton\death' }
    'spider'    = @{ idle = 'mob\spider\say';           hurt = 'mob\spider\say';           death = 'mob\spider\death' }
    'creeper'   = @{ idle = 'mob\creeper\say';          hurt = 'mob\creeper\say';          death = 'mob\creeper\death' }
    'slime'     = @{ idle = 'mob\slime\small';          hurt = 'mob\slime\small';          death = 'mob\slime\small' }
    'magma'     = @{ idle = 'mob\magmacube\small';      hurt = 'mob\magmacube\big';        death = 'mob\magmacube\big' }
    'silverfish'= @{ idle = 'mob\silverfish\say';       hurt = 'mob\silverfish\hit';       death = 'mob\silverfish\kill' }
    'princepin' = @{ idle = 'mob\piglin\idle';          hurt = 'mob\piglin\hurt';          death = 'mob\piglin\death' }
    'fish'      = @{ idle = 'mob\dolphin\swim';         hurt = 'mob\dolphin\hurt';         death = 'mob\dolphin\death' }
}

foreach ($family in $voices.Keys) {
    foreach ($state in @('idle', 'hurt', 'death')) {
        $stem = $voices[$family][$state]
        # Numbered variants where they exist, and a bare name where the
        # reference ships only one. Trying both is what keeps this one table
        # rather than two.
        $files = @()
        for ($n = 1; $n -le 6; $n++) {
            $files += "$stem$n.ogg"
        }
        $files += "$stem.ogg"
        $events["voice_${family}_${state}"] = $files
    }
}

$staged = @()
$emptyEvents = @()

foreach ($name in $events.Keys) {
    $index = 1
    foreach ($relative in $events[$name]) {
        $source = Join-Path $soundRoot $relative
        if (-not (Test-Path $source)) {
            # A gap in a numbered run is expected - the reference ships four of
            # some and three of others - so only a wholly empty event is worth
            # reporting.
            continue
        }
        $staged += @{ Name = "$name$index.ogg"; Source = $source }
        $index++
    }
    if ($index -eq 1) {
        $emptyEvents += $name
    }
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
    foreach ($file in $staged) {
        Copy-Item $file.Source (Join-Path $output $file.Name) -Force
    }
    Write-Host "wrote $output ($($staged.Count) files, $($events.Keys.Count) events)"
}

if ($emptyEvents.Count -gt 0) {
    Write-Warning "$($emptyEvents.Count) events found no recordings at all:"
    $emptyEvents | ForEach-Object { Write-Warning "  $_" }
}
