/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// snd_spatial_render.cpp -- the voices and the reverb rendered ahead into a ring, which the mixer adds from.
// See snd_spatial.cpp.

#include "snd_spatial_local.h"

#ifdef JP_SPATIAL

/*
===============================================================================

RENDERING

The mixer of the game paints the next fifth of a second again in every frame, so a sound that starts is heard
after 50 ms and a hitch of the game still finds sound in the buffer. It can do that because it keeps nothing
from one sample to the next. The effects of Steam Audio do: each voice has to be rendered once, block after
block. So every voice is rendered ahead on its own, as far as v_spatialAhead says, and added into a ring of
finished sound by the time of the mixer. The mixer adds what the ring has for the samples it paints, as often
as it paints them. A voice that starts adds itself to what the others already put there, so it is heard as
soon as a sound of the old path would be. What a voice is given later (where it is, what is in the way) only
reaches the blocks that are not rendered yet: that is the delay v_spatialAhead stands for.

===============================================================================
*/

// The ring holds sums. A place in it is emptied before the first voice adds to it.
static void S_SpatialRingExtend( int end ) {
	if ( end - spatial.ringEnd > SPATIAL_RING ) {
		spatial.ringEnd = end - SPATIAL_RING;
	}
	for ( int t = spatial.ringEnd; t < end; t++ ) {
		spatial.ring[t & ( SPATIAL_RING - 1 )][0] = 0.0f;
		spatial.ring[t & ( SPATIAL_RING - 1 )][1] = 0.0f;
	}
	if ( end > spatial.ringEnd ) {
		spatial.ringEnd = end;
	}
}

// A block of two channels into the ring from the time c on, its gain going from from to to over the block, in what
// the paint buffer counts in: 16 bit samples times 256. energy: where what it adds is counted, for v_spatialDebug.
static void S_SpatialRingAdd( int c, const float *left, const float *right, float from, float to, double *energy ) {
	const float step = ( to - from ) * ( 1.0f / SPATIAL_FRAME );

	S_SpatialRingExtend( c + SPATIAL_FRAME );
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		float *slot = spatial.ring[( c + i ) & ( SPATIAL_RING - 1 )];
		const float gain = from + step * i;

		slot[0] += left[i] * gain;
		slot[1] += right[i] * gain;
		if ( energy ) {
			*energy += ( left[i] * left[i] + right[i] * right[i] ) * gain * gain;
		}
	}
}

// what a reflection effect is told of the impulse response it is given: the same for all of them
static void S_SpatialReflectionParams( IPLReflectionEffectParams *params ) {
	params->type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	params->numChannels = SPATIAL_REVERB_CHANNELS;
	params->irSize = (int)( SPATIAL_REVERB_SECONDS * spatial.audio.samplingRate );
}

