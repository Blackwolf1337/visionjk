#!/usr/bin/env python3
"""Writes the model of the boombox and its pictures: tools/boombox/build.py [folder]

An 80s style portable stereo of no make in particular, made here from boxes, cylinders and one turned profile,
without a modelling program. Two models: the box (models/map_objects/vision/boombox.md3) and one speaker
(boombox_cone.md3), which the cgame draws twice in front of the box and stretches along its axis with the song
(codemp/cgame/boombox/cg_boombox.c: BOX_CONE_X, BOX_CONE_Y and BOX_CONE_Z are CONE_X, CONE_Y and CONE_Z here,
change them in both places). The sizes the game picks and places the box by are in
codemp/game/boombox/bg_boombox.h (BOX_HALF_WIDTH, BOX_HALF_DEPTH, BOX_HEIGHT, BOX_SPEAKER_HEIGHT).

The pictures are painted here too: boombox.png (one picture for every face of the box), boombox_lights.png (the
same places, black but for what glows: the dial and a few lights), boombox_env.png (what the chrome reflects) and
boombox_cone.png. Their shader scripts are in assets/japlus_client/shaders/vision.shader.

The model faces +x, stands on z 0, and its middle is at x 0, y 0. A triangle of an MD3 turns clockwise seen from
outside (its cross product points into the model), as in the models of the game; every one is checked.

Without an argument everything goes to assets/japlus_client/models/map_objects/vision.
"""
import math
import os
import struct
import sys

import numpy
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "assets", "japlus_client", "models", "map_objects", "vision")
SHADER_DIR = "models/map_objects/vision/"

# the box (bg_boombox.h: BOX_HALF_DEPTH 6, BOX_HALF_WIDTH 16, BOX_HEIGHT 26 with the handle and a margin)
BODY_LO = (-6.0, -16.0, 1.0)
BODY_HI = (6.0, 16.0, 19.0)
BEVEL = 0.8
# the speakers on its front (cg_boombox.c), and how large they are
CONE_X = 6.0
CONE_Y = 10.0
CONE_Z = 9.0
CONE_R = 6.0
# what stands out of the front: the door of the cassette and the strip of the dial
DOOR_LO = (6.0, -4.3, 3.6)
DOOR_HI = (6.35, 4.3, 12.6)
DIAL_LO = (6.0, -14.6, 15.2)
DIAL_HI = (6.25, 14.6, 18.2)

ATLAS = 512
# where the faces are in boombox.png, in pixels: x0, y0, x1, y1
R_FRONT = (0, 0, 512, 256)		# the front of the box
R_PLASTIC = (0, 256, 512, 384)	# every other face of it
R_DOOR = (0, 384, 256, 512)		# the door of the cassette
R_DIAL = (256, 384, 512, 448)	# the dial
R_KEYS = (256, 448, 512, 512)	# the keys on the top

SIDES = 16						# of a round thing


# ---------------------------------------------------------------------------------------------------------------
# geometry
# ---------------------------------------------------------------------------------------------------------------

def sub(a, b):
	return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
	return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a):
	length = math.sqrt(dot(a, a))
	return (a[0] / length, a[1] / length, a[2] / length)


