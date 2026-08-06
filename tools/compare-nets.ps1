# Decides whether two creature skins share a model, by comparing the opaque run
# boundaries on every row.
#
#   powershell -NoProfile -File tools\compare-nets.ps1 -A <png> -B <png>
#
# Why boundaries and not pixels: a whole-sheet alpha diff produces FALSE
# NEGATIVES on rig sharing. The Blackbone, stray and bogged all differ from the
# skeleton in their interior pixels - ribcage holes, eye sockets, mushrooms -
# and share its net exactly. What decides the model is where each net STARTS and
# ENDS, so that is what this compares.
#
# It also prints every row, never a truncated head. The villager and zombie
# villager are identical for 33 rows and differ only in where the arm nets stop;
# a listing cut short said "same model" with total confidence and shipped a
# zombie villager whose arms were a third too short.

param(
    [Parameter(Mandatory = $true)][string]$A,
    [Parameter(Mandatory = $true)][string]$B,
    [switch]$All
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Get-RowRuns([System.Drawing.Bitmap]$bitmap, [int]$y) {
    $runs = @()
    $start = -1
    for ($x = 0; $x -lt $bitmap.Width; $x++) {
        $opaque = $bitmap.GetPixel($x, $y).A -gt 0
        if ($opaque -and $start -lt 0) { $start = $x }
        if (-not $opaque -and $start -ge 0) {
            $runs += "$start-$($x - 1)"
            $start = -1
        }
    }
    if ($start -ge 0) { $runs += "$start-$($bitmap.Width - 1)" }
    $runs
}

$first = [System.Drawing.Bitmap]::FromFile((Resolve-Path $A).Path)
$second = [System.Drawing.Bitmap]::FromFile((Resolve-Path $B).Path)

Write-Host ("A {0}  {1}x{2}" -f (Split-Path -Leaf $A), $first.Width, $first.Height)
Write-Host ("B {0}  {1}x{2}" -f (Split-Path -Leaf $B), $second.Width, $second.Height)

if ($first.Width -ne $second.Width -or $first.Height -ne $second.Height) {
    Write-Host "VERDICT: different sheet size - these cannot share a net." -ForegroundColor Red
    $first.Dispose(); $second.Dispose()
    return
}

$differing = 0
for ($y = 0; $y -lt $first.Height; $y++) {
    $ra = (Get-RowRuns $first $y) -join ' '
    $rb = (Get-RowRuns $second $y) -join ' '
    if ($ra -ne $rb) {
        $differing++
        Write-Host ("row {0,3} A: {1}" -f $y, $ra) -ForegroundColor Yellow
        Write-Host ("row {0,3} B: {1}" -f $y, $rb) -ForegroundColor Yellow
    } elseif ($All) {
        Write-Host ("row {0,3}    {1}" -f $y, $ra)
    }
}

$first.Dispose()
$second.Dispose()

if ($differing -eq 0) {
    Write-Host "VERDICT: identical net boundaries on every row - one model, two skins." -ForegroundColor Green
} else {
    Write-Host "VERDICT: $differing of $($first.Height) rows differ - read them before assuming a shared rig." -ForegroundColor Red
}
