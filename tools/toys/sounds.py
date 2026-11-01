#!/usr/bin/env python3
# The sounds of the toys of issue #9, made here so no audio of anybody else ships: emote_send.wav (a soft rising
# whoosh where an emoji leaves, codemp/cgame/toys/cg_emote.c) and emote_pop.wav (a bubbly pop with a little chime
# where it arrives), pong_hit.wav (the knock of a paddle on the ball, cg_pong.c) and pong_bounce.wav (the higher,
# shorter tick of the ball on the table), tennis_hit.wav (the deeper pock of a racket) and tennis_bounce.wav (the dull
# thud of the ball on the court), and the water gun (cg_water.c): water_spray.wav (the loop of the jet leaving the
# nozzle), water_hit1-3.wav (the jet breaking on a hard surface), water_pool1-3.wav (into standing water: the
# bubbles it drags under ring), water_face.wav (a face full of it) and water_pump.wav (the trigger and the pump of a
# plastic gun). 22050 Hz, 16 bit, mono, as the sounds of the game. Noise with a fixed seed, so the
# files come out the same every time. Writes into assets/japlus_client/sound/vision/toys/.
# usage: tools/toys/sounds.py
import math
import os
import random
import struct
import wave

RATE = 22050
OUT = os.path.join( os.path.dirname( os.path.abspath( __file__ ) ), '..', '..', 'assets', 'japlus_client', 'sound', 'vision', 'toys' )


def write( name, samples, level=0.85 ):
	peak = max( abs( s ) for s in samples ) or 1.0
	with wave.open( os.path.join( OUT, name ), 'wb' ) as w:
		w.setnchannels( 1 )
		w.setsampwidth( 2 )
		w.setframerate( RATE )
		w.writeframes( b''.join( struct.pack( '<h', int( s / peak * level * 32767 ) ) for s in samples ) )


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


def bubbles( out, start, length, rate, rmin, rmax, rnd, gain=1.0 ):
	"""Air bubbles the water drags under, added into out from start on: each rings at the Minnaert frequency of its
	radius, f = 3.26 / r (r in metres: 3.26 kHz at a millimetre), dies away with the damping of a bubble in water,
	d = 0.043 f + 0.0014 f^1.5 a second, and rises in pitch as it nears the surface, f (1 + 0.1 d t) (van den Doel,
	"Physically based models for liquid sounds", 2005). rate a second, radii in millimetres, spread evenly in log."""
	t = start
	while True:
		t += rnd.expovariate( rate )
		if t >= start + length:
			break
		r = math.exp( rnd.uniform( math.log( rmin ), math.log( rmax ) ) )
		f0 = 3260.0 / r
		if f0 > RATE * 0.45:
			continue
		d = 0.043 * f0 + 0.0014 * f0 ** 1.5
		a = gain * r ** 0.8 * rnd.uniform( 0.4, 1.0 )
		first = int( t * RATE )
		phase = 0.0
		for k in range( min( int( 6.0 / d * RATE ), len( out ) - first ) ):
			tt = k / RATE
			phase += 2.0 * math.pi * f0 * ( 1.0 + 0.1 * d * tt ) / RATE
			out[first + k] += a * math.sin( phase ) * math.exp( -d * tt )


def ticks( out, start, length, rate, rnd, gain=1.0 ):
	"""Droplets breaking on a hard surface, from start on: each a tick of noise of a millisecond or two, louder ones
	rarer (as many small drops as there are large), rate a second."""
	t = start
	while True:
		t += rnd.expovariate( rate )
		if t >= start + length:
			break
		a = gain * min( 4.0, rnd.paretovariate( 2.2 ) ) * 0.4
		first = int( t * RATE )
		decay = rnd.uniform( 0.0003, 0.0014 )
		last = 0.0
		for k in range( min( int( decay * 6.0 * RATE ), len( out ) - first ) ):
			v = rnd.uniform( -1.0, 1.0 )
			# (the difference of two samples: a tick is bright, it has nothing low)
			out[first + k] += a * ( v - last ) * math.exp( -k / RATE / decay )
			last = v


def fade( samples, fade_in, fade_out ):
	n = len( samples )
	for i in range( n ):
		t = i / RATE
		g = min( 1.0, t / fade_in ) if fade_in > 0.0 else 1.0
		g *= min( 1.0, ( n - 1 - i ) / RATE / fade_out ) if fade_out > 0.0 else 1.0
		samples[i] *= g
	return samples


