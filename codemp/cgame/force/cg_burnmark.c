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

// cg_burnmark.c -- what a bolt of force lightning burns into the level (issue #19). Each mark is laid onto the level
// once (CG_MarkPolys) and coloured each frame by its age, in layers (shaders in vision_fx.shader, pictures by
// tools/textures/make_burn_lines.py):
//
// - the soot: a Lichtenberg figure, the branching figure a strike burns into what it runs over, black lines with brown
//   scorch beside them and a black pit where it struck. Stays BURN_SOOT_MSEC and fades to the surface.
// - the glow: the same figure white hot at first, then yellow, orange, dark red, gone; the thin ends go dark first, the
//   trunks and the pit last. Wood and cloth glow on longer and flicker as embers.
// - on steel the temper colours round it (straw, bronze, purple, blue), which come up as it cools and stay.
// - while it is hot: a thin smoke rises, the air shimmers over it (the heat haze of the screen effects), a little
//   orange light for a moment; wood throws embers that float up.

#include "cgame/cg_local.h"
#include "cgame/force/cg_burnmark.h"

#define BURN_SPOTS			32			// marks at once, the oldest goes
#define BURN_PATTERNS		4
#define BURN_VERTS			96			// corners and polygons of a layer of one mark (on a flat wall a handful)
#define BURN_POLYS			24
#define BURN_TINT_SCALE		1.9f		// the temper colours reach that much further than the figure
#define BURN_SOOT_MSEC		25000
#define BURN_FADE_MSEC		4000
#define BURN_TINT_MSEC		1400		// the colours come up in this time as it cools
#define BURN_LIGHT_MSEC		500
#define BURN_LIGHTS			4
#define BURN_FAR			3000.0f
#define BURN_HEAT_ID		( ( 1 << 22 ) + 256 )	// the ids of the heat haze (CG_ScreenFxEmit), after those of the saber scars

typedef struct {
	int			numPolys;
	int			counts[BURN_POLYS];
	polyVert_t	verts[BURN_VERTS];
} burnLayer_t;

typedef struct {
	int				birth;			// 0: free
	impactKind_t	kind;
	vec3_t			origin, normal;
	int				pattern;
	float			seed;			// the flicker of embers
	int				nextSmoke;
	burnLayer_t		figure;			// the soot and the glow
	burnLayer_t		tint;			// steel only
} burnSpot_t;

// how each kind of surface takes a strike
typedef struct {
	qboolean	burns;
	float		soot;				// how dark, 0 to 1
	int			hotMsec;			// until the glow is gone
	float		heat;				// how bright it glows, 0 to 1
	int			smokeMsec;			// smoke rises that long
	int			smokeEvery;			// a puff about that often
	qboolean	embers;				// it glows on as embers that flicker, and throws some up
	qboolean	tint;				// temper colours round it
} burnKind_t;

static const burnKind_t burnKinds[IMPACT_KINDS] = {
	/* stone */	{ qtrue,	0.95f,	1500,	0.8f,	2200,	260,	qfalse,	qfalse },
	/* metal */	{ qtrue,	0.8f,	2300,	1.0f,	1200,	320,	qfalse,	qtrue },
	/* wood */	{ qtrue,	1.0f,	4200,	0.9f,	5500,	170,	qtrue,	qfalse },
	/* glass */	{ qtrue,	0.55f,	900,	0.7f,	600,	400,	qfalse,	qfalse },
	/* cold */	{ qfalse,	0.0f,	0,		0.0f,	0,		0,		qfalse,	qfalse },
	/* soft */	{ qtrue,	0.9f,	2800,	0.75f,	3500,	200,	qtrue,	qfalse },
};

static burnSpot_t	spots[BURN_SPOTS];
static int			nextSpot;
static qhandle_t	tintShader, sootShaders[BURN_PATTERNS], glowShaders[BURN_PATTERNS];
static fxHandle_t	smokeFx;

void CG_BurnMarksInit( void ) {
	memset( spots, 0, sizeof( spots ) );
	nextSpot = 0;
}

