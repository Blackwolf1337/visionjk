#!/usr/bin/env python3
"""Makes the flipbooks of the effects of VisionJK: sheets of tiles a particle goes through over its life (keyword
frames <cols> <rows> [fps] of an .efx, RF_FLIPBOOK of rd-vulkan-vision). Tiles left to right, top to bottom.

  fb/smoke.png     8 x 8 tiles of 128: a puff of smoke that rolls out, grows and thins away. Blended by its alpha
                   (GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA), grey lit from above; the renderer lights it by the level
                   (v_litParticles), the vertices tint and fade it.
  fb/fireball.png  8 x 8 tiles of 128: a ball of fire that bursts, churns and burns down to embers and nothing.
                   Added (GL_ONE GL_ONE): white at its hottest, yellow, orange, deep red, black where nothing burns.
  fb/dust.png      4 x 4 tiles of 128: a cloud of dust kicked up, as smoke but tan and quicker to settle.

The turbulence is octaves of value noise made by blowing up random pictures (PIL alone, no numpy); a tile is a crop
of them that zooms in as the puff grows and turns a little, so the shapes churn outward from one tile to the next
instead of jumping.

usage: tools/materials/flipbooks.py [preview dir]   (writes into assets/japlus_client/gfx/vision/fb; Python and PIL)
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'assets', 'japlus_client', 'gfx', 'vision', 'fb')
TILE = 128
CANVAS = 512        # the noise the tiles are cut from


def octaves(seed, count=5, base=6):
    """Random pictures of growing detail, each blown up to the canvas: the octaves of the turbulence."""
    rnd = random.Random(seed)
    layers = []
    for k in range(count):
        size = base * (2 ** k)
        small = Image.new('L', (size, size))
        small.putdata([rnd.randint(0, 255) for _ in range(size * size)])
        layers.append(small.resize((CANVAS, CANVAS), Image.BICUBIC))
    return layers


def turbulence(layers, zoom, turn, shift):
    """The octaves cut out round the middle of the canvas, zoom times as large (1: all of it, smaller: closer), turned by
    turn degrees and moved by shift (each octave a little more), summed with halving weights, 0 to 1."""
    out = None
    total = sum(0.5 ** k for k in range(len(layers)))
    weight = 1.0 / total
    for k, layer in enumerate(layers):
        size = CANVAS * zoom
        cx = CANVAS / 2 + shift[0] * (k + 1)
        cy = CANVAS / 2 + shift[1] * (k + 1)
        box = (cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2)
        part = layer.rotate(turn * (1 + 0.3 * k), resample=Image.BICUBIC).crop(tuple(int(v) for v in box))
        part = part.resize((TILE, TILE), Image.BICUBIC)
        part = part.point(lambda v, w=weight: v * w)
        out = part if out is None else ImageChops.add(out, part, scale=1.0)
        weight *= 0.5
    # (the octaves average about the middle grey: stretched so the turbulence spans most of 0 to 255)
    return out.point(lambda v: min(255, max(0, int((v - 128) * 2.2 + 128))))


def _unused_radial(radius, soft):
    """A round mask: 1 inside radius (0 to 1 of half the tile), falling to 0 over soft."""
    img = Image.new('L', (TILE, TILE))
    px = []
    for y in range(TILE):
        for x in range(TILE):
            d = math.hypot((x + 0.5) / TILE * 2 - 1, (y + 0.5) / TILE * 2 - 1)
            t = min(max((radius - d) / max(soft, 1e-3), 0.0), 1.0)
            px.append(int(255 * t * t * (3 - 2 * t)))
    img.putdata(px)
    return img


DIST = [math.hypot((x + 0.5) / TILE * 2 - 1, (y + 0.5) / TILE * 2 - 1) for y in range(TILE) for x in range(TILE)]


def puff(layers, age, grow, thin, wobble):
    """The density of a puff at age (0 to 1) and how its surface faces the light: a round body that grows, its edge
    pushed in and out by the turbulence (billows), its inside lumpy, thinning with age. Radii are of half the tile.
    Returns density and light, 0 to 1 each, as lists."""
    zoom = 0.5 - 0.22 * age                     # closer as it grows: the shapes swell outward
    turb = [v / 255.0 for v in turbulence(layers, zoom, 30 * age, (wobble * age * 6, -age * 12)).getdata()]
    radius = 0.5 + grow * age
    dens = []
    for d, t in zip(DIST, turb):
        # billows: the bright lumps of the turbulence, as cauliflower
        billow = 1.0 - abs(2.0 * t - 1.0)
        edge = radius + (t - 0.5) * 0.5
        inside = min(max((edge - d) / 0.18, 0.0), 1.0)
        inside = inside * inside * (3 - 2 * inside)
        # thinner with age: the thin parts open into holes first
        body = min(max(billow * 1.4 - thin * age * 1.2, 0.0), 1.0)
        dens.append(inside * (0.25 + 0.75 * body))
    # light from above and a little from the left: the density a few pixels up-left against here
    light = []
    for i, v in enumerate(dens):
        x, y = i % TILE, i // TILE
        j = max(0, y - 3) * TILE + max(0, x - 2)
        light.append(min(max(0.5 + (v - dens[j]) * 1.6 + (0.5 - y / TILE) * 0.3, 0.0), 1.0))
    return dens, light


def to_image(values, scale=255):
    img = Image.new('L', (TILE, TILE))
    img.putdata([min(255, max(0, int(v * scale))) for v in values])
    return img


def sheet(cols, rows, tile_fn):
    img = Image.new('RGBA', (cols * TILE, rows * TILE))
    n = cols * rows
    for i in range(n):
        tile = tile_fn(i / (n - 1))
        img.paste(tile, ((i % cols) * TILE, (i // cols) * TILE))
    return img


def smoke_tile(layers):
    def make(age):
        dens, light = puff(layers, age, grow=0.35, thin=0.45, wobble=1.0)
        fade = (1.0 - age) ** 0.8
        alpha = to_image([v * fade * 0.95 for v in dens]).filter(ImageFilter.GaussianBlur(0.6))
        grey = to_image([0.36 + 0.5 * l for l in light]).filter(ImageFilter.GaussianBlur(0.6))
        return Image.merge('RGBA', (grey, grey, grey, alpha))
    return make


def dust_tile(layers):
    def make(age):
        dens, light = puff(layers, age, grow=0.42, thin=0.55, wobble=-1.5)
        fade = (1.0 - age) ** 0.9
        alpha = to_image([v * fade * 0.85 for v in dens]).filter(ImageFilter.GaussianBlur(0.6))
        lum = [0.45 + 0.45 * l for l in light]
        r = to_image(lum).filter(ImageFilter.GaussianBlur(0.6))
        g = to_image([v * 0.86 for v in lum]).filter(ImageFilter.GaussianBlur(0.6))
        b = to_image([v * 0.66 for v in lum]).filter(ImageFilter.GaussianBlur(0.6))
        return Image.merge('RGBA', (r, g, b, alpha))
    return make


def fire_ramp(t):
    """Black body like: 0 nothing, deep red, orange, yellow, white at 1 (added, so black is nothing)."""
    keys = [(0.0, (0, 0, 0)), (0.18, (60, 6, 0)), (0.38, (190, 45, 5)), (0.6, (255, 130, 25)),
            (0.8, (255, 215, 110)), (1.0, (255, 250, 230))]
    for (t0, c0), (t1, c1) in zip(keys, keys[1:]):
        if t <= t1:
            f = (t - t0) / (t1 - t0)
            return tuple(int(a + (b - a) * f) for a, b in zip(c0, c1))
    return keys[-1][1]


RAMP = [fire_ramp(i / 255) for i in range(256)]


def fire_tile(layers):
    def make(age):
        dens, light = puff(layers, age * 0.8, grow=0.3, thin=0.35, wobble=0.6)
        heat = []
        for d, v, l in zip(DIST, dens, light):
            # hottest at once and in the middle, the lumps hotter than the gaps, burning down to embers and nothing
            core = max(0.0, 1.0 - d / (0.55 + 0.3 * age))
            h = v * (0.55 + 0.6 * core + 0.35 * (l - 0.5)) * (1.15 - 1.05 * age)
            heat.append(min(max(h, 0.0), 1.0))
        heat_img = to_image(heat).filter(ImageFilter.GaussianBlur(0.6))
        rgb = [heat_img.point(lambda v, c=c: RAMP[v][c]) for c in range(3)]
        return Image.merge('RGBA', (rgb[0], rgb[1], rgb[2], heat_img))
    return make


def main():
    os.makedirs(OUT, exist_ok=True)
    preview = sys.argv[1] if len(sys.argv) > 1 else None
    made = {
        'smoke': sheet(8, 8, smoke_tile(octaves(11, base=4))),
        'fireball': sheet(8, 8, fire_tile(octaves(23, base=4))),
        'dust': sheet(4, 4, dust_tile(octaves(37, base=5))),
    }
    for name, img in made.items():
        img.save(os.path.join(OUT, name + '.png'), optimize=True)
        if preview:
            os.makedirs(preview, exist_ok=True)
            # on a mid grey, as it would look over a wall
            back = Image.new('RGBA', img.size, (90, 90, 96, 255))
            if name == 'fireball':
                shown = ImageChops.add(back.convert('RGB'), img.convert('RGB'))
            else:
                shown = Image.alpha_composite(back, img).convert('RGB')
            shown.save(os.path.join(preview, name + '.png'))
        print('wrote', name, img.size)


if __name__ == '__main__':
    main()
