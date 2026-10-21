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

// snd_spatial_voice.cpp -- the sounds of a frame: which channel or loop sound of the mixer is which voice, and
// the voices of the voice chat and the songs of the boombox. See snd_spatial.cpp.

#include "snd_spatial_local.h"

#ifdef JP_SPATIAL

spatialStream_t	spatialStreams[MAX_CLIENTS + 1 + SPATIAL_BOXES];

// Which sounds are explosions: "explo" in the name of the file (the folder sound/weapons/explosions also has
// glass and debris in it), or one of these in its path.
static const char *spatialShockPaths[] = {
	"rocket/hit_wall", "proton/impact", "conc_missile/impact", "mine/impact"
};

/*
===============================================================================

THE SOUNDS OF A FRAME

Once a frame snd_dma.cpp names the listener and every sound that has a place in the level: the channels of the
mixer that play one, and the loop sounds. Each finds its voice again or gets a free one.

===============================================================================
*/


void S_SpatialResetVoice( spatialVoice_t *v ) {
	S_SpatialLetGoReflect( v );
	v->stream = NULL;
	qiplDirectEffectReset( v->direct );
	qiplBinauralEffectReset( v->binaural );
	qiplPanningEffectReset( v->panning );
	qiplPathEffectReset( v->path );
	v->shockState[0] = v->shockState[1] = 0.0f;
	v->active = qfalse;
	v->channel = NULL;
	v->sfx = NULL;
}

