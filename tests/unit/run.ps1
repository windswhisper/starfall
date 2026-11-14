param([string[]]$Name,[switch]$Rebuild,[string]$ReportPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../lib/Common.ps1')
$names=if($Name) {$Name} else {@((Get-TestCatalog).Units)}
& (Join-Path $PSScriptRoot '../TestInfrastructure.ps1')
& (Join-Path $PSScriptRoot '../build.ps1') -Unit -UnitName $names -Rebuild:$Rebuild
$results=@()
try {
    foreach($test in $names) {
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $output=@(& (Join-Path $script:RepoRoot "build/tests/unit/$test.exe") 2>&1)
        $code=$LASTEXITCODE
        $output | ForEach-Object {Write-Host $_}
        $results+=[pscustomobject]@{Name=$test;Result=$(if($code -eq 0){'PASS'}else{'FAIL'});ExitCode=$code;Seconds=$watch.Elapsed.TotalSeconds;Output=@($output | ForEach-Object {"$_"})}
        if($code -ne 0) {throw "$test exited $code"}
    }
} finally {
    if(!$ReportPath) {$ReportPath=Join-Path $script:TestRoot ('local/records/unit-'+[guid]::NewGuid().ToString('N')+'.json')}
    $null=New-Item -ItemType Directory -Force (Split-Path -Parent $ReportPath)
    [pscustomobject]@{Programs=$results;Passed=@($results | Where-Object Result -eq 'PASS').Count;Requested=$names.Count} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReportPath -Encoding utf8
}
