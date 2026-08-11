# Builds assets/textures/hud.png by stacking the HUD widget art and the
# inventory panel into one sheet.
#
# One sheet because the HUD samples a single texture: a texture array needs
# every layer the same size, and these two are not. Stacking them keeps the
# widget art's existing pixel coordinates valid, since it stays at the origin.
#
#   .\tools\make-hud-sheet.ps1

param(
    [string]$Widgets = "reference\hud-native.png",
    [string]$Inventory = "reference\inv-9col.png",
    [string]$Output = "assets\textures\hud.png",

    # The inventory art is delivered at an integer multiple of its real pixel
    # grid. Sampling every Nth pixel recovers the original exactly, where
    # resizing would blur a crisp image into a soft one.
    [int]$InventoryScale = 2,

    # The mockup includes a drawn character in the preview panel. The game will
    # render its own there, so it is painted out. Native pixels, left/top/right/bottom.
    [int[]]$CharacterBox = @(26, 8, 74, 76)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$widgetPath = Join-Path $root $Widgets
$inventoryPath = Join-Path $root $Inventory

foreach ($path in @($widgetPath, $inventoryPath)) {
    if (-not (Test-Path $path)) {
        Write-Error "Missing source art: $path"
        exit 1
    }
}

# Names differ from the parameters above only in case, and PowerShell variables
# are case-insensitive, so these must not be $widgets and $inventory.
$widgetImage = [System.Drawing.Bitmap]::FromFile($widgetPath)
$sourceImage = [System.Drawing.Bitmap]::FromFile($inventoryPath)

$nativeWidth = [int]($sourceImage.Width / $InventoryScale)
$nativeHeight = [int]($sourceImage.Height / $InventoryScale)

$inventoryImage = New-Object System.Drawing.Bitmap -ArgumentList ([int]$nativeWidth), ([int]$nativeHeight),
    ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
for ($y = 0; $y -lt $nativeHeight; $y++) {
    for ($x = 0; $x -lt $nativeWidth; $x++) {
        $inventoryImage.SetPixel($x, $y, $sourceImage.GetPixel($x * $InventoryScale, $y * $InventoryScale))
    }
}

# Painted with the panel's own backdrop, sampled from a corner the drawing does
# not reach, so the box keeps whatever colour the artist chose for it.
$backdrop = $inventoryImage.GetPixel($CharacterBox[0], $CharacterBox[1])
for ($y = $CharacterBox[1]; $y -le $CharacterBox[3]; $y++) {
    for ($x = $CharacterBox[0]; $x -le $CharacterBox[2]; $x++) {
        $inventoryImage.SetPixel($x, $y, $backdrop)
    }
}

# A blocky arrow: a flat shaft, then a head stepping in one pixel per column so
# the diagonal is a clean 45 degrees rather than an anti-aliased curve.
#
# Shared by the crafting table and the furnace, which point across different
# gaps but draw the same arrow.
function Set-Arrow {
    param([System.Drawing.Bitmap]$Target, [int]$Left, [int]$Right, [int]$CentreY,
          [System.Drawing.Color]$Color)

    $reach = 7
    $headLeft = $Right - $reach
    for ($x = $Left; $x -lt $headLeft; $x++) {
        for ($y = $CentreY - 3; $y -le $CentreY + 3; $y++) { $Target.SetPixel($x, $y, $Color) }
    }
    for ($step = 0; $step -le $reach; $step++) {
        $x = $headLeft + $step
        $half = $reach - $step
        for ($y = $CentreY - $half; $y -le $CentreY + $half; $y++) { $Target.SetPixel($x, $y, $Color) }
    }
}

# The burn indicator: one flame, 14x14, orange rim around a yellow body with a
# brighter core.
#
# Described by the left and right edge of each row rather than as a character
# map, because that guarantees a clean one-pixel rim on every side - the rim is
# derived from where the neighbouring rows stop, so it cannot be drawn wrong.
#
# Three thin licks were tried first, matching the reference's own indicator, and
# at this size they read as lit matches rather than fire. One flame gets the
# full width and can carry an actual rim and core.
#
# The tip leans right and the body bulges low, which is what stops it reading as
# a balloon.
$flameSpans = @(
    @(8, 8), @(7, 9), @(7, 9), @(6, 9), @(5, 9), @(4, 10), @(4, 10),
    @(3, 10), @(3, 11), @(2, 11), @(2, 11), @(2, 11), @(3, 10), @(4, 9))

# Where the hottest part sits: low and slightly left of the lean, so the flame
# has a direction. -1 means this row has no core at all.
$flameCore = @(
    @(-1, -1), @(-1, -1), @(-1, -1), @(-1, -1), @(-1, -1), @(-1, -1), @(6, 8),
    @(5, 8), @(5, 9), @(4, 9), @(4, 9), @(4, 9), @(5, 8), @(-1, -1))

function Set-Flame {
    param([System.Drawing.Bitmap]$Target, [int]$Left, [int]$Top, [hashtable]$Palette)

    $rows = $flameSpans.Count
    $covers = {
        param([int]$row, [int]$x)
        if ($row -lt 0 -or $row -ge $rows) { return $false }
        return ($x -ge $flameSpans[$row][0]) -and ($x -le $flameSpans[$row][1])
    }

    for ($r = 0; $r -lt $rows; $r++) {
        $l = $flameSpans[$r][0]
        $rr = $flameSpans[$r][1]
        for ($x = $l; $x -le $rr; $x++) {
            $onRim = ($x -eq $l) -or ($x -eq $rr) -or
                     (-not (& $covers ($r - 1) $x)) -or (-not (& $covers ($r + 1) $x))
            if ($onRim) {
                $glyph = 'O'
            } elseif ($x -ge $flameCore[$r][0] -and $x -le $flameCore[$r][1]) {
                $glyph = 'W'
            } else {
                $glyph = 'Y'
            }
            $Target.SetPixel($Left + $x, $Top + $r, $Palette[$glyph])
        }
    }
}

# The crafting table's panel is *derived from* the inventory panel rather than
# drawn fresh: same frame, same backdrop, and the slot cells are literally the
# same pixels copied to new positions. That is what keeps the two screens
# looking like one game, and it means revising the artwork updates both.
#
# The top section is cleared wholesale - armour column, character box, offhand
# and the 2x2 grid all live there - and replaced with a 3x3, an arrow and a
# single result cell. The storage rows and hotbar below are untouched.
#
# Positions are the reference GUI's, shifted left by one to match our own art:
# our slot borders sit at x 7, 25, 43 ... where the reference's sit at 8, 26, 44.
# The rows needed no shift at all - both put storage at y 83 and the hotbar at
# y 141.
function New-ContainerPanel {
    param([System.Drawing.Bitmap]$Source)

    $panel = New-Object System.Drawing.Bitmap -ArgumentList $Source.Width, $Source.Height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $Source.Height; $y++) {
        for ($x = 0; $x -lt $Source.Width; $x++) {
            $panel.SetPixel($x, $y, $Source.GetPixel($x, $y))
        }
    }

    # Sampled rather than hardcoded, so a revised panel carries its own colours
    # through to everything drawn on the cleared area.
    $backdrop = $Source.GetPixel(5, 5)

    # Everything between the frame and the storage grid.
    for ($y = 3; $y -le 82; $y++) {
        for ($x = 3; $x -le ($Source.Width - 4); $x++) {
            $panel.SetPixel($x, $y, $backdrop)
        }
    }
    return $panel
}

