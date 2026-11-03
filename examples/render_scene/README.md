# Render a simple scene to TGA

This standalone C90 example uses the installed libYafaRay shared library. It
creates a red cube, a gray floor, a rectangular area light, a perspective camera
and a path tracing integrator. The renderer writes a 480 x 360 TGA image with 32 samples per
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
2. Create two materials and triangle meshes for the cube, floor and emitter.
3. Add an area light linked to the emitter mesh and a constant background.
4. Configure path tracing, the film, anti-aliasing and perspective camera.
5. Attach a TGA output, preprocess the scene and render.
6. Check the file and release the library objects.

Material colors, light position/power and camera coordinates are deliberately
defined in the source for easy experimentation.

## Soft shadows and indirect lighting

All settings below are passed through `yafaray_setParamMap*` before creating the
light, integrator or film. They are implemented by the installed shared library;
no optional dependencies or library rebuild are needed.

### Area light: shadow softness

The light's `type = "arealight"` replaces the original point light. A surface
point can see only part of this emitter when the cube blocks it, producing a
gradual shadow edge (penumbra).

| Light parameter | Value in the example | Meaning |
| --- | --- | --- |
| `corner` | `(-4.5, -5.5, 7)` | First corner of the rectangle |
| `point1` | `(-4.5, -2.5, 7)` | End of the first adjacent edge |
| `point2` | `(-1.5, -5.5, 7)` | End of the second adjacent edge |
| `object_name` | `"emitter"` | Matching scene mesh required by this version |
| `samples` | `8` | Samples of the light surface; more reduce shadow noise |
| `power` | `12.0` | Area light intensity, retuned for this scene |
| `color` | `(1.0, 0.95, 0.85, 1.0)` | Slightly warm light |

The rectangle is 3 x 3 scene units. Larger apparent emitter size gives softer
shadows; increasing `samples` makes their estimate cleaner without changing the
physical size. `point1` and `point2` are absolute positions, not edge vectors.
Their ordering makes the one-sided emitter face down toward the scene. Keep the
four `emitter_vertices` in sync with these three parameters when changing it.
Changing emitter size can also change illumination; retune `power` as needed.
Point-light and area-light power values are not interchangeable.

### Path tracing: reflected light

The integrator's `type = "pathtracing"` replaces `"directlighting"`. It samples
diffuse reflection paths, allowing the illuminated floor to bounce light onto the
cube's side facing away from the emitter. The cube also casts some red reflected
light onto the nearby floor. No extra fill light or self-emitting cube material
is used.

| Integrator parameter | Value | Meaning |
| --- | --- | --- |
| `path_samples` | `8` | Number of sampled diffuse paths per camera sample |
| `bounces` | `3` | Maximum diffuse path depth |
| `russian_roulette_min_bounces` | `3` | Avoid early random termination within the chosen depth |
| `threads` | `2` | Render worker count; affects speed, not lighting |

Increasing `path_samples` reduces indirect-light noise at a time cost. Increasing
`bounces` allows longer reflection paths; it does not directly multiply brightness.
The unlit side should remain darker than the directly lit faces.

The constant background remains a backdrop. In this version, the diffuse path
loop skips paths that miss the scene, so merely brightening that background does
not provide the floor-to-cube indirect lighting used here. Ambient occlusion
(`do_AO`) is also not used as a substitute for reflected light.

### Film: sampling and render time

The film uses `AA_passes = 1` and `AA_minsamples = 32` (previously 16). These
control camera/pixel sampling and help with edges and Monte Carlo noise.
They do not enable indirect light or determine shadow softness by themselves.
Camera samples, diffuse paths and area-light samples all contribute work, so
this version takes longer than the original direct-lighting render. For quick
previews, lower `AA_minsamples` or `path_samples`; for cleaner output, raise them.

Parameter names and behavior were checked against this repository's
`include/light/light_area.h`, `src/light/light_area.cc`,
`include/integrator/surface/integrator_path_tracer.h`,
`src/integrator/surface/integrator_path_tracer.cc` and
`include/render/imagefilm.h`.

Verified on 3 November 2026 using the installed shared library from commit
`8f4906ae88f8a527bbce37991a95e3e3857cb9ac`, TDM-GCC-64 10.3.0 and xmake 3.1.2:
build and rendering succeeded; the TGA was decoded and visually inspected.
The updated area-light/path-tracing scene rendered in approximately 41 seconds
on this machine with two render threads. The shadow has a visible penumbra and
the formerly black cube face receives reflected light from the floor. Timing
depends on hardware; some Monte Carlo noise remains at these preview settings.
