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

$width = [Math]::Max($widgetImage.Width, $inventoryImage.Width)
$height = $widgetImage.Height + $inventoryImage.Height

$sheet = New-Object System.Drawing.Bitmap -ArgumentList ([int]$width), ([int]$height),
    ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$g.InterpolationMode = 'NearestNeighbor'
$g.PixelOffsetMode = 'Half'
$g.DrawImage($widgetImage, 0, 0, $widgetImage.Width, $widgetImage.Height)
$g.DrawImage($inventoryImage, 0, $widgetImage.Height, $inventoryImage.Width, $inventoryImage.Height)
$g.Dispose()

$sheet.Save((Join-Path $root $Output), [System.Drawing.Imaging.ImageFormat]::Png)

Write-Host "wrote $Output ($width x $height)"
Write-Host "  widgets   at (0, 0) size $($widgetImage.Width) x $($widgetImage.Height)"
Write-Host "  inventory at (0, $($widgetImage.Height)) size $nativeWidth x $nativeHeight"
Write-Host "  character box painted out: $($CharacterBox -join ', ')"

$sheet.Dispose()
$widgetImage.Dispose()
$sourceImage.Dispose()
$inventoryImage.Dispose()
