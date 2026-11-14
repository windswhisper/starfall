param([switch]$Apply,[switch]$Intermediates)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'lib/Common.ps1')
$runs=Join-Path $PSScriptRoot 'local/runs'
$files=@()
$buildLock=$null
if(Test-Path -LiteralPath $runs) {
    Assert-NoTestProcesses $runs
    foreach($run in Get-ChildItem -LiteralPath $runs -Directory) {
        $null=Assert-TestPath $run.FullName $runs
        $directories=@($run)
        if($run.Name -like 'multiplayer-*') {$directories=@(Get-ChildItem -LiteralPath $run.FullName -Directory | Where-Object Name -in 'peer0','peer1')}
        foreach($directory in $directories) {
            $null=Assert-TestPath $directory.FullName $runs
            if(!(Test-Path -LiteralPath (Join-Path $directory.FullName 'Starfall.test') -PathType Leaf)) {continue}
            foreach($file in Get-ChildItem -LiteralPath $directory.FullName -Filter '*.mix' -File) {
                $files+=@{Path=(Assert-TestPath $file.FullName $runs);Bytes=$file.Length;Kind='CopiedGameArchive'}
            }
        }
    }
}
try {
if($Intermediates) {
    $compiled=Join-Path $script:RepoRoot 'build/tests'
    if(Test-Path -LiteralPath $compiled) {
        # Do not race the incremental compiler or delete its shared cache.
        try {$buildLock=[IO.File]::Open((Join-Path $compiled '.build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)} catch {throw 'A test build is active; cleanup aborted.'}
        foreach($directory in Get-ChildItem -LiteralPath $compiled -Directory | Where-Object Name -ne 'shared') {
            $null=Assert-TestPath $directory.FullName $compiled
            foreach($file in Get-ChildItem -LiteralPath $directory.FullName -Recurse -File | Where-Object Extension -in '.obj','.lib','.exp','.res','.ilk','.pdb') {
                $files+=@{Path=(Assert-TestPath $file.FullName $compiled);Bytes=$file.Length;Kind='RebuildableIntermediate'}
            }
        }
    }
}
    $total=[long]($files | Measure-Object Bytes -Sum).Sum
    $report=[ordered]@{Applied=[bool]$Apply;Files=$files.Count;Bytes=$total;Items=$files;Completed=0}
    Write-Host "$(if($Apply){'Removing'}else{'Preview'}): $($files.Count) generated files, $([math]::Round($total/1GB,2)) GiB logical size. Shared archives may already occupy no additional space."
    if($Apply) {
        $records=Join-Path $PSScriptRoot 'local/records'
        $null=New-Item -ItemType Directory -Force $records
        $record=Join-Path $records ('cleanup-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.json')
        # Persist the exact resolved deletion plan before the first mutation.
        $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $record -Encoding utf8
        try {
            foreach($file in $files) {Remove-Item -LiteralPath $file.Path -Force;$report.Completed++}
        } finally {$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $record -Encoding utf8}
        Write-Host "Cleanup record: $record"
    }
} finally {if($buildLock) {$buildLock.Dispose()}}
