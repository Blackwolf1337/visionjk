#!/usr/bin/env python3
"""Writes the table tennis and the tennis of issue #9 (codemp/cgame/toys/cg_pong.c): the table, the paddle, the court
and the racket as MD3, made from boxes, cylinders and flat polygons with the geometry and the MD3 writer of
tools/boombox/build.py, and their pictures:

  models/map_objects/vision/pingpong_table.md3   the table, its middle on the floor, its length along x, the net at x 0
  models/map_objects/vision/pingpong_paddle.md3  a paddle, the middle of its blade at the origin, its faces to +x and -x
  pingpong_top.png, pingpong_frame.png, pingpong_net.png, paddle_rubber.png, paddle_wood.png   next to them
  models/map_objects/vision/tennis_court.md3     the court, its middle on the floor, its length along x, the net at x 0
  models/map_objects/vision/tennis_racket.md3    a racket, the end of its grip at the origin, its head up, faces to +x and -x
  tennis_court.png, tennis_strings.png, tennis_frame.png   next to them
  gfx/vision/toys/pingball.png                   the ball of both, a sprite (yellow on the court)

The sizes are those of cg_pong.c (PONG_LENGTH, PONG_WIDTH, PONG_HEIGHT, PONG_NET, COURT_*): a real table a third
larger, so the small ball can be seen, and a court about half the real one, so it fits on the floors of the levels.
The nets, the court, the strings and the ball have shader scripts in assets/japlus_client/shaders/vision_toys.shader.
usage: tools/toys/pingpong.py
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "boombox"))
from build import Surface, cylinder, norm, plain_box, write_md3  # noqa: E402

ASSETS = os.path.join(HERE, "..", "..", "assets", "japlus_client")
OUT = os.path.join(ASSETS, "models", "map_objects", "vision")
SHADERS = "models/map_objects/vision/"

LENGTH, WIDTH, HEIGHT, NET = 124.0, 70.0, 31.0, 7.0		# PONG_LENGTH, PONG_WIDTH, PONG_HEIGHT, PONG_NET of cg_pong.c
BOARD = 1.5
BLADE = 5.0												# PONG_BLADE of cg_pong.c
COURT_LENGTH, COURT_WIDTH, COURT_NET = 440.0, 200.0, 24.0	# COURT_LENGTH, COURT_WIDTH, COURT_NET of cg_pong.c
COURT_RUN_X, COURT_RUN_Y = 60.0, 40.0					# the ground drawn round the lines, behind the ends and beside the sides
COURT_LIFT = 0.5										# over the floor


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


def build_court():
	court = Surface("court", SHADERS + "tennis_court")
	frame = Surface("frame", SHADERS + "pingpong_frame")
	net = Surface("net", SHADERS + "pingpong_net")
	hl, hw = COURT_LENGTH / 2 + COURT_RUN_X, COURT_WIDTH / 2 + COURT_RUN_Y
	corners = [(-hl, -hw, COURT_LIFT), (hl, -hw, COURT_LIFT), (hl, hw, COURT_LIFT), (-hl, hw, COURT_LIFT)]
	court.poly(corners, (0.0, 0.0, 1.0), [((p[0] + hl) / (2 * hl), (p[1] + hw) / (2 * hw)) for p in corners])
	# the net reaches past the lines to its posts, both faces, and a white band along its top
	post = COURT_WIDTH / 2 + 12
	for normal, x in (((1.0, 0.0, 0.0), 0.1), ((-1.0, 0.0, 0.0), -0.1)):
		pts = [(x, -post, 1.0), (x, post, 1.0), (x, post, COURT_NET), (x, -post, COURT_NET)]
		net.poly(pts, normal, [((p[1] + post) / (2 * post) * 40.0, (p[2] - 1.0) / (COURT_NET - 1.0) * 3.0) for p in pts])
	plain_box(frame, (-0.4, -post, COURT_NET - 0.3), (0.4, post, COURT_NET + 0.9), flat_uv)
	for sy in (-1, 1):
		cylinder(frame, (0.0, sy * post, 0.0), 2, 1.3, COURT_NET + 2.0)
	return [court, frame, net]


def build_racket():
	strings = Surface("strings", SHADERS + "tennis_strings")
	frame = Surface("frame", SHADERS + "tennis_frame")
	grip, throat, rw, rh = 7.0, 11.0, 5.0, 6.5		# the head: an ellipse over the throat, half widths along y and z
	cz = throat + rh
	plain_box(frame, (-0.55, -0.55, 0.0), (0.55, 0.55, grip), flat_uv)
	# the throat: two bars from the grip to the head
	for sy in (-1, 1):
		a, b = (0.0, sy * 0.4, grip), (0.0, sy * rw * 0.55, throat + 1.0)
		for nx in (1.0, -1.0):
			x = nx * 0.35
			pts = [(x, a[1] - 0.35, a[2]), (x, a[1] + 0.35, a[2]), (x, b[1] + 0.35, b[2]), (x, b[1] - 0.35, b[2])]
			frame.poly(pts, (nx, 0.0, 0.0), [(p[1] / 8.0, p[2] / 8.0) for p in pts])
	# the rim, a band round the ellipse: its outside, its inside and its two faces
	steps = 24
	ring = [(rw * math.cos(2 * math.pi * k / steps), rh * math.sin(2 * math.pi * k / steps)) for k in range(steps + 1)]
	for k in range(steps):
		(y0, z0), (y1, z1) = ring[k], ring[k + 1]
		n0, n1 = (y0 / rw, z0 / rh), (y1 / rw, z1 / rh)
		o0 = (y0 + n0[0] * 0.7, z0 + n0[1] * 0.7)
		o1 = (y1 + n1[0] * 0.7, z1 + n1[1] * 0.7)
		mid = norm((0.0, (n0[0] + n1[0]) * 0.5, (n0[1] + n1[1]) * 0.5))
		for inner, sign, (a0, a1) in ((False, 1.0, (o0, o1)), (True, -1.0, ((y0, z0), (y1, z1)))):
			pts = [(-0.45, a0[0], cz + a0[1]), (0.45, a0[0], cz + a0[1]), (0.45, a1[0], cz + a1[1]), (-0.45, a1[0], cz + a1[1])]
			frame.poly(pts, (0.0, mid[1] * sign, mid[2] * sign), [(k / 4.0, 0.0), (k / 4.0, 0.2), ((k + 1) / 4.0, 0.2), ((k + 1) / 4.0, 0.0)])
		for nx in (1.0, -1.0):
			x = nx * 0.45
			pts = [(x, y0, cz + z0), (x, o0[0], cz + o0[1]), (x, o1[0], cz + o1[1]), (x, y1, cz + z1)]
			frame.poly(pts, (nx, 0.0, 0.0), [(k / 4.0, 0.0), (k / 4.0, 0.2), ((k + 1) / 4.0, 0.2), ((k + 1) / 4.0, 0.0)])
	# the strings: one face each way over the ellipse, the picture is clear outside it
	for nx in (1.0, -1.0):
		x = nx * 0.05
		pts = [(x, -rw, cz - rh), (x, rw, cz - rh), (x, rw, cz + rh), (x, -rw, cz + rh)]
		strings.poly(pts, (nx, 0.0, 0.0), [((p[1] + rw) / (2 * rw), (p[2] - cz + rh) / (2 * rh)) for p in pts])
	return [strings, frame]


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

	# the court: blue inside the lines, green round them, the lines white (the picture covers the ground round them too)
	w, h = 1024, 512
	hl, hw = COURT_LENGTH / 2 + COURT_RUN_X, COURT_WIDTH / 2 + COURT_RUN_Y
	court = Image.new("RGB", (w, h), (52, 112, 70))
	grain(court, 5, 4)
	d = ImageDraw.Draw(court)

	def at(x, y):
		return ((x + hl) / (2 * hl) * w, (y + hw) / (2 * hw) * h)

	def line(x0, y0, x1, y1):
		d.rectangle(at(x0, y0) + at(x1, y1), fill=(238, 238, 232))

	inner = Image.new("RGB", (w, h), (40, 84, 150))
	grain(inner, 5, 5)
	box = tuple(int(v) for v in at(-COURT_LENGTH / 2, -COURT_WIDTH / 2) + at(COURT_LENGTH / 2, COURT_WIDTH / 2))
	court.paste(inner.crop(box), box[:2])
	cl, cw, lw, service = COURT_LENGTH / 2, COURT_WIDTH / 2, 1.6, COURT_LENGTH / 2 * 0.54
	line(-cl, -cw, cl, -cw + lw)
	line(-cl, cw - lw, cl, cw)
	line(-cl, -cw, -cl + lw, cw)
	line(cl - lw, -cw, cl, cw)
	line(-service, -cw, -service + lw, cw)
	line(service - lw, -cw, service, cw)
	line(-service, -lw / 2, service, lw / 2)
	line(-cl, -lw / 2, -cl + 5, lw / 2)
	line(cl - 5, -lw / 2, cl, lw / 2)
	court.save(os.path.join(OUT, "tennis_court.png"))

	# (a square: the face of the strings stretches it to the ellipse of the head)
	strings = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
	px = strings.load()
	for y in range(128):
		for x in range(128):
			nx, ny = (x + 0.5) / 64.0 - 1.0, (y + 0.5) / 64.0 - 1.0
			if nx * nx + ny * ny <= 1.0 and (x % 8 < 2 or y % 8 < 2):
				px[x, y] = (232, 232, 214, 255)
	strings.save(os.path.join(OUT, "tennis_strings.png"))

	frame = Image.new("RGB", (64, 64), (36, 38, 44))
	d = ImageDraw.Draw(frame)
	d.rectangle((0, 0, 63, 12), fill=(196, 40, 36))
	grain(frame, 6, 6)
	frame.save(os.path.join(OUT, "tennis_frame.png"))

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
	print("court: %i vertices, %i triangles" % write_md3(os.path.join(OUT, "tennis_court.md3"), "tennis_court", build_court()))
	print("racket: %i vertices, %i triangles" % write_md3(os.path.join(OUT, "tennis_racket.md3"), "tennis_racket", build_racket()))
	paint()


if __name__ == "__main__":
	main()
