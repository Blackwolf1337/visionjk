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

// snd_spatial_sim.cpp -- what the level does to each sound: what is in its way, every frame in the thread of
// the game, and the passes of the reflections and the paths in a thread of their own. See snd_spatial.cpp.

#include "snd_spatial_local.h"

#ifdef JP_SPATIAL

spatialPassInputs_t	spatialPass;
std::atomic<int>	spatialWorkerState;
std::atomic<int>	spatialWorkerQuit;
std::atomic<int>	spatialPassReflections;
std::atomic<int>	spatialPassPaths;
std::atomic<int>	spatialReflectUsec;
std::atomic<int>	spatialReflectPasses;
std::atomic<int>	spatialPassDebug;
std::atomic<int>	spatialVisCount;
spatialVisSegment_t	spatialVis[SPATIAL_DEBUG_SEGMENTS];

/*
===============================================================================

SIMULATION

What the level does to each sound on its straight way to the listener: how much of the source is hidden, and
what the walls in between let through. Cheap, every frame, in the thread of the game.

===============================================================================
*/

/*
===============
S_SpatialFade

What is left of a sound of a kind of channel at a distance in units. The game's fade (S_AttenuateDistance), or
with v_spatialPhysical that of a real source: full volume as far as the game has it, then 1/r, so a sound twice
as far away is 6 dB quieter and does not end at 38 m, and nothing below SPATIAL_PHYSICAL_QUIET. Touches nothing
but its arguments and a cvar: the thread of the reflections asks it for the length of a way round a corner.
===============
*/
float S_SpatialFade( float dist, int channel ) {
	if ( v_spatialPhysical && v_spatialPhysical->integer ) {
		float dist_mult;
		const float full = S_FullVolumeDistance( channel, &dist_mult );
		const float fade = ( full < 0.0f || dist <= full ) ? 1.0f : full / dist;

		return fade >= SPATIAL_PHYSICAL_QUIET ? fade : 0.0f;
	}
	return S_AttenuateDistance( dist, channel );
}

// Called by Steam Audio in the thread of S_SpatialWorker(), with the length of a way in metres
static float IPLCALL S_SpatialPathFade( IPLfloat32 distance, void *userData ) {
	const float fade = S_SpatialFade( distance * ( 1.0f / SPATIAL_UNIT ), (int)(intptr_t)userData );

	return fade > 0.0f ? fade : 0.0f;
}

// The rays of a pass. Steam Audio spreads the rays the simulator was made for (spatial.maxRays) round the
// listener in the order of their direction, and a pass of fewer takes the first of them. So the simulator is
// made for the rays it traces, see S_SpatialCreateVoices(). Until 2026-10-17 the desktop made it for
// SPATIAL_MAX_RAYS, and the 2048 of the default went into a quarter of the circle.
int S_SpatialRays( void ) {
	return spatial.maxRays;
}

// where the listener is and how he stands, as Steam Audio has it
void S_SpatialListener( IPLCoordinateSpace3 *listener ) {
	vec3_t right;

	VectorNegate( spatial.listenerAxis[1], right );
	S_SpatialPoint( spatial.listenerOrigin, &listener->origin );
	S_SpatialDirection( spatial.listenerAxis[0], &listener->ahead );
	S_SpatialDirection( spatial.listenerAxis[2], &listener->up );
	S_SpatialDirection( right, &listener->right );
}

// a sound lets go of its reflections of its own: they ring out on their own, and it goes on through the reverb
// of the listener
void S_SpatialLetGoReflect( spatialVoice_t *v ) {
	if ( v->reflect ) {
		v->reflect->voice = NULL;
		v->reflect->ringing = qtrue;
		v->reflect = NULL;
	}
}

// a pass with the sound in it is done: its impulse response is its own
qboolean S_SpatialReflectReady( const spatialReflect_t *r ) {
	return (qboolean)( r->askedPass >= 0 && spatialReflectPasses.load() > r->askedPass );
}

