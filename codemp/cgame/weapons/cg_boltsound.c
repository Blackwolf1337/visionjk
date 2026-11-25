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
//
// Rockets (issue #22, "Rockets"). A rocket that will fly past the head howls past: one sound whose pitch falls as
// that of something passing does, started on the rocket ahead of time, so that the fall is heard when the rocket
// is closest. The doppler of the engine (snd_spatial_voice.cpp) is the real one, and a rocket is at a fifteenth of
// the speed of sound: 7 % of pitch, which nobody hears in its roar. And the ticks of the lock of a vehicle rise
// with every step, as the beeps of the lock of the rocket launcher do (scope/cg_scope.c, v_lockOn).

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_boltsound.h"

#define EMPTY_FROM			0.15f		// of the most ammunition the weapon takes: from here on its shot rises
#define EMPTY_PITCH			0.25f		//	up to this much higher when nothing is left

#define WHIZ_SOUNDS			3
#define WHIZ_RANGE			64.0f		// of a bolt to the head
#define WHIZ_GAP_MSEC		100			// a burst of a repeater is one whiz and not one for every bolt
#define WHIZ_GONE_MSEC		250			// not seen for this long: another missile in the same slot

#define HOWL_SOUNDS			2
#define HOWL_RANGE			320.0f		// of a rocket to the head where it will be closest
#define HOWL_CLEAR			32.0f		//	and not closer than this: that one hits him
#define HOWL_PASS_MSEC		450			// into the howl its pitch falls (HOWL_PASS of tools/sounds/make_bolt_sounds.py)
#define HOWL_LATE			0.5f		// of that: a rocket first seen with less time left passes without it
#define HOWL_SPEED			900.0f		// of the rocket the howl is made for (ROCKET_VELOCITY of the game)
#define HOWL_MAX_PITCH		1.6f		// a faster one, of a vehicle, howls higher and for a shorter time

#define LOCK_TICK_RISE		1.09f		// a tick is this much higher than the one before: lock_1 to lock_8.wav, 883 to 1617 Hz

typedef enum {
	PASS_NONE,
	PASS_WHIZ,		// a bolt
	PASS_HOWL		// a rocket
} passSound_t;

typedef struct {
	int		seen;		// cg.time it was last seen, 0: not yet
	vec3_t	last;		//	and where it was then
	qboolean passed;	// it whizzed or howled by already
} whizTrack_t;