# One storage cell, complete with its dark top-left border and light
# bottom-right highlight, is exactly 18x18.
function Copy-Cell {
    param([System.Drawing.Bitmap]$Target, [System.Drawing.Bitmap]$Source, [int]$DestX, [int]$DestY)

    for ($dy = 0; $dy -lt 18; $dy++) {
        for ($dx = 0; $dx -lt 18; $dx++) {
            $Target.SetPixel($DestX + $dx, $DestY + $dy, $Source.GetPixel(7 + $dx, 83 + $dy))
        }
    }
}

function New-CraftingPanel {
    param([System.Drawing.Bitmap]$Source)

    $panel = New-ContainerPanel -Source $Source
    for ($row = 0; $row -lt 3; $row++) {
        for ($column = 0; $column -lt 3; $column++) {
            Copy-Cell -Target $panel -Source $Source -DestX (28 + $column * 18) -DestY (16 + $row * 18)
        }
    }
    # Aligned with the middle row of the grid, right of the panel's centre.
    Copy-Cell -Target $panel -Source $Source -DestX 122 -DestY 34
    Set-Arrow -Target $panel -Left 89 -Right 109 -CentreY 43 -Color $Source.GetPixel(16, 92)
    return $panel
}

function New-FurnacePanel {
    param([System.Drawing.Bitmap]$Source)

    $panel = New-ContainerPanel -Source $Source
    # Input above, fuel below, with the burn indicator between them.
    Copy-Cell -Target $panel -Source $Source -DestX 54 -DestY 16
    Copy-Cell -Target $panel -Source $Source -DestX 54 -DestY 52
    Copy-Cell -Target $panel -Source $Source -DestX 114 -DestY 34

    # Both indicators are baked in their **spent** state. The lit versions live
    # on the sheet separately and are drawn over these, clipped to however far
    # along the furnace is.
    Set-Arrow -Target $panel -Left 79 -Right 99 -CentreY 43 -Color ([System.Drawing.Color]::FromArgb(255, 85, 85, 85))
    Set-Flame -Target $panel -Left 55 -Top 36 -Palette @{
        'O' = [System.Drawing.Color]::FromArgb(255, 88, 88, 88)
        'Y' = [System.Drawing.Color]::FromArgb(255, 112, 112, 112)
        'W' = [System.Drawing.Color]::FromArgb(255, 128, 128, 128)
    }
    return $panel
}

