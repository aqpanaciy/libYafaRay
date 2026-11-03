# Minimal libYafaRay client with xmake

The library keeps its CMake build. This MinGW example uses C90 and only the
installed public headers and shared library, with no library source files or
build-tree headers. It prints the runtime version, checks the API major version,
and creates and destroys a parameter map. It does not render an image yet.

## Windows: TDM-GCC-64

Requirements: CMake >= 3.21 (for the supplied preset), xmake >= 2.8, and
TDM-GCC-64 at `C:\temp\TDM-GCC-64`. The library itself still supports its
existing CMake minimum when configured without presets.

Run in PowerShell from the repository root:

```powershell
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
& $cmake --preset tdm-gcc64-shared
& $cmake --build --preset tdm-gcc64-shared --parallel 4
& $cmake --install build/tdm-gcc64-shared
```

The preset builds a Release shared library without optional dependencies or the
existing CMake examples, using the requested compiler and make program. It
installs into `build/install/tdm-gcc64-shared` inside the repository.

Build and run the external example:

```powershell
Set-Location examples/hello
xmake f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64
xmake
xmake run yafaray_hello
```

To use a different installed shared library, configure its prefix explicitly:

```powershell
xmake f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64 --yafaray_prefix=C:/path/to/install
```

The prefix must contain `include/yafaray_c_api.h`,
`include/yafaray_c_api_export.h`, `lib/liblibyafaray4.dll.a`, and
`bin/libyafaray4.dll`. The import library name is the existing CMake output name.
Use an installation built for the same architecture and compatible toolchain.

`xmake run` adds the installation's `bin` directory to the child process's PATH.
Launching the executable directly requires that directory in PATH, and any
compiler runtime DLLs required by the selected toolchain must also be available.
No global environment variables are changed by the example configuration.

Output verified on 3 November 2026 with TDM-GCC-64 10.3.0, CMake 4.4.5,
and xmake 3.1.2:

```text
libYafaRay 4.0.0-pre-alpha+git.g8f4906ae
C API: parameter map created and destroyed successfully.
```

The version suffix depends on the source revision and working-tree state.
The configure, build, install, example build, and run all completed successfully.
