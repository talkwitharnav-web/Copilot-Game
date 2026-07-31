# Launches the game without having to remember where the build puts it.
#
#   .\run.ps1            release build
#   .\run.ps1 debug      debug build, with Vulkan validation layers on
#
# PowerShell does not search the current directory for commands, so the leading
# .\ is required.

param(
    [ValidateSet('release', 'debug')]
    [string]$Config = 'release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root "build\$Config\bin\game.exe"

if (-not (Test-Path $exe)) {
    Write-Host "No $Config build yet at $exe" -ForegroundColor Yellow
    Write-Host "Build it with:" -ForegroundColor Yellow
    Write-Host "  . .\tools\dev-env.ps1; cmake --build --preset $Config"
    exit 1
}

# Started from its own directory because the game resolves assets and saves
# relative to the executable.
Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe)
Write-Host "Launched $Config build."
