"""Convert the three TGA filters to faithful full-view and 2x horizon PNG strips."""
import argparse
from pathlib import Path
import struct
import zlib

FILTERS = ("bilinear", "mipmap_trilinear", "mipmap_ewa")


def read_tga(path):
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError(f"Truncated TGA: {path}")
    w, h, bits, desc = struct.unpack_from("<HHBB", data, 12)
    if (w, h) != (800, 500) or data[1] or data[2] != 2 or bits not in (24, 32) or desc & 0xD0:
        raise ValueError(f"Unsupported TGA format: {path}")
    channels = bits // 8
    pixels = data[18 + data[0]:18 + data[0] + w*h*channels]
    if len(pixels) != w*h*channels:
        raise ValueError(f"Truncated pixels: {path}")
    rows = []
    for y in range(h):
        sy = y if desc & 32 else h - y - 1
        row = bytearray()
        for x in range(w):
            i = (sy*w+x)*channels
            row.extend((pixels[i+2], pixels[i+1], pixels[i]))
        rows.append(bytes(row))
    return rows


def png(path, rows):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind+payload))
    header = struct.pack(">IIBBBBB", len(rows[0])//3, len(rows), 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(b"".join(b"\0"+r for r in rows))) + chunk(b"IEND", b""))


def compare(folder):
    images = [read_tga(folder / (name+".tga")) for name in FILTERS]
    for name, rows in zip(FILTERS, images):
        png(folder / (name+".png"), rows)
    png(folder / "comparison.png", [b"".join(im[y] for im in images) for y in range(500)])
    crops = []
    for image in images:
        rows = []
        for y in range(190, 310):
            row = b"".join(image[y][3*x:3*x+3]*2 for x in range(200, 600))
            rows.extend([row]*2)
        crops.append(rows)
    png(folder / "comparison-horizon.png", [b"".join(im[y] for im in crops) for y in range(240)])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        compare(args.directory)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")
