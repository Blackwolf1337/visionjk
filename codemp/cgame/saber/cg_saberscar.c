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

// cg_saberscar.c -- VisionJK: the scars a saber melts into what it cuts, in place of the marks of the game
// (v_saberScars; 0 gives the game's, cg_players.c CG_CreateSaberMarks). Not from JA+.
//
// A blade that touches the level leaves one smooth ribbon per contact, not a straight strip per frame: the points
// of the contact (one a frame) are joined by a centripetal Catmull-Rom curve, drawn one frame late so the point after
// each piece is known, cut into short pieces that share their edges, so a fast swing curves and has no notches. The
// ribbon grows from nothing where the blade went in and closes where it came out, and its width wanders a little.
// Each short piece is laid onto the level once (trap->R_MarkFragments, as the game's marks are) in two widths, the
// narrow band of the molten core and the wide one of the char, its soot and the heat around it, and kept in a ring
// of its own (SCAR_PIECES).
//
// Each frame the pieces go into the scene with the colours of their age (shaders in vision_fx.shader, pictures by
// tools/materials/saber_marks.py):
// - the char: black glass in a groove, a low lip, uneven soot; lit as a groove by the renderer of VisionJK. It stays
//   v_saberScarTime seconds and fades to the surface.
// - the molten core: melt that runs along the cut, white, yellow, orange, cherry red, then flecks that glow on as
//   embers (CG_ScarHeat). Glows (bloom).
// - the heat around a fresh cut, fading faster than the core.
// - where the blade rests in the wall a pool of melt grows, white hot as long as it stays.
// - on steep walls now and then a drop of melt runs down from the cut and leaves a streak.
// - while it is hot: smoke rises, embers fall and the air above it shimmers (v_saberScarFx, the shimmer through the
//   heat haze of the screen effects, CG_ScreenFxEmit).

#include "cgame/cg_local.h"
#include "cgame/saber/cg_saberscar.h"

#define SCAR_MARK_POINTS	64			// points and fragments one short piece may give (they are small)
#define SCAR_MARK_FRAGS		16
#define SCAR_PIECES			1024		// short pieces of all scars; the oldest are written over
#define SCAR_FRAGS			2			// fragments of one band of a piece (a piece on the edge of two polygons)
#define SCAR_STREAMS		32			// blades touching the level at one time
#define SCAR_STEP			1.6f		// the longest short piece
#define SCAR_WIDE			3.2f		// the wide band, times the width of the cut
#define SCAR_REPEAT_NARROW	10.0f		// units along the cut that the picture of a band covers once
#define SCAR_REPEAT_WIDE	16.0f
#define SCAR_TAPER			3.0f		// units over which the cut opens where the blade went in
#define SCAR_FADE			2000		// msec the char takes to fade at the end of its time
#define SCAR_HOT			6000		// msec until the core has cooled completely
#define SCAR_HALO			1500		// msec of the heat around a fresh cut
#define SCAR_DRIP_RUN		900			// msec a drop takes to run its length
#define SCAR_DRIP_HOT		2200		// msec until a drop has cooled
#define SCAR_DWELL_MOVE		0.75f		// a blade moving less than this a frame rests in the wall
#define SCAR_DWELL_POOL		120			// msec of rest before the pool begins
#define SCAR_POOL_GROW		1500		// msec until the pool is as large as it gets
#define SCAR_POLYS			2400		// polys of the scars in a frame at most, the newest first
#define SCAR_DRAW_RANGE		3000.0f		// units from the eye beyond which no scar is drawn
#define SCAR_HEAT_ID		( 1 << 22 )	// the ids of the heat haze (CG_ScreenFxEmit), above those of the entities

typedef enum {
	SCAR_CUT,
	SCAR_POOL,
	SCAR_DRIP
} scarKind_t;

enum { BAND_NARROW, BAND_WIDE };

typedef struct {
	vec3_t		xyz;
	float		st[2];
} scarVert_t;

typedef struct {
	int			born;					// cg.time it was made (a pool: last time it was hot), 0: free
	int			serial;
	scarKind_t	kind;
	float		heat;					// 0 to 1, how hot it was made (a fast swing heats less)
	vec3_t		centre;
	byte		numFrags[2];
	byte		numVerts[2][SCAR_FRAGS];
	scarVert_t	verts[2][SCAR_FRAGS][MAX_VERTS_ON_POLY];
} scarPiece_t;

typedef struct {
	const void	*key;
	int			index;
	int			lastTime;				// when it was last fed, 0: free
	int			count;					// points of the curve known, up to 4 (p[0] the oldest)
	vec3_t		p[4], n[4];
	int			pTime[4];
	float		length;					// of the scar so far, for its texture along it
	float		width;					// of the cut
	qboolean	haveEdge;				// the end of the last short piece, where the next begins
	vec3_t		edgeCentre, edgeSide;
	float		edgeWidth;
	// the blade resting in the wall
	int			dwell;
	int			pool, poolSerial;
	float		poolRadius;
	// a drop of melt every so many units on a steep wall
	float		dripLength, nextDrip;
	// smoke, embers and heat haze while it is hot
	int			nextSmoke, nextEmbers;
	vec3_t		hotAt, hotNormal;
	int			hotUntil;
} scarStream_t;

