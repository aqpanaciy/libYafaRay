"""Create full views and identical 4x crops from the depth comparison TGAs."""
import argparse
from pathlib import Path
from create_preview import read_tga, png

DEPTHS = (8, 16, 32, 64)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    images = [read_tga(args.directory / f'depth-{d}.tga') for d in DEPTHS]
    # Scene-specific measurement of the terminal dark region, not all shadows.
    for depth, image in zip(DEPTHS, images):
        seen, todo = set(), [(510, 150)]
        while todo:
            x, y = todo.pop()
            if ((x, y) in seen or not (440 <= x < 560 and 105 <= y < 205)
                    or max(image[y][3*x:3*x+3]) >= 5):
                continue
            seen.add((x, y))
            todo.extend(((x-1, y), (x+1, y), (x, y-1), (x, y+1)))
        bounds = ((min(x for x, y in seen), min(y for x, y in seen),
                   max(x for x, y in seen), max(y for x, y in seen))
                  if seen else None)
        print(f'depth={depth}: terminal dark pixels={len(seen)}, bounds={bounds}')
    print(f'Depth 32 and 64 RGB-identical: {images[2] == images[3]}')
    png(args.directory / 'comparison.png',
        [b''.join(image[y] for image in images) for y in range(600)])
    crops = []
    for image in images:
        crop = []
        for y in range(105, 205):
            row = b''.join(image[y][3*x:3*x+3]*4 for x in range(440, 560))
            crop.extend([row]*4)
        crops.append(crop)
    png(args.directory / 'comparison-end.png',
        [b''.join(crop[y] for crop in crops) for y in range(400)])


if __name__ == '__main__':
    main()
