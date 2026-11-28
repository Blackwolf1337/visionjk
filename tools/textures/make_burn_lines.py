#!/usr/bin/env python3
# The pictures of force lightning (issue #19, codemp/cgame/force/cg_lightning.c and cg_burnmark.c), made here with
# fixed seeds, so the files come out the same every time and no picture of anybody else ships:
#
# - burnline1-4: the soot a bolt leaves on a wall: a Lichtenberg figure (the branching figure a strike burns into wood
#   or skin), grown with the dielectric breakdown model (Niemeyer, Pietronero, Wiesmann 1984: each step the figure
#   grows by one cell next to it, picked by the electric potential there to the power eta; the potential by the sum of
#   point charges of Kim et al. 2007). Trunks are thick where much of the figure hangs on them. Around it a blackened
#   pit with mottled soot. Premultiplied as the char of the saber scars: the wall times rgb + 1 - alpha.
# - burnglow1-4: the same figure as the light of the hot lines, brightest on the trunks, so the thin ends go dark
#   first as it cools (added, coloured by the cgame).
# - heattint: the temper colours a hot spot leaves on steel (thin oxide: straw, bronze, purple, blue, grey in the
#   middle), rings bent by noise; premultiplied as the soot.
# - arc_core, arc_glow: across a bolt, the white-hot channel and the glow of the air round it (s across, t along).
#
# usage: tools/textures/make_burn_lines.py
import math
import os
import struct

import numpy as np

SIZE = 256		# (the renderer refuses sizes that are no power of two)
GRID = 128		# cells of the grown figure across the picture
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'gfx', 'vision' )


