[CmdletBinding()]
param([string]$ExistingRunDirectory)
$ErrorActionPreference='Stop'
$gammas=@('0.00','0.25','0.50','0.75','1.00')
$run=if($ExistingRunDirectory){
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExistingRunDirectory)
}else{Join-Path $PSScriptRoot ('build/gamma-comparison/'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))}
if(-not $ExistingRunDirectory){
    $xmakeExe=@(Get-Command xmake -CommandType Application -ErrorAction Stop)[0].Source
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    $rows=@()
    Push-Location $PSScriptRoot
    try{
        foreach($gamma in $gammas){
            $watch=[Diagnostics.Stopwatch]::StartNew()
            & $xmakeExe run yafaray_cloud_shadows (Join-Path $run "gamma-$gamma.png") --mask-gamma $gamma --aa-samples 8 --path-samples 2 --light-samples 4 --threads -1 2>&1 |
                Tee-Object -FilePath (Join-Path $run "gamma-$gamma.log") | Out-Host
            $code=$LASTEXITCODE
            $watch.Stop()
            if($code){throw "Gamma $gamma failed"}
            $log=Get-Content (Join-Path $run "gamma-$gamma.log") -Raw
            $time=[regex]::Match($log,'Overall rendertime: ([0-9.]+)s')
            $mean=[regex]::Match($log,'mean transparency=([0-9.]+)')
            if(-not $time.Success -or -not $mean.Success){throw 'Missing render data'}
            $rows += [pscustomobject]@{Gamma=$gamma;AA=8;PathSamples=2;LightSamples=4;
                MeanTransparency=$mean.Groups[1].Value;RenderSeconds=$time.Groups[1].Value;
                ElapsedSeconds=$watch.Elapsed.TotalSeconds.ToString('F3',[Globalization.CultureInfo]::InvariantCulture)}
            $rows | Export-Csv -LiteralPath (Join-Path $run 'timings.csv') -NoTypeInformation
        }
    }finally{Pop-Location}
}
$rows=@(Import-Csv -LiteralPath (Join-Path $run 'timings.csv'))
if(($rows.Gamma -join ',') -ne ($gammas -join ',') -or
    @($rows | Where-Object { $_.AA -ne 8 -or $_.PathSamples -ne 2 -or $_.LightSamples -ne 4 }).Count){
    throw 'Unexpected gamma comparison settings'
}
Add-Type -AssemblyName System.Drawing
$canvas=[Drawing.Bitmap]::new(4000,700)
$graphics=[Drawing.Graphics]::FromImage($canvas)
$title=[Drawing.Font]::new('Segoe UI',26,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
$font=[Drawing.Font]::new('Segoe UI',18,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
try{
    $graphics.Clear([Drawing.Color]::FromArgb(28,33,42))
    $graphics.TextRenderingHint=[Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    for($i=0;$i -lt $rows.Count;++$i){
        $row=$rows[$i];$x=[single]($i*800+16)
        $graphics.DrawString("mask-gamma=$($row.Gamma)",$title,[Drawing.Brushes]::White,$x,8)
        $graphics.DrawString('AA=8 | path=2 | sun=4 | threads=auto',$font,[Drawing.Brushes]::White,$x,40)
        $graphics.DrawString("render=$($row.RenderSeconds)s | total=$($row.ElapsedSeconds)s | mean T=$($row.MeanTransparency)",$font,[Drawing.Brushes]::White,$x,67)
        $image=[Drawing.Bitmap]::FromFile((Join-Path $run "gamma-$($row.Gamma).png"))
        try{
            if($image.Width -ne 800 -or $image.Height -ne 600){throw 'Unexpected input size'}
            $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.DrawImageUnscaled($image,$i*800,100)
            $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceOver
        }finally{$image.Dispose()}
    }
    $canvas.Save((Join-Path $run 'comparison.png'),[Drawing.Imaging.ImageFormat]::Png)
}finally{$graphics.Dispose();$canvas.Dispose();$title.Dispose();$font.Dispose()}
$rows | Format-Table
Write-Output "Comparison images and logs: $run"
