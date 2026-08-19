# Citation sweep. Three layers of self-check, each added after an instrument
# failure was reported by another agent tonight:
#
#   1. Planted positives  - a zero from a test that cannot produce a one is not
#                           evidence. (fx-store)
#   2. DISTINCT expected counts - "the control fired" is not the test; the
#                           control must differ from the claim in the expected
#                           DIRECTION and by a plausible MAGNITUDE. A uniform
#                           result is the instrument, not the world. (fx-world)
#   3. Agreement across line endings - the same text in CRLF and in bare LF must
#                           give the same answer, because a mixed file silently
#                           SHRINKS a newline-spanning search and misses rather
#                           than failing loudly. (fx-draw, fx-world)
#
# Layer 3 needs its own proof: showing that a pattern agrees is worthless unless
# some pattern in the run DISAGREES, or the agreement may simply mean nothing in
# the text could have distinguished the two forms. So two deliberately
# ending-sensitive patterns are carried below and are REQUIRED to disagree.
#
# Every regex result is wrapped in @() - a function returning exactly ONE
# [pscustomobject] unrolls to a scalar whose .Count is the empty string.

param(
    [string]$Target  = "C:\Users\arnav\Downloads\Copilot Game\game\src\Main.cpp",
    [string]$Control = "C:\Users\arnav\Downloads\Copilot Game\probe-fxconserve\citecontrol.txt"
)

$patterns = @(
    @{ name = "1 prose 'line NNN'";     expect = 3; rx = '(?im)\bline\s+\d{2,4}\b' },
    @{ name = "2 bare File.hpp:NNN";    expect = 2; rx = '(?im)\b[A-Za-z_]+\.(?:hpp|cpp|h|ps1|md)\s*:\s*\d{1,5}\b' },
    @{ name = "3 split across a break"; expect = 2; rx = '(?ims)\b[A-Za-z_]+\.(?:hpp|cpp|h)\s*\r?\n\s*(?://|///)?\s*:\s*\d{1,5}\b' },
    @{ name = "4 'lines N-M' range";    expect = 1; rx = '(?im)\blines\s+\d{1,5}\s*[-\u2013]\s*\d{1,5}\b' }
)

# Required to be ending-SENSITIVE. If either of these ever agrees across the two
# forms, layer 3 has stopped being able to detect the hazard it exists for and
# the agreement results above it mean nothing.
$sensitive = @(
    @{ name = "S1 naive bare \n, no \s*"; rx = '(?is)[A-Za-z_]+\.(?:hpp|cpp|h)\n(?://)?\s*:\s*\d{1,5}' },
    @{ name = "S2 literal \r\n only";     rx = '(?is)[A-Za-z_]+\.(?:hpp|cpp|h)\r\n(?://)?\s*:\s*\d{1,5}' }
)

$l1 = [System.Text.Encoding]::GetEncoding(28591)
function LoadRaw([string]$p) { return $l1.GetString([System.IO.File]::ReadAllBytes($p)) }
function AsCrlf([string]$s)  { return [regex]::Replace($s, "\r\n|\n|\r", "`r`n") }
function AsLf([string]$s)    { return [regex]::Replace($s, "\r\n|\n|\r", "`n") }
function Count([string]$s, [string]$rx) { return @([regex]::Matches($s, $rx)).Count }

$targetRaw  = LoadRaw $Target
$controlRaw = LoadRaw $Control

$tCrlf = AsCrlf $targetRaw;  $tLf = AsLf $targetRaw
$cCrlf = AsCrlf $controlRaw; $cLf = AsLf $controlRaw

$mtime = (Get-Item $Target).LastWriteTime
$bareLf = (Count $targetRaw "\n") - (Count $targetRaw "\r\n")
Write-Host ("target  : {0}" -f $Target)
Write-Host ("mtime   : {0}   (a claim without a timestamp is unfalsifiable)" -f $mtime)
Write-Host ("endings : {0} CRLF, {1} bare LF" -f (Count $targetRaw "\r\n"), $bareLf)
Write-Host ""

Write-Host "LAYER 0 - is any pattern AUTHORED with a literal newline? (the read-side form)"
Write-Host "A pattern string that carries a real 0x0A scores ~0 against a CRLF tree and reads"
Write-Host "as a clean sweep. This is decided by inspecting the pattern BYTES, not by eye."
Write-Host ""

# .NET's own answer to the claim the immunity rests on, rather than trusting a report.
$sMatchesCr = [regex]::IsMatch("`r", "\s")
Write-Host ("  does \s match a CR in .NET?  {0}   (this is why \s* around a newline is immune)" -f $sMatchesCr)

# The planted positive. Built from a here-string, which is the authentic mechanism
# by which a pattern acquires a literal newline. Without this row, "0 at risk"
# would be a zero from a detector never shown capable of returning one.
$hereBornePattern = @"
[A-Za-z_]+\.hpp
:\s*\d{1,5}
"@

$audit = @()
foreach ($p in $patterns)  { $audit += @{ name = $p.name; rx = $p.rx } }
foreach ($s in $sensitive) { $audit += @{ name = $s.name; rx = $s.rx } }
$audit += @{ name = "P! here-string borne (control)"; rx = $hereBornePattern }

