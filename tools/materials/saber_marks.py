#!/usr/bin/env python3
"""Makes the two pictures of the saber marks of VisionJK (cgame cg_players.c CG_CreateSaberMarks):

  gfx/vision/saberburn.tga   the burn a blade leaves: across the picture (t) a band with frayed edges and a dark core,
                             along it (s) charred noise that repeats, so the cgame lays it along a whole swing.
                             Premultiplied for blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA: the wall times
                             rgb + 1 - alpha, so outside the band (0, 0) it is the wall and inside the colour.
  gfx/vision/saberglow.tga   the hot core of a fresh cut, added (GL_ONE GL_ONE): narrower, white, its colour comes
                             from the vertices as it cools.

usage: tools/materials/saber_marks.py   (writes into assets/japlus_client/gfx/vision)
"""
import math
import os
import random

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'assets', 'japlus_client', 'gfx', 'vision')


def periodic_noise(width, octaves, seed):
    """Value noise along s that repeats over the width: a list of width values about 0..1."""
    rnd = random.Random(seed)
    out = [0.0] * width
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = 4 << o
        knots = [rnd.random() for _ in range(cells)]
        for x in range(width):
            f = x * cells / width
            i = int(f)
            u = f - i
            u = u * u * (3 - 2 * u)
            out[x] += amp * (knots[i % cells] * (1 - u) + knots[(i + 1) % cells] * u)
        total += amp
        amp *= 0.5
    return [v / total for v in out]


def noise2(width, height, seed):
    """Value noise over the picture that repeats along s (and t, which does not matter): rows of floats about 0..1."""
    rnd = random.Random(seed)
    grid = []
    out = [[0.0] * width for _ in range(height)]
    amp, total = 1.0, 0.0
    for o in range(4):
        cx, cy = 8 << o, 2 << o
        knots = [[rnd.random() for _ in range(cx)] for _ in range(cy)]
        for y in range(height):
            fy = y * cy / height
            j = int(fy)
            v = fy - j
            v = v * v * (3 - 2 * v)
            for x in range(width):
                fx = x * cx / width
                i = int(fx)
                u = fx - i
                u = u * u * (3 - 2 * u)
                a = knots[j % cy][i % cx] * (1 - u) + knots[j % cy][(i + 1) % cx] * u
                b = knots[(j + 1) % cy][i % cx] * (1 - u) + knots[(j + 1) % cy][(i + 1) % cx] * u
                out[y][x] += amp * (a * (1 - v) + b * v)
        total += amp
        amp *= 0.5
    return [[v / total for v in row] for row in out]


def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def burn(width=256, height=64):
    edge = periodic_noise(width, 4, 11)
    char = noise2(width, height, 12)
    img = Image.new('RGBA', (width, height))
    px = img.load()
    for x in range(width):
        half = 0.27 + 0.10 * (edge[x] - 0.5)		# half the width of the band, in t
        for y in range(height):
            d = abs((y + 0.5) / height - 0.5)
            a = 1.0 - smoothstep(half - 0.07, half + 0.05, d)
            core = 1.0 - smoothstep(0.0, half * 0.75, d)
            g = 0.62 - 0.50 * core
            g *= 0.80 + 0.40 * char[y][x]
            g = min(max(g, 0.04), 1.0)
            # (a little brown in the char)
            rgb = (g * a, g * 0.88 * a, g * 0.76 * a)
            px[x, y] = tuple(int(round(c * 255)) for c in rgb) + (int(round(a * 255)),)
    return img


def glow(width=256, height=32):
    flicker = periodic_noise(width, 4, 21)
    img = Image.new('RGBA', (width, height))
    px = img.load()
    for x in range(width):
        w = 0.11 + 0.04 * (flicker[x] - 0.5)
        for y in range(height):
            d = (y + 0.5) / height - 0.5
            i = math.exp(-(d / w) ** 2) * (0.8 + 0.2 * flicker[x])
            v = int(round(min(i, 1.0) * 255))
            px[x, y] = (v, v, v, 255)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    burn().save(os.path.join(OUT, 'saberburn.tga'))
    glow().save(os.path.join(OUT, 'saberglow.tga'))
    print('wrote saberburn.tga and saberglow.tga into', OUT)


if __name__ == '__main__':
    main()
