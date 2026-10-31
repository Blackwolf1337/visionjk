#!/usr/bin/env python3
"""Draws the pictures of the toys of issue #9 (cgame/toys/cg_toys.c) into assets/japlus_client/gfx/vision/toys:

  snowball.png    the ball in flight, a sprite: a lit sphere of packed snow, soft at its rim
  snowpuff.png    a puff of powder for the burst (effects/vision/snowball_hit.efx), and of mist for the water
  snowsplat.png   what stays: on a wall as a mark that melts away, and on the screen of the one who is hit
  waterstream.png the jet of the water gun, along its length: clear water with a bright core and dark rims
  waterdrop.png   a drop for the splash (effects/vision/water_splash.efx)
  wet.png         a wet patch on a wall: darker, soft and uneven at its rim
  puddle.png      a puddle on the floor, and puddleshine.png the light that plays on it

Made from noise with a fixed seed, so the pictures come out the same every time. usage: tools/toys/textures.py
"""
import math
import os
import random

from PIL import Image, ImageFilter

OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'gfx', 'vision', 'toys' )


def noise( size, scale, seed ):
	"""Smooth value noise in 0..1, size x size, cells of scale pixels."""
	rnd = random.Random( seed )
	cells = size // scale + 2
	grid = [ [ rnd.random() for _ in range( cells ) ] for _ in range( cells ) ]
	out = [ [ 0.0 ] * size for _ in range( size ) ]
	for y in range( size ):
		for x in range( size ):
			gx, gy = x / scale, y / scale
			ix, iy = int( gx ), int( gy )
			fx, fy = gx - ix, gy - iy
			fx, fy = fx * fx * ( 3 - 2 * fx ), fy * fy * ( 3 - 2 * fy )
			a = grid[iy][ix] + ( grid[iy][ix + 1] - grid[iy][ix] ) * fx
			b = grid[iy + 1][ix] + ( grid[iy + 1][ix + 1] - grid[iy + 1][ix] ) * fx
			out[y][x] = a + ( b - a ) * fy
	return out


def octaves( size, seed, scales=( 16, 8, 4, 2 ) ):
	layers = [ noise( size, s, seed + i ) for i, s in enumerate( scales ) ]
	weights = [ 0.5 ** i for i in range( len( scales ) ) ]
	total = sum( weights )
	return [ [ sum( w * l[y][x] for w, l in zip( weights, layers ) ) / total for x in range( size ) ] for y in range( size ) ]


def snowball( size=64 ):
	n = octaves( size, 11, ( 8, 4, 2 ) )
	img = Image.new( 'RGBA', ( size, size ) )
	light = ( -0.45, -0.55, 0.7 )
	for y in range( size ):
		for x in range( size ):
			dx, dy = ( x + 0.5 ) / size * 2 - 1, ( y + 0.5 ) / size * 2 - 1
			r = math.hypot( dx, dy )
			edge = 0.86 + 0.08 * ( n[y][x] - 0.5 )
			if r > edge:
				img.putpixel( ( x, y ), ( 255, 255, 255, 0 ) )
				continue
			nz = math.sqrt( max( 0.0, 1 - ( r / edge ) ** 2 ) )
			lit = max( 0.0, dx / edge * light[0] + dy / edge * light[1] + nz * light[2] )
			grain = 0.82 + 0.3 * ( n[y][x] - 0.5 )
			v = ( 0.55 + 0.45 * lit ) * grain
			# a cold blue in the shade, white where the light falls
			col = ( int( min( 255, 225 * v + 40 * lit ) ), int( min( 255, 235 * v + 30 * lit ) ), int( min( 255, 255 * v + 10 ) ) )
			alpha = int( 255 * min( 1.0, ( edge - r ) / 0.08 ) )
			img.putpixel( ( x, y ), col + ( alpha, ) )
	return img


