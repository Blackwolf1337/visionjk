#!/usr/bin/env python3
"""Writes the models of the sound system and their pictures: tools/boombox/build_system.py [folder]

The sound system of the boombox (codemp/game/boombox/bg_boombox.h, BOX_KIND_SYSTEM): a DJ table with two
turntables and a mixer, and a big speaker of which the game puts two beside it. Made like the boombox
(build.py, whose geometry, MD3 writer and cone this script takes): from boxes and cylinders, without a modelling
program. Three models:

  sound_desk.md3     the table. Its size is SYS_DESK_HALF_DEPTH, SYS_DESK_HALF_WIDTH and SYS_DESK_HEIGHT of
                     bg_boombox.h. Its front, which faces the people, has lights that glow.
  sound_platter.md3  one record on its platter, which the cgame draws twice on the table and turns with the song
                     (codemp/cgame/boombox/cg_boombox.c: SYS_PLATTER_X, SYS_PLATTER_Y and SYS_PLATTER_Z are
                     PLATTER_X, PLATTER_Y and PLATTER_Z here, change them in both places).
  sound_speaker.md3  the cabinet of a speaker (SYS_SPK_HALF_DEPTH, SYS_SPK_HALF_WIDTH, SYS_SPK_HEIGHT). Its two
                     cones are the cone of the boombox, drawn larger by the cgame and pushed out with the song: the
                     big one at SYS_SPK_MOUTH, the small one at SYS_TWEETER_Z, both at SPK_FRONT (SYS_CONE_X).
  sound_wave.md3     the shockwave of a beat (cg_boombox.c, Box_AddWaves): a disc of WAVE_R across the plane YZ, its
                     texture coordinates the square of the screen the renderer copies behind it (RF_DISTORTION), so
                     what it shows is what is behind it. Its WAVE_FRAMES frames are a ring that runs out from the
                     middle and gets weaker: there the vertices are pushed out, and the picture under them with
                     them. Shader sound_wave: the copy of the screen and nothing else.

Their pictures are painted here: sound_desk.png and sound_desk_lights.png (black but for what glows),
sound_platter.png, sound_speaker.png and sound_speaker_lights.png. The shader scripts are in
assets/japlus_client/shaders/vision.shader. The metal of the tone arms and knobs is the chrome of the boombox.

A model faces +x, stands on z 0, its middle at x 0, y 0. Without an argument everything goes to
assets/japlus_client/models/map_objects/vision.
"""
import math
import os
import sys

import numpy
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build import (OUT, SHADER_DIR, Surface, plain_box, cylinder, env_uv, write_md3, noise, to_image,  # noqa: E402
	outside, encode_normal)
import struct  # noqa: E402

# the table (bg_boombox.h: SYS_DESK_HALF_DEPTH 13, SYS_DESK_HALF_WIDTH 30, SYS_DESK_HEIGHT 40)
DESK_LO = (-13.0, -30.0, 0.0)
DESK_HI = (13.0, 30.0, 38.0)
TOP_LO = (-13.5, -30.5, 38.0)
TOP_HI = (13.5, 30.5, 40.0)
# the platters (cg_boombox.c: SYS_PLATTER_X, SYS_PLATTER_Y, SYS_PLATTER_Z)
PLATTER_X = -1.0
PLATTER_Y = 17.0
PLATTER_Z = 40.0
PLATTER_R = 8.6
PLATTER_H = 0.8
# the mixer between them
MIXER_LO = (-9.0, -5.5, 40.0)
MIXER_HI = (7.0, 5.5, 41.4)

# a speaker (bg_boombox.h: SYS_SPK_HALF_DEPTH 11, SYS_SPK_HALF_WIDTH 13, SYS_SPK_HEIGHT 62, SYS_SPK_MOUTH 36)
SPK_LO = (-11.0, -13.0, 1.0)
SPK_HI = (11.0, 13.0, 62.0)
SPK_FRONT = 11.0
WOOFER_Z = 36.0
WOOFER_R = 10.0			# the cone of the boombox (radius 5) twice as large: SYS_WOOFER_SCALE
TWEETER_Z = 53.0
TWEETER_R = 2.75		# SYS_TWEETER_SCALE

