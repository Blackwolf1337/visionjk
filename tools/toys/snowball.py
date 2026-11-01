#!/usr/bin/env python3
"""Makes the snowball of issue #9 (cgame/toys/cg_toys.c), all of it here, no model of anybody else:

  models/vision/toys/snowball/ball.md3        the ball, RADIUS units round its origin: a sphere of many faces pushed
                                              in and out a little by noise, as a ball pressed by hands is
  models/vision/toys/snowball/chunk1-3.md3    lumps of snow that fly off when it bursts, rougher and flatter
  models/vision/toys/snowball/snow.png        packed snow: white with a cool blue in its hollows,
                                              snow_n.png the clumps of it as a normal map (the shader keywords
                                              normalMap, wrapLight and sparkle, shaders/vision_toys.shader)

The faces take the picture from the side they face most, so it tiles over the ball without stretching at a pole.
usage:
  tools/toys/snowball.py
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "boombox"))
sys.path.insert(0, HERE)
from build import Surface, norm, write_md3  # noqa: E402
from rccar import normal_map, periodic_noise, to_image  # noqa: E402

OUT = os.path.join(HERE, "..", "..", "assets", "japlus_client", "models", "vision", "toys", "snowball")
SHADER = "models/vision/toys/snowball/snow"
RADIUS = 3.0				# TOY_RADIUS of cg_toys.c
TILE = 4.0					# units the picture covers


def icosphere(subdivisions):
	"""The corners and the faces (turned counter clockwise seen from outside) of a sphere of radius 1"""
	t = (1.0 + math.sqrt(5.0)) / 2.0
	verts = [norm(v) for v in ((-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t),
		(0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1))]
	faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
		(7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7),
		(9, 8, 1)]
	for _ in range(subdivisions):
		middle = {}

		def mid(a, b):
			key = (min(a, b), max(a, b))
			if key not in middle:
				middle[key] = len(verts)
				verts.append(norm(tuple((verts[a][k] + verts[b][k]) / 2.0 for k in range(3))))
			return middle[key]

		new = []
		for a, b, c in faces:
			ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
			new += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
		faces = new
	return verts, faces


def value_noise(p, seed):
	"""smooth noise of a point in space, -1 to 1: a random value at each corner of a grid of units, blended"""
	def corner(ix, iy, iz):
		h = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791) ^ (seed * 2654435761)
		h = (h ^ (h >> 13)) * 1274126177 & 0xffffffff
		return ((h ^ (h >> 16)) & 0xffff) / 32767.5 - 1.0

	x, y, z = p
	ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
	fx, fy, fz = x - ix, y - iy, z - iz
	sx, sy, sz = (f * f * (3.0 - 2.0 * f) for f in (fx, fy, fz))
	out = 0.0
	for dx in (0, 1):
		for dy in (0, 1):
			for dz in (0, 1):
				w = (sx if dx else 1.0 - sx) * (sy if dy else 1.0 - sy) * (sz if dz else 1.0 - sz)
				out += w * corner(ix + dx, iy + dy, iz + dz)
	return out


def lumpy(direction, seed, amount, octaves=3):
	return sum(value_noise(tuple(c * 1.6 * 2 ** o for c in direction), seed + o) * amount / 2 ** o for o in range(octaves))


def build(name, radius, amount, squash, seed, subdivisions):
	"""A ball pushed in and out by noise (amount of its radius) and pressed flat along z by squash"""
	unit, faces = icosphere(subdivisions)
	points = []
	for d in unit:
		r = radius * (1.0 + lumpy(d, seed, amount))
		points.append((d[0] * r, d[1] * r, d[2] * r * squash))
	face_normals = []
	for a, b, c in faces:
		u = tuple(points[b][k] - points[a][k] for k in range(3))
		v = tuple(points[c][k] - points[a][k] for k in range(3))
		face_normals.append(norm((u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])))
	smooth = [[0.0, 0.0, 0.0] for _ in points]
	for (a, b, c), n in zip(faces, face_normals):
		for i in (a, b, c):
			for k in range(3):
				smooth[i][k] += n[k]
	smooth = [norm(tuple(n)) for n in smooth]

	surface = Surface(name, SHADER)
	keys = {}
	for (a, b, c), n in zip(faces, face_normals):
		# the picture from the side the face looks to most
		axis = max(range(3), key=lambda k: abs(n[k]))
		ids = []
		for i in (a, b, c):
			key = (i, axis)
			if key not in keys:
				p = points[i]
				uv = [(p[1], p[2]), (p[0], p[2]), (p[0], p[1])][axis]
				keys[key] = len(surface.verts)
				surface.verts.append((p, smooth[i], (uv[0] / TILE, -uv[1] / TILE)))
			ids.append(keys[key])
		# (counter clockwise seen from outside here, clockwise in the game)
		surface.tris.append((ids[0], ids[2], ids[1]))
	return surface


def paint_snow(size=512):
	"""Packed snow: clumps of it (a height of noise in octaves, the hollows between them), white where it stands out
	and a cool blue in the hollows, fine grains over all of it"""
	clumps = periodic_noise(size, size, 1.4, 201, low=3.0)
	grains = periodic_noise(size, size, 0.3, 202, low=80.0)
	height = clumps * 0.8 + grains * 0.12
	light = np.clip(height * 0.5 + 0.5, 0.0, 1.0)
	white = np.array([0.95, 0.96, 0.98])
	blue = np.array([0.74, 0.82, 0.94])
	rgb = white[None, None, :] * light[..., None] + blue[None, None, :] * (1.0 - light[..., None])
	rgb *= 1.0 + np.maximum(grains, 0.0)[..., None] * 0.05
	return to_image(rgb), normal_map(height, 3.0)


def main():
	os.makedirs(OUT, exist_ok=True)
	ball = build("ball", RADIUS, 0.06, 1.0, 1, 3)
	nv, nt = write_md3(os.path.join(OUT, "ball.md3"), "models/vision/toys/snowball/ball", [ball])
	print("ball: %d vertices, %d triangles" % (nv, nt))
	for n, (radius, squash) in enumerate(((1.4, 0.7), (1.0, 0.6), (0.7, 0.8))):
		chunk = build("chunk", radius, 0.28, squash, 11 + n, 2)
		nv, nt = write_md3(os.path.join(OUT, "chunk%d.md3" % (n + 1)), "models/vision/toys/snowball/chunk%d" % (n + 1), [chunk])
		print("chunk%d: %d vertices, %d triangles" % (n + 1, nv, nt))
	albedo, normals = paint_snow()
	albedo.save(os.path.join(OUT, "snow.png"), optimize=True)
	normals.save(os.path.join(OUT, "snow_n.png"), optimize=True)


if __name__ == "__main__":
	main()
