# Prints a texture as a character grid, one character per distinct colour, plus
# a legend of RGB values and how often each appears.
#
# Adjectives do not settle pixel-art questions and eyeballing a magnified sheet
# has now missed two separate faults - stripes that read as planks, and corners
# that rounded into a circle. A grid shows exactly which pixel is which.
#
#   .\tools\dump-pixels.ps1 -InputPath reference\...\crafting_table_top.png

param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    # Characters are assigned darkest first, so the legend reads as a ramp.
    [string]$Glyphs = '.:-=+*#%@ABCDEFGHIJKLMNOPQRSTUVWXYZ'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

$uri = New-Object System.Uri((Resolve-Path $InputPath).Path)
$source = New-Object System.Windows.Media.Imaging.BitmapImage
$source.BeginInit()
$source.UriSource = $uri
$source.CacheOption = [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
$source.EndInit()

$converted = New-Object System.Windows.Media.Imaging.FormatConvertedBitmap $source, ([System.Windows.Media.PixelFormats]::Bgra32), $null, 0
$width = $converted.PixelWidth
$height = $converted.PixelHeight
$stride = $width * 4
$pixels = New-Object 'byte[]' ($stride * $height)
$converted.CopyPixels($pixels, $stride, 0)

$counts = @{}
$grid = New-Object 'string[,]' $height, $width

for ($y = 0; $y -lt $height; $y++) {
    for ($x = 0; $x -lt $width; $x++) {
        $i = $y * $stride + $x * 4
        # A fully transparent pixel has no meaningful colour, so it is folded
        # into one key rather than however many stale RGB values it carries.
        $key = if ($pixels[$i + 3] -eq 0) { 'none' } else { '{0},{1},{2}' -f $pixels[$i + 2], $pixels[$i + 1], $pixels[$i] }
        $grid[$y, $x] = $key
        if ($counts.ContainsKey($key)) { $counts[$key]++ } else { $counts[$key] = 1 }
    }
}

$luminance = {
    param($key)
    if ($key -eq 'none') { return -1 }
    $c = $key -split ','
    return 0.299 * [int]$c[0] + 0.587 * [int]$c[1] + 0.114 * [int]$c[2]
}

$ordered = $counts.Keys | Sort-Object { & $luminance $_ }
$glyphOf = @{}
for ($i = 0; $i -lt $ordered.Count; $i++) {
    $glyphOf[$ordered[$i]] = if ($i -lt $Glyphs.Length) { $Glyphs[$i] } else { '?' }
}

Write-Host ("{0}  {1}x{2}, {3} distinct colours" -f (Split-Path -Leaf $InputPath), $width, $height, $ordered.Count)
Write-Host ''
Write-Host ('    ' + (0..($width - 1) | ForEach-Object { '{0:x}' -f ($_ % 16) }) -join '')

for ($y = 0; $y -lt $height; $y++) {
    $row = ''
    for ($x = 0; $x -lt $width; $x++) { $row += $glyphOf[$grid[$y, $x]] }
    Write-Host ('{0,3} {1}' -f $y, $row)
}

Write-Host ''
foreach ($key in $ordered) {
    Write-Host ('  {0}  {1,-15} lum {2,5:N0}  x{3}' -f $glyphOf[$key], $key, (& $luminance $key), $counts[$key])
}