/*
===============
S_SpatialChooseReflects

Once a frame: v_spatialSources sounds have reflections of their own. Steam Audio adds the passes for a sound up
as long as neither it nor the listener moves, and begins anew when it gets another sound: a sound that has them
keeps them while it plays, and gives them up only to one SPATIAL_REFLECT_KEEP times as loud, and not in the first
SPATIAL_REFLECT_HOLD. Before 2026-10-21 any sound twice as loud took them after a second: far loops with
v_spatialPhysical took them from a boombox back and forth, and its echo changed while nobody moved. The free
ones go to the loudest. A sound that gets them goes on through the reverb of the listener until a pass with it
in it is done (S_SpatialReflectReady).
===============
*/
static qboolean S_SpatialWantsReflect( const spatialVoice_t *v ) {
	return (qboolean)( v->active && v->frame == spatial.frame && !v->sendOnly );
}

static void S_SpatialChooseReflects( void ) {
	// with the echo of the level from the probes all sounds ring with the room of the listener (v_spatialEcho)
	const int want = ( v_spatialReflections->integer && !S_SpatialEchoBaked() ) ? Com_Clampi( 0, spatial.numReflectSlots, v_spatialSources->integer ) : 0;
	const int now = Sys_Milliseconds();
	qboolean chosen[SPATIAL_VOICES];
	int count = 0;

	memset( chosen, 0, sizeof( chosen ) );
	for ( int i = 0; i < SPATIAL_VOICES && count < want; i++ ) {
		const spatialVoice_t *v = &spatial.voices[i];

		if ( v->reflect && S_SpatialWantsReflect( v ) ) {
			chosen[i] = qtrue;
			count++;
		}
	}
	for ( ; count < want; count++ ) {
		int best = -1;

		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			const spatialVoice_t *v = &spatial.voices[i];

			if ( !chosen[i] && S_SpatialWantsReflect( v ) && v->gain > 0.0f && ( best < 0 || v->gain > spatial.voices[best].gain ) ) {
				best = i;
			}
		}
		if ( best < 0 ) {
			break;
		}
		chosen[best] = qtrue;
	}
	// one much louder takes them from the quietest that had them long enough
	for ( ;; ) {
		int loud = -1, quiet = -1;

		for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
			const spatialVoice_t *v = &spatial.voices[i];

			if ( !chosen[i] && S_SpatialWantsReflect( v ) && ( loud < 0 || v->gain > spatial.voices[loud].gain ) ) {
				loud = i;
			}
			if ( chosen[i] && v->reflect && now - v->reflectSince >= SPATIAL_REFLECT_HOLD
				&& ( quiet < 0 || v->gain < spatial.voices[quiet].gain ) ) {
				quiet = i;
			}
		}
		if ( loud < 0 || quiet < 0 || spatial.voices[loud].gain <= spatial.voices[quiet].gain * SPATIAL_REFLECT_KEEP ) {
			break;
		}
		chosen[quiet] = qfalse;
		chosen[loud] = qtrue;
	}

	// those that lose them first, so that the others find one free
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		if ( !chosen[i] ) {
			S_SpatialLetGoReflect( &spatial.voices[i] );
		}
	}
	spatial.numReflects = 0;
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];

		if ( !chosen[i] ) {
			continue;
		}
		for ( int k = 0; !v->reflect && k < spatial.numReflectSlots; k++ ) {
			spatialReflect_t *r = &spatial.reflects[k];

			if ( !r->voice && !r->ringing ) {
				r->voice = v;
				r->askedPass = -1;
				v->reflect = r;
				v->reflectSince = now;
			}
		}
		if ( v->reflect ) {
			spatial.numReflects++;
		}
	}
}

