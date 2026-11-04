<# Build the libjpeg-compatible static library with the project's compiler. #>
[CmdletBinding()]
param(
    [string]$CMake = 'C:/Program Files/CMake/bin/cmake.exe',
    [string]$Toolchain = 'C:/temp/TDM-GCC-64',
    [ValidateRange(1,64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependencies = Join-Path $repo 'build/dependencies'
$archive = Join-Path $dependencies 'libjpeg-turbo-3.2.0.tar.gz'
$source = Join-Path $dependencies 'libjpeg-turbo-3.2.0'
$build = Join-Path $repo 'build/libjpeg-turbo-3.2.0-tdm-gcc64'
$install = Join-Path $repo 'build/install/jpeg-tdm-gcc64'
New-Item -ItemType Directory -Path $dependencies -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '6F30092CEF9FB839779646608F4EE14AE3CBAC989C47FA05E841B0841F09878E') {
    throw 'Unexpected libjpeg-turbo source SHA256'
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
    & tar -xf $archive -C $dependencies
    if ($LASTEXITCODE) { throw 'JPEG source extraction failed' }
}
$configure = @('-S',$source,'-B',$build,'-G','MinGW Makefiles',
    "-DCMAKE_C_COMPILER=$Toolchain/bin/gcc.exe",
    "-DCMAKE_MAKE_PROGRAM=$Toolchain/bin/mingw32-make.exe",
    '-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_INSTALL_PREFIX=$install",
    '-DENABLE_SHARED=OFF','-DENABLE_STATIC=ON','-DWITH_SIMD=OFF',
    '-DWITH_TURBOJPEG=OFF','-DWITH_TOOLS=ON','-DWITH_TESTS=ON')
& $CMake @configure
if ($LASTEXITCODE) { throw 'JPEG configuration failed' }
& $CMake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw 'JPEG build failed' }
& (Join-Path (Split-Path $CMake) 'ctest.exe') --test-dir $build --output-on-failure --parallel $Jobs -C Release
if ($LASTEXITCODE) { throw 'JPEG regression tests failed' }
& $CMake --install $build
if ($LASTEXITCODE) { throw 'JPEG installation failed' }
Write-Output "JPEG installed: $install"
