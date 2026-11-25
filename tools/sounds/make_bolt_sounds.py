#!/usr/bin/env python3
# The whiz of a bolt flying past the head (issue #22, codemp/cgame/weapons/cg_boltsound.c): whiz1-3.wav, made here so
# no audio of anybody else ships. It is built after the shots of the game, measured 2026-11-25 (blaster/fire.mp3 and
# bryar/fire.mp3 of assets1.pk3): their bolt is a bright tone that falls from about 4 kHz to 900 Hz in 100 ms over a
# body under 2 kHz. The whiz is that tone bent by the Doppler of something passing (high while it comes, low when it
# has gone), with the rush of air of the near miss of a blade (make_saber_sounds.py) under it, quieter than that.
# The howl of a rocket flying past (howl1-2.wav) is built on the loop of the rocket of the game, measured the same
# day (rocket/missleloop.wav of assets0.pk3): a roar of 200 to 450 Hz with tones at 214, 317 and 401 Hz that rattles
# at 89 Hz. The loop is heard with the howl, so the howl is not that roar again (the first one was, and went under in
# it): it is a scream on the third harmonics of those tones, above the roar, bent as the whiz is and more than a
# real rocket would be, and it is at that fall HOWL_PASS seconds in: the cgame starts it this long before the rocket
# is closest (HOWL_PASS_MSEC).
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


def band_noise( hz, q, rng ):
	"""Noise through a band that is at hz (one a sample), a state variable filter."""
	noise = rng.uniform( -1.0, 1.0, hz.size )
	out = np.zeros( hz.size )
	low = band = 0.0
	for i in range( hz.size ):
		f = 2.0 * np.sin( np.pi * min( hz[i], RATE / 6 ) / RATE )
		high = noise[i] - low - band / q
		band += f * high
		low += f * band
		out[i] = band
	return out


def rush( u, start, end, q, rng ):
	"""Air through a band that falls from start to end Hz as the bolt passes."""
	return band_noise( start * ( end / start ) ** ( 0.5 + 0.5 * np.tanh( ( u - 0.45 ) * 7.0 ) ), q, rng )


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


HOWL_PASS = 0.45	# seconds into the howl the rocket is closest, HOWL_PASS_MSEC of cg_boltsound.c


def howl( length, tones, rattle, seed ):
	"""A rocket going past: a scream on the third harmonics of the tones of its loop, where the loop itself has next
	to nothing (a tenth of it lies from 800 to 1600 Hz, a fiftieth above), 1.45 times as high while it comes and 0.65
	times when it has gone, so it falls through an octave into the roar. It rattles as the loop does, air rushes with
	it, and a little of the roar is under it. Loudest as it passes, gone a little slower than it came."""
	rng = np.random.default_rng( seed )
	n = int( length * RATE )
	t = np.arange( n ) / RATE
	bend = 1.45 - 0.8 * ( 0.5 + 0.5 * np.tanh( ( t - HOWL_PASS ) / 0.07 ) )
	tone = np.zeros( n )
	for k, ( hz, gain ) in enumerate( tones ):
		wobble = 1.0 + 0.008 * np.sin( 2.0 * np.pi * ( 6.0 + k ) * t + k )
		phase = 2.0 * np.pi * np.cumsum( 3.0 * hz * bend * wobble ) / RATE + 1.7 * k
		tone += gain * ( np.sin( phase ) + 0.3 * np.sin( 2.0 * phase ) )
	air = band_noise( 2400.0 * bend, 1.5, rng )
	roar = band_noise( 300.0 * bend, 1.1, rng )
	both = 0.7 * tone / np.max( np.abs( tone ) ) + 0.3 * air / np.max( np.abs( air ) ) + 0.2 * roar / np.max( np.abs( roar ) )
	both *= 0.7 + 0.3 * np.sin( 2.0 * np.pi * np.cumsum( rattle * bend ) / RATE )
	away = np.where( t < HOWL_PASS, 0.16, 0.26 )
	env = 1.0 / np.sqrt( 1.0 + ( ( t - HOWL_PASS ) / away ) ** 2 )
	env *= np.minimum( t / 0.06, 1.0 ) * np.minimum( ( length - t ) / 0.25, 1.0 )
	return both * env


os.makedirs( OUT, exist_ok=True )
# where the tone of each is before it is bent: from 1.5 times that while it comes to 0.6 times when it has gone, the
# fall of the bolts of the game
for k, ( length, hz, buzz ) in enumerate( ( ( 0.26, 2300.0, 95.0 ), ( 0.24, 2600.0, 110.0 ), ( 0.29, 2050.0, 85.0 ) ) ):
	write( 'whiz%d.wav' % ( k + 1 ), whiz( length, hz, buzz, 501 + k ), -10.0 )
# the tones of the loop of the game, the second a little lower and longer. Louder than the loop (-9 dB), which is
# heard with it.
write( 'howl1.wav', howl( 1.15, ( ( 214.0, 1.0 ), ( 317.0, 0.8 ), ( 401.0, 0.5 ) ), 89.0, 601 ), -5.0 )
write( 'howl2.wav', howl( 1.25, ( ( 196.0, 1.0 ), ( 290.0, 0.8 ), ( 368.0, 0.5 ) ), 81.0, 602 ), -5.0 )
