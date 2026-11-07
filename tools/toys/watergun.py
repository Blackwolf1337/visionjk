#!/usr/bin/env python3
"""Makes the water gun of issue #9 (cgame/toys/cg_water.c) from blaster-e of Kenney's Blaster Kit 2.1 (CC0,
www.kenney.nl, also https://opengameart.org/content/blaster-kit):

  models/vision/toys/watergun.md3   the gun, its grip at the origin, the barrel along +x, up along +z, about
                                    WATER_GUN_LENGTH units long; tag_flash where the water leaves the muzzle
  models/vision/toys/watergun.png   the colours of the kit (its colormap)

The kit has the gun with y up and the muzzle towards -z. Faces that meet at a sharp angle keep their own vertices,
so the flat sides of the low poly model stay flat when they are lit. usage:
  tools/toys/watergun.py <Models/OBJ format/blaster-e.obj of the kit>
"""
import math
import os
import shutil
import struct
import sys

OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'models', 'vision', 'toys' )
LENGTH = 30.0				# units from the muzzle to the end of the stock
SHADER = 'models/vision/toys/watergun'
SHARP = math.cos( math.radians( 35.0 ) )	# faces that meet at more than this angle get a hard edge
MAX_VERTS = 999


def read_obj( path, groups=None ):
	"""Positions, texture coordinates and triangles (index of position, index of coordinate, three each). groups:
	a list that gets the name of the group (g) of each triangle."""
	positions, coords, tris = [], [], []
	group = ''
	for line in open( path ):
		p = line.split()
		if not p:
			continue
		if p[0] == 'v':
			positions.append( tuple( map( float, p[1:4] ) ) )
		elif p[0] == 'vt':
			coords.append( tuple( map( float, p[1:3] ) ) )
		elif p[0] == 'g':
			group = p[1] if len( p ) > 1 else ''
		elif p[0] == 'f':
			corners = []
			for c in p[1:]:
				parts = c.split( '/' )
				corners.append( ( int( parts[0] ) - 1, int( parts[1] ) - 1 if len( parts ) > 1 and parts[1] else 0 ) )
			for i in range( 1, len( corners ) - 1 ):
				tris.append( ( corners[0], corners[i], corners[i + 1] ) )
				if groups is not None:
					groups.append( group )
	return positions, coords, tris


def sub( a, b ):
	return ( a[0] - b[0], a[1] - b[1], a[2] - b[2] )


def cross( a, b ):
	return ( a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] )


def normalize( a ):
	n = math.sqrt( a[0] * a[0] + a[1] * a[1] + a[2] * a[2] ) or 1.0
	return ( a[0] / n, a[1] / n, a[2] / n )


def centroid( points ):
	return tuple( sum( p[i] for p in points ) / len( points ) for i in range( 3 ) )


def encode_normal( n ):
	"""The two angles of an MD3 normal as the renderers read them back: the high byte the turn around z, the low byte
	the angle from +z, 256 steps a full circle"""
	turn = int( round( math.atan2( n[1], n[0] ) / ( 2 * math.pi ) * 256 ) ) & 255
	down = int( round( math.acos( max( -1.0, min( 1.0, n[2] ) ) ) / ( 2 * math.pi ) * 256 ) ) & 255
	return ( turn << 8 ) | down


