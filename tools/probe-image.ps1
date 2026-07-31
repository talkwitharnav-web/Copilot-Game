# Reports where content starts and stops along a row or column, which is how the
# native pixel grid of an upscaled reference image gets recovered.
#
#   powershell -File tools\probe-image.ps1 -InputPath img.png -Row 200

param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [int]$Row = -1,
    [int]$Column = -1
)

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

Write-Host ("image {0}x{1}" -f $width, $height)

function Get-Pixel {
    param([int]$x, [int]$y)
    $i = $y * $stride + $x * 4
    return @($pixels[$i + 2], $pixels[$i + 1], $pixels[$i], $pixels[$i + 3])
}

if ($Row -ge 0) {
    $runs = @()
    $previous = $null
    $start = 0
    for ($x = 0; $x -lt $width; $x++) {
        $p = (Get-Pixel -x $x -y $Row) -join ','
        if ($p -ne $previous) {
            if ($null -ne $previous) { $runs += ("{0}..{1} [{2}] len {3}" -f $start, ($x - 1), $previous, ($x - $start)) }
            $previous = $p
            $start = $x
        }
    }
    $runs += ("{0}..{1} [{2}] len {3}" -f $start, ($width - 1), $previous, ($width - $start))
    Write-Host "row $Row runs:"
    $runs | ForEach-Object { Write-Host "  $_" }
}

if ($Column -ge 0) {
    $runs = @()
    $previous = $null
    $start = 0
    for ($y = 0; $y -lt $height; $y++) {
        $p = (Get-Pixel -x $Column -y $y) -join ','
        if ($p -ne $previous) {
            if ($null -ne $previous) { $runs += ("{0}..{1} [{2}] len {3}" -f $start, ($y - 1), $previous, ($y - $start)) }
            $previous = $p
            $start = $y
        }
    }
    $runs += ("{0}..{1} [{2}] len {3}" -f $start, ($height - 1), $previous, ($height - $start))
    Write-Host "column $Column runs:"
    $runs | ForEach-Object { Write-Host "  $_" }
}