# the shockwave (cg_boombox.c: BOX_WAVE_MODEL_R is WAVE_R, BOX_WAVE_FRAMES is WAVE_FRAMES). In units, so the 1/64
# of a unit an MD3 keeps of a place is fine enough; the cgame scales it to the size it wants.
WAVE_R = 32.0
WAVE_RINGS = 40
WAVE_SEGMENTS = 22
WAVE_FRAMES = 16

PX = 14					# pixels a unit on the faces that have a picture of their own

# Where the faces are in the pictures, in pixels: x0, y0, x1, y1
D_SIZE = (1024, 1024)
D_FRONT = (0, 0, 60 * PX, 38 * PX)					# the front of the table, 60 by 38 units
D_TOP = (0, 38 * PX, 61 * PX, 38 * PX + 27 * PX)	# its top, 61 by 27, the side of the people up in the picture
D_PLAIN = (864, 0, 1024, 160)						# every other face of it: black with a grain
D_MIXER = (864, 176, 1024, 336)						# the top of the mixer, 11 by 16 units
S_SIZE = (512, 1024)
S_FRONT = (0, 0, 26 * PX, 61 * PX)					# the front of a speaker, 26 by 61 units
S_PLAIN = (384, 0, 512, 128)


def uv_in(region, size, s, t):
	x0, y0, x1, y1 = region
	return ((x0 + s * (x1 - x0)) / size[0], (y0 + t * (y1 - y0)) / size[1])


def plain_uv(region, size):
	def uv(p, normal):
		# (a grain: where on it does not matter much, so long as it is not one pixel)
		return uv_in(region, size, ((p[0] + p[1]) * 0.03) % 1.0, (p[2] * 0.03) % 1.0)
	return uv


# ---------------------------------------------------------------------------------------------------------------
# models
# ---------------------------------------------------------------------------------------------------------------

def build_desk():
	body = Surface("desk", SHADER_DIR + "sound_desk")
	chrome = Surface("metal", SHADER_DIR + "boombox_chrome")
	plain = plain_uv(D_PLAIN, D_SIZE)

	def desk_uv(p, normal):
		if normal[0] > 0.5:
			return uv_in(D_FRONT, D_SIZE, (p[1] - DESK_LO[1]) / (DESK_HI[1] - DESK_LO[1]), (DESK_HI[2] - p[2]) / (DESK_HI[2] - DESK_LO[2]))
		return plain(p, normal)

	def top_uv(p, normal):
		if normal[2] > 0.5:
			return uv_in(D_TOP, D_SIZE, (p[1] - TOP_LO[1]) / (TOP_HI[1] - TOP_LO[1]), (TOP_HI[0] - p[0]) / (TOP_HI[0] - TOP_LO[0]))
		return plain(p, normal)

	def mixer_uv(p, normal):
		if normal[2] > 0.5:
			return uv_in(D_MIXER, D_SIZE, (p[0] - MIXER_LO[0]) / (MIXER_HI[0] - MIXER_LO[0]), (p[1] - MIXER_LO[1]) / (MIXER_HI[1] - MIXER_LO[1]))
		return plain(p, normal)

	plain_box(body, DESK_LO, DESK_HI, desk_uv)
	plain_box(body, TOP_LO, TOP_HI, top_uv)
	plain_box(body, MIXER_LO, MIXER_HI, mixer_uv)
	# the tone arms: a post behind each platter on its outer side, and an arm from it in over the record
	for side in (-1.0, 1.0):
		post_y = side * ( PLATTER_Y + 9.0 )
		inner_y = side * ( PLATTER_Y + 2.5 )
		cylinder(chrome, (PLATTER_X - 7.0, post_y, TOP_HI[2]), 2, 0.9, 2.6)
		plain_box(chrome, (PLATTER_X - 7.4, min(post_y, inner_y) - 0.3, TOP_HI[2] + 2.1),
			(PLATTER_X - 6.6, max(post_y, inner_y) + 0.3, TOP_HI[2] + 2.5), env_uv)
	# knobs on the mixer
	for k in range(3):
		for side in (-1.0, 1.0):
			cylinder(chrome, (-5.0 + k * 3.2, side * 3.0, MIXER_HI[2]), 2, 0.55, 0.7)
	# four feet
	for x in (DESK_LO[0] + 1.0, DESK_HI[0] - 3.0):
		for y in (DESK_LO[1] + 1.0, DESK_HI[1] - 3.0):
			plain_box(body, (x, y, -0.01), (x + 2.0, y + 2.0, 0.0), plain)
	return [body, chrome]


