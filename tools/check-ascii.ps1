# Counts characters a scripted rewrite could have destroyed.
#
# Written after a `Set-Content -Encoding Ascii` pass over a source file: ASCII
# encoding silently turns every character above 127 into a question mark, and
# this codebase deliberately uses em-dashes, degree signs and superscripts in its
# comments. A BOM would be just as bad, so both are reported.
param([Parameter(Mandatory = $true)][string[]]$Path)

foreach ($file in $Path) {
    $bytes = [System.IO.File]::ReadAllBytes($file)
    $bom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $nul = 0
    foreach ($b in $bytes) {
        if ($b -eq 0) { $nul++ }
    }
    $text = [System.IO.File]::ReadAllText($file)
    $high = 0
    $replacement = 0
    foreach ($c in $text.ToCharArray()) {
        $code = [int]$c
        if ($code -gt 127) { $high++ }
        if ($code -eq 0xFFFD) { $replacement++ }
    }
    Write-Host ("{0}: {1} bytes, BOM {2}, NUL {3}, non-ASCII {4}, U+FFFD {5}" -f `
        $file, $bytes.Length, $bom, $nul, $high, $replacement)
}
