#!/usr/bin/env python3
"""Makes the pictures of the saber scars of VisionJK (codemp/cgame/saber/cg_saberscar.c, shaders in vision_fx.shader).

A scar is laid along the cut: s along it (the pictures repeat that way), t across it from edge to edge. Two widths:
the narrow one of the molten core, the wide one of the char, its soot and the heat halo.

  saberscar.tga        the char, wide: a dark glassy groove, a lighter melted lip, uneven soot fading out. For
                       blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA (the surface times rgb + 1 - alpha): each colour
                       of the factor f is rgb = f - min(f), alpha = 1 - min(f), so the edges (f = 1) are 0, 0 and the
                       colour of the vertices (rgb and alpha together) fades it to the surface.
  saberscar_h.tga      its height for the groove of the Vulkan renderer (red, 0.5 the surface): the dent in the middle,
                       the raised lip beside it, a little boiling in the dent.
  sabermolten.tga      the molten core, narrow, added: a white spine in streaky melt drawn out along the cut, coloured
                       by how hot each part is (white, yellow, orange); the vertices scale it by the heat.
  saberembers.tga      white, in alpha the flecks of the core that glow on longest as it cools (embers).
  saberhalo.tga        the heat around a fresh cut, wide, added: soft.
  saberdrip.tga        a drop of melt running down a wall (t down), added, alpha its shape; clampmap, so beyond its
                       end nothing: the cgame lets it run by its texture coordinates.
  saberdrip_scar.tga   what a drop leaves, as saberscar.
  saberpool.tga        the melt pool where a blade rests, round, added.
  saberpool_scar.tga   the crater it leaves, as saberscar; saberpool_h.tga its height.

usage: tools/materials/saber_marks.py   (writes into assets/japlus_client/gfx/vision; plain Python and PIL)
"""
import math
import os
import random

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'assets', 'japlus_client', 'gfx', 'vision')


# ---------------------------------------------------------------- noise

def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3 - 2 * t)


class ValueNoise:
    """Value noise that repeats over periodX cells along x (and over periodY along y)."""

    def __init__(self, seed, periodX, periodY):
        rnd = random.Random(seed)
        self.px, self.py = periodX, periodY
        self.knots = [[rnd.random() for _ in range(periodX)] for _ in range(periodY)]

    def __call__(self, x, y):
        i, j = math.floor(x), math.floor(y)
        u, v = x - i, y - j
        u, v = u * u * (3 - 2 * u), v * v * (3 - 2 * v)
        k = self.knots
        a = k[j % self.py][i % self.px] * (1 - u) + k[j % self.py][(i + 1) % self.px] * u
        b = k[(j + 1) % self.py][i % self.px] * (1 - u) + k[(j + 1) % self.py][(i + 1) % self.px] * u
        return a * (1 - v) + b * v


def fbm(noises, x, y):
    """Sum of octaves (each a ValueNoise of twice the cells), x and y 0..1 of the picture: about 0..1."""
    total, amp, out = 0.0, 1.0, 0.0
    for n in noises:
        out += amp * n(x * n.px, y * n.py)
        total += amp
        amp *= 0.5
    return out / total


def octaves(seed, cellsX, cellsY, count):
    return [ValueNoise(seed + o, cellsX << o, cellsY << o) for o in range(count)]


class Cells:
    """Worley cells that repeat over cx x cy along the picture: (distance to the nearest point, to the second), in
    cells, and a random number of the nearest cell."""

    def __init__(self, seed, cx, cy):
        rnd = random.Random(seed)
        self.cx, self.cy = cx, cy
        self.pts = [[(rnd.random(), rnd.random(), rnd.random()) for _ in range(cx)] for _ in range(cy)]

    def __call__(self, x, y):
        fx, fy = x * self.cx, y * self.cy
        i, j = math.floor(fx), math.floor(fy)
        d1 = d2 = 9.0
        r = 0.0
        for dj in (-1, 0, 1):
            for di in (-1, 0, 1):
                ci, cj = i + di, j + dj
                px, py, pr = self.pts[cj % self.cy][ci % self.cx]
                d = math.hypot(ci + px - fx, cj + py - fy)
                if d < d1:
                    d1, d2, r = d, d1, pr
                elif d < d2:
                    d2 = d
        return d1, d2, r


# ---------------------------------------------------------------- encoding

def factor_pixel(f):
    """The factor f (r, g, b each 0..1) for blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA."""
    m = min(f)
    return tuple(int(round(min(max(c - m, 0.0), 1.0) * 255)) for c in f) + (int(round((1.0 - m) * 255)),)


def byte(v):
    return int(round(min(max(v, 0.0), 1.0) * 255))


# ---------------------------------------------------------------- the scar

SCAR_CORE = 0.17	# half the width of the groove, of the half width of the wide band (d = |t - 0.5| * 2)
SCAR_LIP = 0.27		# where the lip is highest


