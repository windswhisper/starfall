$script:TestRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:RepoRoot=[IO.Path]::GetFullPath((Join-Path $script:TestRoot '..'))

function Get-TestCatalog {
    $catalog=Get-Content -LiteralPath (Join-Path $script:TestRoot 'cases.json') -Raw | ConvertFrom-Json
    if($catalog.Version -ne 1) {throw 'Unsupported test catalog version'}
    return $catalog
}
function Get-TestCase([string]$Name) {
    $found=@((Get-TestCatalog).Cases | Where-Object Name -eq $Name)
    if($found.Count -ne 1) {throw "Unknown test '$Name'. Run tests/run.ps1 -List."}
    return $found[0]
}
function Get-TestExpected($Definition,[string]$Phase,[bool]$WithPhobos) {
    $checks=$Definition.Expected.PSObject.Properties[$Phase]
    if($WithPhobos -and $Definition.PSObject.Properties['ExpectedPhobos']) {
        $override=$Definition.ExpectedPhobos.PSObject.Properties[$Phase]
        if($override) {$checks=$override}
    }
    if(!$checks -or !@($checks.Value).Count) {throw "No expected messages for $($Definition.Name)/$Phase"}
    return @($checks.Value)
}
function Assert-TestReport($Definition,[string]$Phase,[string]$Text,[bool]$WithPhobos) {
    if($Text -match '(?m)^FAIL[^\r\n]*') {throw "$($Definition.Name)/${Phase}: $($Matches[0])"}
    foreach($line in Get-TestExpected $Definition $Phase $WithPhobos) {
        if(!$Text.Contains($line)) {throw "Missing '$line' in $($Definition.Name)/$Phase"}
    }
    if($Definition.Name -eq 'projectiles' -and $Phase -eq 'fresh' -and $Text -notmatch '(?m)^damage [1-9]\d* restored 1 stack 0') {
        throw 'Scaled blast missed the test target'
    }
    if($Definition.Name -eq 'script-grants') {
        $late=($Text -split '(?m)^frame 210\r?$',2);$end=($Text -split '(?m)^frame 510\r?$',2)
        if($late.Count -ne 2 -or $late[1].Contains('sf_shotreward') -or $end.Count -ne 2 -or $end[1].Contains('sf_periodicrepair')) {
            throw 'Grant expiry check failed'
        }
    }
}
function Assert-TestPath([string]$Path,[string]$Root) {
    $allowed=(Resolve-Path -LiteralPath $Root).Path.TrimEnd('\','/')
    if((Get-Item -LiteralPath $allowed -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw "Reparse point cannot be a test workspace: $allowed"}
    $full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith($allowed+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside test workspace: $full"
    }
    $probe=$full
    while($probe -and $probe -ne $allowed) {
        if(Test-Path -LiteralPath $probe) {
            $item=Get-Item -LiteralPath $probe -Force
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw "Reparse point is not a test cleanup target: $probe"}
        }
        $probe=Split-Path -Parent $probe
    }
    return $full
}
function Assert-NoTestProcesses([string]$Directory) {
    $prefix=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    $active=@(Get-Process -Name gamemd,Syringe -ErrorAction SilentlyContinue | Where-Object {$_.Path -and $_.Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)})
    if($active.Count) {throw "Test processes are still running in $Directory"}
}
function Copy-TestGameFiles([string]$Source,[string]$Destination,[switch]$CopyArchives) {
    $records=@()
    foreach($file in Get-ChildItem -LiteralPath $Source -File | Where-Object {
        ($_.Extension -in '.mix','.dll','.tlb','.inj','.dsk','.lcf','.pcx' -or $_.Name -in 'Syringe.exe','gamemd.exe','ConquerMD.dat','ddraw.ini','RA2MD.ini') -and $_.Name -ne 'Starfall.dll'
    }) {
        $target=Join-Path $Destination $file.Name
        $linked=$false
        # Only immutable game archives may share storage. DLLs and settings are
        # copied because extension replacement and the engine may write them.
        if($file.Extension -eq '.mix' -and !$CopyArchives) {
            try {$null=New-Item -ItemType HardLink -Path $target -Value $file.FullName -ErrorAction Stop;$linked=$true} catch {}
        }
        if(!$linked) {Copy-Item -LiteralPath $file.FullName -Destination $target}
        $records+=[pscustomobject]@{Name=$file.Name;Bytes=$file.Length;HardLink=$linked}
    }
    [pscustomobject]@{Source=[IO.Path]::GetFullPath($Source);Files=$records} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'staged-game-files.json') -Encoding utf8
}
function Remove-TestArchives([string]$Directory) {
    $runs=Join-Path $script:TestRoot 'local/runs'
    $resolved=Assert-TestPath $Directory $runs
    Assert-NoTestProcesses $resolved
    if(!(Test-Path -LiteralPath (Join-Path $resolved 'Starfall.test') -PathType Leaf)) {throw "Missing generated-test marker: $resolved"}
    foreach($file in Get-ChildItem -LiteralPath $resolved -Filter '*.mix' -File) {
        $checked=Assert-TestPath $file.FullName $resolved
        Remove-Item -LiteralPath $checked -Force
    }
}
function Get-TestFingerprint([string[]]$Files,[string]$Signature='') {
    $lines=@($Signature)
    foreach($file in $Files | Sort-Object -Unique) {
        if(!(Test-Path -LiteralPath $file -PathType Leaf)) {throw "Build dependency missing: $file"}
        $lines+=([IO.Path]::GetFullPath($file)+'|'+(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash)
    }
    $hash=[Security.Cryptography.SHA256]::Create()
    try {return ([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($lines -join "`n")))).Replace('-','')} finally {$hash.Dispose()}
}
function Test-BuildCache([string]$Stamp,[string]$Fingerprint,[string[]]$Outputs) {
    if(!(Test-Path -LiteralPath $Stamp -PathType Leaf)) {return $false}
    try {
        $saved=Get-Content -LiteralPath $Stamp -Raw | ConvertFrom-Json
        if($saved.Fingerprint -cne $Fingerprint -or @($saved.Outputs).Count -ne $Outputs.Count) {return $false}
        foreach($output in $Outputs) {
            if(!(Test-Path -LiteralPath $output -PathType Leaf)) {return $false}
            $entry=@($saved.Outputs | Where-Object Path -eq $output)
            if($entry.Count -ne 1 -or $entry[0].SHA256 -cne (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash) {return $false}
        }
        return $true
    } catch {return $false}
}
function Save-BuildCache([string]$Stamp,[string]$Fingerprint,[string[]]$Outputs) {
    $data=[pscustomobject]@{Fingerprint=$Fingerprint;Outputs=@($Outputs | ForEach-Object {@{Path=$_;SHA256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})}
    $data | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Stamp -Encoding utf8
}
