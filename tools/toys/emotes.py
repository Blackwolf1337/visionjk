#!/usr/bin/env python3
"""Draws the pieces of the bursts of the interactive emojis of issue #9 (codemp/cgame/toys/cg_emote.c) into
assets/japlus_client/gfx/vision/emote, and their shaders into shaders/vision_emotes.shader:

  sparkle.png   a four-pointed glint with long thin rays and a soft core, added to what is behind it
  ring.png      a thin ring of light with a glow inside, added, for a ring that runs out from the pop
  confetti.png  a scrap of paper with rounded corners, curled, so the light runs across it
  heart.png     a heart, rounded, with a gloss at its top
  star.png      a five-pointed star with bevelled facets
  smoke.png     a soft puff, uneven at its edge, lit from above

All are white and tinted by the colours of the emoji (rgbGen vertex): what the page of the store
(visionjk-website, site/emotes.html) shows in its preview is the same picture, tinted the same way. 256 pixels each
way, drawn four times as fine and shrunk, so the edges are smooth. Noise with a fixed seed, so the pictures come out
the same every time.

usage: tools/toys/emotes.py [--site <folder>]   with --site the pictures are also written there for the page
"""
import argparse
import math
import os

import numpy as np
from PIL import Image

ROOT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client' )
OUT = os.path.join( ROOT, 'gfx', 'vision', 'emote' )
SHADERS = os.path.join( ROOT, 'shaders', 'vision_emotes.shader' )
SIZE = 256
FINE = 4
ADDED = ( 'sparkle', 'ring' )		# added to what is behind them, the others blended by their alpha


def grid():
	"""x and y of every fine pixel, -1 to 1, y up."""
	n = SIZE * FINE
	c = ( np.arange( n ) + 0.5 ) / n * 2.0 - 1.0
	x, y = np.meshgrid( c, -c )
	return x, y


def smooth( edge0, edge1, v ):
	t = np.clip( ( v - edge0 ) / ( edge1 - edge0 ), 0.0, 1.0 )
	return t * t * ( 3.0 - 2.0 * t )


def shrink( rgba ):
	"""The fine picture down to SIZE, each pixel the mean of its FINE x FINE, colour weighed by alpha."""
	r = rgba.reshape( SIZE, FINE, SIZE, FINE, 4 )
	a = r[..., 3].mean( axis=( 1, 3 ) )
	rgb = ( r[..., :3] * r[..., 3:4] ).mean( axis=( 1, 3 ) ) / np.maximum( a[..., None], 1e-6 )
	return np.concatenate( [ rgb, a[..., None] ], axis=-1 )


def value_noise( x, y, scale, seed ):
	rnd = np.random.default_rng( seed )
	cells = int( scale ) + 3
	table = rnd.random( ( cells, cells ) )
	gx, gy = ( x * 0.5 + 0.5 ) * scale, ( y * 0.5 + 0.5 ) * scale
	ix, iy = np.floor( gx ).astype( int ), np.floor( gy ).astype( int )
	fx, fy = gx - ix, gy - iy
	fx, fy = fx * fx * ( 3 - 2 * fx ), fy * fy * ( 3 - 2 * fy )
	a, b = table[iy % cells, ix % cells], table[iy % cells, ( ix + 1 ) % cells]
	c, d = table[( iy + 1 ) % cells, ix % cells], table[( iy + 1 ) % cells, ( ix + 1 ) % cells]
	return ( a * ( 1 - fx ) + b * fx ) * ( 1 - fy ) + ( c * ( 1 - fx ) + d * fx ) * fy


