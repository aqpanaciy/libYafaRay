"""Convert the example's uncompressed 800x600 TGA output to RGB PNG."""
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
    if (w, h) != (800, 600) or data[1] or data[2] != 2 or bits not in (24, 32) or desc & 0xD0:
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    png(args.output, read_tga(args.input))
