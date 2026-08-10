# Runs the game for a fixed time and reports the steady-state frame numbers.
#
# Unlike benchmark.ps1 this does NOT rewrite the whole settings file - it appends
# the keys it wants so the rest of the player's configuration is preserved, and
# it restores the file in a finally so a measurement can never leave a value
# behind. A benchmark once left frame_cap=0 and the limiter was reported broken.
#
#   .\tools\measure-frame.ps1 -Runs 3
#   .\tools\measure-frame.ps1 -Set "detail_distance=8" -Runs 3

param(
    [string]$Build = "release",
    [string[]]$Set = @(),
    [int]$Runs = 3,
    [int]$Seconds = 30,
    [switch]$KeepCap
)

$ErrorActionPreference = 'Stop'

$binDir = Resolve-Path (Join-Path $PSScriptRoot "..\build\$Build\bin")
$exe = Join-Path $binDir "game.exe"
$config = Join-Path $binDir "settings.cfg"
$log = Join-Path $binDir "measure.log"

if (-not (Test-Path $exe)) { Write-Error "no build at $exe"; exit 1 }

Get-Process game -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
Start-Sleep -Milliseconds 800
Get-Process game -ErrorAction SilentlyContinue | Stop-Process -Force

$backup = $null
if (Test-Path $config) { $backup = Get-Content $config -Raw -Encoding UTF8 }

$samples = @()
try {
    # Appended, never substituted: settings.cfg on disk is often partial, so a
    # -replace that sets a test value can silently do nothing. The parser takes
    # the last occurrence of a key.
    $extra = @()
    if (-not $KeepCap) { $extra += "frame_cap=0" }
    $extra += $Set
    $text = if ($null -ne $backup) { $backup.TrimEnd() + "`n" } else { "" }
    $text += ($extra -join "`n") + "`n"
    [System.IO.File]::WriteAllText($config, $text, (New-Object System.Text.UTF8Encoding($false)))

    for ($run = 1; $run -le $Runs; $run++) {
        Remove-Item $log -ErrorAction SilentlyContinue
        $proc = Start-Process -FilePath $exe -WorkingDirectory $binDir -PassThru `
            -RedirectStandardOutput $log -RedirectStandardError "$log.err"
        Start-Sleep -Seconds $Seconds

        $ram = 0
        try { $ram = [math]::Round((Get-Process -Id $proc.Id).WorkingSet64 / 1MB) } catch {}

        if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null }
        $waited = 0
        while (-not $proc.HasExited -and $waited -lt 15) { Start-Sleep -Seconds 1; $waited++ }
        if (-not $proc.HasExited) { $proc.Kill() }
        Start-Sleep -Milliseconds 500

        # The last third of the run only: chunks are still streaming early on and
        # the first lines are not steady state.
        $lines = @(Get-Content $log -ErrorAction SilentlyContinue | Select-String -Pattern " fps \| ")
        if ($lines.Count -lt 6) { Write-Host "run ${run}: too few samples ($($lines.Count))" -ForegroundColor Yellow; continue }
        $tail = $lines[[int]($lines.Count * 2 / 3)..($lines.Count - 1)]
        foreach ($line in $tail) {
            if ($line.Line -match '(\d+) fps .*gpu ([\d.]+) ms \| draws (\d+) \| tris (\d+)') {
                $samples += [pscustomobject]@{
                    Run = $run; Fps = [int]$matches[1]; Gpu = [double]$matches[2]
                    Draws = [int]$matches[3]; Tris = [int]$matches[4]; Ram = $ram
                }
            }
        }
        $last = $lines[-1].Line
        Write-Host "run ${run}: $last" -ForegroundColor DarkGray
    }
}
finally {
    if ($null -ne $backup) {
        [System.IO.File]::WriteAllText($config, $backup, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "restored settings.cfg" -ForegroundColor DarkGray
    }
}

if ($samples.Count -eq 0) { Write-Error "no samples"; exit 1 }

# Minimum GPU time and maximum fps, per the project's own rule: report the best
# run, because background noise can only ever make a frame slower.
$byRun = $samples | Group-Object Run
Write-Host ""
Write-Host ("{0,-6} {1,>6} {2,>9} {3,>8} {4,>11} {5,>7}" -f "run", "fps", "gpu ms", "draws", "tris", "ram") -ForegroundColor Cyan
foreach ($g in $byRun) {
    $f = ($g.Group | Measure-Object Fps -Maximum).Maximum
    $gp = ($g.Group | Measure-Object Gpu -Minimum).Minimum
    $d = [int](($g.Group | Measure-Object Draws -Average).Average)
    $t = [int](($g.Group | Measure-Object Tris -Average).Average)
    $r = ($g.Group | Select-Object -First 1).Ram
    Write-Host ("{0,-6} {1,6} {2,9:F2} {3,8} {4,11} {5,7}" -f $g.Name, $f, $gp, $d, $t, $r)
}
$bestFps = ($samples | Measure-Object Fps -Maximum).Maximum
$bestGpu = ($samples | Measure-Object Gpu -Minimum).Minimum
$avgDraws = [int](($samples | Measure-Object Draws -Average).Average)
$avgTris = [int](($samples | Measure-Object Tris -Average).Average)
Write-Host ""
Write-Host ("BEST   fps {0}   gpu {1:F2} ms   draws {2}   tris {3}" -f $bestFps, $bestGpu, $avgDraws, $avgTris) -ForegroundColor Green