def snowpuff( size=64 ):
	n = octaves( size, 23 )
	img = Image.new( 'RGBA', ( size, size ) )
	for y in range( size ):
		for x in range( size ):
			dx, dy = ( x + 0.5 ) / size * 2 - 1, ( y + 0.5 ) / size * 2 - 1
			r = math.hypot( dx, dy )
			a = max( 0.0, 1 - r ) ** 1.6 * ( 0.55 + 0.9 * ( n[y][x] - 0.35 ) )
			img.putpixel( ( x, y ), ( 245, 250, 255, int( 255 * max( 0.0, min( 1.0, a ) ) ) ) )
	return img


def snowsplat( size=256 ):
	"""A clump in the middle, lumps around it and specks thrown off, with the grain of packed snow."""
	rnd = random.Random( 5 )
	n = octaves( size, 31, ( 32, 16, 8, 4 ) )
	grain = noise( size, 2, 77 )
	blobs = [ ( 0.0, 0.0, 0.3 ) ]
	for _ in range( 8 ):
		ang, dist = rnd.uniform( 0, 2 * math.pi ), rnd.uniform( 0.22, 0.48 )
		blobs.append( ( math.cos( ang ) * dist, math.sin( ang ) * dist, rnd.uniform( 0.07, 0.13 ) ) )
	for _ in range( 22 ):
		ang, dist = rnd.uniform( 0, 2 * math.pi ), rnd.uniform( 0.5, 0.86 )
		blobs.append( ( math.cos( ang ) * dist, math.sin( ang ) * dist, rnd.uniform( 0.018, 0.04 ) ) )
	img = Image.new( 'RGBA', ( size, size ) )
	for y in range( size ):
		for x in range( size ):
			dx, dy = ( x + 0.5 ) / size * 2 - 1, ( y + 0.5 ) / size * 2 - 1
			field = 0.0
			for bx, by, br in blobs:
				d2 = ( dx - bx ) ** 2 + ( dy - by ) ** 2
				field += br * br / ( d2 + 1e-4 )
			field *= 0.7 + 0.6 * n[y][x]
			a = max( 0.0, min( 1.0, ( field - 1.0 ) * 4.0 ) )
			if a <= 0:
				img.putpixel( ( x, y ), ( 255, 255, 255, 0 ) )
				continue
			# packed snow: white on top, a blue shade in the dents between the lumps
			shade = 0.78 + 0.16 * grain[y][x] + 0.08 * min( 1.0, ( field - 1.0 ) * 0.5 )
			col = ( int( 232 * shade ), int( 240 * shade ), min( 255, int( 255 * shade ) ) )
			img.putpixel( ( x, y ), col + ( int( 255 * a * ( 0.85 + 0.15 * grain[y][x] ) ), ) )
	return img.filter( ImageFilter.GaussianBlur( 0.6 ) )


def waterstream( w=64, h=32 ):
	"""Across the height the profile of a jet of water: the core lets the light through and shines, the rims are
	darker where the jet bends what is behind it. Along the width it wobbles a little, so it can be scrolled."""
	n = noise( max( w, h ), 8, 41 )
	img = Image.new( 'RGBA', ( w, h ) )
	for y in range( h ):
		v = ( y + 0.5 ) / h * 2 - 1
		for x in range( w ):
			wob = 0.12 * ( n[y % len( n )][x] - 0.5 )
			r = abs( v + wob )
			if r >= 1:
				img.putpixel( ( x, y ), ( 0, 0, 0, 0 ) )
				continue
			core = max( 0.0, 1 - r / 0.35 ) ** 2
			rim = max( 0.0, ( r - 0.55 ) / 0.45 ) * ( 1 - r ) * 4
			body = ( 1 - r * r )
			col = ( int( 150 + 105 * core ), int( 190 + 65 * core ), int( 225 + 30 * core ) )
			alpha = 0.22 * body + 0.55 * core + 0.35 * rim
			img.putpixel( ( x, y ), col + ( int( 255 * min( 1.0, alpha ) ), ) )
	return img


