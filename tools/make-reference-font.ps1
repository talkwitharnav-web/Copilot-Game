# Stages Mojang's own text font beside each built executable as
# `font-reference.png`.
#
# Same arrangement as `blocks-reference/` and `hud-reference.png`: reference art
# lives next to the exe, never under `assets/`, never ships, and the game
# prefers it over our own `assets/textures/font.png` whenever it is there.
#
# `assets/minecraft/textures/font/ascii.png` is 128x128: a 16x16 grid of 8x8
# cells where **the cell index is the codepoint**, which is why our own
# placeholder is generated in exactly that layout. Nothing is composited or
# resized here - it is a straight copy, and the only reason it is a script
# rather than a file copy in `run.ps1` is that the same rule about where
# reference pixels may live applies to it.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-reference-font.ps1

Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures\font\ascii.png'

if (-not (Test-Path $source)) {
    Write-Warning "No reference font at $source - the game will fall back to assets/textures/font.png."
    exit 0
}

$image = [System.Drawing.Bitmap]::FromFile((Resolve-Path $source))
try {
    if ($image.Width -ne 128 -or $image.Height -ne 128) {
        throw "ascii.png is $($image.Width)x$($image.Height), expected 128x128 - the glyph grid would be wrong."
    }

    $wrote = 0
    foreach ($config in @('debug', 'release')) {
        $binDir = Join-Path $root "build\$config\bin"
        if (-not (Test-Path $binDir)) {
            continue
        }
        $target = Join-Path $binDir 'font-reference.png'
        Copy-Item $source $target -Force
        Write-Host "wrote $target (128x128, 16x16 cells of 8x8)"
        $wrote++
    }
    if ($wrote -eq 0) {
        Write-Warning "No build\<config>\bin directory yet - build first, then re-run."
    }
}
finally {
    $image.Dispose()
}
