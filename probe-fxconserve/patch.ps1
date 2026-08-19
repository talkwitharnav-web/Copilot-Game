# Applies a single exact-once string replacement to a file that concurrent
# builds keep locking. Reads and writes with retries; UTF-8, no BOM.
#
#   .\patch.ps1 -Target <file> -OldFile <path> -NewFile <path> [-Tries 400]
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [Parameter(Mandatory = $true)][string]$OldFile,
    [Parameter(Mandatory = $true)][string]$NewFile,
    [int]$Tries = 400
)

$enc = [System.Text.UTF8Encoding]::new($false)
$old = [System.IO.File]::ReadAllText((Resolve-Path $OldFile))
$new = [System.IO.File]::ReadAllText((Resolve-Path $NewFile))
$targetPath = (Resolve-Path $Target).Path

# The payload files are written with a trailing newline by the editor; strip a
# single trailing CRLF/LF from each so the match is exactly what was asked for.
$old = $old -replace "`r`n$", "" -replace "`n$", ""
$new = $new -replace "`r`n$", "" -replace "`n$", ""

$text = $null
for ($i = 0; $i -lt $Tries; $i++) {
    try {
        $text = [System.IO.File]::ReadAllText($targetPath)
        break
    } catch {
        Start-Sleep -Milliseconds 500
    }
}
if ($null -eq $text) { Write-Host "READ FAILED after $Tries tries"; exit 1 }

$count = ([regex]::Matches($text, [regex]::Escape($old))).Count
if ($count -ne 1) { Write-Host "MATCHES=$count (need exactly 1) - nothing written"; exit 1 }

$patched = $text.Replace($old, $new)

for ($i = 0; $i -lt $Tries; $i++) {
    try {
        [System.IO.File]::WriteAllText($targetPath, $patched, $enc)
        Write-Host "OK after $i retries"
        exit 0
    } catch {
        Start-Sleep -Milliseconds 25
    }
}
Write-Host "WRITE FAILED after $Tries tries"
exit 1
