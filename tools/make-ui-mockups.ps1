# Composes the UI mock-ups the developer looks at and judges, out of the real
# reference sprites rather than out of anybody's memory of them.
#
# Why this is a script and not a pile of one-liners: every number it draws with
# is read from assets/ui/ui-atlas.json, assets/ui/ui-palette.json and
# assets/ui/ui-screens.json at run time, so a mock-up can never drift away from
# the measured data. Change a slot origin in the JSON and every picture moves.
# Nothing here hard-codes a border inset, a slot pitch or a grey.
#
# It draws nothing by hand where a real sprite exists. The button, the slider,
# the tab, the checkbox, the scroller, the tooltip frame, the hotbar, the hearts
# and the whole 176x166 inventory panel are the reference's own PNGs, nine-sliced
# with the borders their .mcmeta files declare. The classic panel bevel, the
# durability bar and the terrain backdrop are the only constructed pixels, and
# each one is built from a recipe recorded in the palette or the HUD fragment.
#
# Text is the reference's own font/ascii.png, glyph by glyph, with Minecraft's
# proportional advance (rightmost non-transparent column + 2, space = 4) and the
# standard quarter-brightness drop shadow that ui-palette.json records as
# text.java.shadow_rule = #3F3F3F. It is not a system font.
#
# Everything is composed in 1x GUI pixels and blitted at an integer scale with
# NearestNeighbor and PixelOffsetMode Half, so a mock-up is exactly the 1x
# picture magnified - no resampling anywhere, ever. A blurry Minecraft mock-up is
# a failed Minecraft mock-up.
#
# Output lands in the repository root as *-preview.png, which .gitignore already
# ignores - these are regenerable and are not history.
#
# Keep this file pure ASCII. PowerShell 5.1 reads a BOM-less .ps1 as
# Windows-1252 and mangles non-ASCII literals; ASCII-only is the cheap defence.

param(
    [string]$OutputDir = '',
    [ValidateRange(1, 8)][int]$Scale = 4,
    [string[]]$Only = @()
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = $root }
$referenceRoot = Join-Path $root 'reference'
$javaTextures = Join-Path $referenceRoot 'minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures'
$bedrockIcons = Join-Path $referenceRoot 'ui-icons'

# ---------------------------------------------------------------- measured data

function Read-UiJson {
    param([string]$Relative)
    $path = Join-Path $root $Relative
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing $path - run tools\extract-ui-spec.ps1 first." }
    return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
}

$atlas = Read-UiJson 'assets\ui\ui-atlas.json'
$palette = Read-UiJson 'assets\ui\ui-palette.json'
$screens = Read-UiJson 'assets\ui\ui-screens.json'

$sprites = @{}
foreach ($group in $atlas.groups) {
    foreach ($entry in $group.sprites) { $sprites[$entry.id] = $entry }
}

function ConvertTo-Colour {
    param([string]$Hex)
    $h = $Hex.Trim().TrimStart('#')
    if ($h.Length -eq 6) { $h = $h + 'FF' }
    if ($h.Length -ne 8) { throw "Bad colour '$Hex'" }
    return [System.Drawing.Color]::FromArgb(
        [Convert]::ToInt32($h.Substring(6, 2), 16),
        [Convert]::ToInt32($h.Substring(0, 2), 16),
        [Convert]::ToInt32($h.Substring(2, 2), 16),
        [Convert]::ToInt32($h.Substring(4, 2), 16))
}

$colours = @{}
foreach ($groupName in $palette.groups.PSObject.Properties.Name) {
    foreach ($entry in $palette.groups.$groupName) { $colours[$entry.id] = ConvertTo-Colour $entry.hex }
}

function Get-PaletteColour {
    param([string]$Id)
    if (-not $colours.ContainsKey($Id)) { throw "No palette entry '$Id' in assets\ui\ui-palette.json" }
    return $colours[$Id]
}

function Get-SpriteRecord {
    param([string]$Id)
    if (-not $sprites.ContainsKey($Id)) { throw "No atlas entry '$Id' in assets\ui\ui-atlas.json" }
    return $sprites[$Id]
}

Write-Host ("read {0} sprites, {1} colours, {2} screens" -f $sprites.Count, $colours.Count, $screens.screens.Count)

# ------------------------------------------------------------- bitmap plumbing

$script:bitmaps = @{}