// The samples of a voice for its next block, from v->readPos on, v->rate of its sound for each sample heard.
// qfalse: there are none.
static qboolean S_SpatialSamples( spatialVoice_t *v, float *out ) {
	sfx_t *sfx = v->sfx;
	int have = 0, length;

	if ( v->stream ) {
		// a whole block or nothing: S_SpatialEndFrame() only asks when there is one
		if ( S_SpatialStreamQueued( v->stream ) < SPATIAL_FRAME ) {
			return qfalse;
		}
		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			out[i] = v->stream->fifo[( v->stream->read + i ) & ( SPATIAL_STREAM_FIFO - 1 )];
		}
		v->stream->read += SPATIAL_FRAME;
		return qtrue;
	}
	if ( !sfx || !sfx->bInMemory || sfx->iSoundLengthInSamples <= 0 ) {
		return qfalse;
	}
	length = sfx->iSoundLengthInSamples;
	if ( v->loop ) {
		// round and round
		while ( have < SPATIAL_FRAME ) {
			int got;

			v->readPos = fmod( v->readPos, (double)length );
			got = S_SampleRun( sfx, NULL, v->readPos, v->rate, SPATIAL_FRAME - have, qtrue, out + have );
			if ( got <= 0 ) {
				break;
			}
			have += got;
			v->readPos += got * (double)v->rate;
		}
	} else {
		// what is before its start is silence (the time it takes to get here, see v_spatialPhysical)
		while ( have < SPATIAL_FRAME && v->readPos < 0.0 ) {
			out[have++] = 0.0f;
			v->readPos += v->rate;
		}
		if ( have < SPATIAL_FRAME && v->readPos < length ) {
			const int got = S_SampleRun( sfx, v->channel, v->readPos, v->rate, SPATIAL_FRAME - have, qfalse, out + have );

			v->readPos += ( SPATIAL_FRAME - have ) * (double)v->rate;
			have += Q_max( 0, got );
		} else if ( have < SPATIAL_FRAME ) {
			v->readPos += ( SPATIAL_FRAME - have ) * (double)v->rate;
		}
	}
	if ( have <= 0 ) {
		return qfalse;
	}
	if ( have < SPATIAL_FRAME ) {
		memset( out + have, 0, ( SPATIAL_FRAME - have ) * sizeof( float ) );
	}
	return qtrue;
}