static struct {
	scarPiece_t		pieces[SCAR_PIECES];
	int				next, serial;
	scarStream_t	streams[SCAR_STREAMS];
	qhandle_t		charShader, moltenShader, haloShader, dripShader, dripCharShader, poolShader, poolCharShader;
	fxHandle_t		smokeFx, embersFx;
} scar;

/*
===============================================================================

Heat

===============================================================================
*/

typedef struct {
	int		msec;
	float	v[4];
} scarKey_t;

static void Scar_Ramp( const scarKey_t *keys, int numKeys, int msec, float *out, int count ) {
	int i;

	if ( msec <= keys[0].msec ) {
		memcpy( out, keys[0].v, sizeof( float ) * count );
		return;
	}
	for ( i = 1; i < numKeys - 1 && msec > keys[i].msec; i++ )
		;
	const float f = Com_Clamp( 0.0f, 1.0f, (float)( msec - keys[i - 1].msec ) / (float)( keys[i].msec - keys[i - 1].msec ) );
	for ( int c = 0; c < count; c++ ) {
		out[c] = keys[i - 1].v[c] + f * ( keys[i].v[c] - keys[i - 1].v[c] );
	}
}

/*
=================
CG_ScarHeat

The colours of a cut msec after it was made, heat 0 to 1 how hot it was made: core, the colour the molten core is
multiplied by (its picture is coloured by how hot each part is, so a white-hot core is white in the middle and
yellow beside it); embers, how strongly the flecks glow on (they come up as the rest cools and outlast it); halo,
the heat around it. As hot metal cools: white, yellow, orange, cherry red, dark red, gone.
=================
*/
static void CG_ScarHeat( int msec, float heat, vec3_t core, float *embers, vec3_t halo ) {
	static const scarKey_t coreKeys[] = {
		{ 0,		{ 1.0f, 1.0f, 1.0f } },
		{ 250,		{ 1.0f, 0.8f, 0.55f } },
		{ 700,		{ 1.0f, 0.5f, 0.2f } },
		{ 1600,		{ 0.8f, 0.22f, 0.05f } },
		{ 2800,		{ 0.38f, 0.07f, 0.01f } },
		{ 4000,		{ 0.0f, 0.0f, 0.0f } },
	};
	static const scarKey_t emberKeys[] = {
		{ 500,		{ 0.0f } },
		{ 1500,		{ 1.0f } },
		{ 3500,		{ 0.75f } },
		{ SCAR_HOT,	{ 0.0f } },
	};
	static const scarKey_t haloKeys[] = {
		{ 0,		{ 0.55f, 0.32f, 0.12f } },
		{ 500,		{ 0.32f, 0.11f, 0.02f } },
		{ SCAR_HALO, { 0.0f, 0.0f, 0.0f } },
	};
	// a cut made cooler is further along the way
	const int along = msec + (int)( ( 1.0f - heat ) * 600.0f );

	Scar_Ramp( coreKeys, ARRAY_LEN( coreKeys ), along, core, 3 );
	Scar_Ramp( emberKeys, ARRAY_LEN( emberKeys ), along, embers, 1 );
	Scar_Ramp( haloKeys, ARRAY_LEN( haloKeys ), along, halo, 3 );
	VectorScale( halo, heat, halo );
}

/*
===============================================================================

Pieces

===============================================================================
*/

static scarPiece_t *Scar_NewPiece( scarKind_t kind, float heat, const vec3_t centre ) {
	scarPiece_t *piece = &scar.pieces[scar.next];

	scar.next = ( scar.next + 1 ) % SCAR_PIECES;
	memset( piece->numFrags, 0, sizeof( piece->numFrags ) );
	piece->born = cg.time ? cg.time : 1;
	piece->serial = ++scar.serial;
	piece->kind = kind;
	piece->heat = heat;
	VectorCopy( centre, piece->centre );
	return piece;
}

/*
=================
Scar_Lay

Lays the convex quad corners (in the plane of the surface, wound as the game's marks are) onto the level along -normal
and keeps its fragments as band of piece, each corner with its texture coordinates from st( corner, data ).
=================
*/
typedef void ( *scarSt_t )( const vec3_t xyz, const void *data, float *st );