void CG_BurnMarksRegister( void ) {
	int i;

	// (in this order: the renderer sorts decals of one sort by when their shader was made, and the soot goes over the
	// colours of the steel, the glow over the soot)
	tintShader = trap->R_RegisterShader( "gfx/vision/heattint" );
	for ( i = 0; i < BURN_PATTERNS; i++ ) {
		sootShaders[i] = trap->R_RegisterShader( va( "gfx/vision/burnline%i", i + 1 ) );
	}
	for ( i = 0; i < BURN_PATTERNS; i++ ) {
		glowShaders[i] = trap->R_RegisterShader( va( "gfx/vision/burnglow%i", i + 1 ) );
	}
	// (the thin smoke of the saber scars)
	smokeFx = trap->FX_RegisterEffect( "vision/saber_scorch_smoke.efx" );
}

static void Burn_Lay( burnLayer_t *layer, const vec3_t origin, const vec3_t normal, float orientation, float radius ) {
	layer->numPolys = CG_MarkPolys( origin, normal, orientation, radius, layer->verts, BURN_VERTS, layer->counts, BURN_POLYS );
}

void CG_BurnMark( const vec3_t origin, const vec3_t normal, impactKind_t kind, float size ) {
	const burnKind_t	*how;
	burnSpot_t			*spot;
	float				orientation;

	if ( kind < 0 || kind >= IMPACT_KINDS || !sootShaders[0] || !cg_marks.integer ) {
		return;
	}
	how = &burnKinds[kind];
	if ( !how->burns ) {
		return;
	}
	spot = &spots[nextSpot];
	nextSpot = ( nextSpot + 1 ) % BURN_SPOTS;
	memset( spot, 0, sizeof( *spot ) );
	orientation = flrand( 0.0f, 360.0f );
	Burn_Lay( &spot->figure, origin, normal, orientation, size );
	if ( !spot->figure.numPolys ) {
		return;
	}
	if ( how->tint ) {
		Burn_Lay( &spot->tint, origin, normal, flrand( 0.0f, 360.0f ), size * BURN_TINT_SCALE );
	}
	spot->birth = cg.time ? cg.time : 1;
	spot->kind = kind;
	VectorCopy( origin, spot->origin );
	VectorCopy( normal, spot->normal );
	spot->pattern = Q_irand( 0, BURN_PATTERNS - 1 );
	spot->seed = flrand( 0.0f, 100.0f );
	spot->nextSmoke = cg.time + Q_irand( 40, 120 );

	// wood and cloth throw embers that float up and cool
	if ( how->embers ) {
		sparkBurst_t b;

		memset( &b, 0, sizeof( b ) );
		VectorMA( origin, 2.0f, normal, b.origin );
		VectorCopy( normal, b.dir );
		// (quick: the hot air throws them up and they are gone within a second)
		b.count = 8; b.speed = 150.0f; b.spread = 0.8f; b.life = 0.9f; b.size = 0.3f;
		b.gravity = -220.0f; b.drag = 0.8f; b.cooling = 1.3f; b.swirl = 70.0f;
		VectorSet( b.color, 4.0f, 1.6f, 0.4f );
		CG_EmitSparks( &b );
	}
}

// how hot metal and char look as they cool, t 0 (struck) to 1 (cold)
static void Burn_Heat( float t, vec3_t out ) {
	static const float keys[][4] = {
		{ 0.0f,		1.0f,	0.97f,	0.92f },
		{ 0.07f,	1.0f,	0.84f,	0.52f },
		{ 0.22f,	1.0f,	0.52f,	0.17f },
		{ 0.5f,		0.78f,	0.22f,	0.045f },
		{ 0.78f,	0.34f,	0.06f,	0.008f },
		{ 1.0f,		0.0f,	0.0f,	0.0f },
	};
	int i;

	t = Com_Clamp( 0.0f, 1.0f, t );
	for ( i = 1; i < (int)ARRAY_LEN( keys ) - 1 && t > keys[i][0]; i++ ) {
	}
	{
		const float f = ( t - keys[i - 1][0] ) / ( keys[i][0] - keys[i - 1][0] );
		int c;

		for ( c = 0; c < 3; c++ ) {
			out[c] = keys[i - 1][c + 1] + ( keys[i][c + 1] - keys[i - 1][c + 1] ) * f;
		}
	}
}

