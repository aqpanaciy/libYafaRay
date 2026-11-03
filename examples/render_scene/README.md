# Render a simple scene to TGA

This standalone C90 example uses the installed libYafaRay shared library. It
creates a red cube, a gray floor, a point light, a perspective camera and a direct
lighting integrator. The renderer writes a 480 x 360 TGA image with 16 samples per
pixel. No textures, scene files or optional image libraries are required.

The library continues to use CMake. If it is not installed yet, follow the
[TDM-GCC-64 library build instructions](../hello/README.md#windows-tdm-gcc-64)
from the repository root. The default installation prefix is
`build/install/tdm-gcc64-shared` in the repository.

## Build and run

From the repository root in PowerShell:

```powershell
Set-Location examples/render_scene
xmake f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64
xmake
xmake run yafaray_render_scene
```

The image is saved to `examples/render_scene/build/scene.tga` relative to the
repository root. The run directory is `examples/render_scene/build`, so generated
images stay in the ignored build directory. An existing output file is overwritten.

To choose an output path, pass an absolute path with an existing parent directory:

```powershell
xmake run yafaray_render_scene "C:/path/to/result.tga"
```

The filename must end with `.tga` or `.TGA`. A relative output path is resolved
against the run directory. The example checks API compatibility, scene setup,
preprocessing, and the saved TGA header and size; failures return a nonzero code.

For a different installed MinGW x64 shared library:

```powershell
xmake f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64 --yafaray_prefix=C:/path/to/install
```

Only installed headers and `lib/liblibyafaray4.dll.a` are used for compilation and
linking. `xmake run` adds the installation's `bin` directory to the child process's
PATH for loading `libyafaray4.dll`; the global environment is unchanged. Direct
executable launches need this DLL directory and the toolchain runtimes in PATH.

## Scene walkthrough

`render_scene.c` follows these steps:

1. Create the logger, scene and parameter maps.
2. Create two materials and triangle meshes for the cube and floor.
3. Add a point light and constant background.
4. Configure direct lighting, the film, anti-aliasing and perspective camera.
5. Attach a TGA output, preprocess the scene and render.
6. Check the file and release the library objects.

Material colors, light position/power and camera coordinates are deliberately
defined in the source for easy experimentation. Direct lighting gives the cube
hard shadows and a dark unlit side; indirect illumination is outside this example.

Verified on 3 November 2026 using the installed shared library from commit
`8f4906ae88f8a527bbce37991a95e3e3857cb9ac`, TDM-GCC-64 10.3.0 and xmake 3.1.2:
build and rendering succeeded; the TGA was decoded and visually inspected.
