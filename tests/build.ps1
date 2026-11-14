param([string]$Case,[switch]$Unit,[string[]]$UnitName,[switch]$Rebuild,[switch]$CheckOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'lib/Common.ps1')
$catalog=Get-TestCatalog
if($Unit) {
    $names=if($UnitName) {$UnitName} else {@($catalog.Units)}
    foreach($name in $names) {if($name -cnotin $catalog.Units) {throw "Unknown unit program: $name"}}
} else {$definition=Get-TestCase $Case;$names=@($definition.Name)}
$out=Join-Path $script:RepoRoot 'build/tests'
$shared=Join-Path $out 'shared'
$null=New-Item -ItemType Directory -Force $out,$shared,(Join-Path $shared 'modules')
try {$lock=[IO.File]::Open((Join-Path $out '.build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)} catch {throw 'Another test build is using the shared cache. Wait for that build to finish.'}
try {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$vs) {throw 'Install Visual Studio with the x86 C++ build tools.'}
    $toolset=(Get-Content -LiteralPath (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
    # Match vcvars32.bat in tools/msvc-x86.cmd.
    $compiler=Join-Path $vs "VC/Tools/MSVC/$toolset/bin/Hostx86/x86/cl.exe"
    $sdk=Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include') -Directory | Sort-Object Name | Select-Object -Last 1
    $signature="x86|MSVC=$toolset|Compiler=$((Get-FileHash -LiteralPath $compiler).Hash)|SDK=$($sdk.Name)|$($sdk.LastWriteTimeUtc.Ticks)"
    $control=@('tests/compile.cmd','tests/build.ps1','tools/build-lua.cmd','tools/msvc-x86.cmd','tools/module-sources.cmd') | ForEach-Object {Join-Path $script:RepoRoot $_}
    $headers=@(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'references/Phobos/YRpp') -Recurse -File | Where-Object Extension -in '.h','.inl' | ForEach-Object FullName)
    $source=@(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'src') -Recurse -File | Where-Object Extension -in '.h','.inc','.cpp' | ForEach-Object FullName)
    $commonSource=@($source | Where-Object {[IO.Path]::GetFileName($_) -notin 'Starfall.cpp','ResearchUI.cpp'})
    $lua=@(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'references/lua') -File | Where-Object Extension -in '.h','.c' | ForEach-Object FullName)
    $assets=@(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'assets/research') -Recurse -File | ForEach-Object FullName)
    function Build-Part([string]$Kind,[string]$Name,[string[]]$Dependencies,[string[]]$Outputs) {
        $key=if($Name) {"$Kind-$Name"} else {$Kind}
        $fingerprint=Get-TestFingerprint ($control+$Dependencies) $signature
        $stamp=Join-Path $shared "$key.json"
        $forceThis=$Rebuild -and $Kind -in 'game','unit'
        if(!$forceThis -and (Test-BuildCache $stamp $fingerprint $Outputs)) {Write-Host "CACHED $key";return}
        if($CheckOnly) {throw "Stale or missing build: $key. Run without -SkipBuild to rebuild safely."}
        foreach($file in $Outputs) {$null=New-Item -ItemType Directory -Force (Split-Path -Parent $file)}
        # Invalidate before compiling: an interrupted/failed build cannot be reused.
        if(Test-Path -LiteralPath $stamp) {Remove-Item -LiteralPath $stamp}
        Write-Host "BUILD $key"
        & (Join-Path $PSScriptRoot 'compile.cmd') $Kind $Name
        if($LASTEXITCODE) {throw "Compile failed: $key ($LASTEXITCODE)"}
        Save-BuildCache $stamp $fingerprint $Outputs
    }
    Build-Part lua '' $lua @((Join-Path $shared 'lua/lua.lib'))
    Build-Part resources '' $assets @((Join-Path $shared 'research.res'))
    if(!$Unit -or 'ActionArgumentsTest' -in $names) {
        Build-Part modules '' ($commonSource+$headers) @((Join-Path $shared 'modules.lib'))
        if(!$Unit -and $Case -eq 'hex-icons') {Build-Part core '' ($source+$headers+$lua) @((Join-Path $shared 'Starfall.obj'))}
        else {Build-Part ui '' ($commonSource+$headers+@((Join-Path $script:RepoRoot 'src/ResearchUI.cpp'))) @((Join-Path $shared 'ResearchUI.obj'))}
    }
    foreach($name in $names) {
        if($Unit) {
            $dependencies=$source+$headers+$lua+$assets+@((Join-Path $PSScriptRoot "unit/$name.cpp"))
            Build-Part unit $name $dependencies @((Join-Path $out "unit/$name.exe"))
        } else {
            $fixtures=@(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot "game/$name") -File | Where-Object Extension -in '.cpp','.h','.inc' | ForEach-Object FullName)
            $sharedHeaders=@(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'game') -File | Where-Object Extension -in '.h','.inc' | ForEach-Object FullName)
            Build-Part game $name ($source+$headers+$lua+$assets+$fixtures+$sharedHeaders) @((Join-Path $out "$name/Starfall.dll"))
        }
    }
} finally {$lock.Dispose()}