class Surface:
	"""One surface of an MD3: vertices with a normal and a place in the picture, and triangles."""

	def __init__(self, name, shader):
		self.name = name
		self.shader = shader
		self.verts = []		# (xyz, normal, (u, v))
		self.tris = []		# turned as written: clockwise seen from outside

	def poly(self, points, normal, uvs):
		"""A flat polygon with that normal out of it, any turn: it is turned the way of the game here."""
		base = len(self.verts)
		for p, uv in zip(points, uvs):
			self.verts.append((p, normal, uv))
		# the first triangle tells which way the points go round
		ccw = dot(cross(sub(points[1], points[0]), sub(points[2], points[0])), normal) > 0
		for i in range(1, len(points) - 1):
			a, b, c = base, base + i, base + i + 1
			self.tris.append((a, c, b) if ccw else (a, b, c))

	def smooth(self, rows, closed=True):
		"""A band of quads between rows of vertices (xyz, normal, uv) that already have their normals."""
		base = len(self.verts)
		width = len(rows[0])
		for row in rows:
			self.verts.extend(row)
		for r in range(len(rows) - 1):
			for k in range(width - 1):
				a = base + r * width + k
				b = a + 1
				c = a + width + 1
				d = a + width
				self._quad(a, b, c, d)

	def _quad(self, a, b, c, d):
		# turned by the normals of its vertices
		pa, pb, pc = self.verts[a][0], self.verts[b][0], self.verts[c][0]
		n = self.verts[a][1]
		face = cross(sub(pb, pa), sub(pc, pa))
		if dot(face, face) < 1e-12:
			pd = self.verts[d][0]
			face = cross(sub(pc, pa), sub(pd, pa))
		if dot(face, n) > 0:
			self.tris.extend([(a, c, b), (a, d, c)])
		else:
			self.tris.extend([(a, b, c), (a, c, d)])

	def check(self):
		for a, b, c in self.tris:
			pa, pb, pc = self.verts[a][0], self.verts[b][0], self.verts[c][0]
			face = cross(sub(pb, pa), sub(pc, pa))
			n = tuple(self.verts[a][1][i] + self.verts[b][1][i] + self.verts[c][1][i] for i in range(3))
			if dot(face, face) > 1e-12 and dot(face, n) > 0:
				sys.exit("%s: a triangle turns the wrong way" % self.name)
		if len(self.verts) > 1000 or len(self.tris) > 2000:
			sys.exit("%s: %i vertices and %i triangles, a surface has 1000 and 2000 at most" % (self.name, len(self.verts), len(self.tris)))


def region_uv(region, s, t):
	"""s and t from 0 to 1 across a region of the atlas, to the place in the picture"""
	x0, y0, x1, y1 = region
	return ((x0 + s * (x1 - x0)) / ATLAS, (y0 + t * (y1 - y0)) / ATLAS)


def plastic_uv(p, normal):
	"""a face of plastic: the picture of the plastic laid on it from the side it faces"""
	n = [abs(c) for c in normal]
	if n[0] >= n[1] and n[0] >= n[2]:
		s, t = p[1], p[2]
	elif n[1] >= n[2]:
		s, t = p[0], p[2]
	else:
		s, t = p[0], p[1]
	# (the box is within 25 units of its middle on every axis)
	return region_uv(R_PLASTIC, (s + 25.0) / 50.0, (t + 25.0) / 50.0)


def chamfered_box(surface, lo, hi, bevel, front=None):
	"""A box with its edges cut off. front: how the face toward +x takes the picture, else it is plastic"""
	c = bevel
	faces = []
	for axis in range(3):
		for sign in (1, -1):
			normal = [0.0, 0.0, 0.0]
			normal[axis] = float(sign)
			a, b = [i for i in range(3) if i != axis]
			corners = []
			for sa, sb in ((0, 0), (1, 0), (1, 1), (0, 1)):
				p = [0.0, 0.0, 0.0]
				p[axis] = hi[axis] if sign > 0 else lo[axis]
				p[a] = (lo[a] + c) if sa == 0 else (hi[a] - c)
				p[b] = (lo[b] + c) if sb == 0 else (hi[b] - c)
				corners.append(tuple(p))
			n = tuple(normal)
			if front and axis == 0 and sign > 0:
				uvs = [front(p) for p in corners]
			else:
				uvs = [plastic_uv(p, n) for p in corners]
			surface.poly(corners, n, uvs)
			faces.append((axis, sign))

	# the cut edges: between two faces, along the third axis
	for ax1 in range(3):
		for ax2 in range(ax1 + 1, 3):
			along = 3 - ax1 - ax2
			for s1 in (1, -1):
				for s2 in (1, -1):
					v1 = hi[ax1] if s1 > 0 else lo[ax1]
					v2 = hi[ax2] if s2 > 0 else lo[ax2]
					points = []
					for end in (lo[along] + c, hi[along] - c):
						p = [0.0, 0.0, 0.0]
						p[ax1] = v1
						p[ax2] = v2 - s2 * c
						p[along] = end
						points.append(tuple(p))
						q = [0.0, 0.0, 0.0]
						q[ax1] = v1 - s1 * c
						q[ax2] = v2
						q[along] = end
						points.append(tuple(q))
					quad = [points[0], points[2], points[3], points[1]]
					normal = [0.0, 0.0, 0.0]
					normal[ax1] = s1
					normal[ax2] = s2
					n = norm(normal)
					surface.poly(quad, n, [plastic_uv(p, n) for p in quad])
	# the corners
	for sx in (1, -1):
		for sy in (1, -1):
			for sz in (1, -1):
				corner = (hi[0] if sx > 0 else lo[0], hi[1] if sy > 0 else lo[1], hi[2] if sz > 0 else lo[2])
				tri = []
				for axis in range(3):
					p = list(corner)
					for i in range(3):
						if i != axis:
							p[i] -= (sx, sy, sz)[i] * c
					tri.append(tuple(p))
				n = norm((sx, sy, sz))
				surface.poly(tri, n, [plastic_uv(p, n) for p in tri])


