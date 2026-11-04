"""Create full views and identical 4x crops from the depth comparison TGAs."""
import argparse
from pathlib import Path
from create_preview import read_tga, png

def terminal_region(image):
    """Near-black component at the vanishing point, excluding nearby shadows."""
    remaining = {(x, y) for y in range(105, 205) for x in range(440, 560)
                 if max(image[y][3*x:3*x+3]) < 5}
    largest = set()
    while remaining:
        todo, component = [remaining.pop()], set()
        while todo:
            x, y = todo.pop()
            component.add((x, y))
            for neighbor in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    todo.append(neighbor)
        touches_edge = any(x in (440, 559) or y in (105, 204) for x, y in component)
        near_end = any(500 <= x < 525 and 145 <= y < 170 for x, y in component)
        if not touches_edge and near_end and len(component) > len(largest):
            largest = component
    bounds = ((min(x for x, y in largest), min(y for x, y in largest),
               max(x for x, y in largest), max(y for x, y in largest))
              if largest else None)
    return len(largest), bounds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--depths', nargs='+', type=int, default=(16, 32, 64))
    args = parser.parse_args()
    images = [read_tga(args.directory / f'depth-{d}.tga') for d in args.depths]
    # Scene-specific measurement of the terminal dark region, not all shadows.
    for depth, image in zip(args.depths, images):
        count, bounds = terminal_region(image)
        print(f'depth={depth}: terminal dark pixels={count}, bounds={bounds}')
    for a, b, left, right in zip(args.depths, args.depths[1:], images, images[1:]):
        print(f'Depth {a} and {b} RGB-identical: {left == right}')
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
