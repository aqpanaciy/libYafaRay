<# Build only the OpenCV modules required by libYafaRay with the same MinGW ABI. #>
[CmdletBinding()]
param(
    [string]$CMake = 'C:/Program Files/CMake/bin/cmake.exe',
    [string]$Toolchain = 'C:/temp/TDM-GCC-64',
    [ValidateRange(1, 64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependencies = Join-Path $repo 'build/dependencies'
$archive = Join-Path $dependencies 'opencv-4.10.0.zip'
$source = Join-Path $dependencies 'opencv-4.10.0'
$build = Join-Path $repo 'build/opencv-4.10.0-tdm-gcc64'
$install = Join-Path $repo 'build/install/opencv-4.10.0-tdm-gcc64'
New-Item -ItemType Directory -Path $dependencies -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://github.com/opencv/opencv/archive/refs/tags/4.10.0.zip' -OutFile $archive
}
$expected = '3810BCA2B1D1C572912DF0AC3888126341F3762DFD28E91068C805FB656D0E51'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    throw 'Unexpected OpenCV archive SHA256; archive was not extracted.'
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
    Expand-Archive -LiteralPath $archive -DestinationPath $dependencies
}
$configure = @(
    '-S', $source, '-B', $build, '-G', 'MinGW Makefiles',
    "-DCMAKE_C_COMPILER=$Toolchain/bin/gcc.exe",
    "-DCMAKE_CXX_COMPILER=$Toolchain/bin/g++.exe",
    "-DCMAKE_MAKE_PROGRAM=$Toolchain/bin/mingw32-make.exe",
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=$install",
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5', '-DBUILD_LIST=core,imgproc,photo',
    '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_TESTS=OFF', '-DBUILD_PERF_TESTS=OFF',
    '-DBUILD_EXAMPLES=OFF', '-DBUILD_opencv_apps=OFF', '-DBUILD_JAVA=OFF',
    '-DBUILD_opencv_python2=OFF', '-DBUILD_opencv_python3=OFF',
    '-DBUILD_opencv_gapi=OFF', '-DWITH_ADE=OFF',
    '-DWITH_JPEG=OFF', '-DWITH_PNG=OFF', '-DWITH_TIFF=OFF', '-DWITH_WEBP=OFF',
    '-DWITH_OPENJPEG=OFF', '-DWITH_JASPER=OFF', '-DWITH_PROTOBUF=OFF', '-DBUILD_PROTOBUF=OFF',
    '-DWITH_IPP=OFF', '-DWITH_ITT=OFF', '-DWITH_OPENCL=OFF',
    '-DWITH_OPENEXR=OFF', '-DWITH_FFMPEG=OFF', '-DWITH_MSMF=OFF',
    '-DWITH_DSHOW=OFF', '-DWITH_VTK=OFF', '-DWITH_TBB=OFF',
    '-DCPU_BASELINE=SSE2', '-DCPU_DISPATCH='
)
& $CMake @configure
if ($LASTEXITCODE) { throw 'OpenCV configuration failed.' }
& $CMake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw 'OpenCV build failed.' }
& $CMake --install $build
if ($LASTEXITCODE) { throw 'OpenCV installation failed.' }
Write-Host "OpenCV installed: $install"
