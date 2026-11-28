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

// cg_drain.c -- force drain (issue #19). In the place of the effect of the game, which sprayed red out of the hand as
// if the Force went out of it, the life flows the other way:
//
// - From each one drained a stream into the hand: three crimson strands braided round a gentle arch, turning as they
//   go, with pulses that run towards the hand (ribbons of cg_ribbon.c, shaders of the lightning). Who is drained is
//   worked out as the game does it (ForceShootDrain): levels 1 and 2 the one the view points at within 2048, level 3
//   everybody in the cone of MAX_DRAIN_DISTANCE in front that can be seen.
// - Specks of light come off the body of the drained, are pulled into the stream and along it, smaller as they near
//   the hand, where they are taken in.
// - At the hand a small whirl of specks that spiral into the palm, and a crimson light (the hand casts no shadow of
//   its own light, ext.R_AddShadowLight with the body as owner); the drained glows faintly.
// - Nobody in reach: two faint tendrils reach out in front and curl, flowing back to the hand.
// - When it ends, the streams fade out in DRAIN_FADE_MSEC.
// The one drained sees the colour run out of the picture (screenfx/cg_screenfx.c, EV_FORCE_DRAINED).
//
// Part of Force Looks (v_fxForce).

#include "cgame/cg_local.h"
#include "cgame/force/cg_drain.h"
#include "cgame/force/cg_ribbon.h"

#define DRAIN_LINE_REACH	2048.0f	// levels 1 and 2: the trace of the game
#define DRAIN_CONE_REACH	512.0f	// level 3: MAX_DRAIN_DISTANCE
#define DRAIN_CONE_DOT		0.5f
#define DRAIN_VICTIMS		4
#define DRAIN_LOOK_MSEC		100		// who is drained, worked out that often
#define DRAIN_FADE_MSEC		250
#define STREAM_POINTS		25
#define STREAM_BRAIDS		3
#define STREAM_MOTES		10
#define STREAM_PEEL			0.2f	// of the way of a speck: from the body to the stream
#define WHIRL_MOTES			8
#define WHIRL_RADIUS		9.0f
#define REACH_LENGTH		95.0f	// the tendrils when nobody is in reach

typedef struct {
	int		lastLook;
	int		numVictims;
	int		victims[DRAIN_VICTIMS];
	int		lastCast;
	int		numHands;
	vec3_t	hands[2], dir;
	int		level;
} drainer_t;

static drainer_t	drainers[MAX_GENTITIES];
static qhandle_t	coreShader, glowShader, flareShader;

static const vec3_t streamColor = { 0.85f, 0.08f, 0.16f };
static const vec3_t coreColor = { 1.0f, 0.5f, 0.55f };
static const vec3_t hazeColor = { 0.55f, 0.03f, 0.1f };
static const vec3_t moteColor = { 1.0f, 0.28f, 0.32f };

void CG_DrainInit( void ) {
	memset( drainers, 0, sizeof( drainers ) );
}

void CG_DrainRegister( void ) {
	coreShader = trap->R_RegisterShader( "gfx/vision/arc_core" );
	glowShader = trap->R_RegisterShader( "gfx/vision/arc_glow" );
	flareShader = trap->R_RegisterShader( "gfx/effects/whiteFlare" );		// (of the game)
}

static qboolean Drain_On( void ) {
	return (qboolean)( v_fxForce.integer && coreShader && glowShader );
}

static qboolean Drain_ShadowLight( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameShadowLight", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.R_AddShadowLight ) ? 1 : 0;
	}
	return (qboolean)( has > 0 );
}

static void Drain_Center( int entityNum, vec3_t out ) {
	VectorCopy( cg_entities[entityNum].lerpOrigin, out );
	out[2] += 10.0f;
}

