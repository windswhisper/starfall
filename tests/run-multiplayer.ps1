param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$PhobosPackageDirectory,
    [Parameter(Mandatory)][string]$StandaloneSpawner,
    [ValidateRange(1024,65532)][int]$Port=31001,
    [int]$Seed=1804805662,
    [int]$TimeoutSeconds=180,
    [ValidateSet('Direct','Impaired')][string]$NetworkProfile='Direct',
    [switch]$InjectDivergence,
    [switch]$SwapPlayers,
    [switch]$KeepGameAssets,
    [switch]$SkipBuild,
    [switch]$Rebuild,
    [switch]$CopyGameAssets,
    [string]$ReportPath
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'lib/Common.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$game=(Resolve-Path -LiteralPath $GameDirectory).Path
$package=(Resolve-Path -LiteralPath $PhobosPackageDirectory).Path
& (Join-Path $PSScriptRoot 'build.ps1') -Case multiplayer -Rebuild:$Rebuild -CheckOnly:$SkipBuild
$run=Join-Path $PSScriptRoot ('local/runs/multiplayer-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory $run
Write-Output "Multiplayer directory: $run"
$processes=@()
$dirs=@()
$relay=$null
$compared=0
function Compare-Samples {
    $files=@($dirs | ForEach-Object {Join-Path $_ 'snapshots.csv'})
    if($files.Count -ne 2 -or !(Test-Path -LiteralPath $files[0]) -or !(Test-Path -LiteralPath $files[1])) {return}
    $left=@(Get-Content -LiteralPath $files[0] | Where-Object {$_ -match '^\d+,[0-9a-f]+,[0-9a-f]+$'})
    $right=@(Get-Content -LiteralPath $files[1] | Where-Object {$_ -match '^\d+,[0-9a-f]+,[0-9a-f]+$'})
    for($sample=$script:compared;$sample -lt [Math]::Min($left.Count,$right.Count);$sample++) {
        if($left[$sample] -ne $right[$sample]) {throw "Peers differ: $($left[$sample]) versus $($right[$sample]); $run"}
        $script:compared++
    }
}
try {
    foreach ($peer in 0..1) {
        $dir=Join-Path $run ('peer'+$peer)
        $null=New-Item -ItemType Directory $dir
        $null=New-Item -ItemType File (Join-Path $dir 'Starfall.test')
        $dirs+=$dir
        Copy-TestGameFiles $game $dir -CopyArchives:$CopyGameAssets
        foreach ($name in 'Phobos.dll','Syringe.exe','gamemd.edb') { Copy-Item -LiteralPath (Join-Path $package $name) -Destination $dir -Force }
        Copy-Item -LiteralPath (Join-Path $root 'build/tests/multiplayer/Starfall.dll') -Destination $dir
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'game/multiplayer/Starfall.ini') -Destination $dir
        $scripts=Get-Item -LiteralPath $dir
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'game/multiplayer/Network.lua') -Destination $scripts.FullName
        $map=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'maps/Starfall_TestRange.map'))
        # Retain terrain and spawn positions; remove the single-player target range
        # combatants and triggers so they cannot destroy network fixture objects.
        $map=[regex]::Replace($map,'(?ms)^\[(?:Units|Infantry|Structures|Triggers|Tags|Events|Actions|TeamTypes|TaskForces|ScriptTypes)\]\r?\n.*?(?=^\[|\z)','')
        [IO.File]::WriteAllText((Join-Path $dir 'spawnmap.ini'),$map)
        if($InjectDivergence -and $peer -eq 1) {$null=New-Item -ItemType File (Join-Path $dir 'diverge.flag')}
        $spawn=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'game/spawn.ini'))
        $color=if($SwapPlayers) {4+$peer} else {5-$peer}
        $spawn=$spawn.Replace('PlayerCount=1','PlayerCount=2').Replace('AIPlayers=1','AIPlayers=0').Replace('UnitCount=10','UnitCount=0').Replace('Color=5',('Color='+$color)).Replace('Seed=1804805662',('Seed='+$Seed))
        $spawn=[regex]::Replace($spawn,'(?s)\[HouseHandicaps\].*?(?=\[SpawnLocations\])','')
        $spawn=$spawn.Replace('Name=5b4e657720506c61',('Name=StarfallPeer'+$peer))
        $spawn=$spawn.Replace('[Settings]',("[Settings]`r`nPort="+($Port+$peer)+"`r`nFrameSendRate=3`r`nMaxAhead=12`r`nProtocol=0`r`n"))
        $otherPort=if($NetworkProfile -eq 'Impaired') {$Port+2+$peer} else {$Port+1-$peer}
        $spawn+="`r`n[Other1]`r`nName=StarfallPeer$((1-$peer))`r`nSide=0`r`nColor=$((9-$color))`r`nIp=127.0.0.1`r`nPort=$otherPort`r`nIsSpectator=False`r`n"
        [IO.File]::WriteAllText((Join-Path $dir 'spawn.ini'),$spawn)
        [IO.File]::AppendAllText((Join-Path $dir 'RA2MD.ini'),"`r`n[Debug]`r`nSkipCreateAppMutex=yes`r`nMPDebug=yes`r`nMPDebug.Show=no`r`n")
        Copy-Item -LiteralPath $StandaloneSpawner -Destination (Join-Path $dir 'CnCNet-Spawner.dll')
        $modules=@('Starfall.dll','Ares.dll','CnCNet-Spawner.dll','Phobos.dll')
        & (Join-Path $PSScriptRoot 'check-hook-ranges.ps1') -Primary (Join-Path $dir 'Starfall.dll') -Extensions @($modules | Select-Object -Skip 1 | ForEach-Object {Join-Path $dir $_})
    }
    $identities=@('Starfall.dll','Ares.dll','Phobos.dll','gamemd.exe','Syringe.exe','Starfall.ini','spawnmap.ini','Network.lua')
    $identities+='CnCNet-Spawner.dll'
    $fingerprints=@(foreach($name in $identities) {
        $left=(Get-FileHash -LiteralPath (Join-Path $dirs[0] $name)).Hash
        $right=(Get-FileHash -LiteralPath (Join-Path $dirs[1] $name)).Hash
        if($left -ne $right) {throw "Peer input differs: $name"}
        [pscustomobject]@{File=$name;SHA256=$left}
    })
    [pscustomobject]@{Seed=$Seed;SwappedPlayers=[bool]$SwapPlayers;NetworkProfile=$NetworkProfile;InjectedDivergence=[bool]$InjectDivergence;IdenticalInputs=$fingerprints} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $run 'inputs.json')
    if($NetworkProfile -eq 'Impaired') {
        $relayScript=Join-Path $PSScriptRoot 'network/run-relay.ps1'
        $relay=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList @('-NoProfile','-File',('"'+$relayScript+'"'),'-Port',$Port,'-Directory',('"'+$run+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'relay-stdout.log') -RedirectStandardError (Join-Path $run 'relay-stderr.log')
        $readyDeadline=(Get-Date).AddSeconds(15)
        while(!(Test-Path (Join-Path $run 'relay.ready')) -and !$relay.HasExited -and (Get-Date) -lt $readyDeadline) {Start-Sleep -Milliseconds 100}
        if(!(Test-Path (Join-Path $run 'relay.ready'))) {throw "Loopback relay failed: $run"}
    }
    foreach($dir in $dirs) {
        $imports=($modules | ForEach-Object {'-i='+$_}) -join ' '
        $arguments='gamemd.exe '+$imports+' --args="-SPAWN -LOG -CD" --nodetach'
        $processes+=Start-Process -FilePath (Join-Path $dir 'Syringe.exe') -ArgumentList $arguments -WorkingDirectory $dir -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 2
    }
    $watch=[Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 250
        $active=@($processes | Where-Object {!$_.HasExited})
        if($relay -and $relay.HasExited) {throw "Loopback relay exited unexpectedly: $run"}
        Compare-Samples
        foreach($dir in $dirs) {
            $log=Join-Path $dir 'multiplayer.log'
            if((Test-Path -LiteralPath $log) -and (Get-Content -LiteralPath $log -Raw) -match '(?m)^FAIL[^\r\n]*') {throw "$dir`: $($Matches[0])"}
            $loaderLog=Join-Path $dir 'syringe.log'
            if((Test-Path -LiteralPath $loaderLog) -and (Get-Content -LiteralPath $loaderLog -Raw) -match 'Done with exit code (?!0 \(0\))\w+ \(') {throw "Game exited abnormally: $dir"}
            if(@(Get-ChildItem -LiteralPath (Join-Path $dir 'debug') -Filter 'except.txt' -Recurse -ErrorAction SilentlyContinue).Count) {throw "Game generated a crash report: $dir"}
            if(@(Get-ChildItem -LiteralPath $dir -Filter 'SYNC*.TXT' -File).Count) {throw "Engine reported a synchronization error: $dir"}
        }
    } while($active.Count -and $watch.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    if($active.Count) {throw "Multiplayer timed out after $TimeoutSeconds seconds: $run"}
    foreach($dir in $dirs) {
        if((Get-Content -LiteralPath (Join-Path $dir 'multiplayer.log') -Raw) -notmatch 'PASS completed network run') {throw "Incomplete multiplayer run: $dir"}
    }
    $identities=@($dirs | ForEach-Object {
        $log=Get-Content -LiteralPath (Join-Path $_ 'multiplayer.log') -Raw
        if($log -notmatch 'LOCAL house=(\d+) mode=\d+ players=2') {throw 'Missing real multiplayer identity'}
        $Matches[1]
    })
    if($identities[0] -eq $identities[1]) {throw 'Both peers are controlling the same player'}
    $a=@(Get-Content -LiteralPath (Join-Path $dirs[0] 'snapshots.csv'))
    $b=@(Get-Content -LiteralPath (Join-Path $dirs[1] 'snapshots.csv'))
    if($a.Count -ne 881 -or $a.Count -ne $b.Count) {throw 'Missing snapshot frames'}
    for($i=0;$i -lt $a.Count;$i++) {if(($a[$i] -split ',')[0] -ne [string](20+$i)) {throw 'Non-contiguous snapshot frames'}}
    for($i=0;$i -lt $a.Count;$i++) {if($a[$i] -ne $b[$i]) {throw "Peers differ: $($a[$i]) versus $($b[$i]); $run"}}
    if($NetworkProfile -eq 'Impaired') {
        $relayStats=Get-Content (Join-Path $run 'relay.json') -Raw | ConvertFrom-Json
        if(!$relayStats.dropped -or !$relayStats.blackoutDropped -or !$relayStats.peer0Forwarded -or !$relayStats.peer1Forwarded) {throw 'Network impairment was not exercised in both directions'}
    }
    [pscustomobject]@{Result='PASS';NetworkProfile=$NetworkProfile;Samples=$a.Count;Run=$run;Seconds=[int]$watch.Elapsed.TotalSeconds} | ConvertTo-Json | Set-Content (Join-Path $run 'result.json')
    Write-Output "PASS two real peers, $($a.Count) matching state samples: $run"
} catch {
    [pscustomobject]@{Result='FAIL';NetworkProfile=$NetworkProfile;ComparedSamples=$compared;Run=$run;Reason=$_.Exception.Message} | ConvertTo-Json | Set-Content (Join-Path $run 'result.json')
    throw
} finally {
    if($ReportPath -and (Test-Path -LiteralPath (Join-Path $run 'result.json'))) {Copy-Item -LiteralPath (Join-Path $run 'result.json') -Destination $ReportPath -Force}
    $paths=@($dirs | ForEach-Object {Join-Path $_ 'gamemd.exe';Join-Path $_ 'Syringe.exe'})
    Get-Process -Name gamemd,Syringe -ErrorAction SilentlyContinue | Where-Object {$_.Path -in $paths} | Stop-Process -Force
    if($relay -and !$relay.HasExited) {
        $null=New-Item -ItemType File -Force (Join-Path $run 'relay.stop')
        if(!$relay.WaitForExit(3000)) {$relay.Kill()}
    }
    if(!$KeepGameAssets) {
        foreach($dir in $dirs) {
            Remove-TestArchives $dir
        }
    }
}
