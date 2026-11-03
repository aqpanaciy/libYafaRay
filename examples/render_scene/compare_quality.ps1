<#
.SYNOPSIS
Render three quality profiles sequentially and record elapsed wall time.
.EXAMPLE
.\compare_quality.ps1 -Threads 2
#>
[CmdletBinding()]
param(
    [ValidateScript({ $_ -eq -1 -or $_ -gt 0 })]
    [int]$Threads = -1,

    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build/quality-comparison')
)

$ErrorActionPreference = 'Stop'
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
}
finally {
    $totalWatch.Stop()
    Pop-Location
    Write-Host ("Total elapsed: {0:N2} seconds" -f $totalWatch.Elapsed.TotalSeconds)
}

$results | Format-Table Profile, AaSamples, PathSamples, LightSamples, RequestedThreads, ElapsedSeconds -AutoSize
Write-Host "Images, logs and timings.csv: $runDirectory"