static void Scar_Lay( scarPiece_t *piece, int band, vec3_t corners[4], const vec3_t normal, scarSt_t st, const void *data ) {
	vec3_t			points[SCAR_MARK_POINTS], projection;
	markFragment_t	fragments[SCAR_MARK_FRAGS];
	int				numFragments;

	VectorScale( normal, -1.0f, projection );
	numFragments = trap->R_MarkFragments( 4, (const float (*)[3])corners, projection, SCAR_MARK_POINTS, points[0],
		SCAR_MARK_FRAGS, fragments );
	piece->numFrags[band] = 0;
	for ( int f = 0; f < numFragments && piece->numFrags[band] < SCAR_FRAGS; f++ ) {
		const markFragment_t *mf = &fragments[f];
		const int numVerts = Q_min( mf->numPoints, MAX_VERTS_ON_POLY );
		scarVert_t *out = piece->verts[band][piece->numFrags[band]];

		if ( numVerts < 3 ) {
			continue;
		}
		for ( int v = 0; v < numVerts; v++ ) {
			VectorCopy( points[mf->firstPoint + v], out[v].xyz );
			st( out[v].xyz, data, out[v].st );
		}
		piece->numVerts[band][piece->numFrags[band]] = (byte)numVerts;
		piece->numFrags[band]++;
	}
}

// a short piece of a cut from edge a to edge b: its centres, sides (unit) and widths of the band, where its texture
// begins along the cut
typedef struct {
	vec3_t		a, b, sideA, sideB;
	float		widthA, widthB;
	float		s, repeat;
} scarQuad_t;

static void Scar_QuadSt( const vec3_t xyz, const void *data, float *st ) {
	const scarQuad_t	*q = (const scarQuad_t *)data;
	vec3_t				dir, rel, centre, side;
	float				len, u, width;

	VectorSubtract( q->b, q->a, dir );
	len = VectorNormalize( dir );
	VectorSubtract( xyz, q->a, rel );
	u = len > 0.001f ? Com_Clamp( 0.0f, 1.0f, DotProduct( rel, dir ) / len ) : 0.0f;
	for ( int k = 0; k < 3; k++ ) {
		centre[k] = q->a[k] + u * ( q->b[k] - q->a[k] );
		side[k] = q->sideA[k] + u * ( q->sideB[k] - q->sideA[k] );
	}
	VectorNormalize( side );
	width = q->widthA + u * ( q->widthB - q->widthA );
	VectorSubtract( xyz, centre, rel );
	st[0] = ( q->s + u * len ) / q->repeat;
	st[1] = 0.5f + DotProduct( rel, side ) / Q_max( width, 0.01f );
}

// the quad of a band from edge a to edge b; where the two ends turn so far apart that it would not be convex, both
// take the side between them
static void Scar_QuadCorners( scarQuad_t *q, const vec3_t normal, vec3_t corners[4] ) {
	vec3_t e[4], c;
	int sign = 0, k;

	VectorMA( q->a, -0.5f * q->widthA, q->sideA, corners[0] );
	VectorMA( q->b, -0.5f * q->widthB, q->sideB, corners[1] );
	VectorMA( q->b, 0.5f * q->widthB, q->sideB, corners[2] );
	VectorMA( q->a, 0.5f * q->widthA, q->sideA, corners[3] );
	for ( k = 0; k < 4; k++ ) {
		VectorSubtract( corners[( k + 1 ) % 4], corners[k], e[k] );
	}
	for ( k = 0; k < 4; k++ ) {
		CrossProduct( e[k], e[( k + 1 ) % 4], c );
		const int s = DotProduct( c, normal ) >= 0.0f ? 1 : -1;
		if ( sign && s != sign ) {
			break;
		}
		sign = s;
	}
	if ( k < 4 ) {
		vec3_t side;

		VectorAdd( q->sideA, q->sideB, side );
		if ( VectorNormalize( side ) < 0.001f ) {
			VectorCopy( q->sideB, side );
		}
		VectorCopy( side, q->sideA );
		VectorCopy( side, q->sideB );
		Scar_QuadCorners( q, normal, corners );
	}
}

/*
===============================================================================

Streams of contact points

===============================================================================
*/

static scarStream_t *Scar_Stream( const void *key, int index, qboolean make ) {
	scarStream_t *free = NULL, *oldest = NULL;

	for ( int i = 0; i < SCAR_STREAMS; i++ ) {
		scarStream_t *s = &scar.streams[i];

		if ( s->lastTime && s->key == key && s->index == index ) {
			return s;
		}
		if ( !s->lastTime && !free ) {
			free = s;
		}
		if ( !oldest || s->lastTime < oldest->lastTime ) {
			oldest = s;
		}
	}
	if ( !make ) {
		return NULL;
	}
	if ( !free ) {
		free = oldest;
	}
	memset( free, 0, sizeof( *free ) );
	free->key = key;
	free->index = index;
	free->pool = -1;
	return free;
}