def build_platter():
	"""a record on its platter: a disc of PLATTER_R, PLATTER_H high, standing on z 0, and a spindle"""
	record = Surface("record", SHADER_DIR + "sound_platter")
	chrome = Surface("spindle", SHADER_DIR + "boombox_chrome")
	sides = 32
	ring0, ring1, top = [], [], []
	for k in range(sides + 1):
		a = 2.0 * math.pi * k / sides
		c, s = math.cos(a), math.sin(a)
		ring0.append(((PLATTER_R * c, PLATTER_R * s, 0.0), (c, s, 0.0), (0.98, 0.5)))
		ring1.append(((PLATTER_R * c, PLATTER_R * s, PLATTER_H), (c, s, 0.0), (0.98, 0.5)))
	record.smooth([ring0, ring1])
	for k in range(sides):
		a = 2.0 * math.pi * k / sides
		top.append((PLATTER_R * math.cos(a), PLATTER_R * math.sin(a), PLATTER_H))
	record.poly(top, (0.0, 0.0, 1.0), [(0.5 + 0.5 * p[0] / PLATTER_R, 0.5 - 0.5 * p[1] / PLATTER_R) for p in top])
	cylinder(chrome, (0.0, 0.0, PLATTER_H), 2, 0.25, 0.6)
	return [record, chrome]


def build_speaker():
	cab = Surface("cabinet", SHADER_DIR + "sound_speaker")
	plain = plain_uv(S_PLAIN, S_SIZE)

	def cab_uv(p, normal):
		if normal[0] > 0.5:
			return uv_in(S_FRONT, S_SIZE, (p[1] - SPK_LO[1]) / (SPK_HI[1] - SPK_LO[1]), (SPK_HI[2] - p[2]) / (SPK_HI[2] - SPK_LO[2]))
		return plain(p, normal)

	plain_box(cab, SPK_LO, SPK_HI, cab_uv)
	# a plinth it stands on, a little smaller
	plain_box(cab, (SPK_LO[0] + 1.0, SPK_LO[1] + 1.0, 0.0), (SPK_HI[0] - 1.0, SPK_HI[1] - 1.0, SPK_LO[2]), plain)
	return [cab]


def wave_push(r, p):
	"""where a point of the disc at radius r is in the frame p (0 to 1): the ring at rr, band half wide, pushes the
	points of the band outward on its outer side and inward on its inner one, less as it gets older. Never so much
	that a point passes the next one."""
	rr = WAVE_R * (0.10 + 0.82 * p)
	band = WAVE_R * (0.07 + 0.05 * p)
	push = 0.55 * band / math.pi * (1.0 - p) ** 0.6
	d = (r - rr) / band
	if abs(d) >= 1.0:
		return r
	return r + push * math.sin(math.pi * d)


