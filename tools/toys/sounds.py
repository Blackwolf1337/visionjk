#!/usr/bin/env python3
# The sounds of the toys of issue #9, made here so no audio of anybody else ships: emote_send.wav (a soft rising
# whoosh where an emoji leaves, codemp/cgame/toys/cg_emote.c) and emote_pop.wav (a bubbly pop with a little chime
# where it arrives), pong_hit.wav (the knock of a paddle on the ball, cg_pong.c) and pong_bounce.wav (the higher,
# shorter tick of the ball on the table), tennis_hit.wav (the deeper pock of a racket) and tennis_bounce.wav (the dull
# thud of the ball on the court), and the water gun (cg_water.c): water_spray.wav (the loop of the jet leaving the
# nozzle), water_hit1-3.wav (the jet breaking on a hard surface), water_pool1-3.wav (into standing water: the
# bubbles it drags under ring), water_face.wav (a face full of it) and water_pump.wav (the trigger and the pump of a
# plastic gun), and the RC car (cg_rccar.c): rc_motor1-4.wav (loops of its electric motor at four speeds, which the
# cgame crossfades), rc_scrub.wav (the loop of tires sliding on hard ground), rc_crunch.wav (the loop of tires rolling
# over dirt and gravel), rc_servo.wav (the buzz of the steering servo), rc_land1-2.wav (the thump of a landing) and
# rc_on.wav (the chirp of its receiver), and the snowball (cg_toys.c): snow_pack.wav (packing it in the hands),
# snow_throw.wav (it leaves the hand), snow_hard1-3.wav (it bursts on stone or metal), snow_soft1-2.wav (on soft ground),
# snow_body.wav (on a player) and snow_face.wav (in the own face), and spray_can.wav (the can of a spray, cg_spray.c). 22050 Hz, 16 bit, mono, as the sounds of the game. Noise with a fixed seed, so the
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


def seamless( render, length, over=0.2 ):
	"""A loop of length seconds: render( seconds ) made longer, its end laid over its start, so there is no seam."""
	raw = render( length + over + 2.0 / RATE )
	m = int( length * RATE )
	k = int( over * RATE )
	out = raw[:m]
	for i in range( k ):
		w = i / k
		out[i] = raw[m + i] * ( 1.0 - w ) + raw[i] * w
	return out


def motor( freq, seed ):
	"""The electric motor of the RC car at one speed, a loop of a second. Its whine: the turn of the rotor (freq,
	a whole number of Hz, so the tones fit the loop) with the strong second harmonic of the windings, a third and the
	sixth of its magnets; the gears it drives buzz at 1.7 times that (narrow noise around the mesh of their teeth);
	the bearings hiss; and the speed controller whines faintly high above it all, at every speed the same."""
	rnd = random.Random( seed )
	gear = round( freq * 1.7 )

	def render( length ):
		n = int( length * RATE )
		noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
		mesh = bandpass( noise, lambda t: gear, 18.0 )
		hiss = bandpass( noise, lambda t: 1500.0 + freq, 1.2 )
		out = []
		for i in range( n ):
			t = i / RATE
			w = 2.0 * math.pi * freq * t
			warble = 1.0 + 0.06 * math.sin( 2.0 * math.pi * 7.0 * t )
			whine = ( 0.55 * math.sin( w ) + 0.9 * math.sin( 2.0 * w + 0.3 ) + 0.3 * math.sin( 3.0 * w + 1.1 )
				+ 0.18 * math.sin( 6.0 * w + 0.7 ) ) * warble
			esc = 0.05 * math.sin( 2.0 * math.pi * 3150.0 * t )
			out.append( whine + 1.6 * mesh[i] + ( 0.12 + freq / 4000.0 ) * hiss[i] + esc )
		return out

	return seamless( render, 1.0 )


def scrub():
	"""Tires sliding on hard ground, a loop: rubber dragged over stone (noise around 1.4 kHz) in grains that come and
	go, and the thin squeal of it (a tone near 2.1 kHz that wavers)."""
	rnd = random.Random( 71 )

	def render( length ):
		n = int( length * RATE )
		noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
		drag = bandpass( noise, lambda t: 1400.0, 2.0 )
		grains = bandpass( [rnd.uniform( -1.0, 1.0 ) for _ in range( n )], lambda t: 18.0, 0.7 )
		out = []
		phase = 0.0
		for i in range( n ):
			t = i / RATE
			phase += 2.0 * math.pi * ( 2100.0 + 60.0 * math.sin( 2.0 * math.pi * 3.0 * t ) ) / RATE
			level = 0.6 + 4.0 * abs( grains[i] )
			out.append( drag[i] * level + 0.25 * math.sin( phase ) * ( 0.5 + 0.5 * math.sin( 2.0 * math.pi * 5.0 * t ) ) )
		return out

	return seamless( render, 1.2 )


