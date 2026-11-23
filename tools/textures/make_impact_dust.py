#!/usr/bin/env python3
# The puff of dust a shot knocks off a wall (issue #22, codemp/cgame/weapons/cg_impact.c): white with a soft, uneven
# alpha, tinted by the game. Made here so no picture of anybody else ships. Noise with a fixed seed, so the file comes
# out the same every time. Writes assets/japlus_client/gfx/vision/impact_dust.tga.
# usage: tools/textures/make_impact_dust.py
import os
import struct

import numpy as np

SIZE = 64		# (the renderer refuses sizes that are no power of two)
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'gfx', 'vision', 'impact_dust.tga' )


def noise( rng, cells ):
	"""Smooth noise, 0 to 1: a coarse random grid blown up."""
	grid = rng.random( ( cells + 1, cells + 1 ) )
	u = np.linspace( 0.0, cells, SIZE, endpoint=False )
	i = u.astype( int )
	f = u - i
	f = f * f * ( 3.0 - 2.0 * f )
	top = grid[np.ix_( i, i )] * ( 1.0 - f )[:, None] + grid[np.ix_( i + 1, i )] * f[:, None]
	low = grid[np.ix_( i, i + 1 )] * ( 1.0 - f )[:, None] + grid[np.ix_( i + 1, i + 1 )] * f[:, None]
	return top * ( 1.0 - f )[None, :] + low * f[None, :]


def main():
	rng = np.random.default_rng( 2227 )
	axis = ( np.arange( SIZE ) + 0.5 ) / SIZE * 2.0 - 1.0
	y, x = np.meshgrid( axis, axis, indexing='ij' )
	cloud = 0.5 * noise( rng, 3 ) + 0.3 * noise( rng, 6 ) + 0.2 * noise( rng, 12 )
	radius = np.sqrt( x * x + y * y ) + ( cloud - 0.5 ) * 0.55
	alpha = np.clip( 1.0 - radius / 0.86, 0.0, 1.0 ) ** 1.5
	alpha *= 0.55 + 0.45 * cloud
	edge = np.clip( ( 1.0 - np.sqrt( x * x + y * y ) ) / 0.12, 0.0, 1.0 )
	alpha = np.clip( alpha * edge * 1.5, 0.0, 1.0 )
	pixels = np.full( ( SIZE, SIZE, 4 ), 255, dtype=np.uint8 )
	pixels[:, :, 3] = ( alpha * 255.0 + 0.5 ).astype( np.uint8 )
	with open( OUT, 'wb' ) as f:
		f.write( struct.pack( '<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, SIZE, SIZE, 32, 8 ) )
		f.write( pixels.tobytes() )


if __name__ == '__main__':
	main()
