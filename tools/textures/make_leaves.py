#!/usr/bin/env python3
# The leaves a Force Push or an explosion throws up from a floor of plants (issue #23, codemp/cgame/level/cg_gust.c):
# four shapes in a 2 x 2 sheet, light grey with a midrib and veins, tinted by the game; alpha cut at half. Made here so
# no picture of anybody else ships. Fixed seed, so the file comes out the same every time. Writes
# assets/japlus_client/gfx/vision/leaves.tga.
# usage: tools/textures/make_leaves.py
import os
import struct

import numpy as np

CELL = 64		# a leaf; the sheet is twice that (a power of two)
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'gfx', 'vision', 'leaves.tga' )

# half width along the stem (t from 0 at the stalk to 1 at the tip), as a part of the cell
SHAPES = (
	lambda t: 0.19 * np.sin( np.pi * t ) ** 0.7,								# slender
	lambda t: 0.36 * np.sin( np.pi * t ** 0.8 ) ** 0.9,							# broad, widest near the stalk
	lambda t: 0.3 * np.sin( np.pi * t ) ** 0.6 * ( 0.78 + 0.22 * np.cos( t * 21.0 ) ),	# lobed
	lambda t: 0.27 * np.sin( np.pi * t ) ** 0.45,								# round
)


def leaf( shape, rng ):
	axis = ( np.arange( CELL ) + 0.5 ) / CELL
	y, x = np.meshgrid( axis, axis, indexing='ij' )
	t = np.clip( ( x - 0.08 ) / 0.84, 0.0, 1.0 )
	half = shape( t )
	off = np.abs( y - 0.5 )
	inside = ( x > 0.08 ) & ( x < 0.92 )
	alpha = np.clip( ( half - off ) * CELL * 0.9 + 0.5, 0.0, 1.0 ) * inside
	# the stalk
	alpha = np.maximum( alpha, np.clip( ( 0.022 - off ) * CELL + 0.5, 0.0, 1.0 ) * ( x < 0.2 ) * ( x > 0.01 ) )
	# the midrib and the veins that leave it towards the tip
	rib = np.exp( -( off * CELL / 1.3 ) ** 2 )
	veins = np.sin( ( x * 9.0 - off * 6.0 ) * 2.0 * np.pi ) ** 8 * np.clip( off / np.maximum( half, 1e-3 ), 0.0, 1.0 )
	shade = 0.86 - 0.3 * rib - 0.14 * veins + 0.1 * ( y - 0.5 ) + rng.normal( 0.0, 0.025, ( CELL, CELL ) )
	return np.clip( shade, 0.0, 1.0 ), alpha


def main():
	rng = np.random.default_rng( 2311 )
	pixels = np.zeros( ( CELL * 2, CELL * 2, 4 ), dtype=np.uint8 )
	for i, shape in enumerate( SHAPES ):
		shade, alpha = leaf( shape, rng )
		cell = pixels[( i >> 1 ) * CELL:( ( i >> 1 ) + 1 ) * CELL, ( i & 1 ) * CELL:( ( i & 1 ) + 1 ) * CELL]
		cell[:, :, 0:3] = ( shade * 255.0 + 0.5 ).astype( np.uint8 )[:, :, None]
		cell[:, :, 3] = ( alpha * 255.0 + 0.5 ).astype( np.uint8 )
	# (where it is cut away the colour of the leaf stays, so its edge does not go dark when the picture is made smaller)
	pixels[:, :, 0:3][pixels[:, :, 3] == 0] = 200
	with open( OUT, 'wb' ) as f:
		f.write( struct.pack( '<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, CELL * 2, CELL * 2, 32, 8 ) )
		f.write( pixels.tobytes() )


if __name__ == '__main__':
	main()