def spray( length=2.0, seed=21 ):
	"""The jet leaving the nozzle, a loop: the hiss of water pressed through a small hole (noise around 3.8 kHz),
	the rush of the stream under it (around 650 Hz) and a few bubbles of air in it, breathing with the pump of the
	gun 11 times a second, a whole number of times in the loop. Made longer and its end laid over its start, so the
	loop has no seam."""
	rnd = random.Random( seed )
	over = 0.25
	n = int( ( length + over ) * RATE )
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
	hiss = bandpass( noise, lambda t: 3800.0, 1.6 )
	rush = bandpass( noise, lambda t: 650.0, 1.4 )
	gurgle = [0.0] * n
	bubbles( gurgle, 0.0, length + over, 70.0, 1.5, 5.0, rnd, 0.5 )
	raw = []
	low = 0.0
	for i in range( n ):
		t = i / RATE
		pump = 1.0 + 0.12 * math.sin( 2.0 * math.pi * 11.0 * t ) + 0.05 * math.sin( 2.0 * math.pi * 3.0 * t )
		v = ( hiss[i] + 0.6 * rush[i] ) * pump + gurgle[i]
		# (and nothing very high: the hiss of water is softer than the one of air)
		low += 0.55 * ( v - low )
		raw.append( low )
	m = int( length * RATE )
	k = int( over * RATE )
	out = raw[:m]
	for i in range( k ):
		w = i / k
		out[i] = raw[m + i] * ( 1.0 - w ) + raw[i] * w
	return out


def hit( seed ):
	"""The jet breaking on stone or metal for a fifth of a second: hundreds of droplets ticking over the hiss of the
	spray they make."""
	rnd = random.Random( seed )
	n = int( 0.3 * RATE )
	out = [0.0] * n
	ticks( out, 0.0, 0.28, 900.0, rnd )
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
	bed = bandpass( noise, lambda t: 3200.0, 0.7 )
	for i in range( n ):
		out[i] += 0.25 * bed[i]
	return fade( out, 0.02, 0.08 )


def pool( seed ):
	"""The jet into standing water: the bubbles it drags under, many small and some larger, and a little of the
	ticking of the drops that fly off and come back."""
	rnd = random.Random( seed )
	n = int( 0.32 * RATE )
	out = [0.0] * n
	bubbles( out, 0.0, 0.26, 260.0, 0.6, 4.0, rnd )
	ticks( out, 0.0, 0.26, 250.0, rnd, 0.35 )
	return fade( out, 0.015, 0.06 )


def face():
	"""A face full of water: the slap of it, a burst of splatter, and drops that fall off with a few bubbles."""
	rnd = random.Random( 31 )
	n = int( 0.6 * RATE )
	out = [0.0] * n
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
	low = bandpass( noise, lambda t: 260.0, 1.0 )
	for i in range( n ):
		t = i / RATE
		out[i] += ( 2.2 * low[i] + 0.5 * math.sin( 2.0 * math.pi * 95.0 * t ) ) * math.exp( -t / 0.035 ) * min( 1.0, t / 0.002 )
	ticks( out, 0.0, 0.18, 1400.0, rnd, 1.2 )
	ticks( out, 0.15, 0.4, 40.0, rnd, 0.6 )
	bubbles( out, 0.05, 0.3, 50.0, 1.0, 3.0, rnd, 0.6 )
	return fade( out, 0.0, 0.1 )


def pump():
	"""The trigger of a plastic water gun and its pump taking up: two plastic clicks and the puff of air between."""
	rnd = random.Random( 41 )
	out = [0.0] * int( 0.16 * RATE )
	for at, freq, decay, gain in ( ( 0.0, 2600.0, 0.004, 1.0 ), ( 0.075, 1800.0, 0.006, 0.7 ) ):
		click = knock( freq, decay, 0.04, int( freq ) )
		first = int( at * RATE )
		for k, v in enumerate( click ):
			out[first + k] += v * gain
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( len( out ) )]
	air = bandpass( noise, lambda t: 2200.0, 1.2 )
	for i in range( len( out ) ):
		t = i / RATE - 0.01
		if t > 0.0:
			out[i] += 0.35 * air[i] * math.exp( -t / 0.025 ) * min( 1.0, t / 0.005 )
	return out


os.makedirs( OUT, exist_ok=True )
write( 'emote_send.wav', send() )
write( 'emote_pop.wav', pop() )
write( 'pong_hit.wav', knock( 1150.0, 0.018, 0.12, 7 ) )
write( 'pong_bounce.wav', knock( 1900.0, 0.010, 0.08, 8 ) )
write( 'tennis_hit.wav', knock( 520.0, 0.030, 0.2, 10 ) )
write( 'tennis_bounce.wav', knock( 240.0, 0.022, 0.15, 11 ) )
write( 'water_spray.wav', spray(), 0.5 )
for n in range( 3 ):
	write( 'water_hit%d.wav' % ( n + 1 ), hit( 51 + n ), 0.7 )
	write( 'water_pool%d.wav' % ( n + 1 ), pool( 61 + n ), 0.7 )
write( 'water_face.wav', face() )
write( 'water_pump.wav', pump(), 0.6 )
