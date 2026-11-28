param(
    [Parameter(Mandatory)][string]$Case,
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$PhobosPackageDirectory,
    [string]$StandaloneSpawner,
    [string]$SpawnerOverride,
    [string]$TornadoConfig,
    [switch]$Rebuild,
    [switch]$CopyGameAssets,
    [switch]$KeepGameAssets,
    [string]$ReportPath,
    [ValidateRange(10,1800)][int]$TimeoutSeconds=120
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Common.ps1')
$definition=Get-TestCase $Case
if($definition.Kind -ne 'game') {throw 'Use run-multiplayer.ps1 for a network case.'}
if($StandaloneSpawner -and ($PhobosPackageDirectory -or !$definition.Standalone)) {throw 'This case does not support the requested standalone extension combination.'}
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
if ($Case -like 'hex-*') {
    $hexRoot=if ($env:STARFALL_HEXBRAWL_ROOT) { $env:STARFALL_HEXBRAWL_ROOT } else { Join-Path (Split-Path $root -Parent) 'HexBrawl' }
    $hexConfig=Join-Path $hexRoot 'Starfall.HexBrawl.PCX.ini'
    if (!(Test-Path -LiteralPath $hexConfig -PathType Leaf)) {
        throw 'HexBrawl project missing. Place it next to Starfall or set STARFALL_HEXBRAWL_ROOT to its directory.'
    }
}
foreach ($name in 'Syringe.exe', 'gamemd.exe') {
    if (!(Test-Path -LiteralPath (Join-Path $game $name) -PathType Leaf)) { throw "Missing $name in $game" }
}
& (Join-Path $PSScriptRoot 'build.ps1') -Case $Case -Rebuild:$Rebuild

$runs = (New-Item -Force -ItemType Directory (Join-Path $PSScriptRoot 'local/runs')).FullName
$run = (New-Item -ItemType Directory (Join-Path $runs "$Case-$([Guid]::NewGuid().ToString('N'))")).FullName
New-Item -ItemType File (Join-Path $run 'Starfall.test') | Out-Null
$runResult=[ordered]@{Case=$Case;Run=$run;Result='RUNNING';Started=(Get-Date).ToString('o');Phases=@()}
$runWatch=[Diagnostics.Stopwatch]::StartNew()
try {
$fixture = Join-Path $PSScriptRoot "game/$Case"
$map=Join-Path $PSScriptRoot $definition.Map
$save=$definition.Save
$logName=$definition.Log
Write-Host "Test directory: $run"
Copy-TestGameFiles $game $run -CopyArchives:$CopyGameAssets
if($SpawnerOverride) {
    if($StandaloneSpawner) {throw 'Choose either standalone or regular extension spawner override.'}
    Copy-Item -LiteralPath $SpawnerOverride -Destination (Join-Path $run 'CnCNet-Spawner.dll')
    [IO.File]::AppendAllText((Join-Path $run 'RA2MD.ini'),"`r`n[Debug]`r`nSkipCreateAppMutex=yes`r`n")
}
if ($StandaloneSpawner) {
    Copy-Item -LiteralPath $StandaloneSpawner -Destination (Join-Path $run 'CnCNet-Spawner.dll')
}
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
if ($Case -eq 'multi-shot') {
    foreach ($kind in 'rules','art') {
        Copy-Item -LiteralPath (Join-Path $fixture "Tracer.$kind.ini") -Destination $run
        [IO.File]::WriteAllText((Join-Path $run "${kind}_include.ini"),"[#include]`r`n1=${kind}_triumph.ini`r`n2=Tracer.$kind.ini`r`n")
    }
}
if ($Case -in 'conversion','spawn-docks','ground-docks','projectile-blocker','piercing','projectile-script','build-nodes') {
    Copy-Item -LiteralPath (Join-Path $fixture 'SpawnRules.ini') -Destination $run
    [IO.File]::WriteAllText((Join-Path $run 'rules_include.ini'),"[#include]`r`n1=rules_triumph.ini`r`n2=SpawnRules.ini`r`n")
}
if ($Case -eq 'projectile-script') {
    Copy-Item -LiteralPath (Join-Path $fixture 'TestFlight.lua') -Destination $run
    Copy-Item -LiteralPath (Join-Path $root 'examples/ProjectileScript/ReturnWave.lua') -Destination $run
    Copy-Item -LiteralPath (Join-Path $root 'examples/ProjectileScript/GroundBlastLine.lua') -Destination $run
    Copy-Item -LiteralPath (Join-Path $fixture 'Trail.ini') -Destination $run
    [IO.File]::WriteAllLines((Join-Path $run 'art_include.ini'),@('[#include]','1=art_triumph.ini','2=Trail.ini'))
}
if ($Case -eq 'entity-walls') {
    Copy-Item -LiteralPath (Join-Path $root 'examples/EntityWalls/EntityWalls.rules.ini') -Destination $run
    Copy-Item -LiteralPath (Join-Path $fixture 'WallProbe.rules.ini') -Destination $run
    [IO.File]::WriteAllText((Join-Path $run 'rules_include.ini'),"[#include]`r`n1=rules_triumph.ini`r`n2=EntityWalls.rules.ini`r`n3=WallProbe.rules.ini`r`n")
}
if ($Case -eq 'actsp') {
    Copy-Item -LiteralPath (Join-Path $fixture 'Guard.lua') -Destination $run
    # The test game's rulesmd.ini includes rules_include.ini through Ares.
    $rules=[IO.File]::ReadAllText((Join-Path $fixture 'rules_include.ini')).Replace('1=rules_triumph.ini',"1=rules_triumph.ini`r`n2=GrizzlyOutpost.rules.ini")
    [IO.File]::WriteAllText((Join-Path $run 'rules_include.ini'),$rules)
    $grizzly=Join-Path $root 'examples/GrizzlyOutpost'
    Copy-Item -LiteralPath (Join-Path $root 'examples/GrizzlyGroundOutpost/GrizzlyGround.rules.ini') -Destination $run
    foreach ($name in 'GrizzlyOutpost.rules.ini','GrizzlyOutpost.art.ini','asgrdrn.vxl','asgrdrn.hva') {
        Copy-Item -LiteralPath (Join-Path $grizzly $name) -Destination $run
    }
    [IO.File]::WriteAllText((Join-Path $run 'art_include.ini'),"[#include]`r`n1=art_triumph.ini`r`n2=GrizzlyOutpost.art.ini`r`n")
}
if ($Case -eq 'high-platform') {
    Copy-Item -LiteralPath (Join-Path $fixture 'rules_include.ini') -Destination $run
    [IO.File]::WriteAllText((Join-Path $run 'art_include.ini'),"[#include]`r`n1=art_triumph.ini`r`n2=HighPlatform.art.ini`r`n3=Rectangle.art.ini`r`n")
    $platform=Join-Path $root 'examples/HighPlatform'
    foreach ($name in 'HighPlatform.rules.ini','HighPlatform.art.ini','ashighpl.shp','ashplmk.shp','aspltem.pal','asplsno.pal','asplurb.pal','aspldes.pal','asplubn.pal','aspllun.pal') {
        Copy-Item -LiteralPath (Join-Path $platform $name) -Destination $run
    }
    Copy-Item -LiteralPath (Join-Path $platform 'ashighpl.shp') -Destination (Join-Path $run 'asrectpl.shp')
    $rectangle=@('[ASRECTPL]')+@(& (Join-Path $root 'tools/art/Write-HighPlatformFoundation.ps1') -Width 8 -Height 5)+
        @('Height=4','CanHideThings=no','NewTheater=no','Palette=aspl')
    [IO.File]::WriteAllLines((Join-Path $run 'Rectangle.art.ini'),[string[]]$rectangle)
}
if ($Case -eq 'research-grant') {
    Copy-Item -LiteralPath (Join-Path $root "build/tests/$Case/ResearchApiClient.dll") -Destination $run
}
$loaderText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $run 'Syringe.exe')))
if (!$loaderText.Contains('-i=') -or !$loaderText.Contains('--nodetach') -or !$loaderText.Contains('--args')) {
    throw 'These tests require SyringeEx with explicit DLL imports, --args and --nodetach. Supply a compatible loader in GameDirectory or PhobosPackageDirectory.'
}
if ($Case -like 'hex-*') {
    Copy-Item -LiteralPath $hexConfig -Destination (Join-Path $run 'Starfall.ini')
    foreach ($name in 'HexBrawl.lua','HexBrawl.rules.ini','HexBrawl.art.ini') {
        Copy-Item -LiteralPath (Join-Path $hexRoot $name) -Destination $run
    }
    $effects = @{
        'blind_effect.shp'='hxblind.shp'; 'pierce_effect.shp'='hxpierce.shp'; 'slow_effect.shp'='hxslow.shp'
        'shield_effect.shp'='hxshield.shp'; 'shield_block_effect.shp'='hxshblk.shp'; 'deathwill_effect.shp'='hxlegacy.shp'
    }
    foreach ($name in $effects.Keys) {
        Copy-Item -LiteralPath (Join-Path $hexRoot "assets/vfx/$name") -Destination (Join-Path $run $effects[$name])
    }
    # Expedition's rulesmd.ini and artmd.ini load these includes from the game root.
    foreach ($kind in 'rules','art') {
        $include=Join-Path $run "${kind}_include.ini"
        [IO.File]::WriteAllText($include,"[#include]`r`n1=${kind}_triumph.ini`r`n2=HexBrawl.$kind.ini`r`n")
    }
}
if ($Case -eq 'hex-icons') {
    Get-ChildItem -LiteralPath (Join-Path $hexRoot 'HexBrawlIconsPCX') -Filter '*.pcx' | Copy-Item -Destination $run
    Copy-Item -LiteralPath (Join-Path $fixture 'hx-test.pcx') -Destination (Join-Path $run 'hx-test.pcx')
} elseif ($Case -in 'hex-gameplay','hex-fortress','hex-helicopter','hex-defense','hex-economy','hex-ordnance','hex-support','hex-tactics') {
    if($Case -eq 'hex-economy') {
        $path=Join-Path $run 'Starfall.ini'
        $text=[IO.File]::ReadAllText($path).Replace('[Starfall.PassiveTypes]',"[Starfall.PassiveTypes]`r`n9998=InvalidLiveEvent`r`n9999=InvalidLiveAction")
        $text += [Environment]::NewLine+[IO.File]::ReadAllText((Join-Path $fixture 'Starfall.ini'))
        [IO.File]::WriteAllText($path,$text)
    }
    if($Case -eq 'hex-helicopter') {
        $path=Join-Path $run 'Starfall.ini'
        $text=[IO.File]::ReadAllText($path).Replace('[Starfall.PassiveTypes]',"[Starfall.PassiveTypes]`r`n9999=HelicopterTestShots")
        $text += [Environment]::NewLine+[IO.File]::ReadAllText((Join-Path $fixture 'Starfall.ini'))
        [IO.File]::WriteAllText($path,$text)
    }
} elseif ($Case -eq 'unit-orders') {
    Copy-Item -LiteralPath (Join-Path $fixture 'uimd.ini') -Destination (Join-Path $run 'uimd.ini') -Force
    Copy-Item -LiteralPath (Join-Path $root 'examples/AreaAttack/AreaAttack.shp') -Destination (Join-Path $run 'AreaAttack.shp')
    Copy-Item -LiteralPath (Join-Path $root 'examples/AutoEnter/AutoBoard.shp') -Destination (Join-Path $run 'TestBoard.shp')
    foreach($buttonFile in 'IconProbe.shp','MissingProbe.shp') {
        Copy-Item -LiteralPath (Join-Path $fixture 'TestButton.shp') -Destination (Join-Path $run $buttonFile)
    }
    $example=[IO.File]::ReadAllText((Join-Path $root 'examples/AreaAttack/Starfall.ini'))
    [IO.File]::WriteAllText((Join-Path $run 'AreaAttack.rules.ini'),$example)
    [IO.File]::WriteAllText((Join-Path $run 'rules_include.ini'),"[#include]`r`n1=rules_triumph.ini`r`n2=AreaAttack.rules.ini`r`n")
    $extra=[IO.File]::ReadAllText((Join-Path $fixture 'Probe.ini'))
    [IO.File]::WriteAllText((Join-Path $run 'Starfall.ini'),$extra)
    Copy-Item -LiteralPath (Join-Path $fixture 'OrderProbe.lua') -Destination $run
} elseif ($Case -eq 'example-starters') {
    $configs=foreach ($exampleName in 'Passive','Research','WeaponScript') {
        [IO.File]::ReadAllText((Join-Path $root "examples/$exampleName/Starfall.ini"))
    }
    [IO.File]::WriteAllText((Join-Path $run 'Starfall.ini'), ($configs -join [Environment]::NewLine))
    Copy-Item -LiteralPath (Join-Path $root 'examples/WeaponScript/DemoDoubleShot.lua') -Destination $run
} elseif ($Case -eq 'action-sequence') {
    $scripts=Get-Item -LiteralPath $run
    Copy-Item -LiteralPath (Join-Path $fixture 'GravityWell.lua') -Destination $scripts.FullName
    Copy-Item -LiteralPath (Join-Path $fixture 'Presentation.lua') -Destination $scripts.FullName
    Copy-Item -LiteralPath (Join-Path $fixture 'Presentation.ini') -Destination (Join-Path $run 'Starfall.ini')
} elseif ($Case -eq 'tornado') {
    $scripts=Get-Item -LiteralPath $run
    if ($TornadoConfig) {
        $userScripts=$game
        if (Test-Path -LiteralPath $userScripts -PathType Container) {
            Get-ChildItem -LiteralPath $userScripts -Filter '*.lua' -File | Copy-Item -Destination $scripts.FullName
        }
    }
    Copy-Item -LiteralPath (Join-Path $fixture 'Tornado.lua') -Destination $scripts.FullName
    Copy-Item -LiteralPath (Join-Path $fixture 'tornado_attack.wav') -Destination $run
    $config=if ($TornadoConfig) { (Resolve-Path -LiteralPath $TornadoConfig).Path } else { Join-Path $fixture 'Starfall.ini' }
    $rules=[IO.File]::ReadAllText($config)
    $section=[regex]::new('(?im)^\[SF_Tornado\][^\r\n]*\r?\n')
    if($section.Matches($rules).Count -ne 1) { throw 'Expected one SF_Tornado section' }
    $rules=$section.Replace($rules,('$0'+'Script.Param.BlowWeight=999'+[Environment]::NewLine),1)
    [IO.File]::WriteAllText((Join-Path $run 'Starfall.ini'),$rules)
} elseif ($Case -eq 'lua-anim') {
    $scripts=Get-Item -LiteralPath $run
    Copy-Item -LiteralPath (Join-Path $fixture 'AnimationCheck.lua') -Destination $scripts.FullName
    Copy-Item -LiteralPath (Join-Path $fixture 'Starfall.ini') -Destination $run
} elseif ($Case -in 'area-barrage','active-skills','lua-skills','lua-timers') {
    $sharedLua=Join-Path $PSScriptRoot 'resources/lua-skills'
    $scripts=Get-Item -LiteralPath $run
    Get-ChildItem -LiteralPath $sharedLua -Filter '*.lua' | Copy-Item -Destination $scripts.FullName
    Get-ChildItem -LiteralPath $fixture -Filter '*.lua' | Copy-Item -Destination $scripts.FullName
    $rules=[IO.File]::ReadAllText((Join-Path $sharedLua 'Starfall.ini'))
    $fixtureRules=Join-Path $fixture 'Starfall.ini'
    if(Test-Path -LiteralPath $fixtureRules) {
        $extra=[IO.File]::ReadAllText($fixtureRules)
        $registry=[regex]::Match($extra,'(?im)^\[Starfall\.PassiveTypes\][^\[]*')
        if($registry.Success) {
            $rules=$rules.Replace('[Starfall.PassiveTypes]',$registry.Value.TrimEnd())
            $extra=$extra.Remove($registry.Index,$registry.Length)
        }
        $rules += [Environment]::NewLine + $extra
    }
    [IO.File]::WriteAllText((Join-Path $run 'Starfall.ini'),$rules)
} else {
    Copy-Item -LiteralPath (Join-Path $fixture 'Starfall.ini') -Destination $run
}
if ($Case -in 'taunt','chrono-teleport','force-move','vehicle-jump','passenger-ejection','building-upgrade','spawn-building','building-transform','unit-mount','weapon-script','map-lua','reward-text-map','research-grant','ground-pickups','passenger-consume') {
    $scripts=Get-Item -LiteralPath $run
    Get-ChildItem -LiteralPath $fixture -Filter '*.lua' | Copy-Item -Destination $scripts.FullName
}
if ($Case -eq 'research-hud') {
    Copy-Item -LiteralPath (Join-Path $fixture 'ui-test.wav') -Destination $run
}
Copy-Item -LiteralPath $map -Destination (Join-Path $run 'spawnmap.ini')
if ($Case -eq 'unit-orders') {
    $scenarioFile=Join-Path $run 'spawnmap.ini'
    $scenarioText=[IO.File]::ReadAllText($scenarioFile).Replace('0=Neutral,V3,','0=Neutral,DRED,')
    $scenarioText += "`r`n[FV]`r`nPassengers=2`r`n"
    [IO.File]::WriteAllText($scenarioFile,$scenarioText)
}
if ($Case -eq 'taunt-team') {
    $scenarioFile=Join-Path $run 'spawnmap.ini'
    $scenarioText=[IO.File]::ReadAllText($scenarioFile).Replace('[Waypoints]',"[Waypoints]`r`n20=89067`r`n21=78071")
    [IO.File]::WriteAllText($scenarioFile,$scenarioText)
}
if ($Case -eq 'hex-gameplay') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), [Environment]::NewLine+[IO.File]::ReadAllText((Join-Path $fixture 'EnemyTeam.ini')))
}
if ($Case -eq 'building-upgrade') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[GAPOWR]`r`nUpgrades=3`r`n[GAPILE]`r`nPowersUpBuilding=GAPOWR`r`n")
}
if ($Case -eq 'action-sequence') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), [Environment]::NewLine+[IO.File]::ReadAllText((Join-Path $fixture 'Rules.ini'))+[Environment]::NewLine+[IO.File]::ReadAllText((Join-Path $fixture 'Damage.ini')))
}
if ($Case -eq 'research-cost') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[CLEG]`r`nTechLevel=-1`r`n")
}
if ($Case -eq 'weapon-script') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n"+[IO.File]::ReadAllText((Join-Path $fixture 'Weapon.ini')))
}
if ($Case -eq 'before-fire') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[120mm]`r`nBurst=3`r`n")
}
if ($Case -in 'spawn-passives','cloakable','actsp','taunt-team') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n"+[IO.File]::ReadAllText((Join-Path $fixture 'SpawnRules.ini')))
}
New-Item -ItemType Directory (Join-Path $run 'Saved Games') | Out-Null
$spawn = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'game/spawn.ini'))
if($SpawnerOverride) {
    # Most probes pass "Saved Games\\name.sav" to the native save function.
    # The modern spawner prefixes SavedGameDir on both save and load.
    $spawn=$spawn.Replace('[Settings]',"[Settings]`r`nSavedGameDir=.")
    $save='Saved Games\'+$save
}
$records = (New-Item -Force -ItemType Directory (Join-Path $PSScriptRoot 'local/records')).FullName
if ($Case -eq 'action-sequence') { $spawn=$spawn.Replace('Superweapons=False','Superweapons=True') }
$freshSpawn=$spawn
$phases=if($PhobosPackageDirectory -and $definition.PSObject.Properties['PhobosPhases']) {@($definition.PhobosPhases)} else {@($definition.Phases)}
foreach ($phase in $phases) {
    $phaseWatch=[Diagnostics.Stopwatch]::StartNew()
    if ($Case -in 'hex-defense','hex-economy','hex-ordnance','hex-support','hex-tactics') {
        if ($phase -eq 'art') { New-Item -ItemType File (Join-Path $run 'art-export.flag') | Out-Null }
        else { Remove-Item -LiteralPath (Join-Path $run 'art-export.flag') -ErrorAction SilentlyContinue }
    }
    if ($phase -in 'load','without-phobos-load') {
        if($SpawnerOverride -and !(Test-Path -LiteralPath (Join-Path $run $save) -PathType Leaf)) {
            # Older standalone-aware probes explicitly save into the run root.
            # Resolve the actual file before starting the modern load screen.
            if(Test-Path -LiteralPath (Join-Path $run $definition.Save) -PathType Leaf) {$save=$definition.Save}
            else {throw "Missing save for $Case/$phase`: $save"}
        }
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
    $modules=@('Starfall.dll')
    $extensions=if ($StandaloneSpawner) { @('CnCNet-Spawner.dll') } elseif($SpawnerOverride) { @('Ares.dll','CnCNet-Spawner.dll','Phobos.dll') } elseif ($Case -eq 'action-sequence' -and !$PhobosPackageDirectory) { @('cncnet5.dll') } else { @('Ares.dll','cncnet5.dll','Phobos.dll') }
    $modules+=@($extensions | Where-Object { Test-Path -LiteralPath (Join-Path $run $_) -PathType Leaf })
    $imports=($modules | ForEach-Object { '-i='+$_ }) -join ' '
    $gameArgs=if ($StandaloneSpawner -or $SpawnerOverride) {'-SPAWN -LOG -CD'} else {'-SPAWN -LOG -CD -WIN'}
    $arguments='gamemd.exe '+$imports+' --args="'+$gameArgs+'" --nodetach'
    Write-Host "$Case/$phase module order: $($modules -join ' -> ')"
    & (Join-Path $PSScriptRoot 'check-hook-ranges.ps1') -Primary (Join-Path $run 'Starfall.dll') -Extensions @($modules | Select-Object -Skip 1 | ForEach-Object { Join-Path $run $_ })
    $proc = Start-Process -FilePath (Join-Path $run 'Syringe.exe') -ArgumentList $arguments -WorkingDirectory $run -WindowStyle Hidden -PassThru
    $child=$null
    $watch=[Diagnostics.Stopwatch]::StartNew()
    do {
        if (!$child) {
            $child=Get-Process -Name gamemd -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $run 'gamemd.exe') } | Select-Object -First 1
            if ($child) { try { $null=$child.Handle } catch { $child=$null } }
        }
        $launcherDone=$proc.WaitForExit(100)
        $finished=$launcherDone -and (!$child -or $child.HasExited)
    } while (!$finished -and $watch.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    if (!$finished) {
        Get-Process -Name gamemd,Syringe -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -in (Join-Path $run 'gamemd.exe'),(Join-Path $run 'Syringe.exe')
        } | Stop-Process -Force
        throw "Game timed out in $run"
    }
    $reportText = [IO.File]::ReadAllText($report)
    Copy-Item -LiteralPath $report -Destination (Join-Path $records "$Case-$phase.log") -Force
    Copy-Item -LiteralPath $report -Destination (Join-Path $run "$phase.log") -Force
    if ($reportText -match '(?m)^FAIL[^\r\n]*') { throw "$Case/$phase`: $($Matches[0]); report: $report" }
    if ($child -and $child.ExitCode -ne 0) { throw "$Case/$phase`: game exited $($child.ExitCode); report: $report" }
    if ($proc.ExitCode -ne 0) { throw "$Case/$phase`: loader exited $($proc.ExitCode); report: $report" }
    Assert-TestReport $definition $phase $reportText ([bool]$PhobosPackageDirectory)
    $runResult.Phases+=@{Name=$phase;Result='PASS';Seconds=[math]::Round($phaseWatch.Elapsed.TotalSeconds,3);ExpectedMessages=@(Get-TestExpected $definition $phase ([bool]$PhobosPackageDirectory)).Count;Modules=$modules}
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
$runResult.Result='PASS'
Write-Host "$Case passed ($($phases -join ', '))."
} catch {
    $runResult.Result='FAIL';$runResult.Reason=$_.Exception.Message
    throw
} finally {
    Get-Process -Name gamemd,Syringe -ErrorAction SilentlyContinue | Where-Object {$_.Path -in (Join-Path $run 'gamemd.exe'),(Join-Path $run 'Syringe.exe')} | Stop-Process -Force
    $runResult.Seconds=[math]::Round($runWatch.Elapsed.TotalSeconds,3)
    $runResult | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8
    if($ReportPath) {Copy-Item -LiteralPath (Join-Path $run 'result.json') -Destination $ReportPath -Force}
    if(!$KeepGameAssets) {Remove-TestArchives $run}
}
