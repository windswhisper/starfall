$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'lib/Common.ps1')
$count=0
function Check([bool]$Ok,[string]$Label) {if(!$Ok){throw "FAIL $Label"};$script:count++;Write-Host "PASS $Label"}
function Reject([scriptblock]$Action,[string]$Label) {$failed=$false;try {& $Action | Out-Null} catch {$failed=$true};Check $failed $Label}
$catalog=Get-TestCatalog
$actual=@(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'game') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'Probe.cpp')} | ForEach-Object Name)
Check (@($catalog.Cases.Name | Sort-Object -Unique).Count -eq $catalog.Cases.Count) 'unique case names'
Check (@(Compare-Object ($actual | Sort-Object) ($catalog.Cases.Name | Sort-Object)).Count -eq 0) 'catalog covers all engine probes'
foreach($case in $catalog.Cases) {
    if($case.Kind -eq 'network') {continue}
    Check (Test-Path -LiteralPath (Join-Path $PSScriptRoot $case.Map)) "$($case.Name) map exists"
    foreach($phase in $case.Phases) {Check (@(Get-TestExpected $case $phase $false).Count -gt 0) "$($case.Name)/$phase expectations exist"}
}
foreach($suite in $catalog.Suites.PSObject.Properties) {foreach($name in $suite.Value) {if($name -ne 'unit') {$null=Get-TestCase $name}}}
Check $true 'suite membership resolves'
Reject {Get-TestCase '../src'} 'invalid case rejected before build'
$cloak=Get-TestCase 'cloakable';$valid=(Get-TestExpected $cloak 'fresh' $false) -join "`n"
Assert-TestReport $cloak 'fresh' $valid $false
Reject {Assert-TestReport $cloak 'fresh' ($valid+"`nFAIL injected") $false} 'explicit failure rejected'
Reject {Assert-TestReport $cloak 'fresh' ($valid.Replace('PASS saved while cloaked','')) $false} 'missing success marker rejected'
$projectiles=Get-TestCase 'projectiles';$projectileLog=(Get-TestExpected $projectiles 'fresh' $false) -join "`n"
Reject {Assert-TestReport $projectiles 'fresh' $projectileLog $false} 'projectile damage predicate retained'
Assert-TestReport $projectiles 'fresh' ($projectileLog+"`ndamage 1 restored 1 stack 0") $false
$grants=Get-TestCase 'script-grants';$grantLog=(Get-TestExpected $grants 'fresh' $false) -join "`n"
Reject {Assert-TestReport $grants 'fresh' $grantLog $false} 'grant expiry predicate retained'
$scratch=Join-Path $script:RepoRoot ('build/tests/infrastructure-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Force $scratch
try {
    $input=Join-Path $scratch 'input.h';$output=Join-Path $scratch 'output.dll';$stamp=Join-Path $scratch 'stamp.json'
    [IO.File]::WriteAllText($input,'old');[IO.File]::WriteAllText($output,'binary')
    $mtime=(Get-Item -LiteralPath $input).LastWriteTimeUtc
    $fingerprint=Get-TestFingerprint @($input) 'compiler-A'
    Save-BuildCache $stamp $fingerprint @($output)
    Check (Test-BuildCache $stamp $fingerprint @($output)) 'matching build is reused'
    [IO.File]::WriteAllText($input,'new');(Get-Item -LiteralPath $input).LastWriteTimeUtc=$mtime
    Check (!(Test-BuildCache $stamp (Get-TestFingerprint @($input) 'compiler-A') @($output))) 'same-size source edit with preserved timestamp invalidates build'
    [IO.File]::WriteAllText($input,'old');(Get-Item -LiteralPath $input).LastWriteTimeUtc=$mtime
    Check (!(Test-BuildCache $stamp (Get-TestFingerprint @($input) 'compiler-B') @($output))) 'compiler signature invalidates build'
    [IO.File]::WriteAllText($output,'broken')
    Check (!(Test-BuildCache $stamp $fingerprint @($output))) 'corrupt executable invalidates build'
    $source=Join-Path $scratch 'base';$copy=Join-Path $scratch 'run';$null=New-Item -ItemType Directory $source,$copy
    [IO.File]::WriteAllText((Join-Path $source 'sample.mix'),'archive')
    [IO.File]::WriteAllText((Join-Path $source 'RA2MD.ini'),'original')
    Copy-TestGameFiles $source $copy
    [IO.File]::WriteAllText((Join-Path $copy 'RA2MD.ini'),'changed')
    Check ([IO.File]::ReadAllText((Join-Path $source 'RA2MD.ini')) -eq 'original') 'runtime configuration is isolated'
    $checked=Assert-TestPath (Join-Path $copy 'sample.mix') $scratch
    Remove-Item -LiteralPath $checked
    Check ([IO.File]::ReadAllText((Join-Path $source 'sample.mix')) -eq 'archive') 'archive cleanup preserves source'
    Reject {Assert-TestPath (Join-Path $scratch '../outside') $scratch} 'cleanup rejects parent traversal'
    Reject {Assert-TestPath $scratch $scratch} 'cleanup rejects workspace root'
} finally {$safe=Assert-TestPath $scratch (Join-Path $script:RepoRoot 'build/tests');Remove-Item -LiteralPath $safe -Recurse -Force}
Write-Host "PASS $count infrastructure checks"
