#!/usr/bin/env python3
# The sounds of the toys of issue #9, made here so no audio of anybody else ships: emote_send.wav (a soft rising
# whoosh where an emoji leaves, codemp/cgame/toys/cg_emote.c) and emote_pop.wav (a bubbly pop with a little chime
# where it arrives), pong_hit.wav (the knock of a paddle on the ball, cg_pong.c) and pong_bounce.wav (the higher,
# shorter tick of the ball on the table). 22050 Hz, 16 bit, mono, as the sounds of the game. Noise with a fixed seed, so the
# files come out the same every time. Writes into assets/japlus_client/sound/vision/toys/.
# usage: tools/toys/sounds.py
import math
import os
import random
import struct
import wave

RATE = 22050
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'sound', 'vision', 'toys' )


def write( name, samples ):
	peak = max( abs( s ) for s in samples ) or 1.0
	with wave.open( os.path.join( OUT, name ), 'wb' ) as w:
		w.setnchannels( 1 )
		w.setsampwidth( 2 )
		w.setframerate( RATE )
		w.writeframes( b''.join( struct.pack( '<h', int( s / peak * 0.85 * 32767 ) ) for s in samples ) )


def bandpass( samples, freq_of, q ):
	"""A state variable filter whose centre follows freq_of(t)."""
	low = band = 0.0
	out = []
	for i, x in enumerate( samples ):
		f = 2.0 * math.sin( math.pi * min( freq_of( i / RATE ), RATE / 6 ) / RATE )
		high = x - low - band / q
		band += f * high
		low += f * band
		out.append( band )
	return out


def send():
	"""Air that rises in pitch and fades: 0.32 s."""
	rnd = random.Random( 9 )
	length = 0.32
	n = int( length * RATE )
	noise = [ rnd.uniform( -1.0, 1.0 ) for _ in range( n ) ]
	air = bandpass( noise, lambda t: 500.0 * ( 5.0 ** ( t / length ) ), 3.0 )
	out = []
	for i, v in enumerate( air ):
		t = i / RATE
		env = math.sin( math.pi * min( 1.0, t / length ) ) ** 1.5
		out.append( v * env )
	return out


def pop():
	"""A bubble that pops (a sine sweeping up fast), a click, and a chime of two bells: 0.45 s."""
	rnd = random.Random( 5 )
	length = 0.45
	out = []
	phase = 0.0
	for i in range( int( length * RATE ) ):
		t = i / RATE
		freq = 380.0 + 900.0 * ( 1.0 - math.exp( -t / 0.025 ) )
		phase += 2.0 * math.pi * freq / RATE
		v = math.sin( phase ) * math.exp( -t / 0.045 ) * min( 1.0, t / 0.002 )
		if t < 0.004:
			v += rnd.uniform( -0.5, 0.5 ) * ( 1.0 - t / 0.004 )
		u = t - 0.035
		if u > 0.0:
			bell = math.sin( 2.0 * math.pi * 1568.0 * u ) + 0.6 * math.sin( 2.0 * math.pi * 2349.3 * u ) \
				+ 0.25 * math.sin( 2.0 * math.pi * 4186.0 * u )
			v += 0.35 * bell * math.exp( -u / 0.11 ) * min( 1.0, u / 0.003 )
		out.append( v )
	return out


def knock( freq, decay, length, seed ):
	"""A hollow knock: a click of noise and two partials of a small wooden body that die away fast."""
	rnd = random.Random( seed )
	out = []
	for i in range( int( length * RATE ) ):
		t = i / RATE
		v = math.sin( 2.0 * math.pi * freq * t ) + 0.5 * math.sin( 2.0 * math.pi * freq * 2.7 * t )
		v *= math.exp( -t / decay ) * min( 1.0, t / 0.0005 )
		if t < 0.003:
			v += rnd.uniform( -0.8, 0.8 ) * ( 1.0 - t / 0.003 )
		out.append( v )
	return out


os.makedirs( OUT, exist_ok=True )
write( 'emote_send.wav', send() )
write( 'emote_pop.wav', pop() )
write( 'pong_hit.wav', knock( 1150.0, 0.018, 0.12, 7 ) )
write( 'pong_bounce.wav', knock( 1900.0, 0.010, 0.08, 8 ) )
