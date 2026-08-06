# Measures how the player floats and bobs in water, without needing a playtest.
#
# Water tuning is the one part of the physics that is almost impossible to judge
# by eye in one sitting and trivial to get wrong by a factor of five - the bob
# shipped twice at the wrong size before this existed. The vertical motion is a
# pure function of the constants, so it can simply be replayed here.
#
# **Every constant is read out of the headers rather than copied**, because a
# second copy of a number that already has a home is this codebase's most
# repeated bug. Change `Fluid.hpp` or `Player.hpp` and this follows.
#
#   .\tools\simulate-swim.ps1              # open water
#   .\tools\simulate-swim.ps1 -Sprint      # sprint-swimming
#   .\tools\simulate-swim.ps1 -PoolDepth 1 # standing in a one-block pool
[CmdletBinding()]
param(
    [switch]$Sprint,
    # 0 = open water. Otherwise how many blocks deep the pool is, with a floor
    # under it, which is the case that used to be a trap.
    [int]$PoolDepth = 0,
    # Overrides `kFloatEye` for sweeping. 0 uses whatever the header says.
    # **Deliberately not named `$FloatEye`**: PowerShell variable names are case
    # insensitive, so it would be the same variable as the one read from the
    # header below, and the override would silently do nothing.
    [double]$TryFloatEye = 0.0,
    # Overrides `kStroke`, which is what sets the bob's amplitude. Same naming
    # rule as above.
    [double]$TryStroke = 0.0,
    # Overrides `kTreadSpeed`, which is what sets the *period*: how quickly the
    # stroke carries you up against how slowly the sink brings you back.
    [double]$TryTread = 0.0,
    [double]$Seconds = 30.0,
    [int]$Fps = 120
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fluid = Get-Content (Join-Path $root 'game\src\world\Fluid.hpp') -Raw
$player = Get-Content (Join-Path $root 'game\src\world\Player.hpp') -Raw

function Read-Constant([string]$text, [string]$name, [string]$where) {
    $m = [regex]::Match($text, "constexpr\s+float\s+$name\s*=\s*([-0-9.]+)f")
    if (-not $m.Success) { throw "$name not found in $where - has it been renamed?" }
    return [double]$m.Groups[1].Value
}

$eyeHeight   = Read-Constant $player 'kEyeHeight'   'Player.hpp'
$bodyHeight  = Read-Constant $player 'kHeight'      'Player.hpp'
$gravity     = Read-Constant $player 'kGravity'     'Player.hpp'
$jumpSpeed   = Read-Constant $player 'kJumpVelocity' 'Player.hpp'

$sink        = Read-Constant $fluid 'kSinkSpeed'    'Fluid.hpp'
$waterDrag   = Read-Constant $fluid 'kWaterDrag'    'Fluid.hpp'
$sprintDrag  = Read-Constant $fluid 'kSprintSwimDrag' 'Fluid.hpp'
$swimUp      = Read-Constant $fluid 'kSwimUpSpeed'  'Fluid.hpp'
$sprintUp    = Read-Constant $fluid 'kSprintSwimUpSpeed' 'Fluid.hpp'
$floatEye    = Read-Constant $fluid 'kFloatEye'     'Fluid.hpp'
$stroke      = Read-Constant $fluid 'kStroke'       'Fluid.hpp'
$shallow     = Read-Constant $fluid 'kShallowDepth' 'Fluid.hpp'
$tick        = Read-Constant $fluid 'kTickSeconds'  'Fluid.hpp'
if ($TryFloatEye -gt 0.0) { $floatEye = $TryFloatEye }
if ($TryStroke -gt 0.0) { $stroke = $TryStroke }
# `kTreadSpeed` is written as a multiple of the sink speed, so read the
# multiplier rather than the product - hardcoding it here would be exactly the
# second copy this whole script exists to avoid.
$m = [regex]::Match($fluid, 'constexpr\s+float\s+kTreadSpeed\s*=\s*kSinkSpeed\s*\*\s*([0-9.]+)f')
if (-not $m.Success) { throw 'kTreadSpeed is no longer a multiple of kSinkSpeed - update this script.' }
# **The override has to come after this**, or the derivation silently wins.
$treadSpeed = $sink * [double]$m.Groups[1].Value
if ($TryTread -gt 0.0) { $treadSpeed = $TryTread }

$rise = if ($Sprint) { $sprintUp } else { $swimUp }
$drag = if ($Sprint) { $sprintDrag } else { $waterDrag }
# The drift between strokes is the ordinary sink even while sprint-swimming -
# skipping water gravity is a rule about holding depth with the head under.
$idle = -$sink

# One water source is 8/9 of a block tall, as `fluidHeight` has it.
$fluidTop = 8.0 / 9.0
$topWaterBlock = 63
$surface = $topWaterBlock + $fluidTop
$floorY = if ($PoolDepth -gt 0) { [double]($topWaterBlock - $PoolDepth + 1) } else { -1e9 }

# `sampleFluid`: the surface is the highest water block the box overlaps, so it
# is capped by the body rather than being the true sea level.
function Get-Depth([double]$y) {
    $minY = [math]::Floor($y)
    $maxY = [math]::Floor($y + $bodyHeight - 0.001)
    $top = -1e9
    for ($b = $minY; $b -le $maxY; $b++) {
        if ($b -le $topWaterBlock -and $b -ge $floorY) { $top = [math]::Max($top, $b + $fluidTop) }
    }
    if ($top -lt -1e8) { return -1.0 }
    return [math]::Max(0.0, $top - $y)
}

$dt = 1.0 / $Fps
$k = [math]::Pow($drag, $dt / $tick)
$y = if ($PoolDepth -gt 0) { $floorY } else { $surface - 6.0 }
$v = 0.0
$onGround = $PoolDepth -gt 0
$treading = $true
$samples = New-Object System.Collections.Generic.List[double]
$steps = [int]($Seconds * $Fps)

for ($i = 0; $i -lt $steps; $i++) {
    $depth = Get-Depth $y
    if ($depth -ge 0.0) {
        if ($onGround -and $depth -lt $shallow) {
            $v = $jumpSpeed
            $onGround = $false
        } else {
            $headClear = $eyeHeight - $depth
            if ($headClear -lt ($floatEye - $stroke)) { $treading = $true }
            elseif ($headClear -gt ($floatEye + $stroke)) { $treading = $false }
            # Standing on the bottom pushes off at the climb speed, whatever the
            # head is doing - otherwise a shallow pool is a trap.
            $pushing = $onGround
            $target = if ($headClear -le 0.0 -or $pushing) { $rise } elseif ($treading) { $treadSpeed } else { $idle }
            $v = $v * $k + $target * (1.0 - $k)
            if ($v -gt 0.0) { $onGround = $false }
        }
    } else {
        $v = $v - $gravity * $dt
    }
    $y += $v * $dt
    if ($y -le $floorY) { $y = $floorY; $v = 0.0; $onGround = $true }
    # Only the settled half, so the initial approach does not skew the range.
    if ($i -gt $steps / 2) { $samples.Add($y) }
}

$lo = ($samples | Measure-Object -Minimum).Minimum
$hi = ($samples | Measure-Object -Maximum).Maximum

# Period from upward crossings of the midpoint - one per cycle.
$mid = ($lo + $hi) / 2.0
$crossings = 0
for ($j = 1; $j -lt $samples.Count; $j++) {
    if ($samples[$j - 1] -le $mid -and $samples[$j] -gt $mid) { $crossings++ }
}
$window = $samples.Count / $Fps
$period = if ($crossings -gt 0) { $window / $crossings } else { [double]::NaN }

$what = if ($PoolDepth -gt 0) { "$PoolDepth-block pool" } elseif ($Sprint) { 'sprint-swimming' } else { 'open water' }

Write-Host ""
Write-Host "  $what, holding jump" -ForegroundColor Cyan
Write-Host ("    bob                {0,7:N3} m   every {1:N2} s" -f ($hi - $lo), $period)
Write-Host ("    eyes above water   {0,7:N3} .. {1:N3} m" -f ($lo + $eyeHeight - $surface), ($hi + $eyeHeight - $surface))
Write-Host ("    body out of water  {0,6:N0}% .. {1:N0}%" -f `
    ([math]::Max(0.0, $lo + $bodyHeight - $surface) / $bodyHeight * 100.0), `
    ([math]::Max(0.0, $hi + $bodyHeight - $surface) / $bodyHeight * 100.0))
if (($lo + $eyeHeight - $surface) -lt 0.0) {
    Write-Host "    WARNING: the eyes go under at the bottom of the bob." -ForegroundColor Yellow
}
Write-Host ""
