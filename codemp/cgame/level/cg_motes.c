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

// cg_motes.c -- small things in the air of the level (issue #23, "Fireflies and embers", "Dust in light"). Fireflies
// wander and blink over a floor of plants where the level is dark (the floor by steamaudio/materials.txt, the light by
// the light grid), a few around the eye at a time. Lava throws embers, all over it: thin glowing specks of three sorts
// that the air carries up and off in sweeps while they cool (sparks that float, sparkBurst_t swirl of the renderer; on
// GPU Sparks), there from far off. In the light of the lamps fine dust floats, and whoever walks through it parts it.
// Nothing is placed in a level: all of it is looked for around the eye.
//
// On v_levelFx. Only a look: the game knows nothing of it.

#include "cgame/cg_local.h"
#include "cgame/weapons/cg_impact.h"
#include "cgame/level/cg_motes.h"

#define FIREFLIES			20
#define FIREFLY_LOOK		300			// msec between the spots looked at for one
#define FIREFLY_NEAR		120.0f		// units from the eye a new one is, at least and at most
#define FIREFLY_REACH		520.0f
#define FIREFLY_FAR			800.0f		// further from the eye it goes out
#define FIREFLY_DARK		96.0f		// the light of the grid (0 to 255) a place has at most
#define FIREFLY_FADE		700			// msec it takes to come and to go
#define DUST_LAMPS			32			// lamps around the eye that have dust in their light
#define DUST_WARM_SEC		4.0f		// a lamp new to the list gets the specks of that many seconds at once
#define DUST_LAMPS_MSEC		400			// how often they are asked for
#define DUST_REACH			1100.0f		// units from the eye
#define DUST_CONE			120.0f		// how far from a lamp its light shows dust at most (v_dustReach)
#define DUST_SEEN			900.0f		// units from the eye a speck is still made: far ones are faint and come up as the eye nears
#define DUST_SPREAD			0.55f		// how wide the cone is for its length (a half angle of some 29 degrees)
#define DUST_RATE			20.0f		// specks a second tried in the light of a lamp
#define DUST_FACE			48.0f		// the sides of a face, in half edges together, that count as one lamp
#define DUST_BRIGHT			1.0f		// of the colour of the lamp, right under it (v_dustBright)
#define LAVA_SPOTS			512			// places on the lava around the eye that are known, half close by, half further off
#define EMBER_LOOK			25			// msec between the spots looked at for lava while the eye stands
#define EMBER_LOOKS			4			// spots a frame at most for the eye's way, and as many again for a list short of places
#define EMBER_STRIDE		50.0f		// units the eye moves for one more spot looked at
#define EMBER_NEAR			900.0f		// units from the eye: close by
#define EMBER_REACH			2600.0f		// and as far as lava is looked for
#define EMBER_RATE			0.9f		// embers a second off each known place
#define EMBER_WARM			8			// embers a new place has in the air at once
#define EMBER_WARM_FROM		450.0f		// not a place closer to the eye than this: they would be seen to appear
#define EMBER_DEPTH			1600.0f		// lava this far below the eye is still looked for

typedef struct {
	int		birth, life;				// birth 0: none
	vec3_t	home;
	float	roam;						// units round its home
	float	rate[3], phase[4];			// of its way along each axis; phase 3: of its blinking
} firefly_t;

typedef struct {
	qboolean	known;
	vec3_t		origin;
	float		beside;					// units beside it an ember may start
} lavaSpot_t;

static firefly_t	fireflies[FIREFLIES];
static lavaSpot_t	lava[LAVA_SPOTS];
static int			numLava, lavaHead[2];
static vmCvar_t		v_dustAmount, v_dustReach, v_dustBright, v_dustSize;
static levelLamp_t	dustLamps[DUST_LAMPS];
static float		dustWarm[DUST_LAMPS];	// specks a lamp new to the list is still owed
static float		dustOwed[DUST_LAMPS];	// and what each is owed by its rate
static int			numDustLamps, nextLamps;
static int			lavaSeen;			// when a spot looked at was lava last, 0: never in this level
static qhandle_t	glowShader;
static int			nextFirefly, nextEmber;
static int			motesLevel = -1;	// cgs.levelStartTime the above are of