def plain_box(surface, lo, hi, uv_of=None):
	"""A box without cut edges. uv_of(p, normal): where a corner is in the picture, else plastic"""
	for axis in range(3):
		for sign in (1, -1):
			normal = [0.0, 0.0, 0.0]
			normal[axis] = float(sign)
			n = tuple(normal)
			a, b = [i for i in range(3) if i != axis]
			corners = []
			for sa, sb in ((0, 0), (1, 0), (1, 1), (0, 1)):
				p = [0.0, 0.0, 0.0]
				p[axis] = hi[axis] if sign > 0 else lo[axis]
				p[a] = lo[a] if sa == 0 else hi[a]
				p[b] = lo[b] if sb == 0 else hi[b]
				corners.append(tuple(p))
			surface.poly(corners, n, [(uv_of or plastic_uv)(p, n) for p in corners])


def env_uv(p, normal):
	# (chrome: the picture is what it reflects, tcGen environment. The coordinates do not matter)
	return (0.5, 0.5)


def cylinder(surface, start, axis, radius, length, caps=True):
	"""A cylinder from start along the axis (0, 1 or 2) for length, smooth round"""
	a, b = [i for i in range(3) if i != axis]
	ring0, ring1 = [], []
	for k in range(SIDES + 1):
		angle = 2.0 * math.pi * k / SIDES
		n = [0.0, 0.0, 0.0]
		n[a] = math.cos(angle)
		n[b] = math.sin(angle)
		p0 = list(start)
		p0[a] += radius * n[a]
		p0[b] += radius * n[b]
		p1 = list(p0)
		p1[axis] += length
		ring0.append((tuple(p0), tuple(n), (k / SIDES, 0.0)))
		ring1.append((tuple(p1), tuple(n), (k / SIDES, 1.0)))
	surface.smooth([ring0, ring1])
	if caps:
		for end, sign in ((0.0, -1.0), (length, 1.0)):
			n = [0.0, 0.0, 0.0]
			n[axis] = sign
			points = []
			for k in range(SIDES):
				angle = 2.0 * math.pi * k / SIDES
				p = list(start)
				p[a] += radius * math.cos(angle)
				p[b] += radius * math.sin(angle)
				p[axis] += end
				points.append(tuple(p))
			surface.poly(points, tuple(n), [(0.5, 0.5)] * len(points))


def front_uv(p):
	"""the front of the box: across it as one looks at it (+y is right then), down from its top"""
	return region_uv(R_FRONT, (p[1] - BODY_LO[1]) / (BODY_HI[1] - BODY_LO[1]), (BODY_HI[2] - p[2]) / (BODY_HI[2] - BODY_LO[2]))


def flat_front_uv(region, lo, hi):
	def uv(p, normal):
		if normal[0] > 0.5:
			return region_uv(region, (p[1] - lo[1]) / (hi[1] - lo[1]), (hi[2] - p[2]) / (hi[2] - lo[2]))
		return plastic_uv(p, normal)
	return uv


def keys_uv(lo, hi, index, count):
	def uv(p, normal):
		if normal[2] > 0.5:
			s = (index + (p[1] - lo[1]) / (hi[1] - lo[1])) / count
			t = (hi[0] - p[0]) / (hi[0] - lo[0])
			return region_uv(R_KEYS, s, t)
		return plastic_uv(p, normal)
	return uv


