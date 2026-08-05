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
$eggDir = Join-Path (Split-Path -Parent $exe) "spawn-eggs"
if (-not (Test-Path (Join-Path $eggDir "egg35.png")) -and (Test-Path $referenceRoot)) {
    & (Join-Path $root "tools\make-spawn-egg-sprites.ps1") | Out-Null
    Write-Host "Restored spawn egg sprites." -ForegroundColor DarkGray
}

# And again for the HUD proof atlas, which carries a COPY of our own hud.png
# with the reference's real tabs laid over ours. Regenerating the sheet without
# regenerating this leaves the game loading a stale copy of every HUD sprite,
# and a wrong sheet size does not fail - it silently skews all of them.
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
# art here, so there is no staleness to chase - they are either present or the
# game falls back to ours, per texture.
$blockPlaceholder = Join-Path (Split-Path -Parent $exe) "blocks-reference"
if (-not (Test-Path (Join-Path $blockPlaceholder "stone.png")) -and (Test-Path $referenceRoot)) {
    & (Join-Path $root "tools\make-reference-blocks.ps1") | Out-Null
    Write-Host "Restored placeholder block and item textures." -ForegroundColor DarkGray
}

# Started from its own directory because the game resolves assets and saves
# relative to the executable.
Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe)
Write-Host "Launched $Config build."
