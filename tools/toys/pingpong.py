#!/usr/bin/env python3
"""Writes the table tennis of issue #9 (codemp/cgame/toys/cg_pong.c): the table and the paddle as MD3, made from
boxes and cylinders with the geometry and the MD3 writer of tools/boombox/build.py, and their pictures:

  models/map_objects/vision/pingpong_table.md3   the table, its middle on the floor, its length along x, the net at x 0
  models/map_objects/vision/pingpong_paddle.md3  a paddle, the middle of its blade at the origin, its faces to +x and -x
  pingpong_top.png, pingpong_frame.png, pingpong_net.png, paddle_rubber.png, paddle_wood.png   next to them
  gfx/vision/toys/pingball.png                   the ball, a sprite

The sizes are those of cg_pong.c (PONG_LENGTH, PONG_WIDTH, PONG_HEIGHT, PONG_NET): a real table a third larger, so
the small ball can be seen. The net and the ball have shader scripts in assets/japlus_client/shaders/vision_toys.shader.
usage: tools/toys/pingpong.py
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "boombox"))
from build import Surface, plain_box, cylinder, write_md3  # noqa: E402

ASSETS = os.path.join(HERE, "..", "..", "assets", "japlus_client")
OUT = os.path.join(ASSETS, "models", "map_objects", "vision")
SHADERS = "models/map_objects/vision/"

LENGTH, WIDTH, HEIGHT, NET = 124.0, 70.0, 31.0, 7.0		# PONG_LENGTH, PONG_WIDTH, PONG_HEIGHT, PONG_NET of cg_pong.c
BOARD = 1.5
BLADE = 5.0												# PONG_BLADE of cg_pong.c


def flat_uv(p, normal):
	return ((p[0] + p[1]) / 32.0, p[2] / 32.0)


def build_table():
	top = Surface("top", SHADERS + "pingpong_top")
	frame = Surface("frame", SHADERS + "pingpong_frame")
	net = Surface("net", SHADERS + "pingpong_net")
	hl, hw = LENGTH / 2, WIDTH / 2
	# the playing face, the picture along its length
	corners = [(-hl, -hw, HEIGHT), (hl, -hw, HEIGHT), (hl, hw, HEIGHT), (-hl, hw, HEIGHT)]
	top.poly(corners, (0.0, 0.0, 1.0), [((p[0] + hl) / LENGTH, (p[1] + hw) / WIDTH) for p in corners])
	# the board under it, a rail round it, the legs and the bars between them
	plain_box(frame, (-hl, -hw, HEIGHT - BOARD), (hl, hw, HEIGHT - 0.05), flat_uv)
	plain_box(frame, (-hl + 6, -hw + 6, HEIGHT - BOARD - 3), (hl - 6, hw - 6, HEIGHT - BOARD), flat_uv)
	for sx in (-1, 1):
		for sy in (-1, 1):
			x, y = sx * (hl - 10), sy * (hw - 8)
			plain_box(frame, (x - 1.4, y - 1.4, 0.0), (x + 1.4, y + 1.4, HEIGHT - BOARD - 3), flat_uv)
		plain_box(frame, (sx * (hl - 10) - 0.8, -hw + 8, 6.0), (sx * (hl - 10) + 0.8, hw - 8, 7.6), flat_uv)
	# the net, both faces, and its posts
	for normal, x in (((1.0, 0.0, 0.0), 0.05), ((-1.0, 0.0, 0.0), -0.05)):
		pts = [(x, -hw - 3, HEIGHT), (x, hw + 3, HEIGHT), (x, hw + 3, HEIGHT + NET), (x, -hw - 3, HEIGHT + NET)]
		net.poly(pts, normal, [((p[1] + hw + 3) / (WIDTH + 6) * 8.0, (p[2] - HEIGHT) / NET) for p in pts])
	for sy in (-1, 1):
		plain_box(frame, (-0.8, sy * (hw + 3) - 0.8, HEIGHT - 2), (0.8, sy * (hw + 3) + 0.8, HEIGHT + NET + 0.5), flat_uv)
	return [top, frame, net]


def build_paddle():
	rubber = Surface("rubber", SHADERS + "paddle_rubber")
	wood = Surface("wood", SHADERS + "paddle_wood")
	cylinder(rubber, (-0.4, 0.0, 0.0), 0, BLADE, 0.8)
	plain_box(wood, (-0.5, -0.7, -BLADE - 4.5), (0.5, 0.7, -BLADE + 0.6), flat_uv)
	return [rubber, wood]


def grain(img, amount, seed):
	rnd = random.Random(seed)
	px = img.load()
	for y in range(img.size[1]):
		for x in range(img.size[0]):
			d = rnd.randint(-amount, amount)
			r, g, b = px[x, y][:3]
			px[x, y] = (max(0, min(255, r + d)), max(0, min(255, g + d)), max(0, min(255, b + d))) + tuple(px[x, y][3:])


def paint():
	top = Image.new("RGB", (512, 256), (24, 62, 120))
	grain(top, 6, 1)
	d = ImageDraw.Draw(top)
	d.rectangle((0, 0, 511, 255), outline=(236, 236, 230), width=5)
	d.line((0, 128, 511, 128), fill=(236, 236, 230), width=2)
	top.save(os.path.join(OUT, "pingpong_top.png"))

	frame = Image.new("RGB", (64, 64), (46, 48, 52))
	grain(frame, 8, 2)
	frame.save(os.path.join(OUT, "pingpong_frame.png"))

	net = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
	d = ImageDraw.Draw(net)
	for k in range(0, 64, 4):
		d.line((k, 4, k, 31), fill=(230, 230, 230, 255), width=1)
	for k in range(4, 32, 4):
		d.line((0, k, 63, k), fill=(230, 230, 230, 255), width=1)
	d.rectangle((0, 0, 63, 3), fill=(250, 250, 250, 255))
	net.save(os.path.join(OUT, "pingpong_net.png"))

	rubber = Image.new("RGB", (64, 64), (178, 24, 28))
	grain(rubber, 10, 3)
	rubber = rubber.filter(ImageFilter.GaussianBlur(0.6))
	rubber.save(os.path.join(OUT, "paddle_rubber.png"))

	wood = Image.new("RGB", (64, 64), (150, 104, 62))
	d = ImageDraw.Draw(wood)
	for k in range(0, 64, 3):
		d.line((0, k, 63, k + 2), fill=(128, 86, 50), width=1)
	wood.save(os.path.join(OUT, "paddle_wood.png"))

	# the ball: a lit white sphere, soft at its rim
	size = 32
	ball = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	px = ball.load()
	for y in range(size):
		for x in range(size):
			nx, ny = (x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1
			r2 = nx * nx + ny * ny
			if r2 > 1.0:
				continue
			nz = math.sqrt(1.0 - r2)
			light = max(0.0, -0.4 * nx - 0.5 * ny + 0.77 * nz)
			v = int(150 + 105 * light)
			alpha = int(255 * min(1.0, (1.0 - math.sqrt(r2)) * 8.0))
			px[x, y] = (v, v, int(v * 0.97), alpha)
	ball.save(os.path.join(ASSETS, "gfx", "vision", "toys", "pingball.png"))


def main():
	os.makedirs(OUT, exist_ok=True)
	print("table: %i vertices, %i triangles" % write_md3(os.path.join(OUT, "pingpong_table.md3"), "pingpong_table", build_table()))
	print("paddle: %i vertices, %i triangles" % write_md3(os.path.join(OUT, "pingpong_paddle.md3"), "pingpong_paddle", build_paddle()))
	paint()


if __name__ == "__main__":
	main()
