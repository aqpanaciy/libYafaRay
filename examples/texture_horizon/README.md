# Periodic textures approaching the horizon

An independent C90/xmake example using the installed MinGW x64 shared libYafaRay
library. A warm checkerboard floor at `z = 0` and a cool checkerboard ceiling at
`z = 3` extend to `y = 10000`. The horizontal camera sits halfway between them,
at `(0, 0, 1.5)`, looking along positive Y. Output is 800 x 500 TGA.

The floor repeats every **2 world units**; the ceiling repeats every **3.4 units**.
One repeat contains two cells per axis, so their cell widths are 1 and 1.7 units.
Both patterns are 64 x 64 RGB images generated in memory. UVs are computed as
`(x / period, y / period)` and the image texture repeats over the planes.
The opposite triangle winding makes the floor face up and the ceiling face down.

Both materials use `shinydiffusemat` with `emit = 1` and a texture connected to
`diffuse_shader`. Their brightness is uniform and independent of distance:
lighting, shadows and indirect-light noise cannot hide texture-sampling artifacts.
There are intentionally no light objects. The corresponding "Lights not found"
warning is expected; the materials are self-emitting. Geometry is finite, not
an actual infinite plane, but it extends far enough for this view.

## Build and run

First build/install the shared library using the
[TDM-GCC-64 instructions](../hello/README.md#windows-tdm-gcc-64).
Then, from the repository root in PowerShell:

```powershell
Set-Location examples/texture_horizon
xmake f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64
xmake
xmake run yafaray_texture_horizon
```

The default output is `examples/texture_horizon/build/horizon.tga`. Existing
output files are overwritten; relative output paths are relative to that build
directory. A different installation can be selected with
`--yafaray_prefix=C:/path/to/install` on the **xmake configuration** command.
Only installed public headers and the import library are used; `xmake run`
adds the installed DLL directory to the child process's PATH.

## Compare interpolation and camera sampling

Options follow the target name, accept separate values, and need no rebuild:

| Option | Default | Meaning |
| --- | --- | --- |
| `--filter none` | — | Nearest texel; no texture interpolation |
| `--filter bilinear` | `bilinear` | Blend four neighboring texels |
| `--filter bicubic` | — | Cubic interpolation of neighboring texels |
| `--filter mipmap_trilinear` | — | Trilinear mipmap filtering; requires OpenCV-enabled DLL |
| `--filter mipmap_ewa` | — | Elliptical weighted average mipmap filtering; requires OpenCV-enabled DLL |
| `--aa-samples N` | `16` | Positive integer; film `AA_minsamples`, one AA pass |
| `--threads N` | `-1` | Positive worker count, or `-1` for auto |
| `--help` | — | Usage without rendering |

To isolate texture interpolation, keep camera sampling fixed:

```powershell
xmake run yafaray_texture_horizon none-aa16.tga --filter none --aa-samples 16
xmake run yafaray_texture_horizon bilinear-aa16.tga --filter bilinear --aa-samples 16
xmake run yafaray_texture_horizon bicubic-aa16.tga --filter bicubic --aa-samples 16
```

To isolate camera anti-aliasing, keep interpolation fixed:

```powershell
xmake run yafaray_texture_horizon bilinear-aa1.tga --filter bilinear --aa-samples 1
xmake run yafaray_texture_horizon bilinear-aa16.tga --filter bilinear --aa-samples 16
xmake run yafaray_texture_horizon bilinear-aa64.tga --filter bilinear --aa-samples 64
```

Open the TGAs at **100% display scale** first. Viewer resizing can add its own
filtering and change or conceal the moire. Compare the thin band around the
middle of the image, then the nearer rows of cells. The renderer logs
`Overall rendertime` for timing comparisons; keep thread count fixed to compare
quality costs. Periods, plane heights, colors and camera geometry can be changed
in `texture_horizon.c` and require rebuilding the example.

## What the experiment demonstrates

Perspective compresses distant repeats into less than a pixel. Eventually one
pixel covers many cells, and their correct spatial average should approach a
uniform value. The shorter-period floor reaches this condition sooner than the
ceiling at the same distance. A pixel footprint is also very elongated along the
plane near a grazing angle.

Nearest/bilinear/bicubic all sample the original-resolution image. Bilinear and
bicubic soften texel boundaries, but do not average every distant repetition
inside that elongated footprint. Thus they can still produce stripes, moire and
unstable detail near the horizon. Increasing `AA_minsamples` averages more camera
rays per output pixel, generally reducing these artifacts at a time cost; finite
sampling does not guarantee their complete removal. This example does not apply
post-render blur or denoising.

### Mipmaps and the installed library

This version of libYafaRay also defines `mipmap_trilinear` and `mipmap_ewa`
texture interpolation. Mipmaps prefilter progressively smaller images; EWA uses
an anisotropic footprint, particularly relevant at grazing angles. Proper use
also requires texture-coordinate ray differentials (this example supplies UVs).

The original `build/install/tdm-gcc64-shared` installation has OpenCV disabled.
Use the separate `tdm-gcc64-shared-opencv` installation below for mipmap filters.
`image_manipulation::generateMipMaps` in `src/image/image_manipulation.cc`
requires OpenCV and otherwise returns an empty mipmap list with a warning.
The example detects that warning during texture creation and stops before
rendering, instead of attempting to sample an unavailable mipmap chain.

The present example keeps the library's existing CMake build unchanged.

### Build OpenCV and a separate shared libYafaRay

From the repository root in PowerShell:

```powershell
.\examples\texture_horizon\build_opencv.ps1
$cmake = 'C:/Program Files/CMake/bin/cmake.exe'
& $cmake --preset tdm-gcc64-shared-opencv
& $cmake --build --preset tdm-gcc64-shared-opencv --parallel 4
& $cmake --install build/tdm-gcc64-shared-opencv
```

The script downloads the pinned OpenCV 4.10.0 source archive, verifies SHA256,
and builds static `core`, `imgproc` and `photo` modules using the same
`C:/temp/TDM-GCC-64` compiler. It installs to
`build/install/opencv-4.10.0-tdm-gcc64`. These are linked into the separate
shared libYafaRay at `build/install/tdm-gcc64-shared-opencv`; the original
installation is retained. The first source build takes much longer than a render.
OpenCV tests, examples, Python bindings, IPP and OpenCL are disabled.

Select this DLL installation for the example:

```powershell
Set-Location examples/texture_horizon
xmake f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64 --yafaray_prefix=../../build/install/tdm-gcc64-shared-opencv
xmake
.\compare_filters.ps1 -AaSamples 16 -Threads -1
```

The comparison runs `bilinear`, `mipmap_trilinear` and `mipmap_ewa` sequentially
at the same resolution, geometry, UVs, AA count and worker count. It saves TGA,
individual PNGs, verbose logs, `timings.csv`, a labeled side-by-side
`comparison.png`, and `comparison-horizon.png` (the middle 400 x 120 region
enlarged 2x without smoothing) in a timestamped `build/filter-comparison` folder.
PNG generation needs Python 3 and Windows .NET System.Drawing; no Python image
packages are required. Elapsed times in headings include launch and TGA saving.
Logs should confirm mipmap generation, rather than report the missing-OpenCV warning.

The scene automatically enables ray differentials when it contains mipmapped
textures; UV coordinates let the mapper compute their pixel footprints.
Trilinear chooses/blends isotropic mip levels, often blurring grazing-angle
detail. EWA adapts to an elongated footprint, aiming to preserve more detail
while suppressing aliasing. Neither changes the geometry or either texture period.

### Measured comparison with OpenCV

All three modes were run with the **same OpenCV-enabled DLL**, 800 x 500 output,
16 camera samples per pixel and 8 automatically detected render workers on
3 November 2026. OpenCV 4.10.0 was built from source using TDM-GCC-64 10.3.0;
libYafaRay identifies itself as `4.0.0-pre-alpha+git.g5448d81b.dirty`.
The comparison run after the EWA bounds fixes below is
`build/filter-comparison/20261103-211354-226`.
Both mipmap modes logged 6 generated levels per texture and enabled ray
differentials. No denoising, extra blur or color correction was applied.

| Filter | Renderer-only time | Launch + render + TGA save | Observed result |
| --- | --- | --- | --- |
| `bilinear` | 2.661 s | 4.599 s | Sharp nearby cells, but grain/moire in the horizon band |
| `mipmap_trilinear` | 3.241 s | 4.329 s | Horizon converges to a stable average; visible isotropic blur starts earlier |
| `mipmap_ewa` | 7.780 s | 8.798 s | No colored bands; more distant detail survives before the pattern converges to its average |

Left to right: **bilinear — mipmap_trilinear — mipmap_ewa**.
Headings display elapsed time including launch and saving, rounded to two decimals.

![Three texture filters at the same AA settings](images/filter-comparison.png)

The same horizon region at 2x enlargement without smoothing:

![Horizon detail: bilinear, Trilinear and EWA](images/filter-comparison-horizon.png)

**EWA now gives the best detail-preserving smoothing in this comparison.**
It removes the colored bands after the bounds fixes, while retaining more
visible distant cells than Trilinear. It takes about **2.9x Bilinear's**
renderer time (2.4x Trilinear's). Trilinear is the faster, softer option, at about
1.2x Bilinear's renderer time; Bilinear remains the aliasing reference.
Some residual patterning can still occur during the transition at finite AA
and the default EWA anisotropy limit of 8. The fix addresses invalid memory
reads and spurious colors, not a guarantee of perfect filtering at every angle.
Launch/save overhead and system scheduling explain why the wall-time ordering
can differ from renderer-only time in this short, single-run measurement.

#### EWA bug and regression checks

The original comparison (`build/filter-comparison/20261103-205949-016`) exposed
bright colored bands. A color-range/hue regression check counted 16,450 invalid
pixels in that EWA image, versus zero in Bilinear and Trilinear. The cause was
out-of-bounds texel access, not normal moire or a property of EWA filtering.

Runtime instrumentation caught texel `(11, 16)` being read from a 16 x 16 mipmap:
the original Y coordinate was `-16`. `math::mod(-16, 16)` returned **16**, because
the helper added the divisor for every negative input, including exact multiples
whose remainder was already zero. The fix in `include/math/math.h` adds the
divisor only when the **remainder** is negative, keeping wrapped indices in
`[0, size)`.

An additional bounds fix in `ImageTexture::ewaEllipticCalculation` samples
`(0, 0)` directly at the coarsest 1 x 1 mipmap level; the old code incorrectly
used the original image dimensions for that level's coordinate wrap. Correcting
this second issue alone did not eliminate the colored bands; the modulo fix
addresses the observed root cause. Temporary instrumentation was removed.

The standalone C++17 regression test failed before the modulo correction and
passes after it. It checks negative exact multiples, 1 x 1 textures, ordinary
negative coordinates, and index bounds/congruence over positive and negative
coordinates at all power-of-two sizes from 1 through 64. From the repository root:

```powershell
& C:/temp/TDM-GCC-64/bin/g++.exe -std=c++17 -I include tests/regression/math_mod.cc -o build/math_mod_test.exe
& ./build/math_mod_test.exe
```

For a full comparison run, check the rendered pixels as well (substitute your
run directory):

```powershell
python -B examples/texture_horizon/verify_filter_outputs.py examples/texture_horizon/build/filter-comparison/RUN_DIRECTORY
```

This scene-specific check validates texture hue ordering and maximum intensity
outside a narrow floor/ceiling boundary band where AA mixes both palettes. It
does not establish a general image-quality score and assumes this example's
original self-emitting materials and colors.

After rebuilding and installing `tdm-gcc64-shared-opencv`, the complete comparison
was repeated with unchanged scene and sampling settings. The new check reports
**zero invalid pixels for Bilinear, Trilinear and EWA**, and both comparison PNGs
above were replaced with this verified run. No post-render repair was applied.

The comparison images are kept in `images/` so cleaning `build/` does not remove
the documented result. Timings are a single local measurement, not a guarantee.

## Verified output

Built and rendered on 3 November 2026 using TDM-GCC-64 10.3.0, xmake 3.1.2 and
the installed libYafaRay 4.0.0-pre-alpha shared library. Bilinear/16 camera samples
rendered in approximately 2.43 seconds with 8 detected workers. The scene and
texture periods were visually checked; time varies with hardware and settings.
Also verified: `none`/1 sample, `bicubic`/16 samples (3.27 s), and
`bilinear`/64 samples (11.73 s). All four TGA outputs passed header and size
checks; invalid filters and sample counts were rejected.

![Bilinear filtering with 16 camera samples per pixel](images/bilinear-aa16.png)
