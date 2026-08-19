# Byte-level line-ending census, with a control that proves the counter can see
# each class it reports.
#
# The hazard being measured (broadcast 2026-08-19 07:35): the `edit` tool writes
# LF and WriteAllText with an explicit `r`n writes CRLF, so ANY file touched by
# both this session is mixed - and a mixed file silently SHRINKS every
# newline-spanning search rather than failing loudly.
#
# Reading as Latin-1 is deliberate: that encoding is byte-preserving, so the
# regex counts below are counts of BYTES 13 and 10, not of decoded characters.
# Reading as UTF-8 would be just as correct for the ending question but would
# throw away the byte length that step 3 (verify by arithmetic) depends on.

param([switch]$Fix)

$latin1 = [System.Text.Encoding]::GetEncoding(28591)

function Census([string]$text) {
    $crlf = ([regex]::Matches($text, "\r\n")).Count
    $lf   = ([regex]::Matches($text, "\n")).Count
    $cr   = ([regex]::Matches($text, "\r")).Count
    return [pscustomobject]@{
        Crlf   = $crlf
        BareLf = $lf - $crlf
        BareCr = $cr - $crlf
    }
}

# --- control -------------------------------------------------------------
# Three endings and one bare CR that is NOT part of a CRLF, so every branch of
# the arithmetic above has to be exercised to get this right. A counter that
# cannot tell a bare LF from a CRLF scores BareLf 0 here and would then report
# every damaged file in the tree as clean.
$ctrl = "a`r`nb`nc`rd`r`ne"
$c = Census $ctrl
$ctrlOk = ($c.Crlf -eq 2) -and ($c.BareLf -eq 1) -and ($c.BareCr -eq 1)
Write-Host ("control text 'a CRLF b LF c CR d CRLF e' -> CRLF={0} bare LF={1} bare CR={2}  {3}" -f
            $c.Crlf, $c.BareLf, $c.BareCr, $(if ($ctrlOk) { "counter works" } else { "COUNTER BROKEN" }))
if (-not $ctrlOk) { Write-Host "refusing to report a census from a counter that failed its own control."; exit 1 }

# A second control, for the claim this census supports: prove the shrink is
# real, on text whose two forms differ ONLY in their line endings.
$body   = "// note: see the rule in`r`n// Block.hpp:1234 for why`r`n"
$pat    = "//[^\r\n]*\r?\n//[^\r\n]*"
$patBad = "//[^\r\n]*\n//[^\r\n]*"
$crlfHits    = ([regex]::Matches($body, $pat)).Count
$lfHits      = ([regex]::Matches(($body -replace "\r\n", "`n"), $pat)).Count
$crlfHitsBad = ([regex]::Matches($body, $patBad)).Count
$lfHitsBad   = ([regex]::Matches(($body -replace "\r\n", "`n"), $patBad)).Count
Write-Host ("`nagreement control, same text in both endings:")
Write-Host ("  '\r?\n' pattern : CRLF={0}  LF={1}   {2}" -f $crlfHits, $lfHits,
            $(if ($crlfHits -eq $lfHits -and $crlfHits -gt 0) { "agrees - and can fire" } else { "DISAGREES" }))
Write-Host ("  bare '\n'      : CRLF={0}  LF={1}   {2}" -f $crlfHitsBad, $lfHitsBad,
            $(if ($crlfHitsBad -ne $lfHitsBad) { "shrinks - the hazard, demonstrated" } else { "did NOT demonstrate the shrink" }))

# --- census --------------------------------------------------------------
$root = "C:\Users\arnav\Downloads\Copilot Game"
$targets = @()
$targets += (Join-Path $root "game\src\Main.cpp")
$targets += @(Get-ChildItem (Join-Path $root "probe-fxconserve") -File |
              Where-Object { $_.Extension -in @('.ps1', '.cpp', '.txt') } |
              ForEach-Object { $_.FullName })

Write-Host ("`n{0,-34} {1,9} {2,7} {3,8} {4,8}  {5}" -f "file", "bytes", "CRLF", "bare LF", "bare CR", "verdict")
Write-Host ("-" * 96)

$damaged = @()
foreach ($t in $targets) {
    if (-not (Test-Path $t)) { continue }
    $bytes = [System.IO.File]::ReadAllBytes($t)
    $text  = $latin1.GetString($bytes)
    $n = Census $text
    $mixed = ($n.Crlf -gt 0) -and (($n.BareLf -gt 0) -or ($n.BareCr -gt 0))
    $verdict = if ($mixed) { "MIXED" }
               elseif ($n.BareLf -gt 0) { "pure LF" }
               elseif ($n.Crlf -gt 0) { "pure CRLF" }
               else { "no line breaks" }
    # citecontrol.txt carries a deliberate bare LF; it is a control, not damage.
    if ($t -like "*citecontrol.txt") { $verdict += " (deliberate)" }
    elseif ($mixed) { $damaged += $t }
    Write-Host ("{0,-34} {1,9} {2,7} {3,8} {4,8}  {5}" -f
                (Split-Path $t -Leaf), $bytes.Length, $n.Crlf, $n.BareLf, $n.BareCr, $verdict)
}

Write-Host ("`nmixed files needing repair: {0}" -f @($damaged).Count)

if ($Fix -and @($damaged).Count -gt 0) {
    foreach ($d in $damaged) {
        $before = [System.IO.File]::ReadAllBytes($d)
        $text   = $latin1.GetString($before)
        $n      = Census $text
        $repair = $n.BareLf + $n.BareCr
        # \r\n first, so an existing pair is consumed whole and never split.
        $fixed  = [regex]::Replace($text, "\r\n|\n|\r", "`r`n")
        [System.IO.File]::WriteAllText($d, $fixed, [System.Text.UTF8Encoding]::new($false))
        $after = (Get-Item $d).Length
        $delta = $after - $before.Length
        $ok = ($delta -eq $repair)
        Write-Host ("{0}: {1} -> {2} bytes, delta {3}, predicted {4}  {5}" -f
                    (Split-Path $d -Leaf), $before.Length, $after, $delta, $repair,
                    $(if ($ok) { "ok" } else { "ARITHMETIC DISAGREES - something else changed" }))
    }
}