// a point of the centripetal Catmull-Rom curve through p0..p3, t 0 to 1 between p1 and p2 (Barry and Goldman)
static void Scar_Curve( const vec3_t p0, const vec3_t p1, const vec3_t p2, const vec3_t p3, float t, vec3_t out ) {
	const float	t0 = 0.0f;
	const float	t1 = t0 + sqrtf( Q_max( Distance( p0, p1 ), 0.001f ) );
	const float	t2 = t1 + sqrtf( Q_max( Distance( p1, p2 ), 0.001f ) );
	const float	t3 = t2 + sqrtf( Q_max( Distance( p2, p3 ), 0.001f ) );
	const float	tt = t1 + t * ( t2 - t1 );
	vec3_t		a1, a2, a3, b1, b2;

	for ( int k = 0; k < 3; k++ ) {
		a1[k] = ( ( t1 - tt ) * p0[k] + ( tt - t0 ) * p1[k] ) / ( t1 - t0 );
		a2[k] = ( ( t2 - tt ) * p1[k] + ( tt - t1 ) * p2[k] ) / ( t2 - t1 );
		a3[k] = ( ( t3 - tt ) * p2[k] + ( tt - t2 ) * p3[k] ) / ( t3 - t2 );
		b1[k] = ( ( t2 - tt ) * a1[k] + ( tt - t0 ) * a2[k] ) / ( t2 - t0 );
		b2[k] = ( ( t3 - tt ) * a2[k] + ( tt - t1 ) * a3[k] ) / ( t3 - t1 );
		out[k] = ( ( t2 - tt ) * b1[k] + ( tt - t1 ) * b2[k] ) / ( t2 - t1 );
	}
}

// how wide the cut is length units along it: it opens where the blade went in and wanders a little
static float Scar_Width( const scarStream_t *s, float length ) {
	const float wander = 1.0f + 0.09f * sinf( length * 0.41f ) + 0.06f * sinf( length * 1.13f + 1.7f );

	return s->width * wander * ( 0.15f + 0.85f * Com_Clamp( 0.0f, 1.0f, length / SCAR_TAPER ) );
}

static void Scar_Drip( scarStream_t *s, const vec3_t at, const vec3_t normal, float heat );

/*
=================
Scar_Piece

The curve through p0..p3 from p1 to p2 into short pieces, each laid onto the level in both bands. end: the blade came
out at p2, the cut closes there. millis: how long the blade took from p1 to p2, for its heat.
=================
*/
static void Scar_Piece( scarStream_t *s, const vec3_t p0, const vec3_t p1, const vec3_t p2, const vec3_t p3, const vec3_t normal,
	qboolean end, int millis ) {
	const float	span = Distance( p1, p2 );
	const int	steps = Com_Clampi( 1, 32, (int)ceilf( span / SCAR_STEP ) );
	const float	heat = Com_Clamp( 0.55f, 1.0f, 1.15f - span / ( Q_max( millis, 1 ) * 0.001f ) / 2500.0f );
	const qboolean steep = (qboolean)( fabsf( normal[2] ) < 0.5f );
	vec3_t		prev, cur, next, tangent, side;

	Scar_Curve( p0, p1, p2, p3, 0.0f, prev );
	if ( !s->haveEdge ) {
		// the first edge of the cut: across the way it goes
		Scar_Curve( p0, p1, p2, p3, 0.02f, next );
		VectorSubtract( next, prev, tangent );
		CrossProduct( tangent, normal, side );
		if ( VectorNormalize( side ) < 0.0001f ) {
			PerpendicularVector( side, normal );
		}
		VectorCopy( prev, s->edgeCentre );
		VectorCopy( side, s->edgeSide );
		s->edgeWidth = Scar_Width( s, s->length );
		s->haveEdge = qtrue;
	}
	for ( int k = 1; k <= steps; k++ ) {
		const float t = (float)k / steps;
		float width, len;

		Scar_Curve( p0, p1, p2, p3, t, cur );
		// the direction through this point, from a little before to a little after
		Scar_Curve( p0, p1, p2, p3, Q_max( t - 0.02f, 0.0f ), prev );
		if ( t < 1.0f ) {
			Scar_Curve( p0, p1, p2, p3, Q_min( t + 0.02f, 1.0f ), next );
		} else {
			// past the end of this piece: toward p3
			VectorSubtract( p3, p2, next );
			VectorMA( cur, 0.02f, next, next );
		}
		VectorSubtract( next, prev, tangent );
		CrossProduct( tangent, normal, side );
		if ( VectorNormalize( side ) < 0.0001f ) {
			VectorCopy( s->edgeSide, side );
		}
		if ( DotProduct( side, s->edgeSide ) < 0.0f ) {
			VectorScale( side, -1.0f, side );
		}
		len = Distance( s->edgeCentre, cur );
		if ( len < 0.01f ) {
			continue;
		}
		width = Scar_Width( s, s->length + len );
		if ( end ) {
			// it closes toward where the blade came out
			width *= 1.0f - 0.85f * t;
		}

		scarQuad_t q;
		vec3_t corners[4], centre;
		VectorAdd( s->edgeCentre, cur, centre );
		VectorScale( centre, 0.5f, centre );
		scarPiece_t *piece = Scar_NewPiece( SCAR_CUT, heat, centre );

		VectorCopy( s->edgeCentre, q.a );
		VectorCopy( cur, q.b );
		VectorCopy( s->edgeSide, q.sideA );
		VectorCopy( side, q.sideB );
		q.widthA = s->edgeWidth;
		q.widthB = width;
		q.s = s->length;
		q.repeat = SCAR_REPEAT_NARROW;
		Scar_QuadCorners( &q, normal, corners );
		Scar_Lay( piece, BAND_NARROW, corners, normal, Scar_QuadSt, &q );

		VectorCopy( s->edgeSide, q.sideA );
		VectorCopy( side, q.sideB );
		q.widthA = s->edgeWidth * SCAR_WIDE;
		q.widthB = width * SCAR_WIDE;
		q.repeat = SCAR_REPEAT_WIDE;
		Scar_QuadCorners( &q, normal, corners );
		Scar_Lay( piece, BAND_WIDE, corners, normal, Scar_QuadSt, &q );

		VectorCopy( cur, s->edgeCentre );
		VectorCopy( side, s->edgeSide );
		s->edgeWidth = width;
		s->length = fmodf( s->length + len, SCAR_REPEAT_NARROW * SCAR_REPEAT_WIDE * 8.0f );
		VectorCopy( cur, s->hotAt );

		// a drop of melt on a steep wall, every 5 to 12 units
		if ( steep ) {
			s->dripLength += len;
			if ( s->dripLength >= s->nextDrip ) {
				s->dripLength = 0.0f;
				s->nextDrip = flrand( 5.0f, 12.0f );
				Scar_Drip( s, cur, normal, heat );
			}
		}
	}
}