// the next block of a voice into the ring, getting from the gain of its last block to this one
static void S_SpatialRenderBlock( spatialVoice_t *v, float gain ) {
	static float mono[SPATIAL_FRAME], direct[SPATIAL_FRAME], left[SPATIAL_FRAME], right[SPATIAL_FRAME];
	static float whole[SPATIAL_FRAME], pathLeft[SPATIAL_FRAME], pathRight[SPATIAL_FRAME], early[SPATIAL_FRAME];
	float *monoData = mono, *directData = direct, *stereoData[2] = { left, right };
	float *wholeData = whole, *pathData[2] = { pathLeft, pathRight };
	IPLAudioBuffer in = { 1, SPATIAL_FRAME, &monoData };
	IPLAudioBuffer mid = { 1, SPATIAL_FRAME, &directData };
	IPLAudioBuffer out = { 2, SPATIAL_FRAME, stereoData };
	IPLAudioBuffer pathIn = { 1, SPATIAL_FRAME, &wholeData };
	IPLAudioBuffer pathOut = { 2, SPATIAL_FRAME, pathData };
	const int c = v->rendered;
	const float from = v->renderedGain, step = ( gain - v->renderedGain ) * ( 1.0f / SPATIAL_FRAME );
	// the sound without its fade by the distance in a straight line: a way round a corner has a length of its own
	const float unfade = ( v->gain > 0.001f ) ? v->volume / v->gain : 0.0f;
	const qboolean fresh = v->fresh;
	qboolean pathed = qfalse;
	float volume, hidden, hiddenFrom, send;
	const float *reflectIn;

	if ( v->fresh ) {
		v->dryRead = c;
		// where in its sound it begins: a loop where the time of the mixer is in it, as the mixer plays loops
		if ( v->sfx && v->sfx->iSoundLengthInSamples > 0 ) {
			v->readPos = v->loop ? fmod( (double)c, (double)v->sfx->iSoundLengthInSamples ) : (double)( c - v->startSample - v->delay );
		}
	}
	v->rate = S_SpatialRate( v );
	v->rendered += SPATIAL_FRAME;
	v->renderedGain = gain;
	v->fresh = qfalse;
	if ( !S_SpatialSamples( v, mono ) ) {
		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			v->dry[( c + i ) & ( SPATIAL_DRY - 1 )] = 0.0f;
			v->own[( c + i ) & ( SPATIAL_DRY - 1 )] = 0.0f;
		}
		return;
	}
	for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
		mono[i] *= from + step * i;
		whole[i] = mono[i] * unfade;
	}
	// A sound that is late by the way here (v_spatialPhysical) goes into its own reflections as the game plays it:
	// their impulse response has the time to the walls and from them in it. An MP3 can only be read once.
	reflectIn = mono;
	if ( v->delay && v->reflect && v->sfx->eSoundCompressionMethod == ct_16 ) {
		const int got = S_SampleRun( v->sfx, NULL, (double)( c - v->startSample ), 1.0f, SPATIAL_FRAME, qfalse, early );

		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			early[i] = i < got ? early[i] * ( from + step * i ) : 0.0f;
		}
		reflectIn = early;
	}

	// A voice of the chat is as loud as the cgame said, s_volume is not for it. Against the other sounds it is
	// louder by what its two factors differ, which the reverb has to know: it has one factor for all it gets.
	send = v->voiceVolume ? s_volumeVoice->value : s_volume->value;
	if ( v->stream ) {
		send = v_spatialHrtf->integer ? ( SPATIAL_GAIN_VOICE_HRTF / SPATIAL_GAIN_HRTF ) : ( SPATIAL_GAIN_VOICE_PANNING / SPATIAL_GAIN_PANNING );
	}
	if ( v->sendOnly ) {
		// of the own player: the room gets it as the game plays it, the mixer plays the sound itself
		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			v->dry[( c + i ) & ( SPATIAL_DRY - 1 )] = mono[i] * send * SPATIAL_OWN_SEND;
		}
		return;
	}

	qiplDirectEffectApply( v->direct, &v->directParams, &in, &mid );

	// The way round the corners, for as much of the sound as the walls hide: what is in sight needs no way
	// round. Steam Audio gives it the direction it arrives from, which is the last corner. The effect runs while
	// the sound is in sight too, so that it goes on from where it was when the sound goes behind a wall.
	hidden = 1.0f - v->directParams.occlusion;
	hiddenFrom = fresh ? hidden : v->lastHidden;
	v->lastHidden = hidden;
	if ( v->pathing ) {
		IPLPathEffectParams params;

		// what the last pass found, as S_SpatialTakePaths() took it
		memset( &params, 0, sizeof( params ) );
		for ( int i = 0; i < SPATIAL_REVERB_CHANNELS; i++ ) {
			S_SpatialFollow( &v->pathSh[i], v->pathTarget[i], SPATIAL_PATH_SMOOTH, fresh );
		}
		memcpy( params.eqCoeffs, v->pathEq, sizeof( params.eqCoeffs ) );
		params.shCoeffs = v->pathSh;
		params.order = SPATIAL_REVERB_ORDER;
		params.binaural = v_spatialHrtf->integer ? IPL_TRUE : IPL_FALSE;
		params.hrtf = spatial.hrtf;
		// what bending round the corners costs: all of it, or only how much duller the sound gets
		params.normalizeEQ = v_spatialPathLoss->integer ? IPL_FALSE : IPL_TRUE;
		S_SpatialListener( &params.listener );
		qiplPathEffectApply( v->path, &params, &pathIn, &pathOut );
		pathed = qtrue;
	}

	// What the walls in the way left of it, and what came round them, is what the room around the listener
	// gets to hear. Its own reflections get the sound as it leaves it: their impulse response has the walls. A box
	// far away gives the room more than its straight way (S_SpatialBoxWet).
	{
		const float wet = S_SpatialBoxWet( v );

		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			const float through = hiddenFrom + ( hidden - hiddenFrom ) * i * ( 1.0f / SPATIAL_FRAME );

			v->dry[( c + i ) & ( SPATIAL_DRY - 1 )] = ( direct[i] + ( pathed ? ( pathLeft[i] + pathRight[i] ) * 0.5f * through : 0.0f ) ) * send * wet;
			v->own[( c + i ) & ( SPATIAL_DRY - 1 )] = reflectIn[i] * send * wet;
		}
	}
	volume = ( v->stream ? 1.0f : send ) * ( 32768.0f * 256.0f );
	if ( v_spatialHrtf->integer ) {
		IPLBinauralEffectParams params;

		memset( &params, 0, sizeof( params ) );
		params.direction = v->direction;
		params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		params.spatialBlend = 1.0f;
		params.hrtf = spatial.hrtf;
		qiplBinauralEffectApply( v->binaural, &params, &mid, &out );
		volume *= v->stream ? SPATIAL_GAIN_VOICE_HRTF : SPATIAL_GAIN_HRTF;
	} else {
		IPLPanningEffectParams params;

		params.direction = v->direction;
		qiplPanningEffectApply( v->panning, &params, &mid, &out );
		volume *= v->stream ? SPATIAL_GAIN_VOICE_PANNING : SPATIAL_GAIN_PANNING;
	}

	// into the ring, and the way round the corners for as much as is hidden
	S_SpatialRingAdd( c, left, right, volume, volume, &spatial.directEnergy );
	if ( pathed ) {
		S_SpatialRingAdd( c, pathLeft, pathRight, hiddenFrom * volume, hidden * volume, NULL );
	}

	// The thump of an explosion: its lows once more, for both ears alike. A wall takes from them what Steam
	// Audio takes from the lows of any sound behind it, and the block gets there from what the last one had.
	if ( v->shock && v_spatialShock->value > 0.0f ) {
		const float *k = spatial.shockCoeffs;
		const float through = v->directParams.occlusion + ( 1.0f - v->directParams.occlusion ) * v->directParams.transmission[0];
		const float throughFrom = fresh ? through : v->shockThrough;
		const float throughStep = ( through - throughFrom ) * ( 1.0f / SPATIAL_FRAME );
		const float shock = send * v_spatialShock->value * SPATIAL_SHOCK_GAIN * ( 32768.0f * 256.0f );
		float z1 = v->shockState[0], z2 = v->shockState[1];

		for ( int i = 0; i < SPATIAL_FRAME; i++ ) {
			float *slot = spatial.ring[( c + i ) & ( SPATIAL_RING - 1 )];
			const float low = k[0] * mono[i] + z1;
			const float add = low * ( throughFrom + throughStep * i ) * shock;

			z1 = k[1] * mono[i] - k[3] * low + z2;
			z2 = k[2] * mono[i] - k[4] * low;
			slot[0] += add;
			slot[1] += add;
		}
		v->shockState[0] = z1;
		v->shockState[1] = z2;
		v->shockThrough = through;
	}
}