def crunch():
	"""Tires rolling over dirt and gravel, a loop: small stones ticking under the knobs and the low rumble of the
	ground."""
	rnd = random.Random( 81 )

	def render( length ):
		n = int( length * RATE )
		out = [0.0] * n
		ticks( out, 0.0, length, 420.0, rnd, 0.8 )
		noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
		rumble = bandpass( noise, lambda t: 260.0, 1.0 )
		for i in range( n ):
			out[i] += 1.4 * rumble[i]
		return out

	return seamless( render, 1.5 )


def servo():
	"""The steering servo turning the front wheels: the buzz of its small motor rising as it goes, and the grind of
	its plastic gears: a fifth of a second."""
	rnd = random.Random( 91 )
	n = int( 0.2 * RATE )
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
	grind = bandpass( noise, lambda t: 900.0 + 600.0 * t / 0.2, 6.0 )
	out = []
	phase = 0.0
	for i in range( n ):
		t = i / RATE
		phase += 2.0 * math.pi * ( 130.0 + 200.0 * t ) / RATE
		buzz = math.sin( phase ) + 0.5 * math.sin( 3.0 * phase ) + 0.3 * math.sin( 5.0 * phase )
		out.append( 0.6 * buzz + 1.2 * grind[i] )
	return fade( out, 0.008, 0.05 )


def land( seed ):
	"""The car landing: the low thump of its body and tires, the knock of its plastic, and its springs ringing
	out: four tenths of a second."""
	rnd = random.Random( seed )
	n = int( 0.4 * RATE )
	out = [0.0] * n
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
	thud = bandpass( noise, lambda t: 140.0, 1.2 )
	spring = 300.0 + rnd.uniform( -30.0, 30.0 )
	for i in range( n ):
		t = i / RATE
		out[i] += ( 1.2 * math.sin( 2.0 * math.pi * 62.0 * t ) + 2.0 * thud[i] ) * math.exp( -t / 0.06 ) * min( 1.0, t / 0.002 )
		out[i] += 0.25 * math.sin( 2.0 * math.pi * spring * t * ( 1.0 - 0.15 * t ) ) * math.exp( -t / 0.12 )
	click = knock( 950.0, 0.012, 0.08, seed )
	for k, v in enumerate( click ):
		out[k] += 0.6 * v
	return fade( out, 0.0, 0.08 )


def power():
	"""The receiver of the car coming on: two short rising beeps."""
	out = []
	for freq, length in ( ( 1500.0, 0.06 ), ( 0.0, 0.03 ), ( 2000.0, 0.08 ) ):
		for i in range( int( length * RATE ) ):
			t = i / RATE
			out.append( math.sin( 2.0 * math.pi * freq * t ) * min( 1.0, t / 0.004, ( length - t ) / 0.01 ) if freq else 0.0 )
	return out


def lowpass( samples, cutoff, passes=2 ):
	"""A one pole lowpass at cutoff Hz, passes times over: 6 dB an octave each"""
	a = 1.0 - math.exp( -2.0 * math.pi * cutoff / RATE )
	for _ in range( passes ):
		low = 0.0
		out = []
		for x in samples:
			low += a * ( x - low )
			out.append( low )
		samples = out
	return samples