void S_SpatialSimulate( void ) {
	IPLSimulationSharedInputs shared;

	// the reflections and the ways round the corners get theirs in their own thread, see S_SpatialPassInputs()
	memset( &shared, 0, sizeof( shared ) );
	S_SpatialListener( &shared.listener );
	qiplSimulatorSetSharedInputs( spatial.simulator, IPL_SIMULATIONFLAGS_DIRECT, &shared );
	S_SpatialChooseReflects();

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		IPLSimulationInputs inputs;

		// a voice that plays nothing is not looked at, and neither is one of the own player: nothing is in his way
		memset( &inputs, 0, sizeof( inputs ) );
		if ( v->active && v->frame == spatial.frame && !v->sendOnly ) {
			inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
			// with v_spatialPhysical a far sound loses its highs in the air too (Valve's default model), a box always
			inputs.directFlags = (IPLDirectSimulationFlags)( IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION
				| ( ( v_spatialPhysical->integer || S_SpatialBoxVoice( v ) ) ? IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION : 0 ) );
			IPLVector3 origin;

			S_SpatialPoint( v->origin, &origin );
			S_SpatialSourceSpace( &origin, &inputs.source );
			inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
			inputs.occlusionRadius = SPATIAL_OCCLUSION_RADIUS;
			inputs.numOcclusionSamples = SPATIAL_OCCLUSION_SAMPLES;
			inputs.numTransmissionRays = SPATIAL_TRANSMISSION_RAYS;

			// its way round the corners, from the probes of the level, is looked for in the pass (S_SpatialFillPaths())
			v->pathing = (qboolean)( spatial.probes && v_spatialPaths->integer );
		} else {
			v->pathing = qfalse;
		}
		qiplSourceSetInputs( v->source, IPL_SIMULATIONFLAGS_DIRECT, &inputs );
	}

	qiplSimulatorRunDirect( spatial.simulator );

	// A wall comes between in one frame: what it lets through gets there over SPATIAL_WALL_MSEC, so that a step
	// behind it is a quick fade and not a cut. A sound that starts takes it as it is.
	const float wallStep = 1.0f - expf( -spatial.lastFrameMsec / SPATIAL_WALL_MSEC );

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		spatialVoice_t *v = &spatial.voices[i];
		IPLSimulationOutputs outputs;

		if ( !v->active || v->frame != spatial.frame || v->sendOnly ) {
			continue;
		}
		memset( &outputs, 0, sizeof( outputs ) );
		qiplSourceGetOutputs( v->source, IPL_SIMULATIONFLAGS_DIRECT, &outputs );
		v->directParams = outputs.direct;
		S_SpatialFollow( &v->occlusion, outputs.direct.occlusion, wallStep, v->fresh );
		v->directParams.occlusion = v->occlusion;
		for ( int k = 0; k < 3; k++ ) {
			S_SpatialFollow( &v->transmission[k], outputs.direct.transmission[k], wallStep, v->fresh );
			v->directParams.transmission[k] = v->transmission[k];
		}
		// how loud a sound is at its distance stays what the game makes of it (v->gain)
		v->directParams.flags = (IPLDirectEffectFlags)( IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION
			| ( ( v_spatialPhysical->integer || S_SpatialBoxVoice( v ) ) ? IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION : 0 ) );
		v->directParams.transmissionType = IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
	}
}

/*
===============================================================================

REFLECTIONS

One source stands where the listener stands. In a thread of its own Steam Audio follows rays from there through
the level (v_spatialRays of them, v_spatialBounces times off a wall) and makes of what comes back the impulse
response of the place: how the room rings. Every sound in the level is sent through that one reverb, as it
arrives after the walls in its way. A pass takes longer than a frame, so the game only asks for one and picks
up what the last one found.

The reverb is rendered block after block behind the voices, as far as all of them are rendered, into the same
ring.

===============================================================================
*/

// In the thread of the reflections, before a pass: what spatialPass says it is to start from.
void S_SpatialPassInputs( void ) {
	IPLSimulationSharedInputs shared;
	IPLSimulationInputs inputs;

	memset( &shared, 0, sizeof( shared ) );
	shared.listener = spatialPass.listener;
	shared.numRays = spatialPass.rays;
	shared.numBounces = spatialPass.bounces;
	shared.duration = SPATIAL_REVERB_SECONDS;
	shared.order = SPATIAL_REVERB_ORDER;
	shared.irradianceMinDistance = 1.0f;
	qiplSimulatorSetSharedInputs( spatial.simulator, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared );

	memset( &inputs, 0, sizeof( inputs ) );
	if ( spatialPass.reverb ) {
		inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		inputs.source = spatialPass.listener;
		inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
		// the echo where the listener stands from the probes around him: no ray, nothing that begins anew
		if ( spatialPass.echoBaked ) {
			inputs.baked = IPL_TRUE;
			inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
			inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
		}
	}
	qiplSourceSetInputs( spatial.reverbSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs );

	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		memset( &inputs, 0, sizeof( inputs ) );
		if ( spatialPass.own[i] ) {
			inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
			inputs.source = spatialPass.source[i];
			inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
		}
		qiplSourceSetInputs( spatial.reflects[i].source, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs );
	}
}