// nothing of the reflections rings on: the level changed, or the time of the mixer went back
void S_SpatialResetReverb( void ) {
	if ( spatial.reverbEffect ) {
		qiplReflectionEffectReset( spatial.reverbEffect );
		qiplAmbisonicsDecodeEffectReset( spatial.reverbDecode );
	}
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		qiplReflectionEffectReset( spatial.reflects[i].effect );
		spatial.reflects[i].ringing = qfalse;
	}
	for ( int i = 0; i < 2; i++ ) {
		if ( spatial.dreamEffect[i] ) {
			qiplReflectionEffectReset( spatial.dreamEffect[i] );
		}
	}
	spatial.dreamMix = 0.0f;
	spatial.dreamTail = 0;
	if ( spatial.reflectMixer ) {
		qiplReflectionMixerReset( spatial.reflectMixer );
	}
}

/*
===============
S_SpatialRenderDream

v_dreamReverb, 0 to 1, from the cgame while Force Sight is on: every sound rings long and dark as in a dream or
another world. Every sound (in, a block from the time c, also those with reflections of their own) goes into an
artificial reverb for each ear, the two a little apart, so it is wide. Its lows ring longest, its highs die first. How much
goes in moves towards v_dreamReverb over a third of a second, and what went in rings on after it is 0.
===============
*/
#define DREAM_FADE			0.3f		// seconds to go from no dream to all of it
#define DREAM_TAIL			10.0f		// seconds it rings after nothing more goes in
#define DREAM_GAIN			2.2f		// of what goes in, against the reverb of the room