# Two inputs side by side and a result, with an arrow between. Derived from the
# inventory panel like every other screen, so the cells are literally the same
# pixels as the slots beneath them.
function New-SmithingPanel {
    param([System.Drawing.Bitmap]$Source)

    $panel = New-ContainerPanel -Source $Source
    Copy-Cell -Target $panel -Source $Source -DestX 36 -DestY 34
    Copy-Cell -Target $panel -Source $Source -DestX 54 -DestY 34
    Copy-Cell -Target $panel -Source $Source -DestX 122 -DestY 34
    Set-Arrow -Target $panel -Left 89 -Right 109 -CentreY 43 -Color $Source.GetPixel(16, 92)
    return $panel
}

# Three rows of nine above the player's own storage. The reference's chest GUI
# puts them at y 17, 35 and 53 on the same 18 px pitch and the same x columns as
# the storage rows below - so this is the inventory panel with the top cleared
# and the very same cell stamped 27 times.
function New-ChestPanel {
    param([System.Drawing.Bitmap]$Source)

    $panel = New-ContainerPanel -Source $Source
    for ($row = 0; $row -lt 3; $row++) {
        for ($column = 0; $column -lt 9; $column++) {
            Copy-Cell -Target $panel -Source $Source -DestX (7 + $column * 18) -DestY (17 + $row * 18)
        }
    }
    return $panel
}

# One input on the left and the cuts it offers in a row to the right, with an
# arrow between. Same derivation as every other screen, so the cells match.
function New-StonecutterPanel {
    param([System.Drawing.Bitmap]$Source)

    $panel = New-ContainerPanel -Source $Source
    Copy-Cell -Target $panel -Source $Source -DestX 19 -DestY 34
    for ($column = 0; $column -lt 3; $column++) {
        Copy-Cell -Target $panel -Source $Source -DestX (85 + $column * 18) -DestY 34
    }
    Set-Arrow -Target $panel -Left 54 -Right 74 -CentreY 43 -Color $Source.GetPixel(16, 92)
    return $panel
}

# Five slots in one centred row. The reference's hopper GUI is a short card of
# its own; ours is the ordinary container panel with that row stamped into it,
# so the cells are the same pixels as every other screen's.
function New-HopperPanel {
    param([System.Drawing.Bitmap]$Source)

    $panel = New-ContainerPanel -Source $Source
    # Five cells on the 18 px pitch come to 90, so the run starts at 43 to be
    # centred in the 176-wide card.
    for ($column = 0; $column -lt 5; $column++) {
        Copy-Cell -Target $panel -Source $Source -DestX (43 + $column * 18) -DestY 34
    }
    return $panel
}