Write-Host ("`n  {0,-30} {1,9} {2,9} {3,9}   verdict" -f "pattern", "raw LF", "raw CR", "escaped")
Write-Host ("  " + ("-" * 82))
$atRisk = 0
foreach ($a in $audit) {
    $rawLf   = @([regex]::Matches($a.rx, "\x0A")).Count
    $rawCr   = @([regex]::Matches($a.rx, "\x0D")).Count
    $escaped = @([regex]::Matches($a.rx, [regex]::Escape('\n'))).Count
    $guarded = [regex]::IsMatch($a.rx, [regex]::Escape('\s*') + '(?:' + [regex]::Escape('\r?') + ')?' + [regex]::Escape('\n'))
    $risky   = ($rawLf -gt 0) -or ($rawCr -gt 0)
    if ($risky) { $atRisk++ }
    $verdict = if ($risky)      { "AT RISK - carries a real newline byte" }
               elseif ($guarded){ "immune - escaped, and guarded by \s*" }
               elseif ($escaped -gt 0) { "immune - escaped \n, no raw byte" }
               else             { "immune by construction - no newline at all" }
    Write-Host ("  {0,-30} {1,9} {2,9} {3,9}   {4}" -f $a.name, $rawLf, $rawCr, $escaped, $verdict)
}
Write-Host ("`n  patterns carrying a raw newline byte : {0}  (1 is the planted control)" -f $atRisk)
if ($atRisk -lt 1) {
    Write-Host "`n  refusing to report: the detector never returned a positive, so its zero on the"
    Write-Host "  real patterns is untested. The here-string control should have been flagged."
    exit 1
}
Write-Host ""

Write-Host "LAYER 3 - can this instrument even see a line-ending difference?"
Write-Host ("{0,-24} {1,7} {2,7}   {3}" -f "sensitive pattern", "CRLF", "LF", "must DISAGREE")
Write-Host ("-" * 68)
$proved = 0
foreach ($s in $sensitive) {
    $a = Count $cCrlf $s.rx
    $b = Count $cLf   $s.rx
    $ok = ($a -ne $b)
    if ($ok) { $proved++ }
    Write-Host ("{0,-24} {1,7} {2,7}   {3}" -f $s.name, $a, $b,
                $(if ($ok) { "disagrees - the hazard is visible here" } else { "AGREES - layer 3 proves nothing" }))
}
if ($proved -lt @($sensitive).Count) {
    Write-Host "`nrefusing to report: an ending-sensitive pattern failed to show sensitivity."
    exit 1
}

Write-Host ""
Write-Host ("{0,-26} {1,8} {2,6} {3,4} {4,7} {5,5}   verdict" -f "pattern", "ctrlCRLF", "ctrlLF", "exp", "tgtCRLF", "tgtLF")
Write-Host ("-" * 100)

$broken = 0; $hits = 0; $disagreed = 0; $vacuous = 0
foreach ($p in $patterns) {
    $cA = Count $cCrlf $p.rx; $cB = Count $cLf $p.rx
    $tA = Count $tCrlf $p.rx; $tB = Count $tLf $p.rx

    $cAgree = ($cA -eq $cB)
    $tAgree = ($tA -eq $tB)
    $tVac   = ($tA -eq 0 -and $tB -eq 0)
    if (-not ($cAgree -and $tAgree)) { $disagreed++ }
    if ($tVac) { $vacuous++ }

    $verdict =
        if (-not $cAgree)          { $broken++; "CONTROL ENDING-SENSITIVE - unusable" }
        elseif ($cA -eq 0)         { $broken++; "PATTERN DEAD - zero means nothing" }
        elseif ($cA -ne $p.expect) { $broken++; ("CONTROL OFF - planted {0}, found {1}" -f $p.expect, $cA) }
        elseif (-not $tAgree)      { "TARGET ENDING-SENSITIVE - zero not trustworthy" }
        elseif ($tVac)             { ("clean; control agrees {0}/{0} non-vacuously" -f $cA) }
        else                       { "{0} to inspect" -f $tA }

    Write-Host ("{0,-26} {1,8} {2,6} {3,4} {4,7} {5,5}   {6}" -f $p.name, $cA, $cB, $p.expect, $tA, $tB, $verdict)
    $hits += $tA
    if ($tA -gt 0) {
        foreach ($m in @([regex]::Matches($tCrlf, $p.rx))) {
            $line = @([regex]::Matches($tCrlf.Substring(0, $m.Index), "\r\n")).Count + 1
            $text = ($m.Value -replace "\r?\n", " | ").Trim()
            Write-Host ("        :{0,-6} {1}" -f $line, $text)
        }
    }
}

Write-Host ""
Write-Host ("ending-sensitive patterns : {0}" -f $disagreed)
Write-Host ("patterns that cannot fire : {0}" -f $broken)
Write-Host ("hits needing an eye       : {0}" -f $hits)
Write-Host ("vacuous target agreements : {0} of {1}" -f $vacuous, @($patterns).Count)
if ($vacuous -gt 0) {
    Write-Host ""
    Write-Host "READ THE VACUITY COLUMN BEFORE BELIEVING THIS. Where the target scores 0 in"
    Write-Host "both endings, the two agree only because both are empty - and agreement of two"
    Write-Host "zeros is not agreement. That row carries NO ending evidence of its own."
    Write-Host "The weight sits in two other places, both non-vacuous:"
    Write-Host "  - the control finds its distinct planted counts in BOTH endings, so the"
    Write-Host "    pattern demonstrably survives the conversion; and"
    Write-Host "  - the target is measured pure CRLF above, so the LF column is hypothetical."
}
if ($disagreed -eq 0 -and $broken -eq 0 -and $hits -eq 0) {
    Write-Host ""
    Write-Host "zero, from four patterns that each hit their planted count exactly and give"
    Write-Host "the same answer in both line endings, on a target measured pure CRLF."
}
