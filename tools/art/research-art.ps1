param([string]$Concept,[string]$Destination,[switch]$DefaultIcon)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName PresentationCore,WindowsBase,System.Drawing
function Write-Pcx($bmp,$name){
    $png=[IO.MemoryStream]::new()
    $bmp.Save($png,[Drawing.Imaging.ImageFormat]::Png);$png.Position=0
    $frame=[Windows.Media.Imaging.PngBitmapDecoder]::new($png,0,2).Frames[0]
    $pal=[Windows.Media.Imaging.BitmapPalette]::new($frame,256)
    $idx=[Windows.Media.Imaging.FormatConvertedBitmap]::new($frame,[Windows.Media.PixelFormats]::Indexed8,$pal,0)
    $width=$bmp.Width;$height=$bmp.Height;$stride=$width+($width%2)
    $pixels=[byte[]]::new($height*$stride);$idx.CopyPixels($pixels,$stride,0)
    $header=[byte[]]::new(128);$header[0]=10;$header[1]=5;$header[2]=1;$header[3]=8
    foreach($pair in @(@(8,($width-1)),@(10,($height-1)),@(12,72),@(14,72),@(66,$stride),@(68,1))){[BitConverter]::GetBytes([int16]$pair[1]).CopyTo($header,$pair[0])}
    $header[65]=1;$file=[IO.File]::Create((Join-Path $Destination "$name.pcx"));$w=[IO.BinaryWriter]::new($file)
    try{$w.Write($header)
        for($y=0;$y -lt $height;$y++){for($x=0;$x -lt $stride;){$v=$pixels[$y*$stride+$x];$n=1
            while($n -lt 63 -and $x+$n -lt $stride -and $pixels[$y*$stride+$x+$n] -eq $v){$n++}
            if($n -gt 1 -or $v -ge 192){$w.Write([byte](192+$n))};$w.Write($v);$x+=$n}}
        $w.Write([byte]12);$rgb=[byte[]]::new(768)
        for($i=0;$i -lt $pal.Colors.Count;$i++){$c=$pal.Colors[$i];$rgb[3*$i]=$c.R;$rgb[3*$i+1]=$c.G;$rgb[3*$i+2]=$c.B}
        $w.Write($rgb)
    }finally{$w.Dispose();$png.Dispose()}
}
$art=[Drawing.Image]::FromFile($Concept)
if($DefaultIcon) {
    try {
        foreach($size in 96,62) {
            $bmp=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $g=[Drawing.Graphics]::FromImage($bmp)
            try {
                $g.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $g.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $scale=[Math]::Min($size/$art.Width,$size/$art.Height)
                $w=[int]($art.Width*$scale);$h=[int]($art.Height*$scale)
                $g.DrawImage($art,[Drawing.Rectangle]::new(($size-$w)/2,($size-$h)/2,$w,$h))
                if($size -eq 96) {
                    $bmp.Save((Join-Path $Destination 'sfrdefault.png'),[Drawing.Imaging.ImageFormat]::Png)
                } else {
                    # PCX supports a single transparent color. Keep the edges free of magenta fringes.
                    for($y=0;$y -lt $size;$y++){for($x=0;$x -lt $size;$x++){
                        $c=$bmp.GetPixel($x,$y)
                        if($c.A -lt 128){$bmp.SetPixel($x,$y,[Drawing.Color]::Magenta)}
                        elseif($c.A -lt 255){$bmp.SetPixel($x,$y,[Drawing.Color]::FromArgb($c.R,$c.G,$c.B))}
                    }}
                    Write-Pcx $bmp 'sfrdefault'
                }
            } finally {$g.Dispose();$bmp.Dispose()}
        }
    } finally {$art.Dispose()}
    return
}
foreach($tile in @(@('sfrlab',1403,14,70,64,46),@('sfrheal',906,177,131,123,62),@('sfrdrain',906,332,131,125,62),@('sfrripst',906,488,131,124,62))){
    $size=$tile[5];$bmp=[Drawing.Bitmap]::new($size,$size);$g=[Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($art,[Drawing.Rectangle]::new(0,0,$size,$size),[Drawing.Rectangle]::new($tile[1],$tile[2],$tile[3],$tile[4]),[Drawing.GraphicsUnit]::Pixel)
    Write-Pcx $bmp $tile[0];$g.Dispose();$bmp.Dispose()
}
$art.Dispose()
function Color($hex){[Drawing.ColorTranslator]::FromHtml($hex)}
foreach($plate in @(@('sfrpanel',384,286,16,'#101925','#131523','#839aca'),@('sfrfocus',360,74,5,'#292442','#17182c','#8378b5'))){
    $w=$plate[1];$h=$plate[2];$cut=$plate[3];$bmp=[Drawing.Bitmap]::new($w,$h);$g=[Drawing.Graphics]::FromImage($bmp)
    $g.Clear([Drawing.Color]::Magenta)
    $points=[Drawing.Point[]]@([Drawing.Point]::new($cut,0),[Drawing.Point]::new($w-1,0),[Drawing.Point]::new($w-1,$h-1-$cut),[Drawing.Point]::new($w-1-$cut,$h-1),[Drawing.Point]::new(0,$h-1),[Drawing.Point]::new(0,$cut))
    $brush=[Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.Rectangle]::new(0,0,$w,$h),(Color $plate[4]),(Color $plate[5]),32)
    $g.FillPolygon($brush,$points);$edge=[Drawing.Pen]::new((Color $plate[6]));$g.DrawPolygon($edge,$points)
    $glow=[Drawing.Pen]::new((Color '#adcaff'),2)
    if($plate[0] -eq 'sfrpanel'){$g.DrawLine($glow,2,$cut,$cut,2);$g.DrawLine($glow,$w-3,$h-$cut-1,$w-$cut-1,$h-3);$g.DrawLine($edge,14,39,$w-15,39)}
    else{$glow.Color=Color '#b59aff';$g.DrawLine($glow,3,7,3,$h-5);$g.DrawLine($edge,$w-16,6,$w-7,6);$g.DrawLine($edge,$w-7,6,$w-7,15)}
    Write-Pcx $bmp $plate[0];$g.Dispose();$bmp.Dispose();$brush.Dispose();$edge.Dispose();$glow.Dispose()
}