def write_tga( name, rgba ):
	h, w = rgba.shape[:2]
	pixels = np.clip( rgba * 255.0 + 0.5, 0, 255 ).astype( np.uint8 )
	# BGRA, bottom-up rows are fine for a picture that is turned at random anyway, but keep top-down (flag 0x20)
	bgra = pixels[:, :, [2, 1, 0, 3]]
	with open( os.path.join( OUT, name + '.tga' ), 'wb' ) as f:
		f.write( struct.pack( '<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8 | 0x20 ) )
		f.write( bgra.tobytes() )
	print( name + '.tga' )


def noise( rng, size, octaves=5 ):
	"""Smooth value noise 0..1, size x size, wrapped."""
	out = np.zeros( ( size, size ) )
	amp, total = 1.0, 0.0
	for o in range( octaves ):
		cells = 4 * 2 ** o
		grid = rng.random( ( cells + 1, cells + 1 ) )
		grid[-1, :] = grid[0, :]
		grid[:, -1] = grid[:, 0]
		axis = np.linspace( 0, cells, size, endpoint=False )
		i = axis.astype( int )
		f = axis - i
		f = f * f * ( 3 - 2 * f )
		a = grid[i][:, i] * ( 1 - f )[None, :] + grid[i][:, i + 1] * f[None, :]
		b = grid[i + 1][:, i] * ( 1 - f )[None, :] + grid[i + 1][:, i + 1] * f[None, :]
		out += amp * ( a * ( 1 - f )[:, None] + b * f[:, None] )
		total += amp
		amp *= 0.5
	return out / total


def grow( rng, eta=3.2, cells=1600 ):
	"""A Lichtenberg figure by the dielectric breakdown model: list of ( x, y, parent ) in grid cells."""
	n = GRID
	c = n // 2
	ys, xs = np.mgrid[0:n, 0:n].astype( float )
	occupied = np.zeros( ( n, n ), bool )
	candidate = np.zeros( ( n, n ), bool )
	phi = np.zeros( ( n, n ) )
	inside = ( xs - c ) ** 2 + ( ys - c ) ** 2 < ( 0.9 * c ) ** 2
	points = []
	index = -np.ones( ( n, n ), int )

	def add( x, y, parent ):
		occupied[y, x] = True
		candidate[y, x] = False
		index[y, x] = len( points )
		points.append( ( x, y, parent ) )
		r = np.hypot( xs - x, ys - y )
		r[y, x] = 1.0
		phi[:] += 1.0 - 0.5 / r
		for dy in ( -1, 0, 1 ):
			for dx in ( -1, 0, 1 ):
				nx, ny = x + dx, y + dy
				if 0 <= nx < n and 0 <= ny < n and not occupied[ny, nx] and inside[ny, nx]:
					candidate[ny, nx] = True

	add( c, c, -1 )
	# a few seeds round the strike, so the figure starts as several trunks
	for k in range( rng.integers( 3, 6 ) ):
		a = rng.random() * 2 * math.pi
		x, y = int( round( c + math.cos( a ) * 2 ) ), int( round( c + math.sin( a ) * 2 ) )
		if not occupied[y, x]:
			add( x, y, 0 )
	edge = 0
	# until the figure is that large or a few of its trunks reached the rim
	while len( points ) < cells and candidate.any() and edge < 4:
		cy, cx = np.nonzero( candidate )
		v = phi[cy, cx]
		v = ( v - v.min() ) / max( v.max() - v.min(), 1e-9 )
		w = v ** eta + 1e-12
		k = rng.choice( len( w ), p=w / w.sum() )
		x, y = cx[k], cy[k]
		# the neighbour it grew from: the one nearest the middle
		best, bestd = -1, 1e9
		for dy in ( -1, 0, 1 ):
			for dx in ( -1, 0, 1 ):
				nx, ny = x + dx, y + dy
				if 0 <= nx < n and 0 <= ny < n and occupied[ny, nx]:
					d = ( nx - c ) ** 2 + ( ny - c ) ** 2
					if d < bestd:
						best, bestd = index[ny, nx], d
		add( x, y, best )
		if ( x - c ) ** 2 + ( y - c ) ** 2 > ( 0.84 * c ) ** 2:
			edge += 1
	return points


def figure( rng ):
	"""The grown figure drawn as lines: ( line 0..1, thickness of the trunk 0..1, halo 0..1, distance to the lines )."""
	points = grow( rng )
	count = len( points )
	hang = np.ones( count )
	for i in range( count - 1, 0, -1 ):
		p = points[i][2]
		if p >= 0:
			hang[p] += hang[i]
	thick = ( hang / hang.max() ) ** 0.45
	scale = SIZE / GRID
	# the cells sit on a grid: shake them a little so the lines run in every direction, not in eight
	jitter = rng.uniform( -0.4, 0.4, ( count, 2 ) )
	pos = np.array( [( p[0], p[1] ) for p in points], float ) + jitter
	pos[0] = ( GRID // 2, GRID // 2 )
	pos = ( pos + 0.5 ) * scale

	line = np.zeros( ( SIZE, SIZE ) )
	trunk = np.zeros( ( SIZE, SIZE ) )
	halo = np.zeros( ( SIZE, SIZE ) )
	for i in range( 1, count ):
		p = points[i][2]
		if p < 0:
			continue
		x0, y0 = pos[p]
		x1, y1 = pos[i]
		t = 0.5 * ( thick[i] + thick[p] )
		width = 0.55 + 3.2 * t
		reach = width * 4.0
		lx, hx = int( max( 0, min( x0, x1 ) - reach ) ), int( min( SIZE, max( x0, x1 ) + reach + 1 ) )
		ly, hy = int( max( 0, min( y0, y1 ) - reach ) ), int( min( SIZE, max( y0, y1 ) + reach + 1 ) )
		if lx >= hx or ly >= hy:
			continue
		yy, xx = np.mgrid[ly:hy, lx:hx] + 0.5
		dx, dy = x1 - x0, y1 - y0
		length = max( dx * dx + dy * dy, 1e-9 )
		u = np.clip( ( ( xx - x0 ) * dx + ( yy - y0 ) * dy ) / length, 0, 1 )
		d = np.hypot( xx - ( x0 + u * dx ), yy - ( y0 + u * dy ) )
		l = np.clip( 1.0 - d / width, 0, 1 ) ** 0.7
		hl = np.clip( 1.0 - d / reach, 0, 1 ) ** 2 * ( 0.25 + 0.4 * t )
		line[ly:hy, lx:hx] = np.maximum( line[ly:hy, lx:hx], l )
		trunk[ly:hy, lx:hx] = np.maximum( trunk[ly:hy, lx:hx], l * t )
		halo[ly:hy, lx:hx] = np.maximum( halo[ly:hy, lx:hx], hl )
	return line, trunk, halo


def radius_map():
	axis = ( np.arange( SIZE ) + 0.5 ) / SIZE * 2.0 - 1.0
	y, x = np.meshgrid( axis, axis, indexing='ij' )
	return np.hypot( x, y )


def burn( n, seed ):
	rng = np.random.default_rng( seed )
	line, trunk, halo = figure( rng )
	r = radius_map()
	mottle = noise( rng, SIZE )
	rim = np.clip( ( 1.0 - r ) / 0.1, 0, 1 )

	# soot: the lines black, a brown scorch beside them, a black pit where it struck with mottled soot round it
	pit = np.clip( 1.0 - r / ( 0.11 + 0.04 * mottle ), 0, 1 ) ** 0.6
	cloud = np.clip( 1.0 - r / 0.45, 0, 1 ) ** 1.5 * ( 0.15 + 0.35 * mottle )
	dark = np.maximum( np.maximum( line * ( 0.75 + 0.25 * trunk ), pit * 0.95 ), np.maximum( halo, cloud ) ) * rim
	# black where it burned through, brown at the edge of the scorch
	black = np.maximum( line, pit )
	color = np.zeros( ( SIZE, SIZE, 3 ) )
	brown = np.array( [0.22, 0.14, 0.08] )
	char = np.array( [0.03, 0.028, 0.026] )
	for c in range( 3 ):
		color[:, :, c] = char[c] * black + brown[c] * ( 1 - black )
	rgba = np.zeros( ( SIZE, SIZE, 4 ) )
	rgba[:, :, :3] = color * dark[:, :, None]
	rgba[:, :, 3] = dark
	write_tga( 'burnline%i' % n, rgba )

	# the glow: the trunks bright, the thin ends dim, the pit white hot
	core = np.clip( 1.0 - r / 0.08, 0, 1 ) ** 1.5
	glow = np.maximum( line ** 1.6 * ( 0.2 + 0.8 * trunk ), core ) * rim
	glow = np.maximum( glow, halo * 0.18 * rim )
	rgba = np.zeros( ( SIZE, SIZE, 4 ) )
	rgba[:, :, 0] = rgba[:, :, 1] = rgba[:, :, 2] = glow
	rgba[:, :, 3] = 1.0
	write_tga( 'burnglow%i' % n, rgba )


def heat_tint():
	rng = np.random.default_rng( 1999 )
	r = radius_map()
	bend = noise( rng, SIZE, 4 )
	rr = r * ( 0.85 + 0.3 * bend )
	# what the steel is multiplied by, from the middle out (hottest first)
	keys = [
		( 0.00, ( 0.50, 0.52, 0.56 ) ),		# thick grey oxide
		( 0.22, ( 0.52, 0.62, 0.92 ) ),		# blue
		( 0.38, ( 0.74, 0.52, 0.80 ) ),		# purple
		( 0.55, ( 0.90, 0.66, 0.46 ) ),		# bronze
		( 0.72, ( 1.00, 0.88, 0.62 ) ),		# straw
		( 0.92, ( 1.00, 1.00, 1.00 ) ),		# the bare steel
	]
	mult = np.ones( ( SIZE, SIZE, 3 ) )
	# (thin and uneven: a film of oxide, no paint)
	strength = 0.55 + 0.25 * noise( rng, SIZE, 3 )
	for i in range( len( keys ) - 1 ):
		r0, c0 = keys[i]
		r1, c1 = keys[i + 1]
		m = ( rr >= r0 ) & ( rr < r1 )
		f = ( ( rr - r0 ) / ( r1 - r0 ) )[m]
		f = f * f * ( 3 - 2 * f )
		for c in range( 3 ):
			mult[:, :, c][m] = c0[c] + ( c1[c] - c0[c] ) * f
	mult = 1.0 - ( 1.0 - mult ) * strength[:, :, None]
	# premultiplied: rgb + 1 - alpha = mult, the vertices fade rgb and alpha together to the bare steel
	alpha = 1.0 - mult.min( axis=2 )
	rgba = np.zeros( ( SIZE, SIZE, 4 ) )
	rgba[:, :, :3] = mult - ( 1.0 - alpha )[:, :, None]
	rgba[:, :, 3] = alpha
	write_tga( 'heattint', rgba )


def arcs():
	# 64 across, 4 along
	x = ( np.arange( 64 ) + 0.5 ) / 64 * 2.0 - 1.0
	edge = np.clip( 1.0 - x * x, 0, 1 )
	core = ( 0.75 * np.exp( -( x / 0.14 ) ** 2 ) + 0.25 * np.exp( -( x / 0.4 ) ** 2 ) ) * edge
	glow = np.exp( -( x / 0.42 ) ** 2 ) * edge
	for name, profile in ( ( 'arc_core', core ), ( 'arc_glow', glow ) ):
		rgba = np.ones( ( 4, 64, 4 ) )
		for c in range( 3 ):
			rgba[:, :, c] = profile[None, :]
		write_tga( name, rgba )


def main():
	os.makedirs( OUT, exist_ok=True )
	for n, seed in enumerate( ( 1901, 1902, 1903, 1904 ), 1 ):
		burn( n, seed )
	heat_tint()
	arcs()


if __name__ == '__main__':
	main()