def build_box():
	body = Surface("body", SHADER_DIR + "boombox")
	chrome = Surface("chrome", SHADER_DIR + "boombox_chrome")

	chamfered_box(body, BODY_LO, BODY_HI, BEVEL, front=front_uv)
	plain_box(body, DOOR_LO, DOOR_HI, flat_front_uv(R_DOOR, DOOR_LO, DOOR_HI))
	plain_box(body, DIAL_LO, DIAL_HI, flat_front_uv(R_DIAL, DIAL_LO, DIAL_HI))
	# four feet
	for x in (-4.5, 3.5):
		for y in (-14.0, 12.5):
			plain_box(body, (x, y, 0.0), (x + 1.0, y + 1.5, 1.0))
	# six keys along the front of the top
	for k in range(6):
		lo = (1.2, -9.0 + k * 1.9, BODY_HI[2])
		hi = (4.8, -9.0 + k * 1.9 + 1.6, BODY_HI[2] + 0.7)
		plain_box(body, lo, hi, keys_uv(lo, hi, k, 6))
	# the handle: two posts and a bar between them
	for y in (-13.0, 11.6):
		plain_box(body, (-0.7, y, BODY_HI[2]), (0.7, y + 1.4, 23.2))
	cylinder(chrome, (0.0, -13.6, 23.9), 1, 1.0, 27.2)
	# two knobs on the top, at the right
	for y in (7.5, 11.5):
		cylinder(chrome, (2.4, y, BODY_HI[2]), 2, 1.1, 1.0)
	# the antenna, pushed in, along the back of the top
	cylinder(chrome, (-4.6, -14.0, BODY_HI[2] + 0.4), 1, 0.35, 23.0)
	return [body, chrome]


# the speaker, turned round its axis +x from the edge in: (radius, height out of the box)
CONE_PROFILE = [
	(6.0, 0.0), (5.95, 0.18), (5.75, 0.25),		# the ring of chrome it is held by
	(5.6, 0.25), (5.35, 0.55), (5.0, 0.7), (4.65, 0.55), (4.45, 0.3),	# the rubber round it
	(4.0, 0.28), (3.3, 0.22), (2.6, 0.16), (2.0, 0.12),	# the cone
	(1.9, 0.14), (1.6, 0.5), (1.15, 0.78), (0.6, 0.94), (0.0, 1.0),	# the cap in its middle
]


def build_cone():
	cone = Surface("cone", SHADER_DIR + "boombox_cone")
	rows = []
	for i, (r, h) in enumerate(CONE_PROFILE):
		# the normal of the profile: across its slope, outward
		prev = CONE_PROFILE[max(0, i - 1)]
		nxt = CONE_PROFILE[min(len(CONE_PROFILE) - 1, i + 1)]
		dr, dh = nxt[0] - prev[0], nxt[1] - prev[1]
		# across the profile, out of the box: (dh, -dr) for a profile that goes toward the middle (dr < 0)
		nr, nh = dh, -dr
		if nh < 0:
			nr, nh = -nr, -nh
		length = math.hypot(nr, nh) or 1.0
		nr, nh = nr / length, nh / length
		row = []
		for k in range(SIDES * 2 + 1):
			angle = 2.0 * math.pi * k / (SIDES * 2)
			c, s = math.cos(angle), math.sin(angle)
			p = (h, r * c, r * s)
			n = (nh, nr * c, nr * s)
			# the picture lies on it as seen from the front: +y to the right, +z up
			uv = (0.5 + r * c / (2.0 * CONE_R), 0.5 - r * s / (2.0 * CONE_R))
			row.append((p, n, uv))
		rows.append(row)
	cone.smooth(rows)
	return [cone]


# ---------------------------------------------------------------------------------------------------------------
# MD3
# ---------------------------------------------------------------------------------------------------------------

def encode_normal(n):
	"""latitude in the high byte, longitude in the low one, as the renderer reads them (256 steps a turn)"""
	x, y, z = n
	if abs(x) < 1e-6 and abs(y) < 1e-6:
		return 0 if z > 0 else 128
	lat = int(round(math.atan2(y, x) * 256.0 / (2.0 * math.pi))) & 0xff
	lng = int(round(math.acos(max(-1.0, min(1.0, z))) * 256.0 / (2.0 * math.pi))) & 0xff
	return (lat << 8) | lng


