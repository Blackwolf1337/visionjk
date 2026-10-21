/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
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

// spatial_debug.h -- what the sound of the level with Steam Audio (client/snd_spatial.cpp) shows of itself for
// v_spatialDebug: the engine fills it once a frame (ext.S_SpatialDebug of cg_public.h), the cgame draws it
// (cgame/cg_spatialdebug.c). Places are units of the level.

#pragma once

#define SPATIAL_DEBUG_SOUNDS		128
#define SPATIAL_DEBUG_FALLBACKS		32
#define SPATIAL_DEBUG_SEGMENTS		512
#define SPATIAL_DEBUG_RAYS			256
#define SPATIAL_DEBUG_NAME			48

// v_spatialDebug: 1 all of it, or the sum of these
#define SPATIAL_DEBUG_LABELS		2		// over every sound what becomes of it
#define SPATIAL_DEBUG_PATHS			4		// the ways round the corners
#define SPATIAL_DEBUG_REFLECTIONS	8		// rays from the listener that bounce off the walls as off a mirror
#define SPATIAL_DEBUG_PANEL			16		// the numbers, and the wall under the crosshair

// spatialDebug_t.echo
#define SPATIAL_DEBUG_ECHO_LIVE		0		// worked out as the listener goes (v_spatialEcho 0)
#define SPATIAL_DEBUG_ECHO_STORED	1		// looked up at the probes of the level
#define SPATIAL_DEBUG_ECHO_BAKING	2		//	which are being worked out, live until then
#define SPATIAL_DEBUG_ECHO_NONE		3		//	which the level does not have, live

// spatialDebugSound_t.flags
#define SPATIAL_DEBUG_OWN			1		// it has reflections of its own (v_spatialSources)
#define SPATIAL_DEBUG_STREAM		2		// a voice of the voice chat or a box of the boombox
#define SPATIAL_DEBUG_LOOP			4
#define SPATIAL_DEBUG_PATHING		8		// it looks for a way round the corners
#define SPATIAL_DEBUG_SEND			16		// of the own player: only the reverb gets it

// the materials, as snd_spatial.cpp gives them to Steam Audio
enum {
	SPATIAL_DEBUG_GENERIC, SPATIAL_DEBUG_BRICK, SPATIAL_DEBUG_CONCRETE, SPATIAL_DEBUG_CERAMIC, SPATIAL_DEBUG_GRAVEL,
	SPATIAL_DEBUG_CARPET, SPATIAL_DEBUG_GLASS, SPATIAL_DEBUG_PLASTER, SPATIAL_DEBUG_WOOD, SPATIAL_DEBUG_METAL,
	SPATIAL_DEBUG_ROCK, SPATIAL_DEBUG_SKY, SPATIAL_DEBUG_MATERIALS
};

typedef struct spatialDebugSound_s {
	vec3_t		origin;
	char		name[SPATIAL_DEBUG_NAME];
	float		distance;
	float		gain;				// how loud, 0 to 1, with the fade by its distance
	float		occlusion;			// what the walls leave of it: 1 nothing in the way, 0 all of it hidden
	float		transmission[3];	//	and of what is hidden, what comes through: low, middle, high
	int			flags;				// SPATIAL_DEBUG_OWN ...
} spatialDebugSound_t;

typedef struct spatialDebugSegment_s {
	vec3_t		from, to;
	int			kind;				// a way round a corner: 1 blocked. A ray: its material (SPATIAL_DEBUG_GENERIC ...)
	int			bounce;				// a ray: 0 from the listener to the first wall, then 1 ...
} spatialDebugSegment_t;

typedef struct spatialDebugFallback_s {
	vec3_t		origin;
	char		name[SPATIAL_DEBUG_NAME];
} spatialDebugFallback_t;

// A brush model of the snapshot for Steam Audio (ext.S_SpatialMovers): model is its number (entityState_t
// modelindex of a SOLID_BMODEL), gone: it is no longer there (broken, switched off)
typedef struct spatialMover_s {
	int			model;
	qboolean	gone;
	vec3_t		origin, angles;
} spatialMover_t;

typedef struct spatialDebug_s {
	qboolean				active;				// Steam Audio plays the sounds of a level
	vec3_t					listener;			// where he hears from
	qboolean				embree;
	int						threads;
	int						passRays, passBounces;	// of a pass of the reflections
	float					passMsec;			// the last pass of the reflections
	int						passes;
	int						settledPasses;		// passes since the listener last moved: Steam Audio adds them up
	int						voices, maxVoices;	// voices playing, of how many
	int						reflects, reflectSlots;	// sounds with reflections of their own, of how many
	float					direct, reverb;		// how loud the sounds and the reverb are, dB of full scale
	float					reverbVolume;		// v_spatialReverb

	// the wall under the crosshair (SPATIAL_DEBUG_PANEL): what Steam Audio has there
	qboolean				aimHit;
	vec3_t					aimPoint;
	float					aimDistance;
	int						aimMaterial;		// SPATIAL_DEBUG_GENERIC ...
	qboolean				aimByTable;			// the material is from steamaudio/materials.txt, not from the level
	char					aimShader[64];
	qboolean				aimBrush;			// it is a brush model (a door ...), not the world

	int						brushes;			// brush models with walls (doors, lifts ...)
	int						brushesIn;			//	of them in the scene now (not gone)
	int						brushesMoved;		//	of them changed in the scene this frame

	int						echo;				// SPATIAL_DEBUG_ECHO_*, v_spatialEcho
	int						echoProbes;			// ECHO_STORED: at how many probes
	int						echoProgress;		// ECHO_BAKING: percent

	int						numSounds;
	spatialDebugSound_t		sounds[SPATIAL_DEBUG_SOUNDS];
	// sounds no voice was free for this frame: the mixer plays them, which knows no walls
	int						numFallbacks;
	spatialDebugFallback_t	fallbacks[SPATIAL_DEBUG_FALLBACKS];
	// the ways round the corners of the last pass, probe to probe
	int						numSegments;
	spatialDebugSegment_t	segments[SPATIAL_DEBUG_SEGMENTS];
	int						numRays;
	spatialDebugSegment_t	rays[SPATIAL_DEBUG_RAYS];
} spatialDebug_t;
