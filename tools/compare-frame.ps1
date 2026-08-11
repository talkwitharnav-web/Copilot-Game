# Runs the game across several settings and prints one table.
#
# Wraps measure-frame.ps1 so an A/B is one command and settings.cfg is restored
# by each leg's own finally. Report the MINIMUM gpu time and the MAXIMUM frame
# rate: background noise can only ever make a frame slower.
#
#   .\tools\compare-frame.ps1

param(
    [string]$Build = "release",
    [int]$Runs = 3,
    [int]$Seconds = 22
)

$ErrorActionPreference = 'Stop'

$cases = @(
    @{ Name = "rd6   detail off";  Set = @("render_distance=6",  "detail_distance=6")  },
    @{ Name = "rd6   detail 4";    Set = @("render_distance=6",  "detail_distance=4")  },
    @{ Name = "rd12  detail off";  Set = @("render_distance=12", "detail_distance=12") },
    @{ Name = "rd12  detail 8";    Set = @("render_distance=12", "detail_distance=8")  },
    @{ Name = "rd16  detail off";  Set = @("render_distance=16", "detail_distance=16") },
    @{ Name = "rd16  detail 8";    Set = @("render_distance=16", "detail_distance=8")  }
)

$results = @()
foreach ($case in $cases) {
    Write-Host ""
    Write-Host "=== $($case.Name) ===" -ForegroundColor Cyan
    $summary = & (Join-Path $PSScriptRoot "measure-frame.ps1") -Build $Build -Runs $Runs -Seconds $Seconds -Set $case.Set
    if ($null -ne $summary) {
        $results += [pscustomobject]@{
            Case = $case.Name; Fps = $summary.Fps; Gpu = $summary.Gpu
            Draws = $summary.Draws; Tris = $summary.Tris
        }
    } else {
        Write-Host "no result for $($case.Name)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host ("{0,-18} {1,6} {2,9} {3,8} {4,11}" -f "case", "fps", "gpu ms", "draws", "tris") -ForegroundColor Cyan
foreach ($r in $results) {
    Write-Host ("{0,-18} {1,6} {2,9:F2} {3,8} {4,11}" -f $r.Case, $r.Fps, $r.Gpu, $r.Draws, $r.Tris)
}