def snow_grains( out, start, length, rate, rnd, gain=1.0, compress=1.0, crush=1.0 ):
	"""Snow breaking, added into out from start on, rate grains a second: each grain a short burst of noise (13 to 40
	ms) with a sharp start, louder ones rarer. The sum goes through the two bands of snow of footstep synthesis: the
	compression of the snow (around 1.6 kHz) and the crunch of its crystals breaking (higher, around 4 kHz), with
	compress and crush as their shares (the model of Turchet et al., two components of grains)."""
	raw = [0.0] * len( out )
	t = start
	while True:
		t += rnd.expovariate( rate )
		if t >= start + length:
			break
		a = min( 3.0, rnd.paretovariate( 2.0 ) ) * 0.5
		dur = rnd.uniform( 0.013, 0.04 )
		first = int( t * RATE )
		for k in range( min( int( dur * RATE ), len( raw ) - first ) ):
			tt = k / RATE
			raw[first + k] += a * rnd.uniform( -1.0, 1.0 ) * math.exp( -tt / ( dur * 0.3 ) ) * min( 1.0, tt / 0.0008 )
	low = lowpass( bandpass( raw, lambda t: 1300.0, 1.6 ), 2500.0 )
	high = lowpass( bandpass( raw, lambda t: 3200.0, 1.2 ), 5000.0 )
	for i in range( len( out ) ):
		out[i] += gain * ( compress * low[i] + crush * 0.6 * high[i] )


def thump( out, start, freq, decay, gain, rnd ):
	"""The dull blow of a soft mass: a low tone falling a little and a band of noise under it, gone in decay"""
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( len( out ) )]
	body = bandpass( noise, lambda t: freq * 2.0, 1.0 )
	first = int( start * RATE )
	phase = 0.0
	for k in range( len( out ) - first ):
		t = k / RATE
		phase += 2.0 * math.pi * freq * ( 1.0 - 0.3 * min( 1.0, t / decay ) ) / RATE
		env = math.exp( -t / decay ) * min( 1.0, t / 0.002 )
		out[first + k] += gain * env * ( math.sin( phase ) + 1.4 * body[first + k] )


def powder( out, start, decay, gain, rnd, centre=3200.0 ):
	"""The hiss of fine snow thrown into the air by a burst"""
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( len( out ) )]
	hiss = lowpass( bandpass( noise, lambda t: centre, 0.8 ), centre * 1.4 )
	first = int( start * RATE )
	for k in range( len( out ) - first ):
		t = k / RATE
		out[first + k] += gain * hiss[first + k] * math.exp( -t / decay ) * min( 1.0, t / 0.003 )


def snow_pack():
	"""Snow pressed into a ball by two hands: two squeezes of dense crunch, the compression stronger: 0.45 s"""
	rnd = random.Random( 121 )
	out = [0.0] * int( 0.45 * RATE )
	snow_grains( out, 0.0, 0.16, 260.0, rnd, 1.0, 1.4, 0.6 )
	snow_grains( out, 0.22, 0.17, 300.0, rnd, 1.1, 1.5, 0.7 )
	return fade( out, 0.005, 0.06 )


def snow_throw():
	"""The ball leaving the hand: the swish of the arm and of a small ball through the air, rising and falling in
	pitch as it passes: 0.32 s"""
	rnd = random.Random( 122 )
	length = 0.32
	n = int( length * RATE )
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
	air = lowpass( bandpass( noise, lambda t: 400.0 + 900.0 * math.sin( math.pi * min( 1.0, t / length ) ) ** 2, 2.5 ), 1800.0 )
	out = []
	for i, v in enumerate( air ):
		t = i / RATE
		out.append( v * math.sin( math.pi * min( 1.0, t / length ) ) ** 1.8 )
	return out


def snow_hard( seed ):
	"""A snowball bursting on stone or metal: a short dull blow, the hiss of the powder and a quick crunch: 0.35 s"""
	rnd = random.Random( seed )
	out = [0.0] * int( 0.35 * RATE )
	thump( out, 0.0, rnd.uniform( 120.0, 150.0 ), 0.03, 2.2, rnd )
	powder( out, 0.0, 0.05, 0.7, rnd, 2600.0 )
	snow_grains( out, 0.0, 0.07, 420.0, rnd, 0.9, 0.8, 1.2 )
	return fade( out, 0.0, 0.08 )


def snow_soft( seed ):
	"""On soft ground: a deeper, longer blow and more crunch, little hiss: 0.4 s"""
	rnd = random.Random( seed )
	out = [0.0] * int( 0.4 * RATE )
	thump( out, 0.0, rnd.uniform( 80.0, 100.0 ), 0.06, 3.0, rnd )
	powder( out, 0.0, 0.04, 0.4, rnd, 2600.0 )
	snow_grains( out, 0.005, 0.13, 320.0, rnd, 1.0, 1.3, 0.8 )
	return fade( out, 0.0, 0.1 )