def write_wave(path):
	"""The disc with its frames. write_md3() of build.py writes one frame, this one as many as WAVE_FRAMES."""
	surface = Surface("wave", SHADER_DIR + "sound_wave")
	rest = []
	for j in range(WAVE_RINGS + 1):
		r = WAVE_R * j / WAVE_RINGS
		for k in range(WAVE_SEGMENTS + 1):
			a = 2.0 * math.pi * k / WAVE_SEGMENTS
			p = (0.0, r * math.cos(a), r * math.sin(a))
			# the square of the screen: s to the right (+y, as the cgame turns it), t up (+z)
			uv = (0.5 + p[1] / (2.0 * WAVE_R), 0.5 + p[2] / (2.0 * WAVE_R))
			surface.verts.append((p, (1.0, 0.0, 0.0), uv))
			rest.append((r, a))
	width = WAVE_SEGMENTS + 1
	for j in range(WAVE_RINGS):
		for k in range(WAVE_SEGMENTS):
			a = j * width + k
			# turned the way of the game: clockwise seen from +x (the ring at r 0 has its triangles of no size)
			surface.tris.extend([(a, a + 1, a + width + 1), (a, a + width + 1, a + width)])
	surface.check()

	frames, blob_xyz = [], b""
	for f in range(WAVE_FRAMES):
		p = f / (WAVE_FRAMES - 1)
		points = []
		for r, a in rest:
			q = wave_push(r, p)
			points.append((0.0, q * math.cos(a), q * math.sin(a)))
		for pt in points:
			blob_xyz += struct.pack("<hhhH", *[int(round(c * 64.0)) for c in pt], encode_normal((1.0, 0.0, 0.0)))
		radius = max(math.hypot(pt[1], pt[2]) for pt in points)
		frames.append(struct.pack("<3f3f3ff16s", 0.0, -radius, -radius, 0.0, radius, radius, 0.0, 0.0, 0.0, radius,
			b"wave%d" % f))

	nv, nt = len(surface.verts), len(surface.tris)
	ofs_tris = 108 + 68
	ofs_st = ofs_tris + 12 * nt
	ofs_xyz = ofs_st + 8 * nv
	ofs_end = ofs_xyz + len(blob_xyz)
	surf = struct.pack("<4s64siiiiiiiiii", b"IDP3", b"wave", 0, WAVE_FRAMES, 1, nv, nt, ofs_tris, 108, ofs_st, ofs_xyz, ofs_end)
	surf += struct.pack("<64si", surface.shader.encode(), 0)
	for t in surface.tris:
		surf += struct.pack("<iii", *t)
	for v in surface.verts:
		surf += struct.pack("<ff", v[2][0], v[2][1])
	surf += blob_xyz
	assert len(surf) == ofs_end

	ofs_frames = 108
	ofs_tags = ofs_frames + 56 * WAVE_FRAMES
	data = struct.pack("<4si64siiiiiiiii", b"IDP3", 15, b"sound_wave", 0, WAVE_FRAMES, 0, 1, 0, ofs_frames, ofs_tags, ofs_tags,
		ofs_tags + len(surf))
	data += b"".join(frames) + surf
	with open(path, "wb") as fh:
		fh.write(data)
	return nv, nt


# ---------------------------------------------------------------------------------------------------------------
# pictures
# ---------------------------------------------------------------------------------------------------------------

def grain(region, value, seed):
	x0, y0, x1, y1 = region
	g = value + noise(x1 - x0, y1 - y0, 3.0, seed)
	return to_image(numpy.stack([g, g, g + 3], axis=2))