/*
=================
Scar_Drip

A drop of melt that runs down a steep wall from at: laid at its whole length now, it runs by its texture coordinates
(CG_SaberScarsAdd), clamped so nothing shows below its end.
=================
*/
typedef struct {
	vec3_t	top, down, side;
	float	length, width;
} scarDrip_t;

static void Scar_DripSt( const vec3_t xyz, const void *data, float *st ) {
	const scarDrip_t *d = (const scarDrip_t *)data;
	vec3_t rel;

	VectorSubtract( xyz, d->top, rel );
	st[0] = 0.5f + DotProduct( rel, d->side ) / d->width;
	st[1] = DotProduct( rel, d->down ) / d->length;
}

static void Scar_Drip( scarStream_t *s, const vec3_t at, const vec3_t normal, float heat ) {
	static const vec3_t	gravity = { 0.0f, 0.0f, -1.0f };
	scarDrip_t			d;
	vec3_t				corners[4], centre;

	// down along the wall
	VectorMA( gravity, -DotProduct( gravity, normal ), normal, d.down );
	if ( VectorNormalize( d.down ) < 0.3f ) {
		return;
	}
	CrossProduct( d.down, normal, d.side );
	VectorNormalize( d.side );
	d.length = flrand( 2.5f, 6.5f );
	d.width = s->width * flrand( 0.4f, 0.6f );
	VectorMA( at, s->width * 0.25f, d.down, d.top );
	VectorMA( d.top, -0.5f * d.width, d.side, corners[0] );
	VectorMA( corners[0], d.length, d.down, corners[1] );
	VectorMA( corners[1], d.width, d.side, corners[2] );
	VectorMA( d.top, 0.5f * d.width, d.side, corners[3] );
	VectorMA( d.top, 0.5f * d.length, d.down, centre );

	scarPiece_t *piece = Scar_NewPiece( SCAR_DRIP, heat, centre );
	// (it starts a little after the cut passed)
	piece->born += Q_irand( 0, 250 );
	Scar_Lay( piece, 0, corners, normal, Scar_DripSt, &d );
}

/*
=================
Scar_Pool

The blade rests in the wall at the last point of s: a pool of melt there that grows and stays white hot.
=================
*/
typedef struct {
	vec3_t	centre, u, v;
	float	radius;
} scarPool_t;

static void Scar_PoolSt( const vec3_t xyz, const void *data, float *st ) {
	const scarPool_t *p = (const scarPool_t *)data;
	vec3_t rel;

	VectorSubtract( xyz, p->centre, rel );
	st[0] = 0.5f + DotProduct( rel, p->u ) / ( 2.0f * p->radius );
	st[1] = 0.5f + DotProduct( rel, p->v ) / ( 2.0f * p->radius );
}

