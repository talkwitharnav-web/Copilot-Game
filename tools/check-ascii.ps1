# Counts characters a scripted rewrite could have destroyed.
#
# Written after a `Set-Content -Encoding Ascii` pass over a source file: ASCII
# encoding silently turns every character above 127 into a question mark, and
# this codebase deliberately uses em-dashes, degree signs and superscripts in its
# comments. A BOM would be just as bad, so both are reported.
param([Parameter(Mandatory = $true)][string[]]$Path)

$missing = 0
$damaged = 0
foreach ($file in $Path) {
    # **A checker that reports clean for a file it never opened is worse than no
    # checker.** Without this, a typo'd path - or several paths joined with
    # commas into one string, which is what `powershell -File check-ascii.ps1
    # -Path a,b,c` actually delivers - printed
    # "0 bytes, BOM False, NUL 0, non-ASCII 0, U+FFFD 0" and read as a pass. The
    # only tell was the "0 bytes", and it is easy to skim past when everything
    # beside it says green. Measured 2026-08-19: the string
    # "a.cpp,b.cpp,c.cpp" checks nothing and reports clean.
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        Write-Host ("{0}: *** NOT FOUND - NOTHING WAS CHECKED ***" -f $file)
        $missing++
        continue
    }
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
    # **The two damage classes this tool was blind to - both reported, neither
    # judged.**
    #
    # `?` count. A `Set-Content -Encoding Ascii` pass - the exact pass named in
    # this script's own header, which is why this omission mattered - turns
    # every character above 127 into `?`. It leaves BOM False, NUL 0, U+FFFD 0
    # and **non-ASCII 0**, the greenest line this script can print, so an
    # ASCII-ified file read *cleaner than a healthy one*. Measured 2026-08-19
    # on a real source file: 68 characters destroyed, `?` went 60 -> 128, and
    # every other field stayed green.
    #
    # It is printed and never judged, because `?` is legitimate everywhere -
    # ternaries and prose account for 1,225 of them across 165 source files
    # here. There is no bad value, only a bad *change*. Runs are not judged
    # either: the tree's only run of two or more is `Log.cpp`'s `"?????"`, a
    # deliberate five-character placeholder aligned with "WARN " and "ERROR".
    #
    # mojibake. UTF-8 read as Windows-1252 and written back yields *valid* code
    # points, so U+FFFD stays 0 and non-ASCII merely drifts - 68 to 70 reads as
    # "someone added two em-dashes", not as damage. Written as escapes so this
    # file stays pure ASCII and cannot flag itself.
    #
    # Three families, derived from how the misread works rather than copied:
    # a UTF-8 lead byte of 0xC3 becomes `A-tilde` (most accented letters), 0xE2
    # 0x80 becomes `a-circumflex` + euro (the punctuation block - em-dashes,
    # curly quotes, ellipses), and **0xC2 becomes `A-circumflex` followed by
    # whatever 0xA0-0xBF maps to** - which is the range holding the section
    # mark, degree sign, plus-minus and the superscripts this codebase actually
    # uses. `check-encoding.ps1` tests only the 0xA0 case of that third family,
    # so it would miss a round-tripped section mark; the range is a strict
    # superset and costs nothing. Verified 0 hits across 165 source files.
    #
    # Not judged, because `check-encoding.ps1` legitimately carries two of
    # these as documented sentinels, and hardcoding an exception for that one
    # file is the trap already filed as 9319 against it.
    #
    # **A file that should carry non-ASCII and reports zero is the alarm** -
    # that is what these two numbers are for.
    $question = ([regex]::Matches($text, "\?")).Count
    $mojibake = ([regex]::Matches($text, "\u00C3|\u00E2\u20AC|\u00C2[\u00A0-\u00BF]")).Count

    Write-Host ("{0}: {1} bytes, BOM {2}, NUL {3}, non-ASCII {4}, U+FFFD {5}, '?' {6}, mojibake {7}" -f `
        $file, $bytes.Length, $bom, $nul, $high, $replacement, $question, $mojibake)

    # **Damage, as distinct from intent - and only the first sets the code.**
    # A BOM, a NUL byte and a U+FFFD are never legitimate in this tree's
    # sources; the BOM alone corrupted three files on 2026-08-18 and two more on
    # 2026-08-19. A *non-ASCII count is expected*, because comments deliberately
    # carry em-dashes and section marks - `HudPrimitives.cpp` holds three
    # section marks on purpose - so `$high` is reported and never judged.
    #
    # Without this the script printed "BOM True" and **exited 0**, so an agent
    # following this session's "exit code as the primary verdict" rule read a
    # BOM-corrupted file as clean. Measured 2026-08-19 on the live
    # `game/CMakeLists.txt`, whose BOM is a known filed defect: exit was 0.
    # That is the same false green the not-found guard above was written for,
    # one step further in - a checker that opens the file, sees the damage,
    # says so, and still reports success.
    if ($bom -or $nul -gt 0 -or $replacement -gt 0) {
        Write-Host ("{0}: *** DAMAGED - BOM {1}, NUL {2}, U+FFFD {3} ***" -f `
            $file, $bom, $nul, $replacement)
        $damaged++
    }
}

if ($missing -gt 0) {
    Write-Host ("{0} of {1} path(s) were not found and were NOT checked." -f $missing, $Path.Count)
}
if ($damaged -gt 0) {
    Write-Host ("{0} path(s) carry damage a scripted rewrite could have caused." -f $damaged)
}
if ($missing -gt 0 -or $damaged -gt 0) {
    exit 1
}

# **And an explicit success exit, because a caller cannot otherwise tell
# "passed" from "never ran".** PowerShell writes `$LASTEXITCODE` only when a
# script actually exits with a code; falling off the end here left the variable
# holding whatever the *previous* command had set. That collides head-on with
# the `&` invocation the comment above recommends for arrays, and it collides
# worst with the habit of treating the exit code as the verdict. Measured
# 2026-08-19: `$LASTEXITCODE = 99`, then `& check-ascii.ps1 -Path <clean file>`
# came back **99** - never written - while the same call on a missing path
# correctly came back 1. A stale 1 on a clean file is a harmless false red;
# **a stale 0 inherited from any preceding success is a false green, which is
# the exact failure this script was repaired for hours earlier.** One line, and
# the caveat "read the printed line, not the exit code" stops being needed.
exit 0
