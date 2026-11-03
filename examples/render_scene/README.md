# Render a simple scene to TGA

This standalone C90 example uses the installed libYafaRay shared library. It
creates a red-and-cream checkerboard cube, mirror and glass spheres, a gray floor,
a rectangular area light, a perspective camera
and a path tracing integrator. The renderer writes a 480 x 360 TGA image with 32
camera samples per pixel by default. No external textures, scene files or optional image
libraries are required.

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

## Compare quality and rendering time without rebuilding

After building once, pass these options to `xmake run yafaray_render_scene`:

| Option | libYafaRay parameter | Default | Allowed values |
| --- | --- | --- | --- |
| `--aa-samples N` | Film `AA_minsamples` | `32` | Positive integer |
| `--path-samples N` | Integrator `path_samples` | `8` | Positive integer |
| `--light-samples N` | Area light `samples` | `8` | Positive integer |
| `--threads N` | Integrator `threads` | `2` | Positive integer, or `-1` for automatic detection |

Integers must fit in a C `int` (up to 2147483647 for this toolchain). Very large
values can be impractical. Zero, fractions, missing values and unknown options
are rejected before an output file is opened. `--name=N` is also supported.
Repeated options use the last value. `--help` shows usage without rendering.
The output filename is optional and can appear before or after the options;
omitting it uses `scene.tga`. Existing invocations with just a filename still work.

Run these commands from `examples/render_scene`:

```powershell
# Fast, noisy preview
xmake run yafaray_render_scene preview.tga --aa-samples 4 --path-samples 2 --light-samples 2 --threads 2

# Previous quality settings, explicitly specified
xmake run yafaray_render_scene baseline.tga --aa-samples 32 --path-samples 8 --light-samples 8 --threads 2

# More samples for cleaner indirect light and shadows; takes longer
xmake run yafaray_render_scene quality.tga --aa-samples 64 --path-samples 16 --light-samples 16 --threads 2

# Let libYafaRay detect the worker count
xmake run yafaray_render_scene auto.tga --threads=-1

# Help only
xmake run yafaray_render_scene --help
```

These files appear in `examples/render_scene/build`. At startup the example
prints all four selected settings. libYafaRay reports its actual worker count
and `Overall rendertime` in seconds at the end of the render. Compare those
times with separate output images; the renderer time excludes example startup
and the final TGA write. For a quality comparison keep the worker count fixed
and change one sampling option at a time. To compare parallel performance,
keep all sample counts fixed and vary only `--threads`. Timings can vary with
other work on the machine, and Monte Carlo noise may vary between runs.

For a different installed MinGW x64 shared library:

```powershell
xmake f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64 --yafaray_prefix=C:/path/to/install
```

## Run all quality profiles

Requirements for the comparison script: Python 3.8 or newer available as
`python`, in addition to Windows PowerShell/PowerShell on Windows and the
configured xmake example. Image conversion uses the Python standard library;
labels use Windows .NET System.Drawing and the Segoe UI font. Pillow and external
converters are not needed. Python is checked before rendering starts.

After configuring and building the example once, run from the repository root:

```powershell
.\examples\render_scene\compare_quality.ps1
```

Or from `examples/render_scene`:

```powershell
.\compare_quality.ps1 -Threads 2
.\compare_quality.ps1 -Threads -1 -OutputDirectory 'C:/renders/libYafaRay comparison'
```

The script runs three profiles sequentially through `xmake run`, without
rebuilding. The default `-Threads -1` lets the library select the worker count;
use the same explicit count for repeatable comparisons.

| Profile | `AA_minsamples` | `path_samples` | Light `samples` | TGA filename |
| --- | --- | --- | --- | --- |
| preview | 4 | 2 | 2 | `preview-aa4-path2-light2.tga` |
| baseline | 32 | 8 | 8 | `baseline-aa32-path8-light8.tga` |
| quality | 64 | 16 | 16 | `quality-aa64-path16-light16.tga` |

Every run gets a new timestamped subdirectory under
`examples/render_scene/build/quality-comparison` (or `-OutputDirectory`). Each
image has a matching `.log`; `timings.csv` records settings, requested threads,
elapsed seconds and paths. Relative `-OutputDirectory` paths are relative to the
directory from which the script is called. The script prints per-profile and
total time, stops on failure, and preserves already completed measurements.

After all three renders succeed, the script automatically creates:

- `comparison.png`: the three complete images side by side, with a heading
  above each panel, 1440 x 492 pixels in total.
