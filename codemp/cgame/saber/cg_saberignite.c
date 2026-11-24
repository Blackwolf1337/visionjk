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

// cg_saberignite.c -- a blade going on and off (issue #20): it flickers on its way, and so does its light, and a hilt
// without a sound of its own in its .sab gets one of the three ignition sounds of the game by its name. Client side only: the server keeps
// sending the sound of the game, which is swapped here, so this works on every server.

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberignite.h"
#include "cgame/saber/cg_saberdrawn.h"

#define IGNITE_VOICES	3
#define FLICKER_MSEC	34			// a new brightness about 30 times a second, at any frame rate
#define FLICKER_DEPTH	0.5f
#define EVENT_RANGE		96.0f		// of the spot of a sound event to the player it belongs to
#define EVENT_DEAD_RANGE	16.0f
#define DEFLECT_RANGE		40.0f		// of a deflected shot to the blade that did it
#define DEFLECT_DRAWN_MSEC	120
#define DEFLECT_MSEC		190
#define DEFLECT_STEP_MSEC	22

static sfxHandle_t	voiceOn[IGNITE_VOICES], voiceOff[IGNITE_VOICES];
static sfxHandle_t	stockOn[2], stockOff[2];
static int			deflectTime[MAX_CLIENTS];

void CG_SaberIgniteRegister( void ) {
	int i;

	static const char *const on[IGNITE_VOICES] = { "saberon", "saberonquick", "enemy_saber_on" };
	static const char *const off[IGNITE_VOICES] = { "saberoff", "saberoffquick", "enemy_saber_off" };

	for ( i = 0; i < IGNITE_VOICES; i++ ) {
		voiceOn[i] = trap->S_RegisterSound( va( "sound/weapons/saber/%s.wav", on[i] ) );
		voiceOff[i] = trap->S_RegisterSound( va( "sound/weapons/saber/%s.wav", off[i] ) );
	}
	stockOn[0] = trap->S_RegisterSound( "sound/weapons/saber/saberon.wav" );
	stockOn[1] = trap->S_RegisterSound( "sound/weapons/saber/enemy_saber_on.wav" );
	stockOff[0] = trap->S_RegisterSound( "sound/weapons/saber/saberoff.wav" );
	stockOff[1] = trap->S_RegisterSound( "sound/weapons/saber/enemy_saber_off.wav" );
}

// FNV-1a, folded because hilts are named alike (single_1, single_2) and the low bits alone repeat
static int Ignite_Voice( const saberInfo_t *saber ) {
	unsigned int	hash = 2166136261u;
	const char		*c;

	for ( c = saber->name; *c; c++ ) {
		hash = ( hash ^ (unsigned char)tolower( (unsigned char)*c ) ) * 16777619u;
	}
	return (int)( ( hash ^ ( hash >> 15 ) ) % IGNITE_VOICES );
}

sfxHandle_t CG_SaberIgniteSound( const saberInfo_t *saber, qboolean on ) {
	const sfxHandle_t		sound = on ? saber->soundOn : saber->soundOff;
	const sfxHandle_t		*stock = on ? stockOn : stockOff;

	if ( !v_saberIgnition.integer || !sound || ( sound != stock[0] && sound != stock[1] ) ) {
		return sound;
	}
	return on ? voiceOn[Ignite_Voice( saber )] : voiceOff[Ignite_Voice( saber )];
}