def snow_body():
	"""On a player: the soft slap on cloth, low and muffled, a little crunch: 0.3 s"""
	rnd = random.Random( 131 )
	out = [0.0] * int( 0.3 * RATE )
	thump( out, 0.0, 95.0, 0.04, 3.2, rnd )
	snow_grains( out, 0.0, 0.06, 300.0, rnd, 0.6, 1.2, 0.3 )
	return fade( out, 0.0, 0.08 )


def snow_face():
	"""In the own face, close by: a louder slap, the crunch of it right at the ear, and the powder falling off: 0.6 s"""
	rnd = random.Random( 132 )
	out = [0.0] * int( 0.6 * RATE )
	thump( out, 0.0, 110.0, 0.045, 3.0, rnd )
	powder( out, 0.0, 0.08, 0.8, rnd, 2800.0 )
	snow_grains( out, 0.0, 0.12, 380.0, rnd, 1.1, 1.0, 1.0 )
	snow_grains( out, 0.15, 0.35, 40.0, rnd, 0.5, 0.6, 1.0 )
	return fade( out, 0.0, 0.12 )


def spray_can():
	"""A spray can (cg_spray.c): the ball inside clacks twice as the can comes up, then the gas hisses out of the
	nozzle for as long as the paint goes on (0.7 s), bright and airy (noise around 6.5 kHz with a broad band of
	air under it), wavering a little as the finger presses, with the rattle of the paint in the valve: 0.85 s"""
	rnd = random.Random( 171 )
	n = int( 0.85 * RATE )
	out = [0.0] * n
	# the ball: two short metallic clicks, each a few rings of the thin can
	for start in ( 0.0, 0.07 ):
		at = int( start * RATE )
		for freq, decay, gain in ( ( 2900.0, 0.012, 1.0 ), ( 4700.0, 0.008, 0.6 ), ( 1700.0, 0.02, 0.4 ) ):
			f = freq * rnd.uniform( 0.97, 1.03 )
			for i in range( int( 0.06 * RATE ) ):
				if at + i < n:
					t = i / RATE
					out[at + i] += gain * 0.5 * math.sin( 2.0 * math.pi * f * t ) * math.exp( -t / decay )
	noise = [rnd.uniform( -1.0, 1.0 ) for _ in range( n )]
	bright = bandpass( noise, lambda t: 6500.0, 1.2 )
	air = bandpass( noise, lambda t: 2400.0, 0.8 )
	start, length = 0.1, 0.7
	for i in range( n ):
		t = i / RATE - start
		if t < 0.0:
			continue
		env = min( 1.0, t / 0.015 ) * ( 1.0 if t < length else math.exp( -( t - length ) / 0.03 ) )
		press = 1.0 + 0.08 * math.sin( 2.0 * math.pi * 7.0 * t ) + 0.05 * math.sin( 2.0 * math.pi * 17.0 * t + 1.3 )
		spit = 1.0 + ( 0.6 if rnd.random() < 0.002 else 0.0 )
		out[i] += ( bright[i] + 0.45 * air[i] ) * env * press * spit
	return fade( out, 0.0, 0.05 )


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
for n, freq in enumerate( ( 160, 330, 560, 820 ) ):
	write( 'rc_motor%d.wav' % ( n + 1 ), motor( freq, 101 + n ), 0.55 )
write( 'rc_scrub.wav', scrub(), 0.5 )
write( 'rc_crunch.wav', crunch(), 0.5 )
write( 'rc_servo.wav', servo(), 0.45 )
write( 'rc_land1.wav', land( 111 ), 0.8 )
write( 'rc_land2.wav', land( 112 ), 0.8 )
write( 'rc_on.wav', power(), 0.35 )
write( 'snow_pack.wav', snow_pack(), 0.5 )
write( 'snow_throw.wav', snow_throw(), 0.45 )
for n in range( 3 ):
	write( 'snow_hard%d.wav' % ( n + 1 ), snow_hard( 141 + n ), 0.75 )
for n in range( 2 ):
	write( 'snow_soft%d.wav' % ( n + 1 ), snow_soft( 151 + n ), 0.75 )
write( 'snow_body.wav', snow_body(), 0.75 )
write( 'snow_face.wav', snow_face(), 0.8 )
write( 'spray_can.wav', spray_can(), 0.6 )