static void Burn_Add( burnLayer_t *layer, qhandle_t shader, const vec3_t rgb, float alpha ) {
	byte	color[4];
	int		i, j, first;

	if ( !shader ) {
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		color[i] = (byte)( Com_Clamp( 0.0f, 1.0f, rgb[i] ) * 255.0f );
	}
	color[3] = (byte)( Com_Clamp( 0.0f, 1.0f, alpha ) * 255.0f );
	for ( i = 0, first = 0; i < layer->numPolys; first += layer->counts[i], i++ ) {
		for ( j = 0; j < layer->counts[i]; j++ ) {
			memcpy( layer->verts[first + j].modulate, color, 4 );
		}
		trap->R_AddPolysToScene( shader, layer->counts[i], &layer->verts[first], 1 );
	}
}

void CG_BurnMarksAdd( void ) {
	int lights = 0;
	int k;

	if ( !cg_marks.integer ) {
		return;
	}
	// newest first, they get the lights
	for ( k = 1; k <= BURN_SPOTS; k++ ) {
		const int			slot = ( nextSpot - k + BURN_SPOTS ) % BURN_SPOTS;
		burnSpot_t			*spot = &spots[slot];
		const burnKind_t	*how = &burnKinds[spot->kind];
		const int			age = cg.time - spot->birth;
		float				fade;
		vec3_t				rgb;

		if ( !spot->birth ) {
			continue;
		}
		if ( age < 0 || age >= BURN_SOOT_MSEC + BURN_FADE_MSEC ) {
			spot->birth = 0;
			continue;
		}
		if ( DistanceSquared( spot->origin, cg.refdef.vieworg ) > BURN_FAR * BURN_FAR ) {
			continue;
		}
		fade = age > BURN_SOOT_MSEC ? 1.0f - ( age - BURN_SOOT_MSEC ) / (float)BURN_FADE_MSEC : 1.0f;

		// the colours of the steel, then the soot over them: premultiplied, rgb and alpha fade together to the surface
		if ( spot->tint.numPolys ) {
			const float f = fade * Com_Clamp( 0.0f, 1.0f, age / (float)BURN_TINT_MSEC );

			VectorSet( rgb, f, f, f );
			Burn_Add( &spot->tint, tintShader, rgb, f );
		}
		{
			const float f = fade * how->soot;

			VectorSet( rgb, f, f, f );
			Burn_Add( &spot->figure, sootShaders[spot->pattern], rgb, f );
		}

		if ( age < how->hotMsec ) {
			const float t = age / (float)how->hotMsec;
			vec3_t at;

			Burn_Heat( t, rgb );
			VectorScale( rgb, how->heat, rgb );
			if ( how->embers ) {
				// embers breathe
				VectorScale( rgb, 0.8f + 0.2f * sinf( cg.time * 0.011f + spot->seed ) * sinf( cg.time * 0.0047f + spot->seed * 1.7f ), rgb );
			}
			Burn_Add( &spot->figure, glowShaders[spot->pattern], rgb, 1.0f );

			// the air shimmers over it
			VectorMA( spot->origin, 2.0f, spot->normal, at );
			if ( v_fxForce.integer ) {
				const vec3_t rise = { 0.0f, 0.0f, 18.0f };

				CG_ScreenFxEmit( BURN_HEAT_ID + slot, at, rise, 5.0f, 0.35f * ( 1.0f - t ), 0.0f );
			}
			// and it lights the wall round it for a moment
			if ( age < BURN_LIGHT_MSEC && lights < BURN_LIGHTS ) {
				const float l = 1.0f - age / (float)BURN_LIGHT_MSEC;

				VectorMA( spot->origin, 6.0f, spot->normal, at );
				trap->R_AddLightToScene( at, 24.0f + 40.0f * l, 1.0f * l * how->heat, 0.45f * l * how->heat, 0.12f * l * how->heat );
				lights++;
			}
		}
		// thin smoke while it is hot
		if ( age < how->smokeMsec && cg.time >= spot->nextSmoke && smokeFx ) {
			vec3_t at;

			VectorMA( spot->origin, 1.5f, spot->normal, at );
			trap->FX_PlayEffectID( smokeFx, at, spot->normal, -1, -1, qfalse );
			// (thinner as it cools)
			spot->nextSmoke = cg.time + (int)( how->smokeEvery * ( 1.0f + 2.0f * age / (float)how->smokeMsec ) * flrand( 0.7f, 1.3f ) );
		}
	}
}