sfxHandle_t CG_SaberIgniteEvent( const vec3_t origin, sfxHandle_t sound ) {
	static int		lastClient = -1, lastTime;
	const qboolean	on = (qboolean)( sound == stockOn[0] || sound == stockOn[1] );
	float			best = EVENT_RANGE * EVENT_RANGE;
	int				i, owner = -1;
	qboolean		dying = qfalse;

	if ( !v_saberIgnition.integer || !sound || !cg.snap || ( !on && sound != stockOff[0] && sound != stockOff[1] ) ) {
		return sound;
	}
	for ( i = 0; i < cgs.maxclients; i++ ) {
		const centity_t	*cent = &cg_entities[i];
		const qboolean	own = (qboolean)( i == cg.snap->ps.clientNum );
		float			d;

		if ( !cgs.clientinfo[i].infoValid || ( !own && !cent->currentValid ) ) {
			continue;
		}
		d = DistanceSquared( origin, own ? cg.predictedPlayerState.origin : cent->lerpOrigin );
		if ( own ? cg.predictedPlayerState.pm_type == PM_DEAD : ( cent->currentState.eFlags & EF_DEAD ) != 0 ) {
			// (the event of a death starts exactly where the player stands)
			if ( !on && d < EVENT_DEAD_RANGE * EVENT_DEAD_RANGE ) {
				dying = qtrue;
			}
			continue;
		}
		if ( ( own ? cg.predictedPlayerState.weapon : cent->currentState.weapon ) == WP_SABER && d < best ) {
			best = d;
			owner = i;
		}
	}
	if ( dying ) {
		return 0;	// the change of weapon of the dead player has played the sound already
	}
	if ( owner < 0 ) {
		return sound;
	}
	{
		// (two sabers send two events at once, the second is the second hilt)
		const clientInfo_t	*ci = &cgs.clientinfo[owner];
		const int			saberNum = ( lastClient == owner && lastTime == cg.time && ci->saber[1].model[0] ) ? 1 : 0;

		lastClient = owner;
		lastTime = cg.time;
		return on ? voiceOn[Ignite_Voice( &ci->saber[saberNum] )] : voiceOff[Ignite_Voice( &ci->saber[saberNum] )];
	}
}

// the blade nearest to a deflected shot, of whoever holds one there. The sparks are GPU sparks (v_gpuSparks), the flicker
// of the blade belongs to v_saberIgnition
void CG_SaberDeflect( const vec3_t at, const vec3_t dir ) {
	float	best = DEFLECT_RANGE * DEFLECT_RANGE;
	int		i, s, b, owner = -1;

	CG_GpuSparks( at, dir, GPUSPARKS_DEFLECT, 1.0f );
	if ( !v_saberIgnition.integer ) {
		return;
	}
	for ( i = 0; i < cgs.maxclients; i++ ) {
		for ( s = 0; s < MAX_SABERS; s++ ) {
			for ( b = 0; b < MAX_BLADES; b++ ) {
				const saberDrawn_t	*blade = CG_SaberDrawnLast( i, s, b );
				vec3_t				to, point;
				float				t, d;

				if ( !blade || cg.time - blade->time > DEFLECT_DRAWN_MSEC ) {
					continue;
				}
				VectorSubtract( at, blade->start, to );
				t = Com_Clamp( 0.0f, 1.0f, DotProduct( to, blade->span ) / DotProduct( blade->span, blade->span ) );
				VectorMA( blade->start, t, blade->span, point );
				d = DistanceSquared( at, point );
				if ( d < best ) {
					best = d;
					owner = i;
				}
			}
		}
	}
	if ( owner >= 0 ) {
		deflectTime[owner] = cg.time;
	}
}

float CG_SaberIgniteFlicker( const bladeInfo_t *blade, int num, int saberNum, int bladeNum ) {
	const int		seed = num * 16 + saberNum * 8 + bladeNum;
	float			target, f;
	unsigned int	n;

	if ( v_saberIgnition.integer && num >= 0 && num < MAX_CLIENTS && deflectTime[num]
		&& cg.time - deflectTime[num] >= 0 && cg.time - deflectTime[num] < DEFLECT_MSEC ) {
		// (a flash that dies down, shaking)
		const float left = 1.0f - ( cg.time - deflectTime[num] ) / (float)DEFLECT_MSEC;

		n = ( (unsigned int)( cg.time / DEFLECT_STEP_MSEC ) + (unsigned int)seed * 7919u ) * 2654435761u;
		return 1.0f + left * ( 0.45f - 0.8f * ( ( n >> 24 ) / 255.0f ) );
	}
	if ( !v_saberIgnition.integer || blade->lengthMax <= 0.0f ) {
		return 1.0f;
	}
	target = blade->desiredLength == -1 ? blade->lengthMax : Q_min( blade->desiredLength, blade->lengthMax );
	if ( blade->length == target ) {
		return 1.0f;
	}
	f = Com_Clamp( 0.0f, 1.0f, blade->length / blade->lengthMax );
	n = ( (unsigned int)( cg.time / FLICKER_MSEC ) + (unsigned int)seed * 7919u ) * 2654435761u;
	// (hardest near the hilt, where the blade is just catching)
	return 1.0f - FLICKER_DEPTH * ( 0.35f + 0.65f * ( 1.0f - f ) ) * ( ( n >> 24 ) / 255.0f );
}
