#!/usr/bin/env python3
"""Makes the RC car of issue #9 (cgame/toys/cg_rccar.c) from vehicle-monster-truck of Kenney's Toy Car Kit 1.0 (CC0,
www.kenney.nl, also https://opengameart.org/content/toy-car-kit):

  models/vision/toys/rccar/body.md3    the body: paint, trim and glass. Its origin lies in the middle between the
                                       four axles, forward along +x, up along +z. tag_wheel_fl, _fr, _bl and _br at
                                       the axles, tag_head and tag_tail where the lamps shine from.
  models/vision/toys/rccar/wheel.md3   one wheel, its axle at the origin along y, the hub cap towards +y (a left
                                       wheel; the cgame turns it round for the right side)
  models/vision/toys/rccar/*.png       the pictures of the materials and their normal maps (the shader keywords
                                       normalMap and specular, shaders/vision_toys.shader), the same blurred by the
                                       turn of a fast wheel (wheel_blur.skin), and the marks the tires leave: the
                                       tread pressed into soft ground (track) and rubber on hard ground (skid)

The kit has y up, the front towards -z and the left towards -x, and all its colours in one palette picture: which
cell of the palette a face takes says what it is made of. Each material gets coordinates and pictures of its own
here: the paint and the plastic boxed onto the faces, the tread around the tire, the rim flat across the wheel.
usage:
  tools/toys/rccar.py <Models/OBJ format/vehicle-monster-truck.obj of the kit>
"""
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "boombox"))
sys.path.insert(0, HERE)
from build import Surface, norm, write_md3  # noqa: E402
from watergun import read_obj  # noqa: E402

OUT = os.path.join(HERE, "..", "..", "assets", "japlus_client", "models", "vision", "toys", "rccar")
SHADERS = "models/vision/toys/rccar/"
LENGTH = 26.0				# units from the front of the front wheels to the back of the rear ones
SHARP = math.cos(math.radians(35.0))	# faces that meet at more than this angle get a hard edge
PAINT_TILE = 14.0			# units the paint picture covers
TREAD_BLOCKS = 18			# knobs of the tread around the tire


def sub(a, b):
	return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
	return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def material(group, cell):
	"""what a face is made of, by the palette cell (column, row of eight) it takes"""
	if group != "body":
		return "tire" if cell == (2, 6) else "rim"
	if cell[0] == 7:
		return "paint"
	if cell[0] in (4, 6):
		return "glass"
	return "trim"


def box_uv(p, n, tile):
	"""the picture laid onto a face from the side it faces most"""
	a = [abs(c) for c in n]
	if a[2] >= a[0] and a[2] >= a[1]:
		return (p[0] / tile, p[1] / tile)
	if a[0] >= a[1]:
		return (p[1] / tile, -p[2] / tile)
	return (p[0] / tile, -p[2] / tile)


def wheel_uvs(points, n, radius, width, hub):
	"""Tire: the tread around it (the upper three quarters of the picture), the side walls by angle and radius (the
	lowest quarter). Rim: flat across the axle (along y)."""
	if not hub:
		return [(0.5 + p[0] / (2.0 * radius), 0.5 - p[2] / (2.0 * radius)) for p in points]
	angles = [math.atan2(p[2], p[0]) / (2.0 * math.pi) for p in points]
	# a face across the seam of the angle keeps its corners together
	if max(angles) - min(angles) > 0.5:
		angles = [a + 1.0 if a < 0.0 else a for a in angles]
	if abs(n[1]) > 0.7:
		return [(a, 0.75 + 0.25 * min(1.0, max(0.0, (radius - math.hypot(p[0], p[2])) / (radius * 0.45))))
			for a, p in zip(angles, points)]
	return [(a, 0.75 * min(1.0, max(0.0, 0.5 - p[1] / width))) for a, p in zip(angles, points)]


