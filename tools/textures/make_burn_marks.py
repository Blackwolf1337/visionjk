#!/usr/bin/env python3
# The pictures of the burn wounds of a blade (issue #20, codemp/cgame/saber/cg_saberwound.c): a white core and an orange
# glow, both on black for adding, and a dark char with alpha. Made here so no picture of anybody else ships. Noise with
# a fixed seed, so the files come out the same every time. Writes assets/japlus_client/gfx/vision/burn_*.tga.
# usage: tools/textures/make_burn_marks.py
import os
import struct

import numpy as np

SIZE = 128		# (the renderer refuses sizes that are no power of two)
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'gfx', 'vision' )


def noise( seed, cells ):
	"""Smooth noise, 0 to 1: a coarse random grid blown up."""
	rng = np.random.default_rng( seed )
	grid = rng.random( ( cells + 1, cells + 1 ) )
	u = np.linspace( 0.0, cells, SIZE, endpoint=False )
	i = u.astype( int )
	f = u - i
	f = f * f * ( 3.0 - 2.0 * f )
	a = grid[np.ix_( i, i )]
	b = grid[np.ix_( i + 1, i )]
	c = grid[np.ix_( i, i + 1 )]
	d = grid[np.ix_( i + 1, i + 1 )]
	fx, fy = np.meshgrid( f, f )
	return ( a * ( 1 - fy ) + b * fy ) * ( 1 - fx ) + ( c * ( 1 - fy ) + d * fy ) * fx


def shape( width, height, seed ):
	"""1 in the middle of a ragged, somewhat long patch, 0 at its edge; never reaches the border (the marks are clamped)."""
	v = np.linspace( -1.0, 1.0, SIZE )
	x, y = np.meshgrid( v, v )
	rough = 0.65 * noise( seed, 5 ) + 0.35 * noise( seed + 1, 13 )
	d = np.sqrt( ( x / width ) ** 2 + ( y / height ) ** 2 ) + ( rough - 0.5 ) * 0.7
	edge = np.sqrt( x * x + y * y )
	return np.clip( 1.0 - d, 0.0, 1.0 ) * np.clip( ( 1.0 - edge ) / 0.12, 0.0, 1.0 )


def write_tga( name, rgba ):
	"""Uncompressed, 32 bit, top row first."""
	data = ( np.clip( rgba, 0.0, 1.0 ) * 255.0 + 0.5 ).astype( np.uint8 )[:, :, [2, 1, 0, 3]]
	with open( os.path.join( OUT, name ), 'wb' ) as f:
		f.write( struct.pack( '<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, SIZE, SIZE, 32, 0x28 ) )
		f.write( data.tobytes() )


os.makedirs( OUT, exist_ok=True )
core = np.clip( shape( 0.74, 0.16, 501 ) * 3.0, 0.0, 1.0 )
write_tga( 'burn_core.tga', np.dstack( ( core, core * 0.92, core * 0.66, np.ones_like( core ) ) ) )
glow = np.clip( shape( 0.88, 0.3, 501 ) * 2.0, 0.0, 1.0 )
write_tga( 'burn_glow.tga', np.dstack( ( glow, glow * glow * 0.42, glow ** 3 * 0.08, np.ones_like( glow ) ) ) )
char = np.clip( shape( 0.94, 0.4, 501 ) * 1.5, 0.0, 1.0 ) ** 1.2
flecks = 0.07 + 0.09 * noise( 511, 19 )
write_tga( 'burn_char.tga', np.dstack( ( flecks * 1.15, flecks, flecks * 0.85, char * 0.68 ) ) )