def blur( a, radius ):
	"""A soft copy of a fine picture: shrunk by eight, a box of about radius (of -1 to 1) passed over it three times
	each way, grown back."""
	n = SIZE * FINE // 8
	small = a.reshape( n, 8, n, 8 ).mean( axis=( 1, 3 ) )
	k = max( 1, int( radius * n / 2 / 1.7 ) )
	for _ in range( 3 ):
		for axis in ( 0, 1 ):
			padded = np.pad( small, [ ( k, k ) if i == axis else ( 0, 0 ) for i in range( 2 ) ], mode='edge' )
			c = np.cumsum( padded, axis=axis )
			c = np.concatenate( [ np.zeros_like( np.take( c, [ 0 ], axis=axis ) ), c ], axis=axis )
			small = ( np.take( c, range( 2 * k + 1, c.shape[axis] ), axis=axis ) - np.take( c, range( 0, c.shape[axis] - 2 * k - 1 ), axis=axis ) ) / ( 2 * k + 1 )
	return np.kron( small, np.ones( ( 8, 8 ) ) )


def sparkle():
	x, y = grid()
	r = np.hypot( x, y )
	core = np.exp( -( r / 0.12 ) ** 2 ) + 0.35 * np.exp( -( r / 0.35 ) ** 2 )
	rays = np.exp( -np.abs( y ) / 0.018 ) * np.exp( -np.abs( x ) / 0.42 ) + np.exp( -np.abs( x ) / 0.018 ) * np.exp( -np.abs( y ) / 0.42 )
	u, v = ( x + y ) / math.sqrt( 2 ), ( x - y ) / math.sqrt( 2 )
	rays += 0.35 * ( np.exp( -np.abs( v ) / 0.012 ) * np.exp( -np.abs( u ) / 0.2 ) + np.exp( -np.abs( u ) / 0.012 ) * np.exp( -np.abs( v ) / 0.2 ) )
	light = np.clip( core + rays, 0.0, 1.0 ) * smooth( 1.0, 0.85, r )
	return np.stack( [ light, light, light, light ], axis=-1 )


def ring():
	x, y = grid()
	r = np.hypot( x, y )
	band = np.exp( -( ( r - 0.78 ) / 0.045 ) ** 2 )
	glow = 0.25 * smooth( 0.82, 0.3, r ) * smooth( 0.0, 0.75, r )
	light = np.clip( band + glow, 0.0, 1.0 ) * smooth( 1.0, 0.92, r )
	return np.stack( [ light, light, light, light ], axis=-1 )


def confetti():
	x, y = grid()
	# a scrap 0.5 wide and 0.95 long with rounded corners, a little turned
	a = 0.25
	u, v = x * math.cos( a ) - y * math.sin( a ), x * math.sin( a ) + y * math.cos( a )
	qx, qy = np.maximum( np.abs( u ) - 0.22, 0.0 ), np.maximum( np.abs( v ) - 0.43, 0.0 )
	dist = np.hypot( qx, qy ) - 0.06
	alpha = smooth( 0.01, -0.01, dist )
	# curled along its length: the light runs across it, darker at one edge
	shade = 0.72 + 0.28 * np.cos( ( u / 0.28 ) * 1.4 - 0.5 ) ** 2
	shade *= 1.0 - 0.12 * smooth( -0.03, 0.0, dist )
	return np.stack( [ shade, shade, shade, alpha ], axis=-1 )


def heart():
	x, y = grid()
	hx, hy = x / 0.82, ( y + 0.12 ) / 0.82
	f = ( hx * hx + hy * hy - 1.0 ) ** 3 - hx * hx * hy ** 3
	# the curve as a distance: the value over the length of its slope
	step = 2.0 / ( SIZE * FINE )
	gy, gx = np.gradient( f, step )
	d = f / np.maximum( np.hypot( gx, gy ), 1e-4 )
	alpha = smooth( 0.006, -0.006, d )
	# a dome: its height the outline blurred, lighter towards the middle, a gloss at the upper left, a darker rim
	depth = np.clip( blur( alpha, 0.14 ) * 2.0 - 1.0, 0.0, 1.0 )
	shade = 0.5 + 0.38 * np.sqrt( depth )
	gloss = np.exp( -( ( x + 0.34 ) ** 2 / 0.018 + ( y - 0.34 ) ** 2 / 0.008 ) ) * 0.55
	shade = np.clip( shade + gloss, 0.0, 1.0 )
	return np.stack( [ shade, shade, shade, alpha ], axis=-1 )