- `comparison-detail.png`: identical 160 x 120 crops of the dark face and shadow,
  enlarged 3x without smoothing, with the same headings, also 1440 x 492 pixels.

Both use the order **preview — baseline — quality**, from left to right. RGB
values are copied from the TGA files without color correction or denoising.
Each heading shows the profile name, `AA_minsamples`, `path_samples`, light
`samples`, requested `threads` (`-1` means auto), and elapsed seconds from that
run's `timings.csv`. `Elapsed` includes launch and saving the TGA, matching the
CSV; it is not the renderer-only time. The detail image also labels the 3x zoom.
Headings are added above the original pixels, without covering the scene.
The source TGA files are preserved. The crop is fixed for this example's
480 x 360 camera view; adjust `create_comparison.py` if the framing changes.
Malformed, missing or unsupported TGA files cause a clear error.

To generate or regenerate the PNGs for an existing run without rendering:

```powershell
# From the repository root; replace the directory with your own run
.\examples\render_scene\compare_quality.ps1 -ExistingRunDirectory '.\examples\render_scene\build\quality-comparison\20261103-195010-004'
```

This mode needs Python, Windows System.Drawing, the three TGA files with the
profile filenames shown above, and their `timings.csv`; it does not launch xmake
or use `-Threads`/`-OutputDirectory`. Missing or invalid timing rows cause an
error instead of guessed labels. Both decimal points and decimal commas in the
CSV are supported.
Existing comparison PNGs in that run directory are overwritten.

The script measures elapsed wall time including the xmake launch, rendering,
TGA writing and log capture. The renderer-only time is also available in each
log as `Overall rendertime`. The profiles change several sample counts together
to compare overall presets; use individual CLI options to isolate one setting.
The total script time also includes PNG generation, while per-profile CSV times
continue to measure only their individual render invocations.

Measured on this machine on 3 November 2026 for the textured cube with mirror
and glass spheres, with the installed TDM-GCC-64 Release shared library,
480 x 360 output and `-Threads -1` (8 detected workers).
Run directory: `build/quality-comparison/20261103-202317-707`.

| Profile | Elapsed wall time | Renderer-only time |
| --- | --- | --- |
| preview | 1.92 s | 0.928 s |
| baseline | 27.57 s | 26.588 s |
| quality | 133.98 s | 133.019 s |
| Entire script, including comparison PNGs | 164.79 s (2 min 44.79 s) | — |

Budget approximately 3 minutes for this complete set under similar conditions.
This is a single measured run, not a guarantee; worker count, CPU load, hardware
and build configuration affect time. Increasing several sample counts together
does not produce a simple linear runtime increase. Build/configuration time is
not included because the script uses the already-built example.
The total includes automatic PNG generation and headings; per-profile times
include the corresponding xmake launch, render and TGA saving.

### Visual comparison: preview, baseline and quality

These reference images and timings show the current scene: the checkerboard
cube, mirror sphere and glass sphere, rendered in the run identified above.

Left to right: **preview — baseline — quality**, from the same measured run
above. Each panel shows the original 480 x 360 render, converted losslessly
from TGA to PNG, with the profile name, sampling settings, requested threads
and elapsed time in a heading above it. Times include launch and TGA saving.

![Preview, baseline and quality side by side](images/quality-comparison.png)

The same crop of the dark cube face, spheres and nearby floor is enlarged 3x
below, in the same order, using nearest-neighbor scaling so smoothing does not
hide noise. The fixed crop now also exposes noise in glass and reflections.

![Dark face, spheres and floor: preview, baseline and quality at 3x](images/quality-comparison-detail.png)

| Profile | Elapsed time | Visible quality | When to use it |
| --- | --- | --- | --- |
| preview | 1.92 s | Strong grain on the dark cube face, floor, glass and reflections; composition and material differences are clear | Quick iteration on geometry, camera, textures and lighting |
| baseline | 27.57 s | Much cleaner cube, floor and mirror; visible grain remains in glass and its reflection | Routine evaluation and an effective working preview of the full scene |
| quality | 133.98 s | Cleaner dark face and reflections; glass still has some noise, especially when enlarged | Final output when the extra cleanliness matters, with further sampling potentially needed for glass |

For this scene, **baseline offers the best balance of quality and time**.
The jump from preview to baseline is clearly visible and worthwhile for a
finished image. Quality costs about **4.9 times** as much as baseline. Its
improvement is clearest in the enlarged dark face and reflections; it does not
make the glass completely noise-free. Use preview while adjusting the scene,
baseline to judge the result, and quality selectively for the final render.
This conclusion applies to this scene and resolution; more complex lighting
can need different sampling settings.

