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

// cg_sabersound.c -- SmoothSwing (issue #20), as replica sabers do it: no sample is pitched. Two loops, one tuned low
// and one high, lie under the hum. How fast the blade turns sets how loud they are and how far the hum ducks, how far
// it has turned crossfades between the two, which is heard as the pitch going up and down through a swing. That needs
// only loops at a volume of their own (ext.S_AddLoopingSoundVolume), so it works with the mixer of the game and with
// Steam Audio. An engine without them (v_cgameLoopVolume) keeps the plain hum.

#include "cgame/cg_local.h"
#include "cgame/saber/cg_sabersound.h"
#include "cgame/saber/cg_saberdrawn.h"

#define SWING_RATE_MIN		90.0f		// degrees a second of the blade: below is the idle animation
#define SWING_RATE_MAX		900.0f		// a full swing
#define SWING_ATTACK_MSEC	25.0f
#define SWING_RELEASE_MSEC	140.0f
#define SWING_CROSS_DEGREES	180.0f		// of turning from the low loop to the high one
#define SWING_HUM_DUCK		0.35f
#define SWING_LAYER			0.6f		// the two loops against the hum: at 1 a fight was 3.4 dB louder than without them
#define SWING_SILENT		0.02f		// no loops below this, there are only 32 in the mixer (MAX_LOOP_SOUNDS)
#define SWING_MAX_OTHERS	3			// players beside the own one who get them, each takes two loops
#define SWING_RANGE			1200.0f
#define SWING_GAP_MSEC		250			// longer without a frame of the blade: it did not turn, it jumped

#define NEAR_SOUNDS			4
#define NEAR_RANGE			30.0f		// of a blade to the head
#define NEAR_SPEED			260.0f		// units a second of that point of the blade: a blade held still is no pass
#define NEAR_GAP_MSEC		420			// per player
#define NEAR_HIT_MSEC		300			// it struck the own player: that is no miss

typedef struct saberSwing_s {
	vec3_t	dir[2];			// of blade 0 of each saber
	int		dirTime[2];
	float	peak;			// degrees a second, the fastest saber since the last hum
	float	rate;			// the same, smoothed
	float	turned;			// degrees, 0 to 360, runs the crossfade
	int		bladeTime;
	int		humTime;
	int		layerTime;
} saberSwing_t;

static saberSwing_t	swings[MAX_GENTITIES];
static sfxHandle_t	swingLow, swingHigh, swingKinds[3][3];
static qboolean		loopVolume;
static int			othersTime, others;
static sfxHandle_t	nearSounds[NEAR_SOUNDS];
static int			nearNext[MAX_CLIENTS];
static qboolean		nearInside[MAX_CLIENTS];

void CG_SaberSoundInit( void ) {
	memset( swings, 0, sizeof( swings ) );
	othersTime = 0;
	others = 0;
	memset( nearNext, 0, sizeof( nearNext ) );
	memset( nearInside, 0, sizeof( nearInside ) );
}

void CG_SaberSoundRegister( void ) {
	// the swings of the game (saberhup1-9), sorted by stance: the short ones for the fast stance, the ones with the
	// higher tone for the medium one, the long and low ones for the strong one
	static const int kinds[3][3] = { { 1, 2, 3 }, { 6, 7, 8 }, { 4, 5, 9 } };
	char	value[8];
	int		k, n;

	swingLow = trap->S_RegisterSound( "sound/vision/saber/swing_low.wav" );
	swingHigh = trap->S_RegisterSound( "sound/vision/saber/swing_high.wav" );
	for ( k = 0; k < 3; k++ ) {
		for ( n = 0; n < 3; n++ ) {
			swingKinds[k][n] = trap->S_RegisterSound( va( "sound/weapons/saber/saberhup%d.wav", kinds[k][n] ) );
		}
	}
	for ( n = 0; n < NEAR_SOUNDS; n++ ) {
		nearSounds[n] = trap->S_RegisterSound( va( "sound/vision/saber/nearmiss%d.wav", n + 1 ) );
	}
	// (the cvar first: a stock engine's table of functions ends before this one)
	trap->Cvar_VariableStringBuffer( "v_cgameLoopVolume", value, sizeof( value ) );
	loopVolume = (qboolean)( atoi( value ) && trap->ext.S_AddLoopingSoundVolume );
}

