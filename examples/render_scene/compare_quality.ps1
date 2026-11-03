<#
.SYNOPSIS
Render three quality profiles, record wall time and create comparison PNGs.
.EXAMPLE
.\compare_quality.ps1 -Threads 2
#>
[CmdletBinding()]
param(
    [ValidateScript({ $_ -eq -1 -or $_ -gt 0 })]
    [int]$Threads = -1,

    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build/quality-comparison'),

    [string]$ExistingRunDirectory
)

$ErrorActionPreference = 'Stop'
# Check the image-conversion dependency before starting expensive renders.
$pythonCommand = @(Get-Command python -CommandType Application -ErrorAction Stop)[0].Source
& $pythonCommand -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.8 or newer is required to create comparison PNGs.' }

function New-ComparisonImages([string]$Directory) {
    # The CSV belongs to this run; never substitute hard-coded timings.
    $timings = @(Import-Csv -LiteralPath (Join-Path $Directory 'timings.csv'))
    $labels = foreach ($name in @('preview', 'baseline', 'quality')) {
        $row = @($timings | Where-Object { $_.Profile -eq $name })
        if ($row.Count -ne 1) { throw "Expected one timing row for '$name'." }
        foreach ($field in @('AaSamples', 'PathSamples', 'LightSamples', 'RequestedThreads')) {
            if ($row[0].$field -notmatch '^-?\d+$') { throw "Invalid $field for '$name'." }
        }
        $seconds = [double]::Parse($row[0].ElapsedSeconds.Replace(',', '.'), [Globalization.CultureInfo]::InvariantCulture)
        if ([double]::IsNaN($seconds) -or [double]::IsInfinity($seconds) -or $seconds -lt 0) {
            throw "Invalid elapsed time for '$name'."
        }
        [pscustomobject]@{
            Name = $name
            Samples = "AA_minsamples=$($row[0].AaSamples)  path_samples=$($row[0].PathSamples)"
            Light = "samples=$($row[0].LightSamples)  threads=$($row[0].RequestedThreads)" + $(if ($row[0].RequestedThreads -eq '-1') { ' (auto)' } else { '' })
            Time = 'Elapsed: ' + $seconds.ToString('F2', [Globalization.CultureInfo]::InvariantCulture) + ' s (incl. launch + save)'
        }
    }
    Add-Type -AssemblyName System.Drawing
    & $pythonCommand (Join-Path $PSScriptRoot 'create_comparison.py') $Directory
    if ($LASTEXITCODE -ne 0) { throw "Failed to create comparison PNGs in $Directory" }

    foreach ($filename in @('comparison.png', 'comparison-detail.png')) {
        $path = Join-Path $Directory $filename
        $source = $canvas = $graphics = $titleFont = $textFont = $brush = $mutedBrush = $null
        try {
            $source = [Drawing.Bitmap]::FromFile($path)
            $headerHeight = 132
            $canvas = [Drawing.Bitmap]::new($source.Width, $source.Height + $headerHeight)
            $graphics = [Drawing.Graphics]::FromImage($canvas)
            $graphics.Clear([Drawing.Color]::FromArgb(28, 33, 42))
            $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
            $titleFont = [Drawing.Font]::new('Segoe UI', 21, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
            $textFont = [Drawing.Font]::new('Segoe UI', 15, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
            $brush = [Drawing.SolidBrush]::new([Drawing.Color]::White)
            $mutedBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(205, 214, 227))
            for ($i = 0; $i -lt $labels.Count; ++$i) {
                $x = [single]($i * 480 + 16)
                $title = $labels[$i].Name + $(if ($filename -eq 'comparison-detail.png') { ' - shadow detail 3x' } else { ' - 480 x 360' })
                $graphics.DrawString($title, $titleFont, $brush, $x, [single]8)
                $graphics.DrawString($labels[$i].Samples, $textFont, $mutedBrush, $x, [single]42)
                $graphics.DrawString($labels[$i].Light, $textFont, $mutedBrush, $x, [single]68)
                $graphics.DrawString($labels[$i].Time, $textFont, $brush, $x, [single]94)
            }
            # Copy image pixels at their original size; labels never cover them.
            $graphics.DrawImageUnscaled($source, 0, $headerHeight)
            $source.Dispose()
            $source = $null
            $canvas.Save($path, [Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            foreach ($resource in @($graphics, $canvas, $source, $titleFont, $textFont, $brush, $mutedBrush)) {
                if ($null -ne $resource) { $resource.Dispose() }
            }
        }
    }
}

if ($ExistingRunDirectory) {
    $existingPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExistingRunDirectory)
    New-ComparisonImages $existingPath
    return
}

$xmakeCommand = @(Get-Command xmake -CommandType Application -ErrorAction Stop)[0].Source
$profiles = @(
    [pscustomobject]@{ Name = 'preview'; AaSamples = 4; PathSamples = 2; LightSamples = 2 },
    [pscustomobject]@{ Name = 'baseline'; AaSamples = 32; PathSamples = 8; LightSamples = 8 },
    [pscustomobject]@{ Name = 'quality'; AaSamples = 64; PathSamples = 16; LightSamples = 16 }
)

# A new subdirectory per run keeps earlier comparisons intact.
$outputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$runDirectory = Join-Path $outputRoot (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
New-Item -ItemType Directory -Path $runDirectory -ErrorAction Stop | Out-Null
$results = @()
$totalWatch = [System.Diagnostics.Stopwatch]::StartNew()
Write-Host "Results: $runDirectory"
Write-Host "Threads: $Threads (-1 = automatic detection). Profiles run sequentially."

Push-Location $PSScriptRoot
try {
    foreach ($profile in $profiles) {
        $stem = '{0}-aa{1}-path{2}-light{3}' -f $profile.Name, $profile.AaSamples, $profile.PathSamples, $profile.LightSamples
        $imagePath = Join-Path $runDirectory "$stem.tga"
        $logPath = Join-Path $runDirectory "$stem.log"
        $renderArguments = @(
            'run', 'yafaray_render_scene', $imagePath,
            '--aa-samples', "$($profile.AaSamples)",
            '--path-samples', "$($profile.PathSamples)",
            '--light-samples', "$($profile.LightSamples)",
            "--threads=$Threads"
        )
        Write-Host "Starting $($profile.Name)..."
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        & $xmakeCommand @renderArguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
        $renderExitCode = $LASTEXITCODE
        $watch.Stop()
        if ($renderExitCode -ne 0) {
            throw "Profile '$($profile.Name)' failed (exit $renderExitCode). See $logPath"
        }
        if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
            throw "Profile '$($profile.Name)' produced no TGA. See $logPath"
        }
        $results += [pscustomobject]@{
            Profile = $profile.Name
            AaSamples = $profile.AaSamples
            PathSamples = $profile.PathSamples
            LightSamples = $profile.LightSamples
            RequestedThreads = $Threads
            ElapsedSeconds = [Math]::Round($watch.Elapsed.TotalSeconds, 3)
            Image = $imagePath
            Log = $logPath
        }
        # Preserve completed measurements even if a later profile fails.
        $results | Export-Csv -LiteralPath (Join-Path $runDirectory 'timings.csv') -NoTypeInformation -Encoding UTF8
        Write-Host ("Completed {0}: {1:N2} seconds" -f $profile.Name, $watch.Elapsed.TotalSeconds)
    }
    New-ComparisonImages $runDirectory
}
finally {
    $totalWatch.Stop()
    Pop-Location
    Write-Host ("Total elapsed: {0:N2} seconds" -f $totalWatch.Elapsed.TotalSeconds)
}

$results | Format-Table Profile, AaSamples, PathSamples, LightSamples, RequestedThreads, ElapsedSeconds -AutoSize
Write-Host "TGA images, comparison PNGs, logs and timings.csv: $runDirectory"