def build(positions, coords, tris, groups):
	cells = []
	for (a, ta), (b, tb), (c, tc) in tris:
		u = (coords[ta][0] + coords[tb][0] + coords[tc][0]) / 3.0
		v = (coords[ta][1] + coords[tb][1] + coords[tc][1]) / 3.0
		cells.append((int(u * 8), int((1.0 - v) * 8)))

	# the wheels: the middle of each, and the size of a wheel
	centres = {}
	for g in ("wheel-fl", "wheel-fr", "wheel-bl", "wheel-br"):
		vs = {i for t, gt in zip(tris, groups) if gt == g for i, _ in t}
		centres[g] = tuple((min(positions[i][k] for i in vs) + max(positions[i][k] for i in vs)) / 2.0 for k in range(3))
	axle = tuple(sum(c[k] for c in centres.values()) / 4.0 for k in range(3))
	front = min(p[2] for p in positions)
	back = max(p[2] for p in positions)
	scale = LENGTH / (back - front)

	def to_game(p, origin):
		# forward is -z of the kit, left is -x, up is y
		d = sub(p, origin)
		return (-d[2] * scale, -d[0] * scale, d[1] * scale)

	fl = centres["wheel-fl"]
	wheel_verts = {i for t, g in zip(tris, groups) if g == "wheel-fl" for i, _ in t}
	radius = max(math.hypot(positions[i][1] - fl[1], positions[i][2] - fl[2]) for i in wheel_verts) * scale
	width = (max(positions[i][0] for i in wheel_verts) - min(positions[i][0] for i in wheel_verts)) * scale

	# the faces of the body and of the front left wheel, in game units around their origins
	parts = {"body": ([], axle), "wheel": ([], fl)}
	for t, g, cell in zip(tris, groups, cells):
		if g == "body":
			parts["body"][0].append((t, material(g, cell)))
		elif g == "wheel-fl":
			parts["wheel"][0].append((t, material(g, cell)))

	models = {}
	for part, (faces, origin) in parts.items():
		corners = [[to_game(positions[i], origin) for i, _ in t] for t, _ in faces]
		# (the kit has a few triangles without area)
		keep = [f for f, (a, b, c) in enumerate(corners) if dot(cross(sub(b, a), sub(c, a)), cross(sub(b, a), sub(c, a))) > 1e-10]
		faces = [faces[f] for f in keep]
		corners = [corners[f] for f in keep]
		normals = [norm(cross(sub(b, a), sub(c, a))) for a, b, c in corners]
		by_position = {}
		for f, (t, m) in enumerate(faces):
			for i, _ in t:
				by_position.setdefault((i, m), []).append(f)
		surfaces = {}
		keys = {}
		for f, (t, m) in enumerate(faces):
			s = surfaces.setdefault(m, Surface(m, SHADERS + m))
			n = normals[f]
			if m in ("tire", "rim"):
				uvs = wheel_uvs(corners[f], n, radius, width, m == "tire")
			else:
				uvs = [box_uv(p, n, PAINT_TILE) for p in corners[f]]
			ids = []
			for (i, _), p, uv in zip(t, corners[f], uvs):
				near = [g for g in by_position[(i, m)] if dot(n, normals[g]) > SHARP]
				smooth = norm(tuple(sum(normals[g][k] for g in near) for k in range(3)))
				key = (m, i, round(uv[0], 5), round(uv[1], 5), min(near))
				if key not in keys:
					keys[key] = len(s.verts)
					s.verts.append((p, smooth, uv))
				ids.append(keys[key])
			# the kit turns its faces the other way round from the renderers
			s.tris.append((ids[0], ids[2], ids[1]))
		models[part] = list(surfaces.values())

	tags = [("tag_wheel_" + g[-2:], to_game(c, axle)) for g, c in centres.items()]
	body_points = [v[0] for s in models["body"] for v in s.verts]
	top = max(p[2] for p in body_points)
	bottom = min(p[2] for p in body_points)
	lamp_z = bottom + (top - bottom) * 0.55
	tags.append(("tag_head", (max(p[0] for p in body_points), 0.0, lamp_z)))
	tags.append(("tag_tail", (min(p[0] for p in body_points), 0.0, lamp_z)))
	return models, tags, radius, width


# ---------------------------------------------------------------------------------------------------------------
# pictures
# ---------------------------------------------------------------------------------------------------------------