void CG_SaberSoundBlade( const centity_t *cent, int saberNum, int bladeNum, const vec3_t dir ) {
	const int		num = cent->currentState.number;
	saberSwing_t	*s;
	int				gap;

	if ( !v_saberSounds.integer || bladeNum || saberNum < 0 || saberNum > 1 || num < 0 || num >= MAX_GENTITIES ) {
		return;
	}
	s = &swings[num];
	gap = cg.time - s->dirTime[saberNum];
	if ( !gap ) {
		return;		// drawn twice in a frame
	}
	if ( gap > 0 && gap <= SWING_GAP_MSEC ) {
		const float degrees = RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( s->dir[saberNum], dir ) ) ) );

		s->peak = Q_max( s->peak, degrees * 1000.0f / gap );
		if ( !saberNum ) {
			s->turned = fmodf( s->turned + degrees, SWING_CROSS_DEGREES * 2.0f );
		}
	}
	VectorCopy( dir, s->dir[saberNum] );
	s->dirTime[saberNum] = cg.time;
	s->bladeTime = cg.time;
}

// how much of a swing the blade is in, 0 to 1, moved on once a frame
static float Swing_Level( saberSwing_t *s ) {
	if ( s->humTime != cg.time ) {
		const int	ms = cg.time - s->humTime;
		float		target = s->peak;

		s->humTime = cg.time;
		s->peak = 0.0f;
		if ( ms < 0 || ms > SWING_GAP_MSEC || cg.time - s->bladeTime > SWING_GAP_MSEC ) {
			s->rate = 0.0f;
			target = 0.0f;
		} else {
			s->rate += ( target - s->rate ) * ( 1.0f - expf( -ms / ( target > s->rate ? SWING_ATTACK_MSEC : SWING_RELEASE_MSEC ) ) );
		}
	}
	{
		const float f = Com_Clamp( 0.0f, 1.0f, ( s->rate - SWING_RATE_MIN ) / ( SWING_RATE_MAX - SWING_RATE_MIN ) );

		return f * sqrtf( f );
	}
}

// the own player always, of the others the first few in range: the table of loops is shared with the whole level
static qboolean Swing_HasLoops( int num, const vec3_t spot ) {
	if ( num == cg.snap->ps.clientNum ) {
		return qtrue;
	}
	if ( DistanceSquared( spot, cg.refdef.vieworg ) > SWING_RANGE * SWING_RANGE ) {
		return qfalse;
	}
	if ( othersTime != cg.time ) {
		othersTime = cg.time;
		others = 0;
	}
	if ( others >= SWING_MAX_OTHERS ) {
		return qfalse;
	}
	others++;
	return qtrue;
}

void CG_SaberSoundHum( const centity_t *cent, const vec3_t spot, sfxHandle_t hum ) {
	const int		num = cent->currentState.number;
	saberSwing_t	*s;
	float			level, x;

	if ( !v_saberSounds.integer || !loopVolume || num < 0 || num >= MAX_GENTITIES ) {
		trap->S_AddLoopingSound( num, spot, vec3_origin, hum );
		return;
	}
	s = &swings[num];
	level = Swing_Level( s );
	trap->ext.S_AddLoopingSoundVolume( num, spot, vec3_origin, hum, (int)( 255.0f * ( 1.0f - SWING_HUM_DUCK * level ) ) );

	// (a second saber comes here too, the loops are there once)
	if ( level < SWING_SILENT || s->layerTime == cg.time || !Swing_HasLoops( num, spot ) ) {
		return;
	}
	s->layerTime = cg.time;
	// equal power: 0 at the low loop, a quarter circle at the high one, and back
	x = fabsf( s->turned / SWING_CROSS_DEGREES - 1.0f ) * ( (float)M_PI * 0.5f );
	trap->ext.S_AddLoopingSoundVolume( num, spot, vec3_origin, swingLow, (int)( 255.0f * SWING_LAYER * level * cosf( x ) ) );
	trap->ext.S_AddLoopingSoundVolume( num, spot, vec3_origin, swingHigh, (int)( 255.0f * SWING_LAYER * level * sinf( x ) ) );
}