// who is drained: as ForceShootDrain does it, from the origin along the view
static void Drain_Look( drainer_t *d, centity_t *cent, const vec3_t dir, int level ) {
	const int	skip = cent->currentState.number;
	trace_t		tr;
	vec3_t		start, end;
	int			i;

	d->numVictims = 0;
	d->lastLook = cg.time;
	VectorCopy( cent->lerpOrigin, start );
	if ( level < FORCE_LEVEL_3 ) {
		VectorMA( start, DRAIN_LINE_REACH, dir, end );
		CG_Trace( &tr, start, NULL, NULL, end, skip, MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum < ENTITYNUM_WORLD ) {
			const entityState_t *es = &cg_entities[tr.entityNum].currentState;

			if ( ( es->eType == ET_PLAYER || es->eType == ET_NPC ) && !( es->eFlags & EF_DEAD ) ) {
				d->victims[d->numVictims++] = tr.entityNum;
			}
		}
		return;
	}
	for ( i = 0; i < cg.snap->numEntities && d->numVictims < DRAIN_VICTIMS; i++ ) {
		const entityState_t	*es = &cg.snap->entities[i];
		vec3_t				to, way;

		if ( es->number == skip || ( es->eType != ET_PLAYER && es->eType != ET_NPC ) || ( es->eFlags & EF_DEAD ) ) {
			continue;
		}
		Drain_Center( es->number, to );
		VectorSubtract( to, start, way );
		if ( VectorNormalize( way ) > DRAIN_CONE_REACH + 16.0f || DotProduct( way, dir ) < DRAIN_CONE_DOT ) {
			continue;
		}
		CG_Trace( &tr, start, NULL, NULL, to, skip, MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum != es->number ) {
			continue;
		}
		d->victims[d->numVictims++] = es->number;
	}
}

// a point of braid b of the stream from from to to at t (0 at from), turning with the time
static void Stream_Point( const vec3_t from, const vec3_t to, const vec3_t right, const vec3_t up, float length, int b, float t,
	float curl, vec3_t out ) {
	const float time = cg.time * 0.001f;
	const float arch = sinf( M_PI * t );
	const float amp = powf( arch, 0.8f ) * Com_Clamp( 4.0f, 12.0f, 5.0f + length * 0.02f ) * ( 1.0f + curl * t * 2.0f );
	const float phase = t * 9.0f - time * 6.0f + b * ( 2.0f * M_PI / STREAM_BRAIDS );
	int c;

	for ( c = 0; c < 3; c++ ) {
		out[c] = from[c] + ( to[c] - from[c] ) * t;
	}
	VectorMA( out, arch * Q_min( length * 0.05f, 20.0f ), up, out );
	VectorMA( out, cosf( phase ) * amp, right, out );
	VectorMA( out, sinf( phase ) * amp * 0.8f, up, out );
}

// the stream from from (the drained, or the end of a tendril) into the hand; fade: brightness towards from falls off
// (a tendril), level 0 to 1
static void Stream_Draw( const vec3_t from, const vec3_t to, int braids, float curl, qboolean fadeFrom, float level, qboolean haze ) {
	const float		time = cg.time * 0.001f;
	ribbonLayer_t	layers[3];
	ribbon_t		r;
	vec3_t			fwd, right, up;
	float			length;
	int				b, i;

	VectorSubtract( to, from, fwd );
	length = VectorNormalize( fwd );
	if ( length < 4.0f || level <= 0.0f ) {
		return;
	}
	CG_RibbonBasis( fwd, right, up );
	memset( layers, 0, sizeof( layers ) );
	layers[0].shader = glowShader;
	layers[0].halfWidth = 3.0f;
	layers[0].minPixels = 2.0f;
	VectorCopy( streamColor, layers[0].color );
	layers[0].level = 0.75f * level;
	layers[1].shader = coreShader;
	layers[1].halfWidth = 0.4f;
	layers[1].minPixels = 1.0f;
	VectorCopy( coreColor, layers[1].color );
	layers[1].level = 0.45f * level;
	layers[2].shader = glowShader;
	layers[2].halfWidth = 9.0f;
	layers[2].minPixels = 5.0f;
	VectorCopy( hazeColor, layers[2].color );
	layers[2].level = 0.16f * level;

	for ( b = 0; b < braids; b++ ) {
		r.numPoints = STREAM_POINTS;
		for ( i = 0; i < STREAM_POINTS; i++ ) {
			const float t = i / (float)( STREAM_POINTS - 1 );
			// pulses whose crests run towards the hand
			const float pulse = 0.5f + 0.5f * sinf( 2.0f * M_PI * ( 3.0f * t - time * 1.8f ) + b );
			float bright = ( 0.35f + 0.65f * pulse * pulse ) * powf( sinf( M_PI * Q_max( t, 0.02f ) ), 0.3f );

			if ( fadeFrom ) {
				bright *= t * t;
			}
			Stream_Point( from, to, right, up, length, b, t, curl, r.p[i] );
			r.width[i] = 0.6f + 0.4f * sinf( M_PI * t );
			r.bright[i] = bright;
		}
		CG_RibbonDraw( &r, layers, ( haze && b == 0 ) ? 3 : 2 );
	}
}

