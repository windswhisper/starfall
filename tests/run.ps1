param(
    [string]$Suite='quick',
    [string[]]$Case,
    [switch]$List,
    [switch]$Plan,
    [string]$GameDirectory,
    [string]$PhobosPackageDirectory,
    [string]$StandaloneSpawner,
    [ValidateSet('Direct','Impaired')][string]$NetworkProfile='Direct',
    [int]$Seed=1804805662,
    [switch]$Rebuild,
    [switch]$CopyGameAssets,
    [switch]$KeepGameAssets,
    [switch]$Standalone,
    [switch]$ContinueOnFailure
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'lib/Common.ps1')
$catalog=Get-TestCatalog
if($List) {
    $catalog.Suites.PSObject.Properties | ForEach-Object {[pscustomobject]@{Suite=$_.Name;Cases=($_.Value -join ', ')}} | Format-Table -AutoSize
    $catalog.Cases | Select-Object Name,Kind,@{n='Phases';e={$_.Phases -join ', '}} | Format-Table -AutoSize
    return
}
if($Case) {$selected=@($Case | Select-Object -Unique)}
else {
    $entry=$catalog.Suites.PSObject.Properties[$Suite]
    if(!$entry) {throw "Unknown suite '$Suite'. Run with -List."}
    $selected=@($entry.Value)
}
foreach($name in $selected) {if($name -ne 'unit') {$null=Get-TestCase $name}}
$localConfig=Join-Path $PSScriptRoot 'local/environment.json'
if(Test-Path -LiteralPath $localConfig) {
    $environment=Get-Content -LiteralPath $localConfig -Raw | ConvertFrom-Json
    if(!$GameDirectory) {$GameDirectory=$environment.GameDirectory}
    if(!$PhobosPackageDirectory) {$PhobosPackageDirectory=$environment.PhobosPackageDirectory}
    if(!$StandaloneSpawner) {$StandaloneSpawner=$environment.StandaloneSpawner}
}
if(!$GameDirectory) {$GameDirectory=Join-Path $PSScriptRoot 'local/game-base'}
if($Plan) {
    foreach($name in $selected) {
        if($name -eq 'unit') {[pscustomobject]@{Name='unit';Kind='unit';Steps="$($catalog.Units.Count) programs + infrastructure checks";NeedsGame=$false}}
        else {
            $d=Get-TestCase $name
            $phases=if($PhobosPackageDirectory -and !$Standalone -and $d.PSObject.Properties['PhobosPhases']) {@($d.PhobosPhases)} else {@($d.Phases)}
            $steps=if($d.Kind -eq 'network') {'2 peers / 881 snapshots'} else {$phases -join ' -> '}
            [pscustomobject]@{Name=$name;Kind=$d.Kind;Steps=$steps;NeedsGame=$true}
        }
    }
    return
}
if(@($selected | Where-Object {$_ -ne 'unit'}).Count) {
    foreach($file in 'gamemd.exe','Syringe.exe') {if(!(Test-Path -LiteralPath (Join-Path $GameDirectory $file))) {throw "Missing $file. Supply -GameDirectory or tests/local/environment.json."}}
}
if('multiplayer' -in $selected -and (!$PhobosPackageDirectory -or !$StandaloneSpawner)) {throw 'Network tests require -PhobosPackageDirectory and -StandaloneSpawner.'}
if($Standalone) {
    if(!$StandaloneSpawner -or 'multiplayer' -in $selected) {throw '-Standalone requires a spawner and single-client cases.'}
    foreach($name in $selected) {if($name -ne 'unit' -and !(Get-TestCase $name).Standalone) {throw "$name does not support the standalone profile."}}
}
$reports=Join-Path $PSScriptRoot ('local/records/suite-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$null=New-Item -ItemType Directory $reports
$watch=[Diagnostics.Stopwatch]::StartNew();$results=@()
try {
    foreach($name in $selected) {
        $caseWatch=[Diagnostics.Stopwatch]::StartNew()
        $detail=Join-Path $reports "$name.json"
        $log=Join-Path $reports "$name.log"
        Write-Host "RUN $name"
        try {
            if($name -eq 'unit') {
                & (Join-Path $PSScriptRoot 'unit/run.ps1') -Rebuild:$Rebuild -ReportPath $detail *> $log
            } elseif($name -eq 'multiplayer') {
                & (Join-Path $PSScriptRoot 'run-multiplayer.ps1') -GameDirectory $GameDirectory -PhobosPackageDirectory $PhobosPackageDirectory -StandaloneSpawner $StandaloneSpawner -NetworkProfile $NetworkProfile -Seed $Seed -Rebuild:$Rebuild -CopyGameAssets:$CopyGameAssets -KeepGameAssets:$KeepGameAssets -ReportPath $detail *> $log
            } else {
                $caseArgs=@{Case=$name;GameDirectory=$GameDirectory;Rebuild=$Rebuild;CopyGameAssets=$CopyGameAssets;KeepGameAssets=$KeepGameAssets;ReportPath=$detail}
                if($Standalone) {$caseArgs.StandaloneSpawner=$StandaloneSpawner}
                elseif($PhobosPackageDirectory) {$caseArgs.PhobosPackageDirectory=$PhobosPackageDirectory}
                & (Join-Path $PSScriptRoot 'run-game.ps1') @caseArgs *> $log
            }
            $results+=[pscustomobject]@{Case=$name;Result='PASS';Seconds=[math]::Round($caseWatch.Elapsed.TotalSeconds,3);Log=$log;Detail=$detail}
            Write-Host "PASS $name ($([math]::Round($caseWatch.Elapsed.TotalSeconds,1)) s)"
        } catch {
            $results+=[pscustomobject]@{Case=$name;Result='FAIL';Seconds=[math]::Round($caseWatch.Elapsed.TotalSeconds,3);Log=$log;Reason=$_.Exception.Message}
            Write-Host "FAIL $name`: $($_.Exception.Message)"
            if(!$ContinueOnFailure) {break}
        }
    }
} finally {
    $summary=[pscustomobject]@{Result=$(if(@($results | Where-Object Result -eq 'FAIL').Count){'FAIL'}else{'PASS'});Requested=$selected;Executed=$results.Count;Seconds=[math]::Round($watch.Elapsed.TotalSeconds,3);Results=$results}
    $summary | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $reports 'summary.json') -Encoding utf8
    Write-Host "Report: $reports"
}
if($summary.Result -eq 'FAIL') {throw 'One or more tests failed. See the suite report.'}
