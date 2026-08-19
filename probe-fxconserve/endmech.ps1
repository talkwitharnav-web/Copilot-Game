# WHICH writer actually produces a mixed file? Measured, not deduced.
#
# The fleet broadcast at 07:35 named the `edit` tool as the LF writer. That is
# NOT what this machine does, and the difference matters because the natural
# mitigation for the broadcast version ("stop using edit, use WriteAllText")
# points straight at the mechanism that IS responsible.
#
# Every case below starts from the SAME byte-exact pure-CRLF file, so the only
# variable is the writer. Byte deltas are predicted before they are measured.

$d    = "C:\Users\arnav\Downloads\Copilot Game\probe-fxconserve"
$enc  = [System.Text.UTF8Encoding]::new($false)
$l1   = [System.Text.Encoding]::GetEncoding(28591)
$base = "alpha`r`nbravo`r`nMARK`r`ncharlie`r`ndelta`r`n"

function Census([string]$path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    $t = $l1.GetString($b)
    $c = ([regex]::Matches($t, "\r\n")).Count
    $l = ([regex]::Matches($t, "\n")).Count
    $r = ([regex]::Matches($t, "\r")).Count
    return [pscustomobject]@{ Bytes = $b.Length; Crlf = $c; BareLf = $l - $c; BareCr = $r - $c }
}

function Show([string]$label, [string]$path, [string]$note) {
    $n = Census $path
    # BareCr is counted and printed because omitting it made THIS script report
    # case E's broadcast row as "pure CRLF" when it still held a lone \r. A
    # verdict that cannot see one of the three classes it rules on is worse than
    # no verdict, because it reads as a pass.
    $v = if ($n.Crlf -gt 0 -and ($n.BareLf -gt 0 -or $n.BareCr -gt 0)) { "MIXED" }
         elseif ($n.BareLf -gt 0 -or $n.BareCr -gt 0) { "no CRLF" } else { "pure CRLF" }
    Write-Host ("  {0,-44} bytes={1,4}  CRLF={2,2}  bareLF={3,2}  bareCR={4,2}  {5,-9} {6}" -f
                $label, $n.Bytes, $n.Crlf, $n.BareLf, $n.BareCr, $v, $note)
}

$f = Join-Path $d "mech.txt"
Write-Host "starting file, laid by WriteAllText with an explicit CRLF payload:"
[System.IO.File]::WriteAllText($f, $base, $enc)
Show "(baseline)" $f ""

Write-Host "`nA. WriteAllText, payload built with PowerShell's ``n (the common way):"
[System.IO.File]::WriteAllText($f, ($base -replace "MARK", "MARK`nEXTRA"), $enc)
Show "WriteAllText + ``n in the replacement" $f "<-- reproduces the reported damage"

Write-Host "`nB. WriteAllText, payload built with an explicit ``r``n:"
[System.IO.File]::WriteAllText($f, ($base -replace "MARK", "MARK`r`nEXTRA"), $enc)
Show "WriteAllText + ``r``n in the replacement" $f "safe"

Write-Host "`nC. WriteAllText, payload from a here-string (carries the transport's endings):"
$here = @"
alpha
bravo
MARK
charlie
delta
"@
[System.IO.File]::WriteAllText($f, $here, $enc)
Show "WriteAllText + here-string" $f "whatever the transport gave it"

Write-Host "`nD. The forbidden writers, for completeness - BOM is the reason they are banned,"
Write-Host "   but their line endings are worth knowing too:"
[System.IO.File]::WriteAllText($f, $base, $enc)
$base -replace "MARK", "MARK`nEXTRA" | Set-Content -Path (Join-Path $d "mech2.txt")
Show "Set-Content (banned: adds a BOM)" (Join-Path $d "mech2.txt") "does NOT normalise - see below"

Write-Host "`nE. The RECOMMENDED normalisation snippet, tested against all three endings."
Write-Host "   Input carries CRLF, a bare LF and a bare CR - the third is the question."
$mixed = "alpha`r`nbravo`ncharlie`rdelta`r`n"
[System.IO.File]::WriteAllText($f, $mixed, $enc)
Show "(input)" $f "1 bare LF + 1 bare CR"

# The snippet as broadcast: two sequential -replace passes.
$broadcast = ($mixed -replace "`r`n", "`n") -replace "`n", "`r`n"
[System.IO.File]::WriteAllText($f, $broadcast, $enc)
Show "broadcast: -replace CRLF->LF then LF->CRLF" $f "<-- check bare CR"

# Single-pass alternation, CRLF first so an existing pair is consumed whole.
$onepass = [regex]::Replace($mixed, "\r\n|\n|\r", "`r`n")
[System.IO.File]::WriteAllText($f, $onepass, $enc)
Show "single pass: \r\n|\n|\r -> CRLF" $f "handles all three"

Write-Host "`nVERDICT"
Write-Host "  The writer is not the variable - the PAYLOAD is. WriteAllText writes exactly"
Write-Host "  the bytes it is given, so a payload holding a bare ``n makes a mixed file and a"
Write-Host "  payload holding ``r``n does not. Case A is the reported damage shape: a handful"
Write-Host "  of bare LF inside thousands of CRLF, which is what one multi-line replacement"
Write-Host "  with a ``n in it produces."
Write-Host "  Separately measured on this machine: the `edit` tool PRESERVES a uniform file's"
Write-Host "  convention in both directions, and the `create` tool writes CRLF."
Write-Host ""
Write-Host "  Case D was a surprise and this line was WRONG before it was measured: I had"
Write-Host "  annotated Set-Content as 'normalises to CRLF'. It does not. It appends CRLF at"
Write-Host "  the joins it controls and passes an embedded bare ``n straight through, so it"
Write-Host "  produces a mixed file AND a BOM. It is banned for the BOM; it would not have"
Write-Host "  saved anyone from this either."

Remove-Item (Join-Path $d "mech.txt"), (Join-Path $d "mech2.txt") -ErrorAction SilentlyContinue