def scar(width=256, height=128):
    edge = octaves(31, 6, 1, 3)		# how far the soot reaches, along the scar
    blot = octaves(32, 8, 3, 4)		# blotches of soot
    spray = octaves(34, 14, 1, 3)		# where the soot was thrown further out across the cut
    grain = octaves(33, 32, 8, 3)
    wobble = octaves(35, 10, 1, 3)		# the groove a little wider and narrower along the scar
    img = Image.new('RGBA', (width, height))
    hgt = Image.new('RGBA', (width, height))
    px, ph = img.load(), hgt.load()
    for x in range(width):
        u = (x + 0.5) / width
        reach = 0.9 + 0.2 * (fbm(edge, u, 0.0) - 0.5)
        core = SCAR_CORE * (1.0 + 0.45 * (fbm(wobble, u, 0.5) - 0.5))
        streak = fbm(spray, u, 0.0)
        for y in range(height):
            v = (y + 0.5) / height
            d = abs(v - 0.5) * 2.0
            g = fbm(grain, u, v)
            # the soot: blotchy, with streaks thrown out across the cut, gone at its reach
            r = reach * (0.85 + 0.3 * streak)
            s = (1.0 - smoothstep(core, r, d)) ** 1.6 * (0.3 + 0.7 * fbm(blot, u, v))
            near = 1.0 - smoothstep(core, core * 2.4, d)		# brown where it was hottest
            tint = (0.82 - 0.1 * near, 0.9 - 0.04 * near, 1.0)		# (blue darkened most: brown soot)
            fr = [1.0 - 0.86 * s * c for c in tint]
            # a low lip of melted rock, a little lighter than the soot on it
            lip = math.exp(-((d - core * 1.55) / 0.06) ** 2) * (0.6 + 0.4 * g)
            fr = [c + (0.5 + 0.12 * g - c) * lip * 0.45 for c in fr]
            # the groove: black glass with a warm cast, its edge not a line
            # (its edge wanders slowly; a fine noise there read as the teeth of a tread)
            gedge = core * (0.95 + 0.12 * (fbm(blot, u, 0.5) - 0.5))
            groove = 1.0 - smoothstep(gedge * 0.7, gedge * 1.1, d)
            dark = 0.04 + 0.07 * g
            fr = [c * (1.0 - groove) + dark * k * groove for c, k in zip(fr, (1.15, 0.95, 0.8))]
            px[x, y] = factor_pixel(fr)
            # height: a wide soft dent, a low lip, boiling in the dent, flat beyond
            h = 0.5
            h -= 0.42 * (1.0 - smoothstep(0.0, core * 1.35, d)) ** 2
            h += 0.08 * lip
            h += 0.06 * (g - 0.5) * groove
            b = byte(h)
            ph[x, y] = (b, b, b, 255)
    return img, hgt


def heat_ramp(i):
    """A colour for how hot a part is, 0..1: dark red, orange, yellow, white (the vertices scale it by the heat)."""
    return (i, i ** 1.7, i ** 3.2)


def molten(width=256, height=64):
    flow = octaves(42, 10, 1, 4)		# drawn out along the cut: the melt runs that way
    ridge = octaves(43, 18, 2, 3)
    fleckN = octaves(44, 40, 12, 2)		# (round flecks: as many cells across the band as its width takes)
    img = Image.new('RGBA', (width, height))
    emb = Image.new('RGBA', (width, height))
    px, pe = img.load(), emb.load()
    for x in range(width):
        u = (x + 0.5) / width
        for y in range(height):
            v = (y + 0.5) / height
            d = abs(v - 0.5) * 2.0
            w = fbm(flow, u, v)
            # streaks of brighter melt where a ridged noise peaks, bent by the flow
            rn = fbm(ridge, u + 0.03 * (w - 0.5), v + 0.25 * (w - 0.5))
            streak = (1.0 - abs(2.0 * rn - 1.0)) ** 4
            spine = math.exp(-(d / (0.16 + 0.08 * (w - 0.5))) ** 2)
            body = math.exp(-(d / 0.5) ** 2) * (1.0 - smoothstep(0.8, 1.0, d))
            # (below white, so the streaks still show at full heat)
            i = min(1.0, spine * (0.45 + 0.35 * w) + body * (0.25 + 0.25 * w + 0.4 * streak))
            r, g, b = heat_ramp(i)
            # embers: small flecks of the body that glow on longest
            fl = smoothstep(0.62, 0.78, fbm(fleckN, u, v)) * body
            fl = max(fl, streak * body * 0.5)
            px[x, y] = (byte(r), byte(g), byte(b), 255)
            pe[x, y] = (255, 255, 255, byte(fl))
    return img, emb


def halo(width=128, height=64):
    swell = octaves(51, 4, 1, 3)
    img = Image.new('RGBA', (width, height))
    px = img.load()
    for x in range(width):
        u = (x + 0.5) / width
        k = 0.85 + 0.3 * (fbm(swell, u, 0.0) - 0.5)
        for y in range(height):
            d = abs((y + 0.5) / height - 0.5) * 2.0
            i = math.exp(-(d / (0.42 * k)) ** 2) * (1.0 - smoothstep(0.8, 1.0, d))
            px[x, y] = (byte(i), byte(i), byte(i), 255)
    return img