def write_md3(path, name, surfaces):
	for s in surfaces:
		s.check()
	points = [v[0] for s in surfaces for v in s.verts]
	lo = [min(p[i] for p in points) for i in range(3)]
	hi = [max(p[i] for p in points) for i in range(3)]
	radius = max(math.sqrt(dot(p, p)) for p in points)

	blobs = []
	for s in surfaces:
		head = 108
		ofs_shaders = head
		ofs_tris = ofs_shaders + 68
		ofs_st = ofs_tris + 12 * len(s.tris)
		ofs_xyz = ofs_st + 8 * len(s.verts)
		ofs_end = ofs_xyz + 8 * len(s.verts)
		blob = struct.pack("<4s64siiiiiiiiii", b"IDP3", s.name.encode(), 0, 1, 1, len(s.verts), len(s.tris),
			ofs_tris, ofs_shaders, ofs_st, ofs_xyz, ofs_end)
		blob += struct.pack("<64si", s.shader.encode(), 0)
		for t in s.tris:
			blob += struct.pack("<iii", *t)
		for v in s.verts:
			blob += struct.pack("<ff", v[2][0], v[2][1])
		for v in s.verts:
			p = v[0]
			blob += struct.pack("<hhhH", *[int(round(c * 64.0)) for c in p], encode_normal(norm(v[1])))
		assert len(blob) == ofs_end
		blobs.append(blob)

	ofs_frames = 108
	ofs_tags = ofs_frames + 56
	ofs_surfaces = ofs_tags
	ofs_end = ofs_surfaces + sum(len(b) for b in blobs)
	data = struct.pack("<4si64siiiiiiiii", b"IDP3", 15, name.encode(), 0, 1, 0, len(surfaces), 0,
		ofs_frames, ofs_tags, ofs_surfaces, ofs_end)
	data += struct.pack("<3f3f3ff16s", *lo, *hi, 0.0, 0.0, 0.0, radius, b"frame0")
	data += b"".join(blobs)
	assert len(data) == ofs_end
	with open(path, "wb") as f:
		f.write(data)
	return sum(len(s.verts) for s in surfaces), sum(len(s.tris) for s in surfaces)


# ---------------------------------------------------------------------------------------------------------------
# pictures
# ---------------------------------------------------------------------------------------------------------------

def noise(w, h, amount, seed):
	rng = numpy.random.default_rng(seed)
	return rng.normal(0.0, amount, (h, w))


def to_image(rgb):
	return Image.fromarray(numpy.clip(rgb, 0, 255).astype(numpy.uint8), "RGB")


def front_place(y, z):
	"""a place of the front of the box in pixels of the picture"""
	s = (y - BODY_LO[1]) / (BODY_HI[1] - BODY_LO[1])
	t = (BODY_HI[2] - z) / (BODY_HI[2] - BODY_LO[2])
	x0, y0, x1, y1 = R_FRONT
	return (x0 + s * (x1 - x0), y0 + t * (y1 - y0))