static void Scar_Pool( scarStream_t *s ) {
	const int	last = s->count - 1;
	const float	grow = Com_Clamp( 0.0f, 1.0f, (float)( s->dwell - SCAR_DWELL_POOL ) / SCAR_POOL_GROW );
	const float	radius = s->width * ( 0.6f + 0.9f * grow ) * SCAR_WIDE * 0.5f;
	scarPiece_t	*piece = ( s->pool >= 0 && scar.pieces[s->pool].serial == s->poolSerial ) ? &scar.pieces[s->pool] : NULL;

	if ( s->dwell < SCAR_DWELL_POOL || last < 0 ) {
		return;
	}
	if ( piece && radius - s->poolRadius < 0.3f ) {
		piece->born = cg.time;		// still hot
		return;
	}
	if ( !piece ) {
		s->pool = scar.next;
		piece = Scar_NewPiece( SCAR_POOL, 1.0f, s->p[last] );
		s->poolSerial = piece->serial;
	}
	piece->born = cg.time;

	scarPool_t p;
	vec3_t corners[4];
	VectorCopy( s->p[last], p.centre );
	PerpendicularVector( p.u, s->n[last] );
	CrossProduct( s->n[last], p.u, p.v );
	p.radius = radius;
	for ( int k = 0; k < 4; k++ ) {
		const float a = ( k == 0 || k == 3 ) ? -1.0f : 1.0f, b = ( k < 2 ) ? -1.0f : 1.0f;

		VectorMA( p.centre, a * radius, p.u, corners[k] );
		VectorMA( corners[k], b * radius, p.v, corners[k] );
	}
	Scar_Lay( piece, 0, corners, s->n[last], Scar_PoolSt, &p );
	s->poolRadius = radius;
}

static void Scar_Forget( scarStream_t *s ) {
	const int hotUntil = s->hotUntil;
	vec3_t at, normal;

	VectorCopy( s->hotAt, at );
	VectorCopy( s->hotNormal, normal );
	memset( s, 0, sizeof( *s ) );
	s->pool = -1;
	// the smoke and the shimmer go on a moment where it ended (a free stream keeps them, CG_SaberScarsAdd)
	s->hotUntil = hotUntil;
	VectorCopy( at, s->hotAt );
	VectorCopy( normal, s->hotNormal );
}

// the blade came out: the last piece, closing
static void Scar_End( scarStream_t *s ) {
	if ( s->count >= 2 ) {
		const int	a = s->count - 2, b = s->count - 1;
		vec3_t		before, after, normal;

		if ( a > 0 ) {
			VectorCopy( s->p[a - 1], before );
		} else {
			VectorScale( s->p[a], 2.0f, before );
			VectorSubtract( before, s->p[b], before );
		}
		VectorScale( s->p[b], 2.0f, after );
		VectorSubtract( after, s->p[a], after );
		VectorAdd( s->n[a], s->n[b], normal );
		VectorNormalize( normal );
		Scar_Piece( s, before, s->p[a], s->p[b], after, normal, qtrue, s->pTime[b] - s->pTime[a] );
	}
	s->hotUntil = cg.time + 1500;
	Scar_Forget( s );
}

/*
=================
CG_SaberScarContact

A blade touches the level at pos (normal: of the surface there) this frame. key and index tell the blade and which of
its two traces (cg_players.c, its trail and i).
=================
*/
void CG_SaberScarContact( const void *key, int index, const vec3_t pos, const vec3_t normal, float bladeRadius ) {
	scarStream_t *s;

	if ( !v_saberScars.integer || !cg_marks.integer ) {
		return;
	}
	s = Scar_Stream( key, index, qtrue );
	if ( s->count ) {
		const int last = s->count - 1;
		const float move = Distance( pos, s->p[last] );

		// another wall, or the blade jumped: the cut ends and another begins
		if ( move > 48.0f || DotProduct( normal, s->n[last] ) < 0.7f ) {
			Scar_End( s );
			s = Scar_Stream( key, index, qtrue );
		} else if ( move < SCAR_DWELL_MOVE ) {
			// resting in the wall: the pool grows
			s->dwell += Q_max( cg.frametime, 1 );
			s->lastTime = cg.time;
			Scar_Pool( s );
			VectorCopy( pos, s->hotAt );
			VectorCopy( normal, s->hotNormal );
			s->hotUntil = cg.time + 1500;
			return;
		}
	}
	s->lastTime = cg.time;
	s->dwell = 0;
	s->pool = -1;
	s->width = Com_Clamp( 1.4f, 3.5f, 2.2f * bladeRadius / 3.0f );
	if ( !s->count ) {
		s->nextDrip = flrand( 3.0f, 8.0f );
	}
	if ( s->count == 4 ) {
		memmove( s->p, s->p + 1, sizeof( s->p[0] ) * 3 );
		memmove( s->n, s->n + 1, sizeof( s->n[0] ) * 3 );
		memmove( s->pTime, s->pTime + 1, sizeof( s->pTime[0] ) * 3 );
		s->count = 3;
	}
	VectorCopy( pos, s->p[s->count] );
	VectorCopy( normal, s->n[s->count] );
	s->pTime[s->count] = cg.time;
	s->count++;
	VectorCopy( pos, s->hotAt );
	VectorCopy( normal, s->hotNormal );
	s->hotUntil = cg.time + 1500;

	// one frame late: the piece before the newest point, with the points around it
	{
		vec3_t normalAB, before;

		if ( s->count == 3 ) {
			// the first piece: a point before the first one, made up along the way
			VectorScale( s->p[0], 2.0f, before );
			VectorSubtract( before, s->p[1], before );
			VectorAdd( s->n[0], s->n[1], normalAB );
			VectorNormalize( normalAB );
			Scar_Piece( s, before, s->p[0], s->p[1], s->p[2], normalAB, qfalse, s->pTime[1] - s->pTime[0] );
		} else if ( s->count == 4 ) {
			VectorAdd( s->n[1], s->n[2], normalAB );
			VectorNormalize( normalAB );
			Scar_Piece( s, s->p[0], s->p[1], s->p[2], s->p[3], normalAB, qfalse, s->pTime[2] - s->pTime[1] );
		}
	}

	// smoke rises and embers fall from where it cuts
	if ( v_saberScarFx.integer ) {
		if ( cg.time >= s->nextSmoke && scar.smokeFx ) {
			trap->FX_PlayEffectID( scar.smokeFx, (float *)pos, (float *)normal, -1, -1, qfalse );
			s->nextSmoke = cg.time + 70;
		}
		if ( cg.time >= s->nextEmbers && scar.embersFx ) {
			trap->FX_PlayEffectID( scar.embersFx, (float *)pos, (float *)normal, -1, -1, qfalse );
			s->nextEmbers = cg.time + Q_irand( 40, 90 );
		}
	}
}

