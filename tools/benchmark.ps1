# Sweeps a setting across values and reports startup and steady-state numbers.
#
# Backs up settings.cfg and always restores it, because the file being measured
# is the same one the game is played with, and leaving a benchmark value behind
# looks exactly like a broken frame limiter.
#
#   .\tools\benchmark.ps1 -Key render_distance -Values 8,16,24

param(
    [string]$Build = "release",
    [string]$Key = "render_distance",
    # A string, not int[]: array arguments are flattened into one token when the
    # script is invoked through `powershell -File`, so 8,16 arrives as 816.
    [string]$Values = "8,16,24",
    [int]$Workers = 11,
    [int]$Seconds = 12
)

$binDir = Join-Path $PSScriptRoot "..\build\$Build\bin"
$exe = Join-Path $binDir "game.exe"
$config = Join-Path $binDir "settings.cfg"
$log = Join-Path $binDir "benchmark.log"

if (-not (Test-Path $exe)) {
    Write-Error "no build at $exe"
    exit 1
}

$backup = $null
if (Test-Path $config) {
    $backup = Get-Content $config -Raw
}

try {
    foreach ($value in ($Values -split ',' | ForEach-Object { $_.Trim() })) {
        # Uncapped, or the frame rate reads as the cap rather than the ceiling.
        $settings = [ordered]@{
            worker_threads   = $Workers
            render_distance  = 8
            frame_cap        = 0
        }
        $settings[$Key] = $value
        Set-Content $config (($settings.Keys | ForEach-Object { "$_=$($settings[$_])" }) -join "`n")

        $proc = Start-Process -FilePath $exe -WorkingDirectory $binDir -PassThru `
            -RedirectStandardOutput $log -RedirectStandardError "$log.err"
        Start-Sleep -Seconds $Seconds

        $ram = 0
        try { $ram = [math]::Round((Get-Process -Id $proc.Id).WorkingSet64 / 1MB) } catch {}

        if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 4 }
        if (-not $proc.HasExited) { $proc.Kill() }

        Write-Host "=== $Key=$value   RAM=${ram}MB ==="
        (Select-String -Path $log -Pattern "Initial load").Line
        (Get-Content $log | Select-String -Pattern " fps \| " | Select-Object -Last 1).Line
    }
}
finally {
    if ($null -ne $backup) {
        Set-Content $config $backup -NoNewline
        Write-Host "restored settings.cfg"
    } else {
        Remove-Item $config -ErrorAction SilentlyContinue
    }
}
