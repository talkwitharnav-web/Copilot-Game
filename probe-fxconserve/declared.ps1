# The high-precision form of the bug shape #15 hunt.
#
# The 277-name sweep is mostly static_assert helpers, which are supposed to have
# no runtime caller - that is what a compile-time proof IS. Both of tonight's
# real finds were instead announced by their own author, in the header, in
# prose: `armourDefence` and `explosionDropChance` each carried a comment saying
# so. That is a tiny, precise search with almost no noise, and it reads the one
# source that actually knows: the person who wrote the rule.
#
# Vocabulary matters more than cleverness here. This codebase says "no caller"
# and "nothing calls", not "unused" or "dead code". A narrow guess would score
# zero and read as clean - the failure fx-store measured when it grepped
# minecraft|bedrock|wiki|vanilla in a tree that says "the reference".

$root = "C:\Users\arnav\Downloads\Copilot Game\game\src"
$files = @(Get-ChildItem $root -Recurse -File -Include *.hpp, *.cpp)

$phrases = @(
    'no caller',
    'no callers',
    'nothing calls',
    'never called',
    'not called',
    'has no reader',
    'no readers',
    'zero readers',
    'nobody reads',
    'unreachable',
    'never reached',
    'not reachable',
    'inert',
    'dormant',
    'no call site',
    'call site is missing',
    'missing call site',
    'wired on the .* side'
)
$rx = '(?i)(' + ($phrases -join '|') + ')'

# Control: a phrase that must NOT appear, and one that must. Without both, a
# zero from this search is a zero from an instrument never shown able to fire.
$mustFind = 'the reference'
$mustNotFind = 'zzzPhraseThatCannotExist'

$ctlFind = 0
$ctlMiss = 0
foreach ($f in $files) {
    $t = [System.IO.File]::ReadAllText($f.FullName)
    $ctlFind += @([regex]::Matches($t, [regex]::Escape($mustFind))).Count
    $ctlMiss += @([regex]::Matches($t, [regex]::Escape($mustNotFind))).Count
}
Write-Host ("files scanned : {0}" -f $files.Count)
Write-Host ("control, codebase vocabulary 'the reference' : {0}  (must be high)" -f $ctlFind)
Write-Host ("control, impossible phrase                   : {0}  (must be 0)" -f $ctlMiss)
if ($ctlFind -lt 20 -or $ctlMiss -ne 0) {
    Write-Host "instrument failed its controls - refusing to report."
    exit 1
}

Write-Host ""
Write-Host "declarations of unreachability, by file"
Write-Host ("-" * 100)

$total = 0
foreach ($f in ($files | Sort-Object Name)) {
    $raw = [System.IO.File]::ReadAllText($f.FullName)
    $lines = $raw -split "\r?\n"
    $hits = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $rx) {
            # Only comments - a string literal saying "inert" is not a claim
            # about reachability.
            if ($lines[$i].Trim() -match '^(//|/\*|\*)') {
                $hits += [pscustomobject]@{ Line = $i + 1; Text = $lines[$i].Trim() }
            }
        }
    }
    if (@($hits).Count -gt 0) {
        Write-Host ("`n{0}  ({1} hits)" -f $f.Name, @($hits).Count)
        foreach ($h in $hits) {
            $t = $h.Text
            if ($t.Length -gt 88) { $t = $t.Substring(0, 88) }
            Write-Host ("   :{0,-6} {1}" -f $h.Line, $t)
        }
        $total += @($hits).Count
    }
}

Write-Host ""
Write-Host ("total declarations of unreachability : {0}" -f $total)