// Called by Steam Audio where the paths are worked out, maybe in several threads at once: a stretch of a way
// round the corners, probe to probe, and whether it is blocked now (v_spatialDebug)
static void IPLCALL S_SpatialPathVis( IPLVector3 from, IPLVector3 to, IPLbool occluded, void *userData ) {
	const int n = spatialVisCount.fetch_add( 1 );

	if ( n < SPATIAL_DEBUG_SEGMENTS ) {
		spatialVis[n].from = from;
		spatialVis[n].to = to;
		spatialVis[n].occluded = occluded;
	}
}

// Where iplSimulatorRunPathing() runs, before it: the ways round the corners spatialPass asks for. How loud a sound
// is at the end of its way is what the game makes of a sound at that distance.
void S_SpatialPathInputs( void ) {
	IPLSimulationSharedInputs shared;
	IPLSimulationInputs inputs;

	memset( &shared, 0, sizeof( shared ) );
	shared.listener = spatialPass.listener;
	spatialVisCount.store( 0 );
	if ( spatialPassDebug.load() ) {
		shared.pathingVisCallback = S_SpatialPathVis;
	}
	qiplSimulatorSetSharedInputs( spatial.simulator, IPL_SIMULATIONFLAGS_PATHING, &shared );

	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		memset( &inputs, 0, sizeof( inputs ) );
		if ( spatialPass.path[i] ) {
			inputs.flags = IPL_SIMULATIONFLAGS_PATHING;
			S_SpatialSourceSpace( &spatialPass.pathOrigin[i], &inputs.source );
			inputs.baked = IPL_TRUE;
			inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_PATHING;
			inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
			inputs.pathingProbes = spatial.probes;
			inputs.visRadius = SPATIAL_PATH_RADIUS;
			inputs.visThreshold = SPATIAL_PATH_THRESHOLD;
			inputs.visRange = SPATIAL_PATH_VIS_RANGE;
			inputs.pathingOrder = SPATIAL_REVERB_ORDER;
			inputs.enableValidation = IPL_TRUE;
			inputs.findAlternatePaths = IPL_TRUE;
			inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_CALLBACK;
			inputs.distanceAttenuationModel.callback = S_SpatialPathFade;
			inputs.distanceAttenuationModel.userData = (void *)(intptr_t)spatialPass.pathChannel[i];
		}
		qiplSourceSetInputs( spatial.voices[i].source, IPL_SIMULATIONFLAGS_PATHING, &inputs );
	}
}

// In the thread of the game, while the thread of the reflections is idle: the voices that look for a way round the
// corners in the next pass (S_SpatialSimulate() said which, v->pathing).
void S_SpatialFillPaths( void ) {
	S_SpatialListener( &spatialPass.listener );
	for ( int i = 0; i < SPATIAL_VOICES; i++ ) {
		const spatialVoice_t *v = &spatial.voices[i];

		spatialPass.path[i] = v->pathing;
		if ( v->pathing ) {
			S_SpatialPoint( v->origin, &spatialPass.pathOrigin[i] );
			spatialPass.pathChannel[i] = v->entchannel;
		}
	}
}