def paint_atlas():
	img = Image.new("RGB", (ATLAS, ATLAS), (40, 42, 46))
	lights = Image.new("RGB", (ATLAS, ATLAS), (0, 0, 0))
	draw = ImageDraw.Draw(img)
	glow = ImageDraw.Draw(lights)

	# the front: brushed silver, a little darker toward the bottom
	x0, y0, x1, y1 = R_FRONT
	w, h = x1 - x0, y1 - y0
	ramp = numpy.linspace(205, 168, h)[:, None] * numpy.ones((1, w))
	brushed = ramp + noise(w, 1, 6.0, 1) + noise(w, h, 2.5, 2)
	front = numpy.stack([brushed, brushed + 2, brushed + 6], axis=2)
	img.paste(to_image(front), (x0, y0))
	# a dark grille behind each speaker, wider than it. (A unit of the box is more pixels across the picture than
	# down it: a circle on the box is an ellipse here.)
	sx = (x1 - x0) / (BODY_HI[1] - BODY_LO[1])
	sy = (y1 - y0) / (BODY_HI[2] - BODY_LO[2])
	for side in (-1, 1):
		cx, cy = front_place(side * CONE_Y, CONE_Z)
		rx, ry = (CONE_R + 0.6) * sx, (CONE_R + 0.6) * sy
		draw.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=(28, 29, 32))
		for gy in range(int(cy - ry), int(cy + ry), 4):
			for gx in range(int(cx - rx), int(cx + rx), 4):
				if ((gx - cx) / (rx - 2)) ** 2 + ((gy - cy) / (ry - 2)) ** 2 < 1.0:
					draw.ellipse((gx, gy, gx + 1.6, gy + 1.6), fill=(64, 66, 72))
		draw.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), outline=(120, 122, 128), width=2)
	# a line of small lights over the door of the cassette: a level meter, green to red
	for k in range(7):
		lx, ly = front_place(-3.0 + k * 1.0, 13.6)
		color = (90, 230, 120) if k < 4 else (240, 200, 70) if k < 6 else (250, 80, 60)
		draw.ellipse((lx - 2.5, ly - 2.5, lx + 2.5, ly + 2.5), fill=tuple(c // 2 for c in color))
		glow.ellipse((lx - 2.5, ly - 2.5, lx + 2.5, ly + 2.5), fill=color)
	# screws in the corners
	for y, z in ((-15.0, 18.0), (15.0, 18.0), (-15.0, 2.0), (15.0, 2.0)):
		sx, sy = front_place(y, z)
		draw.ellipse((sx - 3, sy - 3, sx + 3, sy + 3), fill=(120, 122, 126), outline=(70, 72, 76))
		draw.line((sx - 2, sy, sx + 2, sy), fill=(60, 62, 66), width=1)
	# a power light at the lower left
	px, py = front_place(-14.6, 2.6)
	draw.ellipse((px - 2.5, py - 2.5, px + 2.5, py + 2.5), fill=(120, 30, 20))
	glow.ellipse((px - 2.5, py - 2.5, px + 2.5, py + 2.5), fill=(255, 70, 40))

	# the plastic: black-grey with a fine grain
	x0, y0, x1, y1 = R_PLASTIC
	w, h = x1 - x0, y1 - y0
	grain = 44 + noise(w, h, 4.0, 3)
	img.paste(to_image(numpy.stack([grain, grain + 1, grain + 4], axis=2)), (x0, y0))

	# the door of the cassette: a smoked window over a cassette with two reels
	x0, y0, x1, y1 = R_DOOR
	draw.rectangle((x0, y0, x1 - 1, y1 - 1), fill=(150, 152, 158))
	draw.rectangle((x0 + 6, y0 + 6, x1 - 7, y1 - 7), fill=(30, 32, 38))
	draw.rounded_rectangle((x0 + 26, y0 + 22, x1 - 27, y1 - 24), radius=6, fill=(196, 188, 170))
	draw.rectangle((x0 + 40, y0 + 30, x1 - 41, y0 + 54), fill=(228, 120, 60))
	draw.rounded_rectangle((x0 + 60, y0 + 62, x1 - 61, y0 + 96), radius=14, fill=(36, 34, 32))
	for cx in (x0 + 86, x1 - 87):
		cy = y0 + 79
		draw.ellipse((cx - 13, cy - 13, cx + 13, cy + 13), fill=(220, 220, 220))
		draw.ellipse((cx - 6, cy - 6, cx + 6, cy + 6), fill=(36, 34, 32))
		for k in range(6):
			a = k * math.pi / 3
			draw.line((cx, cy, cx + 10 * math.cos(a), cy + 10 * math.sin(a)), fill=(150, 150, 150), width=2)
	# the shine on the smoked glass
	shine = Image.new("L", (x1 - x0, y1 - y0), 0)
	sd = ImageDraw.Draw(shine)
	sd.polygon([(30, 6), (90, 6), (40, y1 - y0 - 7), (-20, y1 - y0 - 7)], fill=60)
	shine = shine.filter(ImageFilter.GaussianBlur(4))
	img.paste((255, 255, 255), (x0, y0), shine)

	# the dial: black, with marks, a scale of light and a needle
	x0, y0, x1, y1 = R_DIAL
	draw.rectangle((x0, y0, x1 - 1, y1 - 1), fill=(18, 18, 22))
	draw.rectangle((x0 + 2, y0 + 2, x1 - 3, y1 - 3), outline=(110, 112, 118))
	for k in range(41):
		mx = x0 + 12 + k * (x1 - x0 - 24) / 40.0
		tall = 10 if k % 5 == 0 else 5
		draw.line((mx, y0 + 18, mx, y0 + 18 + tall), fill=(110, 140, 150), width=1)
		glow.line((mx, y0 + 18, mx, y0 + 18 + tall), fill=(40, 120, 150), width=1)
	for k in range(4):
		bx = x0 + 24 + k * 56
		draw.rectangle((bx, y0 + 40, bx + 30, y0 + 44), fill=(60, 80, 90))
		glow.rectangle((bx, y0 + 40, bx + 30, y0 + 44), fill=(30, 90, 110))
	nx = x0 + (x1 - x0) * 0.62
	draw.line((nx, y0 + 8, nx, y1 - 8), fill=(255, 120, 40), width=3)
	glow.line((nx, y0 + 8, nx, y1 - 8), fill=(255, 110, 30), width=3)

	# the keys: silver, each with its sign
	x0, y0, x1, y1 = R_KEYS
	kw = (x1 - x0) / 6.0
	for k in range(6):
		kx = x0 + k * kw
		draw.rectangle((kx, y0, kx + kw - 1, y1 - 1), fill=(186, 188, 194))
		draw.rectangle((kx, y0, kx + kw - 1, y0 + 6), fill=(214, 216, 222))
		cx, cy = kx + kw / 2.0, y0 + (y1 - y0) / 2.0 + 4
		dark = (50, 52, 58)
		if k == 0:		# record
			draw.ellipse((cx - 6, cy - 6, cx + 6, cy + 6), fill=(200, 40, 30))
		elif k == 1:	# play
			draw.polygon([(cx - 6, cy - 8), (cx - 6, cy + 8), (cx + 8, cy)], fill=dark)
		elif k == 2:	# back
			draw.polygon([(cx + 8, cy - 7), (cx + 8, cy + 7), (cx, cy)], fill=dark)
			draw.polygon([(cx, cy - 7), (cx, cy + 7), (cx - 8, cy)], fill=dark)
		elif k == 3:	# forward
			draw.polygon([(cx - 8, cy - 7), (cx - 8, cy + 7), (cx, cy)], fill=dark)
			draw.polygon([(cx, cy - 7), (cx, cy + 7), (cx + 8, cy)], fill=dark)
		elif k == 4:	# stop
			draw.rectangle((cx - 6, cy - 6, cx + 6, cy + 6), fill=dark)
		else:			# pause
			draw.rectangle((cx - 6, cy - 7, cx - 2, cy + 7), fill=dark)
			draw.rectangle((cx + 2, cy - 7, cx + 6, cy + 7), fill=dark)
	return img, lights


def paint_env():
	"""what chrome reflects: a bright sky over a horizon, a dark floor"""
	size = 128
	v = numpy.linspace(0.0, 1.0, size)[:, None] * numpy.ones((1, size))
	sky = 235 - 70 * v
	floor = 60 + 30 * (1.0 - v)
	value = numpy.where(v < 0.5, sky, floor)
	band = numpy.exp(-((v - 0.5) / 0.04) ** 2) * 60
	value = value + band
	rgb = numpy.stack([value * 0.96, value * 0.98, value * 1.04], axis=2)
	return to_image(rgb)


def paint_cone():
	size = 256
	c = (size - 1) / 2.0
	yy, xx = numpy.mgrid[0:size, 0:size]
	r = numpy.hypot(xx - c, yy - c) / c * CONE_R		# in units of the model
	value = numpy.full((size, size), 40.0)
	# the chrome ring
	ring = (r > 5.72) & (r <= 6.0)
	value[ring] = 190
	# the rubber
	rubber = (r > 4.45) & (r <= 5.72)
	value[rubber] = 30 + 10 * numpy.cos((r[rubber] - 4.45) / 1.27 * math.pi * 2)
	# the cone, with rings in it
	paper = (r > 1.9) & (r <= 4.45)
	value[paper] = 52 + 8 * numpy.sin(r[paper] * 9.0)
	# the cap, with a light on it
	cap = r <= 1.9
	shine = numpy.exp(-(((xx - c * 0.88) ** 2 + (yy - c * 0.88) ** 2) / (2 * (c * 0.12) ** 2)))
	value[cap] = 34 + 90 * shine[cap]
	value += noise(size, size, 2.0, 4)
	rgb = numpy.stack([value, value, value * 1.06], axis=2)
	return to_image(rgb)


def main():
	out = sys.argv[1] if len(sys.argv) > 1 else OUT
	os.makedirs(out, exist_ok=True)
	n_box = write_md3(os.path.join(out, "boombox.md3"), "boombox", build_box())
	n_cone = write_md3(os.path.join(out, "boombox_cone.md3"), "boombox_cone", build_cone())
	atlas, lights = paint_atlas()
	atlas.save(os.path.join(out, "boombox.png"), optimize=True)
	lights.save(os.path.join(out, "boombox_lights.png"), optimize=True)
	paint_env().save(os.path.join(out, "boombox_env.png"), optimize=True)
	paint_cone().save(os.path.join(out, "boombox_cone.png"), optimize=True)
	print("boombox.md3: %i vertices, %i triangles; boombox_cone.md3: %i vertices, %i triangles" % (n_box + n_cone))


if __name__ == "__main__":
	main()