// the blade of another player passing the head: once per pass, from where it passed
void CG_SaberSoundNearMiss( void ) {
	vec3_t	head;
	int		me, i;

	if ( !v_saberSounds.integer || !cg.snap || cg.predictedPlayerState.pm_type >= PM_DEAD
		|| cg.predictedPlayerState.pm_type == PM_SPECTATOR ) {
		return;
	}
	me = cg.snap->ps.clientNum;
	VectorCopy( cg.predictedPlayerState.origin, head );
	head[2] += cg.predictedPlayerState.viewheight;

	for ( i = 0; i < cgs.maxclients; i++ ) {
		const centity_t	*cent = &cg_entities[i];
		float			best = NEAR_RANGE * NEAR_RANGE * 2.0f, speed = 0.0f;
		vec3_t			at = { 0.0f, 0.0f, 0.0f };
		int				s, b;

		if ( i == me || !cgs.clientinfo[i].infoValid || !cent->currentValid || ( cent->currentState.eFlags & EF_DEAD ) ) {
			nearInside[i] = qfalse;
			continue;
		}
		for ( s = 0; s < MAX_SABERS; s++ ) {
			for ( b = 0; b < MAX_BLADES; b++ ) {
				const saberDrawn_t	*blade = CG_SaberDrawnBlade( i, s, b );
				vec3_t				toHead, point;
				float				t, d;

				if ( !blade ) {
					continue;
				}
				VectorSubtract( head, blade->start, toHead );
				t = Com_Clamp( 0.0f, 1.0f, DotProduct( toHead, blade->span ) / DotProduct( blade->span, blade->span ) );
				VectorMA( blade->start, t, blade->span, point );
				d = DistanceSquared( point, head );
				if ( d >= best ) {
					continue;
				}
				best = d;
				VectorCopy( point, at );
				speed = 0.0f;
				if ( blade->oldTime && cg.time - blade->oldTime <= SWING_GAP_MSEC ) {
					vec3_t was;

					VectorMA( blade->oldStart, t, blade->oldSpan, was );
					speed = Distance( point, was ) * 1000.0f / ( cg.time - blade->oldTime );
				}
			}
		}
		if ( best < NEAR_RANGE * NEAR_RANGE && speed >= NEAR_SPEED && !nearInside[i] && cg.time >= nearNext[i]
			&& !( cent->serverSaberHitIndex == me && cg.time - cent->serverSaberHitTime < NEAR_HIT_MSEC ) ) {
			nearNext[i] = cg.time + NEAR_GAP_MSEC;
			trap->S_StartSound( at, ENTITYNUM_WORLD, CHAN_AUTO, nearSounds[Q_irand( 0, NEAR_SOUNDS - 1 )] );
		}
		// (the way out is wider than the way in, so a blade at the edge does not count as passing again and again)
		nearInside[i] = (qboolean)( best < NEAR_RANGE * NEAR_RANGE * 2.0f );
	}
}

sfxHandle_t CG_SaberSwingSound( int kind ) {
	if ( !v_saberSounds.integer || kind < 0 || kind > 2 ) {
		return 0;
	}
	return swingKinds[kind][Q_irand( 0, 2 )];
}

sfxHandle_t CG_SaberStyleSwingSound( int style ) {
	switch ( style ) {
	case SS_FAST:
	case SS_TAVION:
		return CG_SaberSwingSound( 0 );
	case SS_STRONG:
	case SS_DESANN:
		return CG_SaberSwingSound( 2 );
	default:
		return CG_SaberSwingSound( 1 );
	}
}