def star():
	x, y = grid()
	r = np.hypot( x, y )
	t = np.arctan2( x, y )
	# the edge of a five-pointed star: between the tips at 0.92 and the notches at 0.4
	sector = ( t + math.pi / 5 ) % ( 2 * math.pi / 5 ) - math.pi / 5
	tip, notch = 0.92, 0.4
	half = math.pi / 5
	edge = 1.0 / ( np.cos( sector ) / tip + ( 1.0 / notch - math.cos( half ) / tip ) / math.sin( half ) * np.abs( np.sin( sector ) ) )
	alpha = smooth( 0.012, -0.012, r - edge )
	# bevelled: each point has two facets, the one turned to the light (upper left) brighter
	facet = np.sign( sector )
	normal_angle = t - facet * 0.35
	light = 0.72 + 0.28 * np.cos( normal_angle - ( -0.8 ) )
	ridge = 1.0 - 0.15 * smooth( 0.0, 0.05, np.abs( sector ) * r )
	shade = np.clip( light * ridge + 0.1 * smooth( 0.3, 0.0, r ), 0.0, 1.0 )
	return np.stack( [ shade, shade, shade, alpha ], axis=-1 )


def smoke():
	x, y = grid()
	n = value_noise( x, y, 5, 3 ) * 0.5 + value_noise( x, y, 11, 4 ) * 0.3 + value_noise( x, y, 23, 5 ) * 0.2
	# a cloud of five round lobes, its edge broken by the noise
	lobes = np.zeros_like( x )
	for cx, cy, rad in ( ( 0.0, 0.05, 0.5 ), ( -0.38, -0.12, 0.36 ), ( 0.38, -0.1, 0.38 ), ( -0.15, 0.38, 0.32 ), ( 0.22, 0.34, 0.34 ) ):
		lobes = np.maximum( lobes, smooth( rad + 0.08, rad - 0.2, np.hypot( x - cx, y - cy ) + ( n - 0.5 ) * 0.35 ) )
	alpha = lobes * ( 0.6 + 0.4 * n )
	shade = 0.62 + 0.38 * smooth( -0.7, 0.7, y + ( n - 0.5 ) * 0.4 )
	return np.stack( [ shade, shade, shade, alpha ], axis=-1 )


PIECES = ( ( 'sparkle', sparkle ), ( 'ring', ring ), ( 'confetti', confetti ), ( 'heart', heart ), ( 'star', star ), ( 'smoke', smoke ) )


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument( '--site', help='a folder of the page to write the pictures into as well' )
	args = ap.parse_args()
	os.makedirs( OUT, exist_ok=True )
	for name, draw in PIECES:
		pic = Image.fromarray( ( np.clip( shrink( draw() ), 0.0, 1.0 ) * 255.0 + 0.5 ).astype( np.uint8 ), 'RGBA' )
		pic.save( os.path.join( OUT, name + '.png' ), optimize=True )
		if args.site:
			os.makedirs( args.site, exist_ok=True )
			pic.save( os.path.join( args.site, name + '.png' ), optimize=True )
	with open( SHADERS, 'w', newline='\n' ) as f:
		f.write( '// the pieces of the bursts of the interactive emojis of issue #9 (cgame/toys/cg_emote.c), written by tools/toys/emotes.py\n' )
		for name, _ in PIECES:
			blend = 'GL_ONE GL_ONE' if name in ADDED else 'GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA'
			f.write( '\ngfx/vision/emote/%s\n{\n\tnopicmip\n\t{\n\t\tclampmap gfx/vision/emote/%s\n\t\tblendFunc %s\n'
				'\t\trgbGen vertex\n\t\talphaGen vertex\n\t}\n}\n' % ( name, name, blend ) )
	print( '%d pieces' % len( PIECES ) )


if __name__ == '__main__':
	main()