void CG_MotesInit( void ) {
	memset( fireflies, 0, sizeof( fireflies ) );
	memset( lava, 0, sizeof( lava ) );
	numLava = lavaHead[0] = lavaHead[1] = 0;
	lavaSeen = 0;
	numDustLamps = nextLamps = 0;
	nextFirefly = nextEmber = 0;
}

void CG_MotesRegister( void ) {
	glowShader = trap->R_RegisterShader( "gfx/effects/whiteGlow" );		// (of the game)
}

// a spot on the ground around the eye, somewhere from least to reach away: qfalse, there is none there
static qboolean Motes_Ground( float least, float reach, int mask, trace_t *tr ) {
	const float	angle = flrand( 0.0f, 2.0f * M_PI ), dist = flrand( least, reach );
	vec3_t		start, end;

	VectorSet( start, cg.refdef.vieworg[0] + cosf( angle ) * dist, cg.refdef.vieworg[1] + sinf( angle ) * dist,
		cg.refdef.vieworg[2] + 96.0f );
	// (not through the ceiling of the room the eye is in)
	CG_Trace( tr, cg.refdef.vieworg, NULL, NULL, start, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr->startsolid ) {
		return qfalse;
	}
	VectorCopy( tr->endpos, start );
	VectorSet( end, start[0], start[1], start[2] - 600.0f );
	CG_Trace( tr, start, NULL, NULL, end, ENTITYNUM_NONE, mask );
	return (qboolean)( !tr->startsolid && tr->fraction < 1.0f && tr->plane.normal[2] >= 0.7f );
}

// a firefly over a floor of plants in the dark
static void Motes_SeekFirefly( void ) {
	firefly_t	*fly = NULL;
	trace_t		tr;
	vec3_t		point, ambient, directed, dir;
	int			i, material;

	for ( i = 0; i < FIREFLIES; i++ ) {
		if ( !fireflies[i].birth ) {
			fly = &fireflies[i];
			break;
		}
	}
	if ( !fly || !Motes_Ground( FIREFLY_NEAR, FIREFLY_REACH, MASK_SOLID | MASK_WATER, &tr ) || ( tr.contents & MASK_WATER )
		|| !CG_ImpactMaterial( tr.endpos, tr.plane.normal, qfalse, &material, NULL ) ) {
		return;
	}
	if ( material != MATERIAL_SHORTGRASS && material != MATERIAL_LONGGRASS && material != MATERIAL_GREENLEAVES ) {
		return;
	}
	VectorSet( point, tr.endpos[0], tr.endpos[1], tr.endpos[2] + flrand( 22.0f, 70.0f ) );
	if ( !trap->R_LightForPoint( point, ambient, directed, dir )
		|| ( ambient[0] + ambient[1] + ambient[2] ) / 3.0f + ( directed[0] + directed[1] + directed[2] ) / 6.0f > FIREFLY_DARK ) {
		return;
	}
	fly->birth = cg.time;
	fly->life = Q_irand( 9000, 16000 );
	fly->roam = flrand( 14.0f, 34.0f );
	VectorCopy( point, fly->home );
	for ( i = 0; i < 3; i++ ) {
		fly->rate[i] = flrand( 0.35f, 0.9f );
	}
	for ( i = 0; i < 4; i++ ) {
		fly->phase[i] = flrand( 0.0f, 2.0f * M_PI );
	}
}

