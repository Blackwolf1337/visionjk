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

// cg_boltsound.c -- what bolts sound like to the own player (issue #22, items 10 and 11). His shots get higher and
// thinner as the weapon runs empty, which needs an engine that starts a sound higher (ext.S_StartSoundPitch,
// v_cgameSoundPitch); an older one plays the shot as it is. And a bolt of somebody else that flies past close to his
// head whizzes by, as a blade does (saber/cg_sabersound.c): once per bolt, from where it passed. Both go with
// v_weaponImpacts.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_boltsound.h"

#define EMPTY_FROM			0.15f		// of the most ammunition the weapon takes: from here on its shot rises
#define EMPTY_PITCH			0.25f		//	up to this much higher when nothing is left

#define WHIZ_SOUNDS			3
#define WHIZ_RANGE			64.0f		// of a bolt to the head
#define WHIZ_GAP_MSEC		100			// a burst of a repeater is one whiz and not one for every bolt
#define WHIZ_GONE_MSEC		250			// not seen for this long: another missile in the same slot

typedef struct {
	int		seen;		// cg.time it was last seen, 0: not yet
	vec3_t	last;		//	and where it was then
	qboolean passed;	// it whizzed by already
} whizTrack_t;

static qboolean		soundPitch;
static sfxHandle_t	whizSounds[WHIZ_SOUNDS];
static int			nextWhiz;
static whizTrack_t	whizTracks[MAX_GENTITIES];

void CG_BoltSoundInit( void ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( "v_cgameSoundPitch", value, sizeof( value ) );
	soundPitch = (qboolean)( atoi( value ) && trap->ext.S_StartSoundPitch );
	memset( whizTracks, 0, sizeof( whizTracks ) );
	nextWhiz = 0;
}

void CG_BoltSoundRegister( void ) {
	int i;

	for ( i = 0; i < WHIZ_SOUNDS; i++ ) {
		whizSounds[i] = trap->S_RegisterSound( va( "sound/vision/bolt/whiz%i.wav", i + 1 ) );
	}
}

// How much higher a shot of the own player is: 1, or up to 1 + EMPTY_PITCH as the ammunition runs out. Only the
// weapons that shoot; a weapon that takes nothing for a shot never runs empty, and what is thrown or laid is no shot.
static float Bolt_EmptyPitch( int weapon, qboolean altFire ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	int					index, cost, most;
	float				left;

	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
	case WP_BLASTER:
	case WP_DISRUPTOR:
	case WP_BOWCASTER:
	case WP_REPEATER:
	case WP_DEMP2:
	case WP_FLECHETTE:
	case WP_ROCKET_LAUNCHER:
	case WP_CONCUSSION:
		break;
	default:
		return 1.0f;
	}
	index = weaponData[weapon].ammoIndex;
	cost = altFire ? weaponData[weapon].altEnergyPerShot : weaponData[weapon].energyPerShot;
	if ( index <= AMMO_NONE || index >= AMMO_MAX || cost <= 0 ) {
		return 1.0f;
	}
	most = ammoData[index].max;
	if ( most <= 0 || ps->ammo[index] < 0 ) {
		return 1.0f;
	}
	left = ps->ammo[index] / (float)most;
	if ( left >= EMPTY_FROM ) {
		return 1.0f;
	}
	return 1.0f + EMPTY_PITCH * ( 1.0f - left / EMPTY_FROM );
}

void CG_FireSound( int entityNum, int weapon, qboolean altFire, sfxHandle_t sfx ) {
	float pitch = 1.0f;

	if ( soundPitch && v_weaponImpacts.integer && entityNum == cg.predictedPlayerState.clientNum ) {
		pitch = Bolt_EmptyPitch( weapon, altFire );
	}
	if ( pitch > 1.0f ) {
		trap->ext.S_StartSoundPitch( NULL, entityNum, CHAN_WEAPON, sfx, pitch );
	} else {
		trap->S_StartSound( NULL, entityNum, CHAN_WEAPON, sfx );
	}
}

// a bolt that flies straight and glows, and not a rocket (it has a sound of its own), a grenade or a thrown saber
static qboolean Bolt_Whizzes( int weapon, qboolean altFire ) {
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
	case WP_BLASTER:
	case WP_BOWCASTER:
	case WP_TURRET:
	case WP_EMPLACED_GUN:
		return qtrue;
	case WP_REPEATER:
	case WP_DEMP2:
	case WP_FLECHETTE:
		return (qboolean)!altFire;
	default:
		return qfalse;
	}
}

// The bolt went from where it was last frame to where it is now. If the point of that way closest to the head lies
// inside it, the bolt passed the head this frame; one that comes towards the head and hits does not get past it.
// The own shots start in front of the eye and fly away, so the head is always behind them.
void CG_BoltWhiz( const centity_t *cent ) {
	const entityState_t	*s = &cent->currentState;
	whizTrack_t			*t;
	vec3_t				head, dir, toHead, closest;
	float				length, along;
	qboolean			fresh;

	if ( !v_weaponImpacts.integer || s->number < 0 || s->number >= MAX_GENTITIES
		|| !Bolt_Whizzes( s->weapon, (qboolean)!!( s->eFlags & EF_ALT_FIRING ) ) ) {
		return;
	}
	t = &whizTracks[s->number];
	if ( t->seen == cg.time ) {
		return;	// (a second view of the same frame)
	}
	VectorSubtract( cent->lerpOrigin, t->last, dir );
	length = VectorNormalize( dir );
	fresh = (qboolean)( !t->seen || cg.time < t->seen || cg.time - t->seen > WHIZ_GONE_MSEC
		|| length > VectorLength( s->pos.trDelta ) * ( cg.time - t->seen ) * 0.002f + WHIZ_RANGE );
	if ( fresh ) {
		t->passed = qfalse;
	} else if ( !t->passed && length > 0.0f && cg.snap && cg.predictedPlayerState.pm_type < PM_DEAD
		&& cg.predictedPlayerState.pm_type != PM_SPECTATOR && !CG_JPInSeparatedDuel() ) {
		VectorCopy( cg.predictedPlayerState.origin, head );
		head[2] += cg.predictedPlayerState.viewheight;
		VectorSubtract( head, t->last, toHead );
		along = DotProduct( toHead, dir );
		if ( along > 0.0f && along <= length ) {
			VectorMA( t->last, along, dir, closest );
			if ( DistanceSquared( closest, head ) < WHIZ_RANGE * WHIZ_RANGE ) {
				t->passed = qtrue;
				if ( cg.time >= nextWhiz ) {
					trap->S_StartSound( closest, ENTITYNUM_WORLD, CHAN_AUTO, whizSounds[Q_irand( 0, WHIZ_SOUNDS - 1 )] );
					nextWhiz = cg.time + WHIZ_GAP_MSEC;
				}
			}
		}
	}
	t->seen = cg.time;
	VectorCopy( cent->lerpOrigin, t->last );
}