// specks that come off the body round center and flow along the stream into the hand
static void Stream_Motes( int seedBase, const vec3_t center, const vec3_t to, float level ) {
	const float	time = cg.time * 0.001f;
	vec3_t		fwd, right, up;
	float		length;
	int			m;

	VectorSubtract( to, center, fwd );
	length = VectorNormalize( fwd );
	if ( length < 4.0f ) {
		return;
	}
	CG_RibbonBasis( fwd, right, up );
	for ( m = 0; m < STREAM_MOTES; m++ ) {
		const float	along = time * 0.9f + m / (float)STREAM_MOTES + ( seedBase % 97 ) * 0.0137f;
		const float	u = along - floorf( along );
		int			seed = seedBase * 131 + m * 7 + (int)floorf( along ) * 977;
		vec3_t		at;
		float		size, bright;

		if ( u < STREAM_PEEL ) {
			// off the body: a spot on it, drawn to where the stream starts
			const float	f = u / STREAM_PEEL;
			const float	a = Q_random( &seed ) * 2.0f * M_PI;
			vec3_t		body, start;

			VectorCopy( center, body );
			body[0] += cosf( a ) * 14.0f;
			body[1] += sinf( a ) * 14.0f;
			body[2] += -24.0f + 52.0f * Q_random( &seed );
			Stream_Point( center, to, right, up, length, m % STREAM_BRAIDS, 0.04f, 0.0f, start );
			VectorSubtract( start, body, at );
			VectorMA( body, f * f, at, at );
			size = 1.8f;
			bright = 0.4f + 0.6f * f;
		} else {
			const float t = ( u - STREAM_PEEL ) / ( 1.0f - STREAM_PEEL );

			Stream_Point( center, to, right, up, length, m % STREAM_BRAIDS, t, 0.0f, at );
			size = 2.4f * ( 1.0f - 0.6f * t );
			bright = t > 0.85f ? ( 1.0f - t ) / 0.15f : 1.0f;
		}
		CG_RibbonSprite( flareShader, at, size, moteColor, bright * level );
	}
}

// the specks that whirl into the palm
static void Drain_Whirl( int n, const vec3_t hand, const vec3_t dir, float level ) {
	const float	time = cg.time * 0.001f;
	vec3_t		right, up;
	int			m;

	CG_RibbonBasis( dir, right, up );
	for ( m = 0; m < WHIRL_MOTES; m++ ) {
		const float	life = time * 1.2f + m / (float)WHIRL_MOTES + ( n % 13 ) * 0.07f;
		const float	left = 1.0f - ( life - floorf( life ) );
		const float	a = time * 6.0f + m * ( 2.0f * M_PI / WHIRL_MOTES );
		vec3_t		at;

		VectorMA( hand, cosf( a ) * WHIRL_RADIUS * left, right, at );
		VectorMA( at, sinf( a ) * WHIRL_RADIUS * left, up, at );
		VectorMA( at, 4.0f * left, dir, at );
		CG_RibbonSprite( flareShader, at, 0.8f + 0.6f * left, moteColor, ( 1.0f - left * 0.6f ) * 0.7f * level );
	}
}

