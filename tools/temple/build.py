#!/usr/bin/env python3
"""Writes the map of the Vision Temple: tools/temple/build.py [output.bsp]

The place itself is drawn by a shader of the renderer (codemp/rd-vulkan-vision/shaders/temple.frag). This map
is what the game needs of it: what one stands on and where one starts. It has brushes and no surfaces, so no
map compiler is needed, and its numbers are the ones of the shader: change them in both places.

Without an argument the map goes to assets/japlus_client/maps/vision_temple.bsp.
"""
import math
import os
import struct
import sys

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

# game/surfaceflags.h
CONTENTS_SOLID = 0x1
CONTENTS_PLAYERCLIP = 0x10
SURF_NODAMAGE = 0x40000
SURF_NOIMPACT = 0x80000
SURF_NOMARKS = 0x100000
SURF_NODRAW = 0x200000
MATERIAL_MARBLE = 12

SHADERS = (
    ("textures/vision/temple_floor", SURF_NODAMAGE | SURF_NOMARKS | SURF_NODRAW | MATERIAL_MARBLE, CONTENTS_SOLID),
    ("textures/system/clip", SURF_NODAMAGE | SURF_NOIMPACT | SURF_NOMARKS | SURF_NODRAW, CONTENTS_PLAYERCLIP),
)
FLOOR, CLIP = 0, 1

SPAWNS = [(x, y) for x in (-490.0, -450.0, -410.0, -370.0) for y in (-10.0, 10.0)]

# qcommon/qfiles.h
LUMPS = 18
(LUMP_ENTITIES, LUMP_SHADERS, LUMP_PLANES, LUMP_NODES, LUMP_LEAFS, LUMP_LEAFSURFACES, LUMP_LEAFBRUSHES,
 LUMP_MODELS, LUMP_BRUSHES, LUMP_BRUSHSIDES) = range(10)


class Map:
    def __init__(self):
        self.planes = []
        self.sides = []
        self.brushes = []
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
    return m


def entities():
    text = ['{\n"classname" "worldspawn"\n"message" "Vision Temple"\n"distanceCull" "8192"\n}\n']
    for x, y in SPAWNS:
        # the feet are 24 under the origin, and a spawn point may hang a little over the ground
        text.append('{\n"classname" "info_player_deathmatch"\n"origin" "%g %g %g"\n"angle" "0"\n}\n'
                    % (x, y, WAY_TOP + 25.0))
    return "".join(text).encode("ascii") + b"\0"


def write(m, path):
    ints = lambda values: [int(math.floor(v)) for v in values]
    mins = ints(m.mins)
    maxs = [-v for v in ints(-x for x in m.maxs)]
    # One plane far under everything is the whole tree: leaf 1 above it holds every brush, leaf 0 under it
    # is solid and empty.
    under = len(m.planes)
    planes = m.planes + [((0.0, 0.0, 1.0), DEEP - 4096.0), ((0.0, 0.0, -1.0), -(DEEP - 4096.0))]

    lumps = [b""] * LUMPS
    lumps[LUMP_ENTITIES] = entities()
    lumps[LUMP_SHADERS] = b"".join(struct.pack("<64sii", name.encode("ascii"), surface, contents)
                                   for name, surface, contents in SHADERS)
    lumps[LUMP_PLANES] = b"".join(struct.pack("<4f", *normal, dist) for normal, dist in planes)
    lumps[LUMP_NODES] = struct.pack("<3i3i3i", under, -2, -1, *mins, *maxs)
    lumps[LUMP_LEAFS] = (struct.pack("<2i3i3i4i", -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
                         + struct.pack("<2i3i3i4i", 0, 0, *mins, *maxs, 0, 0, 0, len(m.brushes)))
    lumps[LUMP_LEAFBRUSHES] = struct.pack("<%di" % len(m.brushes), *range(len(m.brushes)))
    lumps[LUMP_MODELS] = struct.pack("<6f4i", *m.mins, *m.maxs, 0, 0, 0, len(m.brushes))
    lumps[LUMP_BRUSHES] = b"".join(struct.pack("<3i", *brush) for brush in m.brushes)
    lumps[LUMP_BRUSHSIDES] = b"".join(struct.pack("<3i", plane, shader, -1) for plane, shader in m.sides)

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
    return len(m.brushes), offset


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "..", "assets", "japlus_client", "maps", "vision_temple.bsp")
    brushes, size = write(temple(), path)
    print("%s: %d brushes, %d bytes" % (os.path.normpath(path), brushes, size))


if __name__ == "__main__":
    main()
