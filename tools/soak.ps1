# Runs the debug build for a while and reports what validation had to say.
#
# The acceptance bar for this project is zero Vulkan validation errors and an
# empty stderr. Both are checked here rather than eyeballed, because a soak that
# is only looked at proves nothing.
#
# Backs up settings.cfg AND the save, and restores both in the same command's
# finally - a soak once left a setting behind and the user filed a bug against a
# system that was working.
#
#   .\tools\soak.ps1 -Seconds 45
#   .\tools\soak.ps1 -Seconds 45 -Set "detail_distance=4","render_distance=8"

param(
    [string]$Build = "debug",
    [int]$Seconds = 45,
    [string[]]$Set = @()
)

$ErrorActionPreference = 'Stop'

$binDir = Resolve-Path (Join-Path $PSScriptRoot "..\build\$Build\bin")
$exe = Join-Path $binDir "game.exe"
$config = Join-Path $binDir "settings.cfg"
$saves = Join-Path $binDir "saves"
$log = Join-Path $binDir "soak.log"
$errLog = "$log.err"
$saveBackup = Join-Path $env:TEMP ("voxel-soak-saves-" + [guid]::NewGuid().ToString("N"))

if (-not (Test-Path $exe)) { Write-Error "no build at $exe"; exit 1 }

Get-Process game -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
Start-Sleep -Milliseconds 800
Get-Process game -ErrorAction SilentlyContinue | Stop-Process -Force

$backup = $null
if (Test-Path $config) { $backup = Get-Content $config -Raw -Encoding UTF8 }
if (Test-Path $saves) { Copy-Item $saves $saveBackup -Recurse -Force }

try {
    if ($Set.Count -gt 0) {
        # Appended: settings.cfg on disk is often partial, so replacing a key
        # that is not there silently does nothing. The parser takes the last
        # occurrence, so appending always wins.
        $text = if ($null -ne $backup) { $backup.TrimEnd() + "`n" } else { "" }
        $text += ($Set -join "`n") + "`n"
        [System.IO.File]::WriteAllText($config, $text, (New-Object System.Text.UTF8Encoding($false)))
    }

    Remove-Item $log, $errLog -ErrorAction SilentlyContinue
    $proc = Start-Process -FilePath $exe -WorkingDirectory $binDir -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError $errLog
    Start-Sleep -Seconds $Seconds

    # CloseMainWindow, never a force kill: the save path runs after the window
    # closes, and killing it is how a world loses whatever was built that run.
    if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null }
    $waited = 0
    while (-not $proc.HasExited -and $waited -lt 20) { Start-Sleep -Seconds 1; $waited++ }
    if (-not $proc.HasExited) { $proc.Kill(); Write-Host "had to kill it" -ForegroundColor Red }
    Start-Sleep -Milliseconds 500
}
finally {
    if ($null -ne $backup) {
        [System.IO.File]::WriteAllText($config, $backup, (New-Object System.Text.UTF8Encoding($false)))
    }
    if (Test-Path $saveBackup) {
        Remove-Item $saves -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item $saveBackup $saves -Recurse -Force
        Remove-Item $saveBackup -Recurse -Force
    }
    Write-Host "restored settings.cfg and saves/" -ForegroundColor DarkGray
}

$errBytes = 0
if (Test-Path $errLog) { $errBytes = (Get-Item $errLog).Length }
# "validation layers ON" is the startup line saying they are enabled, not a
# finding. Matching it would make a healthy run look like a failing one.
$errors = @(Get-Content $log -ErrorAction SilentlyContinue |
            Select-String -Pattern "\[ERROR\]|VUID-|Validation Error" |
            Where-Object { $_.Line -notmatch "validation layers ON" })
$warnings = @(Get-Content $log -ErrorAction SilentlyContinue | Select-String -Pattern "\[WARN \]|\[WARN\]")
$frames = @(Get-Content $log -ErrorAction SilentlyContinue | Select-String -Pattern " fps \| ")

Write-Host ""
Write-Host "=== soak $Build ${Seconds}s ===" -ForegroundColor Cyan
Write-Host ("stderr bytes   : {0}" -f $errBytes)   -ForegroundColor $(if ($errBytes -eq 0) { "Green" } else { "Red" })
Write-Host ("error lines    : {0}" -f $errors.Count) -ForegroundColor $(if ($errors.Count -eq 0) { "Green" } else { "Red" })
Write-Host ("warning lines  : {0}" -f $warnings.Count) -ForegroundColor $(if ($warnings.Count -eq 0) { "Green" } else { "Yellow" })
Write-Host ("frame reports  : {0}" -f $frames.Count)
if ($errors.Count -gt 0) { $errors | Select-Object -First 20 | ForEach-Object { Write-Host $_.Line -ForegroundColor Red } }
if ($warnings.Count -gt 0) { $warnings | Select-Object -First 20 | ForEach-Object { Write-Host $_.Line -ForegroundColor Yellow } }
if ($frames.Count -gt 0) { Write-Host $frames[-1].Line -ForegroundColor DarkGray }
if ($errBytes -gt 0) { Get-Content $errLog | Select-Object -First 20 | ForEach-Object { Write-Host $_ -ForegroundColor Red } }
