#!/usr/bin/env python3
"""Makes the packs of the TV in the browser (issue #7): the files of the game a viewer needs, per map, encrypted.

usage: tools/tvpack/tvpack.py --game <folder of a Linux build> --out <folder> [--maps mp|all|mp/ffa1,mp/ffa3]
                               [--bots "Kyle Katarn,Jan Ors"] [--seconds 20] [--work <folder>] [--no-record]

The page of the browser client in TV mode (shared/sys/web/page/boot.js, ?tv) does not load the pk3 files of the
server: it loads tv-common.vpk and the pack of the map that runs, both made here. Which files go in is not worked
out from the maps but seen: a dedicated server of the build plays every map with bots and records it with
tvrecord, the client of the build plays those demos headless with fs_debug 1, and every file it read from a pk3
is noted with the map it was read for. A file read before the first map, or for two maps or more, goes into the
common pack, the others into the pack of their map. Sounds, music and videos are left out: the TV of the page
has no sound. A player whose model is not in the packs is drawn with the default model.

A pack is a pk3 (zip), encrypted with AES-128-CTR: "VJKTV1" and two zero bytes, the 16 bytes of the counter it
starts with, then the zip. The key is TV_PACK_KEY below and in boot.js. That keeps the files of the game from being
taken off the site as they are; whoever reads the page finds the key. tv-index.json names the packs, their size
and SHA-256, so the page fetches a pack again only when it changed.

Needs gamescope (the client runs headless, see docs/japlus/testing.md) and openssl.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

TV_PACK_KEY = '5a1c9e3b07d24f68a1b2c3d4e5f60718'
PACK_MAGIC = b'VJKTV1\0\0'
DEFAULT_BOTS = 'Kyle Katarn,Jan Ors,Jedi,Tavion,Reborn,Stormtrooper,Luke Skywalker,Desann'
LEFT_OUT = ( 'sound/', 'music/', 'video/' )
# a file of a pk3 (found in 'base/assets1.pk3') or of a folder (found in './japlus')
READ_LINE = re.compile( r"FS_FOpenFileRead: (.+?) \(found in '(.+?)'\)" )
MARK = 'TVPACK_MAP '
START = 'TVPACK_START'


def pk3s( game ):
	for folder in ( 'base', 'japlus' ):
		path = os.path.join( game, folder )
		for name in sorted( os.listdir( path ) ) if os.path.isdir( path ) else ():
			if name.lower().endswith( '.pk3' ):
				yield os.path.join( path, name )


def all_maps( game, multiplayer ):
	"""the maps of the pk3 files of the game; multiplayer: only those under maps/mp (the others are the levels of
	the single player game)"""
	maps = set()
	for pk3 in pk3s( game ):
		with zipfile.ZipFile( pk3 ) as z:
			for name in z.namelist():
				m = re.fullmatch( r'maps/(.+)\.bsp', name, re.I )
				if m and ( not multiplayer or m.group( 1 ).lower().startswith( 'mp/' ) ):
					maps.add( m.group( 1 ).lower() )
	return sorted( maps )


def demo_name( mapname ):
	return 'tvpack_' + re.sub( r'[^a-z0-9]', '_', mapname )


def record( game, home, maps, bots, seconds ):
	"""Every map with the bots, recorded by the TV of the server into <home>/japlus/demos."""
	server = subprocess.Popen( [ './visionjkded.x86_64', '+set', 'fs_homepath', home, '+set', 'fs_game', 'japlus',
		'+set', 'dedicated', '1', '+set', 'net_port', '29591', '+set', 'sv_maxclients', '16', '+set', 'g_gametype', '0',
		'+set', 'fraglimit', '0', '+set', 'timelimit', '0', '+set', 'bot_enable', '1' ], cwd=game,
		stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True )

	def say( line, wait ):
		server.stdin.write( line + '\n' )
		server.stdin.flush()
		time.sleep( wait )

	try:
		for i, mapname in enumerate( maps ):
			print( f'record {i + 1}/{len( maps )}: {mapname}', flush=True )
			say( 'map ' + mapname, 8 )
			for bot in bots:
				say( f'addbot "{bot}" 2', 0.5 )
			say( 'tvrecord ' + demo_name( mapname ), seconds )
			say( 'tvstop', 1 )
		say( 'quit', 3 )
	finally:
		if server.poll() is None:
			server.kill()


def play( game, home, maps, seconds, log ):
	"""Each demo in a client of its own with fs_debug 1, so nothing a map before loaded is still in memory (the
	models stay from one level to the next), with a mark before it. Returns the lines of their consoles, each
	client beginning with START. What the client prints is read from its output: its log file opens only after
	mpdefault.cfg was read."""
	env = dict( os.environ, SDL_AUDIODRIVER='dummy' )
	lines = []
	with open( log, 'w', encoding='latin-1' ) as out:
		for i, mapname in enumerate( maps ):
			print( f'play {i + 1}/{len( maps )}: {mapname}', flush=True )
			with open( os.path.join( home, 'japlus', 'tvpack.cfg' ), 'w' ) as f:
				f.write( f'wait 120; echo {MARK}{mapname}; demo {demo_name( mapname )}; wait {( seconds + 10 ) * 60}; quit\n' )
			result = subprocess.run( [ 'timeout', '-k', '3', str( seconds + 60 ), 'gamescope', '--backend', 'headless', '-W', '1280',
				'-H', '720', '--', './visionjk.x86_64', '+set', 'fs_homepath', home, '+set', 'fs_game', 'japlus',
				'+set', 'fs_debug', '1', '+set', 'logfile', '2', '+set', 'cl_updateCheck', '0', '+set', 'com_maxfps', '60',
				'+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1', '+set', 'r_customwidth', '1280', '+set', 'r_customheight',
				'720', '+exec', 'tvpack.cfg' ], cwd=game, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT )
			lines.append( START )
			lines += result.stdout.decode( 'latin-1' ).splitlines()
		out.write( '\n'.join( lines ) + '\n' )
	return lines


def sort_files( lines, maps, game ):
	"""{file: where} of the startup and per map, from the console of the client. where is the pk3, or the path of
	a file of the folders of the game itself (base, japlus); a file of the home path (the demos) is left out."""
	startup, per_map, current = {}, { m: {} for m in maps }, None
	for line in lines:
		if line == START:
			current = None
			continue
		at = line.find( MARK )
		if at >= 0 and not line.startswith( 'echo' ):
			name = line[at + len( MARK ):].strip()
			current = name if name in per_map else current
			continue
		m = READ_LINE.search( line )
		if not m:
			continue
		name, found = m.group( 1 ).replace( '\\', '/' ), os.path.normpath( os.path.join( game, m.group( 2 ) ) )
		if name.lower().startswith( LEFT_OUT ):
			continue
		if not found.lower().endswith( '.pk3' ):
			if os.path.dirname( found ) != game:
				continue
			found = os.path.join( found, name )
		( per_map[current] if current else startup ).setdefault( name, found )
	return startup, per_map


def encrypt( data ):
	iv = os.urandom( 16 )
	out = subprocess.run( [ 'openssl', 'enc', '-aes-128-ctr', '-K', TV_PACK_KEY, '-iv', iv.hex() ], input=data,
		capture_output=True, check=True ).stdout
	return PACK_MAGIC + iv + out


def write_pack( path, files ):
	"""files: {name: pk3 or path of the file}. The zip as the engine reads a pk3, encrypted. Returns its entry for the
	index."""
	tmp = path + '.zip'
	opened = {}
	with zipfile.ZipFile( tmp, 'w', zipfile.ZIP_DEFLATED ) as out:
		for name, pk3 in sorted( files.items() ):
			if not pk3.lower().endswith( '.pk3' ):
				out.write( pk3, name )
				continue
			z = opened.get( pk3 ) or opened.setdefault( pk3, zipfile.ZipFile( pk3 ) )
			try:
				data = z.read( name )
			except KeyError:
				# the engine looks a name up without regard to case
				match = next( ( n for n in z.namelist() if n.lower() == name.lower() ), None )
				if not match:
					continue
				data = z.read( match )
			out.writestr( name, data )
	for z in opened.values():
		z.close()
	with open( tmp, 'rb' ) as f:
		blob = encrypt( f.read() )
	os.remove( tmp )
	with open( path, 'wb' ) as f:
		f.write( blob )
	return { 'file': os.path.basename( path ), 'size': len( blob ), 'sha256': hashlib.sha256( blob ).hexdigest(),
		'files': len( files ) }


def main():
	ap = argparse.ArgumentParser( description=__doc__.split( '\n' )[0] )
	ap.add_argument( '--game', required=True, help='folder of a Linux build: visionjkded.x86_64, visionjk.x86_64, base, japlus' )
	ap.add_argument( '--out', required=True )
	ap.add_argument( '--maps', default='mp', help='mp: every map under maps/mp, all: every map, or a list' )
	ap.add_argument( '--bots', default=DEFAULT_BOTS )
	ap.add_argument( '--seconds', type=int, default=20, help='how long each map is recorded' )
	ap.add_argument( '--work', help='folder for the home path, the demos and the log (default: <out>/work)' )
	ap.add_argument( '--no-record', action='store_true', help='play the demos of an earlier run of the same work folder' )
	args = ap.parse_args()

	game = os.path.abspath( args.game )
	out = os.path.abspath( args.out )
	work = os.path.abspath( args.work or os.path.join( out, 'work' ) )
	home = os.path.join( work, 'home' )
	os.makedirs( os.path.join( home, 'japlus' ), exist_ok=True )
	maps = all_maps( game, args.maps == 'mp' ) if args.maps in ( 'mp', 'all' ) else [ m.strip().lower() for m in args.maps.split( ',' ) if m.strip() ]
	bots = [ b.strip() for b in args.bots.split( ',' ) if b.strip() ]

	if not args.no_record:
		record( game, home, maps, bots, args.seconds )
	lines = play( game, home, maps, args.seconds, os.path.join( work, 'client.log' ) )
	startup, per_map = sort_files( lines, maps, game )

	seen = {}
	for files in per_map.values():
		for name in files:
			seen[name] = seen.get( name, 0 ) + 1
	common = dict( startup )
	for files in per_map.values():
		for name, pk3 in files.items():
			if seen[name] > 1:
				common.setdefault( name, pk3 )

	index = { 'version': 1, 'common': write_pack( os.path.join( out, 'tv-common.vpk' ), common ), 'maps': {} }
	print( f"common: {len( common )} files, {index['common']['size'] / 1e6:.1f} MB" )
	for mapname, files in per_map.items():
		own = { n: p for n, p in files.items() if n not in common }
		if not any( n.lower() == f'maps/{mapname}.bsp' for n in files ):
			print( f'{mapname}: not played (no bsp read), left out' )
			continue
		entry = write_pack( os.path.join( out, f"tv-{demo_name( mapname )[len( 'tvpack_' ):]}.vpk" ), own )
		index['maps'][mapname] = entry
		print( f"{mapname}: {len( own )} files, {entry['size'] / 1e6:.1f} MB" )
	with open( os.path.join( out, 'tv-index.json' ), 'w' ) as f:
		json.dump( index, f, indent=1, sort_keys=True )
	return 0


if __name__ == '__main__':
	sys.exit( main() )
