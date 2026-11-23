#!/usr/bin/env python3
# The sounds of SmoothSwing (issue #20, codemp/cgame/saber/cg_sabersound.c), made here so no audio of anybody else
# ships: swing_low.wav and swing_high.wav (two seamless loops the cgame crossfades over the hum by how far the blade
# turned) and fast1-3, medium1-3, strong1-3.wav (a swing of each stance). 22050 Hz, 16 bit, mono, as the sounds of the
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


def swing_loop( centre, tones, seed, length=2.0 ):
	"""Air round a centre frequency and a few tones. Made from a spectrum of whole cycles, so its end runs into its
	start without a seam."""
	n = int( length * RATE )
	rng = np.random.default_rng( seed )
	freq = np.fft.rfftfreq( n, 1.0 / RATE )
	shape = np.exp( -0.5 * ( np.log2( np.maximum( freq, 1.0 ) / centre ) / 0.75 ) ** 2 )
	air = np.fft.irfft( shape * np.exp( 2j * np.pi * rng.random( freq.size ) ), n )
	air /= np.max( np.abs( air ) )

	t = np.arange( n ) / n
	hum = np.zeros( n )
	for number, ( hz, gain ) in enumerate( tones ):
		cycles = round( hz * length )
		flutter = 1.0 + 0.25 * np.sin( 2.0 * np.pi * ( 7 + number * 2 ) * t + number )	# whole cycles as well
		hum += gain * flutter * np.sin( 2.0 * np.pi * cycles * t )
	return 0.62 * air + 0.38 * hum / max( np.max( np.abs( hum ) ), 1e-6 )


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


def blade_tone( hz, length, start, rolloff ):
	"""The buzz of a blade, its pitch running from start * hz to hz in the first third (start above 1: falling)."""
	n = int( length * RATE )
	u = np.arange( n ) / n
	bend = start + ( 1.0 - start ) * np.minimum( u / 0.34, 1.0 ) ** 0.7
	phase = 2.0 * np.pi * np.cumsum( hz * bend ) / RATE
	tone = np.zeros( n )
	for partial in range( 1, 8 ):
		tone += rolloff ** ( partial - 1 ) * np.sin( partial * phase + partial * partial * 0.7 )
	return tone / np.max( np.abs( tone ) )


def crackle( length, until, per_second, seed ):
	"""Short sparks in the first part of the sound, the flicker of the blade catching."""
	n = int( length * RATE )
	rng = np.random.default_rng( seed )
	out = np.zeros( n )
	for _ in range( max( 1, int( per_second * until ) ) ):
		at = int( rng.random() ** 1.4 * until * RATE )
		size = int( rng.uniform( 0.002, 0.009 ) * RATE )
		if at + size < n:
			out[at:at + size] += rng.uniform( 0.4, 1.0 ) * rng.choice( ( -1.0, 1.0 ) ) * rng.uniform( -1.0, 1.0, size ) * np.hanning( size )
	return out / ( np.max( np.abs( out ) ) or 1.0 )


def ignite( hz, rolloff, sparks, length, seed ):
	"""Catches with a crackle, the buzz climbs to its pitch, and fades so the hum takes over."""
	n = int( length * RATE )
	u = np.arange( n ) / RATE
	env = np.minimum( u / 0.018, 1.0 ) * np.minimum( 1.0, np.exp( -( u - 0.30 ) / 0.34 ) ) * np.minimum( ( length - u ) / 0.05, 1.0 )
	body = blade_tone( hz, length, 0.42, rolloff ) * env
	rush = whoosh( length, 260.0, 2100.0, 1.5, seed + 100 )
	return 0.62 * body + 0.26 * rush / np.max( np.abs( rush ) ) + 0.34 * crackle( length, 0.30, sparks, seed ) * np.minimum( u / 0.004, 1.0 )


def retract( hz, rolloff, sparks, length, seed ):
	"""The buzz falls away into the hilt and ends with a small snap."""
	n = int( length * RATE )
	u = np.arange( n ) / RATE
	env = np.minimum( u / 0.012, 1.0 ) * np.minimum( 1.0, ( length - u ) / ( length * 0.82 ) ) ** 1.5
	body = blade_tone( hz, length, 1.0, rolloff )
	fall = 2.0 * np.pi * np.cumsum( hz * ( 1.0 - 0.62 * u / length ) ) / RATE
	body = 0.5 * body + 0.5 * np.sin( fall ) + 0.25 * np.sin( 2.0 * fall + 0.4 )
	rush = whoosh( length, 1500.0, 240.0, 1.5, seed + 100 )
	snap = crackle( length, length, sparks * 0.4, seed ) * np.exp( -np.abs( u - length * 0.86 ) / 0.018 )
	return 0.62 * body * env + 0.24 * rush / np.max( np.abs( rush ) ) + 0.32 * snap


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
# (one voice per entry: pitch, how bright, sparks a second, seconds on, seconds off; targets: saberon and saberoff of
# the game in their loudest 50 ms, measured 2026-11-23)
VOICES = ( ( 92.0, 0.62, 70, 1.25, 0.86 ), ( 78.0, 0.72, 40, 1.34, 0.94 ), ( 110.0, 0.52, 100, 1.16, 0.80 ),
	( 66.0, 0.78, 30, 1.42, 1.02 ), ( 131.0, 0.56, 130, 1.10, 0.76 ), ( 98.0, 0.84, 55, 1.22, 0.88 ),
	( 84.0, 0.48, 90, 1.30, 0.90 ), ( 120.0, 0.70, 60, 1.14, 0.82 ), ( 72.0, 0.60, 110, 1.38, 0.98 ),
	( 104.0, 0.76, 35, 1.20, 0.84 ), ( 146.0, 0.46, 80, 1.06, 0.74 ), ( 58.0, 0.82, 50, 1.48, 1.06 ) )
for n in range( 4 ):
	write_swing( 'nearmiss%d.wav' % ( n + 1 ), near_miss( 0.30 + 0.03 * n, 118.0 + 17.0 * n, 401 + n ), -8.0 )
for n, ( hz, rolloff, sparks, on, off ) in enumerate( VOICES ):
	write_swing( 'ignite%d.wav' % ( n + 1 ), ignite( hz, rolloff, sparks, on, 301 + n ), -9.7 )
	write_swing( 'retract%d.wav' % ( n + 1 ), retract( hz, rolloff, sparks, off, 341 + n ), -9.7 )
write( 'swing_low.wav', swing_loop( 240.0, ( ( 92.0, 1.0 ), ( 184.0, 0.55 ), ( 276.0, 0.2 ) ), 201 ), 0.6 )
write( 'swing_high.wav', swing_loop( 680.0, ( ( 138.0, 0.8 ), ( 276.0, 0.6 ), ( 414.0, 0.35 ) ), 202 ), 0.6 )
for n in range( 3 ):
	bend = 0.92 + 0.08 * n
	# (targets: the mean of saberhup1-3, 4-6 and 7-9 of the game, measured 2026-11-23)
	write_swing( 'fast%d.wav' % ( n + 1 ), whoosh( 0.42 + 0.02 * n, 500.0 * bend, 1500.0 * bend, 2.4, 211 + n ), -6.8 )
	write_swing( 'medium%d.wav' % ( n + 1 ), whoosh( 0.58 + 0.02 * n, 340.0 * bend, 1000.0 * bend, 2.0, 221 + n, 0.15 ), -5.2 )
	write_swing( 'strong%d.wav' % ( n + 1 ), whoosh( 0.78 + 0.03 * n, 180.0 * bend, 620.0 * bend, 1.6, 231 + n, 0.35 ), -6.4 )
