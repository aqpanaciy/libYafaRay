[CmdletBinding()]
param([string]$ExistingRunDirectory)
$ErrorActionPreference = 'Stop'
$run = if ($ExistingRunDirectory) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExistingRunDirectory)
} else { Join-Path $PSScriptRoot ('build/depth-comparison/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')) }
$pythonExe = @(Get-Command python -CommandType Application -ErrorAction Stop)[0].Source
if (-not $ExistingRunDirectory) {
    $xmakeExe = @(Get-Command xmake -CommandType Application -ErrorAction Stop)[0].Source
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    $rows = @()
    Push-Location $PSScriptRoot
    try {
        foreach ($depth in @(8,16,32,64)) {
            $watch = [Diagnostics.Stopwatch]::StartNew()
            & $xmakeExe run yafaray_mirror_tunnel (Join-Path $run "depth-$depth.tga") --ray-depth $depth --aa-samples 16 --path-samples 4 --light-samples 4 --threads -1 2>&1 |
                Tee-Object -FilePath (Join-Path $run "depth-$depth.log") | Out-Host
            $code = $LASTEXITCODE
            $watch.Stop()
            if ($code) { throw "Render failed at depth $depth" }
            $log = Get-Content -LiteralPath (Join-Path $run "depth-$depth.log") -Raw
            $match = [regex]::Match($log, 'Overall rendertime: ([0-9.]+)s')
            if (-not $match.Success) { throw 'Missing renderer timing' }
            $workers = [regex]::Match($log, "Renderer 'path' using \[(\d+)\] Threads")
            if (-not $workers.Success) { throw 'Missing renderer worker count' }
            $rows += [pscustomobject]@{ Depth=$depth; AA=16; PathSamples=4; LightSamples=4;
                Threads=[int]$workers.Groups[1].Value; RenderSeconds=$match.Groups[1].Value;
                ElapsedSeconds=$watch.Elapsed.TotalSeconds.ToString('F3',[Globalization.CultureInfo]::InvariantCulture) }
            $rows | Export-Csv -LiteralPath (Join-Path $run 'timings.csv') -NoTypeInformation
        }
    } finally { Pop-Location }
}
$rows = @(Import-Csv -LiteralPath (Join-Path $run 'timings.csv'))
if (($rows.Depth -join ',') -ne '8,16,32,64' -or
    @($rows | Where-Object { $_.AA -ne 16 -or $_.PathSamples -ne 4 -or $_.LightSamples -ne 4 }).Count) {
    throw 'Expected depths 8,16,32,64 with AA=16 and path/light samples=4'
}
& $pythonExe -B (Join-Path $PSScriptRoot 'create_depth_comparison.py') $run
if ($LASTEXITCODE) { throw 'Comparison conversion failed' }
Add-Type -AssemblyName System.Drawing
foreach ($name in @('comparison.png','comparison-end.png')) {
    $path = Join-Path $run $name
    $source = $canvas = $graphics = $titleFont = $detailFont = $null
    try {
        $source = [Drawing.Bitmap]::FromFile($path)
        $canvas = [Drawing.Bitmap]::new($source.Width, $source.Height + 90)
        $graphics = [Drawing.Graphics]::FromImage($canvas)
        $graphics.Clear([Drawing.Color]::FromArgb(28,33,42))
        $graphics.DrawImageUnscaled($source,0,90)
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $titleFont = [Drawing.Font]::new('Segoe UI',24,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
        $detailFont = [Drawing.Font]::new('Segoe UI',16,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
        for ($i=0; $i -lt 4; ++$i) {
            $row = $rows[$i]
            $x = [single]($i * $source.Width / 4 + 14)
            $graphics.DrawString("ray-depth=$($row.Depth)",$titleFont,[Drawing.Brushes]::White,$x,9)
            $graphics.DrawString("AA=16 | path=4 | light=4 | threads=$($row.Threads)",$detailFont,[Drawing.Brushes]::White,$x,40)
            $suffix = if ($name -eq 'comparison-end.png') { ' | end 4x' } else { '' }
            $graphics.DrawString("render=$($row.RenderSeconds)s | total=$($row.ElapsedSeconds)s$suffix",$detailFont,[Drawing.Brushes]::White,$x,63)
        }
        $source.Dispose(); $source = $null
        $canvas.Save($path,[Drawing.Imaging.ImageFormat]::Png)
    } finally {
        foreach ($resource in @($graphics,$source,$canvas,$titleFont,$detailFont)) {
            if ($resource) { $resource.Dispose() }
        }
    }
}
$rows | Format-Table
Write-Output "Comparison images and logs: $run"