static void S_SpatialRenderDream( int c, const float *in ) {
	// seconds the low, middle and high band take to fall by 60 dB, for the left and the right ear
	static const float	times[2][IPL_NUM_BANDS] = { { 8.0f, 6.4f, 3.6f }, { 8.7f, 7.0f, 4.0f } };
	static float		feed[SPATIAL_FRAME], ears[2][SPATIAL_FRAME];
	const float			step = SPATIAL_FRAME / ( DREAM_FADE * spatial.audio.samplingRate );
	const float			from = spatial.dreamMix;
	float				*feedData = feed;
	float				volume;

	if ( !spatial.dreamEffect[0] || !spatial.dreamEffect[1] ) {
		return;
	}
	spatial.dreamMix += Com_Clamp( -step, step, Com_Clamp( 0.0f, 1.0f, v_dreamReverb->value ) - spatial.dreamMix );
	if ( from > 0.0f || spatial.dreamMix > 0.0f ) {
		spatial.dreamTail = (int)( DREAM_TAIL * spatial.audio.samplingRate );
	} else if ( spatial.dreamTail > 0 ) {
		spatial.dreamTail -= SPATIAL_FRAME;
		if ( spatial.dreamTail <= 0 ) {
			qiplReflectionEffectReset( spatial.dreamEffect[0] );
			qiplReflectionEffectReset( spatial.dreamEffect[1] );
			return;
		}
	} else {
		return;
	}
	for ( int k = 0; k < SPATIAL_FRAME; k++ ) {
		feed[k] = in[k] * ( from + ( spatial.dreamMix - from ) * k * ( 1.0f / SPATIAL_FRAME ) );
	}
	for ( int i = 0; i < 2; i++ ) {
		float *earData = ears[i];
		IPLAudioBuffer inBuffer = { 1, SPATIAL_FRAME, &feedData };
		IPLAudioBuffer outBuffer = { 1, SPATIAL_FRAME, &earData };
		IPLReflectionEffectParams params;

		memset( &params, 0, sizeof( params ) );
		params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
		params.numChannels = 1;
		for ( int b = 0; b < IPL_NUM_BANDS; b++ ) {
			params.reverbTimes[b] = times[i][b];
		}
		qiplReflectionEffectApply( spatial.dreamEffect[i], &params, &inBuffer, &outBuffer, NULL );
	}
	volume = DREAM_GAIN * ( v_spatialHrtf->integer ? SPATIAL_GAIN_HRTF : SPATIAL_GAIN_PANNING ) * ( 32768.0f * 256.0f );
	S_SpatialRingAdd( c, ears[0], ears[1], volume, volume, &spatial.reverbEnergy );
}

