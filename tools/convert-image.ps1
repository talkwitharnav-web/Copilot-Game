# Converts an image Windows can decode (including WebP) into a PNG, optionally
# cropping a region and scaling it down by an integer factor.
#
# Exists because neither stb_image nor System.Drawing reads WebP, and reference
# art often arrives upscaled.
#
#   powershell -File tools\convert-image.ps1 -Input in.webp -Output out.png
#   powershell -File tools\convert-image.ps1 -Input in.webp -Output out.png -X 0 -Y 0 -Width 182 -Height 22 -Divide 8

param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [int]$X = -1,
    [int]$Y = -1,
    [int]$Width = -1,
    [int]$Height = -1,
    [int]$Divide = 1
)

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

$uri = New-Object System.Uri((Resolve-Path $InputPath).Path)
$source = New-Object System.Windows.Media.Imaging.BitmapImage
$source.BeginInit()
$source.UriSource = $uri
$source.CacheOption = [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
$source.EndInit()

$image = [System.Windows.Media.Imaging.BitmapFrame]::Create($source)

if ($X -ge 0 -and $Y -ge 0 -and $Width -gt 0 -and $Height -gt 0) {
    $rect = New-Object System.Windows.Int32Rect $X, $Y, $Width, $Height
    $image = New-Object System.Windows.Media.Imaging.CroppedBitmap $image, $rect
}

if ($Divide -gt 1) {
    # Nearest neighbour: reference art is usually an integer upscale of pixel
    # art, and anything smoother destroys the pixel grid being recovered.
    $scale = New-Object System.Windows.Media.ScaleTransform (1.0 / $Divide), (1.0 / $Divide)
    $image = New-Object System.Windows.Media.Imaging.TransformedBitmap $image, $scale
}

$encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
$encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($image))

$stream = [System.IO.File]::Create((Join-Path (Get-Location) $OutputPath))
$encoder.Save($stream)
$stream.Close()

Write-Host ("wrote {0} ({1}x{2})" -f $OutputPath, $image.PixelWidth, $image.PixelHeight)
