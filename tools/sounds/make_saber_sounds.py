#!/usr/bin/env python3
# The sounds of SmoothSwing (issue #20, codemp/cgame/saber/cg_sabersound.c), made here so no audio of anybody else
# ships: swing_low.wav and swing_high.wav (two seamless loops the cgame crossfades over the hum by how far the blade
# turned) and nearmiss1-4.wav. The swings of the stances and the ignitions are the sounds of the game itself, the
# cgame only picks among them. 22050 Hz, 16 bit, mono, as the sounds of the
# game. Noise with a fixed seed, so the files come out the same every time. Writes into
# assets/japlus_client/sound/vision/saber/.
# usage: tools/sounds/make_saber_sounds.py
import os
import wave

import numpy as np

RATE = 22050
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'sound', 'vision', 'saber' )


def write( name, samples, level ):
	samples = samples / ( np.max( np.abs( samples ) ) or 1.0 ) * level
	with wave.open( os.path.join( OUT, name ), 'wb' ) as w:
		w.setnchannels( 1 )
		w.setsampwidth( 2 )
		w.setframerate( RATE )
		w.writeframes( ( samples * 32767.0 ).astype( '<i2' ).tobytes() )


def loudest( samples ):
	"""dB of the loudest 50 ms."""
	n = int( 0.05 * RATE )
	return 10.0 * np.log10( np.max( np.convolve( samples * samples, np.ones( n ) / n, 'valid' ) ) )


def write_swing( name, samples, target ):
	"""As loud as the swings of the game in their loudest 50 ms. Noise has high peaks, so a soft limiter takes them."""
	samples = samples / np.max( np.abs( samples ) )
	low, high = 0.5, 40.0
	for _ in range( 32 ):
		gain = ( low + high ) * 0.5
		if loudest( 0.94 * np.tanh( gain * samples ) ) < target:
			low = gain
		else:
			high = gain
	limited = 0.94 * np.tanh( low * samples )
	write( name, limited, np.max( np.abs( limited ) ) )


def hum_loop( hz, partials, seed, length=2.0 ):
	"""A hum built like the hum of the game: a tone and its partials that swell slowly, each on its own, and a little
	air under 400 Hz. Whole cycles of everything, so its end runs into its start without a seam."""
	n = int( length * RATE )
	rng = np.random.default_rng( seed )
	t = np.arange( n ) / n
	out = np.zeros( n )
	for number, gain in enumerate( partials, 1 ):
		cycles = round( hz * number * length )
		swell = 1.0 + 0.18 * np.sin( 2.0 * np.pi * ( 1 + number % 2 ) * t + number * 1.3 )
		out += gain * swell * np.sin( 2.0 * np.pi * cycles * t + 2.0 * np.pi * rng.random() )
	freq = np.fft.rfftfreq( n, 1.0 / RATE )
	air = np.fft.irfft( np.exp( -freq / 160.0 ) * ( freq > 30.0 ) * np.exp( 2j * np.pi * rng.random( freq.size ) ), n )
	return out / np.max( np.abs( out ) ) + 0.06 * air / np.max( np.abs( air ) )


def write_rms( name, samples, db ):
	"""At a mean level, so the mix of the cgame (SWING_LAYER) holds whatever the loop is made of."""
	samples = samples * ( 10.0 ** ( db / 20.0 ) / np.sqrt( np.mean( samples * samples ) ) )
	write( name, samples, np.max( np.abs( samples ) ) )


def whoosh( length, start, end, q, seed, sub=0.0 ):
	"""A swing: air through a band that sweeps up and falls back, loudest a little before the middle."""
	n = int( length * RATE )
	rng = np.random.default_rng( seed )
	noise = rng.uniform( -1.0, 1.0, n )
	out = np.zeros( n )
	low = band = 0.0
	for i in range( n ):
		u = i / n
		hz = start * ( end / start ) ** np.sin( np.pi * min( u * 1.25, 1.0 ) * 0.5 )
		f = 2.0 * np.sin( np.pi * min( hz, RATE / 6 ) / RATE )
		high = noise[i] - low - band / q
		band += f * high
		low += f * band
		out[i] = band
	u = np.arange( n ) / n
	env = np.minimum( u / 0.38, 1.0 ) ** 1.6 * np.minimum( ( 1.0 - u ) / 0.62, 1.0 ) ** 2.2
	if sub:
		out += sub * np.max( np.abs( out ) ) * np.sin( 2.0 * np.pi * np.cumsum( 70.0 + 50.0 * u ) / RATE )
	return out * env


def near_miss( length, hz, seed ):
	"""A blade going past the ear: a short rush whose pitch falls as it passes (Doppler), loudest in the middle."""
	n = int( length * RATE )
	u = np.arange( n ) / n
	bend = 1.45 - 0.85 * ( 0.5 + 0.5 * np.tanh( ( u - 0.46 ) * 9.0 ) )
	phase = 2.0 * np.pi * np.cumsum( hz * bend ) / RATE
	tone = np.sin( phase ) + 0.55 * np.sin( 2.0 * phase + 0.6 ) + 0.25 * np.sin( 3.0 * phase + 1.3 )
	env = np.exp( -( ( u - 0.46 ) / 0.2 ) ** 2 ) * np.minimum( ( 1.0 - u ) / 0.08, 1.0 ) * np.minimum( u / 0.04, 1.0 )
	rush = whoosh( length, 1800.0, 520.0, 1.3, seed )
	return 0.5 * tone / np.max( np.abs( tone ) ) * env + 0.62 * rush / np.max( np.abs( rush ) )


os.makedirs( OUT, exist_ok=True )
for n in range( 4 ):
	write_swing( 'nearmiss%d.wav' % ( n + 1 ), near_miss( 0.30 + 0.03 * n, 118.0 + 17.0 * n, 401 + n ), -8.0 )
# The two loops stand next to the hum of the game, so they are built from what it measures (saberhum1-5 and
# saberhup1-9 of the game, 2026-11-23): its tone is 93 Hz with nearly all of its sound under 500 Hz, and a swing of the
# game climbs to 100 to 107 Hz, some with the third partial the loudest. Low: a little above the hum, with its mix of
# partials. High: the top of a swing, the third partial in front.
write_rms( 'swing_low.wav', hum_loop( 99.0, ( 1.0, 0.28, 0.28, 0.24, 0.18 ), 201 ), -15.3 )
write_rms( 'swing_high.wav', hum_loop( 107.0, ( 0.6, 0.3, 1.0, 0.4, 0.2 ), 202 ), -15.3 )