# Six rows of nine, which does not fit the ordinary 166-tall card - so the plain
# interior is STRETCHED by three rows' worth and everything below it moves down
# together. The top border, the player's own grid and the bottom border are
# copied verbatim; only the empty middle is repeated, which is why no bevel or
# frame has to be redrawn.
function New-DoubleChestPanel {
    param([System.Drawing.Bitmap]$Source)

    $extra = 54
    $height = $Source.Height + $extra
    # Already cleared to flat backdrop between the frame and the storage grid,
    # so any row of it is a safe thing to repeat.
    $clean = New-ContainerPanel -Source $Source
    $panel = New-Object System.Drawing.Bitmap -ArgumentList $Source.Width, $height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    for ($x = 0; $x -lt $Source.Width; $x++) {
        for ($y = 0; $y -lt 3; $y++) {
            $panel.SetPixel($x, $y, $clean.GetPixel($x, $y))
        }
        $filler = $clean.GetPixel($x, 40)
        for ($y = 3; $y -lt (83 + $extra); $y++) {
            $panel.SetPixel($x, $y, $filler)
        }
        for ($y = 83; $y -lt $Source.Height; $y++) {
            $panel.SetPixel($x, $y + $extra, $Source.GetPixel($x, $y))
        }
    }

    for ($row = 0; $row -lt 6; $row++) {
        for ($column = 0; $column -lt 9; $column++) {
            Copy-Cell -Target $panel -Source $Source -DestX (7 + $column * 18) -DestY (17 + $row * 18)
        }
    }

    $clean.Dispose()
    return $panel
}

# The lit indicators, on transparent ground so they can be drawn over the panel
# and cut off part way. Sixteen rows is enough for both.
function New-IndicatorStrip {    $strip = New-Object System.Drawing.Bitmap -ArgumentList 176, 16,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($strip)
    $g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    $g.Dispose()

    Set-Flame -Target $strip -Left 0 -Top 1 -Palette @{
        'O' = [System.Drawing.Color]::FromArgb(255, 224, 100, 20)
        'Y' = [System.Drawing.Color]::FromArgb(255, 255, 200, 30)
        'W' = [System.Drawing.Color]::FromArgb(255, 255, 232, 122)
    }
    Set-Arrow -Target $strip -Left 16 -Right 36 -CentreY 8 -Color ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    return $strip
}

# The whole GUI language is one face colour lit from the top-left or the
# inverse, so a highlight and a shadow are the face scaled rather than two more
# colours to keep in step with the artwork.
function Get-Scaled {
    param([System.Drawing.Color]$Color, [double]$Factor)

    $red = [Math]::Max(0, [Math]::Min(255, [int]($Color.R * $Factor)))
    $green = [Math]::Max(0, [Math]::Min(255, [int]($Color.G * $Factor)))
    $blue = [Math]::Max(0, [Math]::Min(255, [int]($Color.B * $Factor)))
    return [System.Drawing.Color]::FromArgb(255, $red, $green, $blue)
}

# The recipe book card: 146 wide against the inventory's 176, the same 166 tall.
#
# Built by butting the source's left and right halves together, so both cards
# carry the *same* frame, mitred corners and drop shadow rather than a second
# hand-drawn approximation of them. The interior is then cleared, because the
# catalogue stamps its own cells at runtime - there are six background states
# and a baked grid could only ever show one of them.
function New-BookPanel {
    param([System.Drawing.Bitmap]$Source, [int]$Width = 146)

    $panel = New-Object System.Drawing.Bitmap -ArgumentList ([int]$Width), $Source.Height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $leftHalf = [int]($Width / 2)
    $rightHalf = $Width - $leftHalf
    for ($y = 0; $y -lt $Source.Height; $y++) {
        for ($x = 0; $x -lt $leftHalf; $x++) {
            $panel.SetPixel($x, $y, $Source.GetPixel($x, $y))
        }
        for ($x = 0; $x -lt $rightHalf; $x++) {
            $panel.SetPixel(($Width - 1 - $x), $y, $Source.GetPixel(($Source.Width - 1 - $x), $y))
        }
    }

    $backdrop = $Source.GetPixel(5, 5)
    for ($y = 3; $y -le ($Source.Height - 4); $y++) {
        for ($x = 3; $x -le ($Width - 4); $x++) {
            $panel.SetPixel($x, $y, $backdrop)
        }
    }
    return $panel
}

