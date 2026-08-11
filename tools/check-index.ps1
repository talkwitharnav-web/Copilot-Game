# Proves the generated indexes point where they say: for every entry in every
# document's index block, the line it names must be the heading it names.
#
# Written because an index of line numbers is the project's own commonest bug
# shape - a value kept somewhere other than the thing that owns it - and a
# generator is only worth trusting if something checks its output.

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$checked = 0
$bad = 0

foreach ($file in Get-ChildItem -Path $root -Filter *.md -File | Sort-Object Name) {
    $lines = @(Get-Content -Encoding UTF8 $file.FullName)
    $inBlock = $false
    foreach ($line in $lines) {
        if ($line -eq '<!-- INDEX -->') { $inBlock = $true; continue }
        if ($line -eq '<!-- /INDEX -->') { $inBlock = $false; continue }
        if (-not $inBlock) { continue }
        if ($line -notmatch '^\s*- \*\*(\d+)\*\* &middot; (.+)$') { continue }
        $row = [int]$Matches[1]
        $text = $Matches[2]
        $checked++
        $actual = if ($row -ge 1 -and $row -le $lines.Count) { $lines[$row - 1] } else { '<past end of file>' }
        if ($actual -notmatch ('^#{1,6} ' + [regex]::Escape($text) + '$')) {
            $bad++
            Write-Host ("{0}:{1} says '{2}' but line reads '{3}'" -f $file.Name, $row, $text, $actual) -ForegroundColor Red
        }
    }
}

Write-Host ("{0} index entries checked, {1} wrong" -f $checked, $bad)
if ($bad -gt 0) { exit 1 }