static void Motes_AddFireflies( void ) {
	refEntity_t	re;
	int			i;

	if ( !glowShader ) {
		return;
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	re.customShader = glowShader;
	for ( i = 0; i < FIREFLIES; i++ ) {
		firefly_t	*fly = &fireflies[i];
		const int	age = cg.time - fly->birth;
		float		t, glow;

		if ( !fly->birth ) {
			continue;
		}
		if ( age < 0 || age >= fly->life || DistanceSquared( fly->home, cg.refdef.vieworg ) > FIREFLY_FAR * FIREFLY_FAR ) {
			fly->birth = 0;
			continue;
		}
		t = age * 0.001f;
		// (it lights up for a moment and is dark for longer; it comes and goes softly)
		glow = sinf( t * 2.1f + fly->phase[3] );
		glow = glow > 0.0f ? glow * glow : 0.0f;
		glow *= Q_min( 1.0f, Q_min( age, fly->life - age ) / (float)FIREFLY_FADE );
		if ( glow < 0.03f ) {
			continue;
		}
		re.origin[0] = fly->home[0] + sinf( t * fly->rate[0] + fly->phase[0] ) * fly->roam;
		re.origin[1] = fly->home[1] + sinf( t * fly->rate[1] + fly->phase[1] ) * fly->roam;
		re.origin[2] = fly->home[2] + sinf( t * fly->rate[2] + fly->phase[2] ) * fly->roam * 0.35f;
		re.radius = 1.8f + 1.8f * glow;
		re.shaderRGBA[0] = (byte)( 200.0f * glow );
		re.shaderRGBA[1] = (byte)( 255.0f * glow );
		re.shaderRGBA[2] = (byte)( 95.0f * glow );
		re.shaderRGBA[3] = 255;
		trap->R_AddRefEntityToScene( &re );
	}
}

// The embers lava throws, three sorts of them, each ember a little its own: most are fine specks that soon go out, some
// are thrown up fast and show as a short bright line, a few are larger flakes that drift slowly and glow long and dark.
typedef struct {
	float	share;				// of all of them
	float	size, streak;		// half its width in units; seconds of its way it is drawn out by
	float	speed, lift, drag;	// thrown up at most this fast; what lifts it while it is hot; how fast the air takes it
	float	swirl;
	float	life, cooling;
	vec3_t	color;
} emberLook_t;

static const emberLook_t emberLooks[] = {
	{ 0.62f,	0.19f, 0.05f,	70.0f, 110.0f, 0.9f,	90.0f,	6.0f, 1.0f,		{ 7.0f, 2.6f, 0.5f } },
	{ 0.22f,	0.24f, 0.075f,	170.0f, 150.0f, 0.7f,	110.0f,	6.5f, 0.9f,		{ 9.0f, 3.8f, 0.9f } },
	{ 0.16f,	0.45f, 0.02f,	40.0f, 60.0f, 1.2f,		70.0f,	10.0f, 1.25f,	{ 4.5f, 1.4f, 0.25f } },
};

// One ember off the lava at this spot. age above 0: it is put into the air above the spot where it would be by now
// had it left the lava that part of its life ago, as cool as that.
static void Motes_Ember( const vec3_t spot, float age ) {
	const emberLook_t	*look = &emberLooks[0];
	sparkBurst_t		b;
	float				pick = flrand( 0.0f, 1.0f ), warm;
	int					i;

	for ( i = 0; i < (int)ARRAY_LEN( emberLooks ) - 1 && pick > emberLooks[i].share; i++ ) {
		pick -= emberLooks[i].share;
	}
	look = &emberLooks[i];
	memset( &b, 0, sizeof( b ) );
	VectorCopy( spot, b.origin );
	VectorSet( b.dir, 0.0f, 0.0f, 1.0f );
	b.count = 1;
	b.speed = look->speed;
	b.spread = 0.6f;
	b.life = look->life * flrand( 0.8f, 1.15f );
	b.size = look->size * flrand( 0.75f, 1.3f );
	b.gravity = -look->lift * flrand( 0.8f, 1.2f );
	b.drag = look->drag;
	b.bounce = 0.2f;
	b.cooling = look->cooling;
	b.swirl = look->swirl * flrand( 0.8f, 1.2f );
	b.streak = look->streak * flrand( 0.6f, 1.4f );
	// (some burn a little hotter and yellower, some redder)
	warm = flrand( -0.25f, 0.25f );
	VectorSet( b.color, look->color[0], look->color[1] * ( 1.0f + warm ), look->color[2] * ( 1.0f + 2.0f * warm ) );
	if ( age > 0.0f ) {
		// how high the heat has lifted it by now: it rises as fast as what lifts it and the air allow, less as it cools
		// (about three quarters of its life is what one lives on average)
		const float whole = b.life * 0.72f, t = age * whole;
		const float height = look->lift / look->drag * ( t - t * t / ( 2.0f * whole ) ) * flrand( 0.7f, 1.1f );

		b.age = age;
		b.speed *= 0.3f;
		b.origin[0] += flrand( -1.0f, 1.0f ) * height * 0.3f;
		b.origin[1] += flrand( -1.0f, 1.0f ) * height * 0.3f;
		b.origin[2] += height;
	}
	CG_EmitSparks( &b );
}

// Where the lava around the eye is: spots are looked at all the time, those on lava kept, the oldest replaced; half of
// the list for the lava close by, half for the lava further off. So the list comes to hold places all over the lava
// around and follows the eye, and lava ahead is known long before the eye is there. A place that is new and not right
// in front of the eye gets embers at once, already in the air as if it had thrown them for a while: the air over a
// lake of lava is full when the eye comes to it. The collision does not find the lava of the Taspir levels, a floor
// nobody sees lies over it: the renderer says what is drawn below a spot (ext.R_SurfacePick). ring: 0 for a spot close
// by, 1 for one further off.
static void Motes_SeekLava( int ring ) {
	static int	checked = -1;
	// (more of them where the eye looks, the rest all round, so a turn does not find the air empty; the root spreads
	// them evenly over the ground and not towards the eye)
	const float	angle = flrand( 0.0f, 1.0f ) < 0.6f ? DEG2RAD( cg.refdef.viewangles[YAW] + flrand( -65.0f, 65.0f ) ) : flrand( 0.0f, 2.0f * M_PI );
	const float	dist = ring ? sqrtf( EMBER_NEAR * EMBER_NEAR + flrand( 0.0f, 1.0f ) * ( EMBER_REACH * EMBER_REACH - EMBER_NEAR * EMBER_NEAR ) )
		: EMBER_NEAR * sqrtf( flrand( 0.0f, 1.0f ) );
	lavaSpot_t	*spot;
	vec3_t		start, end, at;
	int			contents, i;

	if ( checked < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameSurfacePick", value, sizeof( value ) );
		checked = atoi( value ) == 1 ? 1 : 0;
	}
	if ( !checked || !trap->ext.R_SurfacePick ) {
		return;
	}
	VectorSet( start, cg.refdef.vieworg[0] + cosf( angle ) * dist, cg.refdef.vieworg[1] + sinf( angle ) * dist,
		cg.refdef.vieworg[2] + 32.0f );
	VectorSet( end, start[0], start[1], start[2] - EMBER_DEPTH );
	if ( !trap->ext.R_SurfacePick( start, end, at, &contents ) || !( contents & CONTENTS_LAVA ) ) {
		return;
	}
	lavaSeen = cg.time;
	spot = &lava[ring * ( LAVA_SPOTS / 2 ) + lavaHead[ring]];
	lavaHead[ring] = ( lavaHead[ring] + 1 ) % ( LAVA_SPOTS / 2 );
	// (new to the list, or the place it takes was left behind long ago: the air over it is empty)
	if ( ( !spot->known || Distance( spot->origin, cg.refdef.vieworg ) > EMBER_REACH ) && dist > EMBER_WARM_FROM ) {
		for ( i = 0; i < EMBER_WARM; i++ ) {
			vec3_t beside;

			VectorSet( beside, at[0] + flrand( -spot->beside, spot->beside ), at[1] + flrand( -spot->beside, spot->beside ), at[2] );
			Motes_Ember( beside, flrand( 0.08f, 0.9f ) );
		}
	}
	if ( !spot->known ) {
		spot->known = qtrue;
		numLava++;
	}
	VectorCopy( at, spot->origin );
	spot->beside = ring ? 110.0f : 50.0f;
}

// The places of the list the eye has left behind go, and spots are looked at for new ones: one every moment, more
// the faster the eye moves, and many while a half of the list is short of places though lava is around. So an eye
// that flies through a level has the lava ahead in its list as fast as it loses the lava behind (flying fast there
// were few embers: the list was full of places far behind, and new ones came no faster than for an eye that stands).
static void Motes_LookForLava( void ) {
	static vec3_t	lastEye;
	static float	moved;
	static int		turn;
	int				known[2] = { 0, 0 };
	int				i, ring, looks = 0;
	float			step;

	for ( i = 0; i < LAVA_SPOTS; i++ ) {
		lavaSpot_t *spot = &lava[i];

		if ( !spot->known ) {
			continue;
		}
		ring = i / ( LAVA_SPOTS / 2 );
		if ( Distance( spot->origin, cg.refdef.vieworg ) > ( ring ? EMBER_REACH * 1.15f : EMBER_NEAR * 1.3f ) ) {
			spot->known = qfalse;
			numLava--;
			continue;
		}
		known[ring]++;
	}
	step = Distance( cg.refdef.vieworg, lastEye );
	VectorCopy( cg.refdef.vieworg, lastEye );
	// (a jump across the level is no flight)
	moved = Q_min( moved + ( step < 1000.0f ? step : 0.0f ), EMBER_STRIDE * EMBER_LOOKS );
	if ( cg.time >= nextEmber ) {
		nextEmber = cg.time + EMBER_LOOK;
		Motes_SeekLava( turn++ & 1 );
		looks++;
	}
	for ( ; moved >= EMBER_STRIDE && looks < EMBER_LOOKS; moved -= EMBER_STRIDE, looks++ ) {
		Motes_SeekLava( turn++ & 1 );
	}
	// (only while lava was found a moment ago: a level without any is not searched this hard for ever)
	if ( lavaSeen && cg.time - lavaSeen < 3000 ) {
		for ( ring = 0; ring < 2; ring++ ) {
			for ( i = 0; known[ring] < LAVA_SPOTS * 3 / 10 && i < EMBER_LOOKS / 2 && looks < EMBER_LOOKS * 2; i++, looks++ ) {
				Motes_SeekLava( ring );
			}
		}
	}
}

// Embers off the lava all over, one at a time from a place of the list and a little beside it: the more lava around,
// the more of them.
static void Motes_Embers( void ) {
	static float	owed;
	int				tries;

	if ( !numLava || !CG_GpuSparksReady() ) {
		owed = 0.0f;
		return;
	}
	owed = Q_min( owed + numLava * EMBER_RATE * cg.frametime * 0.001f, 16.0f );
	for ( tries = 0; owed >= 1.0f && tries < 32; tries++ ) {
		lavaSpot_t	*spot = &lava[Q_irand( 0, LAVA_SPOTS - 1 )];
		vec3_t		at;

		if ( !spot->known ) {
			continue;
		}
		VectorSet( at, spot->origin[0] + flrand( -spot->beside, spot->beside ), spot->origin[1] + flrand( -spot->beside, spot->beside ),
			spot->origin[2] );
		Motes_Ember( at, 0.0f );
		owed -= 1.0f;
	}
}

// How much dust the face of a lamp has in front of it, for its size: by the length of its sides
static float Motes_DustFace( const levelLamp_t *lamp ) {
	// (of a face of lava a thousand units high only the part near the eye has dust made: Motes_DustSpeck)
	return Com_Clamp( 0.5f, 2.0f, ( Q_min( VectorLength( lamp->right ), DUST_SEEN ) + Q_min( VectorLength( lamp->up ), DUST_SEEN ) ) / DUST_FACE );
}

// somewhere along an edge of the face of a lamp, -1 to 1 of the half edge: all of it, or of a long one the part around
// where the eye is
static float Motes_DustAlong( const levelLamp_t *lamp, const vec3_t edge ) {
	const float	len2 = DotProduct( edge, edge );
	vec3_t		to;
	float		window;

	if ( len2 <= DUST_SEEN * DUST_SEEN ) {
		return flrand( -1.0f, 1.0f );
	}
	VectorSubtract( cg.refdef.vieworg, lamp->origin, to );
	window = DUST_SEEN / sqrtf( len2 );
	return Com_Clamp( -1.0f, 1.0f, Com_Clamp( -1.0f, 1.0f, DotProduct( to, edge ) / len2 ) + flrand( -window, window ) );
}

// One speck in the light of a lamp, somewhere in front of its face, if the lamp sees the place and the eye is not too
// far to make it out. age: the part of its time that is over when it is made.
static void Motes_DustSpeck( const levelLamp_t *lamp, float cone, float age ) {
	// (most of them close to the lamp: the square puts them there)
	const float		nearLamp = flrand( 0.0f, 1.0f ), reach = 10.0f + nearLamp * nearLamp * ( cone - 10.0f ), wide = reach * DUST_SPREAD;
	const float		offA = flrand( -1.0f, 1.0f ), offB = flrand( -1.0f, 1.0f );
	vec3_t			axis, side, up, from, at;
	sparkBurst_t	b;
	trace_t			tr;
	float			shine;

	if ( VectorNormalize2( lamp->normal, axis ) < 0.5f ) {
		// (it shines all round: below it is where one looks)
		VectorSet( axis, 0.0f, 0.0f, -1.0f );
	}
	PerpendicularVector( side, axis );
	CrossProduct( axis, side, up );
	// (from anywhere on the face that glows, not from its middle: a long panel has dust all along it)
	VectorMA( lamp->origin, Motes_DustAlong( lamp, lamp->right ), lamp->right, from );
	VectorMA( from, Motes_DustAlong( lamp, lamp->up ), lamp->up, from );
	VectorMA( from, 4.0f, axis, from );
	VectorMA( from, reach, axis, at );
	VectorMA( at, offA * wide, side, at );
	VectorMA( at, offB * wide, up, at );
	if ( DistanceSquared( at, cg.refdef.vieworg ) > DUST_SEEN * DUST_SEEN ) {
		return;
	}
	CG_Trace( &tr, from, NULL, NULL, at, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		return;
	}
	// (brightest close to the lamp in the middle of its light; it fades out towards the end of the cone, quickly at
	// first, and towards its sides)
	shine = 1.0f - reach / cone;
	shine = Com_Clamp( 0.0f, 4.0f, v_dustBright.value ) * shine * shine * Q_max( 0.0f, 1.0f - 0.5f * ( offA * offA + offB * offB ) );
	if ( shine < 0.02f ) {
		return;
	}
	memset( &b, 0, sizeof( b ) );
	VectorCopy( at, b.origin );
	VectorSet( b.dir, 0.0f, 0.0f, -1.0f );
	b.count = 1;
	b.speed = 3.0f;
	b.spread = 2.0f;
	b.life = flrand( 7.0f, 11.0f );
	b.size = flrand( 0.09f, 0.19f ) * Com_Clamp( 0.25f, 4.0f, v_dustSize.value );
	// (it sinks, very slowly)
	b.gravity = 3.0f;
	b.drag = 1.5f;
	b.swirl = flrand( 5.0f, 9.0f );
	b.age = age;
	VectorScale( lamp->color, shine, b.color );
	CG_EmitSparks( &b );
}

// Dust in the light of the lamps around the eye ("Dust in light"): fine specks that float in the light a lamp throws,
// in front of all of its face, carried slowly by the air, and take its colour; where the lamp does not shine there are
// none. The renderer says where the lamps of the level are (ext.R_LampsNear); a speck is put where the lamp sees it
// (a trace). Somebody who walks through parts them (the sparks of the renderer that float, cooling 0).
// The dust is there before the eye is: the lamps are known from far off, and one that is new to the list gets the dust
// it would have by now at once, specks of every age (seen from that far they are faint, and come up as the eye comes
// nearer; made only beside the eye, the dust was seen to appear).
static void Motes_Dust( void ) {
	static int			checked = -1;
	static qboolean		dustCvars;
	float				cone, amount;
	int					i, j, tries;

	if ( checked < 0 ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgameLamps", value, sizeof( value ) );
		checked = atoi( value ) == 1 ? 1 : 0;
	}
	if ( !checked || !trap->ext.R_LampsNear || !CG_GpuSparksReady() ) {
		return;
	}
	if ( cg.time >= nextLamps ) {
		levelLamp_t	all[DUST_LAMPS * 2], fresh[DUST_LAMPS];
		float		away[DUST_LAMPS * 2], freshWarm[DUST_LAMPS], freshOwed[DUST_LAMPS];
		const int	listed = trap->ext.R_LampsNear( cg.refdef.vieworg, DUST_REACH, all, DUST_LAMPS * 2 );
		int			count = 0;

		// The renderer lists the lamps by how much light they give the place, and a face of lava a thousand units high
		// outdoes the small lamp right over the eye (mp/ffa5: the lamps of the ceilings had no dust). Here the nearest
		// count, by the nearest point of their face: twice as many are asked for and the nearer half is kept.
		for ( i = 0; i < listed; i++ ) {
			vec3_t nearest, to;

			VectorCopy( all[i].origin, nearest );
			VectorSubtract( cg.refdef.vieworg, all[i].origin, to );
			for ( j = 0; j < 2; j++ ) {
				const float *edge = j ? all[i].up : all[i].right;
				const float len2 = DotProduct( edge, edge );

				if ( len2 > 1.0f ) {
					VectorMA( nearest, Com_Clamp( -1.0f, 1.0f, DotProduct( to, edge ) / len2 ), edge, nearest );
				}
			}
			away[i] = DistanceSquared( nearest, cg.refdef.vieworg );
		}
		for ( count = 0; count < DUST_LAMPS; count++ ) {
			int best = -1;

			for ( i = 0; i < listed; i++ ) {
				if ( away[i] >= 0.0f && ( best < 0 || away[i] < away[best] ) ) {
					best = i;
				}
			}
			if ( best < 0 ) {
				break;
			}
			fresh[count] = all[best];
			away[best] = -1.0f;
		}

		nextLamps = cg.time + DUST_LAMPS_MSEC;
		// the dust to taste, on the console (saved): how much of it (1: as made, 0: none), how far from a lamp it shows
		// (units), how bright it is (of the colour of the lamp) and how large the specks are (1: as made)
		if ( !dustCvars ) {
			dustCvars = qtrue;
			trap->Cvar_Register( &v_dustAmount, "v_dustAmount", "1", CVAR_ARCHIVE );
			trap->Cvar_Register( &v_dustReach, "v_dustReach", va( "%g", DUST_CONE ), CVAR_ARCHIVE );
			trap->Cvar_Register( &v_dustBright, "v_dustBright", va( "%g", DUST_BRIGHT ), CVAR_ARCHIVE );
			trap->Cvar_Register( &v_dustSize, "v_dustSize", "1", CVAR_ARCHIVE );
		}
		trap->Cvar_Update( &v_dustAmount );
		trap->Cvar_Update( &v_dustReach );
		trap->Cvar_Update( &v_dustBright );
		trap->Cvar_Update( &v_dustSize );
		for ( i = 0; i < count; i++ ) {
			for ( j = 0; j < numDustLamps && DistanceSquared( fresh[i].origin, dustLamps[j].origin ) > 1.0f; j++ ) {
			}
			// (new to the list: what it would have in its light by now, about half of its specks' time)
			freshWarm[i] = j < numDustLamps ? dustWarm[j] : Motes_DustFace( &fresh[i] ) * DUST_RATE * DUST_WARM_SEC;
			freshOwed[i] = j < numDustLamps ? dustOwed[j] : 0.0f;
		}
		memcpy( dustLamps, fresh, sizeof( levelLamp_t ) * count );
		memcpy( dustWarm, freshWarm, sizeof( float ) * count );
		memcpy( dustOwed, freshOwed, sizeof( float ) * count );
		numDustLamps = count;
	}
	if ( !numDustLamps ) {
		return;
	}
	cone = Com_Clamp( 20.0f, 600.0f, v_dustReach.value );
	amount = Com_Clamp( 0.0f, 8.0f, v_dustAmount.value );
	// Each lamp has its own count of the specks it is owed, by the size of its face: with one count for all of them
	// the large faces used up what a frame can try, and the small lamp overhead got next to nothing.
	for ( i = 0; i < numDustLamps; i++ ) {
		dustOwed[i] = Q_min( dustOwed[i] + Motes_DustFace( &dustLamps[i] ) * DUST_RATE * amount * cg.frametime * 0.001f, 4.0f );
		for ( tries = 0; dustOwed[i] >= 1.0f && tries < 3; tries++ ) {
			dustOwed[i] -= 1.0f;
			Motes_DustSpeck( &dustLamps[i], cone, flrand( 0.0f, 0.3f ) );
		}
	}
	// what the new lamps are owed, some of it a frame
	for ( i = 0, tries = 0; i < numDustLamps && tries < 24; i++ ) {
		for ( ; dustWarm[i] >= 1.0f && tries < 24; tries++ ) {
			dustWarm[i] -= 1.0f;
			if ( flrand( 0.0f, 1.0f ) < amount ) {
				Motes_DustSpeck( &dustLamps[i], cone, flrand( 0.05f, 0.85f ) );
			}
		}
	}
}

void CG_MotesFrame( void ) {
	if ( motesLevel != cgs.levelStartTime ) {
		motesLevel = cgs.levelStartTime;
		CG_MotesInit();
	}
	if ( !v_levelFx.integer || !cg.snap ) {
		return;
	}
	if ( cg.time >= nextFirefly ) {
		nextFirefly = cg.time + FIREFLY_LOOK;
		Motes_SeekFirefly();
	}
	Motes_LookForLava();
	Motes_Embers();
	Motes_Dust();
	Motes_AddFireflies();
}