# The five category tabs, in both states: unselected on the top row, selected
# below it. Five slots across on a 24 px pitch, so each sprite has a two-pixel
# guard and mip generation cannot bleed one tab into its neighbour.
#
# Each slot is a *complete* tab, icon included, because that is how the
# reference's are - which means the proof atlas can replace a whole tab with a
# real one and nothing has to know. Ours stamp textures the world already uses
# rather than drawing anything new; only the search glyph is drawn, because no
# item means "search".
#
# The only difference between the two states is the interior: the panel's own
# face colour reads as part of the card, the recessed grey reads as behind it.
# There is no glow and no accent in the reference art either.
function New-TabStrip {
    param([System.Drawing.Bitmap]$Source, [string]$BlockDir, [int]$TabWidth = 22, [int]$TabHeight = 25,
          [int]$Pitch = 24, [int]$RowPitch = 27)

    $strip = New-Object System.Drawing.Bitmap -ArgumentList ([int]($Pitch * 4 + $TabWidth)),
        ([int]($RowPitch + $TabHeight)), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($strip)
    $graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    $graphics.Dispose()

    $face = $Source.GetPixel(5, 5)
    # Inside a storage cell, which is the recessed grey every slot interior uses.
    $recess = $Source.GetPixel(16, 92)
    $highlight = Get-Scaled -Color $face -Factor 1.3
    $shadow = Get-Scaled -Color $face -Factor 0.45
    $outline = [System.Drawing.Color]::FromArgb(255, 0, 0, 0)

    # Kept to straight runs and 45-degree steps: a traced curve is what made the
    # crafting table's corners read as circular.
    $magnifier = @(
        '...####.....',
        '..#....#....',
        '.#......#...',
        '.#......#...',
        '.#......#...',
        '..#....#....',
        '...####.....',
        '.....#.#....',
        '......###...',
        '.......###..',
        '........##..',
        '............')

    foreach ($selected in @($false, $true)) {
        $originY = if ($selected) { $RowPitch } else { 0 }
        $interior = if ($selected) { $face } else { $recess }
        for ($tab = 0; $tab -lt 5; $tab++) {
            $originX = $tab * $Pitch
            for ($y = 0; $y -lt $TabHeight; $y++) {
                for ($x = 0; $x -lt $TabWidth; $x++) {
                    $bottomEdge = ($y -ge ($TabHeight - 1)) -and (-not $selected)
                    if ($y -eq 0 -or $x -eq 0 -or $x -eq ($TabWidth - 1) -or $bottomEdge) {
                        $color = $outline
                    } elseif ($y -le 2 -or $x -le 2) {
                        $color = $highlight
                    } elseif ($x -ge ($TabWidth - 3)) {
                        $color = $shadow
                    } else {
                        $color = $interior
                    }
                    $strip.SetPixel(($originX + $x), ($originY + $y), $color)
                }
            }
            if ($tab -eq 4) {
                for ($y = 0; $y -lt $magnifier.Count; $y++) {
                    $row = $magnifier[$y]
                    for ($x = 0; $x -lt $row.Length; $x++) {
                        if ($row[$x] -eq '#') {
                            $strip.SetPixel(($originX + 5 + $x), ($originY + 7 + $y), $outline)
                        }
                    }
                }
            }
        }
    }

    # The interior is exactly sixteen columns wide, so a block texture drops in
    # at 1:1 with no scaling at all. Done after every frame, because SetPixel and
    # an open Graphics cannot both hold the same bitmap.
    $icons = @('bricks.png', 'stone_pickaxe.png', 'charcoal.png', 'grass_top.png')
    $graphics = [System.Drawing.Graphics]::FromImage($strip)
    $graphics.InterpolationMode = 'NearestNeighbor'
    $graphics.PixelOffsetMode = 'Half'
    for ($row = 0; $row -lt 2; $row++) {
        for ($tab = 0; $tab -lt $icons.Count; $tab++) {
            $iconPath = Join-Path $BlockDir $icons[$tab]
            if (-not (Test-Path $iconPath)) {
                Write-Warning "Tab icon missing: $iconPath"
                continue
            }
            $icon = [System.Drawing.Bitmap]::FromFile($iconPath)
            $graphics.DrawImage($icon, ($tab * $Pitch + 3), ($row * $RowPitch + 5), 16, 16)
            $icon.Dispose()
        }
    }
    $graphics.Dispose()
    return $strip
}