static qboolean		soundPitch;
static sfxHandle_t	whizSounds[WHIZ_SOUNDS];
static sfxHandle_t	howlSounds[HOWL_SOUNDS];
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
	for ( i = 0; i < HOWL_SOUNDS; i++ ) {
		howlSounds[i] = trap->S_RegisterSound( va( "sound/vision/bolt/howl%i.wav", i + 1 ) );
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

// What a missile is heard as when it passes the head. A bolt that flies straight and glows whizzes; a rocket howls,
// and so does the shot of a vehicle that has a sound of its flight (a rocket or a torpedo, CG_Missile() plays it).
// Not a grenade or a thrown saber.
static passSound_t Bolt_PassSound( const entityState_t *s ) {
	const qboolean altFire = (qboolean)!!( s->eFlags & EF_ALT_FIRING );

	if ( s->otherEntityNum2 && s->weapon != WP_SABER && ( s->eFlags & EF_JETPACK_ACTIVE )
		&& s->otherEntityNum2 < MAX_VEH_WEAPONS && g_vehWeaponInfo[s->otherEntityNum2].iLoopSound ) {
		return PASS_HOWL;
	}
	switch ( s->weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
	case WP_BLASTER:
	case WP_BOWCASTER:
	case WP_TURRET:
	case WP_EMPLACED_GUN:
		return PASS_WHIZ;
	case WP_REPEATER:
	case WP_DEMP2:
	case WP_FLECHETTE:
		return altFire ? PASS_NONE : PASS_WHIZ;
	case WP_ROCKET_LAUNCHER:
		return PASS_HOWL;
	default:
		return PASS_NONE;
	}
}

// The rocket flies on as it does now: is it HOWL_PASS_MSEC before the point of its way that is closest to the head,
// and does it get there? Then its howl starts, on the rocket, so that it comes with it and goes with it. A rocket
// that flies away, one that hits him or a wall first, and one seen too late for the fall to fit are not heard; one
// that seeks turns, so this is asked every frame until it howled. A faster rocket is there sooner: its howl is
// played that much higher, which also makes it that much shorter, where the engine can.
static qboolean Bolt_Howl( const centity_t *cent, const vec3_t head ) {
	const entityState_t	*s = &cent->currentState;
	vec3_t				dir, toHead, closest;
	trace_t				tr;
	float				speed, along, miss, pitch, lead, left;

	BG_EvaluateTrajectoryDelta( &s->pos, cg.time, dir );
	speed = VectorNormalize( dir );
	VectorSubtract( head, cent->lerpOrigin, toHead );
	along = DotProduct( toHead, dir );
	if ( speed < 1.0f || along <= 0.0f ) {
		return qfalse;
	}
	VectorMA( cent->lerpOrigin, along, dir, closest );
	miss = DistanceSquared( closest, head );
	if ( miss > HOWL_RANGE * HOWL_RANGE || miss < HOWL_CLEAR * HOWL_CLEAR ) {
		return qfalse;
	}
	pitch = soundPitch ? Com_Clamp( 1.0f, HOWL_MAX_PITCH, speed / HOWL_SPEED ) : 1.0f;
	lead = HOWL_PASS_MSEC * 0.001f / pitch;
	left = along / speed;
	if ( left > lead || left < lead * HOWL_LATE ) {
		return qfalse;
	}
	CG_Trace( &tr, cent->lerpOrigin, NULL, NULL, closest, s->number, MASK_SHOT );
	if ( tr.fraction < 1.0f ) {
		return qfalse;
	}
	if ( pitch > 1.0f ) {
		trap->ext.S_StartSoundPitch( NULL, s->number, CHAN_AUTO, howlSounds[Q_irand( 0, HOWL_SOUNDS - 1 )], pitch );
	} else {
		trap->S_StartSound( NULL, s->number, CHAN_AUTO, howlSounds[Q_irand( 0, HOWL_SOUNDS - 1 )] );
	}
	return qtrue;
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
	passSound_t			sound;

	if ( !v_weaponImpacts.integer || s->number < 0 || s->number >= MAX_GENTITIES ) {
		return;
	}
	sound = Bolt_PassSound( s );
	if ( sound == PASS_NONE ) {
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
	}
	if ( !t->passed && cg.snap && cg.predictedPlayerState.pm_type < PM_DEAD
		&& cg.predictedPlayerState.pm_type != PM_SPECTATOR && !CG_JPInSeparatedDuel() ) {
		VectorCopy( cg.predictedPlayerState.origin, head );
		head[2] += cg.predictedPlayerState.viewheight;
		if ( sound == PASS_HOWL ) {
			t->passed = Bolt_Howl( cent, head );
		} else if ( !fresh && length > 0.0f ) {
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
	}
	t->seen = cg.time;
	VectorCopy( cent->lerpOrigin, t->last );
}

// A tick of a lock that has no beeps of its own (a vehicle, CG_DrawRocketLocking()): step 1 as it is, each one after
// it higher, where the engine can and v_lockOn is on.
void CG_LockTick( const vec3_t origin, sfxHandle_t sfx, int step ) {
	if ( soundPitch && v_lockOn.integer && step > 1 ) {
		trap->ext.S_StartSoundPitch( origin, 0, CHAN_AUTO, sfx, powf( LOCK_TICK_RISE, step - 1 ) );
	} else {
		trap->S_StartSound( origin, 0, CHAN_AUTO, sfx );
	}
}
