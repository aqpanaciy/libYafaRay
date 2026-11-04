# Facing mirrors: a reflection tunnel

A standalone C90/xmake example linked to the installed shared libYafaRay
library. Two parallel rectangular mirrors face each other above a textured
floor, with one coral-colored sphere between them. The camera looks into the
front mirror from inside the gap, slightly off-axis, revealing nested mirror
frames and a receding chain of reflected spheres.

## Build and run

From this directory in PowerShell, using the existing TDM-GCC-64 installation:

```powershell
& C:/temp/xmake/xmake.exe f -p mingw -a x86_64 -m release --sdk=C:/temp/TDM-GCC-64 --yafaray_prefix=../../build/install/tdm-gcc64-shared-opencv
& C:/temp/xmake/xmake.exe
& C:/temp/xmake/xmake.exe run yafaray_mirror_tunnel
```

The default output is `build/mirror_tunnel.tga`, 800 x 600. A relative output
filename is resolved inside `build`, the target's run directory. Override
`--yafaray_prefix` to use another compatible MinGW shared-library installation.
This scene uses bilinear image filtering and does not require OpenCV; the
default prefix selects the installation already used by the other examples.
The library itself continues to use CMake.

```powershell
# Quick camera/geometry preview
xmake run yafaray_mirror_tunnel preview.tga --aa-samples 4 --path-samples 2 --light-samples 4

# Cleaner image
xmake run yafaray_mirror_tunnel mirror_tunnel.tga --aa-samples 32 --path-samples 8 --light-samples 8

# Shorter reflection chain, for comparison
xmake run yafaray_mirror_tunnel depth8.tga --ray-depth 8

# Extend the chain and select render workers without rebuilding
xmake run yafaray_mirror_tunnel depth64.tga --ray-depth 64 --threads 4

# Optional lossless PNG conversion using Python's standard library
python -B create_preview.py build/mirror_tunnel.tga build/mirror_tunnel.png
```

| Option | Default | Effect |
| --- | --- | --- |
| `--aa-samples N` | 16 | Film `AA_minsamples`; camera/edge sampling |
| `--path-samples N` | 4 | Diffuse indirect-light samples |
| `--light-samples N` | 4 | Area-light samples; shadow noise |
| `--ray-depth N` | 32 | Integrator `raydepth`; specular reflection recursion, 1..128 |
| `--threads N` | -1 | Rendering workers; -1 selects automatically |
| `--help` | | Show usage without rendering |

Options accept `--name N` and `--name=N`. Counts must be positive integers;
invalid values are rejected before rendering.

## Scene and reflection settings

The mirror planes are at `y=-3` and `y=3`, each 6 units wide, from `z=0.08`
to `z=4.4`. Their triangle normals point inward, along +Y and -Y respectively.
Slightly larger gold-colored rectangles behind the mirrors form visible
borders, making the successive reflections easier to follow.

The sphere has radius 0.72 at `(0.1, 0, 0.72)`. The camera is at
`(1.15, -2.45, 1.85)`, looking toward `(0.15, 3, 0.55)`, with perspective
`focal=0.75`. Its lateral offset exposes repeated frames instead of stacking
every reflected sphere directly over the next one. No camera mesh is created,
so the camera does not appear in the mirrors.

The floor uses a generated 64 x 64 blue/ivory checker image, with 8 cells per
axis and a world-space `texture_mapper` using `mapping=cube`, `texco=global`.
There are no external texture assets. A downward-facing 4 x 3 area light at
`z=5`, power 8, lights the scene. `pathtracing` with 3 diffuse bounces provides
fill on the sphere and soft shadows beneath it.

Both mirrors use material `type=mirror`, white `color`, and `reflect=0.98`.
Every reflection retains approximately 98% of the incoming contribution, so
successive images gradually darken. Raising `raydepth` extends the specular
chain; increasing diffuse `bounces` or `path_samples` is not a substitute.
The apparent infinity is a finite render: recursion, finite mirror edges,
and image resolution limit the visible tunnel. A dark termination may remain
at the vanishing point when the recursion limit is reached.

## Rendered result

Built and rendered on 4 November 2026 with TDM-GCC-64 10.3.0 and the installed
OpenCV-enabled shared library. The final view at the default settings
(AA=16, path samples=4, light samples=4, ray depth=32, 8 detected workers)
took **22.120 seconds** of renderer time. Timing varies with the machine.
The application verified the TGA header, dimensions and pixel data size;
the resulting PNG was visually checked. `--help` and rejection of AA=0 and
ray depth=129 were also checked.

![Textured floor, colored sphere and a tunnel of mirror reflections](images/mirror-tunnel.png)

The PNG is a direct conversion of the rendered TGA, without denoising or
post-render retouching. Build products and TGA outputs stay under ignored
`build` directories; this illustration is kept in `images`.
