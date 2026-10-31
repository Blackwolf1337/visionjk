#!/usr/bin/env python3
"""Writes the sprays of issue #9 (cgame/toys/cg_spray.c) from the emoji of the chat (assets/japlus_client/gfx/emoji):

  gfx/vision/sprays.txt         the pictures, one file name a line: a spray goes to the others as its line number
  shaders/vision_sprays.shader  a shader for each line, gfx/vision/spray/<number>: the picture on a wall, with its
                                alpha, lifted off the wall (polygonOffset) and as bright as the light there

Run it again when pictures are added to gfx/emoji: the numbers follow the sorted names, so a picture added in the
middle moves the ones after it, which only matters between clients of different versions.
usage: tools/toys/sprays.py
"""
import os

ASSETS = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client' )
MOST = 1400		# TOY_SPRAY_MOST of game/toys/bg_toys.h


def main():
	pictures = sorted( n for n in os.listdir( os.path.join( ASSETS, 'gfx', 'emoji' ) ) if n.lower().endswith( '.png' ) )
	if len( pictures ) > MOST:
		raise SystemExit( 'more than %d pictures' % MOST )
	if any( c.isspace() or c == '"' for n in pictures for c in n ):
		raise SystemExit( 'a picture name with a blank or a quote' )
	with open( os.path.join( ASSETS, 'gfx', 'vision', 'sprays.txt' ), 'w', newline='\n' ) as f:
		f.write( ''.join( n + '\n' for n in pictures ) )
	with open( os.path.join( ASSETS, 'shaders', 'vision_sprays.shader' ), 'w', newline='\n' ) as f:
		f.write( '// the sprays of issue #9 (cgame/toys/cg_spray.c), written by tools/toys/sprays.py from gfx/vision/sprays.txt\n' )
		for i, name in enumerate( pictures ):
			f.write( '\ngfx/vision/spray/%d\n{\n\tnopicmip\n\tpolygonOffset\n\t{\n\t\tclampmap gfx/emoji/%s\n'
				'\t\tblendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n\t\trgbGen vertex\n\t\talphaGen vertex\n\t}\n}\n' % ( i, name ) )
	print( '%d sprays' % len( pictures ) )


if __name__ == '__main__':
	main()
