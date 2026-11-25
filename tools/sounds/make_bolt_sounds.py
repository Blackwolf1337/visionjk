#!/usr/bin/env python3
# The whiz of a bolt flying past the head (issue #22, codemp/cgame/weapons/cg_boltsound.c): whiz1-3.wav, made here so
# no audio of anybody else ships. It is built after the shots of the game, measured 2026-11-25 (blaster/fire.mp3 and
# bryar/fire.mp3 of assets1.pk3): their bolt is a bright tone that falls from about 4 kHz to 900 Hz in 100 ms over a
# body under 2 kHz. The whiz is that tone bent by the Doppler of something passing (high while it comes, low when it
# has gone), with the rush of air of the near miss of a blade (make_saber_sounds.py) under it, quieter than that.
# 22050 Hz, 16 bit, mono, as the sounds of the game. Noise with a fixed seed, so the files come out the same every
# time. Writes into assets/japlus_client/sound/vision/bolt/.
# usage: tools/sounds/make_bolt_sounds.py
import os
import wave

import numpy as np

RATE = 22050
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'sound', 'vision', 'bolt' )


def write( name, samples, target ):
	"""As loud as target dB in its loudest 50 ms, the peaks taken by a soft limiter, with a short fade at its end."""
	samples = samples / np.max( np.abs( samples ) )
	n = int( 0.05 * RATE )

	def loudest( s ):
		return 10.0 * np.log10( np.max( np.convolve( s * s, np.ones( n ) / n, 'valid' ) ) )

	low, high = 0.5, 40.0
	for _ in range( 32 ):
		gain = ( low + high ) * 0.5
		if loudest( 0.94 * np.tanh( gain * samples ) ) < target:
			low = gain
		else:
			high = gain
	samples = 0.94 * np.tanh( low * samples )
	samples[-64:] *= np.linspace( 1.0, 0.0, 64 )
	with wave.open( os.path.join( OUT, name ), 'wb' ) as w:
		w.setnchannels( 1 )
		w.setsampwidth( 2 )
		w.setframerate( RATE )
		w.writeframes( ( samples * 32767.0 ).astype( '<i2' ).tobytes() )


def rush( u, start, end, q, rng ):
	"""Air through a band that falls from start to end Hz as the bolt passes (a state variable filter on noise)."""
	noise = rng.uniform( -1.0, 1.0, u.size )
	out = np.zeros( u.size )
	low = band = 0.0
	for i in range( u.size ):
		hz = start * ( end / start ) ** ( 0.5 + 0.5 * np.tanh( ( u[i] - 0.45 ) * 7.0 ) )
		f = 2.0 * np.sin( np.pi * min( hz, RATE / 6 ) / RATE )
		high = noise[i] - low - band / q
		band += f * high
		low += f * band
		out[i] = band
	return out


def whiz( length, hz, buzz, seed ):
	"""A bolt going past the ear: its tone a fifth up while it comes and down to 0.6 of it when it has gone, a hum
	of buzz Hz on it as the bolts of the game have, loudest a little before the middle."""
	rng = np.random.default_rng( seed )
	n = int( length * RATE )
	u = np.arange( n ) / n
	bend = 1.5 - 0.9 * ( 0.5 + 0.5 * np.tanh( ( u - 0.45 ) * 8.0 ) )
	phase = 2.0 * np.pi * np.cumsum( hz * bend ) / RATE
	tone = np.sin( phase ) + 0.35 * np.sin( 2.0 * phase + 0.4 ) + 0.12 * np.sin( 3.0 * phase + 1.1 )
	tone *= 0.75 + 0.25 * np.sin( 2.0 * np.pi * buzz * np.arange( n ) / RATE )
	env = np.exp( -( ( u - 0.42 ) / 0.17 ) ** 2 ) * np.minimum( ( 1.0 - u ) / 0.1, 1.0 ) * np.minimum( u / 0.05, 1.0 )
	air = rush( u, 3200.0, 900.0, 1.6, rng )
	air_env = np.exp( -( ( u - 0.45 ) / 0.22 ) ** 2 )
	return 0.62 * tone / np.max( np.abs( tone ) ) * env + 0.45 * air / np.max( np.abs( air ) ) * air_env


os.makedirs( OUT, exist_ok=True )
# where the tone of each is before it is bent: from 1.5 times that while it comes to 0.6 times when it has gone, the
# fall of the bolts of the game
for k, ( length, hz, buzz ) in enumerate( ( ( 0.26, 2300.0, 95.0 ), ( 0.24, 2600.0, 110.0 ), ( 0.29, 2050.0, 85.0 ) ) ):
	write( 'whiz%d.wav' % ( k + 1 ), whiz( length, hz, buzz, 501 + k ), -10.0 )