/*
=================
CG_SaberScarRelease

The blade of key and index touches nothing this frame. Its cut closes only after a moment without a touch: a blade
that grazes the wall touches it every other frame, and every touch would open and close a cut of its own.
=================
*/
void CG_SaberScarRelease( const void *key, int index ) {
	scarStream_t *s = Scar_Stream( key, index, qfalse );

	if ( s && cg.time - s->lastTime > 100 ) {
		Scar_End( s );
	}
}

/*
===============================================================================

Drawing

===============================================================================
*/

static int scarPolys;

static void Scar_Add( const scarPiece_t *piece, int band, qhandle_t shader, const byte color[4], float dripRun ) {
	polyVert_t verts[MAX_VERTS_ON_POLY];

	if ( !shader ) {
		return;
	}
	for ( int f = 0; f < piece->numFrags[band] && scarPolys < SCAR_POLYS; f++ ) {
		const int numVerts = piece->numVerts[band][f];

		for ( int v = 0; v < numVerts; v++ ) {
			const scarVert_t *in = &piece->verts[band][f][v];

			VectorCopy( in->xyz, verts[v].xyz );
			verts[v].st[0] = in->st[0];
			// a drop runs down: its texture drawn over the part it has run so far, nothing below (clampmap)
			verts[v].st[1] = dripRun > 0.0f ? in->st[1] / dripRun : in->st[1];
			memcpy( verts[v].modulate, color, 4 );
		}
		trap->R_AddPolysToScene( shader, numVerts, verts, 1 );
		scarPolys++;
	}
}

static void Scar_Color( byte out[4], const vec3_t rgb, float alpha ) {
	for ( int c = 0; c < 3; c++ ) {
		out[c] = (byte)( Com_Clamp( 0.0f, 1.0f, rgb[c] ) * 255.0f );
	}
	out[3] = (byte)( Com_Clamp( 0.0f, 1.0f, alpha ) * 255.0f );
}

