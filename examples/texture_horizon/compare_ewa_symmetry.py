"""Measure mirrored horizon contrast and create an unfiltered before/after crop.

Contrast is the mean row-wise population standard deviation of the red channel.
This scene-specific diagnostic avoids requiring identical checkerboard phases.
"""
import argparse
import statistics
from pathlib import Path

from create_filter_comparison import read_tga, png


def contrast(rows, x0, x1, y0, y1):
    return statistics.mean(statistics.pstdev(row[3*x0:3*x1:3])
                           for row in rows[y0:y1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    images = [read_tga(path) for path in (args.before, args.after)]
    print('image,y-range,left contrast,right contrast,relative imbalance')
    for name, rows in zip(('before', 'after'), images):
        for y0, y1 in ((190, 210), (210, 230), (270, 290), (290, 310)):
            left = contrast(rows, 200, 400, y0, y1)
            right = contrast(rows, 400, 600, y0, y1)
            imbalance = abs(left-right) / ((left+right)*0.5)
            print(f'{name},{y0}:{y1},{left:.6f},{right:.6f},{imbalance:.2%}')
    # Same central 400x120 region as the filter comparison, enlarged 2x.
    crops = []
    for rows in images:
        crop = []
        for row in rows[190:310]:
            enlarged = b''.join(row[3*x:3*x+3]*2 for x in range(200, 600))
            crop.extend((enlarged, enlarged))
        crops.append(crop)
    png(args.output, [a+b for a, b in zip(*crops)])


if __name__ == '__main__':
    main()