def main():
	if len( sys.argv ) != 2:
		sys.exit( __doc__ )
	obj = sys.argv[1]
	positions, coords, tris = read_obj( obj )

	# the muzzle: the front end of the barrel; the grip: what hangs below the body behind the magazine
	front = min( p[2] for p in positions )
	back = max( p[2] for p in positions )
	muzzle = centroid( [ p for p in positions if p[2] < front + 0.02 ] )
	grip = centroid( [ p for p in positions if p[1] < -0.06 and 0.85 < p[2] < 1.15 ] )
	scale = LENGTH / ( back - front )

	def to_game( p ):
		# forward is -z of the kit, left is -x, up is y; the grip at the origin
		d = sub( p, grip )
		return ( -d[2] * scale, -d[0] * scale, d[1] * scale )

	# one vertex per position, coordinate and group of faces that meet without a hard edge
	face_normals = []
	for tri in tris:
		a, b, c = ( to_game( positions[i] ) for i, _ in tri )
		face_normals.append( normalize( cross( sub( b, a ), sub( c, a ) ) ) )
	by_position = {}
	for f, tri in enumerate( tris ):
		for i, _ in tri:
			by_position.setdefault( i, [] ).append( f )
	# The renderers take at most 999 vertices a surface (SHADER_MAX_VERTEXES): the triangles go into surfaces in
	# their order, a new one where the next triangle would not fit
	surfaces = [ ( [], {}, [] ) ]
	for f, tri in enumerate( tris ):
		verts, index_of, out_tris = surfaces[-1]
		if len( verts ) + 3 > MAX_VERTS:
			surfaces.append( ( [], {}, [] ) )
			verts, index_of, out_tris = surfaces[-1]
		corner_ids = []
		for i, t in tri:
			n = face_normals[f]
			near = [ g for g in by_position[i] if sum( n[k] * face_normals[g][k] for k in range( 3 ) ) > SHARP ]
			smooth = normalize( tuple( sum( face_normals[g][k] for g in near ) for k in range( 3 ) ) )
			key = ( i, t, min( near ) )
			if key not in index_of:
				index_of[key] = len( verts )
				verts.append( ( to_game( positions[i] ), coords[t] if coords else ( 0.0, 0.0 ), smooth ) )
			corner_ids.append( index_of[key] )
		# the kit winds its faces the other way round from the renderers
		out_tris.append( ( corner_ids[0], corner_ids[2], corner_ids[1] ) )

	every = [ v for verts, _, _ in surfaces for v in verts ]
	mins = [ min( v[0][k] for v in every ) for k in range( 3 ) ]
	maxs = [ max( v[0][k] for v in every ) for k in range( 3 ) ]
	radius = max( math.sqrt( sum( c * c for c in v[0] ) ) for v in every )
	flash = to_game( muzzle )

	# a surface: header, shader, triangles, texture coordinates, positions with normals
	surfs = b''
	for number, ( verts, _, out_tris ) in enumerate( surfaces ):
		surf_header = 4 + 64 + 4 * 10
		ofs_shaders = surf_header
		ofs_tris = ofs_shaders + 68
		ofs_st = ofs_tris + 12 * len( out_tris )
		ofs_xyz = ofs_st + 8 * len( verts )
		ofs_surf_end = ofs_xyz + 8 * len( verts )
		surf = struct.pack( '<4s64s10i', b'IDP3', b'watergun%d' % number, 0, 1, 1, len( verts ), len( out_tris ),
			ofs_tris, ofs_shaders, ofs_st, ofs_xyz, ofs_surf_end )
		surf += struct.pack( '<64si', SHADER.encode(), 0 )
		for t in out_tris:
			surf += struct.pack( '<3i', *t )
		for _, st, _ in verts:
			surf += struct.pack( '<2f', st[0], 1.0 - st[1] )
		for pos, _, n in verts:
			surf += struct.pack( '<3hH', *( int( round( c * 64 ) ) for c in pos ), encode_normal( n ) )
		surfs += surf

	frame = struct.pack( '<3f3f3ff16s', *mins, *maxs, 0.0, 0.0, 0.0, radius, b'watergun' )
	tag = struct.pack( '<64s3f9f', b'tag_flash', *flash, 1, 0, 0, 0, 1, 0, 0, 0, 1 )
	header_size = 4 + 4 + 64 + 4 * 9
	ofs_frames = header_size
	ofs_tags = ofs_frames + len( frame )
	ofs_surfs = ofs_tags + len( tag )
	ofs_end = ofs_surfs + len( surfs )
	header = struct.pack( '<4si64siiiiiiiii', b'IDP3', 15, b'models/vision/toys/watergun.md3', 0, 1, 1, len( surfaces ), 0,
		ofs_frames, ofs_tags, ofs_surfs, ofs_end )

	os.makedirs( OUT, exist_ok=True )
	with open( os.path.join( OUT, 'watergun.md3' ), 'wb' ) as f:
		f.write( header + frame + tag + surfs )
	shutil.copyfile( os.path.join( os.path.dirname( obj ), 'Textures', 'colormap.png' ), os.path.join( OUT, 'watergun.png' ) )
	print( '%d vertices, %d triangles in %d surfaces, %.1f units long, muzzle at %.1f %.1f %.1f' % ( len( every ),
		len( tris ), len( surfaces ), maxs[0] - mins[0], *flash ) )


if __name__ == '__main__':
	main()