# Loaded through a byte array rather than FromFile so no file handle is held for
# the life of the bitmap, and re-blitted into 32bppArgb so every source has the
# same pixel format and GDI+ never silently converts one mid-draw.
function Get-Bitmap {
    param([string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($script:bitmaps.ContainsKey($full)) { return $script:bitmaps[$full] }
    if (-not (Test-Path -LiteralPath $full)) { throw "Missing texture: $full" }
    $bytes = [System.IO.File]::ReadAllBytes($full)
    $stream = New-Object System.IO.MemoryStream -ArgumentList (, $bytes)
    $image = [System.Drawing.Image]::FromStream($stream)
    $copy = New-Object System.Drawing.Bitmap -ArgumentList $image.Width, $image.Height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($copy)
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.DrawImage($image, 0, 0, $image.Width, $image.Height)
    $graphics.Dispose()
    $image.Dispose()
    $stream.Dispose()
    $script:bitmaps[$full] = $copy
    return $copy
}

function Get-SpriteBitmap {
    param([string]$Id)
    return Get-Bitmap (Join-Path $referenceRoot (Get-SpriteRecord $Id).path)
}

function Get-Texture {
    param([string]$Relative)
    return Get-Bitmap (Join-Path $javaTextures $Relative)
}

function Get-BedrockIcon {
    param([string]$Name)
    return Get-Bitmap (Join-Path $bedrockIcons $Name)
}

$script:tinted = @{}

# Multiplies a source by a colour. Used for the font sheet (white glyphs become
# any colour), for foliage, and for the dirt backdrop - all of which are exactly
# what the game does with a vertex colour.
function Get-Tinted {
    param([System.Drawing.Bitmap]$Source, [System.Drawing.Color]$Colour)
    $key = "{0}|{1}" -f $Source.GetHashCode(), $Colour.ToArgb()
    if ($script:tinted.ContainsKey($key)) { return $script:tinted[$key] }
    $out = New-Object System.Drawing.Bitmap -ArgumentList $Source.Width, $Source.Height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($out)
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $matrix = New-Object System.Drawing.Imaging.ColorMatrix
    $matrix.Matrix00 = $Colour.R / 255.0
    $matrix.Matrix11 = $Colour.G / 255.0
    $matrix.Matrix22 = $Colour.B / 255.0
    $matrix.Matrix33 = $Colour.A / 255.0
    $attributes = New-Object System.Drawing.Imaging.ImageAttributes
    $attributes.SetColorMatrix($matrix)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $Source.Width, $Source.Height
    $graphics.DrawImage($Source, $rect, 0, 0, $Source.Width, $Source.Height,
        [System.Drawing.GraphicsUnit]::Pixel, $attributes)
    $attributes.Dispose()
    $graphics.Dispose()
    $script:tinted[$key] = $out
    return $out
}

# ------------------------------------------------------------------ the canvas

# TileFlipXY stops GDI+ sampling one texel outside the source rectangle, which is
# what puts a stray line of the neighbouring sprite along the edge of every crop
# taken from a sheet. Without it the 176x166 inventory panel wears a seam.
$script:wrap = New-Object System.Drawing.Imaging.ImageAttributes
$script:wrap.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)

function New-Canvas {
    param([int]$Width, [int]$Height)
    $bitmap = New-Object System.Drawing.Bitmap -ArgumentList ($Width * $Scale), ($Height * $Scale),
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $graphics.Clear([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
    return [pscustomobject]@{ Bitmap = $bitmap; G = $graphics; Width = $Width; Height = $Height }
}

function Save-Canvas {
    param($Canvas, [string]$Name)
    $path = Join-Path $OutputDir $Name
    $Canvas.G.Dispose()
    $Canvas.Bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host ("wrote {0} ({1} x {2}, {3}x{4} GUI px at {5}x)" -f $Name,
        $Canvas.Bitmap.Width, $Canvas.Bitmap.Height, $Canvas.Width, $Canvas.Height, $Scale)
    $Canvas.Bitmap.Dispose()
}

function Add-Blit {
    param($Canvas, [System.Drawing.Bitmap]$Source,
          [int]$SrcX, [int]$SrcY, [int]$SrcW, [int]$SrcH,
          [int]$X, [int]$Y, [int]$W, [int]$H)
    if ($SrcW -le 0 -or $SrcH -le 0 -or $W -le 0 -or $H -le 0) { return }
    $dest = New-Object System.Drawing.Rectangle ($X * $Scale), ($Y * $Scale), ($W * $Scale), ($H * $Scale)
    $Canvas.G.DrawImage($Source, $dest, $SrcX, $SrcY, $SrcW, $SrcH,
        [System.Drawing.GraphicsUnit]::Pixel, $script:wrap)
}

function Add-Rect {
    param($Canvas, [System.Drawing.Color]$Colour, [int]$X, [int]$Y, [int]$W, [int]$H)
    if ($W -le 0 -or $H -le 0) { return }
    $brush = New-Object System.Drawing.SolidBrush -ArgumentList $Colour
    $Canvas.G.FillRectangle($brush, ($X * $Scale), ($Y * $Scale), ($W * $Scale), ($H * $Scale))
    $brush.Dispose()
}

function Add-Tiled {
    param($Canvas, [System.Drawing.Bitmap]$Source,
          [int]$SrcX, [int]$SrcY, [int]$SrcW, [int]$SrcH,
          [int]$X, [int]$Y, [int]$W, [int]$H)
    if ($SrcW -le 0 -or $SrcH -le 0 -or $W -le 0 -or $H -le 0) { return }
    $oy = 0
    while ($oy -lt $H) {
        $th = [Math]::Min($SrcH, $H - $oy)
        $ox = 0
        while ($ox -lt $W) {
            $tw = [Math]::Min($SrcW, $W - $ox)
            Add-Blit $Canvas $Source $SrcX $SrcY $tw $th ($X + $ox) ($Y + $oy) $tw $th
            $ox += $tw
        }
        $oy += $th
    }
}

# Corners 1:1, edges repeated along one axis, centre repeated on both - which is
# what a nine_slice without stretchInner does. tooltip/frame is the one sprite in
# the whole set that sets stretchInner, and the atlas says so, so it is read from
# the atlas rather than special-cased here.
function Add-NineSlice {
    param($Canvas, [string]$Id, [int]$X, [int]$Y, [int]$W, [int]$H)
    $record = Get-SpriteRecord $Id
    $bitmap = Get-SpriteBitmap $Id
    $sw = [int]$record.width
    $sh = [int]$record.height
    $left = 0; $top = 0; $right = 0; $bottom = 0
    if ($record.PSObject.Properties.Name -contains 'border' -and $record.border) {
        $left = [int]$record.border.left
        $top = [int]$record.border.top
        $right = [int]$record.border.right
        $bottom = [int]$record.border.bottom
    }
    if ($left + $right -ge $W) { $left = [Math]::Max(0, [Math]::Floor($W / 2)); $right = $W - $left }
    if ($top + $bottom -ge $H) { $top = [Math]::Max(0, [Math]::Floor($H / 2)); $bottom = $H - $top }
    $stretchInner = $false
    if ($record.PSObject.Properties.Name -contains 'stretchInner') { $stretchInner = [bool]$record.stretchInner }

    $midSrcW = $sw - $left - $right
    $midSrcH = $sh - $top - $bottom
    $midW = $W - $left - $right
    $midH = $H - $top - $bottom

    Add-Blit $Canvas $bitmap 0 0 $left $top $X $Y $left $top
    Add-Blit $Canvas $bitmap ($sw - $right) 0 $right $top ($X + $W - $right) $Y $right $top
    Add-Blit $Canvas $bitmap 0 ($sh - $bottom) $left $bottom $X ($Y + $H - $bottom) $left $bottom
    Add-Blit $Canvas $bitmap ($sw - $right) ($sh - $bottom) $right $bottom ($X + $W - $right) ($Y + $H - $bottom) $right $bottom

    if ($stretchInner) {
        Add-Blit $Canvas $bitmap $left 0 $midSrcW $top ($X + $left) $Y $midW $top
        Add-Blit $Canvas $bitmap $left ($sh - $bottom) $midSrcW $bottom ($X + $left) ($Y + $H - $bottom) $midW $bottom
        Add-Blit $Canvas $bitmap 0 $top $left $midSrcH $X ($Y + $top) $left $midH
        Add-Blit $Canvas $bitmap ($sw - $right) $top $right $midSrcH ($X + $W - $right) ($Y + $top) $right $midH
        Add-Blit $Canvas $bitmap $left $top $midSrcW $midSrcH ($X + $left) ($Y + $top) $midW $midH
    } else {
        Add-Tiled $Canvas $bitmap $left 0 $midSrcW $top ($X + $left) $Y $midW $top
        Add-Tiled $Canvas $bitmap $left ($sh - $bottom) $midSrcW $bottom ($X + $left) ($Y + $H - $bottom) $midW $bottom
        Add-Tiled $Canvas $bitmap 0 $top $left $midSrcH $X ($Y + $top) $left $midH
        Add-Tiled $Canvas $bitmap ($sw - $right) $top $right $midSrcH ($X + $W - $right) ($Y + $top) $right $midH
        Add-Tiled $Canvas $bitmap $left $top $midSrcW $midSrcH ($X + $left) ($Y + $top) $midW $midH
    }
}

function Add-Sprite {
    param($Canvas, [string]$Id, [int]$X, [int]$Y, [int]$W = 0, [int]$H = 0)
    $record = Get-SpriteRecord $Id
    if ($W -le 0) { $W = [int]$record.width }
    if ($H -le 0) { $H = [int]$record.height }
    if ($record.scaling -eq 'nine_slice') { Add-NineSlice $Canvas $Id $X $Y $W $H; return }
    Add-Blit $Canvas (Get-SpriteBitmap $Id) 0 0 ([int]$record.width) ([int]$record.height) $X $Y $W $H
}

function Push-Clip {
    param($Canvas, [int]$X, [int]$Y, [int]$W, [int]$H)
    $rect = New-Object System.Drawing.Rectangle ($X * $Scale), ($Y * $Scale), ($W * $Scale), ($H * $Scale)
    $Canvas.G.SetClip($rect)
}

function Pop-Clip {
    param($Canvas)
    $Canvas.G.ResetClip()
}

# -------------------------------------------------------------------- the font

$script:fontSheet = $null
$script:fontWidths = $null

# Minecraft's own rule: a glyph's advance is its rightmost non-transparent column
# plus two (one for the glyph's last column, one for the gap), and space is a
# flat 4. Measured out of the sheet rather than tabulated, so a different
# ascii.png would simply measure differently.
function Initialize-Font {
    if ($script:fontSheet) { return }
    $script:fontSheet = Get-Texture 'font\ascii.png'
    $widths = New-Object 'int[]' 256
    for ($code = 0; $code -lt 256; $code++) {
        $cellX = ($code % 16) * 8
        $cellY = [int][Math]::Floor($code / 16) * 8
        $right = -1
        for ($col = 7; $col -ge 0; $col--) {
            $any = $false
            for ($row = 0; $row -lt 8; $row++) {
                if ($script:fontSheet.GetPixel($cellX + $col, $cellY + $row).A -ne 0) { $any = $true; break }
            }
            if ($any) { $right = $col; break }
        }
        $widths[$code] = $right + 2
    }
    $widths[32] = 4
    $script:fontWidths = $widths
}

function Measure-Text {
    param([string]$Text, [int]$Size = 1)
    Initialize-Font
    $width = 0
    foreach ($ch in $Text.ToCharArray()) {
        $code = [int]$ch
        if ($code -gt 255) { $code = 63 }
        $width += $script:fontWidths[$code]
    }
    return $width * $Size
}

function Add-Glyphs {
    param($Canvas, [string]$Text, [int]$X, [int]$Y, [System.Drawing.Color]$Colour, [int]$Size)
    $sheet = Get-Tinted $script:fontSheet $Colour
    $cursor = $X
    foreach ($ch in $Text.ToCharArray()) {
        $code = [int]$ch
        if ($code -gt 255) { $code = 63 }
        $cellX = ($code % 16) * 8
        $cellY = [int][Math]::Floor($code / 16) * 8
        if ($code -ne 32) { Add-Blit $Canvas $sheet $cellX $cellY 8 8 $cursor $Y (8 * $Size) (8 * $Size) }
        $cursor += $script:fontWidths[$code] * $Size
    }
}

# The shadow is the colour at a quarter brightness, offset by one text pixel.
# White gives #3F3F3F, which is exactly the text.java.shadow_rule the palette
# recorded off a screenshot - so the rule and the sample agree.
function Add-Text {
    param($Canvas, [string]$Text, [int]$X, [int]$Y, [System.Drawing.Color]$Colour,
          [int]$Size = 1, [bool]$Shadow = $true)
    Initialize-Font
    if ($Shadow) {
        $dark = [System.Drawing.Color]::FromArgb($Colour.A, [int]($Colour.R / 4), [int]($Colour.G / 4), [int]($Colour.B / 4))
        Add-Glyphs $Canvas $Text ($X + $Size) ($Y + $Size) $dark $Size
    }
    Add-Glyphs $Canvas $Text $X $Y $Colour $Size
}

function Add-TextCentred {
    param($Canvas, [string]$Text, [int]$CentreX, [int]$Y, [System.Drawing.Color]$Colour,
          [int]$Size = 1, [bool]$Shadow = $true)
    Add-Text $Canvas $Text ([int]($CentreX - (Measure-Text $Text $Size) / 2)) $Y $Colour $Size $Shadow
}

function Add-TextRight {
    param($Canvas, [string]$Text, [int]$RightX, [int]$Y, [System.Drawing.Color]$Colour,
          [int]$Size = 1, [bool]$Shadow = $true)
    Add-Text $Canvas $Text ($RightX - (Measure-Text $Text $Size)) $Y $Colour $Size $Shadow
}

$white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
$grey = Get-PaletteColour 'text.code.7.gray'
$darkGrey = Get-PaletteColour 'text.code.8.dark_gray'
$yellow = [System.Drawing.Color]::FromArgb(255, 255, 255, 160)
$gold = Get-PaletteColour 'text.code.6.gold'
$red = Get-PaletteColour 'text.code.c.red'
$green = Get-PaletteColour 'text.code.a.green'
$titleGrey = Get-PaletteColour 'text.java.container_title'

# ------------------------------------------------------------ classic surfaces

# The panel bevel exactly as assets/ui/ui-screens.json records it: a 1px black
# outline, a 2px #FFFFFF top-left, a #C6C6C6 face and a 2px #555555 bottom-right.
# Offsets are read from the JSON bands, not written down here.
function Add-ClassicPanel {
    param($Canvas, [int]$X, [int]$Y, [int]$W, [int]$H)
    Add-Rect $Canvas (Get-PaletteColour 'java.panel.fill') $X $Y $W $H
    foreach ($band in $screens.panel.border_top_left) {
        $colour = ConvertTo-Colour $band.colour
        $offset = [int]$band.offset
        $width = [int]$band.width
        Add-Rect $Canvas $colour ($X + $offset) $Y $width $H
        Add-Rect $Canvas $colour $X ($Y + $offset) $W $width
    }
    foreach ($band in $screens.panel.border_bottom_right) {
        $colour = ConvertTo-Colour $band.colour
        $offset = [int]$band.offset
        $width = [int]$band.width
        Add-Rect $Canvas $colour ($X + $W + $offset) $Y $width $H
        Add-Rect $Canvas $colour $X ($Y + $H + $offset) $W $width
    }
}

function Add-Slot {
    param($Canvas, [int]$FrameX, [int]$FrameY)
    Add-Sprite $Canvas 'container/slot' $FrameX $FrameY
}

# The hover highlight is the reference's own pair, drawn at the item origin minus
# (4,4) exactly as assets/ui/ui-screens.json cell.highlight says.
function Add-SlotHighlight {
    param($Canvas, [int]$ItemX, [int]$ItemY)
    $inset = [int]$screens.cell.highlight.nine_slice_border
    $size = [int]$screens.cell.highlight.size[0]
    Add-Sprite $Canvas 'container/slot_highlight_back' ($ItemX - $inset) ($ItemY - $inset) $size $size
    Add-Sprite $Canvas 'container/slot_highlight_front' ($ItemX - $inset) ($ItemY - $inset) $size $size
}

function Add-Item {
    param($Canvas, [string]$Texture, [int]$ItemX, [int]$ItemY)
    $bitmap = Get-Texture $Texture
    Add-Blit $Canvas $bitmap 0 0 16 16 $ItemX $ItemY 16 16
}

# hue = remaining/3 at S=V=1 collapses to these two lines - the same constexpr
# the HUD fragment specifies, with the same three known points.
function Get-DurabilityColour {
    param([double]$Remaining)
    $f = [Math]::Max(0.0, [Math]::Min(1.0, $Remaining))
    if ($f -le 0.5) { $r = 1.0; $g = 2.0 * $f } else { $r = 2.0 - 2.0 * $f; $g = 1.0 }
    return [System.Drawing.Color]::FromArgb(255, [int]($r * 255), [int]($g * 255), 0)
}

# Count bottom-right with a shadow, durability bar 13x2 at (2,13) - both of them
# things the game gets wrong today, which is the whole reason they are drawn here.
function Add-StackDecorations {
    param($Canvas, [int]$ItemX, [int]$ItemY, [int]$Count = 1, [double]$Remaining = -1)
    if ($Remaining -ge 0) {
        Add-Rect $Canvas ([System.Drawing.Color]::FromArgb(255, 0, 0, 0)) ($ItemX + 2) ($ItemY + 13) 13 2
        $filled = [int][Math]::Round(13 * $Remaining)
        Add-Rect $Canvas (Get-DurabilityColour $Remaining) ($ItemX + 2) ($ItemY + 13) $filled 1
    }
    if ($Count -gt 1) { Add-TextRight $Canvas ([string]$Count) ($ItemX + 17) ($ItemY + 9) $white 1 $true }
}

function Add-Tooltip {
    param($Canvas, [int]$TextX, [int]$TextY, $Lines)
    $spec = $screens.widgets.tooltip
    $margin = [int]$spec.margin
    $pitch = [int]$spec.line_pitch
    $extra = [int]$spec.first_line_extra
    $width = 0
    foreach ($line in $Lines) {
        $w = Measure-Text $line.Text 1
        if ($w -gt $width) { $width = $w }
    }
    $height = 8
    if ($Lines.Count -gt 1) { $height = 8 + $extra + ($Lines.Count - 1) * $pitch }
    $boxX = $TextX - $margin
    $boxY = $TextY - $margin
    $boxW = $width + $margin * 2
    $boxH = $height + $margin * 2
    Add-NineSlice $Canvas 'tooltip/background' $boxX $boxY $boxW $boxH
    Add-NineSlice $Canvas 'tooltip/frame' $boxX $boxY $boxW $boxH
    $y = $TextY
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        Add-Text $Canvas $Lines[$i].Text $TextX $y $Lines[$i].Colour 1 $true
        $y += $pitch
        if ($i -eq 0) { $y += $extra }
    }
}

# --------------------------------------------------------------- the backdrops

# The classic menu backdrop: block/dirt.png tiled at 32 GUI px and multiplied to
# a quarter brightness, then the reference's own menu_background.png (flat black
# at alpha 64) tiled over it - which is what the modern screens actually draw.
function Add-MenuBackground {
    param($Canvas, [string]$Overlay = 'background/menu_background')
    $dirt = Get-Tinted (Get-Texture 'block\dirt.png') ([System.Drawing.Color]::FromArgb(255, 64, 64, 64))
    for ($y = 0; $y -lt $Canvas.Height; $y += 32) {
        for ($x = 0; $x -lt $Canvas.Width; $x += 32) {
            Add-Blit $Canvas $dirt 0 0 16 16 $x $y 32 32
        }
    }
    $overlayBitmap = Get-SpriteBitmap $Overlay
    Add-Tiled $Canvas $overlayBitmap 0 0 16 16 0 0 $Canvas.Width $Canvas.Height
}

$script:worldCache = @{}

# A stand-in world so the in-world screens have something behind them: real block
# textures, a hand-written height profile, no engine involved. It exists to prove
# the dim layers and the HUD contrast read correctly against a busy scene, and it
# is the one thing in this file that is not reference-accurate by construction.
function Get-WorldBackdrop {
    param([int]$Width, [int]$Height)
    $key = "$Width x $Height"
    if ($script:worldCache.ContainsKey($key)) { return $script:worldCache[$key] }

    $bitmap = New-Object System.Drawing.Bitmap -ArgumentList $Width, $Height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $canvas = [pscustomobject]@{ Bitmap = $bitmap; G = [System.Drawing.Graphics]::FromImage($bitmap);
        Width = $Width; Height = $Height }
    $canvas.G.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $canvas.G.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half

    # The world backdrop is built at 1x and blitted scaled like everything else,
    # so it borrows the canvas helpers by temporarily running them at scale 1.
    $keepScale = $Scale
    Set-Variable -Name Scale -Value 1 -Scope Script

    for ($y = 0; $y -lt $Height; $y++) {
        $t = $y / [double]([Math]::Max(1, $Height - 1))
        $r = [int](0x6C + (0xC3 - 0x6C) * $t)
        $g = [int](0x9A + (0xD8 - 0x9A) * $t)
        $b = [int](0xE8 + (0xF2 - 0xE8) * $t)
        Add-Rect $canvas ([System.Drawing.Color]::FromArgb(255, $r, $g, $b)) 0 $y $Width 1
    }

    $cloud = [System.Drawing.Color]::FromArgb(190, 255, 255, 255)
    foreach ($c in @(@(18, 24, 84, 10), @(140, 40, 56, 8), @(238, 18, 108, 10), @(372, 46, 72, 8), @(60, 58, 44, 6))) {
        Add-Rect $canvas $cloud $c[0] $c[1] $c[2] $c[3]
    }

    $grass = Get-Texture 'block\grass_block_side.png'
    $dirt = Get-Texture 'block\dirt.png'
    $stone = Get-Texture 'block\stone.png'
    $cobble = Get-Texture 'block\cobblestone.png'
    $coal = Get-Texture 'block\coal_ore.png'
    $log = Get-Texture 'block\oak_log.png'
    $leaves = Get-Tinted (Get-Texture 'block\oak_leaves.png') ([System.Drawing.Color]::FromArgb(255, 0x59, 0xAE, 0x30))

    $groundY = [int]($Height * 0.60)
    $columns = [int][Math]::Ceiling($Width / 16.0)
    $surface = New-Object 'int[]' $columns
    for ($i = 0; $i -lt $columns; $i++) {
        $surface[$i] = [int][Math]::Round(2.0 * [Math]::Sin($i * 0.55) + 1.4 * [Math]::Sin($i * 0.21 + 1.3))
    }

    for ($i = 0; $i -lt $columns; $i++) {
        $x = $i * 16
        $top = $groundY - $surface[$i] * 16
        Add-Blit $canvas $grass 0 0 16 16 $x $top 16 16
        $y = $top + 16
        $depth = 0
        while ($y -lt $Height) {
            $texture = $stone
            if ($depth -lt 2) { $texture = $dirt }
            elseif ((($i * 7 + $depth * 5) % 11) -eq 0) { $texture = $cobble }
            elseif ((($i * 3 + $depth * 7) % 17) -eq 0) { $texture = $coal }
            Add-Blit $canvas $texture 0 0 16 16 $x $y 16 16
            $shade = [Math]::Min(140, 18 + $depth * 17)
            Add-Rect $canvas ([System.Drawing.Color]::FromArgb($shade, 0, 0, 0)) $x $y 16 16
            $y += 16
            $depth++
        }
    }

    foreach ($treeColumn in @(4, 21)) {
        if ($treeColumn -ge $columns) { continue }
        $x = $treeColumn * 16
        $trunkBase = $groundY - $surface[$treeColumn] * 16
        for ($t = 1; $t -le 4; $t++) { Add-Blit $canvas $log 0 0 16 16 $x ($trunkBase - $t * 16) 16 16 }
        for ($ly = 0; $ly -lt 2; $ly++) {
            for ($lx = -2; $lx -le 2; $lx++) {
                if ($ly -eq 1 -and [Math]::Abs($lx) -eq 2) { continue }
                Add-Blit $canvas $leaves 0 0 16 16 ($x + $lx * 16) ($trunkBase - (5 + $ly) * 16) 16 16
            }
        }
        Add-Blit $canvas $leaves 0 0 16 16 $x ($trunkBase - 7 * 16) 16 16
    }

    Set-Variable -Name Scale -Value $keepScale -Scope Script
    $canvas.G.Dispose()
    $script:worldCache[$key] = $bitmap
    return $bitmap
}

function Add-World {
    param($Canvas)
    $world = Get-WorldBackdrop $Canvas.Width $Canvas.Height
    Add-Blit $Canvas $world 0 0 $Canvas.Width $Canvas.Height 0 0 $Canvas.Width $Canvas.Height
}

function Add-Dim {
    param($Canvas, [System.Drawing.Color]$Colour)
    Add-Rect $Canvas $Colour 0 0 $Canvas.Width $Canvas.Height
}

# ------------------------------------------------------------------- the parts

# One button, in whatever state, with its label centred and vertically placed the
# way the reference does it: (height - 8) / 2 from the top.
function Add-Button {
    param($Canvas, [int]$X, [int]$Y, [int]$W, [string]$Label,
          [ValidateSet('normal', 'highlighted', 'disabled')][string]$State = 'normal',
          [int]$H = 0)
    if ($H -le 0) { $H = [int]$screens.widgets.button.size[1] }
    $id = 'widget/button'
    if ($State -eq 'highlighted') { $id = 'widget/button_highlighted' }
    if ($State -eq 'disabled') { $id = 'widget/button_disabled' }
    Add-NineSlice $Canvas $id $X $Y $W $H
    if ($Label) {
        $colour = $white
        if ($State -eq 'highlighted') { $colour = $yellow }
        if ($State -eq 'disabled') { $colour = [System.Drawing.Color]::FromArgb(255, 160, 160, 160) }
        Add-TextCentred $Canvas $Label ($X + [int]($W / 2)) ($Y + [int](($H - 8) / 2)) $colour 1 $true
    }
}

function Add-Slider {
    param($Canvas, [int]$X, [int]$Y, [int]$W, [double]$Value, [string]$Label,
          [bool]$Highlighted = $false)
    $H = [int]$screens.widgets.slider.size[1]
    $trackId = 'widget/slider'
    $handleId = 'widget/slider_handle'
    if ($Highlighted) { $trackId = 'widget/slider_highlighted'; $handleId = 'widget/slider_handle_highlighted' }
    Add-NineSlice $Canvas $trackId $X $Y $W $H
    $handleW = [int]$screens.widgets.slider.handle.size[0]
    $handleX = $X + [int](($W - $handleW) * [Math]::Max(0.0, [Math]::Min(1.0, $Value)))
    Add-NineSlice $Canvas $handleId $handleX $Y $handleW $H
    if ($Label) { Add-TextCentred $Canvas $Label ($X + [int]($W / 2)) ($Y + [int](($H - 8) / 2)) $white 1 $true }
}

function Add-TextField {
    param($Canvas, [int]$X, [int]$Y, [int]$W, [string]$Text, [bool]$Focused = $false,
          [string]$Placeholder = '')
    $H = [int]$screens.widgets.text_field.size[1]
    $id = 'widget/text_field'
    if ($Focused) { $id = 'widget/text_field_highlighted' }
    Add-NineSlice $Canvas $id $X $Y $W $H
    $body = $Text
    $colour = $white
    if (-not $body -and $Placeholder) { $body = $Placeholder; $colour = $darkGrey }
    if ($body) { Add-Text $Canvas $body ($X + 4) ($Y + 6) $colour 1 $true }
    if ($Focused) {
        $caretX = $X + 4 + (Measure-Text $Text 1)
        Add-Rect $Canvas $white $caretX ($Y + 5) 1 10
    }
}

function Add-Tab {
    param($Canvas, [int]$X, [int]$Y, [int]$W, [string]$Label, [bool]$Selected = $false,
          [bool]$Highlighted = $false)
    $H = [int]$screens.widgets.tab.size[1]
    $id = 'widget/tab'
    if ($Selected -and $Highlighted) { $id = 'widget/tab_selected_highlighted' }
    elseif ($Selected) { $id = 'widget/tab_selected' }
    elseif ($Highlighted) { $id = 'widget/tab_highlighted' }
    Add-NineSlice $Canvas $id $X $Y $W $H
    $colour = $grey
    if ($Selected) { $colour = $white }
    if ($Label) { Add-TextCentred $Canvas $Label ($X + [int]($W / 2)) ($Y + [int](($H - 8) / 2)) $colour 1 $true }
}

function Add-Scrollbar {
    param($Canvas, [int]$X, [int]$Y, [int]$H, [double]$Fraction, [double]$Offset)
    $W = [int]$screens.widgets.scroller.size[0]
    Add-NineSlice $Canvas 'widget/scroller_background' $X $Y $W $H
    $thumbH = [Math]::Max(8, [int]($H * [Math]::Min(1.0, $Fraction)))
    $thumbY = $Y + [int](($H - $thumbH) * [Math]::Max(0.0, [Math]::Min(1.0, $Offset)))
    Add-NineSlice $Canvas 'widget/scroller' $X $thumbY $W $thumbH
}

function Add-Checkbox {
    param($Canvas, [int]$X, [int]$Y, [bool]$Selected = $false, [bool]$Highlighted = $false)
    $id = 'widget/checkbox'
    if ($Selected -and $Highlighted) { $id = 'widget/checkbox_selected_highlighted' }
    elseif ($Selected) { $id = 'widget/checkbox_selected' }
    elseif ($Highlighted) { $id = 'widget/checkbox_highlighted' }
    Add-Sprite $Canvas $id $X $Y
}

function Add-Separator {
    param($Canvas, [string]$Id, [int]$Y)
    Add-Tiled $Canvas (Get-SpriteBitmap $Id) 0 0 32 2 0 $Y $Canvas.Width 2
}

# -------------------------------------------------------------- the mock-ups

$canvasWidth = 480
$canvasHeight = 270
$centreX = [int]($canvasWidth / 2)
$centreY = [int]($canvasHeight / 2)

function New-MainMenuMockup {
    $c = New-Canvas $canvasWidth $canvasHeight
    Add-MenuBackground $c

    # gui/title/minecraft.png is a 1024x256 high-resolution image whose top
    # 1024x176 - the opaque box the atlas measured - is blitted into 256x44 GUI
    # px. At Scale 4 that lands at exactly its native size, so nothing resamples.
    $logo = Get-SpriteRecord 'title/minecraft'
    $logoBitmap = Get-SpriteBitmap 'title/minecraft'
    $logoW = 256
    $logoH = [int]($logo.opaque.height / 4)
    Add-Blit $c $logoBitmap 0 0 ([int]$logo.opaque.width) ([int]$logo.opaque.height) `
        ($centreX - [int]($logoW / 2)) ($centreY - 96) $logoW $logoH

    $buttonW = [int]$screens.widgets.button.size[0]
    $buttonH = [int]$screens.widgets.button.size[1]
    $buttonX = $centreX - [int]($buttonW / 2)
    $rows = @(
        @{ Label = 'Play'; CentreY = 4; State = 'normal' },
        @{ Label = 'Options...'; CentreY = 28; State = 'highlighted' },
        @{ Label = 'How to Play'; CentreY = 52; State = 'normal' },
        @{ Label = 'Quit Game'; CentreY = 84; State = 'normal' }
    )
    foreach ($row in $rows) {
        Add-Button $c $buttonX ($centreY + $row.CentreY - [int]($buttonH / 2)) $buttonW $row.Label $row.State
    }

    Add-Text $c 'Voxel Game - M29' 4 ($canvasHeight - 12) $white 1 $true
    Add-TextRight $c 'Not affiliated with Mojang' ($canvasWidth - 4) ($canvasHeight - 12) $white 1 $true
    Save-Canvas $c 'ui-mockup-mainmenu-preview.png'
}

function New-SettingsMockup {
    $c = New-Canvas $canvasWidth $canvasHeight
    Add-MenuBackground $c

    $tabH = [int]$screens.widgets.tab.size[1]
    $tabTop = $centreY - 112
    $tabNames = @('Video', 'Audio', 'Controls', 'Game', 'Accessibility')
    $tabW = [Math]::Min([int]$screens.widgets.tab.size[0], [int][Math]::Floor((340 - 4) / $tabNames.Count))
    $stripW = $tabW * $tabNames.Count + ($tabNames.Count - 1)
    $tabX = $centreX - [int]($stripW / 2)
    for ($i = 0; $i -lt $tabNames.Count; $i++) {
        Add-Tab $c ($tabX + $i * ($tabW + 1)) $tabTop $tabW $tabNames[$i] ($i -eq 0) ($i -eq 2)
    }

    $panelW = 340
    $panelX = $centreX - [int]($panelW / 2)
    $listTop = $centreY - 88
    $listBottom = $centreY + 88
    $listH = $listBottom - $listTop

    Add-Separator $c 'background/header_separator' ($listTop - 2)
    Add-Tiled $c (Get-SpriteBitmap 'background/menu_list_background') 0 0 16 16 $panelX $listTop $panelW $listH
    Add-Separator $c 'background/footer_separator' $listBottom

    $inset = 6
    $contentX = $panelX + $inset
    $contentW = $panelW - $inset * 2
    $controlW = [int]$screens.widgets.button.size[0]
    $controlX = $panelX + $panelW - $inset - $controlW

    # The 340 px panel cannot hold "Render Distance: 12 chunks" at the left inset
    # AND a 200x20 control on the right - 140 px of label into 128 px of room. So
    # the value moves onto the control, which is also where the reference puts it.
    $rows = @(
        @{ Kind = 'header'; Label = 'Performance' },
        @{ Kind = 'slider'; Label = 'Render Distance'; Value = '12 chunks'; Fraction = 0.36;
           Description = 'How far the world is drawn' },
        @{ Kind = 'cycler'; Label = 'Frame Rate Limit'; Value = '120 fps';
           Description = 'Caps how many frames are drawn each second' },
        @{ Kind = 'header'; Label = 'Quality' },
        @{ Kind = 'cycler'; Label = 'Bloom'; Value = 'On'; Hover = $true;
           Description = 'Bright things bleed a soft glow into what is around them' },
        @{ Kind = 'slider'; Label = 'Field of View'; Value = '70'; Fraction = 0.33;
           Description = 'How wide an angle the camera sees' },
        @{ Kind = 'check'; Label = 'Fullscreen'; Checked = $true },
        @{ Kind = 'slider'; Label = 'Render Scale'; Value = '100%'; Fraction = 1.0;
           Description = 'Draws the world smaller and stretches it up' }
    )

    Push-Clip $c $panelX $listTop $panelW $listH
    $y = $listTop + 4
    foreach ($row in $rows) {
        if ($row.Kind -eq 'header') {
            Add-Text $c $row.Label $contentX ($y + 6) $white 1 $true
            Add-Rect $c ([System.Drawing.Color]::FromArgb(38, 255, 255, 255)) $contentX ($y + 17) $contentW 1
            $y += 20
            continue
        }
        $hasDescription = $row.ContainsKey('Description')
        $rowH = 24
        if ($hasDescription) { $rowH = 34 }
        Add-Text $c $row.Label $contentX ($y + 8) $white 1 $true
        if ($row.Kind -eq 'slider') {
            $hover = $false
            if ($row.ContainsKey('Hover')) { $hover = [bool]$row.Hover }
            Add-Slider $c $controlX ($y + 2) $controlW ([double]$row.Fraction) $row.Value $hover
        } elseif ($row.Kind -eq 'cycler') {
            $state = 'normal'
            if ($row.ContainsKey('Hover') -and $row.Hover) { $state = 'highlighted' }
            Add-Button $c $controlX ($y + 2) $controlW $row.Value $state
        } elseif ($row.Kind -eq 'check') {
            Add-Checkbox $c ($panelX + $panelW - $inset - 20) ($y + 2) ([bool]$row.Checked) $false
        }
        if ($hasDescription) { Add-Text $c $row.Description $contentX ($y + 24) $grey 1 $true }
        $y += $rowH
    }
    Pop-Clip $c

    $contentHeight = $y - ($listTop + 4)
    Add-Scrollbar $c ($panelX + $panelW + 2) $listTop $listH ($listH / [double]$contentHeight) 0.0

    $footerW = 98
    $footerY = $centreY + 104 - 10
    Add-Button $c ($centreX - $footerW - 2) $footerY $footerW 'Reset to Defaults' 'normal'
    Add-Button $c ($centreX + 2) $footerY $footerW 'Done' 'normal'
    Save-Canvas $c 'ui-mockup-settings-preview.png'
}

function New-PauseMockup {
    $c = New-Canvas $canvasWidth $canvasHeight
    Add-World $c
    Add-Dim $c ([System.Drawing.Color]::FromArgb([int](0.55 * 255), 0, 0, 0))

    Add-TextCentred $c 'Game Paused' $centreX ($centreY - 72) $white 1 $true
    $buttonW = [int]$screens.widgets.button.size[0]
    $buttonH = [int]$screens.widgets.button.size[1]
    $buttonX = $centreX - [int]($buttonW / 2)
    $rows = @(
        @{ Label = 'Back to Game'; CentreY = -40; State = 'highlighted' },
        @{ Label = 'Options...'; CentreY = -16; State = 'normal' },
        @{ Label = 'How to Play'; CentreY = 8; State = 'normal' },
        @{ Label = 'Save and Quit to Title'; CentreY = 40; State = 'normal' }
    )
    foreach ($row in $rows) {
        Add-Button $c $buttonX ($centreY + $row.CentreY - [int]($buttonH / 2)) $buttonW $row.Label $row.State
    }
    Save-Canvas $c 'ui-mockup-pause-preview.png'
}

function New-HudMockup {
    $c = New-Canvas $canvasWidth $canvasHeight
    Add-World $c

    $W = $canvasWidth
    $H = $canvasHeight
    $midX = [int]($W / 2)

    Add-Sprite $c 'hud/crosshair' ([int](($W - 15) / 2)) ([int](($H - 15) / 2))

    $hotbar = $screens.hud.hotbar
    $hotbarX = $midX - 91
    $hotbarY = $H - [int]$hotbar.size[1]
    Add-Sprite $c 'hud/hotbar' $hotbarX $hotbarY

    $selected = 3
    $selection = $screens.hud.hotbar_selection
    Add-Sprite $c 'hud/hotbar_selection' ($midX - 92 + [int]$hotbar.pitch_x * $selected) ($H - [int]$selection.size[1])

    # The offhand plate: 29x24, drawn to the left of the bar at the same baseline.
    Add-Sprite $c 'hud/hotbar_offhand_left' ($hotbarX - 29) ($H - 23)
    Add-Item $c 'item\ender_pearl.png' ($hotbarX - 25) ($H - 19)

    $slots = @(
        @{ Texture = 'item\diamond_pickaxe.png'; Remaining = 0.72 },
        @{ Texture = 'item\diamond_sword.png'; Remaining = 0.31 },
        @{ Texture = 'item\iron_axe.png'; Remaining = 0.09 },
        @{ Texture = 'block\cobblestone.png'; Count = 64 },
        @{ Texture = 'block\oak_planks.png'; Count = 23 },
        @{ Texture = 'item\bread.png'; Count = 8 },
        @{ Texture = 'item\coal.png'; Count = 17 },
        @{ Texture = 'item\iron_ingot.png'; Count = 5 },
        @{ Texture = 'item\bucket.png' }
    )
    for ($i = 0; $i -lt $slots.Count; $i++) {
        $itemX = $midX - 90 + [int]$hotbar.pitch_x * $i + 2
        $itemY = $H - 19
        Add-Item $c $slots[$i].Texture $itemX $itemY
        $count = 1
        if ($slots[$i].ContainsKey('Count')) { $count = [int]$slots[$i].Count }
        $remaining = -1.0
        if ($slots[$i].ContainsKey('Remaining')) { $remaining = [double]$slots[$i].Remaining }
        Add-StackDecorations $c $itemX $itemY $count $remaining
    }

    $xp = $screens.hud.experience_bar
    $xpW = [int]$xp.size[0]
    $xpX = [int](($W - $xpW) / 2)
    $xpY = $H - 29
    Add-Sprite $c 'hud/experience_bar_background' $xpX $xpY
    $progress = [int](0.62 * ($xpW + 1))
    Add-Blit $c (Get-SpriteBitmap 'hud/experience_bar_progress') 0 0 $progress 5 $xpX $xpY $progress 5
    Add-TextCentred $c '27' $midX ($H - 35) ([System.Drawing.Color]::FromArgb(255, 128, 255, 32)) 1 $true

    # Ten hearts on an 8 px pitch over a 9 px icon - they overlap by one, which is
    # why ten of them span 81 and not 90.
    $statusPitch = [int]$screens.hud.status_icons.pitch_x
    $heartY = $H - 39
    $hearts = 7.5
    for ($i = 0; $i -lt 10; $i++) {
        $x = $midX - 91 + $i * $statusPitch
        Add-Sprite $c 'hud/heart/container' $x $heartY
        if ($i + 1 -le [Math]::Floor($hearts)) { Add-Sprite $c 'hud/heart/full' $x $heartY }
        elseif ($i -lt $hearts) { Add-Sprite $c 'hud/heart/half' $x $heartY }
    }

    $armourY = $heartY - 10
    $armour = 6
    for ($i = 0; $i -lt 10; $i++) {
        $x = $midX - 91 + $i * $statusPitch
        Add-Sprite $c 'hud/armor_empty' $x $armourY
        if ($i * 2 + 2 -le $armour) { Add-Sprite $c 'hud/armor_full' $x $armourY }
        elseif ($i * 2 + 1 -eq $armour) { Add-Sprite $c 'hud/armor_half' $x $armourY }
    }

    $food = 8
    for ($i = 0; $i -lt 10; $i++) {
        $x = $midX + 91 - $i * $statusPitch - 9
        Add-Sprite $c 'hud/food_empty' $x $heartY
        if ($i * 2 + 2 -le $food) { Add-Sprite $c 'hud/food_full' $x $heartY }
        elseif ($i * 2 + 1 -eq $food) { Add-Sprite $c 'hud/food_half' $x $heartY }
    }

    $air = 7
    for ($i = 0; $i -lt 10; $i++) {
        $x = $midX + 91 - $i * $statusPitch - 9
        if ($i -lt $air) { Add-Sprite $c 'hud/air' $x ($heartY - 10) }
        else { Add-Sprite $c 'hud/air_bursting' $x ($heartY - 10) }
    }

    # Status effects: 24x24 frame on a 25 px pitch in from the right, beneficial
    # row 1 px from the top, harmful row 26 px below it, 18x18 icon inset by 3.
    # The remaining-time line is Bedrock behaviour rather than a measured pixel
    # rect, so it is drawn only under the bottom row, where there is room for it
    # without touching the row above.
    $effects = @(
        @{ Icon = 'regeneration'; Row = 0; Slot = 0 },
        @{ Icon = 'speed'; Row = 0; Slot = 1; Ambient = $true },
        @{ Icon = 'poison'; Row = 1; Slot = 0; Time = '0:08' }
    )
    foreach ($effect in $effects) {
        $x = $W - 1 - 24 - $effect.Slot * 25
        $y = 1 + $effect.Row * 26
        $frame = 'hud/effect_background'
        if ($effect.ContainsKey('Ambient') -and $effect.Ambient) { $frame = 'hud/effect_background_ambient' }
        Add-Sprite $c $frame $x $y
        Add-Blit $c (Get-Texture ("mob_effect\{0}.png" -f $effect.Icon)) 0 0 18 18 ($x + 3) ($y + 3) 18 18
        if ($effect.ContainsKey('Time')) { Add-TextCentred $c $effect.Time ($x + 12) ($y + 25) $white 1 $true }
    }

    Add-TextCentred $c 'Cobblestone' $midX ($H - 59) $white 1 $true
    Save-Canvas $c 'ui-mockup-hud-preview.png'
}

function New-InventoryMockup {
    $c = New-Canvas $canvasWidth $canvasHeight
    Add-World $c
    Add-Dim $c ([System.Drawing.Color]::FromArgb([int](0.55 * 255), 0, 0, 0))

    $spec = $screens.screens | Where-Object { $_.id -eq 'inventory' }
    $panelW = [int]$spec.panel[0]
    $panelH = [int]$spec.panel[1]
    $panelX = [int](($canvasWidth - $panelW) / 2)
    $panelY = [int](($canvasHeight - $panelH) / 2)

    # The whole 176x166 rectangle straight off gui/container/inventory.png - so
    # the armour column, the offhand cell and the crafting well are the reference
    # art, not a reconstruction. That is the point of this picture.
    Add-Blit $c (Get-SpriteBitmap 'screen/inventory') `
        ([int]$spec.panel_origin_in_file[0]) ([int]$spec.panel_origin_in_file[1]) $panelW $panelH `
        $panelX $panelY $panelW $panelH

    Add-Text $c 'Crafting' ($panelX + 97) ($panelY + 8) $titleGrey 1 $false

    # A flat paper doll from the reference skin, in the player well the JSON
    # measures. Not a rendered model - a stand-in for one.
    $well = $spec.art | Where-Object { $_.id -eq 'player_preview_well' }
    $skin = Get-Texture 'entity\player\wide\steve.png'
    $dollX = $panelX + [int]$well.interior_x + [int](([int]$well.interior_w - 32) / 2)
    $dollY = $panelY + [int]$well.interior_y + [int](([int]$well.interior_h - 64) / 2)
    Add-Blit $c $skin 20 20 8 12 ($dollX + 8) ($dollY + 16) 16 24
    Add-Blit $c $skin 44 20 4 12 ($dollX + 24) ($dollY + 16) 8 24
    Add-Blit $c $skin 36 52 4 12 $dollX ($dollY + 16) 8 24
    Add-Blit $c $skin 4 20 4 12 ($dollX + 8) ($dollY + 40) 8 24
    Add-Blit $c $skin 20 52 4 12 ($dollX + 16) ($dollY + 40) 8 24
    Add-Blit $c $skin 8 8 8 8 ($dollX + 8) $dollY 16 16
    Add-Blit $c $skin 40 8 8 8 ($dollX + 8) $dollY 16 16

    $regions = @{}
    foreach ($region in $spec.regions) { $regions[$region.id] = $region }

    function Add-RegionItems {
        param($Canvas, $Region, $Items)
        $index = 0
        for ($row = 0; $row -lt [int]$Region.rows; $row++) {
            for ($col = 0; $col -lt [int]$Region.cols; $col++) {
                if ($index -ge $Items.Count) { return }
                $entry = $Items[$index]
                $index++
                if (-not $entry) { continue }
                $itemX = $panelX + [int]$Region.origin_x + $col * [int]$Region.pitch_x
                $itemY = $panelY + [int]$Region.origin_y + $row * [int]$Region.pitch_y
                Add-Item $Canvas $entry.Texture $itemX $itemY
                $count = 1
                if ($entry.ContainsKey('Count')) { $count = [int]$entry.Count }
                $remaining = -1.0
                if ($entry.ContainsKey('Remaining')) { $remaining = [double]$entry.Remaining }
                Add-StackDecorations $Canvas $itemX $itemY $count $remaining
            }
        }
    }

    Add-RegionItems $c $regions['armour'] @(
        @{ Texture = 'item\diamond_helmet.png'; Remaining = 0.88 },
        @{ Texture = 'item\iron_chestplate.png'; Remaining = 0.44 },
        $null,
        @{ Texture = 'item\diamond_boots.png'; Remaining = 0.96 })
    Add-RegionItems $c $regions['offhand'] @(@{ Texture = 'block\torch.png'; Count = 12 })

    # the leggings slot is left empty on purpose, so the empty-slot icon system
    # (container/slot/leggings, drawn at runtime rather than baked into the panel)
    # is visible next to three filled slots.
    $legY = $panelY + [int]$regions['armour'].origin_y + 2 * [int]$regions['armour'].pitch_y
    Add-Sprite $c 'container/slot/leggings' ($panelX + [int]$regions['armour'].origin_x) $legY
    Add-RegionItems $c $regions['crafting_grid'] @(
        @{ Texture = 'block\oak_planks.png'; Count = 2 },
        @{ Texture = 'block\oak_planks.png'; Count = 2 },
        @{ Texture = 'block\oak_planks.png'; Count = 2 },
        @{ Texture = 'block\oak_planks.png'; Count = 2 })
    Add-RegionItems $c $regions['crafting_result'] @(@{ Texture = 'block\crafting_table_front.png' })
    Add-RegionItems $c $regions['storage'] @(
        @{ Texture = 'item\coal.png'; Count = 34 }, @{ Texture = 'item\iron_ingot.png'; Count = 12 },
        @{ Texture = 'item\gold_ingot.png'; Count = 3 }, @{ Texture = 'item\diamond.png'; Count = 7 },
        @{ Texture = 'item\redstone.png'; Count = 45 }, $null,
        @{ Texture = 'item\apple.png'; Count = 6 }, @{ Texture = 'item\wheat.png'; Count = 21 },
        @{ Texture = 'item\stick.png'; Count = 63 },
        @{ Texture = 'block\sand.png'; Count = 64 }, @{ Texture = 'block\gravel.png'; Count = 28 },
        $null, @{ Texture = 'item\iron_shovel.png'; Remaining = 0.15 },
        @{ Texture = 'item\bucket.png' }, $null, $null,
        @{ Texture = 'item\arrow.png'; Count = 30 }, @{ Texture = 'item\bone.png'; Count = 9 },
        @{ Texture = 'block\oak_log.png'; Count = 41 }, @{ Texture = 'item\string.png'; Count = 14 },
        $null, $null, @{ Texture = 'item\golden_apple.png'; Count = 2 })
    Add-RegionItems $c $regions['hotbar'] @(
        @{ Texture = 'item\diamond_pickaxe.png'; Remaining = 0.72 },
        @{ Texture = 'item\diamond_sword.png'; Remaining = 0.31 },
        @{ Texture = 'item\iron_axe.png'; Remaining = 0.09 },
        @{ Texture = 'block\cobblestone.png'; Count = 64 },
        @{ Texture = 'block\oak_planks.png'; Count = 23 },
        @{ Texture = 'item\bread.png'; Count = 8 },
        @{ Texture = 'item\coal.png'; Count = 17 },
        @{ Texture = 'item\iron_ingot.png'; Count = 5 },
        @{ Texture = 'item\bucket.png' })

    # Hovered cell: the third storage slot on the second row, highlighted with the
    # reference's own back/front pair, then the tooltip anchored off it.
    $storage = $regions['storage']
    $hoverX = $panelX + [int]$storage.origin_x + 3 * [int]$storage.pitch_x
    $hoverY = $panelY + [int]$storage.origin_y + 1 * [int]$storage.pitch_y
    Add-SlotHighlight $c $hoverX $hoverY

    Add-Tooltip $c ($hoverX + [int]$screens.widgets.tooltip.mouse_offset) ($hoverY + 16) @(
        @{ Text = 'Iron Shovel'; Colour = $white },
        @{ Text = 'Durability: 37 / 251'; Colour = $grey },
        @{ Text = 'When in main hand:'; Colour = $grey },
        @{ Text = ' 1 Attack Damage'; Colour = (Get-PaletteColour 'text.code.9.blue') })

    Save-Canvas $c 'ui-mockup-inventory-preview.png'
}

function New-DeathMockup {
    $c = New-Canvas $canvasWidth $canvasHeight
    Add-World $c
    Add-Dim $c ([System.Drawing.Color]::FromArgb([int](0.55 * 255), [int](0.40 * 255), [int](0.02 * 255), [int](0.02 * 255)))

    Add-TextCentred $c 'You Died!' $centreX ($centreY - 56) $white 3 $true
    Add-TextCentred $c 'You fell from a high place.' $centreX ($centreY - 32) $grey 1 $true
    Add-TextCentred $c 'Your bed was missing or blocked.' $centreX ($centreY - 16) `
        ([System.Drawing.Color]::FromArgb(255, 255, 171, 64)) 1 $true

    $buttonW = [int]$screens.widgets.button.size[0]
    $buttonH = [int]$screens.widgets.button.size[1]
    $buttonX = $centreX - [int]($buttonW / 2)
    Add-Button $c $buttonX ($centreY + 16 - [int]($buttonH / 2)) $buttonW 'Respawn' 'highlighted'
    Add-Button $c $buttonX ($centreY + 40 - [int]($buttonH / 2)) $buttonW 'Title Screen' 'normal'

    Add-TextCentred $c 'Score: 1 274' $centreX ($centreY + 62) $yellow 1 $true
    Save-Canvas $c 'ui-mockup-death-preview.png'
}

function New-WidgetsMockup {
    $sheetW = 480
    $sheetH = 604
    $c = New-Canvas $sheetW $sheetH
    Add-Rect $c ([System.Drawing.Color]::FromArgb(255, 26, 26, 30)) 0 0 $sheetW $sheetH

    $left = 16
    $rightColumn = 256
    $y = 10
    Add-Text $c 'WIDGET VOCABULARY' $left $y $white 2 $true
    $y += 20
    Add-Text $c 'every part below is reference art, nine-sliced per assets/ui/ui-atlas.json' $left $y $grey 1 $true
    $y += 18

    function Add-SectionHeading {
        param($Canvas, [string]$Text, [int]$X, [int]$Y)
        Add-Rect $Canvas ([System.Drawing.Color]::FromArgb(255, 60, 60, 68)) $X ($Y + 10) ($sheetW - $X * 2) 1
        Add-Text $Canvas $Text $X $Y $gold 1 $true
    }

    function Add-Caption {
        param($Canvas, [string]$Text, [int]$CentreX, [int]$Y)
        Add-TextCentred $Canvas $Text $CentreX $Y $grey 1 $true
    }

    Add-SectionHeading $c 'BUTTON  widget/button  200x20  nine-slice 3' $left $y
    $y += 18
    Add-Button $c $left $y 200 'Play' 'normal'
    Add-Button $c $rightColumn $y 200 'Play' 'highlighted'
    Add-Caption $c 'normal' ($left + 100) ($y + 22)
    Add-Caption $c 'hover  (label 255,255,160)' ($rightColumn + 100) ($y + 22)
    $y += 34
    Add-Button $c $left $y 200 'Play' 'disabled'
    Add-Button $c $rightColumn $y 98 'Done' 'normal'
    Add-Button $c ($rightColumn + 102) $y 98 'Cancel' 'normal'
    Add-Caption $c 'disabled  (border 1, not 3)' ($left + 100) ($y + 22)
    Add-Caption $c '98x20 pair - the same sprite, re-sliced' ($rightColumn + 100) ($y + 22)
    $y += 38

    Add-SectionHeading $c 'SLIDER  widget/slider 200x20 + widget/slider_handle 8x20' $left $y
    $y += 18
    Add-Slider $c $left $y 200 0.0 'Render Distance: 2' $false
    Add-Slider $c $rightColumn $y 200 0.62 'Master Volume: 62%' $true
    Add-Caption $c 'normal, handle at 0' ($left + 100) ($y + 22)
    Add-Caption $c 'hover  (track and handle both swap)' ($rightColumn + 100) ($y + 22)
    $y += 38

    Add-SectionHeading $c 'TEXT FIELD  widget/text_field 200x20  nine-slice 1' $left $y
    $y += 18
    Add-TextField $c $left $y 200 '' $false 'Leave blank for a random seed'
    Add-TextField $c $rightColumn $y 200 '-1739 043 662' $true
    Add-Caption $c 'idle, placeholder in dark grey' ($left + 100) ($y + 22)
    Add-Caption $c 'focused, with caret' ($rightColumn + 100) ($y + 22)
    $y += 38

    Add-SectionHeading $c 'CHECKBOX 20x20 - and the small buttons beside it' $left $y
    $y += 18
    $states = @(@($false, $false, 'off'), @($false, $true, 'off hover'), @($true, $false, 'on'), @($true, $true, 'on hover'))
    for ($i = 0; $i -lt $states.Count; $i++) {
        $x = $left + $i * 52
        Add-Checkbox $c $x $y $states[$i][0] $states[$i][1]
        Add-Caption $c $states[$i][2] ($x + 10) ($y + 22)
    }
    $smallX = $rightColumn
    foreach ($small in @(@('widget/cross_button', 'cross'), @('widget/locked_button', 'locked'),
                         @('widget/unlocked_button', 'unlocked'))) {
        Add-Sprite $c $small[0] $smallX $y
        Add-Caption $c $small[1] ($smallX + 9) ($y + 22)
        $smallX += 40
    }
    Add-Sprite $c 'widget/page_backward' $smallX ($y + 3)
    Add-Caption $c 'page' ($smallX + 11) ($y + 22)
    Add-Sprite $c 'widget/page_forward' ($smallX + 27) ($y + 3)
    Add-Caption $c 'page' ($smallX + 38) ($y + 22)
    $y += 38

    Add-SectionHeading $c 'TAB  widget/tab 130x24  nine-slice L2 T2 R2 B0 - no bottom edge' $left $y
    $y += 18
    $tabW = 106
    $tabStates = @(@($false, $false, 'unselected'), @($false, $true, 'unselected hover'),
                   @($true, $false, 'selected'), @($true, $true, 'selected hover'))
    for ($i = 0; $i -lt $tabStates.Count; $i++) {
        $x = $left + $i * ($tabW + 6)
        Add-Tab $c $x $y $tabW 'Video' $tabStates[$i][0] $tabStates[$i][1]
        Add-Caption $c $tabStates[$i][2] ($x + [int]($tabW / 2)) ($y + 26)
    }
    $y += 42

    Add-SectionHeading $c 'SCROLLBAR, PANEL, POPUP AND TOOLTIP' $left $y
    $y += 18
    # Five equal columns so each part and its caption share a centre line.
    $columns = @(64, 160, 256, 352, 440)
    Add-Scrollbar $c ($columns[0] - 3) $y 60 0.4 0.25
    Add-Caption $c 'widget/scroller' $columns[0] ($y + 64)
    # The Bedrock scrollbar art is 15x133, so it is rebuilt three-slice into 60 px
    # rather than cropped - a straight crop threw away both end caps and read as a
    # plain stripe.
    $bedrockBar = Get-BedrockIcon 'scrollbar.png'
    Add-Blit $c $bedrockBar 0 0 15 22 ($columns[1] - 7) $y 15 22
    Add-Blit $c $bedrockBar 0 55 15 16 ($columns[1] - 7) ($y + 22) 15 16
    Add-Blit $c $bedrockBar 0 111 15 22 ($columns[1] - 7) ($y + 38) 15 22
    Add-Caption $c 'bedrock 3-slice' $columns[1] ($y + 64)
    Add-ClassicPanel $c ($columns[2] - 40) $y 80 60
    Add-TextCentred $c 'panel' $columns[2] ($y + 26) $titleGrey 1 $false
    Add-Caption $c 'classic bevel' $columns[2] ($y + 64)
    Add-Sprite $c 'popup/background' ($columns[3] - 44) $y 88 60
    Add-TextCentred $c 'popup' $columns[3] ($y + 26) $white 1 $true
    Add-Caption $c 'nine-slice 6' $columns[3] ($y + 64)
    Add-Sprite $c 'tooltip/background' ($columns[4] - 36) $y 72 60
    Add-Sprite $c 'tooltip/frame' ($columns[4] - 36) $y 72 60
    Add-TextCentred $c 'tooltip' $columns[4] ($y + 26) $white 1 $true
    Add-Caption $c 'frame 9 + 10' $columns[4] ($y + 64)
    $y += 82

    Add-SectionHeading $c 'SLOT  18x18 frame, 16x16 item area at +1,+1, pitch 18' $left $y
    $y += 20
    $slotPitch = 92
    $slotColumns = @($left, ($left + $slotPitch), ($left + $slotPitch * 2), ($left + $slotPitch * 3), ($left + $slotPitch * 4))

    Add-Slot $c $slotColumns[0] $y
    Add-Caption $c 'empty' ($slotColumns[0] + 9) ($y + 24)

    Add-Slot $c $slotColumns[1] $y
    Add-Item $c 'item\diamond.png' ($slotColumns[1] + 1) ($y + 1)
    Add-StackDecorations $c ($slotColumns[1] + 1) ($y + 1) 42 -1
    Add-Caption $c 'stack count' ($slotColumns[1] + 9) ($y + 24)

    Add-Slot $c $slotColumns[2] $y
    Add-Item $c 'item\diamond_pickaxe.png' ($slotColumns[2] + 1) ($y + 1)
    Add-StackDecorations $c ($slotColumns[2] + 1) ($y + 1) 1 0.35
    Add-Caption $c 'durability 13x2' ($slotColumns[2] + 9) ($y + 24)

    Add-Slot $c $slotColumns[3] $y
    Add-Item $c 'item\bread.png' ($slotColumns[3] + 1) ($y + 1)
    Add-SlotHighlight $c ($slotColumns[3] + 1) ($y + 1)
    Add-Caption $c 'hover' ($slotColumns[3] + 9) ($y + 24)

    Add-Slot $c $slotColumns[4] $y
    Add-Sprite $c 'container/slot/helmet' ($slotColumns[4] + 1) ($y + 1)
    Add-Caption $c 'empty-slot icon' ($slotColumns[4] + 9) ($y + 24)
    $y += 36

    Add-Sprite $c 'container/crafter/disabled_slot' $slotColumns[0] $y
    Add-Caption $c 'disabled' ($slotColumns[0] + 9) ($y + 24)

    Add-Blit $c (Get-BedrockIcon 'slot-red-uncraftable.png') 0 0 18 18 $slotColumns[1] $y 18 18
    Add-Caption $c 'uncraftable' ($slotColumns[1] + 9) ($y + 24)

    Add-Blit $c (Get-BedrockIcon 'slot-output-red.png') 0 0 20 20 ($slotColumns[2] - 1) ($y - 1) 20 20
    Add-Caption $c 'output well' ($slotColumns[2] + 9) ($y + 24)

    Add-Sprite $c 'recipe_book/slot_craftable' ($slotColumns[3] - 3) ($y - 3)
    Add-Caption $c 'recipe 25x25' ($slotColumns[3] + 9) ($y + 24)

    Add-Sprite $c 'recipe_book/slot_uncraftable' ($slotColumns[4] - 3) ($y - 3)
    Add-Caption $c 'recipe, no stock' ($slotColumns[4] + 9) ($y + 24)
    $y += 40

    Add-SectionHeading $c 'HOTBAR AND BARS  hud/hotbar 182x22, selection 24x23 at -1,-1' $left $y
    $y += 18
    Add-Sprite $c 'hud/hotbar' $left $y
    Add-Sprite $c 'hud/hotbar_selection' ($left - 1 + 20 * 2) ($y - 1)
    $hotbarItems = @('item\diamond_pickaxe.png', 'block\cobblestone.png', 'item\bread.png',
                     'item\coal.png', 'item\iron_ingot.png', 'item\apple.png',
                     'item\stick.png', 'item\bone.png', 'item\bucket.png')
    for ($i = 0; $i -lt $hotbarItems.Count; $i++) {
        Add-Item $c $hotbarItems[$i] ($left + 3 + 20 * $i) ($y + 3)
    }
    Add-Sprite $c 'hud/heart/full' ($left + 210) ($y + 2)
    Add-Sprite $c 'hud/heart/half' ($left + 220) ($y + 2)
    Add-Sprite $c 'hud/heart/container' ($left + 230) ($y + 2)
    Add-Sprite $c 'hud/food_full' ($left + 244) ($y + 2)
    Add-Sprite $c 'hud/food_half' ($left + 254) ($y + 2)
    Add-Sprite $c 'hud/armor_full' ($left + 268) ($y + 2)
    Add-Sprite $c 'hud/armor_empty' ($left + 278) ($y + 2)
    Add-Sprite $c 'hud/air' ($left + 292) ($y + 2)
    Add-Sprite $c 'hud/crosshair' ($left + 308) ($y + 1)
    Add-Sprite $c 'hud/experience_bar_background' ($left + 210) ($y + 14) 182 5
    Add-Blit $c (Get-SpriteBitmap 'hud/experience_bar_progress') 0 0 110 5 ($left + 210) ($y + 14) 110 5
    Add-Caption $c 'the nine cells, the selector, the status icons and the 182x5 XP bar' 240 ($y + 26)

    Save-Canvas $c 'ui-mockup-widgets-preview.png'
}

# ------------------------------------------------------------------- the sweep

$builders = [ordered]@{
    mainmenu  = { New-MainMenuMockup }
    settings  = { New-SettingsMockup }
    pause     = { New-PauseMockup }
    hud       = { New-HudMockup }
    inventory = { New-InventoryMockup }
    death     = { New-DeathMockup }
    widgets   = { New-WidgetsMockup }
}

$wanted = $builders.Keys
if ($Only.Count -gt 0) {
    foreach ($name in $Only) {
        if (-not $builders.Contains($name)) { throw "Unknown mock-up '$name'. Known: $($builders.Keys -join ', ')" }
    }
    $wanted = $Only
}

foreach ($name in $wanted) { & $builders[$name] }

foreach ($bitmap in $script:bitmaps.Values) { $bitmap.Dispose() }
foreach ($bitmap in $script:tinted.Values) { $bitmap.Dispose() }
foreach ($bitmap in $script:worldCache.Values) { $bitmap.Dispose() }
$script:wrap.Dispose()
Write-Host ("done - {0} mock-up(s) in {1}" -f $wanted.Count, $OutputDir)
