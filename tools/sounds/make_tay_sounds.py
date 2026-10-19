#!/usr/bin/env python3
# The three short sounds of the TaystJK tab (2026-10-19), made here so no audio of anybody else ships:
# hit.wav (tay_hitSounds), kill.wav (tay_killSound) and notify.wav (tay_notify, the name alert).
# 22050 Hz, 16 bit, mono, as the sounds of the game. Writes into assets/japlus_client/sound/tay/.
# usage: tools/sounds/make_tay_sounds.py
import math
import os
import struct
import wave

RATE = 22050
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, '..', '..', 'assets', 'japlus_client', 'sound', 'tay')


def tones(parts, gain):
	"""parts: (start s, length s, frequency Hz, decay per s) each, summed and faded in over 3 ms"""
	length = max(start + dur for start, dur, _, _ in parts)
	samples = []
	for i in range(int(length * RATE)):
		t = i / RATE
		v = 0.0
		for start, dur, freq, decay in parts:
			if start <= t < start + dur:
				u = t - start
				fade = min(1.0, u / 0.003) * min(1.0, (start + dur - t) / 0.01)
				v += math.sin(2.0 * math.pi * freq * u) * math.exp(-decay * u) * fade
		samples.append(max(-1.0, min(1.0, v * gain)))
	return samples


def write(name, samples):
	with wave.open(os.path.join(out, name), 'wb') as w:
		w.setnchannels(1)
		w.setsampwidth(2)
		w.setframerate(RATE)
		w.writeframes(b''.join(struct.pack('<h', int(s * 32767)) for s in samples))


os.makedirs(out, exist_ok=True)
# a short high tick
write('hit.wav', tones([(0.0, 0.06, 1760.0, 60.0), (0.0, 0.06, 3520.0, 90.0)], 0.30))
# two rising notes
write('kill.wav', tones([(0.0, 0.09, 880.0, 18.0), (0.08, 0.16, 1318.5, 14.0)], 0.32))
# a soft chime of two notes together
write('notify.wav', tones([(0.0, 0.30, 987.8, 9.0), (0.0, 0.30, 1480.0, 12.0)], 0.22))
