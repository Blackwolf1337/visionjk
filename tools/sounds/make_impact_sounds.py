#!/usr/bin/env python3
# The sounds of shots hitting a wall (issue #22, codemp/cgame/weapons/cg_impact.c), made here so no audio of anybody
# else ships: stone, metal, wood, glass, cold and soft 1-3.wav, laid quietly over the stock impact, and ricochet1-4.wav
# for a bolt whining off metal. 22050 Hz, 16 bit, mono, as the game. Noise with a fixed seed, so the files come out the
# same every time. Writes into assets/japlus_client/sound/vision/impact/.
# usage: tools/sounds/make_impact_sounds.py
import os
import wave

import numpy as np

RATE = 22050
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'sound', 'vision', 'impact' )


def write( name, samples, level ):
	samples = samples / ( np.max( np.abs( samples ) ) or 1.0 ) * level
	fade = min( 64, len( samples ) )
	samples[-fade:] *= np.linspace( 1.0, 0.0, fade )
	with wave.open( os.path.join( OUT, name ), 'wb' ) as w:
		w.setnchannels( 1 )
		w.setsampwidth( 2 )
		w.setframerate( RATE )
		w.writeframes( ( samples * 32767.0 ).astype( '<i2' ).tobytes() )


def times( seconds ):
	return np.arange( int( seconds * RATE ) ) / RATE


def band( noise, low, high ):
	"""Noise kept between two frequencies."""
	spectrum = np.fft.rfft( noise )
	f = np.fft.rfftfreq( len( noise ), 1.0 / RATE )
	spectrum[( f < low ) | ( f > high )] = 0.0
	return np.fft.irfft( spectrum, len( noise ) )


def modes( t, rng, table ):
	"""Struck partials: (frequency, loudness, decay in seconds), each a little off."""
	out = np.zeros_like( t )
	for freq, gain, decay in table:
		f = freq * rng.uniform( 0.94, 1.06 )
		out += gain * np.sin( 2.0 * np.pi * f * t + rng.uniform( 0.0, 6.28 ) ) * np.exp( -t / decay )
	return out


def burst( t, rng, low, high, decay ):
	return band( rng.standard_normal( len( t ) ), low, high ) * np.exp( -t / decay )


def stone( rng ):
	t = times( 0.16 )
	return burst( t, rng, 500, 5200, 0.018 ) + 0.5 * modes( t, rng, [ ( 170, 1.0, 0.025 ), ( 310, 0.5, 0.018 ) ] )


def metal( rng ):
	t = times( 0.34 )
	return 0.5 * burst( t, rng, 1800, 9000, 0.006 ) + 0.28 * modes( t, rng,
		[ ( 2130, 1.0, 0.07 ), ( 3420, 0.8, 0.055 ), ( 5210, 0.55, 0.04 ), ( 7690, 0.3, 0.025 ) ] )


def wood( rng ):
	t = times( 0.15 )
	return 0.5 * burst( t, rng, 700, 3600, 0.009 ) + modes( t, rng, [ ( 330, 1.0, 0.024 ), ( 610, 0.7, 0.016 ), ( 1140, 0.3, 0.01 ) ] )


def glass( rng ):
	t = times( 0.26 )
	return 0.35 * burst( t, rng, 3000, 10000, 0.012 ) + 0.3 * modes( t, rng,
		[ ( 3810, 1.0, 0.05 ), ( 6120, 0.8, 0.035 ), ( 8930, 0.5, 0.022 ) ] )


def cold( rng ):
	t = times( 0.18 )
	return burst( t, rng, 900, 6500, 0.035 ) * ( 1.0 - np.exp( -t / 0.003 ) )


def soft( rng ):
	t = times( 0.14 )
	return 0.6 * burst( t, rng, 120, 1300, 0.02 ) + modes( t, rng, [ ( 95, 1.0, 0.03 ), ( 185, 0.5, 0.02 ) ] )


def ricochet( rng ):
	length = rng.uniform( 0.42, 0.62 )
	t = times( length )
	x = t / length
	start, end = rng.uniform( 3000, 3900 ), rng.uniform( 1050, 1500 )
	wobble = 1.0 + 0.018 * np.sin( 2.0 * np.pi * rng.uniform( 17, 26 ) * t )
	phase = 2.0 * np.pi * np.cumsum( ( start * ( end / start ) ** x ) * wobble ) / RATE
	envelope = ( 1.0 - np.exp( -t / 0.006 ) ) * ( 1.0 - x ) ** 1.6
	tone = np.sin( phase ) + 0.28 * np.sin( 2.0 * phase + 0.7 ) + 0.1 * np.sin( 3.0 * phase )
	hiss = band( rng.standard_normal( len( t ) ), 2500, 9000 ) * 0.5
	return ( tone + hiss ) * envelope + 0.5 * burst( t, rng, 1800, 9000, 0.005 )


def main():
	os.makedirs( OUT, exist_ok=True )
	rng = np.random.default_rng( 2226 )
	levels = { 'stone': 0.34, 'metal': 0.30, 'wood': 0.36, 'glass': 0.26, 'cold': 0.26, 'soft': 0.32 }
	makers = { 'stone': stone, 'metal': metal, 'wood': wood, 'glass': glass, 'cold': cold, 'soft': soft }
	for name, make in makers.items():
		for i in range( 1, 4 ):
			write( '%s%i.wav' % ( name, i ), make( rng ), levels[name] )
	for i in range( 1, 5 ):
		write( 'ricochet%i.wav' % i, ricochet( rng ), 0.34 )


if __name__ == '__main__':
	main()
