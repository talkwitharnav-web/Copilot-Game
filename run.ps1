# Launches the game without having to remember where the build puts it.
#
#   .\run.ps1            release build
#   .\run.ps1 debug      debug build, with Vulkan validation layers on
#
# PowerShell does not search the current directory for commands, so the leading
# .\ is required.

param(
    [ValidateSet('release', 'debug')]
    [string]$Config = 'release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root "build\$Config\bin\game.exe"

if (-not (Test-Path $exe)) {
    Write-Host "No $Config build yet at $exe" -ForegroundColor Yellow
    Write-Host "Build it with:" -ForegroundColor Yellow
    Write-Host "  . .\tools\dev-env.ps1; cmake --build --preset $Config"
    exit 1
}

# Most species are on placeholder art pending replacement, and it lives beside
# the exe rather than under assets/ because reference pixels may never enter a
# build. Deleting build/ throws it away, so put it back rather than leaving the
# roster wearing skins that were already rejected.
#
# It is also rebuilt whenever the sheet it was composited from is newer, because
# the atlas carries a COPY of our own rows 0-767. Regenerating the sheet without
# regenerating the atlas leaves the game loading stale art for every species we
# have actually drawn - which is how a whole session shipped a mangled sheep.
#
# ONE FILE, so Test-Path here is a complete check: a single file cannot be half
# staged. Do not copy this shape to a FOLDER - see the block section below for
# what that costs.
$placeholder = Join-Path (Split-Path -Parent $exe) "creatures-reference.png"
$sheet = Join-Path $root "assets\textures\creatures.png"
$referenceRoot = Join-Path $root "reference\minecraft-assets-26.2"
$stale = (Test-Path $placeholder) -and (Test-Path $sheet) -and
         ((Get-Item $sheet).LastWriteTimeUtc -gt (Get-Item $placeholder).LastWriteTimeUtc)
if ((-not (Test-Path $placeholder) -or $stale) -and (Test-Path $referenceRoot)) {
    & (Join-Path $root "tools\make-reference-creature-atlas.ps1") | Out-Null
    Write-Host $(if ($stale) { "Rebuilt placeholder creature skins (sheet was newer)." }
                 else { "Restored placeholder creature skins." }) -ForegroundColor DarkGray
}

# Same arrangement for the spawn egg sprites, and the same reason: they are
# reference art staged beside the exe, so deleting build/ throws them away and
# the game falls back to blank icons until they are put back.
#
# This is a FOLDER, not one file, so a single Test-Path is a sample rather than
# a check - see the block section below for what that costs. The egg set is the
# lucky case: it verifies itself. make-spawn-egg-sprites.ps1 writes egg00.png
# upward by index, so counting the files and taking the highest index is enough
# - 58 distinct indices whose maximum is 57 can only be exactly 0..57, so no
# gap scan is needed and no name has to be restated here.
#
# The top of the run is the one number this cannot derive, and it is ASSERTED,
# not derived: 58 sprites, counted 2026-08-19, coupled to the SpawnEgg runs in
# Item.hpp and Block.hpp that make-spawn-egg-sprites.ps1's own header calls
# load-bearing. *Falsified by a 59th species*, which makes this stage once and
# then warn until the number is restated. Deriving it properly needs a manifest
# out of that script, which is not this file's to write - filed rather than
# faked, because a count invented here would match the stager only by luck.
$eggDir = Join-Path (Split-Path -Parent $exe) "spawn-eggs"
$eggTop = 57
function Get-StagedEggIndices {
    param([string]$Dir)
    # Comma for the same reason as Get-MissingStagedTextures below: an empty
    # folder must arrive as an empty array, not as $null.
    return ,@(Get-ChildItem $Dir -Filter 'egg*.png' -File -ErrorAction SilentlyContinue |
              ForEach-Object { if ($_.Name -match '^egg(\d+)\.png$') { [int]$Matches[1] } })
}
$eggNums = Get-StagedEggIndices -Dir $eggDir
$eggsComplete = ($eggNums.Count -eq ($eggTop + 1)) -and
                (($eggNums | Measure-Object -Maximum).Maximum -eq $eggTop)
if (-not $eggsComplete -and (Test-Path $referenceRoot)) {
    & (Join-Path $root "tools\make-spawn-egg-sprites.ps1") | Out-Null
    Write-Host "Restored spawn egg sprites." -ForegroundColor DarkGray
    $eggNums = Get-StagedEggIndices -Dir $eggDir
}

# And again for the HUD proof atlas, which carries a COPY of our own hud.png
# with the reference's real tabs laid over ours. Regenerating the sheet without
# regenerating this leaves the game loading a stale copy of every HUD sprite,
# and a wrong sheet size does not fail - it silently skews all of them.
#
# ONE FILE, so Test-Path here is a complete check. Do not copy this shape to a
# folder.
$hudPlaceholder = Join-Path (Split-Path -Parent $exe) "hud-reference.png"
$hudSheet = Join-Path $root "assets\textures\hud.png"
$uiIcons = Join-Path $root "reference\ui-icons"
$hudStale = (Test-Path $hudPlaceholder) -and (Test-Path $hudSheet) -and
            ((Get-Item $hudSheet).LastWriteTimeUtc -gt (Get-Item $hudPlaceholder).LastWriteTimeUtc)
if ((-not (Test-Path $hudPlaceholder) -or $hudStale) -and (Test-Path $uiIcons)) {
    & (Join-Path $root "tools\make-reference-hud.ps1") | Out-Null
    Write-Host $(if ($hudStale) { "Rebuilt placeholder HUD tabs (sheet was newer)." }
                 else { "Restored placeholder HUD tabs." }) -ForegroundColor DarkGray
}

# And again for the block and item textures. Nothing is composited from our own
# art here, so there is no staleness to chase - but completeness is a real
# question, because this is a FOLDER of 1,340 files rather than one file.
#
# This guard used to be three Test-Path sentinels: stone, the last water frame,
# and the last redstone layer. Each was added after an incident - the comment
# said so, which is the tell. That is a TRIGGER doing a CHECKER's job, and it
# fails in the one direction that matters:
#
#   1. Once all three sentinels exist the stager never runs again, so a folder
#      that staged 900 of 1,340 is never repaired and never mentioned.
#   2. The game does not report it either. A missing reference texture falls
#      back to OUR art per texture, and only counts as missing when BOTH are
#      absent - see the `blockTexture` lambda in Main.cpp, search it for
#      `missingTextures`. So "0 block textures are missing" is true and useless:
#      every texture that failed to stage but has one of our 123 placeholders is
#      on screen, hand-authored, and counted by nothing. The placeholder masks
#      the gap it was meant to reveal.
#   3. So the failure is indistinguishable from success in every log, and the
#      first detector is the user opening the game and seeing our own art on a
#      block face - the exact outcome we are told to prevent.
#
# The fix is to compare the folder against the manifest the stager now writes,
# name by name. The two cannot disagree quietly: the manifest is written by the
# same loop that saves the images, from what it actually saved, and it is
# deleted before that loop and rewritten after it, so an interrupted run leaves
# no manifest at all and this stages again.
#
# BOUND THIS HONESTLY, because it answers one of the two questions and looks
# like it answers both. The manifest compares SUPPLY against SUPPLY: it proves
# the stager finished what it started, and that is exactly the interrupted-run
# hole above. It does NOT prove the game gets what it asks for. Roughly a dozen
# demand sites build their texture name in C++ rather than reading a row in the
# stager, so the two facts only ever meet in one place - the `blockTexture`
# lambda in Main.cpp. That is also why the missing-name assert does not belong
# in make-reference-blocks.ps1: a count restated in the stager and checked
# against the stager is one side of a derivation compared with itself, which is
# the shape that let eleven static_asserts in this tree pass while pointing at
# the wrong texture. The demand-side counter LANDED 2026-08-19 as
# `ownArtTextures` beside `missingTextures` in Main.cpp, built by copying
# `pushEgg`, which had been doing it correctly for spawn eggs all along. The two
# checks are complements, not duplicates: that one names every face already
# being drawn with our art, this one catches a half-staged folder before the
# game is ever launched. Falsified by: `ownArtTextures` disappearing from
# Main.cpp, which would put the demand question back to nobody.
#
# Current state, measured 2026-08-19: both build/debug/bin/blocks-reference and
# build/release/bin/blocks-reference hold 1,340 files, against 123 of our own
# placeholders under assets/. Nothing hand-authored is reaching a block face.
# This guard is hardening, not a repair - do not read it as the art being wrong.
# Falsified by: either folder's count diverging from its own manifest.txt.
$blockPlaceholder = Join-Path (Split-Path -Parent $exe) "blocks-reference"

# $null means "cannot tell" - there is no manifest, so the folder is
# unverifiable and has to be staged. An empty array is the only clean answer;
# anything else is a block face that will be drawn with our own art.
function Get-MissingStagedTextures {
    param([string]$Dir)
    $manifest = Join-Path $Dir "manifest.txt"
    if (-not (Test-Path $manifest)) { return $null }
    $want = @(Get-Content $manifest | Where-Object { $_ -and ($_ -notmatch '^\s*#') })
    $have = @{}
    foreach ($file in @(Get-ChildItem $Dir -Filter *.png -File -ErrorAction SilentlyContinue)) {
        $have[$file.Name] = $true
    }
    # The leading comma is load-bearing and is the difference between this
    # working and inverting itself. PowerShell unrolls a returned array, so a
    # bare `return @()` - the CLEAN case, nothing missing - arrives at the
    # caller as $null, which this file reads as "no manifest, cannot tell". The
    # guard would then re-stage 1,340 textures on every launch and report the
    # folder unverifiable forever. `,@(...)` wraps it so the empty array
    # survives, keeping the three states distinct: $null cannot tell, empty is
    # clean, non-empty names the faces that would be drawn with our own art.
    return ,@($want | Where-Object { -not $have.ContainsKey($_) })
}

$missingBlocks = Get-MissingStagedTextures -Dir $blockPlaceholder
if (($null -eq $missingBlocks -or $missingBlocks.Count -gt 0) -and (Test-Path $referenceRoot)) {
    & (Join-Path $root "tools\make-reference-blocks.ps1") | Out-Null
    Write-Host "Restored placeholder block and item textures." -ForegroundColor DarkGray
    $missingBlocks = Get-MissingStagedTextures -Dir $blockPlaceholder
}

# And again for the text font. `ascii.png` is a straight copy rather than a
# composite, so there is no staleness to chase - it is either staged or the game
# falls back to our own assets/textures/font.png, which is the same layout.
#
# ONE FILE, so Test-Path here is a complete check. Do not copy this shape to a
# folder.
$fontPlaceholder = Join-Path (Split-Path -Parent $exe) "font-reference.png"
if (-not (Test-Path $fontPlaceholder) -and (Test-Path $referenceRoot)) {
    & (Join-Path $root "tools\make-reference-font.ps1") | Out-Null
    Write-Host "Restored placeholder text font." -ForegroundColor DarkGray
}

# And the sound bank. Copied verbatim rather than composited, so like the font
# there is nothing to go stale - the folder is either there or the game runs
# silently, which it is written to tolerate.
#
# A FOLDER again, and the weakest of the three: 519 files across 165 families
# whose sizes vary, so unlike the eggs the set cannot check its own shape. This
# is a FLOOR, asserted from a count taken 2026-08-19, and it is honest about
# being the bottom rung. **Its known weakness, stated rather than hidden:** a
# floor only catches files going missing from today's set. If the bank grows to
# 530 and eleven fail to stage, 519 still passes and nothing warns. Silence
# here is weaker evidence than silence for blocks or eggs. The real fix is the
# same manifest the block stager now writes; that script is not this file's to
# edit, so it is filed rather than faked.
$soundPlaceholder = Join-Path (Split-Path -Parent $exe) "sounds-reference"
$soundFloor = 519
function Get-StagedSoundCount {
    param([string]$Dir)
    return @(Get-ChildItem $Dir -Filter *.ogg -File -ErrorAction SilentlyContinue).Count
}
$soundCount = Get-StagedSoundCount -Dir $soundPlaceholder
if ($soundCount -lt $soundFloor -and (Test-Path $referenceRoot)) {
    & (Join-Path $root "tools\make-reference-sounds.ps1") | Out-Null
    Write-Host "Restored placeholder sounds." -ForegroundColor DarkGray
    $soundCount = Get-StagedSoundCount -Dir $soundPlaceholder
}

# What the six guards above actually promise, because they are spelled alike and
# do not promise the same thing. Three stage ONE FILE - the creature atlas, the
# HUD atlas and the font - and for those Test-Path is a complete check, because
# a single file cannot be half staged. The other three stage a FOLDER, where
# Test-Path on one name is a sample of one: eggs verify themselves from their
# own numbering, blocks are checked against a manifest, and sounds have only a
# floor. That difference is invisible from the code, which is why it is printed.
#
# The user's ruling is the reason this is loud: hand-authored art on a BLOCK
# face is unacceptable, HUD and inventory art is fine, and something that exists
# without reaching the screen is fine. A block texture that failed to stage is
# exactly the unacceptable case, so it gets a red line and a count, and the log
# says which SOURCE served the pixels rather than merely that a file was found.
$artProblems = @()
$canRepair = Test-Path $referenceRoot
# The staged folder is asked FIRST, because it is the thing that decides what
# reaches the screen. The reference dump only decides whether a gap can be
# repaired from here. Asking about the dump first would report "every block face
# is our own art" for a fully staged folder whose dump was deleted afterwards -
# a false alarm carrying a false claim, which is how an instrument teaches the
# people reading it to ignore it.
if ($null -eq $missingBlocks) {
    $artProblems += "blocks-reference has no manifest.txt, so completeness is unverifiable" +
                    $(if ($canRepair) { " - run tools\make-reference-blocks.ps1" }
                      else { " and the reference dump is absent, so it cannot be staged here" })
} elseif ($missingBlocks.Count -gt 0) {
    $shown = ($missingBlocks | Select-Object -First 6) -join ', '
    $extra = if ($missingBlocks.Count -gt 6) { " and $($missingBlocks.Count - 6) more" } else { "" }
    $artProblems += "$($missingBlocks.Count) block textures did not stage and will be served from OUR art: $shown$extra" +
                    $(if (-not $canRepair) { " (the reference dump is absent, so this cannot be repaired here)" })
}
if ($eggNums.Count -ne ($eggTop + 1) -or
    (($eggNums | Measure-Object -Maximum).Maximum -ne $eggTop)) {
    $artProblems += "spawn eggs staged $($eggNums.Count) of $($eggTop + 1), highest index $(($eggNums | Measure-Object -Maximum).Maximum)"
}
if ($soundCount -lt $soundFloor) {
    $artProblems += "sounds staged $soundCount, below the $soundFloor floor"
}

if ($artProblems.Count -gt 0) {
    foreach ($problem in $artProblems) { Write-Host "REFERENCE ART: $problem" -ForegroundColor Red }
} else {
    # Each number here is ONE measurement and the agreement is stated in words.
    # Printing "1340/1340" from a single count would dress one measurement up as
    # two, which is the same fiction as deriving a check from the thing it checks.
    $blockCount = @(Get-ChildItem $blockPlaceholder -Filter *.png -File -ErrorAction SilentlyContinue).Count
    Write-Host ("Reference art staged complete: {0} block textures on disk and every name in manifest.txt present; {1} spawn eggs contiguous 0..{2}; {3} sounds at or above the {4} floor. Creature, HUD and font atlases are single files, so presence is completeness. This is staging completeness only - whether every face the game ASKS for is covered is answered in Main.cpp, not here." -f $blockCount, $eggNums.Count, $eggTop, $soundCount, $soundFloor) -ForegroundColor DarkGray
}

# Started from its own directory because the game resolves assets and saves
# relative to the executable.
Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe)
Write-Host "Launched $Config build."