// one block of the reverb from the time c into the ring, with the reflections of the sounds that have their own
static void S_SpatialRenderReverb( int c ) {
	static float in[SPATIAL_FRAME], ambisonics[SPATIAL_REVERB_CHANNELS][SPATIAL_FRAME], left[SPATIAL_FRAME], right[SPATIAL_FRAME];
	static float all[SPATIAL_FRAME], own[SPATIAL_FRAME], scratch[SPATIAL_REVERB_CHANNELS][SPATIAL_FRAME], mixed[SPATIAL_REVERB_CHANNELS][SPATIAL_FRAME];
	float *inData = in, *ownData = own, *ambisonicsData[SPATIAL_REVERB_CHANNELS], *stereoData[2] = { left, right };
	float *scratchData[SPATIAL_REVERB_CHANNELS], *mixedData[SPATIAL_REVERB_CHANNELS];
	IPLAudioBuffer inBuffer = { 1, SPATIAL_FRAME, &inData };
	IPLAudioBuffer ownBuffer = { 1, SPATIAL_FRAME, &ownData };
	IPLAudioBuffer ambisonicsBuffer = { SPATIAL_REVERB_CHANNELS, SPATIAL_FRAME, ambisonicsData };
	IPLAudioBuffer scratchBuffer = { SPATIAL_REVERB_CHANNELS, SPATIAL_FRAME, scratchData };
	IPLAudioBuffer mixedBuffer = { SPATIAL_REVERB_CHANNELS, SPATIAL_FRAME, mixedData };
	IPLAudioBuffer out = { 2, SPATIAL_FRAME, stereoData };
	IPLSimulationOutputs outputs;
	IPLReflectionEffectParams params;
	IPLAmbisonicsDecodeEffectParams decode;
	float volume;

	for ( int i = 0; i < SPATIAL_REVERB_CHANNELS; i++ ) {
		ambisonicsData[i] = ambisonics[i];
		scratchData[i] = scratch[i];
		mixedData[i] = mixed[i];
	}

	// Every voice gives the next block of what it has rendered, from its first sample on. A sound that starts is
	// put into the ring where the mixer paints, which is behind where the reverb is by then: its start would
	// never get here if the reverb took the voices by the time of the mixer. So the reverb of a sound comes a few
	// blocks late (up to v_spatialAhead), which a room does to a sound anyway. A voice with reflections of its
	// own gives its block to their convolution, which adds it into the mixer of them.
	memset( in, 0, sizeof( in ) );
	memset( all, 0, sizeof( all ) );
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		float mixFrom, mixTo;
		int count;

		if ( !v->active || v->fresh ) {
			continue;
		}
		if ( v->rendered - v->dryRead > SPATIAL_DRY ) {
			v->dryRead = v->rendered - SPATIAL_DRY;
		}
		count = Q_min( SPATIAL_FRAME, v->rendered - v->dryRead );
		memset( &outputs, 0, sizeof( outputs ) );
		if ( v->reflect && S_SpatialReflectReady( v->reflect ) ) {
			qiplSourceGetOutputs( S_SpatialReflectSource( v->reflect, spatial.reflectGpu ), IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs );
		}
		// From the one to the other within a block: a sound that begins in the middle of a convolution is a click.
		// What a sound gave its own goes on ringing there when it goes back to the room of the listener.
		mixFrom = v->ownMix;
		mixTo = outputs.reflections.ir ? 1.0f : 0.0f;
		v->ownMix = mixTo;
		for ( int k = 0; k < count; k++ ) {
			const float mix = mixFrom + ( mixTo - mixFrom ) * k * ( 1.0f / SPATIAL_FRAME );

			in[k] += v->dry[( v->dryRead + k ) & ( SPATIAL_DRY - 1 )] * ( 1.0f - mix );
			all[k] += v->dry[( v->dryRead + k ) & ( SPATIAL_DRY - 1 )];
		}
		if ( outputs.reflections.ir ) {
			memset( own, 0, sizeof( own ) );
			for ( int k = 0; k < count; k++ ) {
				own[k] = v->own[( v->dryRead + k ) & ( SPATIAL_DRY - 1 )] * ( mixFrom + ( mixTo - mixFrom ) * k * ( 1.0f / SPATIAL_FRAME ) );
			}
			params = outputs.reflections;
			S_SpatialReflectionParams( &params );
			qiplReflectionEffectApply( v->reflect->effect, &params, &ownBuffer, &scratchBuffer, spatial.reflectMixer );
		}
		v->dryRead += count;
	}
	// what rings on of the sounds that let go of theirs
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		spatialReflect_t *r = &spatial.reflects[i];

		if ( r->ringing && qiplReflectionEffectGetTail( r->effect, &scratchBuffer, spatial.reflectMixer ) == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE ) {
			qiplReflectionEffectReset( r->effect );
			r->ringing = qfalse;
		}
	}

	S_SpatialRenderDream( c, all );

	// the room where the listener stands, of what the last finished pass found. Before the first one there is
	// nothing to ring.
	memset( &outputs, 0, sizeof( outputs ) );
	qiplSourceGetOutputs( S_SpatialHeardReverb(), IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs );
	params = outputs.reflections;
	if ( params.ir ) {
		S_SpatialReflectionParams( &params );
		qiplReflectionEffectApply( spatial.reverbEffect, &params, &inBuffer, &ambisonicsBuffer, NULL );
	} else {
		memset( ambisonics, 0, sizeof( ambisonics ) );
	}
	if ( spatial.reflectMixer ) {
		memset( &params, 0, sizeof( params ) );
		S_SpatialReflectionParams( &params );
		qiplReflectionMixerApply( spatial.reflectMixer, &params, &mixedBuffer );
		for ( int i = 0; i < SPATIAL_REVERB_CHANNELS; i++ ) {
			for ( int k = 0; k < SPATIAL_FRAME; k++ ) {
				ambisonics[i][k] += mixed[i][k];
			}
		}
	}

	// what comes back comes from all sides of the room: turned to how the listener stands in it
	memset( &decode, 0, sizeof( decode ) );
	decode.order = SPATIAL_REVERB_ORDER;
	decode.hrtf = spatial.hrtf;
	decode.binaural = v_spatialHrtf->integer ? IPL_TRUE : IPL_FALSE;
	S_SpatialListener( &decode.orientation );		// (only its axes count)
	qiplAmbisonicsDecodeEffectApply( spatial.reverbDecode, &decode, &ambisonicsBuffer, &out );

	// how loud each voice is went in with what it gave
	volume = v_spatialReverb->value * ( v_spatialHrtf->integer ? SPATIAL_GAIN_HRTF : SPATIAL_GAIN_PANNING ) * ( 32768.0f * 256.0f );
	S_SpatialRingAdd( c, left, right, volume, volume, &spatial.reverbEnergy );
}

