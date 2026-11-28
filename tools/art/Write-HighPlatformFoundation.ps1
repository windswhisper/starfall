param(
    [Parameter(Mandatory)][ValidateRange(3,512)][int]$Width,
    [Parameter(Mandatory)][ValidateRange(3,512)][int]$Height
)

'Foundation=Custom'
"Foundation.X=$Width"
"Foundation.Y=$Height"
$index=0
for($y=0;$y -lt $Height;$y++) {
    for($x=0;$x -lt $Width;$x++) {
        if($x -eq 0 -or $x -eq $Width-1 -or $y -eq 0 -or $y -eq $Height-1) {
            "Foundation.$index=$x,$y"
            $index++
        }
    }
}

"FoundationOutline.Length=$(2*($Width+$Height)+4)"
$index=0
for($x=-1;$x -le $Width;$x++) {
    "FoundationOutline.$index=$x,-1"
    $index++
}
for($y=0;$y -le $Height;$y++) {
    "FoundationOutline.$index=$Width,$y"
    $index++
}
for($x=$Width-1;$x -ge -1;$x--) {
    "FoundationOutline.$index=$x,$Height"
    $index++
}
for($y=$Height-1;$y -ge 0;$y--) {
    "FoundationOutline.$index=-1,$y"
    $index++
}