def waterdrop( size=32 ):
	img = Image.new( 'RGBA', ( size, size ) )
	for y in range( size ):
		for x in range( size ):
			dx, dy = ( x + 0.5 ) / size * 2 - 1, ( y + 0.5 ) / size * 2 - 1
			r = math.hypot( dx, dy )
			if r >= 1:
				img.putpixel( ( x, y ), ( 0, 0, 0, 0 ) )
				continue
			glint = max( 0.0, 1 - math.hypot( dx + 0.35, dy + 0.35 ) / 0.3 )
			rim = r ** 3
			v = 0.55 + 0.45 * glint
			img.putpixel( ( x, y ), ( int( 170 * v + 85 * glint ), int( 205 * v + 50 * glint ), int( 240 * v + 15 * glint ),
				int( 255 * min( 1.0, 0.35 + 0.5 * rim + 0.6 * glint ) * min( 1.0, ( 1 - r ) * 6 ) ) ) )
	return img


def patch( size, seed, lumps, edge, dark ):
	"""A dark patch, black with an alpha that is soft and uneven at the rim: the colour of the surface under it
	gets darker, as it does when it is wet."""
	rnd = random.Random( seed )
	n = octaves( size, seed, ( 32, 16, 8, 4 ) )
	blobs = [ ( 0.0, 0.0, 0.5 ) ] + [ ( rnd.uniform( -0.4, 0.4 ), rnd.uniform( -0.4, 0.4 ), rnd.uniform( 0.15, 0.3 ) ) for _ in range( lumps ) ]
	img = Image.new( 'RGBA', ( size, size ) )
	for y in range( size ):
		for x in range( size ):
			dx, dy = ( x + 0.5 ) / size * 2 - 1, ( y + 0.5 ) / size * 2 - 1
			field = sum( br * br / ( ( dx - bx ) ** 2 + ( dy - by ) ** 2 + 1e-4 ) for bx, by, br in blobs )
			field *= 0.75 + 0.5 * n[y][x]
			a = max( 0.0, min( 1.0, ( field - 1.0 ) * edge ) )
			img.putpixel( ( x, y ), ( 0, 0, 0, int( 255 * dark * a * ( 0.85 + 0.15 * n[y][x] ) ) ) )
	return img.filter( ImageFilter.GaussianBlur( 1.2 ) )


def puddleshine( size=256 ):
	"""What a puddle mirrors of the light: soft bright streaks on black, added on top, only inside the puddle (its
	shape, a little smaller, so the light wobbling with tcMod turb stays in it)"""
	n = octaves( size, 61, ( 32, 16, 8 ) )
	mask = patch( size, 83, 6, 6.0, 1.0 ).filter( ImageFilter.GaussianBlur( 6 ) )
	img = Image.new( 'RGBA', ( size, size ) )
	for y in range( size ):
		for x in range( size ):
			v = max( 0.0, n[y][x] - 0.55 ) * 3.5
			v = min( 1.0, v * v ) * ( mask.getpixel( ( x, y ) )[3] / 255.0 ) ** 2
			img.putpixel( ( x, y ), ( int( 180 * v ), int( 200 * v ), int( 220 * v ), 255 ) )
	return img


def main():
	os.makedirs( OUT, exist_ok=True )
	makers = ( ( 'snowball', snowball ), ( 'snowpuff', snowpuff ), ( 'snowsplat', snowsplat ), ( 'waterstream', waterstream ),
		( 'waterdrop', waterdrop ), ( 'wet', lambda: patch( 128, 71, 4, 3.0, 0.4 ) ), ( 'puddle', lambda: patch( 256, 83, 6, 6.0, 0.55 ) ),
		( 'puddleshine', puddleshine ) )
	for name, make in makers:
		path = os.path.join( OUT, name + '.png' )
		make().save( path, optimize=True )
		print( os.path.normpath( path ) )


if __name__ == '__main__':
	main()
