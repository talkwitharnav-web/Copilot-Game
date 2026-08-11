# Rewrites the position and yaw inside a saved player.dat, leaving health, food
# and the rest of the record alone.
#
# `SavedPlayer` is position(12) + yaw(4) + pitch(4) + health(4) + food(4) +
# saturation(4) + exhaustion(4) = 36 bytes, behind a 12-byte header - so x, y
# and z are the floats at offsets 12, 16 and 20, and yaw is at 24. The file is
# 48 bytes and any other length means the format has moved and this script is
# out of date, so it refuses rather than writing into the wrong place.

param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][single]$X,
    [Parameter(Mandatory = $true)][single]$Y,
    [Parameter(Mandatory = $true)][single]$Z,
    [single]$Yaw = 0
)

$full = (Resolve-Path $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($full)
if ($bytes.Length -ne 48) {
    throw "player.dat is $($bytes.Length) bytes, expected 48 - SavedPlayer has changed, do not write into it blind"
}

Copy-Item $full "$full.bak" -Force

[System.BitConverter]::GetBytes($X).CopyTo($bytes, 12)
[System.BitConverter]::GetBytes($Y).CopyTo($bytes, 16)
[System.BitConverter]::GetBytes($Z).CopyTo($bytes, 20)
[System.BitConverter]::GetBytes($Yaw).CopyTo($bytes, 24)
[System.IO.File]::WriteAllBytes($full, $bytes)

$check = [System.IO.File]::ReadAllBytes($full)
Write-Host ("moved player to {0}, {1}, {2} (yaw {3}); previous file kept as player.dat.bak" -f
    [System.BitConverter]::ToSingle($check, 12),
    [System.BitConverter]::ToSingle($check, 16),
    [System.BitConverter]::ToSingle($check, 20),
    [System.BitConverter]::ToSingle($check, 24))