/*
===============
S_SpatialEndFrame

All sounds of the frame are named. from: the first sample the mixer paints now, target: up to which sample the
voices have to be in the ring.
===============
*/
void S_SpatialEndFrame( int from, int target ) {
	const int start = S_SpatialUsec();
	int simulated, voicesDone;

	int limit = target;

	// the time of the mixer went back (the sound was stopped and started again): everything from the start
	if ( from < spatial.lastPaint ) {
		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			if ( spatial.voices[i].active ) {
				S_SpatialResetVoice( &spatial.voices[i] );
			}
		}
		S_SpatialResetReverb();
		spatial.reverbRendered = from;
		spatial.ringEnd = from;
	}
	if ( from > spatial.lastPaint && spatial.lastPaint ) {
		spatial.energySamples += from - spatial.lastPaint;
	}
	spatial.lastPaint = from;
	S_SpatialCapture( from );
	if ( spatial.ringEnd < from ) {
		spatial.ringEnd = from;
	}

	S_SpatialGatherStreams();
	S_SpatialApplyBrushes();
	S_SpatialTakePaths();
	S_SpatialSimulate();
	simulated = S_SpatialUsec();

	spatial.numVoices = 0;
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( !v->active ) {
			continue;
		}
		if ( v->frame != spatial.frame ) {
			// The game no longer names it. What is in the ring of it plays on, one more block lets it fade, so
			// that a sound that was cut off does not end in a click. Its channel may be another sound by now.
			v->channel = NULL;
			if ( !v->fresh && v->rendered >= from ) {
				S_SpatialRenderBlock( v, 0.0f );
			}
			continue;
		}
		spatial.numVoices++;
		if ( v->stream ) {
			// A voice of the chat: not there ahead of time. It starts a block short of where the others are
			// rendered to, and goes on as whole blocks of it arrive. If it runs dry it starts there again.
			if ( v->fresh || v->rendered < from ) {
				v->rendered = Q_max( from, target - SPATIAL_FRAME );
				v->dryRead = v->rendered;
				if ( v->fresh ) {
					v->renderedGain = v->gain;
				}
			}
			while ( v->rendered < target + SPATIAL_FRAME && S_SpatialStreamQueued( v->stream ) >= SPATIAL_FRAME ) {
				S_SpatialRenderBlock( v, v->gain );
			}
			continue;
		}
		if ( v->fresh ) {
			// A sound that starts now starts at full volume with its first sample. One that is found in the
			// middle (a loop sound that comes into reach) fades in.
			if ( !v->loop && v->startSample >= from ) {
				v->rendered = v->startSample;
				v->renderedGain = v->gain;
			} else {
				v->rendered = from;
			}
		} else if ( v->rendered < from ) {
			// the game stood still for longer than was rendered ahead
			v->readPos += ( from - v->rendered ) * (double)v->rate;
			v->rendered = from;
			v->dryRead = from;
		}
		while ( v->rendered < target ) {
			S_SpatialRenderBlock( v, v->gain );
		}
		if ( v->rendered < limit ) {
			limit = v->rendered;
		}
	}

	// the reverb, up to where every voice that plays is rendered, and the next pass of the rays
	voicesDone = S_SpatialUsec();
	if ( v_spatialReflections->integer ) {
		if ( spatial.reverbRendered < from ) {
			spatial.reverbRendered = from;
		}
		while ( spatial.reverbRendered + SPATIAL_FRAME <= limit ) {
			S_SpatialRenderReverb( spatial.reverbRendered );
			spatial.reverbRendered += SPATIAL_FRAME;
		}
		spatial.costUsec[COST_REVERB] += S_SpatialUsec() - voicesDone;
	}
	S_SpatialAskPass();

	// the voices that ended have given the reverb what they had
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( v->active && v->frame != spatial.frame ) {
			S_SpatialResetVoice( v );
		}
	}

	spatial.simulateUsec = simulated - start;
	spatial.renderUsec = S_SpatialUsec() - simulated;
	if ( spatial.renderUsec > spatial.peakRenderUsec ) {
		spatial.peakRenderUsec = spatial.renderUsec;
	}

	// what this frame took, with what S_SpatialUpdate() did of a pass before it (a page)
	spatial.costFrames++;
	spatial.costUsec[COST_SIMULATE] += spatial.simulateUsec;
	spatial.costUsec[COST_VOICES] += voicesDone - simulated;
	spatial.costFrameUsec += S_SpatialUsec() - start;
	spatial.costPeakUsec = Q_max( spatial.costPeakUsec, spatial.costFrameUsec );
	spatial.costFrameUsec = 0;
	spatial.costVoices += spatial.numVoices;
	spatial.costPeakVoices = Q_max( spatial.costPeakVoices, spatial.numVoices );
}

