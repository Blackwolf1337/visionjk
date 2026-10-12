#!/usr/bin/env python3
"""Writes the map of the Vision Temple: tools/temple/build.py [output.bsp]

The map is written here, without a map compiler: what one stands on and where one starts (brushes), and what
one sees of the gold (surfaces: the terrace, the steps of the podium, the way and its kerbs, the columns). The
surfaces have the shader script textures/vision/temple_gold (assets/japlus_client/shaders/vision.shader), which
says "q3map_vision gold": rd-vulkan-vision shades them as its gold (codemp/rd-vulkan-vision/shaders/gold.glsl),
any other renderer in the plain colour of that script. The shapes are the ones of gold.glsl, which also works
the shadows out from them: change a size in both places.

The clouds are a brush of the map: a fog whose shader script (textures/vision/temple_clouds in
assets/japlus_client/shaders/vision.shader) says "q3map_vision clouds" with the place they leave free, which
are numbers of this file too. rd-vulkan-vision fills the brush with clouds, any other renderer with the fog
of that script. The light in the air of the place (the rings and what falls from them) is a second brush of
that kind, textures/vision/temple_light with "q3map_vision light".

Without an argument the map goes to assets/japlus_client/maps/vision_temple.bsp.
"""
import math
import os
import struct
import sys

import numpy

# temple.frag: terrace(), podium(), way()
TERRACE_RADIUS = 208.0
TERRACE_TOP = 2.0
STEPS = 5
STEP_RADIUS = 170.5		# the lowest one, each next one 10 less
STEP_HIGH = 6.0
STEP_TOP = 0.5			# the top of step k is at k * STEP_HIGH + STEP_TOP
WAY_START = -520.0
WAY_END = -170.0
WAY_HALF = 26.0
WAY_TOP = 2.5

SIDES = 48				# of a round thing
DEEP = -64.0			# how far down the floors go
FENCE_TOP = 2048.0
# A fence stops the box of a player, which is 15 to each side of the middle of his feet: the feet come to 8 before
# the edge of the terrace and to 6 before the edges of the way. (The kerbs of the way are in the picture only:
# one would stumble onto them.)
FENCE_INSIDE = 215.0
FENCE_OUTSIDE = 239.0
WAY_FENCE = 35.0		# the way is open between -this and this
FENCE_THICK = 24.0
# The sea of clouds: as far as anybody sees, from under everything up to the roof no cloud is higher than
# (CLOUD_ROOF of the shaders). What they leave free is in the shader script: a round place of TERRACE_RADIUS
# around the middle, and the way with its kerbs, from -524 to -170 and 28 to each side.
CLOUDS_FAR = 16384.0
CLOUDS_DEEP = -512.0
CLOUDS_ROOF = 266.0
# the light stands inside a ball of 650 around this place (light.glsl): the brush is the box around that ball
LIGHT_MIDDLE = (60.0, 0.0, 150.0)
LIGHT_REACH = 650.0

# gold.glsl: columns() and sideColumns(). A column: where it stands in its group, how thick, how high.
SECTOR = 0.5235988
COLUMNS = ((255.0, 0.0, 6.5, 270.0), (272.0, 15.0, 5.5, 196.0), (240.0, -13.0, 5.0, 150.0), (286.0, -9.0, 4.5, 118.0),
           (262.0, -24.0, 4.5, 88.0), (246.0, 22.0, 4.0, 62.0), (297.0, 10.0, 3.5, 42.0))
SIDE_MIDDLE = (150.0, 430.0)
SIDE_COLUMNS = ((0.0, 0.0, 5.0, 86.0), (14.0, 6.0, 4.0, 58.0), (-12.0, 9.0, 4.0, 68.0), (6.0, -13.0, 3.5, 42.0),
                (-9.0, -10.0, 3.5, 30.0))
KERB_INSIDE = 25.1		# way(): the kerbs, with the 0.4 they are rounded by
KERB_OUTSIDE = 29.9
KERB_TOP = 5.9
TERRACE_OUT = 208.5		# terrace(), with the 0.5 it is rounded by
TERRACE_LOW = -3.5
ROUND = 96				# sides of a round thing one sees: a step, the terrace
COLUMN_SIDES = 16
MAX_VERTS = 900			# of one surface: the renderer takes fewer than 1000 (ParseTriSurf())

# game/surfaceflags.h
CONTENTS_SOLID = 0x1
CONTENTS_FOG = 0x8
CONTENTS_PLAYERCLIP = 0x10
SURF_NODAMAGE = 0x40000
SURF_NOIMPACT = 0x80000
SURF_NOMARKS = 0x100000
SURF_NODRAW = 0x200000
MATERIAL_MARBLE = 12