def paint_desk():
	img = Image.new("RGB", D_SIZE, (22, 22, 26))
	lights = Image.new("RGB", D_SIZE, (0, 0, 0))
	draw, glow = ImageDraw.Draw(img), ImageDraw.Draw(lights)
	img.paste(grain(D_PLAIN, 20, 11), D_PLAIN[:2])

	# the front: black with a frame of light along its top and a wall of bars that look like an equalizer
	x0, y0, x1, y1 = D_FRONT
	img.paste(grain(D_FRONT, 18, 12), (x0, y0))
	draw.rectangle((x0, y0, x1 - 1, y0 + 2 * PX), fill=(40, 40, 46))
	draw.rectangle((x0, y0 + int(2.2 * PX), x1 - 1, y0 + int(2.7 * PX)), fill=(30, 70, 110))
	glow.rectangle((x0, y0 + int(2.2 * PX), x1 - 1, y0 + int(2.7 * PX)), fill=(60, 170, 255))
	bars, rng = 28, numpy.random.default_rng(13)
	width = (x1 - x0 - 8 * PX) / bars
	for k in range(bars):
		bx = x0 + 4 * PX + k * width
		height = 0.25 + 0.75 * abs(math.sin(k * 0.7)) * (0.6 + 0.4 * rng.random())
		top = y1 - 5 * PX - height * (y1 - y0 - 12 * PX)
		for seg in numpy.arange(y1 - 5 * PX, top, -0.9 * PX):
			frac = (y1 - 5 * PX - seg) / (y1 - y0 - 12 * PX)
			color = (60, 220, 255) if frac < 0.55 else (170, 90, 255) if frac < 0.8 else (255, 70, 160)
			box = (bx + 0.12 * width, seg - 0.7 * PX, bx + 0.88 * width, seg)
			draw.rectangle(box, fill=tuple(c // 4 for c in color))
			glow.rectangle(box, fill=color)
	draw.rectangle((x0, y1 - 2 * PX, x1 - 1, y1 - 1), fill=(12, 12, 14))

	# the top: two turntables on their plates, the side of the people up in the picture
	x0, y0, x1, y1 = D_TOP
	img.paste(grain(D_TOP, 34, 14), (x0, y0))

	def top_at(x, y):
		return (x0 + (y - TOP_LO[1]) * PX, y0 + (TOP_HI[0] - x) * PX)

	for side in (-1.0, 1.0):
		cx, cy = top_at(PLATTER_X, side * PLATTER_Y)
		draw.rounded_rectangle((cx - 11.5 * PX, cy - 11.5 * PX, cx + 11.5 * PX, cy + 11.5 * PX), radius=PX, fill=(52, 52, 58),
			outline=(80, 80, 88), width=2)
		draw.ellipse((cx - 9.2 * PX, cy - 9.2 * PX, cx + 9.2 * PX, cy + 9.2 * PX), fill=(20, 20, 22))
		# the speed and start buttons, the start light
		bx, by = top_at(PLATTER_X + 9.4, side * PLATTER_Y - 9.0)
		draw.rectangle((bx - 1.3 * PX, by - 0.6 * PX, bx + 1.3 * PX, by + 0.6 * PX), fill=(150, 150, 156))
		lx, ly = top_at(PLATTER_X + 9.4, side * PLATTER_Y - 5.8)
		draw.ellipse((lx - 0.35 * PX, ly - 0.35 * PX, lx + 0.35 * PX, ly + 0.35 * PX), fill=(80, 30, 20))
		glow.ellipse((lx - 0.35 * PX, ly - 0.35 * PX, lx + 0.35 * PX, ly + 0.35 * PX), fill=(255, 90, 40))
		# the pitch slider
		sx, sy = top_at(PLATTER_X - 3.0, side * PLATTER_Y + side * 10.4)
		draw.rectangle((sx - 0.25 * PX, sy - 5.0 * PX, sx + 0.25 * PX, sy + 5.0 * PX), fill=(10, 10, 12))
		draw.rectangle((sx - 0.9 * PX, sy - 0.5 * PX, sx + 0.9 * PX, sy + 0.5 * PX), fill=(200, 200, 206))

	# the mixer: faders and a crossfader, its lights
	x0, y0, x1, y1 = D_MIXER
	img.paste(grain(D_MIXER, 28, 15), (x0, y0))
	w, h = x1 - x0, y1 - y0
	for k in range(2):
		fx = x0 + w * 0.62
		fy = y0 + h * (0.3 + 0.4 * k)
		draw.rectangle((fx - 0.3 * w, fy - 0.02 * h, fx + 0.3 * w, fy + 0.02 * h), fill=(8, 8, 10))
		draw.rectangle((fx - 0.05 * w, fy - 0.07 * h, fx + 0.05 * w, fy + 0.07 * h), fill=(220, 220, 226))
	draw.rectangle((x0 + 0.1 * w, y0 + 0.47 * h, x0 + 0.13 * w, y0 + 0.53 * h), fill=(8, 8, 10))
	# a level meter for each deck
	for k in range(6):
		for side in (0.2, 0.8):
			ly = y0 + h * (side - 0.08)
			lx = x0 + w * (0.22 + 0.05 * k)
			color = (60, 230, 120) if k < 4 else (250, 200, 60) if k < 5 else (255, 60, 60)
			draw.rectangle((lx, ly, lx + 0.03 * w, ly + 0.05 * h), fill=tuple(c // 4 for c in color))
			glow.rectangle((lx, ly, lx + 0.03 * w, ly + 0.05 * h), fill=color)
	return img, lights


def paint_platter():
	"""a record from above: grooves, a gloss across it, a label with a mark so its turn is seen"""
	size = 512
	c = (size - 1) / 2.0
	yy, xx = numpy.mgrid[0:size, 0:size]
	r = numpy.hypot(xx - c, yy - c) / c
	angle = numpy.arctan2(yy - c, xx - c)
	grooves = 14 + 7 * (0.5 + 0.5 * numpy.sin(r * 420.0)) + 18 * numpy.abs(numpy.cos(angle)) ** 8 * (r > 0.36)
	value = numpy.where(r > 0.985, 70.0, grooves)
	label = r < 0.34
	rgb = numpy.stack([value, value, value + 2], axis=2)
	rgb[label] = (230, 60, 110)
	mark = label & (numpy.abs(angle) < 0.35) & (r > 0.17)
	rgb[mark] = (250, 240, 230)
	rgb[r < 0.05] = (180, 180, 186)
	return to_image(rgb).filter(ImageFilter.GaussianBlur(0.6))


def paint_speaker():
	img = Image.new("RGB", S_SIZE, (18, 18, 22))
	lights = Image.new("RGB", S_SIZE, (0, 0, 0))
	img.paste(grain(S_PLAIN, 22, 21), S_PLAIN[:2])
	x0, y0, x1, y1 = S_FRONT
	w, h = x1 - x0, y1 - y0
	u = SPK_LO[1] + (numpy.arange(w) + 0.5) / PX
	v = SPK_HI[2] - (numpy.arange(h) + 0.5) / PX
	uu, vv = numpy.meshgrid(u, v)
	value = 26 + noise(w, h, 3.0, 22)
	# a dark ring with a lit bezel round each cone, lit from above as the boombox has them
	edge = 1.0 / PX
	for z, radius in ((WOOFER_Z, WOOFER_R), (TWEETER_Z, TWEETER_R)):
		dv = vv - z
		d = numpy.hypot(uu, dv)
		gap = 1.0 - outside(d, radius + 0.15, edge)
		bezel = (1.0 - outside(d, radius + 0.9, edge)) - gap
		shade = 50 + 80 * (0.5 + 0.5 * dv / numpy.maximum(d, 1e-6))
		value = value * (1.0 - bezel - gap) + shade * bezel + 8 * gap
	rgb = numpy.stack([value, value, value + 3], axis=2)
	img.paste(to_image(rgb), (x0, y0))
	# the power light low on the front, and a port for the bass
	draw, glow = ImageDraw.Draw(img), ImageDraw.Draw(lights)

	def at(uy, vz):
		return (x0 + (uy - SPK_LO[1]) * PX, y0 + (SPK_HI[2] - vz) * PX)

	px, py = at(8.5, 6.0)
	draw.ellipse((px - 0.4 * PX, py - 0.4 * PX, px + 0.4 * PX, py + 0.4 * PX), fill=(30, 60, 90))
	glow.ellipse((px - 0.4 * PX, py - 0.4 * PX, px + 0.4 * PX, py + 0.4 * PX), fill=(60, 170, 255))
	ax, ay = at(-4.0, 13.0)
	bx, by = at(4.0, 9.0)
	draw.rounded_rectangle((ax, ay, bx, by), radius=2.0 * PX, fill=(6, 6, 8), outline=(60, 60, 66), width=2)
	return img, lights


def main():
	out = sys.argv[1] if len(sys.argv) > 1 else OUT
	os.makedirs(out, exist_ok=True)
	counts = []
	for name, build in (("sound_desk", build_desk), ("sound_platter", build_platter), ("sound_speaker", build_speaker)):
		counts.append((name,) + write_md3(os.path.join(out, name + ".md3"), name, build()))
	counts.append(("sound_wave",) + write_wave(os.path.join(out, "sound_wave.md3")))
	desk, desk_lights = paint_desk()
	desk.save(os.path.join(out, "sound_desk.png"), optimize=True)
	desk_lights.save(os.path.join(out, "sound_desk_lights.png"), optimize=True)
	paint_platter().save(os.path.join(out, "sound_platter.png"), optimize=True)
	speaker, speaker_lights = paint_speaker()
	speaker.save(os.path.join(out, "sound_speaker.png"), optimize=True)
	speaker_lights.save(os.path.join(out, "sound_speaker_lights.png"), optimize=True)
	print("; ".join("%s.md3: %i vertices, %i triangles" % c for c in counts))


if __name__ == "__main__":
	main()
