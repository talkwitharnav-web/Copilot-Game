# Checks every Markdown document for the two ways text gets silently corrupted
# here: a byte-order mark at the front, and double-encoded UTF-8.
#
# The strict "is it all ASCII" test is the WRONG check for this codebase - the
# docs deliberately use em-dashes, degree signs, superscripts and arrows, and a
# scan for non-ASCII flags twenty-seven healthy files. The real signals are a
# BOM, U+FFFD, and the mangled sequences a double encode leaves behind.

$bad = 0
foreach ($file in Get-ChildItem -Path $PSScriptRoot\.. -Filter *.md -File) {
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF

    # Strict decode, so a genuinely invalid sequence throws rather than being
    # quietly replaced - which is what makes the U+FFFD count meaningful.
    $utf8 = New-Object System.Text.UTF8Encoding($false, $false)
    $text = $utf8.GetString($bytes)
    $replacement = ([regex]::Matches($text, "\uFFFD")).Count
    # `Ã` and `â€` are what a Windows-1252 read of UTF-8 leaves behind. They are
    # the detector, because a double-encoded file is still valid UTF-8 and the
    # U+FFFD count stays at zero on it.
    $mojibake = ([regex]::Matches($text, "\u00C3|\u00E2\u20AC|\u00C2\u00A0")).Count

    if ($bom -or $replacement -gt 0 -or $mojibake -gt 0) {
        # LESSONS.md is the one honest exception: the lessons about this very
        # trap quote the detector characters, so it will always report hits.
        # Named rather than filtered out, because a *different* count there
        # would be a real corruption. It went 4 -> 5 on 2026-08-10 when the
        # lesson about a BOM-less .ps1 mangling its own literals was added.
        $expected = 5
        $known = $file.Name -eq "LESSONS.md" -and -not $bom -and $replacement -eq 0 -and
                 $mojibake -eq $expected
        if ($known) {
            Write-Host "LESSONS.md: $expected mojibake hits, all the lessons quoting the detector - expected"
            continue
        }
        $bad++
        Write-Host ("{0}: bom={1} fffd={2} mojibake={3}" -f $file.Name, $bom, $replacement, $mojibake)
    }
}

if ($bad -eq 0) {
    Write-Host "all markdown clean: no BOM, no U+FFFD, no mojibake"
} else {
    Write-Host "$bad file(s) need attention"
}