static void Drain_Draw( drainer_t *d, centity_t *cent, int n, float level, qboolean casting ) {
	int k, h;

	for ( k = 0; k < d->numVictims; k++ ) {
		vec3_t center;

		Drain_Center( d->victims[k], center );
		for ( h = 0; h < d->numHands; h++ ) {
			Stream_Draw( center, d->hands[h], STREAM_BRAIDS, 0.0f, qfalse, level, (qboolean)( k == 0 ) );
			Stream_Motes( n * 17 + k * 5 + h, center, d->hands[h], level );
		}
		// the drained glows a little
		trap->R_AddLightToScene( center, 70.0f, 0.4f * level, 0.03f * level, 0.06f * level );
	}
	if ( !d->numVictims ) {
		// nobody in reach: tendrils reach out and curl, flowing back
		for ( h = 0; h < d->numHands; h++ ) {
			vec3_t right, up, tip;
			int i;

			CG_RibbonBasis( d->dir, right, up );
			for ( i = 0; i < 2; i++ ) {
				const float swing = sinf( cg.time * 0.0021f + i * 2.4f + h );

				VectorMA( d->hands[h], REACH_LENGTH, d->dir, tip );
				VectorMA( tip, ( i ? -1.0f : 1.0f ) * 18.0f + swing * 10.0f, right, tip );
				VectorMA( tip, cosf( cg.time * 0.0017f + i ) * 12.0f, up, tip );
				Stream_Draw( tip, d->hands[h], 1, 1.0f, qtrue, 0.7f * level, qfalse );
			}
		}
	}
	if ( casting ) {
		const vec3_t glow = { 0.9f, 0.12f, 0.2f };

		for ( h = 0; h < d->numHands; h++ ) {
			Drain_Whirl( n + h * 3, d->hands[h], d->dir, level );
			CG_RibbonSprite( flareShader, d->hands[h], 4.0f + 1.5f * sinf( cg.time * 0.02f ), glow, 0.45f * level );
		}
		if ( Drain_ShadowLight() ) {
			trap->ext.R_AddShadowLight( d->hands[0], 90.0f, 0.5f * level, 0.04f * level, 0.08f * level, 0, cent ? cent->ghoul2 : NULL );
		} else {
			trap->R_AddLightToScene( d->hands[0], 90.0f, 0.5f * level, 0.04f * level, 0.08f * level );
		}
	}
	CG_RibbonFlush();
}

qboolean CG_DrainCast( centity_t *cent, const vec3_t hand, const vec3_t otherHand, const vec3_t dir, int level ) {
	const int	n = cent->currentState.number;
	drainer_t	*d;

	if ( !Drain_On() || n < 0 || n >= MAX_GENTITIES || !cg.snap ) {
		return qfalse;
	}
	d = &drainers[n];
	if ( cg.time - d->lastLook >= DRAIN_LOOK_MSEC || cg.time < d->lastLook || level != d->level ) {
		Drain_Look( d, cent, dir, level );
	}
	d->level = level;
	d->lastCast = cg.time;
	d->numHands = otherHand ? 2 : 1;
	VectorCopy( hand, d->hands[0] );
	VectorCopy( otherHand ? otherHand : hand, d->hands[1] );
	VectorCopy( dir, d->dir );
	Drain_Draw( d, cent, n, 1.0f, qtrue );
	return qtrue;
}

void CG_DrainFrame( void ) {
	int i;

	if ( !Drain_On() ) {
		return;
	}
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		drainer_t	*d = &drainers[i];
		const int	since = cg.time - d->lastCast;

		if ( !d->lastCast || since <= 0 || since >= DRAIN_FADE_MSEC ) {
			continue;
		}
		{
			const float left = 1.0f - since / (float)DRAIN_FADE_MSEC;

			Drain_Draw( d, NULL, i, left * left, qfalse );
		}
	}
}