// In the thread of the game, while the thread of the reflections is idle: what the next pass is to start from.
static void S_SpatialFillPass( void ) {
	const int passes = spatialReflectPasses.load();

	S_SpatialFillPaths();
	// Steam Audio begins its sum anew when the listener moved by more than a tenth of a millimetre
	{
		const IPLVector3 *o = &spatialPass.listener.origin;
		const float dx = o->x - spatial.settledOrigin.x, dy = o->y - spatial.settledOrigin.y, dz = o->z - spatial.settledOrigin.z;

		spatial.settledPasses = ( dx * dx + dy * dy + dz * dz > 1e-8f ) ? 0 : spatial.settledPasses + 1;
		spatial.settledOrigin = *o;
	}
	spatialPass.rays = S_SpatialRays();
	spatialPass.bounces = S_SpatialBounces();
	spatialPass.reverb = v_spatialReflections->integer ? qtrue : qfalse;
	spatialPass.echoBaked = S_SpatialEchoBaked();
	for ( int i = 0; i < spatial.numReflectSlots; i++ ) {
		spatialReflect_t *r = &spatial.reflects[i];
		const spatialVoice_t *v = r->voice;
		IPLVector3 origin;

		spatialPass.own[i] = (qboolean)( v != NULL );
		if ( !v ) {
			continue;
		}
		S_SpatialPoint( v->origin, &origin );
		S_SpatialSourceSpace( &origin, &spatialPass.source[i] );
		// the pass that is asked now is the first with it in it
		if ( r->askedPass < 0 ) {
			r->askedPass = passes;
		}
	}
}

#ifndef __EMSCRIPTEN__


int SDLCALL S_SpatialWorker( void *unused ) {
	for ( ;; ) {
		SDL_SemWait( spatial.workerWake );
		if ( spatialWorkerQuit.load() ) {
			break;
		}
		if ( spatialWorkerState.load() == WORKER_ASKED ) {
			const int start = S_SpatialUsec();

			spatialWorkerState.store( WORKER_RUNNING );
			if ( spatialPassReflections.load() ) {
				S_SpatialPassInputs();
				qiplSimulatorRunReflections( spatial.simulator );
			}
			if ( spatialPassPaths.load() ) {
				S_SpatialPathInputs();
				qiplSimulatorRunPathing( spatial.simulator );
			}
			spatialReflectUsec.store( S_SpatialUsec() - start );
			spatialReflectPasses.fetch_add( 1 );
			spatialWorkerState.store( WORKER_IDLE );
		}
	}
	return 0;
}

#endif

// The ways round the corners the last pass handed to S_SpatialPathVis(), for v_spatialDebug. Only while the thread
// is idle: then nothing writes them.
static void S_SpatialTakeVis( void ) {
	const int count = Q_min( spatialVisCount.load(), SPATIAL_DEBUG_SEGMENTS );

	if ( !spatialPassDebug.load() ) {
		spatial.numSegments = 0;
		return;
	}
	for ( int i = 0; i < count; i++ ) {
		spatialDebugSegment_t *seg = &spatial.segments[i];

		S_SpatialGamePoint( &spatialVis[i].from, seg->from );
		S_SpatialGamePoint( &spatialVis[i].to, seg->to );
		seg->kind = spatialVis[i].occluded ? 1 : 0;
		seg->bounce = 0;
	}
	spatial.numSegments = count;
}

// The next pass of the thread, if the last one is done and not too long ago: the reflections around the
// listener, and the ways of the sounds round the corners. Never while a level waits to be taken over.
void S_SpatialAskPass( void ) {
	const int now = Sys_Milliseconds();
	qboolean paths = (qboolean)( spatial.probes && v_spatialPaths->integer );
	int idle = WORKER_IDLE;

#ifdef __EMSCRIPTEN__
	S_SpatialPagePaths( paths, now );
	paths = qfalse;
#endif
	if ( ( !v_spatialReflections->integer && !paths ) || spatial.sceneWaits || now - spatial.reflectAsked < SPATIAL_REFLECT_MSEC ) {
		return;
	}
	// the thread reads these only after it was woken
	if ( spatialWorkerState.load() == WORKER_IDLE ) {
		S_SpatialTakeVis();
		spatialPassDebug.store( v_spatialDebug->integer ? 1 : 0 );
		spatialPassReflections.store( v_spatialReflections->integer ? 1 : 0 );
		spatialPassPaths.store( paths ? 1 : 0 );
		S_SpatialFillPass();
		if ( spatialWorkerState.compare_exchange_strong( idle, WORKER_ASKED ) ) {
			spatial.reflectAsked = now;
#ifndef __EMSCRIPTEN__
			SDL_SemPost( spatial.workerWake );
#endif
		}
	}
}

#endif // JP_SPATIAL
