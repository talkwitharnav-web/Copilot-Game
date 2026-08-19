# Bug shape #15 sweep: a complete rule sitting one call site short of doing
# anything. Both of tonight's examples had this exact shape - `armourDefence`
# and `explosionDropChance` were written, asserted and unreachable, and in both
# cases the call site that was missing belonged in Main.cpp.
#
# There is no compiler warning for a table nobody reads, so this has to be
# measured. The instrument discipline that the fleet has paid for tonight:
#
#   - strip comments, strings and char literals first, into a parallel text.
#     `explosionDropChance` occurs 10 times in its own header and only ONE is
#     a definition; the rest are comments and static_asserts.
#   - count BARE NAMES, not `name(`. ~40 names in Creature.cpp sit in a
#     behaviour table with no parentheses and a `name(` search calls them dead.
#   - carry a known-LIVE control, and require it to score high. A sweep that
#     reports everything dead is broken, not lucky.
#   - carry a known-DEAD control, and require it to score zero, or the
#     detector has never been shown able to return the answer it is claiming.
#   - expect DISTINCT counts. A uniform result is the instrument, not the world.

$root = "C:\Users\arnav\Downloads\Copilot Game\game\src"

# --- strip, so a comment cannot be mistaken for a use -------------------------
function Strip([string]$s) {
    $s = [regex]::Replace($s, "/\*.*?\*/", " ", "Singleline")
    $s = [regex]::Replace($s, "//[^\r\n]*", " ")
    $s = [regex]::Replace($s, '"(?:\\.|[^"\\])*"', ' ')
    $s = [regex]::Replace($s, "'(?:\\.|[^'\\])*'", " ")
    return $s
}

$files = @(Get-ChildItem $root -Recurse -File -Include *.hpp, *.cpp)
Write-Host ("files read : {0}" -f $files.Count)

$stripped = @{}
foreach ($f in $files) {
    $raw = [System.IO.File]::ReadAllText($f.FullName)
    $stripped[$f.FullName] = Strip $raw
}

# --- control on the stripper itself ------------------------------------------
$probe = "int live(); // dead1 mentioned only here" + "`r`n" + 'const char* s = "dead2";' + "`r`n" + "int x = live();"
$sp = Strip $probe
$ctrlStrip = (@([regex]::Matches($sp, "\bdead1\b")).Count -eq 0) -and
             (@([regex]::Matches($sp, "\bdead2\b")).Count -eq 0) -and
             (@([regex]::Matches($sp, "\blive\b")).Count -eq 2)
Write-Host ("stripper control (comment gone, string gone, code kept) : {0}" -f
            $(if ($ctrlStrip) { "ok" } else { "BROKEN" }))
if (-not $ctrlStrip) { exit 1 }

# --- collect candidate rule definitions from headers -------------------------
# Free functions returning a value, declared constexpr/inline at namespace
# scope. Deliberately not methods: a member with no caller is a different and
# much noisier question.
$defRx = '(?m)^\s*(?:\[\[nodiscard\]\]\s*)?(?:constexpr|inline)\s+(?:constexpr\s+)?[A-Za-z_][\w:<>,\s\*&]*?\s+([a-z][A-Za-z0-9_]*)\s*\('
$defs = @{}
foreach ($f in $files) {
    if ($f.Extension -ne ".hpp") { continue }
    foreach ($m in @([regex]::Matches($stripped[$f.FullName], $defRx))) {
        $n = $m.Groups[1].Value
        if (-not $defs.ContainsKey($n)) { $defs[$n] = $f.FullName }
    }
}
Write-Host ("candidate rule functions defined in headers : {0}" -f $defs.Count)

# --- count bare-name uses outside the defining file --------------------------
function ExternalUses([string]$name, [string]$homeFile) {
    $total = 0
    $where = @()
    foreach ($f in $files) {
        if ($f.FullName -eq $homeFile) { continue }
        $c = @([regex]::Matches($stripped[$f.FullName], "\b" + [regex]::Escape($name) + "\b")).Count
        if ($c -gt 0) { $total += $c; $where += ("{0}:{1}" -f $f.Name, $c) }
    }
    return [pscustomobject]@{ Count = $total; Where = ($where -join ", ") }
}

# --- controls, checked BEFORE the result is believed -------------------------
Write-Host ""
Write-Host "controls"
$liveCtl = ExternalUses "foodValue" ""
Write-Host ("  known-LIVE 'foodValue' external uses      : {0}   (must be high)" -f $liveCtl.Count)
$deadCtl = ExternalUses "zzzNoSuchRuleExists" ""
Write-Host ("  known-DEAD 'zzzNoSuchRuleExists'          : {0}   (must be 0)" -f $deadCtl.Count)
if ($liveCtl.Count -lt 5 -or $deadCtl.Count -ne 0) {
    Write-Host "  instrument failed its own controls - refusing to report."
    exit 1
}

# --- the sweep ---------------------------------------------------------------
$unreached = @()
foreach ($n in $defs.Keys) {
    $u = ExternalUses $n $defs[$n]
    if ($u.Count -eq 0) {
        $unreached += [pscustomobject]@{
            Name = $n
            Home = (Split-Path $defs[$n] -Leaf)
        }
    }
}

Write-Host ""
Write-Host ("rules defined in a header and used NOWHERE else : {0} of {1}" -f
            @($unreached).Count, $defs.Count)
Write-Host ""
$byHome = $unreached | Group-Object Home | Sort-Object Count -Descending
foreach ($g in $byHome) {
    Write-Host ("  {0,-24} {1,3}   {2}" -f $g.Name, $g.Count,
                (($g.Group | ForEach-Object { $_.Name } | Sort-Object) -join ", "))
}
