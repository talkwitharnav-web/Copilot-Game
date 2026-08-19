# Re-checks the "Main.cpp compiles clean at /W4 /WX" claim against three
# instrument failures at once:
#
#   1. A grep for `error C` misses `Command line error D8036` - a compile that
#      NEVER RAN reporting zero errors. So this matches `error|fatal`, checks the
#      exit code, AND checks that a fresh object file actually appeared, because
#      a compile that never ran cannot produce one.
#   2. "The control fired" is not the test. The control below must differ from
#      the claim in the expected DIRECTION and MAGNITUDE, so all three signals
#      are compared, not just one.
#   3. Every path is absolute - .NET and cmd both ignore PowerShell's location.

param([switch]$Debug)

$root = "C:\Users\arnav\Downloads\Copilot Game"
$src  = Join-Path $root "game\src\Main.cpp"
$out  = Join-Path $root "probe-fxconserve"
$vc   = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

$glm = if ($Debug) { "build\debug\_deps\glm-src" } else { "build\release\_deps\glm-src" }
$mode = if ($Debug) { "/Ob0 /Od /RTC1 -MDd -Zi" } else { "/O2 /Ob2 /DNDEBUG -MD" }
$obj = Join-Path $out ("selfcheck-" + $(if ($Debug) { "debug" } else { "release" }) + ".obj")

function Compile([string]$extraFlag, [string]$objPath) {
    if (Test-Path $objPath) { Remove-Item $objPath -Force }
    $before = Test-Path $objPath
    $cmd = "call `"$vc`" >nul && cl /nologo /TP -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DGLM_FORCE_RADIANS " +
           "-I`"$root\game\src`" -I`"$root\engine\include`" -I`"$root\$glm`" " +
           "-external:IC:\VulkanSDK\1.4.357.0\Include -external:W0 /DWIN32 /D_WINDOWS /EHsc $mode " +
           "-std:c++20 /W4 /WX /permissive- /utf-8 /Zc:__cplusplus $extraFlag /c " +
           "/Fo:`"$objPath`" /Fd:`"$objPath.pdb`" `"$src`""
    $text = (cmd /c $cmd 2>&1) | Out-String
    $code = $LASTEXITCODE
    # NOT `error C` - that pattern is exactly what lets a D#### command-line
    # error through. The narrow one is measured alongside to show the gap.
    $hits = @([regex]::Matches($text, '(?im)^.*\b(error|fatal)\b.*$'))
    $narrow = @([regex]::Matches($text, '(?im)^.*error C\d+.*$'))
    return [pscustomobject]@{
        Exit    = $code
        Matches = $hits.Count
        Narrow  = $narrow.Count
        First   = if ($hits.Count -gt 0) { $hits[0].Value.Trim() } else { "" }
        ObjMade = (Test-Path $objPath)
        Before  = $before
    }
}

Write-Host ("=== /W4 /WX self-check, {0} preset ===" -f $(if ($Debug) { "DEBUG" } else { "RELEASE" }))
Write-Host ("source mtime: {0}" -f (Get-Item $src).LastWriteTime)
Write-Host ""

$claim = Compile "" $obj
Write-Host ("CLAIM   exit={0}  error|fatal lines={1}  object produced={2}" -f
            $claim.Exit, $claim.Matches, $claim.ObjMade)
if ($claim.First -ne "") { Write-Host ("        first: " + $claim.First) }

# The control: `/Zmabc` gives cl a non-numeric argument for a numeric switch, so
# it stops at the command line and NEVER STARTS - the D#### class that a grep for
# `error C` cannot see, because the text reads "Command line error D8004".
#
# An earlier control here was `/Zthis-switch-does-not-exist`, which was WRONG and
# was caught by this harness's own cross-check: cl reads that as a run of /Z
# options, one of which is /Zs, syntax-check-only. It compiled cleanly, exited 0
# and emitted no object - so the control returned the claim's own answer on two
# signals out of three and looked like the claim was broken. "The control fired"
# would have been the wrong verdict either way.
$ctrlObj = Join-Path $out "selfcheck-control.obj"
$control = Compile "/Zmabc" $ctrlObj
Write-Host ("CONTROL exit={0}  error|fatal lines={1}  object produced={2}" -f
            $control.Exit, $control.Matches, $control.ObjMade)
if ($control.First -ne "") { Write-Host ("        first: " + $control.First) }
Write-Host ("        the narrow `error C` pattern scores {0} on it - which is the bug" -f
            $control.Narrow)
if (Test-Path $ctrlObj) { Remove-Item $ctrlObj -Force }
if (Test-Path ($ctrlObj + ".pdb")) { Remove-Item ($ctrlObj + ".pdb") -Force }

Write-Host ""
$ok = $true
function Say([string]$what, [bool]$good) {
    Write-Host ("  {0,-62} {1}" -f $what, $(if ($good) { "ok" } else { "FAIL" }))
    if (-not $good) { $script:ok = $false }
}

Say "claim: exit code 0"                           ($claim.Exit -eq 0)
Say "claim: no line matching error|fatal"          ($claim.Matches -eq 0)
Say "claim: a fresh object file was produced"      ($claim.ObjMade)
Say "control DIFFERS - it fails to exit 0"         ($control.Exit -ne 0)
Say "control DIFFERS - it matches error|fatal"     ($control.Matches -gt 0)
Say "control DIFFERS - it produces NO object"      (-not $control.ObjMade)
Say "all three signals disagree, not just one"     (($claim.Exit -ne $control.Exit) -and
                                                    ($claim.Matches -ne $control.Matches) -and
                                                    ($claim.ObjMade -ne $control.ObjMade))
Say "and the narrow 'error C' pattern MISSES it"   ($control.Narrow -eq 0)

if (Test-Path $obj) { Remove-Item $obj -Force }
if (Test-Path ($obj + ".pdb")) { Remove-Item ($obj + ".pdb") -Force }

Write-Host ""
Write-Host $(if ($ok) { "VERDICT: the clean-compile claim holds and the check can catch D8036." }
             else { "VERDICT: FAILED" })