SHADERS = (
    ("textures/vision/temple_floor", SURF_NODAMAGE | SURF_NOMARKS | SURF_NODRAW | MATERIAL_MARBLE, CONTENTS_SOLID),
    ("textures/system/clip", SURF_NODAMAGE | SURF_NOIMPACT | SURF_NOMARKS | SURF_NODRAW, CONTENTS_PLAYERCLIP),
    ("textures/vision/temple_clouds", SURF_NODAMAGE | SURF_NOIMPACT | SURF_NOMARKS, CONTENTS_FOG),
    ("textures/vision/temple_light", SURF_NODAMAGE | SURF_NOIMPACT | SURF_NOMARKS, CONTENTS_FOG),
    ("textures/vision/temple_gold", SURF_NODAMAGE | SURF_NOMARKS | MATERIAL_MARBLE, CONTENTS_SOLID),
)
FLOOR, CLIP, CLOUDS, LIGHT, GOLD = 0, 1, 2, 3, 4

SPAWNS = [(x, y) for x in (-490.0, -450.0, -410.0, -370.0) for y in (-10.0, 10.0)]

# qcommon/qfiles.h
LUMPS = 18
(LUMP_ENTITIES, LUMP_SHADERS, LUMP_PLANES, LUMP_NODES, LUMP_LEAFS, LUMP_LEAFSURFACES, LUMP_LEAFBRUSHES,
 LUMP_MODELS, LUMP_BRUSHES, LUMP_BRUSHSIDES) = range(10)
LUMP_DRAWVERTS, LUMP_DRAWINDEXES, LUMP_FOGS, LUMP_SURFACES = 10, 11, 12, 13
MST_TRIANGLE_SOUP = 3
LIGHTMAP_BY_VERTEX = -3
LS_NORMAL, LS_NONE = 0, 255


class Map:
    def __init__(self):
        self.planes = []
        self.sides = []
        self.brushes = []
        self.fogs = []
        self.mins = [1.0e9] * 3
        self.maxs = [-1.0e9] * 3

    def plane(self, normal, dist):
        """A plane and its opposite, as the format keeps them. Inside a brush is normal . p <= dist."""
        self.planes.append((normal, dist))
        self.planes.append((tuple(-n for n in normal), -dist))
        return len(self.planes) - 2

    def brush(self, shader, points, planes=()):
        """A convex brush: its corner points and the planes that are not along an axis. The collision code
        reads the bounds of a brush from its first six sides, so those come first (CM_BoundBrush())."""
        first = len(self.sides)
        for axis in range(3):
            low = min(p[axis] for p in points)
            high = max(p[axis] for p in points)
            normal = [0.0, 0.0, 0.0]
            normal[axis] = -1.0
            self.sides.append((self.plane(tuple(normal), -low), shader))
            normal[axis] = 1.0
            self.sides.append((self.plane(tuple(normal), high), shader))
            self.mins[axis] = min(self.mins[axis], low)
            self.maxs[axis] = max(self.maxs[axis], high)
        for normal, dist in planes:
            self.sides.append((self.plane(normal, dist), shader))
        self.brushes.append((first, len(self.sides) - first, shader))

    def box(self, shader, mins, maxs):
        self.brush(shader, (mins, maxs))

    def fog(self, shader, mins, maxs):
        """A box of fog. Its side to the sky (the sixth of the sides along the axes) is the one seen from outside."""
        self.box(shader, mins, maxs)
        self.fogs.append((SHADERS[shader][0], len(self.brushes) - 1, 5))

    def disc(self, shader, radius, bottom, top):
        """A round slab: its flat sides touch the circle, so nothing of the circle sticks out of it."""
        corner = radius / math.cos(math.pi / SIDES)
        points = []
        planes = []
        for i in range(SIDES):
            a = 2.0 * math.pi * i / SIDES
            m = a + math.pi / SIDES
            points.append((corner * math.cos(a), corner * math.sin(a), bottom))
            points.append((corner * math.cos(a), corner * math.sin(a), top))
            planes.append(((math.cos(m), math.sin(m), 0.0), radius))
        self.brush(shader, points, planes)

    def wedge(self, shader, i, inside, outside, bottom, top):
        """One piece of a ring: between the corners i and i + 1 of a disc()."""
        a0 = 2.0 * math.pi * i / SIDES
        a1 = 2.0 * math.pi * (i + 1) / SIDES
        m = 0.5 * (a0 + a1)
        far = outside / math.cos(math.pi / SIDES)
        points = [(r * math.cos(a), r * math.sin(a), z)
                  for r in (inside, far) for a in (a0, a1) for z in (bottom, top)]
        self.brush(shader, points, (
            ((-math.cos(m), -math.sin(m), 0.0), -inside * math.cos(math.pi / SIDES)),
            ((math.cos(m), math.sin(m), 0.0), outside),
            ((math.sin(a0), -math.cos(a0), 0.0), 0.0),
            ((-math.sin(a1), math.cos(a1), 0.0), 0.0),
        ))