These images are documentation assets stored outside the ignored build tree,
so cleaning generated renders does not remove this comparison.

Only installed headers and `lib/liblibyafaray4.dll.a` are used for compilation and
linking. `xmake run` adds the installation's `bin` directory to the child process's
PATH for loading `libyafaray4.dll`; the global environment is unchanged. Direct
executable launches need this DLL directory and the toolchain runtimes in PATH.

## Scene walkthrough

`render_scene.c` follows these steps:

1. Create the logger, scene and parameter maps.
2. Generate a checkerboard image in memory, create its texture and material,
   and create triangle meshes for the cube, floor and emitter, plus mirror and
   glass spheres as analytic primitives.
3. Add an area light linked to the emitter mesh and a constant background.
4. Configure path tracing, the film, anti-aliasing and perspective camera.
5. Attach a TGA output, preprocess the scene and render.
6. Check the file and release the library objects.

Material colors, light position/power and camera coordinates are deliberately
defined in the source for easy experimentation.

## Cube texture

`checker_material()` generates a 64 x 64 RGB image directly in libYafaRay's
memory, using `yafaray_createImage` and `yafaray_setImageColor`. Alternating
8 x 8 pixel cells form a red-and-cream checkerboard (8 cells per image axis).
The image uses `image_optimization = "none"` and linear RGB colors.

An `image` texture references `checker_image` through `image_name`.
`interpolate = "bilinear"` softens pixel transitions without needing OpenCV
or mipmaps. A `texture_mapper` shader node uses `texco = "global"` and
`mapping = "cube"` to project onto the cube's faces using their normals.
It does not require UV coordinates. The material's `diffuse_shader` points
to this mapper, so the texture participates in direct and indirect lighting.
The floor keeps its uniform gray material.

To experiment, edit the two RGB colors or the cell width (`x / 8`, `y / 8`)
in `checker_material()`, then rebuild the example. Global coordinates anchor
the pattern to the scene; moving the object changes which part it samples.
The three quality profiles, CLI options and automatic comparison script also
work with the textured cube.

## Mirror sphere

`mirror_sphere()` creates a `mirror` material with white `color` and
`reflect = 0.95`, then assigns it by name through the sphere's `material`
parameter. This is a sharp ideal mirror with a slightly attenuated reflection,
not a rough glossy surface. It reflects the textured cube, floor and surroundings.

The object uses `type = "sphere"`, `center = (2.5, 0.0, 0.8)` and
`radius = 0.8`. It rests on the floor beside the cube. This native analytic
primitive requires no triangle mesh, UV coordinates or smoothing.

The path tracing integrator explicitly sets `raydepth = 8` for recursive
specular reflections and refractions. This controls a different part of the calculation from
`bounces = 3`, which controls diffuse path depth. All existing sampling options
and the quality comparison script continue to work with this scene.

## Glass sphere

`glass_sphere()` adds a second sphere with `type = "glass"` material and
`IOR = 1.5`, a typical glass-like index of refraction. Unlike the opaque mirror,
it transmits and bends camera rays while also reflecting its surroundings.
Look for distortion of the cube's checkerboard through the sphere and stronger
reflection toward its rim. Both `filter_color` and `mirror_color` are white,
so this example uses clear, untinted glass without absorption or dispersion.

The glass sphere has the same `radius = 0.8` as the mirror sphere, with
`center = (1.6, -2.0, 0.8)`, in front of the cube and beside the mirror. Its
placement makes the checkerboard visible through it. The camera is unchanged.
`raydepth = 8` allows rays to enter and leave the sphere and continue reflecting
or refracting; `bounces` still controls diffuse paths separately.

For a translucent shadow, the material uses `fake_shadows = true`, together
with integrator `transpShad = true` and `shadowDepth = 8`. These approximate
light transmission along shadow rays. Camera-ray refraction is still computed,
but the shadow approximation does not simulate focused caustics on the floor.
The example does not add photon-based caustics or dispersion for this comparison.

## Soft shadows and indirect lighting

All settings below are passed through `yafaray_setParamMap*` before creating the
light, integrator or film. They are implemented by the installed shared library;
no optional dependencies or library rebuild are needed.
The sampling and worker values in the tables are defaults; the command-line
options above override them at runtime.

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
The current textured scene with both spheres was verified in all three profiles;
the measured times are listed above. The shadow has a visible penumbra, the dark
cube face receives reflected light, and the spheres show reflection and
refraction. Timing depends on hardware; glass retains some Monte Carlo noise.
