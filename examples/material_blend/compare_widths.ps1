[CmdletBinding()]
param([string]$ExistingRunDirectory)
$ErrorActionPreference = 'Stop'
$run = if ($ExistingRunDirectory) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExistingRunDirectory)
} else { Join-Path $PSScriptRoot ('build/width-comparison/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')) }
$widths = @('0.04','0.24','0.80')
if (-not $ExistingRunDirectory) {
    $xmakeExe = @(Get-Command xmake -CommandType Application -ErrorAction Stop)[0].Source
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    $rows = @()
    Push-Location $PSScriptRoot
    try {
        foreach ($width in $widths) {
            $watch = [Diagnostics.Stopwatch]::StartNew()
            & $xmakeExe run yafaray_material_blend (Join-Path $run "width-$width.png") --transition-width $width --wave-amplitude 0.42 --aa-samples 16 --path-samples 4 --light-samples 8 --threads -1 2>&1 |
                Tee-Object -FilePath (Join-Path $run "width-$width.log") | Out-Host
            $code = $LASTEXITCODE
            $watch.Stop()
            if ($code) { throw "Render failed: width=$width" }
            $match = [regex]::Match((Get-Content (Join-Path $run "width-$width.log") -Raw),'Overall rendertime: ([0-9.]+)s')
            if (-not $match.Success) { throw 'Missing render timing' }
            $rows += [pscustomobject]@{Width=$width; Amplitude='0.42'; AA=16; PathSamples=4; LightSamples=8;
                RenderSeconds=$match.Groups[1].Value;
                ElapsedSeconds=$watch.Elapsed.TotalSeconds.ToString('F3',[Globalization.CultureInfo]::InvariantCulture)}
            $rows | Export-Csv -LiteralPath (Join-Path $run 'timings.csv') -NoTypeInformation
        }
    } finally { Pop-Location }
}
$rows = @(Import-Csv -LiteralPath (Join-Path $run 'timings.csv'))
if (($rows.Width -join ',') -ne ($widths -join ',') -or
    @($rows | Where-Object { $_.AA -ne 16 -or $_.PathSamples -ne 4 -or $_.LightSamples -ne 8 -or $_.Amplitude -ne '0.42' }).Count) {
    throw 'Unexpected comparison profiles or settings'
}
Add-Type -AssemblyName System.Drawing
foreach ($detail in @($false,$true)) {
    $column = if ($detail) {480} else {800}
    $height = if ($detail) {480} else {600}
    $name = if ($detail) {'comparison-detail.png'} else {'comparison.png'}
    $canvas = [Drawing.Bitmap]::new(3*$column,$height+90)
    $graphics = [Drawing.Graphics]::FromImage($canvas)
    $titleFont = [Drawing.Font]::new('Segoe UI',24,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
    $textFont = [Drawing.Font]::new('Segoe UI',16,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(28,33,42))
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        for($i=0;$i -lt 3;++$i) {
            $row=$rows[$i]
            $x=[single]($i*$column+14)
            $graphics.DrawString("width=$($row.Width) | amplitude=0.42",$titleFont,[Drawing.Brushes]::White,$x,9)
            $graphics.DrawString('AA=16 | path=4 | light=8 | threads=auto',$textFont,[Drawing.Brushes]::White,$x,40)
            $graphics.DrawString("render=$($row.RenderSeconds)s | total=$($row.ElapsedSeconds)s",$textFont,[Drawing.Brushes]::White,$x,63)
            $image=[Drawing.Bitmap]::FromFile((Join-Path $run "width-$($row.Width).png"))
            try {
                if ($image.Width -ne 800 -or $image.Height -ne 600) { throw 'Unexpected input dimensions' }
                # Copy pixels/alpha instead of compositing them against the label background.
                $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
                if ($detail) {
                    $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
                    $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::Half
                    $destination=[Drawing.Rectangle]::new($i*$column,90,$column,$height)
                    $graphics.DrawImage($image,$destination,280,140,240,240,[Drawing.GraphicsUnit]::Pixel)
                } else { $graphics.DrawImageUnscaled($image,$i*$column,90) }
                $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceOver
            } finally { $image.Dispose() }
        }
        $canvas.Save((Join-Path $run $name),[Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose(); $canvas.Dispose(); $titleFont.Dispose(); $textFont.Dispose()
    }
}
$rows | Format-Table
Write-Output "Comparison images and logs: $run"
