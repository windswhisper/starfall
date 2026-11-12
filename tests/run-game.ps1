param(
    [Parameter(Mandatory)][ValidateSet('projectiles', 'script-grants', 'weapon-range', 'fire-rate', 'before-fire', 'before-damage', 'aura', 'power-state', 'condition-formula', 'passenger-consume', 'promotion', 'transport-support', 'signal', 'deployed-state', 'immune', 'health-formula', 'research-hud', 'hex-icons', 'max-health', 'passive-gained', 'reapply', 'passive-stacks', 'passive-animations', 'conversion', 'research-cost', 'spawn-passives')][string]$Case,
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$PhobosPackageDirectory
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
foreach ($name in 'Syringe.exe', 'gamemd.exe') {
    if (!(Test-Path -LiteralPath (Join-Path $game $name) -PathType Leaf)) { throw "Missing $name in $game" }
}
& (Join-Path $PSScriptRoot 'build.cmd') $Case
if ($LASTEXITCODE) { throw 'Test DLL build failed' }

$runs = (New-Item -Force -ItemType Directory (Join-Path $PSScriptRoot 'local/runs')).FullName
$run = (New-Item -ItemType Directory (Join-Path $runs "$Case-$([Guid]::NewGuid().ToString('N'))")).FullName
$fixture = Join-Path $PSScriptRoot "game/$Case"
$projectiles = $Case -eq 'projectiles'
$map = if ($Case -ne 'script-grants') { Join-Path $PSScriptRoot 'maps/Starfall_TestRange.map' } else { Join-Path $fixture 'ScriptGrants.map' }
$save = if ($projectiles) { 'ProjectileScale.sav' } elseif ($Case -eq 'weapon-range') { 'WeaponRange.sav' } elseif ($Case -eq 'fire-rate') { 'FireRate.sav' } elseif ($Case -eq 'deployed-state') { 'DeployedState.sav' } elseif ($Case -eq 'immune') { 'Immune.sav' } elseif ($Case -eq 'health-formula') { 'HealthFormula.sav' } elseif ($Case -eq 'max-health') { 'MaxHealth.sav' } elseif ($Case -eq 'research-cost') { 'Prices.sav' } elseif ($Case -eq 'spawn-passives') { 'Spawns.sav' } elseif ($Case -eq 'passive-gained') { 'PassiveGained.sav' } elseif ($Case -eq 'research-hud') { 'ResearchHUD.sav' } else { 'ScriptGrantTest.sav' }
$logName = if ($projectiles) { 'probe.log' } elseif ($Case -eq 'weapon-range') { 'range.log' } elseif ($Case -eq 'fire-rate') { 'rate.log' } elseif ($Case -eq 'deployed-state') { 'deployment.log' } elseif ($Case -eq 'immune') { 'immune.log' } elseif ($Case -eq 'health-formula') { 'formula.log' } elseif ($Case -eq 'max-health') { 'hp.log' } elseif ($Case -eq 'research-cost') { 'prices.log' } elseif ($Case -eq 'spawn-passives') { 'spawns.log' } elseif ($Case -eq 'passive-gained') { 'gained.log' } elseif ($Case -eq 'research-hud') { 'hud.log' } elseif ($Case -eq 'hex-icons') { 'hex-icons.log' } else { 'integration.log' }
if ($Case -eq 'before-fire') { $save='BeforeFire.sav'; $logName='before-fire.log' }
if ($Case -eq 'before-damage') { $save='BeforeDamage.sav'; $logName='before-damage.log' }
if ($Case -eq 'aura') { $save='Aura.sav'; $logName='aura.log' }
if ($Case -eq 'power-state') { $save='PowerState.sav'; $logName='power-state.log' }
if ($Case -eq 'condition-formula') { $save='ConditionFormula.sav'; $logName='condition-formula.log' }
if ($Case -eq 'passenger-consume') { $save='PassengerConsume.sav'; $logName='passenger-consume.log' }
if ($Case -eq 'promotion') { $save='Promotion.sav'; $logName='promotion.log' }
if ($Case -eq 'transport-support') { $save='TransportSupport.sav'; $logName='transport-support.log' }
if ($Case -eq 'signal') { $save='Signal.sav'; $logName='signal.log' }
if ($Case -eq 'reapply') { $save='Reapply.sav'; $logName='reapply.log' }
if ($Case -eq 'passive-stacks') { $save='PassiveStacks.sav'; $logName='stacks.log' }
if ($Case -eq 'passive-animations') { $save='PassiveAnimations.sav'; $logName='animations.log' }
if ($Case -eq 'conversion') { $save='Conversion.sav'; $logName='conversion.log' }
Write-Host "Test directory: $run"
Get-ChildItem -LiteralPath $game -File | Where-Object {
    ($_.Extension -in '.mix','.dll','.tlb','.inj','.dsk','.lcf','.pcx' -or
     $_.Name -in 'Syringe.exe','gamemd.exe','ConquerMD.dat','ddraw.ini','RA2MD.ini') -and $_.Name -ne 'Starfall.dll'
} | Copy-Item -Destination $run
if ($PhobosPackageDirectory) {
    $package=(Resolve-Path -LiteralPath $PhobosPackageDirectory).Path
    foreach ($name in 'Phobos.dll','Syringe.exe','gamemd.edb') {
        $file=Join-Path $package $name
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing $name in $package" }
        Copy-Item -LiteralPath $file -Destination (Join-Path $run $name) -Force
    }
    $symbols=Join-Path $package 'Phobos.pdb'
    if (Test-Path -LiteralPath $symbols -PathType Leaf) { Copy-Item -LiteralPath $symbols -Destination $run }
}
Copy-Item -LiteralPath (Join-Path $root "build/tests/$Case/Starfall.dll") -Destination $run
if ($Case -eq 'hex-icons') {
    Copy-Item -LiteralPath (Join-Path $root 'examples/Starfall.HexBrawl.ini') -Destination (Join-Path $run 'Starfall.ini')
    Copy-Item -LiteralPath (Join-Path $root 'examples/HexBrawlIcons') -Destination $run -Recurse
    Copy-Item -LiteralPath (Join-Path $fixture 'hx-test.pcx') -Destination (Join-Path $run 'hx-test.pcx')
} else {
    Copy-Item -LiteralPath (Join-Path $fixture 'Starfall.ini') -Destination $run
}
if ($Case -eq 'research-hud') {
    Copy-Item -LiteralPath (Join-Path $fixture 'ui-test.wav') -Destination $run
}
Copy-Item -LiteralPath $map -Destination (Join-Path $run 'spawnmap.ini')
if ($Case -eq 'research-cost') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[CLEG]`r`nTechLevel=-1`r`n")
}
if ($Case -eq 'before-fire') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[120mm]`r`nBurst=3`r`n")
}
if ($Case -eq 'spawn-passives') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n"+[IO.File]::ReadAllText((Join-Path $fixture 'SpawnRules.ini')))
}
New-Item -ItemType Directory (Join-Path $run 'Saved Games') | Out-Null
New-Item -ItemType File (Join-Path $run 'Starfall.test') | Out-Null
$spawn = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'game/spawn.ini'))
$records = (New-Item -Force -ItemType Directory (Join-Path $PSScriptRoot 'local/records')).FullName
$freshSpawn=$spawn
$phases = if ($Case -eq 'hex-icons') { 'fresh' } elseif ($Case -eq 'conversion' -and $PhobosPackageDirectory) { 'fresh','load' } elseif ($Case -eq 'conversion') { 'fresh','without-phobos','without-phobos-load' } else { 'fresh','load' }
foreach ($phase in $phases) {
    if ($phase -in 'load','without-phobos-load') {
        New-Item -ItemType File (Join-Path $run 'reload.flag') | Out-Null
        $spawn = $freshSpawn.Replace('[Settings]', "[Settings]`r`nLoadSaveGame=yes`r`nSaveGameName=$save")
    }
    if ($phase -eq 'without-phobos' -and $Case -eq 'conversion') {
        Remove-Item -LiteralPath (Join-Path $run 'reload.flag') -ErrorAction SilentlyContinue
        Rename-Item -LiteralPath (Join-Path $run 'Phobos.dll') -NewName 'Phobos.disabled'
        New-Item -ItemType File (Join-Path $run 'without-phobos.flag') | Out-Null
        $spawn=$freshSpawn
    }
    [IO.File]::WriteAllText((Join-Path $run 'spawn.ini'), $spawn)
    $report = Join-Path $run $logName
    [IO.File]::WriteAllText($report,'')
    $arguments=if ($PhobosPackageDirectory) { '-SPAWN gamemd.exe -i=Starfall.dll -i=Ares.dll -i=cncnet5.dll -i=Phobos.dll --args="-SPAWN -LOG -CD -WIN" --nodetach' } else { '"gamemd.exe" -SPAWN -LOG -CD -WIN' }
    $proc = Start-Process -FilePath (Join-Path $run 'Syringe.exe') -ArgumentList $arguments -WorkingDirectory $run -WindowStyle Hidden -PassThru
    if (!$proc.WaitForExit(120000)) {
        Get-Process -Name gamemd,Syringe -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -in (Join-Path $run 'gamemd.exe'),(Join-Path $run 'Syringe.exe')
        } | Stop-Process -Force
        throw "Game timed out in $run"
    }
    $reportText = [IO.File]::ReadAllText($report)
    Copy-Item -LiteralPath $report -Destination (Join-Path $records "$Case-$phase.log") -Force
    if ($proc.ExitCode -ne 0) { throw "Game failed in $run" }
    if ($projectiles) {
        $expected = @('live Cannon 300','live BlimpBombP 300','completed 150','PASS triple explosion width')
        if ($phase -eq 'fresh') {
            $expected += @('unit model 100','created 100 at distance 480','damage 0 restored 1 stack 0','created 300 at distance 480','saved 1')
            if ($reportText -notmatch '(?m)^damage [1-9]\d* restored 1 stack 0') { throw 'Scaled blast missed the test target' }
        }
        else { $expected += 'boot 27 base 1 shots 2' }
    } elseif ($Case -eq 'hex-icons') {
        $expected = @('PASS atlas and rarity', 'PASS PCX and fallback')
    } elseif ($Case -eq 'research-hud') {
        $expected = @('PASS half border', 'PASS disabled at 50', 'PASS completed icons', 'PASS transparent badge on dark plate', 'PASS left badge hover', 'PASS balance tooltip', 'PASS retained refresh limits', 'PASS purchase', 'PASS purchase animation closes panel', 'PASS no alternative preserves allowance')
        if ($phase -eq 'fresh') {
            $expected += @('PASS legacy options ignored','PASS unregistered projects empty','PASS registered projects restored','PASS empty border','PASS native click sound configured','PASS configured wav loaded','PASS embedded wav loaded','PASS embedded wav playback','PASS wav playback queued','PASS engine blocks click through','PASS card drag cannot select behind','PASS disabled at 99','PASS funded mid-click','PASS full border','PASS ini disables refresh','PASS independent refresh','PASS three free refreshes','PASS unique eligible projects','PASS reopen retains cards','PASS saved')
        }
    } elseif ($Case -eq 'health-formula') {
        $expected = @('PASS retained fractional stats','PASS retained state','PASS healed state','PASS legacy integer stats','PASS legacy shield records skipped')
        if ($phase -eq 'fresh') { $expected += 'PASS removed shield action rejected' }
        if ($phase -eq 'fresh') { $expected += @('PASS grammar','PASS comparison true','PASS comparison false','PASS invalid conditional syntax','PASS if distance threshold','PASS if lazy nested branches','PASS conditional state without target','PASS conditional distance state','PASS conditional event without target','PASS target health grammar','PASS target maximum increased','PASS dead target snapshot','PASS target event snapshot','PASS target event formula results','PASS target state current health','PASS target state live update','PASS target state no target','PASS target missing skips action','PASS damage grammar','PASS damage trigger validation','PASS damage formula results','PASS distance grammar','PASS distance geometry and attacker','PASS distance state target','PASS distance state no target','PASS distance missing target','PASS distance event snapshot','PASS rejected invalid skill','PASS event snapshot','PASS saved') }
        else { $expected += @('PASS distance result after load') }
    } elseif ($Case -eq 'conversion') {
        if ($phase -eq 'fresh' -and !$PhobosPackageDirectory) { $expected=@('PASS legacy Phobos unavailable') }
        elseif ($phase -eq 'fresh') { $expected=@('PASS public Phobos API','PASS compatible fixture','PASS conversion','PASS identity and ownership','PASS house tracking','PASS passive retained','PASS passive action conversion','PASS infantry conversion','PASS aircraft conversion','PASS research conversion','PASS saved') }
        elseif ($phase -eq 'without-phobos') { $expected=@('PASS compatible fixture','PASS unsupported complex type skipped','PASS conversion','PASS identity and ownership','PASS house tracking','PASS passive retained','PASS passive action conversion','PASS infantry conversion','PASS aircraft conversion','PASS research conversion','PASS saved') }
        else { $expected=@('PASS loaded conversion','PASS loaded passive') }
        if ($phase -eq 'without-phobos') { $expected+=@('PASS builtin health and ammo') }
    } elseif ($Case -eq 'passive-animations') {
        $expected=@('PASS retained after save','PASS condition exit','PASS cloak hides animation','PASS uncloak restores animation','PASS removed passive clears animation')
        if ($phase -eq 'fresh') { $expected+=@('PASS configuration','PASS initial visibility','PASS condition and delay','PASS delayed activation and scale','PASS animation raster scales','PASS animation follows live scale','PASS saved') }
    } elseif ($Case -eq 'reapply') {
        $expected=if ($phase -eq 'fresh') { @('PASS first grant and opt out','PASS two actions two stacks','PASS no change no cooldown','PASS direct and area deduplicated','PASS independent hits in one frame','PASS capped renewal','PASS saved') } else { @('PASS restored stacks and duration','PASS expiry preserves other sources','PASS aura refresh is not an application','PASS reacquired at zero') }
    } elseif ($Case -eq 'passive-stacks') {
        $expected=@('PASS stack retained','PASS queued stack snapshot retained','PASS last source expired','PASS reacquire starts zero','PASS reacquire can gain','PASS explicit removal clears','PASS explicit reacquire starts zero','PASS transferred ownership retained','PASS batch remove skips absent and clears all matches')
        if ($phase -eq 'fresh') { $expected+=@('PASS stack formula namespace','PASS configuration validation','PASS initial zero','PASS cap and unit isolation','PASS absent target skipped','PASS modify another unit','PASS death stack snapshot','PASS decrement and formula','PASS minimum zero','PASS lost research clears stack','PASS event stack snapshot','PASS overlapping source retained','PASS steal list validation','PASS partial and zero stack theft','PASS acquisition sees stolen stacks','PASS steal respects capacity','PASS last stack removes only matching passive','PASS saved') }
    } elseif ($Case -eq 'passive-gained') {
        $expected = @('PASS no repeat after save','PASS pending gain','PASS reacquisition','PASS death reward','PASS actual damage points')
        if ($phase -eq 'fresh') { $expected += @('PASS inclusive thresholds','PASS research and spawn','PASS source renewal','PASS saved') }
    } elseif ($Case -eq 'spawn-passives') {
        $expected=@('PASS own children only','PASS child event','PASS withdraw and preserve other source','PASS reactivate','PASS regenerated child inherits')
        if ($phase -eq 'fresh') { $expected+=@('PASS validate references','PASS saved') }
    } elseif ($Case -eq 'research-cost') {
        $expected=@('PASS stacked price and category','PASS other player unchanged','PASS ownership')
        $expected+=@('PASS loaded production license','PASS licensed unit in production queue')
        if ($phase -eq 'fresh') { $expected+=@('PASS purchase production license','PASS license respects build limit','PASS reject invalid price options','PASS locked order','PASS refund paid credits','PASS factory matches quoted price','PASS saved') }
    } elseif ($Case -eq 'max-health') {
        $expected = @('PASS retained points','PASS minimum and clamp','PASS lethal')
        if ($phase -eq 'fresh') { $expected += @('PASS grant and isolation','PASS native repair','PASS exact damage','PASS no damage on reduction','PASS engine growth 19','PASS saved') }
        else { $expected += @('PASS engine count retained','PASS engine growth 20','PASS engine growth capped') }
    } elseif ($Case -eq 'immune') {
        $expected = @('PASS loaded immunity','PASS one source left','PASS immunity ends','PASS innate immunity preserved','PASS mind loaded','PASS mind one source left','PASS mind immunity ends','PASS mind capture after expiry','PASS mind bullet after expiry','PASS mind release after expiry')
        if ($phase -eq 'fresh') { $expected += @('PASS delayed entry','PASS same type isolation','PASS bypass','PASS native cursor','PASS mind delay','PASS mind isolation','PASS mind can capture','PASS mind direct blocked','PASS mind direct peer captured','PASS mind direct peer released','PASS mind bullet blocked','PASS mind peer captured','PASS mind peer released','PASS permanent mind bullet blocked','PASS permanent mind peer captured','PASS mind dominator blocked','PASS mind dominator peer captured','PASS saved') }
    } elseif ($Case -eq 'deployed-state') {
        $expected = @('PASS deployed after save','PASS packed up','PASS redeployed')
        if ($phase -eq 'fresh') { $expected += @('PASS initial posture','PASS deployed effects','PASS delay','PASS saved') }
    } elseif ($Case -eq 'aura') {
        $expected = if ($phase -eq 'fresh') { @('PASS configuration','PASS local range and overlap','PASS global filters','PASS atomic handoff','PASS saved') } else { @('PASS loaded sample','PASS sampled exit','PASS real reacquisition','PASS source destruction') }
    } elseif ($Case -eq 'power-state') {
        $expected = if ($phase -eq 'fresh') { @('PASS same passive halves output and drain','PASS stacked output and formula drain','PASS zero and withdrawal','PASS saved') } else { @('PASS restored grid','PASS owner transfer','PASS removed source') }
    } elseif ($Case -eq 'passenger-consume') {
        $expected=if ($phase -eq 'fresh') { @('PASS configuration','PASS two passengers','PASS invalid nested list remains intact','PASS nested subtree removed and sibling retained','PASS one passenger consumed','PASS passenger removed','PASS target cost and original event isolated','PASS removed target is not original enemy','PASS consumed event snapshot','PASS same action list steals direct passenger stacks','PASS queued abilities share remaining stacks and zero stack ownership','PASS consumed event reward','PASS empty skips chained reward','PASS open transport cleanup','PASS consume target carrier','PASS target carrier receives event','PASS building passenger consumed','PASS saved') } else { @('PASS load pending event','PASS loaded event uses passenger snapshot','PASS loaded remaining stacks and zero stack ownership','PASS no duplicate inherited stacks','PASS event once after load') }
    } elseif ($Case -eq 'transport-support') {
        $expected=if ($phase -eq 'fresh') { @('PASS configuration','PASS two passengers stack','PASS snapshot layers','PASS passenger withdrawal','PASS separate layers','PASS conditional support','PASS no stack','PASS passenger combat multipliers','PASS passenger stats isolated','PASS passenger healing clears combat bonus','PASS open topped passenger range boundary','PASS open topped shot damage','PASS saved') } else { @('PASS load sources','PASS load no repeated gain','PASS passenger combat multipliers','PASS detached limbo clears combat bonus','PASS unloaded passenger uses normal state','PASS load removal') }
    } elseif ($Case -eq 'signal') {
        $expected=if ($phase -eq 'fresh') { @('PASS configuration','PASS broadcast snapshots','PASS saved','PASS signal values','PASS dead target snapshot','PASS dead sender snapshot','PASS limited chain') } else { @('PASS signal values loaded','PASS once after load') }
    } elseif ($Case -eq 'promotion') {
        $expected=if ($phase -eq 'fresh') { @('PASS configuration','PASS baseline','PASS veteran','PASS elite','PASS direct elite','PASS specific only','PASS saved') } else { @('PASS queued promotion after load','PASS no repeated promotion') }
    } elseif ($Case -eq 'condition-formula') {
        $expected = if ($phase -eq 'fresh') { @('PASS configuration','PASS below stack threshold','PASS inclusive stack threshold','PASS stack withdrawal','PASS ammo passengers health true','PASS ammo below threshold','PASS empty passenger false','PASS health above threshold','PASS unlimited ammo minus one','PASS missing target false','PASS live target condition','PASS lost target false','PASS event condition snapshot','PASS event numeric formula snapshot','PASS saved') } else { @('PASS stack and condition loaded') }
    } elseif ($Case -eq 'before-damage') {
        $expected = if ($phase -eq 'fresh') { @('PASS config','PASS zero damage ignored','PASS lethal blocked','PASS blocked no damage event','PASS baseline','PASS second same frame hits','PASS unblocked damage event','PASS formula after armor','PASS formula chain','PASS filter ignored friendly','PASS second enemy hit blocked','PASS invalid formula skipped','PASS forced damage bypasses','PASS forced hit preserves charge','PASS block cooldown','PASS extra health lethal blocked','PASS extra health next hit','PASS missing attacker skips formula','PASS attacker health formula','PASS stack damage formula','PASS building lethal blocked','PASS outgoing config','PASS area per victim threshold','PASS outgoing before incoming','PASS no attacker skips finisher','PASS saved') } else { @('PASS trigger count loaded','PASS blocked charge remains spent','PASS outgoing grant loaded') }
    } elseif ($Case -eq 'before-fire') {
        $expected = if ($phase -eq 'fresh') { @('PASS replacement fired','PASS burst maintained','PASS remaining burst retained','PASS next volley restores original','PASS invalid target fallback','PASS range fallback','PASS onfire still triggers','PASS saved') } else { @('PASS volley restored','PASS volley shot after load') }
    } elseif ($Case -eq 'fire-rate') {
        $expected = @('PASS retained rate','PASS state exit','PASS lower limit')
        if ($phase -eq 'fresh') { $expected += @('PASS upper limit','PASS stacked rate','PASS burst cadence','PASS real shots','PASS saved') }
    } elseif ($Case -eq 'weapon-range') {
        $expected = @('PASS retained per-unit range','PASS state exit','PASS event lower limit')
        if ($phase -eq 'fresh') { $expected += @('PASS event upper limit','PASS two layers','PASS firing boundary','PASS acquired distant enemy','PASS shot at distant enemy','PASS saved') }
    } else {
        $expected = @('team line=3 success=1 members=2','frame 510','sf_fieldreload HTNK 0 = 1')
        if ($phase -eq 'fresh') { $expected += @('object action 1','saved 1') } else { $expected += 'boot 60' }
        $expected += @('sf_shotreward MTNK 0 = 2','sf_closedefense HTNK 0 = 10')
        $late = ($reportText -split '(?m)^frame 210\r?$',2)
        $end = ($reportText -split '(?m)^frame 510\r?$',2)
        if ($late.Count -ne 2 -or $late[1].Contains('sf_shotreward') -or
            $end.Count -ne 2 -or $end[1].Contains('sf_periodicrepair')) { throw 'Grant expiry check failed' }
    }
    foreach ($expectedLine in $expected) {
        if (!$reportText.Contains($expectedLine)) { throw "Missing '$expectedLine' in $report" }
    }
    if ($Case -eq 'hex-icons') {
        Copy-Item -LiteralPath (Join-Path $run 'hex-icons.ppm') -Destination $records -Force
    }
    if ($Case -eq 'research-hud') {
        if ($phase -eq 'fresh') {
            Copy-Item -LiteralPath (Join-Path $run 'research-cards.ppm') -Destination $records -Force
            Copy-Item -LiteralPath (Join-Path $run 'research-opening.ppm') -Destination $records -Force
        }
        Copy-Item -LiteralPath (Join-Path $run 'completed-badges.ppm') -Destination $records -Force
        Copy-Item -LiteralPath (Join-Path $run 'hud-half.ppm') -Destination $records -Force
    }
}
if ((Split-Path -Parent (Resolve-Path -LiteralPath $run).Path) -ne $runs) { throw 'Unexpected run directory' }
# The debugger can exit just before Windows releases the game's mapped DLLs.
for ($attempt=0; $attempt -lt 10; $attempt++) {
    try {
        Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction Stop
        break
    } catch {
        if ($attempt -eq 9) { throw }
        Start-Sleep -Seconds 1
    }
}
Write-Host "$Case passed ($($phases -join ', '))."
