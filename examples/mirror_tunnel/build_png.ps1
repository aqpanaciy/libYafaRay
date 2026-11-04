<# Build pinned PNG dependencies using the same compiler as libYafaRay. #>
[CmdletBinding()]
param(
    [string]$CMake = 'C:/Program Files/CMake/bin/cmake.exe',
    [string]$Toolchain = 'C:/temp/TDM-GCC-64',
    [ValidateRange(1,64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependencies = Join-Path $repo 'build/dependencies'
$install = Join-Path $repo 'build/install/png-tdm-gcc64'
New-Item -ItemType Directory -Path $dependencies -Force | Out-Null
$packages = @(
    @{Name='zlib-1.3.2'; Archive='zlib.tar.gz'; Url='https://zlib.net/fossils/zlib-1.3.2.tar.gz'; Hash='BB329A0A2CD0274D05519D61C667C062E06990D72E125EE2DFA8DE64F0119D16'},
    @{Name='libpng-1.6.59'; Archive='libpng-1.6.59.zip'; Url='https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.zip'; Hash='6FD97A1E5E2AC55CEC0A538A9800A351EB3844086736291844EBD12DB72BA433'}
)
foreach ($package in $packages) {
    $archive = Join-Path $dependencies $package.Archive
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $package.Url -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.Hash) {
        throw "Unexpected SHA256: $archive"
    }
    $source = Join-Path $dependencies $package.Name
    if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
        if ($package.Archive.EndsWith('.zip')) {
            Expand-Archive -LiteralPath $archive -DestinationPath $dependencies
        } else {
            & tar -xf $archive -C $dependencies
            if ($LASTEXITCODE) { throw 'Source extraction failed' }
        }
    }
    $build = Join-Path $repo "build/$($package.Name)-tdm-gcc64"
    $configure = @('-S',$source,'-B',$build,'-G','MinGW Makefiles',
        "-DCMAKE_C_COMPILER=$Toolchain/bin/gcc.exe",
        "-DCMAKE_MAKE_PROGRAM=$Toolchain/bin/mingw32-make.exe",
        '-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_INSTALL_PREFIX=$install")
    if ($package.Name.StartsWith('zlib')) {
        $configure += @('-DZLIB_BUILD_SHARED=OFF','-DZLIB_BUILD_STATIC=ON','-DZLIB_BUILD_TESTING=ON')
    } else {
        $configure += @("-DZLIB_ROOT=$install",'-DZLIB_USE_STATIC_LIBS=ON',
            '-DPNG_SHARED=OFF','-DPNG_STATIC=ON','-DPNG_TESTS=OFF','-DPNG_TOOLS=OFF')
    }
    & $CMake @configure
    if ($LASTEXITCODE) { throw "Configuration failed: $($package.Name)" }
    & $CMake --build $build --parallel $Jobs
    if ($LASTEXITCODE) { throw "Build failed: $($package.Name)" }
    $ctest = Join-Path (Split-Path $CMake) 'ctest.exe'
    if ($package.Name.StartsWith('zlib')) {
        & $ctest --test-dir $build --output-on-failure -C Release
        if ($LASTEXITCODE) { throw "Dependency tests failed: $($package.Name)" }
    }
    & $CMake --install $build
    if ($LASTEXITCODE) { throw "Installation failed: $($package.Name)" }
    if ($package.Name.StartsWith('libpng')) {
        # Upstream CMake enables its test suite only for shared builds.
        # Run upstream pngtest against our installed static libraries instead.
        $testExe = Join-Path $build 'pngtest-static.exe'
        & "$Toolchain/bin/gcc.exe" (Join-Path $source 'pngtest.c') "-I$install/include" "-L$install/lib" -lpng -lzs -lm -o $testExe
        if ($LASTEXITCODE) { throw 'Static pngtest compilation failed' }
        Push-Location $build
        try {
            & $testExe (Join-Path $source 'pngtest.png')
            if ($LASTEXITCODE) { throw 'Static pngtest failed' }
        } finally { Pop-Location }
    }
}
Write-Host "PNG dependencies installed: $install"
