param(
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$cases=@(
    @('plain','zone1','forest_river220','Dejavu zone1 (11x9)','Ataman220 R2: plain / river (8x8)'),
    @('mountains','zone2','mountain_pass220','Dejavu zone2 (5x6)','Ataman220 R2: mountains / pass (8x8)'),
    @('coast','zone7','coast_bay220','Dejavu zone7 (16x10)','Ataman220 R2: coast / bay (8x8)')
)
foreach($case in $cases){
    $sample=[Drawing.Image]::FromFile((Join-Path $RunRoot ($case[2]+'-top.png')))
    $size=$sample.Width;$sample.Dispose();$pad=16;$header=48;$rowHeader=28
    $canvas=[Drawing.Bitmap]::new(2*$size+3*$pad,2*($size+$rowHeader)+$header+2*$pad)
    $graphics=[Drawing.Graphics]::FromImage($canvas);$graphics.Clear([Drawing.Color]::FromArgb(24,27,30))
    $font=[Drawing.Font]::new('Segoe UI',12)
    try {
        for($column=0;$column -lt 2;$column++){
            $x=$pad+$column*($size+$pad)
            $graphics.DrawString($case[3+$column],$font,[Drawing.Brushes]::White,$x,10)
            for($row=0;$row -lt 2;$row++){
                $angle=@('top','angle')[$row];$y=$header+$row*($size+$rowHeader+$pad)
                $name=if($column -eq 0){'reference-'+$case[1]}else{$case[2]}
                $image=[Drawing.Image]::FromFile((Join-Path $RunRoot ($name+'-'+$angle+'.png')))
                try {
                    if($image.Width -ne $size -or $image.Height -ne $size){throw 'Comparison crops must have equal pixel dimensions.'}
                    $graphics.DrawString($angle,$font,[Drawing.Brushes]::LightGray,$x,$y)
                    # Explicit source pixels avoid DPI-based shrinkage of Qt captures.
                    $graphics.DrawImage($image,[Drawing.Rectangle]::new($x,$y+$rowHeader,$size,$size),0,0,$size,$size,[Drawing.GraphicsUnit]::Pixel)
                } finally {$image.Dispose()}
            }
        }
        $canvas.Save((Join-Path $OutputRoot ('compare-'+$case[0]+'.png')),[Drawing.Imaging.ImageFormat]::Png)
    } finally {$font.Dispose();$graphics.Dispose();$canvas.Dispose()}
}