# The catalogue's four cell backgrounds, in `CellState` order:
#
#   accessible | accessible hovered | blocked | blocked hovered
#
# Every one is the panel's *own* 18x18 storage cell with its interior repainted,
# so all four carry the identical bevel and cannot drift from the slots beside
# them. The two base colours are the only things chosen here, and neither is
# invented: the accessible one is the slot interior's own recessed grey, taken
# from inside that same cell, and blocked is `243,74,63` measured off
# reference/crafting-ui.avif (INTERFACE.md 3.1b).
#
# **Hover is the only emphasis, and it is deliberately the only one.** A
# selected state was built and removed: a cell that stays lit after you have
# taken what was in it reads as a stuck slot, and nothing sets it anyway.
#
# 18 wide on a 20 px pitch leaves a two-pixel guard, exactly like the tab strip.
function New-CellStrip {
    param([System.Drawing.Bitmap]$Source, [int]$CellSize = 18, [int]$Pitch = 20, [int]$Height = 20,
          [int]$CellX = 7, [int]$CellY = 83)

    $strip = New-Object System.Drawing.Bitmap -ArgumentList ([int]($Pitch * 3 + $CellSize)), ([int]$Height),
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($strip)
    $graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    $graphics.Dispose()

    # **The accessible state is the ordinary slot grey, unchanged.** It is the
    # colour every slot interior on the right-hand card already uses, sampled
    # from inside the source's own storage cell, so "you can have this" looks
    # like an ordinary slot and only "you cannot" stands out.
    $pale = $Source.GetPixel(($CellX + 9), ($CellY + 9))
    $red = [System.Drawing.Color]::FromArgb(255, 243, 74, 63)

    # Hover is a mix toward white rather than a multiply, because red is already
    # at 243 and scaling it only clamps the one channel that carries the hue -
    # which turns "brighter red" into "washed-out orange".
    $lift = 0.45

    $states = @(
        @{ Fill = $pale; Lift = 0.0 },
        @{ Fill = $pale; Lift = $lift },
        @{ Fill = $red;  Lift = 0.0 },
        @{ Fill = $red;  Lift = $lift })

    for ($i = 0; $i -lt $states.Count; $i++) {
        $originX = $i * $Pitch
        $base = $states[$i].Fill
        $t = $states[$i].Lift
        $interior = [System.Drawing.Color]::FromArgb(255,
            [int]($base.R + (255 - $base.R) * $t),
            [int]($base.G + (255 - $base.G) * $t),
            [int]($base.B + (255 - $base.B) * $t))
        for ($y = 0; $y -lt $CellSize; $y++) {
            for ($x = 0; $x -lt $CellSize; $x++) {
                # The bevel is the source's, untouched; only the recessed
                # interior carries the state.
                $inside = ($x -ge 1) -and ($x -le ($CellSize - 2)) -and
                          ($y -ge 1) -and ($y -le ($CellSize - 2))
                if ($inside) {
                    $strip.SetPixel(($originX + $x), $y, $interior)
                } else {
                    $strip.SetPixel(($originX + $x), $y, $Source.GetPixel(($CellX + $x), ($CellY + $y)))
                }
            }
        }
    }
    return $strip
}

