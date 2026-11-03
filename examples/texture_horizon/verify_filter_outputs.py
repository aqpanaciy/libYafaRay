"""Regression check for spurious EWA colors in the self-emitting horizon scene."""
import argparse
from pathlib import Path
from create_filter_comparison import FILTERS, read_tga


def invalid_pixels(path):
    count = 0
    for y, row in enumerate(read_tga(path)):
        # Exclude the floor/ceiling boundary: AA may mix their different hues.
        if 246 <= y < 254:
            continue
        for x in range(800):
            r, g, b = row[3*x:3*x+3]
            # Interpolation of either two-color pattern must preserve its hue
            # ordering. 0.8 linear is < 233 in the sRGB TGA output.
            wrong_hue = (r > g + 4 or g > b + 4) if y < 250 else (r + 4 < g or g + 4 < b)
            if wrong_hue or max(r, g, b) > 233:
                count += 1
    return count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    failed = False
    for name in FILTERS:
        count = invalid_pixels(args.directory / (name + ".tga"))
        print(f"{name}: {count} invalid pixels")
        failed |= count != 0
    raise SystemExit(1 if failed else 0)