def periodic_noise(h, w, falloff, seed, low=1.0):
	"""noise that tiles, -1 to 1: random phases with an amplitude of 1 / frequency ^ falloff"""
	rng = np.random.default_rng(seed)
	fy = np.fft.fftfreq(h)[:, None] * h
	fx = np.fft.rfftfreq(w)[None, :] * w
	f = np.sqrt(fx * fx + fy * fy)
	amp = np.where(f >= low, 1.0 / np.maximum(f, 1.0) ** falloff, 0.0)
	spectrum = amp * np.exp(2j * np.pi * rng.random(amp.shape))
	n = np.fft.irfft2(spectrum, s=(h, w))
	return n / max(np.abs(n).max(), 1e-9)


def normal_map(height, strength):
	"""normals of a height field that tiles: x along the picture, y down it, z out of the surface"""
	dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * strength
	dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * strength
	n = np.stack([-dx, -dy, np.ones_like(height)], axis=-1)
	n /= np.linalg.norm(n, axis=-1, keepdims=True)
	return Image.fromarray(np.uint8(np.clip(n * 0.5 + 0.5, 0.0, 1.0) * 255.0 + 0.5), "RGB")


def to_image(rgb):
	return Image.fromarray(np.uint8(np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5), "RGB")


def blur(a, radius):
	"""a box blur that wraps, twice"""
	for _ in range(2):
		for axis in (0, 1):
			a = sum(np.roll(a, k, axis=axis) for k in range(-radius, radius + 1)) / (2 * radius + 1)
	return a


def paint_paint(size=512):
	"""Candy paint: a deep purple with fine metal flakes that catch the light, and the faint orange peel of sprayed
	paint in the normals"""
	base = np.array([0.36, 0.18, 0.78])
	flakes = periodic_noise(size, size, 0.2, 11, low=60.0)
	tone = periodic_noise(size, size, 1.6, 12) * 0.05
	rgb = base[None, None, :] * (1.0 + tone[..., None] + np.maximum(flakes, 0.0)[..., None] * 0.06)
	peel = periodic_noise(size, size, 1.2, 13, low=24.0)
	return to_image(rgb), normal_map(peel + flakes * 0.02, 0.35)


def paint_trim(size=256):
	"""satin blue grey plastic of the chassis and the cab, a little grainy"""
	grain = periodic_noise(size, size, 0.6, 21, low=20.0)
	tone = periodic_noise(size, size, 1.4, 22) * 0.04
	rgb = np.array([0.25, 0.27, 0.32])[None, None, :] * (1.0 + tone[..., None] + grain[..., None] * 0.05)
	return to_image(rgb), normal_map(grain, 0.5)


def paint_tire(w=1024, h=256):
	"""Rubber: blocky knobs of a monster truck tire in two staggered rows across the tread, worn a little lighter on
	top; the side wall below with a raised ring and fine ribs"""
	v, u = np.mgrid[0:h, 0:w].astype(np.float64)
	tread_h = int(h * 0.75)
	height = np.zeros((h, w))
	# tread: across (v from 0 to tread_h) two rows of knobs, the second half a knob along
	across = v / tread_h
	pitch = w / TREAD_BLOCKS
	for row, (lo, hi, shift) in enumerate(((0.06, 0.47, 0.0), (0.53, 0.94, 0.5))):
		along = ((u / pitch + shift) % 1.0)
		# the knobs lean like a chevron, the outer ones a little
		lean = (across - 0.5) * (0.35 if row == 0 else -0.35)
		along = (along + lean) % 1.0
		knob = (along > 0.12) & (along < 0.72) & (across > lo) & (across < hi)
		height = np.maximum(height, knob.astype(np.float64))
	height[tread_h:, :] = 0.0
	height = blur(height, 2)
	# side wall: a raised ring and ribs around
	wall = (v >= tread_h)
	radial = (v - tread_h) / (h - tread_h)
	ring = np.exp(-((radial - 0.35) / 0.08) ** 2)
	ribs = 0.25 * (0.5 + 0.5 * np.cos(u / w * 2.0 * np.pi * 72.0)) * (radial > 0.55)
	height = np.where(wall, ring * 0.6 + ribs, height)
	grain = periodic_noise(h, w, 0.5, 31, low=40.0)
	height = height + grain * 0.04
	rubber = 0.075 + 0.05 * np.clip(height, 0.0, 1.0) + grain * 0.01
	rgb = np.stack([rubber, rubber, rubber * 1.05], axis=-1)
	return to_image(rgb), normal_map(height, 7.0)