# The eight 9x9 status icons the survival bars are built from, in the order
# `hud::StatusIcon` names them. Deliberately the cheapest possible placeholders -
# flat blobs, no shading - because `make-reference-hud.ps1` stamps Mojang's real
# sprites over the top and the friend authoring our art will replace them. Time
# spent making a placeholder pretty is time spent on somebody else's job.
function New-StatusStrip {
    $count = 8
    $pitch = 10
    $size = 9
    $strip = New-Object System.Drawing.Bitmap -ArgumentList ([int]($count * $pitch)), ([int]$pitch),
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    $empty = [System.Drawing.Color]::FromArgb(255, 58, 58, 58)
    $heart = [System.Drawing.Color]::FromArgb(255, 220, 40, 40)
    $food = [System.Drawing.Color]::FromArgb(255, 150, 96, 42)
    $air = [System.Drawing.Color]::FromArgb(255, 200, 232, 255)
    $airLow = [System.Drawing.Color]::FromArgb(255, 132, 168, 200)

    # index, fill, whether only the left half is filled over an empty base
    $icons = @(
        @{ Fill = $empty;  Half = $false; Base = $null }
        @{ Fill = $heart;  Half = $false; Base = $null }
        @{ Fill = $heart;  Half = $true;  Base = $empty }
        @{ Fill = $empty;  Half = $false; Base = $null }
        @{ Fill = $food;   Half = $false; Base = $null }
        @{ Fill = $food;   Half = $true;  Base = $empty }
        @{ Fill = $air;    Half = $false; Base = $null }
        @{ Fill = $airLow; Half = $false; Base = $null }
    )

    for ($i = 0; $i -lt $count; $i++) {
        $originX = $i * $pitch
        for ($y = 0; $y -lt $size; $y++) {
            for ($x = 0; $x -lt $size; $x++) {
                # A one-texel bite out of each corner, so a blob reads as an
                # icon rather than as a square.
                $corner = (($x -eq 0) -or ($x -eq ($size - 1))) -and
                          (($y -eq 0) -or ($y -eq ($size - 1)))
                if ($corner) {
                    continue
                }
                $colour = $icons[$i].Fill
                if ($icons[$i].Half -and ($x -ge [int]($size / 2))) {
                    $colour = $icons[$i].Base
                }
                $strip.SetPixel(($originX + $x), $y, $colour)
            }
        }
    }
    return $strip
}

$craftingImage = New-CraftingPanel -Source $inventoryImage
$furnaceImage = New-FurnacePanel -Source $inventoryImage
$indicatorImage = New-IndicatorStrip
$bookImage = New-BookPanel -Source $inventoryImage
$tabImage = New-TabStrip -Source $inventoryImage -BlockDir (Join-Path $root "assets\textures\blocks")
$cellImage = New-CellStrip -Source $inventoryImage
$smithingImage = New-SmithingPanel -Source $inventoryImage
$chestImage = New-ChestPanel -Source $inventoryImage
$doubleChestImage = New-DoubleChestPanel -Source $inventoryImage
$statusImage = New-StatusStrip
$hopperImage = New-HopperPanel -Source $inventoryImage
$stonecutterImage = New-StonecutterPanel -Source $inventoryImage

$width = [Math]::Max($widgetImage.Width, $inventoryImage.Width)
$height = $widgetImage.Height + $inventoryImage.Height + $craftingImage.Height + $furnaceImage.Height +
    $indicatorImage.Height + $bookImage.Height + $tabImage.Height + $cellImage.Height +
    $smithingImage.Height + $chestImage.Height + $doubleChestImage.Height + $statusImage.Height +
    $hopperImage.Height + $stonecutterImage.Height

$sheet = New-Object System.Drawing.Bitmap -ArgumentList ([int]$width), ([int]$height),
    ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$g.InterpolationMode = 'NearestNeighbor'