class Mesh:
    """The triangles one sees, in pieces the renderer takes: a piece is a surface of the map."""

    def __init__(self):
        self.pieces = []		# (points with their normals, indexes)
        self.points = []
        self.indexes = []

    def cut(self, more):
        if self.points and len(self.points) + more > MAX_VERTS:
            self.pieces.append((self.points, self.indexes))
            self.points, self.indexes = [], []

    def grid(self, rows, turned=False):
        """Rows of as many points each, (place, normal), joined to a sheet. A row that ends where it began is a
        closed band. The renderer draws the side of a triangle on which its corners go around clockwise: that is
        the side one looks at with the first row away from oneself and the rows running to the left, and with
        turned the other one."""
        width = len(rows[0])
        self.cut(width * len(rows))
        first = len(self.points)
        for row in rows:
            self.points.extend(row)
        for r in range(len(rows) - 1):
            for c in range(width - 1):
                a = first + r * width + c
                if turned:
                    self.indexes.extend((a, a + width, a + 1, a + 1, a + width, a + width + 1))
                else:
                    self.indexes.extend((a, a + 1, a + width, a + 1, a + width + 1, a + width))

    def circle(self, x, y, radius, z, normal, sides):
        """The points of a circle, the last one on the first. normal: of each point, None: away from the middle."""
        row = []
        for i in range(sides + 1):
            a = 2.0 * math.pi * (i % sides) / sides
            c, s = math.cos(a), math.sin(a)
            row.append(((x + radius * c, y + radius * s, z), normal or (c, s, 0.0)))
        return row

    def shaft(self, x, y, radius, bottom, top, sides):
        self.grid([self.circle(x, y, radius, bottom, None, sides), self.circle(x, y, radius, top, None, sides)], True)

    def ring(self, x, y, inside, outside, z, sides):
        """A flat ring that looks up. inside 0: a disc."""
        up = (0.0, 0.0, 1.0)
        self.grid([self.circle(x, y, inside, z, up, sides), self.circle(x, y, outside, z, up, sides)])

    def ball(self, x, y, z, radius, sides):
        rows = []
        for j in range(sides // 2 + 1):
            b = math.pi * j / (sides // 2) - 0.5 * math.pi
            row = []
            for i in range(sides + 1):
                a = 2.0 * math.pi * (i % sides) / sides
                n = (math.cos(b) * math.cos(a), math.cos(b) * math.sin(a), math.sin(b))
                row.append(((x + radius * n[0], y + radius * n[1], z + radius * n[2]), n))
            rows.append(row)
        self.grid(rows, True)

    def column(self, x, y, radius, high):
        """column() of gold.glsl: a shaft, and a ball on it that is a little wider."""
        self.shaft(x, y, radius, 0.0, high, COLUMN_SIDES)
        self.ball(x, y, high + 0.5 * radius, 1.3 * radius, COLUMN_SIDES)

    def box(self, mins, maxs):
        """The four walls and the top of a box."""
        (x0, y0, z0), (x1, y1, z1) = mins, maxs
        for a, b, n in ((((x0, y0), (x1, y0)), (z0, z1), (0.0, -1.0, 0.0)), (((x1, y1), (x0, y1)), (z0, z1), (0.0, 1.0, 0.0)),
                        (((x0, y1), (x0, y0)), (z0, z1), (-1.0, 0.0, 0.0)), (((x1, y0), (x1, y1)), (z0, z1), (1.0, 0.0, 0.0))):
            self.grid([[((a[0][0], a[0][1], b[0]), n), ((a[1][0], a[1][1], b[0]), n)],
                       [((a[0][0], a[0][1], b[1]), n), ((a[1][0], a[1][1], b[1]), n)]], True)
        up = (0.0, 0.0, 1.0)
        self.grid([[((x0, y0, z1), up), ((x1, y0, z1), up)], [((x0, y1, z1), up), ((x1, y1, z1), up)]], True)

    def done(self):
        if self.points:
            self.pieces.append((self.points, self.indexes))
            self.points, self.indexes = [], []
        return self.pieces


def hash2(x, y):
    """hash() of vision.glsl, in the numbers of a graphics card: 32 bit."""
    f = numpy.float32
    px, py = f(x) * f(0.1031), f(y) * f(0.1030)
    px, py = px - numpy.floor(px), py - numpy.floor(py)
    d = px * (py + f(33.33)) + py * (px + f(33.33))
    px, py = px + d, py + d
    v = (px + py) * px
    return float(v - numpy.floor(v))


def gold():
    """What one sees of the gold: the shapes of gold.glsl as surfaces."""
    g = Mesh()

    # the terrace, and on it the five steps of the podium: each a wall and what is left of its top
    g.shaft(0.0, 0.0, TERRACE_OUT, TERRACE_LOW, TERRACE_TOP, ROUND)
    g.ring(0.0, 0.0, STEP_RADIUS, TERRACE_OUT, TERRACE_TOP, ROUND)
    for k in range(1, STEPS + 1):
        radius = STEP_RADIUS - 10.0 * (k - 1)
        top = k * STEP_HIGH + STEP_TOP
        g.shaft(0.0, 0.0, radius, TERRACE_TOP if k == 1 else (k - 1) * STEP_HIGH + STEP_TOP, top, ROUND)
        g.ring(0.0, 0.0, radius - 10.0 if k < STEPS else 0.0, radius, top, ROUND)

    # the way, and a kerb at each side of it
    g.box((WAY_START, -WAY_HALF, DEEP), (WAY_END, WAY_HALF, WAY_TOP))
    for side in (-1.0, 1.0):
        ys = sorted((side * KERB_INSIDE, side * KERB_OUTSIDE))
        g.box((WAY_START - 0.4, ys[0], DEEP), (WAY_END + 0.4, ys[1], KERB_TOP))

    # the columns: ten groups around the podium (none on the way in and none before the great ring), each as
    # high as its number says, and a small group under each of the two rings at the sides
    for sector in range(-5, 6):
        if sector == 0:
            continue
        c, s = math.cos(sector * SECTOR), math.sin(sector * SECTOR)
        tall = 0.7 + 0.7 * hash2(sector + 20.0, 7.0)
        for x, y, radius, high in COLUMNS:
            g.column(c * x - s * y, s * x + c * y, radius, high * tall)
    for side in (-1.0, 1.0):
        for x, y, radius, high in SIDE_COLUMNS:
            g.column(SIDE_MIDDLE[0] + x, side * (SIDE_MIDDLE[1] + y), radius, high)
    return g.done()


def temple():
    m = Map()

    m.disc(FLOOR, TERRACE_RADIUS, DEEP, TERRACE_TOP)
    for k in range(1, STEPS + 1):
        m.disc(FLOOR, STEP_RADIUS - 10.0 * (k - 1), TERRACE_TOP, k * STEP_HIGH + STEP_TOP)
    m.box(FLOOR, (WAY_START, -WAY_HALF, DEEP), (WAY_END, WAY_HALF, WAY_TOP))

    # The fences: around the terrace, open where the way comes in (the four pieces there are left out), along
    # the way and across its start. They reach over one another where they meet: what stays open between the
    # ring and the fences of the way is narrower than a player.
    for i in range(SIDES):
        if abs(i + 0.5 - SIDES / 2) > 2.0:
            m.wedge(CLIP, i, FENCE_INSIDE, FENCE_OUTSIDE, DEEP, FENCE_TOP)
    meet = -FENCE_INSIDE * math.cos(4.0 * math.pi / SIDES) + 12.0
    back = WAY_START - FENCE_THICK
    for side in (-1.0, 1.0):
        ys = sorted((side * WAY_FENCE, side * (WAY_FENCE + FENCE_THICK)))
        m.box(CLIP, (back, ys[0], DEEP), (meet, ys[1], FENCE_TOP))
    m.box(CLIP, (back, -WAY_FENCE - FENCE_THICK, DEEP), (WAY_START + 4.0, WAY_FENCE + FENCE_THICK, FENCE_TOP))

    m.fog(CLOUDS, (-CLOUDS_FAR, -CLOUDS_FAR, CLOUDS_DEEP), (CLOUDS_FAR, CLOUDS_FAR, CLOUDS_ROOF))
    m.fog(LIGHT, tuple(v - LIGHT_REACH for v in LIGHT_MIDDLE), tuple(v + LIGHT_REACH for v in LIGHT_MIDDLE))
    return m


def entities():
    text = ['{\n"classname" "worldspawn"\n"message" "Vision Temple"\n"distanceCull" "8192"\n}\n']
    for x, y in SPAWNS:
        # the feet are 24 under the origin, and a spawn point may hang a little over the ground
        text.append('{\n"classname" "info_player_deathmatch"\n"origin" "%g %g %g"\n"angle" "0"\n}\n'
                    % (x, y, WAY_TOP + 25.0))
    return "".join(text).encode("ascii") + b"\0"


def surfaces(pieces):
    """The lumps of what is drawn: every piece a surface of triangles that brings its light in its points (white:
    the script says the colour), in no fog."""
    verts, indexes, out = [], [], []
    for points, tris in pieces:
        out.append(struct.pack("<7i8B4i8i2i3f9f2i", GOLD, -1, MST_TRIANGLE_SOUP, len(verts), len(points), len(indexes), len(tris),
                               LS_NONE, LS_NONE, LS_NONE, LS_NONE, LS_NORMAL, LS_NONE, LS_NONE, LS_NONE,
                               LIGHTMAP_BY_VERTEX, LIGHTMAP_BY_VERTEX, LIGHTMAP_BY_VERTEX, LIGHTMAP_BY_VERTEX,
                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0, 0.0, 0.0, *([0.0] * 9), 0, 0))
        for place, normal in points:
            verts.append(struct.pack("<3f2f8f3f16B", *place, 0.0, 0.0, *([0.0] * 8), *normal,
                                     255, 255, 255, 255, *([0] * 12)))
        indexes.extend(tris)
    return b"".join(verts), struct.pack("<%di" % len(indexes), *indexes), b"".join(out), len(out)


def write(m, path):
    ints = lambda values: [int(math.floor(v)) for v in values]
    mins = ints(m.mins)
    maxs = [-v for v in ints(-x for x in m.maxs)]
    # One plane far under everything is the whole tree: leaf 1 above it holds every brush, leaf 0 under it
    # is solid and empty.
    under = len(m.planes)
    planes = m.planes + [((0.0, 0.0, 1.0), DEEP - 4096.0), ((0.0, 0.0, -1.0), -(DEEP - 4096.0))]

    pieces = gold()
    for points, _ in pieces:
        for place, _ in points:
            for axis in range(3):
                m.mins[axis] = min(m.mins[axis], place[axis])
                m.maxs[axis] = max(m.maxs[axis], place[axis])
    mins = ints(m.mins)
    maxs = [-v for v in ints(-x for x in m.maxs)]
    verts, indexes, drawn, count = surfaces(pieces)

    lumps = [b""] * LUMPS
    lumps[LUMP_ENTITIES] = entities()
    lumps[LUMP_DRAWVERTS] = verts
    lumps[LUMP_DRAWINDEXES] = indexes
    lumps[LUMP_SURFACES] = drawn
    lumps[LUMP_LEAFSURFACES] = struct.pack("<%di" % count, *range(count))
    lumps[LUMP_SHADERS] = b"".join(struct.pack("<64sii", name.encode("ascii"), surface, contents)
                                   for name, surface, contents in SHADERS)
    lumps[LUMP_PLANES] = b"".join(struct.pack("<4f", *normal, dist) for normal, dist in planes)
    lumps[LUMP_NODES] = struct.pack("<3i3i3i", under, -2, -1, *mins, *maxs)
    lumps[LUMP_LEAFS] = (struct.pack("<2i3i3i4i", -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
                         + struct.pack("<2i3i3i4i", 0, 0, *mins, *maxs, 0, count, 0, len(m.brushes)))
    lumps[LUMP_LEAFBRUSHES] = struct.pack("<%di" % len(m.brushes), *range(len(m.brushes)))
    lumps[LUMP_MODELS] = struct.pack("<6f4i", *m.mins, *m.maxs, 0, count, 0, len(m.brushes))
    lumps[LUMP_BRUSHES] = b"".join(struct.pack("<3i", *brush) for brush in m.brushes)
    lumps[LUMP_BRUSHSIDES] = b"".join(struct.pack("<3i", plane, shader, -1) for plane, shader in m.sides)
    lumps[LUMP_FOGS] = b"".join(struct.pack("<64s2i", name.encode("ascii"), brush, side) for name, brush, side in m.fogs)

    offset = 8 + 8 * LUMPS
    header = [b"RBSP", struct.pack("<i", 1)]
    body = []
    for lump in lumps:
        header.append(struct.pack("<2i", offset, len(lump)))
        lump += b"\0" * (-len(lump) % 4)
        body.append(lump)
        offset += len(lump)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"".join(header + body))
    return len(m.brushes), count, offset


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "..", "assets", "japlus_client", "maps", "vision_temple.bsp")
    brushes, drawn, size = write(temple(), path)
    print("%s: %d brushes, %d surfaces, %d bytes" % (os.path.normpath(path), brushes, drawn, size))


if __name__ == "__main__":
    main()