/*
=================
CG_SaberScarsAdd

The scars into the scene with the colours of their age, the newest first (SCAR_POLYS at most), and for each blade
that cut a moment ago the shimmer of the hot air over it.
=================
*/
void CG_SaberScarsAdd( void ) {
	const int	charTime = Com_Clampi( 5, 600, v_saberScarTime.integer ) * 1000;
	const float	range = SCAR_DRAW_RANGE * SCAR_DRAW_RANGE;

	// blades that stopped being fed (the player died, the saber went off) close their cuts
	for ( int i = 0; i < SCAR_STREAMS; i++ ) {
		scarStream_t *s = &scar.streams[i];

		if ( s->lastTime && cg.time - s->lastTime > 150 ) {
			Scar_End( s );
		}
		// the air shimmers over what is still hot (the heat haze of the screen effects)
		if ( v_saberScarFx.integer && s->hotUntil > cg.time ) {
			const float strength = Com_Clamp( 0.0f, 1.0f, ( s->hotUntil - cg.time ) / 1500.0f );
			const vec3_t rise = { 0.0f, 0.0f, 28.0f };
			vec3_t at;

			VectorMA( s->hotAt, 1.5f, s->hotNormal, at );
			CG_ScreenFxEmit( SCAR_HEAT_ID + i, at, rise, 4.0f, 0.7f * strength, 0.0f );
		}
	}
	if ( !v_saberScars.integer || !cg_marks.integer ) {
		return;
	}

	scarPolys = 0;
	for ( int k = 0; k < SCAR_PIECES && scarPolys < SCAR_POLYS; k++ ) {
		scarPiece_t *piece = &scar.pieces[( scar.next - 1 - k + SCAR_PIECES ) % SCAR_PIECES];
		const int age = cg.time - piece->born;
		vec3_t core, halo;
		float embers, fade;
		byte color[4];

		if ( !piece->born ) {
			continue;
		}
		if ( age > charTime + SCAR_FADE ) {
			piece->born = 0;
			continue;
		}
		if ( age < 0 || DistanceSquared( piece->centre, cg.refdef.vieworg ) > range ) {
			continue;
		}
		fade = age > charTime ? 1.0f - (float)( age - charTime ) / SCAR_FADE : 1.0f;
		CG_ScarHeat( age, piece->heat, core, &embers, halo );

		switch ( piece->kind ) {
		case SCAR_CUT: {
			const vec3_t white = { 1.0f, 1.0f, 1.0f };
			vec3_t charColor;

			// (the char fades with rgb and alpha together, to the surface)
			VectorScale( white, fade, charColor );
			Scar_Color( color, charColor, fade );
			Scar_Add( piece, BAND_WIDE, scar.charShader, color, 0.0f );
			if ( age < SCAR_HOT ) {
				Scar_Color( color, core, embers );
				Scar_Add( piece, BAND_NARROW, scar.moltenShader, color, 0.0f );
			}
			if ( age < SCAR_HALO ) {
				Scar_Color( color, halo, 1.0f );
				Scar_Add( piece, BAND_WIDE, scar.haloShader, color, 0.0f );
			}
			break;
		}
		case SCAR_POOL: {
			const vec3_t white = { 1.0f, 1.0f, 1.0f };
			vec3_t charColor;

			VectorScale( white, fade, charColor );
			Scar_Color( color, charColor, fade );
			Scar_Add( piece, 0, scar.poolCharShader, color, 0.0f );
			if ( age < SCAR_HOT ) {
				Scar_Color( color, core, 1.0f );
				Scar_Add( piece, 0, scar.poolShader, color, 0.0f );
			}
			break;
		}
		case SCAR_DRIP: {
			const float run = Com_Clamp( 0.05f, 1.0f, 1.0f - ( 1.0f - Com_Clamp( 0.0f, 1.0f, (float)age / SCAR_DRIP_RUN ) )
				* ( 1.0f - Com_Clamp( 0.0f, 1.0f, (float)age / SCAR_DRIP_RUN ) ) );
			const vec3_t white = { 1.0f, 1.0f, 1.0f };
			vec3_t charColor, dripHeat;
			float dripEmbers;

			VectorScale( white, fade, charColor );
			Scar_Color( color, charColor, fade );
			Scar_Add( piece, 0, scar.dripCharShader, color, run );
			if ( age < SCAR_DRIP_HOT ) {
				// a drop is thin and cools faster than the cut
				CG_ScarHeat( age * 2, piece->heat, dripHeat, &dripEmbers, halo );
				Scar_Color( color, dripHeat, 1.0f );
				Scar_Add( piece, 0, scar.dripShader, color, run );
			}
			break;
		}
		}
	}
}

/*
=================
CG_SaberScarsInit / CG_SaberScarsRegister

All scars gone (a new level). The shaders and effects, in the order the char before the hot layers (the renderer
sorts decals of one sort by when their shader was made, and the hot light belongs on top of the char).
=================
*/
void CG_SaberScarsInit( void ) {
	const qhandle_t shaders[7] = { scar.charShader, scar.moltenShader, scar.haloShader, scar.dripShader, scar.dripCharShader,
		scar.poolShader, scar.poolCharShader };
	const fxHandle_t smoke = scar.smokeFx, embers = scar.embersFx;

	memset( &scar, 0, sizeof( scar ) );
	scar.charShader = shaders[0];
	scar.moltenShader = shaders[1];
	scar.haloShader = shaders[2];
	scar.dripShader = shaders[3];
	scar.dripCharShader = shaders[4];
	scar.poolShader = shaders[5];
	scar.poolCharShader = shaders[6];
	scar.smokeFx = smoke;
	scar.embersFx = embers;
	for ( int i = 0; i < SCAR_STREAMS; i++ ) {
		scar.streams[i].pool = -1;
	}
}

void CG_SaberScarsRegister( void ) {
	scar.charShader = trap->R_RegisterShader( "gfx/vision/saberscar" );
	scar.poolCharShader = trap->R_RegisterShader( "gfx/vision/saberpool_scar" );
	scar.dripCharShader = trap->R_RegisterShader( "gfx/vision/saberdrip_scar" );
	scar.moltenShader = trap->R_RegisterShader( "gfx/vision/sabermolten" );
	scar.haloShader = trap->R_RegisterShader( "gfx/vision/saberhalo" );
	scar.poolShader = trap->R_RegisterShader( "gfx/vision/saberpool" );
	scar.dripShader = trap->R_RegisterShader( "gfx/vision/saberdrip" );
	scar.smokeFx = trap->FX_RegisterEffect( "vision/saber_scorch_smoke.efx" );
	scar.embersFx = trap->FX_RegisterEffect( "vision/saber_embers.efx" );
}