static qboolean S_SpatialIsExplosion( sfx_t *sfx ) {
	if ( Q_stristr( COM_SkipPath( sfx->sSoundName ), "explo" ) ) {
		return qtrue;
	}
	for ( size_t i = 0; i < ARRAY_LEN( spatialShockPaths ); i++ ) {
		if ( Q_stristr( sfx->sSoundName, spatialShockPaths[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

void S_SpatialBeginFrame( const vec3_t origin, matrix3_t axis ) {
	spatial.frame++;
	spatial.numFallbacks = 0;
	// a teleporter, the view going to another player: no sound came closer by that
	if ( DistanceSquared( origin, spatial.listenerOrigin ) > SPATIAL_LISTENER_JUMP * SPATIAL_LISTENER_JUMP ) {
		spatial.listenerJumped = spatial.frame;
	}
	VectorCopy( origin, spatial.listenerOrigin );
	for ( int i = 0; i < 3; i++ ) {
		VectorCopy( axis[i], spatial.listenerAxis[i] );
	}
}

// A channel is the same sound as last frame if it still plays the sample it started then. A loop sound is
// known by its entity and its sample: the mixer makes its loop channels anew every frame.
static spatialVoice_t *S_SpatialFindVoice( spatialStream_t *stream, channel_t *channel, int entnum, sfx_t *sfx, int startSample ) {
	spatialVoice_t *unused = NULL;

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( !v->active ) {
			if ( !unused ) {
				unused = v;
			}
			continue;
		}
		if ( v->stream != stream || v->sfx != sfx ) {
			continue;
		}
		if ( stream || ( channel ? ( !v->loop && v->channel == channel && v->startSample == startSample ) : ( v->loop && v->entnum == entnum ) ) ) {
			return v;
		}
	}
	if ( unused ) {
		unused->active = qtrue;
		unused->fresh = qtrue;
		unused->stream = stream;
		unused->loop = ( channel || stream ) ? qfalse : qtrue;
		unused->entnum = entnum;
		unused->sfx = sfx;
		unused->startSample = startSample;
		unused->renderedGain = 0.0f;
		unused->sendOnly = qfalse;
		unused->shock = channel ? S_SpatialIsExplosion( sfx ) : qfalse;
		unused->rate = 1.0f;
		unused->ownMix = 0.0f;
		unused->lastTime = -1;
		unused->approach = 0.0f;
	}
	return unused;
}

static void S_SpatialPlaceVoice( spatialVoice_t *v, channel_t *channel, int entchannel, const vec3_t origin, float volume, float fade,
	qboolean voiceVolume ) {
	float dist;
	vec3_t d;

	v->frame = spatial.frame;
	v->channel = channel;
	v->entchannel = entchannel;
	v->volume = volume;
	v->gain = volume * fade;
	v->voiceVolume = voiceVolume;
	VectorCopy( origin, v->origin );

	// where it is from the listener, in his axes: x right, y up, z back. The game has forward, left, up.
	VectorSubtract( origin, spatial.listenerOrigin, d );
	dist = VectorNormalize( d );
	if ( dist < 1.0f ) {
		VectorCopy( spatial.listenerAxis[0], d );
	}
	v->direction.x = -DotProduct( d, spatial.listenerAxis[1] );
	v->direction.y = DotProduct( d, spatial.listenerAxis[2] );
	v->direction.z = -DotProduct( d, spatial.listenerAxis[0] );

	// How fast it comes closer, for its doppler: from distances at least SPATIAL_DOPPLER_SAMPLES of the mixer
	// apart, smoothed. A jump faster than sound (a teleporter, the view going to another player) is no speed.
	v->distance = dist;
	if ( v->lastTime < 0 || s_paintedtime < v->lastTime || spatial.listenerJumped == spatial.frame ) {
		v->lastTime = s_paintedtime;
		v->lastDistance = dist;
		v->approach = 0.0f;
	} else if ( s_paintedtime - v->lastTime >= SPATIAL_DOPPLER_SAMPLES ) {
		const float speed = ( v->lastDistance - dist ) * spatial.audio.samplingRate / (float)( s_paintedtime - v->lastTime );

		if ( fabsf( speed ) < SPATIAL_SOUND_SPEED ) {
			v->approach += ( speed - v->approach ) * SPATIAL_DOPPLER_SMOOTH;
		}
		v->lastTime = s_paintedtime;
		v->lastDistance = dist;
	}

	// With v_spatialPhysical a sound the game starts is heard when it got here, at the speed of sound. Decided
	// until its first block is rendered. A voice of the chat and a loop are not late, the own player is where the
	// listener is.
	if ( v->fresh ) {
		v->delay = ( v_spatialPhysical->integer && channel && !v->loop ) ? (int)( dist / SPATIAL_SOUND_SPEED * spatial.audio.samplingRate ) : 0;
	}

}

// is it the song of a box of the boombox (a stream after the one of the own player)
qboolean S_SpatialBoxVoice( const spatialVoice_t *v ) {
	return ( v->stream && v->stream - spatialStreams > SPATIAL_OWN_STREAM ) ? qtrue : qfalse;
}

// how much more of a box the room gets than its straight way at the distance it is (SPATIAL_BOX_WET_NEAR), 1: as much
float S_SpatialBoxWet( const spatialVoice_t *v ) {
	if ( !S_SpatialBoxVoice( v ) ) {
		return 1.0f;
	}
	return Com_Clamp( 1.0f, SPATIAL_BOX_WET_MAX, v->distance / SPATIAL_BOX_WET_NEAR );
}

// samples of its sound a voice reads per sample heard: the doppler of how fast it comes closer
float S_SpatialRate( const spatialVoice_t *v ) {
	if ( v->stream || v->sendOnly || !s_doppler->integer ) {
		return 1.0f;
	}
	return Com_Clamp( 0.5f, 2.0f, 1.0f + v->approach / SPATIAL_SOUND_SPEED );
}

// The time of the mixer a channel has to stay after the end the mixer sees for it, while its voice still reads
// its sound (an MP3 is decoded through the channel): the doppler can make it end later.
static int S_SpatialTail( const spatialVoice_t *v, const channel_t *ch ) {
	const int length = ch->thesfx->iSoundLengthInSamples;
	const float rate = Q_max( 0.5f, v->rate );
	double end;

	if ( v->fresh ) {
		end = ch->startSample + v->delay + length / rate;
	} else {
		end = v->rendered + ( length - v->readPos ) / rate;
	}
	end -= ch->startSample + length;
	return end > 0.0 ? (int)Q_min( end, (double)SPATIAL_MAX_TAIL ) + SPATIAL_FRAME : 0;
}

// qfalse: no voice is free, the mixer plays the channel the old way
qboolean S_SpatialAddChannel( channel_t *ch, const vec3_t origin, float volume, float fade, qboolean voiceVolume ) {
	spatialVoice_t *v = S_SpatialFindVoice( NULL, ch, ch->entnum, ch->thesfx, (int)ch->startSample );

	if ( !v ) {
		return qfalse;
	}
	S_SpatialPlaceVoice( v, ch, ch->entchannel, origin, volume, fade, voiceVolume );
	v->sendOnly = qfalse;
	ch->spatialTail = S_SpatialTail( v, ch );
	return qtrue;
}

// A sound of the own player. The mixer plays it as it always did, at full volume in both ears: from here only
// the room gets it, see S_SpatialRenderBlock().
void S_SpatialAddSend( channel_t *ch, float volume, qboolean voiceVolume ) {
	spatialVoice_t *v;

	if ( !v_spatialReflections->integer ) {
		return;
	}
	v = S_SpatialFindVoice( NULL, ch, ch->entnum, ch->thesfx, (int)ch->startSample );
	if ( v ) {
		S_SpatialPlaceVoice( v, ch, ch->entchannel, spatial.listenerOrigin, volume, 1.0f, voiceVolume );
		v->sendOnly = qtrue;
	}
}

// qfalse: no voice is free, the mixer plays the loop the old way
qboolean S_SpatialAddLoop( int entnum, sfx_t *sfx, const vec3_t origin, float volume, float fade ) {
	spatialVoice_t *v = S_SpatialFindVoice( NULL, NULL, entnum, sfx, 0 );

	if ( !v ) {
		return qfalse;
	}
	S_SpatialPlaceVoice( v, NULL, CHAN_AUTO, origin, volume, fade, qfalse );
	return qtrue;
}

/*
===============================================================================

VOICES OF THE VOICE CHAT

The cgame hands over what it decoded of a player, a frame of 20 ms at a time as it comes in, and says where his
mouth is and how loud he is to be. Here that is a sound in the level like the others, with one difference: it
is not there ahead of time. The other voices are rendered v_spatialAhead ahead of what is heard. A voice of the
chat is rendered as far as what arrived of it reaches, so it is heard that much later than the cgame would
play it itself.

===============================================================================
*/

int S_SpatialStreamQueued( const spatialStream_t *stream ) {
	return (int)( stream->write - stream->read );
}

// What the cgame hands over of a voice, into the queue of it here. samples: 16 bit, one channel, rate of them a
// second. With no samples it only says where the voice is and how loud. origin NULL: where it was.
// Returns how many samples of it wait here, counted at its rate.
static int S_SpatialStreamWrite( spatialStream_t *stream, const vec3_t origin, float volume, const short *samples, int numSamples, int rate ) {
	float ratio;

	if ( !stream->active ) {
		memset( stream, 0, sizeof( *stream ) );
		stream->active = qtrue;
	}
	if ( origin ) {
		VectorCopy( origin, stream->origin );
	}
	stream->volume = volume;

	// to the rate of the mixer: a line from each sample to the next
	ratio = (float)rate / spatial.audio.samplingRate;
	if ( samples && numSamples > 0 ) {
		stream->lastWrite = Sys_Milliseconds();
		for ( int i = 0; i < numSamples; i++ ) {
			const float next = samples[i] * ( 1.0f / 32768.0f );

			while ( stream->phase < 1.0f ) {
				// full: the game stood still, or nothing renders. The newest is dropped.
				if ( S_SpatialStreamQueued( stream ) < SPATIAL_STREAM_FIFO ) {
					stream->fifo[stream->write & ( SPATIAL_STREAM_FIFO - 1 )] = stream->last + ( next - stream->last ) * stream->phase;
					stream->write++;
				}
				stream->phase += ratio;
			}
			stream->phase -= 1.0f;
			stream->last = next;
		}
	}
	return (int)( S_SpatialStreamQueued( stream ) * ratio );
}

/*
===============
S_SpatialVoice

From the cgame, through its engine interface (ext.S_SpatialVoice): the voice of another player, see
S_SpatialStreamWrite(). Returns how many samples of him wait here, counted at his rate, or -1: the sound of the
level does not run, the cgame plays him itself.
===============
*/
int S_SpatialVoice( int clientNum, const vec3_t origin, float volume, const short *samples, int numSamples, int rate ) {
	if ( !S_SpatialActive() || clientNum < 0 || clientNum >= MAX_CLIENTS + SPATIAL_BOXES || rate < 8000 || rate > 48000 ) {
		return -1;
	}
	// a box of the boombox: behind the stream of the own voice
	if ( clientNum >= MAX_CLIENTS ) {
		return S_SpatialStreamWrite( &spatialStreams[clientNum + 1], origin, volume, samples, numSamples, rate );
	}
	return S_SpatialStreamWrite( &spatialStreams[clientNum], origin, volume, samples, numSamples, rate );
}

/*
===============
S_SpatialOwnVoice

From the cgame (ext.S_SpatialOwnVoice): what the own player says into the voice chat, as it goes out. Nobody
hears himself from a place in the level, so the voice is not played. The room around the listener gets it, like
the other sounds of the own player, and he hears it ring.
===============
*/
void S_SpatialOwnVoice( float volume, const short *samples, int numSamples, int rate ) {
	if ( !S_SpatialActive() || !v_spatialReflections->integer || rate < 8000 || rate > 48000 ) {
		return;
	}
	S_SpatialStreamWrite( &spatialStreams[SPATIAL_OWN_STREAM], NULL, volume, samples, numSamples, rate );
}

// once a frame: every voice of the chat that still has something to say is one of the sounds of the frame
void S_SpatialGatherStreams( void ) {
	const int now = Sys_Milliseconds();

	for ( int i = 0; i < (int)ARRAY_LEN( spatialStreams ); i++ ) {
		spatialStream_t *stream = &spatialStreams[i];
		spatialVoice_t *v;

		if ( !stream->active ) {
			continue;
		}
		if ( now - stream->lastWrite > SPATIAL_STREAM_MSEC && S_SpatialStreamQueued( stream ) < SPATIAL_FRAME ) {
			stream->active = qfalse;
			continue;
		}
		v = S_SpatialFindVoice( stream, NULL, i, NULL, 0 );
		if ( v ) {
			// the own voice has no place of its own: it is where the listener is, and only for the reverb
			v->sendOnly = (qboolean)( i == SPATIAL_OWN_STREAM );
			// CHAN_VOICE_GLOBAL: how loud he is at his distance is in the volume the cgame gave, also for a way
			// round a corner
			S_SpatialPlaceVoice( v, NULL, CHAN_VOICE_GLOBAL, v->sendOnly ? spatial.listenerOrigin : stream->origin, stream->volume, 1.0f,
				qfalse );
		}
	}
}

#endif // JP_SPATIAL
