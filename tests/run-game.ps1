param(
    [Parameter(Mandatory)][string]$Case,
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$PhobosPackageDirectory,
    [string]$StandaloneSpawner,
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
$loaderText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $run 'Syringe.exe')))
if (!$loaderText.Contains('-i=') -or !$loaderText.Contains('--nodetach') -or !$loaderText.Contains('--args')) {
    throw 'These tests require SyringeEx with explicit DLL imports, --args and --nodetach. Supply a compatible loader in GameDirectory or PhobosPackageDirectory.'
}
if ($Case -eq 'hex-icons') {
    Copy-Item -LiteralPath (Join-Path $root 'examples/Starfall.HexBrawl.ini') -Destination (Join-Path $run 'Starfall.ini')
    Copy-Item -LiteralPath (Join-Path $root 'examples/HexBrawlIcons') -Destination $run -Recurse
    Copy-Item -LiteralPath (Join-Path $fixture 'hx-test.pcx') -Destination (Join-Path $run 'hx-test.pcx')
} elseif ($Case -eq 'action-sequence') {
    $example=Join-Path $root 'examples/ActionSequence'
    $scripts=Get-Item -LiteralPath $run
    Copy-Item -LiteralPath (Join-Path $example 'GravityWell.lua') -Destination $scripts.FullName
    Copy-Item -LiteralPath (Join-Path $fixture 'Presentation.lua') -Destination $scripts.FullName
    Copy-Item -LiteralPath (Join-Path $fixture 'Presentation.ini') -Destination (Join-Path $run 'Starfall.ini')
} elseif ($Case -eq 'tornado') {
    $example=Join-Path $root 'examples/Tornado'
    $scripts=Get-Item -LiteralPath $run
    if ($TornadoConfig) {
        $userScripts=$game
        if (Test-Path -LiteralPath $userScripts -PathType Container) {
            Get-ChildItem -LiteralPath $userScripts -Filter '*.lua' -File | Copy-Item -Destination $scripts.FullName
        }
    }
    Copy-Item -LiteralPath (Join-Path $example 'Tornado.lua') -Destination $scripts.FullName
    Copy-Item -LiteralPath (Join-Path $example 'tornado_attack.wav') -Destination $run
    $config=if ($TornadoConfig) { (Resolve-Path -LiteralPath $TornadoConfig).Path } else { Join-Path $example 'Starfall.ini' }
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
    $examples=Join-Path $root 'examples/Lua'
    $scripts=Get-Item -LiteralPath $run
    Get-ChildItem -LiteralPath $examples -Filter '*.lua' | Copy-Item -Destination $scripts.FullName
    Get-ChildItem -LiteralPath $fixture -Filter '*.lua' | Copy-Item -Destination $scripts.FullName
    $rules=[IO.File]::ReadAllText((Join-Path $examples 'Starfall.ini'))
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
if ($Case -in 'chrono-teleport','force-move','building-upgrade','map-lua') {
    $scripts=Get-Item -LiteralPath $run
    Get-ChildItem -LiteralPath $fixture -Filter '*.lua' | Copy-Item -Destination $scripts.FullName
}
if ($Case -eq 'research-hud') {
    Copy-Item -LiteralPath (Join-Path $fixture 'ui-test.wav') -Destination $run
}
Copy-Item -LiteralPath $map -Destination (Join-Path $run 'spawnmap.ini')
if ($Case -eq 'building-upgrade') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[GAPOWR]`r`nUpgrades=3`r`n[GAPILE]`r`nPowersUpBuilding=GAPOWR`r`n")
}
if ($Case -eq 'action-sequence') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), [Environment]::NewLine+[IO.File]::ReadAllText((Join-Path $example 'Rules.ini'))+[Environment]::NewLine+[IO.File]::ReadAllText((Join-Path $fixture 'Damage.ini')))
}
if ($Case -eq 'research-cost') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[CLEG]`r`nTechLevel=-1`r`n")
}
if ($Case -eq 'before-fire') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n[120mm]`r`nBurst=3`r`n")
}
if ($Case -in 'spawn-passives','cloakable') {
    [IO.File]::AppendAllText((Join-Path $run 'spawnmap.ini'), "`r`n"+[IO.File]::ReadAllText((Join-Path $fixture 'SpawnRules.ini')))
}
New-Item -ItemType Directory (Join-Path $run 'Saved Games') | Out-Null
$spawn = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'game/spawn.ini'))
$records = (New-Item -Force -ItemType Directory (Join-Path $PSScriptRoot 'local/records')).FullName
if ($Case -eq 'action-sequence') { $spawn=$spawn.Replace('Superweapons=False','Superweapons=True') }
$freshSpawn=$spawn
$phases=if($PhobosPackageDirectory -and $definition.PSObject.Properties['PhobosPhases']) {@($definition.PhobosPhases)} else {@($definition.Phases)}
foreach ($phase in $phases) {
    $phaseWatch=[Diagnostics.Stopwatch]::StartNew()
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
    $modules=@('Starfall.dll')
    $extensions=if ($StandaloneSpawner) { @('CnCNet-Spawner.dll') } elseif ($Case -eq 'action-sequence' -and !$PhobosPackageDirectory) { @('cncnet5.dll') } else { @('Ares.dll','cncnet5.dll','Phobos.dll') }
    $modules+=@($extensions | Where-Object { Test-Path -LiteralPath (Join-Path $run $_) -PathType Leaf })
    $imports=($modules | ForEach-Object { '-i='+$_ }) -join ' '
    $gameArgs=if ($StandaloneSpawner) {'-SPAWN -LOG -CD'} else {'-SPAWN -LOG -CD -WIN'}
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