def drip(width=32, height=128):
    """t down: a thin trail from the top, the drop at 0.82..0.95, nothing on the last rows and the sides."""
    img = Image.new('RGBA', (width, height))
    scr = Image.new('RGBA', (width, height))
    px, ps = img.load(), scr.load()
    for y in range(height):
        v = (y + 0.5) / height
        trail = 0.07 + 0.30 * v * v		# thin at the top, swelling into the drop
        for x in range(width):
            d = abs((x + 0.5) / width - 0.5) * 2.0
            shape = (1.0 - smoothstep(trail * 0.55, trail, d)) * smoothstep(0.0, 0.1, v) * (1.0 - smoothstep(0.88, 0.93, v))
            # the drop: a tear, round below, drawn into the trail above
            dy = v - 0.86
            rad = 0.34 if dy > 0 else 0.34 * (1.0 + dy / 0.25)
            head = 1.0 - smoothstep(rad * 0.75, rad, math.hypot(d, max(dy, 0.0) / 0.075 * 0.34)) if rad > 0 else 0.0
            a = max(shape * (0.55 + 0.35 * v), head)
            if v > 0.96 or d > 0.95:
                a = 0.0
            px[x, y] = (255, 255, 255, byte(a))
            ps[x, y] = factor_pixel([1.0 - 0.55 * a * k for k in (1.0, 1.04, 1.08)])
    return img, scr


def pool(size=64):
    cells = Cells(61, 6, 6)
    grain = octaves(62, 8, 8, 3)
    glow = Image.new('RGBA', (size, size))
    scr = Image.new('RGBA', (size, size))
    hgt = Image.new('RGBA', (size, size))
    pg, ps, ph = glow.load(), scr.load(), hgt.load()
    for y in range(size):
        for x in range(size):
            u, v = (x + 0.5) / size, (y + 0.5) / size
            r = math.hypot(u - 0.5, v - 0.5) * 2.0
            g = fbm(grain, u, v)
            r2 = r * (1.0 + 0.12 * (g - 0.5))
            d1, d2, cr = cells(u, v)
            vein = 1.0 - smoothstep(0.0, 0.15, d2 - d1)
            body = (1.0 - smoothstep(0.15, 0.55, r2))
            i = body * min(1.0, 0.7 + 0.2 * g + 0.5 * vein)
            fleck = body * max(vein * 0.8, smoothstep(0.35, 0.1, d1) * (cr > 0.5))
            if r > 0.97:
                i = fleck = 0.0
            pg[x, y] = (byte(i), byte(i), byte(i), byte(fleck))
            # the crater: black glass inside, a lip of ash, soot around
            soot = (1.0 - smoothstep(0.32, 0.82, r2 * (1.0 + 0.25 * (fbm(grain, u * 0.5, v * 0.5) - 0.5)))) ** 1.5 * (0.35 + 0.65 * g)
            f = 1.0 - 0.6 * soot
            lip = math.exp(-((r2 - 0.42) / 0.07) ** 2)
            f = f + (0.55 - f) * lip * 0.8
            crater = 1.0 - smoothstep(0.28, 0.36, r2)
            f = f * (1.0 - crater) + (0.05 + 0.08 * g) * crater
            ps[x, y] = factor_pixel([1.0 - (1.0 - f) * k for k in (0.85, 0.93, 1.0)])
            h = 0.5 - 0.42 * (1.0 - smoothstep(0.0, 0.38, r2)) ** 1.5 + 0.2 * lip
            ph[x, y] = (byte(h),) * 3 + (255,)
    return glow, scr, hgt


def main():
    os.makedirs(OUT, exist_ok=True)
    s, h = scar()
    s.save(os.path.join(OUT, 'saberscar.tga'))
    h.save(os.path.join(OUT, 'saberscar_h.tga'))
    m, e = molten()
    m.save(os.path.join(OUT, 'sabermolten.tga'))
    e.save(os.path.join(OUT, 'saberembers.tga'))
    halo().save(os.path.join(OUT, 'saberhalo.tga'))
    d, ds = drip()
    d.save(os.path.join(OUT, 'saberdrip.tga'))
    ds.save(os.path.join(OUT, 'saberdrip_scar.tga'))
    p, ps, ph = pool()
    p.save(os.path.join(OUT, 'saberpool.tga'))
    ps.save(os.path.join(OUT, 'saberpool_scar.tga'))
    ph.save(os.path.join(OUT, 'saberpool_h.tga'))
    for old in ('saberburn.tga', 'saberglow.tga'):
        if os.path.exists(os.path.join(OUT, old)):
            os.remove(os.path.join(OUT, old))
    print('wrote the saber scar pictures into', OUT)


if __name__ == '__main__':
    main()
