param(
    [Parameter(Mandatory)][string]$Primary,
    [string[]]$Extensions=@()
)
$ErrorActionPreference='Stop'
function Read-HookRanges([string]$Path) {
    $bytes=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path).Path)
    if ($bytes.Length -lt 64) { throw "Invalid DLL: $Path" }
    $pe=[BitConverter]::ToInt32($bytes,60)
    if ($pe -lt 0 -or $pe+248 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes,$pe+24) -ne 0x10B) { throw "Expected a PE32 DLL: $Path" }
    $imageBase=[BitConverter]::ToUInt32($bytes,$pe+52)
    $count=[BitConverter]::ToUInt16($bytes,$pe+6)
    $table=$pe+24+[BitConverter]::ToUInt16($bytes,$pe+20)
    $sections=@(for ($i=0;$i -lt $count;$i++) {
        $offset=$table+40*$i
        if ($offset+40 -gt $bytes.Length) { throw "Truncated section table: $Path" }
        [pscustomobject]@{
            Name=[Text.Encoding]::ASCII.GetString($bytes,$offset,8).Trim([char]0)
            Length=[BitConverter]::ToUInt32($bytes,$offset+8)
            RVA=[BitConverter]::ToUInt32($bytes,$offset+12)
            RawSize=[BitConverter]::ToUInt32($bytes,$offset+16)
            Offset=[BitConverter]::ToUInt32($bytes,$offset+20)
        }
    })
    foreach ($section in $sections | Where-Object Name -eq '.syhks00') {
        $length=[Math]::Min($section.Length,$section.RawSize)
        if ([long]$section.Offset+$length -gt $bytes.Length) { throw "Truncated hook section: $Path" }
        # Syringe's hookdecl is three DWORDs with 16-byte alignment.
        for ($offset=$section.Offset;$offset+16 -le $section.Offset+$length;$offset+=16) {
            $start=[BitConverter]::ToUInt32($bytes,$offset)
            if (!$start) { continue }
            $size=[Math]::Max(5,[BitConverter]::ToUInt32($bytes,$offset+4))
            $nameRVA=[long][BitConverter]::ToUInt32($bytes,$offset+8)-$imageBase
            $name='<unnamed>'
            foreach ($names in $sections) {
                if ($nameRVA -ge $names.RVA -and $nameRVA -lt [long]$names.RVA+$names.RawSize) {
                    $begin=[long]$names.Offset+$nameRVA-$names.RVA;$end=$begin
                    while ($end -lt $bytes.Length -and $end-$begin -lt 1024 -and $bytes[$end]) { $end++ }
                    if ($begin -ge 0 -and $end -le $bytes.Length) { $name=[Text.Encoding]::ASCII.GetString($bytes,$begin,$end-$begin) }
                    break
                }
            }
            [pscustomobject]@{ Start=[long]$start;End=[long]$start+$size;Name=$name;Module=[IO.Path]::GetFileName($Path) }
        }
    }
}
$primaryHooks=@(Read-HookRanges $Primary)
if (!$primaryHooks.Count) { throw "No Syringe hook declarations found in $Primary" }
$otherHooks=@(foreach ($extension in $Extensions) { Read-HookRanges $extension })
$conflicts=@(foreach ($hook in $primaryHooks) {
    foreach ($other in $otherHooks) {
        # Hooks at the same entry are chained by the loader. A hook starting in
        # another hook's overwritten bytes cannot be chained safely.
        if ($hook.Start -ne $other.Start -and $hook.Start -lt $other.End -and $other.Start -lt $hook.End) {
            '{0}:{1} [0x{2:X},0x{3:X}) overlaps {4}:{5} [0x{6:X},0x{7:X})' -f
                $hook.Module,$hook.Name,$hook.Start,$hook.End,$other.Module,$other.Name,$other.Start,$other.End
        }
    }
})
if ($conflicts.Count) { throw "Incompatible hook ranges:`n$($conflicts -join "`n")" }
Write-Output "Hook ranges checked: $($primaryHooks.Count) primary hooks, $($otherHooks.Count) extension hooks."