def paint_glass(size=64):
	"""the windows: dark tinted glass, the shine does the rest"""
	return to_image(np.full((size, size, 3), 1.0) * np.array([0.05, 0.07, 0.09])), None


def paint_lamp(size=128):
	"""the glow of a lamp as a sprite: a hot core in a soft halo, black around (added to the picture)"""
	y, x = np.mgrid[0:size, 0:size].astype(np.float64)
	r = np.hypot(x - (size - 1) / 2, y - (size - 1) / 2) / (size / 2)
	glow = np.exp(-(r / 0.12) ** 2) + 0.35 * np.exp(-(r / 0.45) ** 2) * np.clip(1.0 - r, 0.0, 1.0)
	return to_image(np.repeat(np.clip(glow, 0.0, 1.0)[..., None], 3, axis=-1)), None


def paint_rim(size=512):
	"""Satin anodized metal of the rims: gold, brushed in circles round the axle"""
	y, x = np.mgrid[0:size, 0:size].astype(np.float64)
	r = np.hypot(x - size / 2, y - size / 2)
	angle = np.arctan2(y - size / 2, x - size / 2)
	rng = np.random.default_rng(41)
	lines = rng.random(size * 2)
	brushed = lines[np.clip(r * 2.0, 0, size * 2 - 1).astype(int)] - 0.5
	rgb = np.array([0.82, 0.62, 0.22])[None, None, :] * (1.0 + brushed[..., None] * 0.12)
	# six spokes darker between them, a ring at the rim
	spokes = 0.5 + 0.5 * np.cos(angle * 6.0)
	shade = 1.0 - 0.25 * (1.0 - spokes) * ((r > size * 0.18) & (r < size * 0.4))
	rgb *= shade[..., None]
	height = brushed * 0.3 + np.exp(-((r - size * 0.44) / (size * 0.02)) ** 2)
	return to_image(rgb), normal_map(height, 2.0)


def blur_along(img, radius):
	"""an RGB picture blurred along its rows, wrapping (a fast tread turning)"""
	a = np.asarray(img, dtype=np.float64) / 255.0
	a = sum(np.roll(a, k, axis=1) for k in range(-radius, radius + 1)) / (2 * radius + 1)
	return a


def blur_round(img, steps=48):
	"""an RGB picture blurred round its middle (a fast rim turning): the mean of it turned through a sixth"""
	out = np.zeros(np.asarray(img).shape)
	for k in range(steps):
		out += np.asarray(img.rotate(60.0 * k / steps, resample=Image.BILINEAR), dtype=np.float64) / 255.0
	return out / steps


def flat_normals(normals, radius=None):
	"""normal maps of the blurred pictures: the bumps smeared the same way, flatter"""
	a = np.asarray(normals, dtype=np.float64) / 255.0 * 2.0 - 1.0
	if radius:
		a = sum(np.roll(a, k, axis=1) for k in range(-radius, radius + 1)) / (2 * radius + 1)
	else:
		a[..., 0:2] *= 0.0
	a /= np.linalg.norm(a, axis=-1, keepdims=True)
	return Image.fromarray(np.uint8(np.clip(a * 0.5 + 0.5, 0.0, 1.0) * 255.0 + 0.5), "RGB")


def paint_track(w=64, h=256):
	"""The tread pressed into soft ground, as darkness (white: dark): two rows of knobs across the width of a tire,
	the rows half a knob apart, softly edged, repeating down the picture"""
	v, u = np.mgrid[0:h, 0:w].astype(np.float64)
	across = u / w
	pitch = h / 8.0
	dark = np.zeros((h, w))
	for lo, hi, shift in ((0.08, 0.46, 0.0), (0.54, 0.92, 0.5)):
		along = (v / pitch + shift) % 1.0
		dark = np.maximum(dark, ((along > 0.1) & (along < 0.7) & (across > lo) & (across < hi)).astype(np.float64))
	dark = blur(dark, 2) * 0.8 + 0.2 * np.exp(-((across - 0.5) / 0.42) ** 8)
	dark *= 1.0 - np.exp(-((np.minimum(across, 1.0 - across)) / 0.06) ** 2)
	return to_image(np.repeat(dark[..., None], 3, axis=-1))


