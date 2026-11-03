[CmdletBinding()]
param(
    [ValidateRange(1, 2147483647)][int]$AaSamples = 16,
    [ValidateScript({ $_ -eq -1 -or $_ -gt 0 })][int]$Threads = -1,
    [switch]$AnisotropyComparison,
    [string]$ExistingRunDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $ExistingRunDirectory) {
    $xmakeExe = @(Get-Command xmake -CommandType Application -ErrorAction Stop)[0].Source
}
$pythonExe = @(Get-Command python -CommandType Application -ErrorAction Stop)[0].Source
$group = if ($AnisotropyComparison) { 'anisotropy-comparison' } else { 'filter-comparison' }
$run = if ($ExistingRunDirectory) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExistingRunDirectory)
} else {
    Join-Path $PSScriptRoot ("build/$group/" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$profiles = if ($AnisotropyComparison) {
    foreach ($limit in @(1,4,8,16)) {
        [pscustomobject]@{ Name = "ewa-$limit"; Filter = 'mipmap_ewa'; Anisotropy = $limit }
    }
} else {
    foreach ($filter in @('bilinear','mipmap_trilinear','mipmap_ewa')) {
        [pscustomobject]@{ Name = $filter; Filter = $filter; Anisotropy = 8 }
    }
}
if (-not $ExistingRunDirectory) { New-Item -ItemType Directory -Path $run | Out-Null }
$results = @()
if ($ExistingRunDirectory) {
    $rows = @(Import-Csv -LiteralPath (Join-Path $run 'timings.csv'))
    foreach ($profile in $profiles) {
        $match = @($rows | Where-Object { $_.Profile -eq $profile.Name -or
            (-not $_.Profile -and $_.Filter -eq $profile.Filter -and -not $AnisotropyComparison) })
        if ($match.Count -ne 1) { throw "Expected one timing row for $($profile.Name)" }
        $row = $match[0]
        if ($row.Filter -ne $profile.Filter -or
            ($row.EwaMaxAnisotropy -and [double]$row.EwaMaxAnisotropy -ne $profile.Anisotropy)) {
            throw "Unexpected filter/anisotropy metadata for $($profile.Name)"
        }
        $results += [pscustomobject]@{ Profile = $profile.Name; Filter = $profile.Filter;
            EwaMaxAnisotropy = $profile.Anisotropy; AaSamples = [int]$row.AaSamples;
            Threads = [int]$row.Threads; ElapsedSeconds = [double]::Parse(
                $row.ElapsedSeconds.Replace(',', '.'), [Globalization.CultureInfo]::InvariantCulture) }
    }
    $AaSamples = $results[0].AaSamples
    $Threads = $results[0].Threads
    if (@($results | Where-Object { $_.AaSamples -ne $AaSamples -or $_.Threads -ne $Threads }).Count) {
        throw 'Existing comparison must use identical AA and thread settings.'
    }
}
Push-Location $PSScriptRoot
try {
    if (-not $ExistingRunDirectory) { foreach ($profile in $profiles) {
        $arguments = @('run', 'yafaray_texture_horizon', (Join-Path $run "$($profile.Name).tga"),
            '--filter', $profile.Filter, '--aa-samples', "$AaSamples", '--threads', "$Threads",
            '--ewa-max-anisotropy', "$($profile.Anisotropy)")
        $watch = [Diagnostics.Stopwatch]::StartNew()
        & $xmakeExe @arguments 2>&1 | Tee-Object -FilePath (Join-Path $run "$($profile.Name).log") | Out-Host
        $renderExitCode = $LASTEXITCODE
        $watch.Stop()
        if ($renderExitCode) { throw "Render failed: $($profile.Name)" }
        $results += [pscustomobject]@{ Profile = $profile.Name; Filter = $profile.Filter;
            EwaMaxAnisotropy = $profile.Anisotropy; AaSamples = $AaSamples; Threads = $Threads;
            ElapsedSeconds = [Math]::Round($watch.Elapsed.TotalSeconds, 3) }
        $results | Export-Csv -LiteralPath (Join-Path $run 'timings.csv') -NoTypeInformation -Encoding UTF8
    } }
    $imageArguments = @((Join-Path $PSScriptRoot 'create_filter_comparison.py'), $run, '--names') + @($profiles.Name)
    & $pythonExe @imageArguments
    if ($LASTEXITCODE) { throw 'Image comparison conversion failed.' }
    Add-Type -AssemblyName System.Drawing
    foreach ($name in @('comparison.png', 'comparison-horizon.png')) {
        $path = Join-Path $run $name
        $image = $canvas = $graphics = $font = $textFont = $brush = $null
        try {
            $image = [Drawing.Bitmap]::FromFile($path)
            $canvas = [Drawing.Bitmap]::new($image.Width, $image.Height + 90)
            $graphics = [Drawing.Graphics]::FromImage($canvas)
            $graphics.Clear([Drawing.Color]::FromArgb(28,33,42))
            $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
            $columnWidth = $image.Width / $results.Count
            $titleSize = if ($columnWidth -lt 800) { 21 } else { 26 }
            $textSize = if ($columnWidth -lt 800) { 14 } else { 20 }
            $font = [Drawing.Font]::new('Segoe UI', $titleSize, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
            $textFont = [Drawing.Font]::new('Segoe UI', $textSize, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
            $brush = [Drawing.SolidBrush]::new([Drawing.Color]::White)
            for ($i = 0; $i -lt $results.Count; ++$i) {
                $row = $results[$i]
                $x = [single]($i * $columnWidth + 16)
                $title = $row.Filter
                if ($AnisotropyComparison) { $title += "  anisotropy=$($row.EwaMaxAnisotropy)" }
                $graphics.DrawString($title, $font, $brush, $x, [single]8)
                $seconds = $row.ElapsedSeconds.ToString('F2', [Globalization.CultureInfo]::InvariantCulture)
                $label = "AA=$AaSamples  threads=$Threads  elapsed=$seconds s"
                if ($name -eq 'comparison-horizon.png') { $label += '  |  horizon 2x' }
                $graphics.DrawString($label, $textFont, $brush, $x, [single]48)
            }
            $graphics.DrawImageUnscaled($image, 0, 90)
            $image.Dispose(); $image = $null
            $canvas.Save($path, [Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            foreach ($resource in @($graphics,$canvas,$image,$font,$textFont,$brush)) {
                if ($null -ne $resource) { $resource.Dispose() }
            }
        }
    }
}
finally { Pop-Location }
$results | Format-Table -AutoSize
Write-Host "Comparison images and logs: $run"
