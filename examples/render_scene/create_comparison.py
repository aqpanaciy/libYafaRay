"""Create faithful RGB comparison PNGs from libYafaRay's uncompressed TGA output.

Uses only the Python standard library; no Pillow or image conversion tools.
"""

import argparse
from pathlib import Path
import struct
import zlib


PROFILES = (
    "preview-aa4-path2-light2",
    "baseline-aa32-path8-light8",
    "quality-aa64-path16-light16",
)


def read_tga(path):
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError(f"Truncated TGA header: {path}")
    width, height, depth, descriptor = struct.unpack_from("<HHBB", data, 12)
    if data[1] != 0 or data[2] != 2 or depth not in (24, 32):
        raise ValueError(f"Expected uncompressed 24/32-bit true-color TGA: {path}")
    if (width, height) != (480, 360) or descriptor & 0xC0:
        raise ValueError(f"Expected non-interleaved 480 x 360 TGA: {path}")
    channels = depth // 8
    offset = 18 + data[0]
    pixels = data[offset:offset + width * height * channels]
    if len(pixels) != width * height * channels:
        raise ValueError(f"Truncated TGA pixels: {path}")
    rows = []
    for y in range(height):
        source_y = y if descriptor & 0x20 else height - 1 - y
        row = bytearray()
        for x in range(width):
            source_x = width - 1 - x if descriptor & 0x10 else x
            index = (source_y * width + source_x) * channels
            # Keep the displayed RGB values; alpha is not needed for this scene.
            row.extend((pixels[index + 2], pixels[index + 1], pixels[index]))
        rows.append(bytes(row))
    return rows


def write_png(path, width, rows):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))

    header = struct.pack(">IIBBBBB", width, len(rows), 8, 2, 0, 0, 0)
    pixels = b"".join(b"\0" + row for row in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def create_comparison(directory):
    # Read and validate every input before writing either output.
    images = [read_tga(directory / (profile + ".tga")) for profile in PROFILES]
    write_png(directory / "comparison.png", 1440,
              [b"".join(image[y] for image in images) for y in range(360)])

    # Identical crop of the dark face and shadow, enlarged without filtering.
    x0, y0, width, height, scale = 245, 155, 160, 120, 3
    crops = []
    for image in images:
        rows = []
        for y in range(y0, y0 + height):
            row = b"".join(image[y][3*x:3*x+3] * scale
                           for x in range(x0, x0 + width))
            rows.extend([row] * scale)
        crops.append(rows)
    write_png(directory / "comparison-detail.png", 1440,
              [b"".join(crop[y] for crop in crops) for y in range(height * scale)])
    print("Comparison order: preview | baseline | quality")
    print(directory / "comparison.png")
    print(directory / "comparison-detail.png")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Run directory containing all three TGA files")
    args = parser.parse_args()
    try:
        create_comparison(args.directory.resolve())
    except (OSError, ValueError) as error:
        parser.exit(1, f"Cannot create comparison: {error}\n")