def paint_smoke(size=128):
	"""A puff of tire smoke as a sprite: soft billows of noise in a round fall off, white (the effect tints it), the
	amount of smoke in its alpha"""
	y, x = np.mgrid[0:size, 0:size].astype(np.float64)
	r = np.hypot(x - (size - 1) / 2, y - (size - 1) / 2) / (size / 2)
	billows = periodic_noise(size, size, 1.3, 61, low=2.0) * 0.5 + 0.5
	alpha = np.clip(1.0 - r, 0.0, 1.0) ** 1.5 * (0.45 + 0.55 * billows)
	rgba = np.dstack([np.full((size, size), 1.0)] * 3 + [np.clip(alpha, 0.0, 1.0)])
	return Image.fromarray(np.uint8(rgba * 255.0 + 0.5), "RGBA")


def paint_skid(w=64, h=256):
	"""Rubber left on hard ground, as darkness: a band the width of the tire, darker along its middle, in streaks"""
	v, u = np.mgrid[0:h, 0:w].astype(np.float64)
	across = u / w
	streaks = periodic_noise(h, w, 0.8, 51, low=4.0)
	streaks = blur_along(to_image(np.repeat(streaks[..., None] * 0.5 + 0.5, 3, axis=-1)).transpose(Image.TRANSPOSE), 12)
	streaks = np.transpose(streaks[..., 0])
	band = np.exp(-((across - 0.5) / 0.38) ** 6)
	dark = band * (0.55 + 0.45 * streaks)
	return to_image(np.repeat(np.clip(dark, 0.0, 1.0)[..., None], 3, axis=-1))


def main():
	if len(sys.argv) != 2:
		sys.exit(__doc__)
	obj = sys.argv[1]
	groups = []
	positions, coords, tris = read_obj(obj, groups)
	models, tags, radius, width = build(positions, coords, tris, groups)

	os.makedirs(OUT, exist_ok=True)
	nv, nt = write_md3(os.path.join(OUT, "body.md3"), SHADERS + "body", models["body"], tags)
	print("body: %d vertices, %d triangles in %s" % (nv, nt, ", ".join(s.name for s in models["body"])))
	nv, nt = write_md3(os.path.join(OUT, "wheel.md3"), SHADERS + "wheel", models["wheel"])
	print("wheel: %d vertices, %d triangles, radius %.2f, width %.2f" % (nv, nt, radius, width))
	for name, origin in tags:
		print("%s %.2f %.2f %.2f" % (name, *origin))

	for name, (albedo, normals) in (("paint", paint_paint()), ("trim", paint_trim()), ("tire", paint_tire()),
		("rim", paint_rim()), ("glass", paint_glass()),
		("lamp", paint_lamp())):
		albedo.save(os.path.join(OUT, name + ".png"), optimize=True)
		if normals:
			normals.save(os.path.join(OUT, name + "_n.png"), optimize=True)

	# a fast wheel: the tread smeared along, the rim round its middle
	tire, tire_n = Image.open(os.path.join(OUT, "tire.png")), Image.open(os.path.join(OUT, "tire_n.png"))
	to_image(blur_along(tire, 40)).save(os.path.join(OUT, "tire_blur.png"), optimize=True)
	flat_normals(tire_n, 40).save(os.path.join(OUT, "tire_blur_n.png"), optimize=True)
	rim = Image.open(os.path.join(OUT, "rim.png"))
	to_image(blur_round(rim)).save(os.path.join(OUT, "rim_blur.png"), optimize=True)
	with open(os.path.join(OUT, "wheel_blur.skin"), "w") as f:
		f.write("tire,%stire_blur\nrim,%srim_blur\n" % (SHADERS, SHADERS))
	paint_track().save(os.path.join(OUT, "track.png"), optimize=True)
	paint_skid().save(os.path.join(OUT, "skid.png"), optimize=True)
	paint_smoke().save(os.path.join(OUT, "smoke.png"), optimize=True)


if __name__ == "__main__":
	main()
