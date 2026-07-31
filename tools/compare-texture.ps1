# Compares one of our textures against its reference counterpart by measurement,
# not by eye. Prints palette size, luminance spread, saturation and run lengths.
#
# Usage: .\tools\compare-texture.ps1 stone stone
#        .\tools\compare-texture.ps1 planks oak_planks

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)] [string] $Ours,
    [Parameter(Mandatory, Position = 1)] [string] $Reference
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$refRoot = Join-Path $root 'reference\minecraft-assets-26.2\minecraft-assets-26.2\assets\minecraft\textures'

function Get-TextureStats([string] $path, [string] $label) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    $w = $bmp.Width
    $h = $bmp.Height

    # Flat arrays indexed y*w+x. PowerShell's handling of 2D arrays across
    # function boundaries is unreliable, so keep everything one-dimensional.
    $r = New-Object 'int[]' ($w * $h)
    $g = New-Object 'int[]' ($w * $h)
    $b = New-Object 'int[]' ($w * $h)
    $a = New-Object 'int[]' ($w * $h)

    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $i = $y * $w + $x
            $r[$i] = $c.R; $g[$i] = $c.G; $b[$i] = $c.B; $a[$i] = $c.A
        }
    }
    $bmp.Dispose()

    $colours = @{}
    $lums = New-Object 'System.Collections.Generic.List[double]'
    $sats = New-Object 'System.Collections.Generic.List[double]'
    $clear = 0
    $partial = 0

    for ($i = 0; $i -lt ($w * $h); $i++) {
        if ($a[$i] -lt 8) { $clear++; continue }
        if ($a[$i] -lt 250) { $partial++ }

        $colours["$($r[$i]),$($g[$i]),$($b[$i])"] = $true
        $lums.Add(0.2126 * $r[$i] + 0.7152 * $g[$i] + 0.0722 * $b[$i])

        $mx = [math]::Max($r[$i], [math]::Max($g[$i], $b[$i]))
        $mn = [math]::Min($r[$i], [math]::Min($g[$i], $b[$i]))
        if ($mx -eq 0) { $sats.Add(0.0) } else { $sats.Add(($mx - $mn) / [double]$mx) }
    }

    if ($lums.Count -eq 0) { throw "$label has no visible pixels" }

    $mean = 0.0
    foreach ($v in $lums) { $mean += $v }
    $mean = $mean / $lums.Count

    $var = 0.0
    foreach ($v in $lums) { $var += ($v - $mean) * ($v - $mean) }
    $sd = [math]::Sqrt($var / $lums.Count)

    $satMean = 0.0
    foreach ($v in $sats) { $satMean += $v }
    $satMean = $satMean / $sats.Count

    $lmin = [double]::MaxValue
    $lmax = 0.0
    foreach ($v in $lums) {
        if ($v -lt $lmin) { $lmin = $v }
        if ($v -gt $lmax) { $lmax = $v }
    }

    # Run lengths: how far an identical colour repeats. Short runs mean noisy
    # per-pixel detail; long runs mean flat bands, which read as planks.
    $hRuns = New-Object 'System.Collections.Generic.List[int]'
    for ($y = 0; $y -lt $h; $y++) {
        $len = 1
        for ($x = 1; $x -lt $w; $x++) {
            $p = $y * $w + $x - 1
            $q = $y * $w + $x
            if ($r[$p] -eq $r[$q] -and $g[$p] -eq $g[$q] -and $b[$p] -eq $b[$q]) { $len++ }
            else { $hRuns.Add($len); $len = 1 }
        }
        $hRuns.Add($len)
    }

    $vRuns = New-Object 'System.Collections.Generic.List[int]'
    for ($x = 0; $x -lt $w; $x++) {
        $len = 1
        for ($y = 1; $y -lt $h; $y++) {
            $p = ($y - 1) * $w + $x
            $q = $y * $w + $x
            if ($r[$p] -eq $r[$q] -and $g[$p] -eq $g[$q] -and $b[$p] -eq $b[$q]) { $len++ }
            else { $vRuns.Add($len); $len = 1 }
        }
        $vRuns.Add($len)
    }

    $h1 = 0
    $hMax = 0
    foreach ($v in $hRuns) {
        if ($v -eq 1) { $h1++ }
        if ($v -gt $hMax) { $hMax = $v }
    }
    $v1 = 0
    $vMax = 0
    foreach ($v in $vRuns) {
        if ($v -eq 1) { $v1++ }
        if ($v -gt $vMax) { $vMax = $v }
    }

    [pscustomobject]@{
        Which  = $label
        Size   = "$($w)x$($h)"
        Cols   = $colours.Count
        Lum    = "$([math]::Round($lmin,0))-$([math]::Round($lmax,0))"
        Span   = [math]::Round($lmax - $lmin, 0)
        Mean   = [math]::Round($mean, 1)
        SD     = [math]::Round($sd, 1)
        'Sat%' = [math]::Round($satMean * 100, 1)
        HMax   = $hMax
        'H1%'  = [math]::Round(100.0 * $h1 / $hRuns.Count, 0)
        VMax   = $vMax
        'V1%'  = [math]::Round(100.0 * $v1 / $vRuns.Count, 0)
        Cutout = $clear
        Blend  = $partial
    }
}

$oursPath = Join-Path $root "assets\textures\blocks\$Ours.png"
$refBlock = Join-Path $refRoot "block\$Reference.png"
$refItem = Join-Path $refRoot "item\$Reference.png"
$refPath = if (Test-Path $refBlock) { $refBlock } else { $refItem }

if (-not (Test-Path $oursPath)) { throw "Missing ours: $oursPath" }
if (-not (Test-Path $refPath)) { throw "Missing reference: $refPath" }

Get-TextureStats $refPath "REF  $Reference"
Get-TextureStats $oursPath "OURS $Ours"