// the mixer paints the samples from to to: what the voices have for them is added
void S_SpatialPaint( int from, int to, portable_samplepair_t *paint ) {
	if ( to > spatial.ringEnd ) {
		to = spatial.ringEnd;
	}
	for ( int t = from; t < to; t++ ) {
		const float *slot = spatial.ring[t & ( SPATIAL_RING - 1 )];

		paint[t - from].left += (int)slot[0];
		paint[t - from].right += (int)slot[1];
	}
}

// How far ahead of what is heard the voices are rendered, in seconds: what a voice is given later (its direction
// when the head turns) is heard that much late. The mixer paints s_mixPreStep ahead of what is heard, and what
// it painted before that is not painted again: the voices have to be there by the next frame, so they are
// SPATIAL_AHEAD_FRAMES frames beyond it, as long as frames take now. And a block more: the reverb is rendered in
// whole blocks up to that, and would never get a block done if it were less (it fell silent above 100 fps).
// Never more than v_spatialAhead.
float S_SpatialAhead( void ) {
	const float pre = Q_max( 0.0f, s_mixPreStep->value ) + (float)SPATIAL_FRAME / spatial.audio.samplingRate;
	const float least = pre + SPATIAL_AHEAD_LEAST;
	const float most = Com_Clamp( least, Q_max( least, 0.5f ), v_spatialAhead->value );

	return Com_Clamp( least, most, pre + SPATIAL_AHEAD_FRAMES * spatial.frameMsec * 0.001f );
}

#endif // JP_SPATIAL