$g.PixelOffsetMode = 'Half'
$g.DrawImage($widgetImage, 0, 0, $widgetImage.Width, $widgetImage.Height)
$g.DrawImage($inventoryImage, 0, $widgetImage.Height, $inventoryImage.Width, $inventoryImage.Height)
$craftingTop = $widgetImage.Height + $inventoryImage.Height
$g.DrawImage($craftingImage, 0, $craftingTop, $craftingImage.Width, $craftingImage.Height)
$furnaceTop = $craftingTop + $craftingImage.Height
$g.DrawImage($furnaceImage, 0, $furnaceTop, $furnaceImage.Width, $furnaceImage.Height)
$indicatorTop = $furnaceTop + $furnaceImage.Height
$g.DrawImage($indicatorImage, 0, $indicatorTop, $indicatorImage.Width, $indicatorImage.Height)
$bookTop = $indicatorTop + $indicatorImage.Height
$g.DrawImage($bookImage, 0, $bookTop, $bookImage.Width, $bookImage.Height)
$tabTop = $bookTop + $bookImage.Height
$g.DrawImage($tabImage, 0, $tabTop, $tabImage.Width, $tabImage.Height)
$cellTop = $tabTop + $tabImage.Height
$g.DrawImage($cellImage, 0, $cellTop, $cellImage.Width, $cellImage.Height)
$smithingTop = $cellTop + $cellImage.Height
$g.DrawImage($smithingImage, 0, $smithingTop, $smithingImage.Width, $smithingImage.Height)
$chestTop = $smithingTop + $smithingImage.Height
$g.DrawImage($chestImage, 0, $chestTop, $chestImage.Width, $chestImage.Height)
$doubleChestTop = $chestTop + $chestImage.Height
$g.DrawImage($doubleChestImage, 0, $doubleChestTop, $doubleChestImage.Width, $doubleChestImage.Height)
$statusTop = $doubleChestTop + $doubleChestImage.Height
$g.DrawImage($statusImage, 0, $statusTop, $statusImage.Width, $statusImage.Height)
# Appended last on purpose: every offset above is written down in
# `InventoryScreen.cpp`, so a new panel goes at the end where it moves nothing.
$hopperTop = $statusTop + $statusImage.Height
$g.DrawImage($hopperImage, 0, $hopperTop, $hopperImage.Width, $hopperImage.Height)
$stonecutterTop = $hopperTop + $hopperImage.Height
$g.DrawImage($stonecutterImage, 0, $stonecutterTop, $stonecutterImage.Width, $stonecutterImage.Height)
$g.Dispose()

$sheet.Save((Join-Path $root $Output), [System.Drawing.Imaging.ImageFormat]::Png)

Write-Host "wrote $Output ($width x $height)"
Write-Host "  widgets    at (0, 0) size $($widgetImage.Width) x $($widgetImage.Height)"
Write-Host "  inventory  at (0, $($widgetImage.Height)) size $nativeWidth x $nativeHeight"
Write-Host "  crafting   at (0, $craftingTop) size $($craftingImage.Width) x $($craftingImage.Height)"
Write-Host "  furnace    at (0, $furnaceTop) size $($furnaceImage.Width) x $($furnaceImage.Height)"
Write-Host "  indicators at (0, $indicatorTop): lit flame 14x14 at x 0, white arrow at x 16..36"
Write-Host "  book card  at (0, $bookTop) size $($bookImage.Width) x $($bookImage.Height)"
Write-Host "  tabs       at (0, $tabTop) size $($tabImage.Width) x $($tabImage.Height): 5 tabs 22x25 on a 24 px pitch, unselected row at +0, selected row at +27"
Write-Host "  cells      at (0, $cellTop) size $($cellImage.Width) x $($cellImage.Height): 4 states 18x18 on a 20 px pitch, in CellState order"
Write-Host "  smithing   at (0, $smithingTop) size $($smithingImage.Width) x $($smithingImage.Height)"
Write-Host "  chest      at (0, $chestTop) size $($chestImage.Width) x $($chestImage.Height)"
Write-Host "  chest x2   at (0, $doubleChestTop) size $($doubleChestImage.Width) x $($doubleChestImage.Height)"
Write-Host "  status     at (0, $statusTop) size $($statusImage.Width) x $($statusImage.Height): 8 icons 9x9 on a 10 px pitch, in StatusIcon order"
Write-Host "  hopper     at (0, $hopperTop) size $($hopperImage.Width) x $($hopperImage.Height)"
Write-Host "  stonecutter at (0, $stonecutterTop) size $($stonecutterImage.Width) x $($stonecutterImage.Height)"
Write-Host "  character box painted out: $($CharacterBox -join ', ')"

$sheet.Dispose()
$widgetImage.Dispose()
$sourceImage.Dispose()
$inventoryImage.Dispose()
$craftingImage.Dispose()
$furnaceImage.Dispose()
$indicatorImage.Dispose()
$cellImage.Dispose()
$smithingImage.Dispose()
$chestImage.Dispose()
$doubleChestImage.Dispose()
$hopperImage.Dispose()
$stonecutterImage.Dispose()
