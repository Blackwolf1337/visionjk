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

/*
The shape of a prop: the convex hull of its model. Shared by the game, whose physics is made of it (g_props.c),
and the cgame, which predicts the moves of a player against it. Not from JA+.

A model can have thousands of vertices and holes. What is kept of it is a hull of up to 32 corners and 60 faces,
which is what a rigid body can be asked about every frame. It starts with the furthest vertex along each of 26
directions (the neighbours in a grid of three by three by three). That alone loses every flat side whose
corners are not the furthest along one of those, a crate with slanted shoulders got a pointed roof. So the
vertex that sticks out of the hull furthest is added, and again, until nothing sticks out any more or the 32
are used up. A hull has no holes: what is hollow in the model is filled, a ring is a disc and a table a block
with the outline of its top and its feet.
*/

#include "qcommon/q_shared.h"
#include "game/bg_public.h"
#include "bg_props.h"
#include "bg_props_math.h"		// the vector functions, inline

#define HULL_ON			0.05f		// a corner this close to a plane is on it
#define HULL_LINE		0.001f		// three corners with less than this sine between them are a line: no plane, no corner
#define HULL_SAME_DIR	0.9995f		// two planes that agree this well are one face
#define HULL_SAME_DIST	0.1f
#define HULL_SAME_SPOT	0.1f
#define HULL_OUT		0.005f		// a vertex that sticks out of the hull by less than this part of the size of
#define HULL_OUT_MIN	0.1f		//	the model, or by less than this, is taken as part of it
#define CLIP_EPSILON	0.125f		// a box stops this far in front of a face, as it does at the brushes of a map

static vec3_t	hullDirs[PROP_HULL_DIRS];
static qboolean	hullDirsMade;

static void Hull_Dirs( void ) {
	int x, y, z, n = 0;

	if ( hullDirsMade )
		return;
	for ( z = -1; z <= 1; z++ ) {
		for ( y = -1; y <= 1; y++ ) {
			for ( x = -1; x <= 1; x++ ) {
				if ( !x && !y && !z )
					continue;
				VectorSet( hullDirs[n], x, y, z );
				VectorNormalize( hullDirs[n] );
				n++;
			}
		}
	}
	hullDirsMade = qtrue;
}

/*
===============================================================================

The vertices of a model

===============================================================================
*/

// an empty one. Not for one that holds points, that is what BG_PropCloudFree() is for.
void BG_PropCloudClear( propCloud_t *cloud ) {
	Hull_Dirs();
	memset( cloud, 0, sizeof( *cloud ) );
	ClearBounds( cloud->mins, cloud->maxs );
}

void BG_PropCloudFree( propCloud_t *cloud ) {
	free( cloud->points );
	cloud->points = NULL;
	cloud->room = 0;
}

void BG_PropCloudAdd( propCloud_t *cloud, const vec3_t point ) {
	int i;

	for ( i = 0; i < PROP_HULL_DIRS; i++ ) {
		const float reach = DotProduct( point, hullDirs[i] );

		if ( !cloud->count || reach > cloud->reach[i] ) {
			cloud->reach[i] = reach;
			VectorCopy( point, cloud->far[i] );
		}
	}
	AddPointToBounds( point, cloud->mins, cloud->maxs );

	// how many vertices a model has is not known before it is read
	if ( cloud->count >= cloud->room ) {
		const int	room = cloud->room ? cloud->room * 2 : 1024;
		vec3_t		*points = (vec3_t *)realloc( cloud->points, room * sizeof( vec3_t ) );

		if ( !points ) {
			// the hull is made of the furthest ones alone then
			cloud->count++;
			return;
		}
		cloud->points = points;
		cloud->room = room;
	}
	VectorCopy( point, cloud->points[cloud->count] );
	cloud->count++;
}

/*
===============================================================================

From corners to a hull

===============================================================================
*/

// the point of the line from a to b next to a point
static void Segment_Nearest( const vec3_t a, const vec3_t b, const vec3_t point, vec3_t out ) {
	vec3_t	ab, ap;
	float	along, len;

	VectorSubtract( b, a, ab );
	VectorSubtract( point, a, ap );
	len = VectorLengthSquared( ab );
	along = ( len > 0.0f ) ? Com_Clamp( 0.0f, 1.0f, DotProduct( ap, ab ) / len ) : 0.0f;
	VectorMA( a, along, ab, out );
}

// The faces: every plane through three corners that has all the others on one side. Two planes are the same
// face when the same corners lie on them, and a plane whose corners all lie on another one is part of that. (Not:
// when they look the same way. Faces that nearly do are common, and each has corners the other has not.)
static qboolean Shape_Planes( propShape_t *s, unsigned int *on ) {
	int i, j, k, m;

	s->numPlanes = 0;
	for ( i = 0; i < s->numVerts; i++ ) {
		for ( j = i + 1; j < s->numVerts; j++ ) {
			for ( k = j + 1; k < s->numVerts; k++ ) {
				vec3_t			a, b, normal;
				float			dist;
				unsigned int	mask = 0;
				int				front = 0, back = 0, slot;

				VectorSubtract( s->verts[j], s->verts[i], a );
				VectorSubtract( s->verts[k], s->verts[i], b );
				CrossProduct( a, b, normal );
				if ( VectorNormalize( normal ) < HULL_LINE * VectorLength( a ) * VectorLength( b ) )
					continue;
				dist = DotProduct( normal, s->verts[i] );

				for ( m = 0; m < s->numVerts; m++ ) {
					const float off = DotProduct( normal, s->verts[m] ) - dist;

					if ( off > HULL_ON )
						front++;
					else if ( off < -HULL_ON )
						back++;
					else
						mask |= 1u << m;
				}
				if ( front && back )
					continue;
				// all of it lies in this plane, it has no volume
				if ( !front && !back )
					return qfalse;
				if ( front ) {
					VectorNegate( normal, normal );
					dist = -dist;
				}

				slot = s->numPlanes;
				for ( m = 0; m < s->numPlanes; m++ ) {
					if ( ( mask & on[m] ) == mask )
						break;
					// the other way around: this one is the whole face
					if ( ( mask & on[m] ) == on[m] )
						slot = m;
				}
				if ( m < s->numPlanes || slot >= PROP_MAX_PLANES )
					continue;
				VectorCopy( normal, s->planes[slot] );
				s->planes[slot][3] = dist;
				on[slot] = mask;
				if ( slot == s->numPlanes )
					s->numPlanes++;
			}
		}
	}
	return ( s->numPlanes >= 4 ) ? qtrue : qfalse;
}

// The corners of each face, in order around its normal. A corner that lies in a face or on an edge without
// being one of its corners is left out (a model can have a vertex in the middle of a flat side).
static qboolean Shape_Loops( propShape_t *s, unsigned int *on ) {
	int used = 0, n, m, i;

	for ( n = 0; n < s->numPlanes; n++ ) {
		byte		list[PROP_MAX_VERTS];
		float		angle[PROP_MAX_VERTS];
		const float	*normal = s->planes[n];
		vec3_t		mid, u, w, rel;
		int			count = 0;
		qboolean	cut;

		VectorClear( mid );
		for ( m = 0; m < s->numVerts; m++ ) {
			if ( on[n] & ( 1u << m ) ) {
				list[count++] = m;
				VectorAdd( mid, s->verts[m], mid );
			}
		}
		if ( count >= 3 ) {
			VectorScale( mid, 1.0f / count, mid );

			// two directions in the plane to measure the angles in
			VectorClear( u );
			for ( m = 0; m < count && VectorLengthSquared( u ) < 0.0001f; m++ ) {
				VectorSubtract( s->verts[list[m]], mid, u );
				VectorMA( u, -DotProduct( u, normal ), normal, u );
			}
			VectorNormalize( u );
			CrossProduct( normal, u, w );
			for ( m = 0; m < count; m++ ) {
				VectorSubtract( s->verts[list[m]], mid, rel );
				angle[m] = atan2f( DotProduct( rel, w ), DotProduct( rel, u ) );
			}
			for ( m = 1; m < count; m++ ) {
				const byte	v = list[m];
				const float	a = angle[m];

				for ( i = m; i > 0 && angle[i - 1] > a; i-- ) {
					list[i] = list[i - 1];
					angle[i] = angle[i - 1];
				}
				list[i] = v;
				angle[i] = a;
			}

			// only what turns left at it is a corner
			do {
				cut = qfalse;
				for ( m = 0; m < count && count > 3; m++ ) {
					const float	*before = s->verts[list[( m + count - 1 ) % count]], *at = s->verts[list[m]];
					const float	*after = s->verts[list[( m + 1 ) % count]];
					vec3_t		in, out, turn;

					VectorSubtract( at, before, in );
					VectorSubtract( after, at, out );
					CrossProduct( in, out, turn );
					if ( DotProduct( turn, normal ) > HULL_LINE * VectorLength( in ) * VectorLength( out ) )
						continue;
					for ( i = m; i < count - 1; i++ )
						list[i] = list[i + 1];
					count--;
					cut = qtrue;
					break;
				}
			} while ( cut );
		}

		if ( count < 3 ) {
			// no face after all: the last one takes its place
			s->numPlanes--;
			VectorCopy4( s->planes[s->numPlanes], s->planes[n] );
			on[n] = on[s->numPlanes];
			n--;
			continue;
		}
		if ( used + count > PROP_MAX_LOOP )
			return qfalse;
		s->loopStart[n] = used;
		s->loopCount[n] = count;
		memcpy( s->loop + used, list, count );
		used += count;
	}
	return ( s->numPlanes >= 4 ) ? qtrue : qfalse;
}

// corners that are the corner of no face go, the loops are told the new numbers
static void Shape_Prune( propShape_t *s ) {
	byte	number[PROP_MAX_VERTS];
	int		n, m, kept = 0;

	for ( m = 0; m < s->numVerts; m++ ) {
		qboolean used = qfalse;

		for ( n = 0; n < s->numPlanes && !used; n++ ) {
			if ( memchr( s->loop + s->loopStart[n], m, s->loopCount[n] ) )
				used = qtrue;
		}
		number[m] = kept;
		if ( used ) {
			VectorCopy( s->verts[m], s->verts[kept] );
			kept++;
		}
	}
	s->numVerts = kept;
	for ( n = 0; n < s->numPlanes; n++ ) {
		for ( m = 0; m < s->loopCount[n]; m++ )
			s->loop[s->loopStart[n] + m] = number[s->loop[s->loopStart[n] + m]];
	}
}

static void Shape_Edges( propShape_t *s ) {
	int n, m, i;

	s->numEdges = 0;
	for ( n = 0; n < s->numPlanes; n++ ) {
		const byte *loop = s->loop + s->loopStart[n];

		for ( m = 0; m < s->loopCount[n]; m++ ) {
			const byte	a = loop[m], b = loop[( m + 1 ) % s->loopCount[n]];
			const byte	lo = ( a < b ) ? a : b, hi = ( a < b ) ? b : a;

			for ( i = 0; i < s->numEdges; i++ ) {
				if ( s->edges[i][0] == lo && s->edges[i][1] == hi )
					break;
			}
			if ( i < s->numEdges || s->numEdges >= PROP_MAX_EDGES )
				continue;
			s->edges[s->numEdges][0] = lo;
			s->edges[s->numEdges][1] = hi;
			s->numEdges++;
		}
	}
}

/*
=================
Shape_Finish

verts and numVerts are set, in the model. The rest of the shape is made from them, and the middle goes to its
centre of mass: the one of a solid of the same stuff all through. qfalse: this is no body, it is flat or a line.
=================
*/
static qboolean Shape_Finish( propShape_t *s ) {
	unsigned int	on[PROP_MAX_PLANES];		// the corners that lie on each face
	vec3_t			mid, com;
	float			volume = 0.0f;
	int				n, m, k;

	// A point on the line between two others is no corner. It would be one of a face without a width, which
	// has no plane, and the hull would have a gap there.
	for ( m = 0; m < s->numVerts; m++ ) {
		qboolean between = qfalse;

		for ( n = 0; n < s->numVerts && !between; n++ ) {
			for ( k = n + 1; k < s->numVerts && !between; k++ ) {
				vec3_t spot;

				if ( n == m || k == m )
					continue;
				Segment_Nearest( s->verts[n], s->verts[k], s->verts[m], spot );
				if ( DistanceSquared( spot, s->verts[m] ) < HULL_ON * HULL_ON )
					between = qtrue;
			}
		}
		if ( between ) {
			s->numVerts--;
			VectorCopy( s->verts[s->numVerts], s->verts[m] );
			m--;
		}
	}
	if ( s->numVerts < 4 )
		return qfalse;

	// around the mean of the corners for now, which is inside
	VectorClear( mid );
	for ( m = 0; m < s->numVerts; m++ )
		VectorAdd( mid, s->verts[m], mid );
	VectorScale( mid, 1.0f / s->numVerts, mid );
	for ( m = 0; m < s->numVerts; m++ )
		VectorSubtract( s->verts[m], mid, s->verts[m] );

	if ( !Shape_Planes( s, on ) || !Shape_Loops( s, on ) )
		return qfalse;
	Shape_Prune( s );
	Shape_Edges( s );

	// the centre of mass: every face cut into triangles, each the base of a pyramid with its tip in the middle
	VectorClear( com );
	for ( n = 0; n < s->numPlanes; n++ ) {
		const byte *loop = s->loop + s->loopStart[n];

		for ( m = 1; m < s->loopCount[n] - 1; m++ ) {
			const float	*a = s->verts[loop[0]], *b = s->verts[loop[m]], *c = s->verts[loop[m + 1]];
			vec3_t		cross;
			float		part;

			CrossProduct( b, c, cross );
			part = DotProduct( a, cross );
			volume += part;
			for ( k = 0; k < 3; k++ )
				com[k] += part * ( a[k] + b[k] + c[k] ) * 0.25f;
		}
	}
	if ( volume < 1.0f )
		return qfalse;
	VectorScale( com, 1.0f / volume, com );

	ClearBounds( s->mins, s->maxs );
	s->radius = 0.0f;
	for ( m = 0; m < s->numVerts; m++ ) {
		float len;

		VectorSubtract( s->verts[m], com, s->verts[m] );
		AddPointToBounds( s->verts[m], s->mins, s->maxs );
		len = VectorLength( s->verts[m] );
		if ( len > s->radius )
			s->radius = len;
	}
	s->inner = s->radius;
	for ( n = 0; n < s->numPlanes; n++ ) {
		s->planes[n][3] -= DotProduct( s->planes[n], com );
		if ( s->planes[n][3] < s->inner )
			s->inner = s->planes[n][3];
	}
	VectorAdd( mid, com, s->centre );
	return ( s->inner > 0.0f ) ? qtrue : qfalse;
}

/*
=================
BG_PropShapeBox

The box of these bounds of a model, scale is a factor. Always a body: nothing is thinner than PROP_MIN_HALF
either way from its middle.
=================
*/
void BG_PropShapeBox( const vec3_t mins, const vec3_t maxs, float scale, propShape_t *shape ) {
	vec3_t	mid, half;
	int		n, k;

	memset( shape, 0, sizeof( *shape ) );
	for ( k = 0; k < 3; k++ ) {
		mid[k] = ( maxs[k] + mins[k] ) * 0.5f * scale;
		half[k] = ( maxs[k] - mins[k] ) * 0.5f * scale;
		if ( !( half[k] >= PROP_MIN_HALF ) )
			half[k] = PROP_MIN_HALF;
	}
	shape->numVerts = 8;
	for ( n = 0; n < 8; n++ ) {
		VectorSet( shape->verts[n], mid[0] + ( ( n & 1 ) ? half[0] : -half[0] ), mid[1] + ( ( n & 2 ) ? half[1] : -half[1] ),
			mid[2] + ( ( n & 4 ) ? half[2] : -half[2] ) );
	}
	Shape_Finish( shape );
}

/*
=================
BG_PropShapeHull

The hull of the vertices that were gone through, scale is a factor. qfalse: they make no body, or one that is
too thin somewhere, and the box of their bounds has to do (BG_PropShapeBox).
=================
*/
qboolean BG_PropShapeHull( propCloud_t *cloud, float scale, propShape_t *shape ) {
	unsigned int	on[PROP_MAX_PLANES];
	int				numPoints = ( cloud->count <= cloud->room ) ? cloud->count : 0;
	vec3_t			size;
	float			out;
	int				i, m, n, k;

	memset( shape, 0, sizeof( *shape ) );
	if ( cloud->count < 4 )
		return qfalse;
	for ( k = 0; k < 3; k++ ) {
		if ( !( ( cloud->maxs[k] - cloud->mins[k] ) * scale >= 2.0f * PROP_MIN_HALF ) )
			return qfalse;
	}
	VectorSubtract( cloud->maxs, cloud->mins, size );
	out = HULL_OUT * VectorLength( size );
	if ( out < HULL_OUT_MIN )
		out = HULL_OUT_MIN;

	// the furthest along each direction
	for ( i = 0; i < PROP_HULL_DIRS; i++ ) {
		for ( m = 0; m < shape->numVerts; m++ ) {
			if ( DistanceSquared( cloud->far[i], shape->verts[m] ) < HULL_SAME_SPOT * HULL_SAME_SPOT )
				break;
		}
		if ( m == shape->numVerts ) {
			VectorCopy( cloud->far[i], shape->verts[shape->numVerts] );
			shape->numVerts++;
		}
	}

	// And what sticks out of the hull of those furthest, one at a time. What is in the hull stays in it as it
	// grows, so the list is cut down to what is still outside on the way: the points are used up here.
	while ( shape->numVerts < PROP_MAX_VERTS && numPoints ) {
		float	worst = 0.0f;
		int		worstPoint = -1, kept = 0;

		if ( !Shape_Planes( shape, on ) )
			return qfalse;
		for ( i = 0; i < numPoints; i++ ) {
			float most = 0.0f;

			for ( n = 0; n < shape->numPlanes; n++ ) {
				const float off = DotProduct( cloud->points[i], shape->planes[n] ) - shape->planes[n][3];

				if ( off > most )
					most = off;
			}
			if ( most <= out )
				continue;
			if ( most > worst ) {
				worst = most;
				worstPoint = kept;
			}
			VectorCopy( cloud->points[i], cloud->points[kept] );
			kept++;
		}
		numPoints = kept;
		if ( worstPoint < 0 )
			break;
		VectorCopy( cloud->points[worstPoint], shape->verts[shape->numVerts] );
		shape->numVerts++;
	}
	BG_PropCloudFree( cloud );

	for ( m = 0; m < shape->numVerts; m++ )
		VectorScale( shape->verts[m], scale, shape->verts[m] );
	return ( Shape_Finish( shape ) && shape->inner >= 0.5f * PROP_MIN_HALF ) ? qtrue : qfalse;
}

// the shape a prop of that kind (PROP_SOLID_) has: the one way the server and the client both make it
void BG_PropShapeOfKind( propCloud_t *cloud, int kind, float scale, propShape_t *shape ) {
	if ( kind != PROP_SOLID_HULL || !BG_PropShapeHull( cloud, scale, shape ) )
		BG_PropShapeBox( cloud->mins, cloud->maxs, scale, shape );
}

/*
===============================================================================

Asking a shape. Points and directions are in the shape: from its middle, along its axes.

===============================================================================
*/

// how far it reaches from its middle along a direction
float BG_PropReach( const propShape_t *shape, const vec3_t dir ) {
	float	reach = DotProduct( dir, shape->verts[0] );
	int		m;

	for ( m = 1; m < shape->numVerts; m++ ) {
		const float d = DotProduct( dir, shape->verts[m] );

		if ( d > reach )
			reach = d;
	}
	return reach;
}

// is a point in it, or within skin of it
qboolean BG_PropHolds( const propShape_t *shape, const vec3_t point, float skin ) {
	int n;

	for ( n = 0; n < shape->numPlanes; n++ ) {
		if ( DotProduct( point, shape->planes[n] ) - shape->planes[n][3] > skin )
			return qfalse;
	}
	return qtrue;
}

/*
=================
BG_PropNearest

The spot of the shape next to a point. Returns how far the point is from it, 0 inside.

It is on a face the point is in front of, straight under the point, or on an edge. All edges are tried, not
only those of the faces in front: a face as thin as a line has no plane here, and its edges are still there.
=================
*/
float BG_PropNearest( const propShape_t *shape, const vec3_t point, vec3_t out ) {
	float		best = -1.0f;
	qboolean	outside = qfalse;
	int			n, m;

	for ( n = 0; n < shape->numPlanes; n++ ) {
		const float	*normal = shape->planes[n];
		const byte	*loop = shape->loop + shape->loopStart[n];
		const int	count = shape->loopCount[n];
		const float	off = DotProduct( point, normal ) - normal[3];
		vec3_t		flat;
		qboolean	inside = qtrue;

		if ( off <= 0.0f )
			continue;
		outside = qtrue;
		if ( best >= 0.0f && off >= best )
			continue;

		// straight down onto the face, if that is within its edges
		VectorMA( point, -off, normal, flat );
		for ( m = 0; m < count && inside; m++ ) {
			const float	*a = shape->verts[loop[m]], *b = shape->verts[loop[( m + 1 ) % count]];
			vec3_t		edge, rel, side;

			VectorSubtract( b, a, edge );
			VectorSubtract( flat, a, rel );
			CrossProduct( edge, normal, side );
			if ( DotProduct( rel, side ) > 0.0f )
				inside = qfalse;
		}
		if ( inside ) {
			best = off;
			VectorCopy( flat, out );
		}
	}

	if ( !outside ) {
		VectorCopy( point, out );
		return 0.0f;
	}

	for ( n = 0; n < shape->numEdges; n++ ) {
		vec3_t	spot;
		float	dist;

		Segment_Nearest( shape->verts[shape->edges[n][0]], shape->verts[shape->edges[n][1]], point, spot );
		dist = Distance( point, spot );
		if ( best < 0.0f || dist < best ) {
			best = dist;
			VectorCopy( spot, out );
		}
	}
	return best;
}

// a plane the shape lies behind, with this normal in the world, joins the brush unless one looks the same way
static void Brush_Add( propBrush_t *brush, const propShape_t *shape, const vec3_t middle, matrix3_t axis, const vec3_t normal ) {
	vec3_t	local;
	int		n;

	// only what looks the very same way: two planes a degree apart are units apart a hundred units on
	for ( n = 0; n < brush->numPlanes; n++ ) {
		if ( DotProduct( normal, brush->planes[n] ) > 0.999999f )
			return;
	}
	if ( brush->numPlanes >= PROP_BRUSH_PLANES )
		return;
	VectorSet( local, DotProduct( normal, axis[0] ), DotProduct( normal, axis[1] ), DotProduct( normal, axis[2] ) );
	VectorCopy( normal, brush->planes[brush->numPlanes] );
	brush->planes[brush->numPlanes][3] = BG_PropReach( shape, local ) + DotProduct( normal, middle );
	brush->numPlanes++;
}

/*
=================
BG_PropBrush

A shape that stands at middle, turned by axis, as planes in the world: what the move of a box that is not
turned (a player) is cut against, as it is against a brush of the map. To be right for such a box that takes
more than the faces of the shape: the six sides of the box in the world the shape fits in, and for every edge
of the shape the planes along it and along one of the three axes, which is where an edge of the box slides
past an edge of the shape. Without those the box is stopped far from a shape that lies across the axes.
=================
*/
void BG_PropBrush( const propShape_t *shape, const vec3_t middle, matrix3_t axis, propBrush_t *brush ) {
	vec3_t	normal, edge, local, unit;
	int		n, k;

	brush->numPlanes = 0;
	for ( k = 0; k < 6; k++ ) {
		VectorClear( normal );
		normal[k >> 1] = ( k & 1 ) ? -1.0f : 1.0f;
		Brush_Add( brush, shape, middle, axis, normal );
		// its sixth plane is known: the box in the world around it, for a quick no
		if ( k & 1 )
			brush->mins[k >> 1] = -brush->planes[brush->numPlanes - 1][3];
		else
			brush->maxs[k >> 1] = brush->planes[brush->numPlanes - 1][3];
	}
	for ( n = 0; n < shape->numPlanes; n++ ) {
		VectorScale( axis[0], shape->planes[n][0], normal );
		VectorMA( normal, shape->planes[n][1], axis[1], normal );
		VectorMA( normal, shape->planes[n][2], axis[2], normal );
		Brush_Add( brush, shape, middle, axis, normal );
	}
	for ( n = 0; n < shape->numEdges; n++ ) {
		VectorSubtract( shape->verts[shape->edges[n][1]], shape->verts[shape->edges[n][0]], local );
		VectorScale( axis[0], local[0], edge );
		VectorMA( edge, local[1], axis[1], edge );
		VectorMA( edge, local[2], axis[2], edge );
		for ( k = 0; k < 3; k++ ) {
			VectorClear( unit );
			unit[k] = 1.0f;
			CrossProduct( edge, unit, normal );
			if ( VectorNormalize( normal ) < 0.001f )
				continue;
			Brush_Add( brush, shape, middle, axis, normal );
			VectorNegate( normal, normal );
			Brush_Add( brush, shape, middle, axis, normal );
		}
	}
}

// how far the corners of a box are from its origin: the same for every shape a move is cut against
float BG_PropBoxReach( const vec3_t mins, const vec3_t maxs ) {
	vec3_t	corner;
	int		k;

	for ( k = 0; k < 3; k++ )
		corner[k] = ( fabsf( mins[k] ) > fabsf( maxs[k] ) ) ? fabsf( mins[k] ) : fabsf( maxs[k] );
	return VectorLength( corner );
}

/*
=================
BG_PropNearMove

A quick no before the planes of a shape are made (BG_PropBrush), which is what costs: can a box that moves from
start to end touch a shape that lies within reach of point at all. A move comes past every prop there is and
is near a few of them.

boxReach is BG_PropBoxReach() of the box. The line from start to end is asked, not the box around it, so a
long shot across the map is not near everything that lies beside its way.
=================
*/
qboolean BG_PropNearMove( const vec3_t start, const vec3_t end, float boxReach, const vec3_t point, float reach ) {
	vec3_t	move, to;
	float	length, along;

	VectorSubtract( end, start, move );
	VectorSubtract( point, start, to );
	length = DotProduct( move, move );
	along = DotProduct( to, move );
	// from the spot of the move that is nearest to the point
	if ( along >= length )
		VectorSubtract( point, end, to );
	else if ( along > 0.0f )
		VectorMA( to, -along / length, move, to );

	reach += boxReach + 1.0f;
	return ( DotProduct( to, to ) <= reach * reach ) ? qtrue : qfalse;
}

/*
=================
BG_PropClipBox

The move of a box from start to end against the brush of a shape: results is cut short if the shape is in the
way, and names the world as what was hit, with the contents of a body (BG_PropHit).

A box that starts in the shape is not stopped by it: a player a prop fell on walks out of it.
=================
*/
void BG_PropClipBox( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
		const propBrush_t *brush ) {
	float		enter = -1.0f, leave = 1.0f;
	const float	*hit = NULL;
	qboolean	startOut = qfalse;
	int			n, k;

	// nowhere near it
	for ( k = 0; k < 3; k++ ) {
		if ( ( start[k] + mins[k] > brush->maxs[k] + 1.0f && end[k] + mins[k] > brush->maxs[k] + 1.0f )
			|| ( start[k] + maxs[k] < brush->mins[k] - 1.0f && end[k] + maxs[k] < brush->mins[k] - 1.0f ) )
			return;
	}

	for ( n = 0; n < brush->numPlanes; n++ ) {
		const float	*plane = brush->planes[n];
		float		dist = plane[3], d1, d2, f;

		// out by the corner of the box that gets to the plane first
		for ( k = 0; k < 3; k++ )
			dist -= plane[k] * ( ( plane[k] < 0.0f ) ? maxs[k] : mins[k] );

		d1 = DotProduct( start, plane ) - dist;
		d2 = DotProduct( end, plane ) - dist;
		if ( d1 > 0.0f )
			startOut = qtrue;
		// all of the way on the outside of this plane: it does not touch
		if ( d1 > 0.0f && ( d2 >= CLIP_EPSILON || d2 >= d1 ) )
			return;
		if ( d1 <= 0.0f && d2 <= 0.0f )
			continue;

		if ( d1 > d2 ) {
			f = ( d1 - CLIP_EPSILON ) / ( d1 - d2 );
			if ( f < 0.0f )
				f = 0.0f;
			if ( f > enter ) {
				enter = f;
				hit = plane;
			}
		} else {
			f = ( d1 + CLIP_EPSILON ) / ( d1 - d2 );
			if ( f > 1.0f )
				f = 1.0f;
			if ( f < leave )
				leave = f;
		}
	}

	if ( !startOut || !hit || enter >= leave || enter >= results->fraction )
		return;

	results->fraction = enter;
	VectorSubtract( end, start, results->endpos );
	VectorMA( start, enter, results->endpos, results->endpos );
	VectorCopy( hit, results->plane.normal );
	results->plane.dist = DotProduct( results->endpos, hit );
	results->entityNum = ENTITYNUM_WORLD;
	results->contents = CONTENTS_SOLID | CONTENTS_BODY;		// see BG_PropHit
	results->surfaceFlags = 0;
}

/*
===============================================================================

Reading the vertices out of a model file. Neither the server nor the cgame has the models in a form that could
be asked, so the file is read, every offset checked against its size.

===============================================================================
*/

#ifndef BG_PROPS_NO_FILES

#include "qcommon/qfiles.h"
#include "rd-common/mdx_format.h"

#if defined( _GAME )
	#include "game/g_local.h"
#elif defined( _CGAME )
	#include "cgame/cg_local.h"
#endif

// the first frame of every surface of an md3
static qboolean Cloud_Md3( const byte *data, int len, propCloud_t *cloud ) {
	md3Header_t	header;
	int			ofs, i, k;

	if ( len < (int)sizeof( header ) )
		return qfalse;
	memcpy( &header, data, sizeof( header ) );
	if ( LittleLong( header.ident ) != MD3_IDENT || LittleLong( header.numFrames ) < 1
		|| LittleLong( header.numSurfaces ) < 1 || LittleLong( header.numSurfaces ) > 1024 )
		return qfalse;

	ofs = LittleLong( header.ofsSurfaces );
	for ( i = 0; i < LittleLong( header.numSurfaces ); i++ ) {
		md3Surface_t	surf;
		int				count, ofsVerts;

		if ( ofs < (int)sizeof( header ) || ofs > len - (int)sizeof( surf ) )
			return qfalse;
		memcpy( &surf, data + ofs, sizeof( surf ) );
		count = LittleLong( surf.numVerts );
		ofsVerts = ofs + LittleLong( surf.ofsXyzNormals );
		if ( count < 0 || count > 65536 || LittleLong( surf.numFrames ) < 1
			|| ofsVerts < ofs || ofsVerts > len - count * (int)sizeof( md3XyzNormal_t ) )
			return qfalse;

		for ( k = 0; k < count; k++ ) {
			md3XyzNormal_t	v;
			vec3_t			point;

			memcpy( &v, data + ofsVerts + k * sizeof( v ), sizeof( v ) );
			VectorSet( point, LittleShort( v.xyz[0] ) * MD3_XYZ_SCALE, LittleShort( v.xyz[1] ) * MD3_XYZ_SCALE,
				LittleShort( v.xyz[2] ) * MD3_XYZ_SCALE );
			BG_PropCloudAdd( cloud, point );
		}

		if ( LittleLong( surf.ofsEnd ) < (int)sizeof( surf ) )
			return qfalse;
		ofs += LittleLong( surf.ofsEnd );
	}
	return qtrue;
}

// The first level of detail of a Ghoul2 model as it stands in the file, which is how the client shows it
// without an animation. Surfaces that are switched off are left out, the client does not draw them either.
static qboolean Cloud_Glm( const byte *data, int len, propCloud_t *cloud ) {
	const mdxmHeader_t	*header = (const mdxmHeader_t *)data;
	// the offsets of the entries of the surface hierarchy follow the header, and count from there
	const int			ofsHier = (int)sizeof( mdxmHeader_t );
	int					numSurfaces, ofsLODs, base, i, k;

	if ( len < (int)sizeof( mdxmHeader_t ) )
		return qfalse;
	numSurfaces = LittleLong( header->numSurfaces );
	ofsLODs = LittleLong( header->ofsLODs );
	// the offsets of the surfaces of the first level of detail follow its mdxmLOD_t, and count from there
	if ( LittleLong( header->ident ) != MDXM_IDENT || LittleLong( header->numLODs ) < 1
		|| numSurfaces < 1 || numSurfaces > 4096
		|| ofsLODs < (int)sizeof( mdxmHeader_t ) || ofsLODs > len - (int)sizeof( mdxmLOD_t ) - numSurfaces * 4
		|| ofsHier > len - numSurfaces * 4 )
		return qfalse;
	base = ofsLODs + (int)sizeof( mdxmLOD_t );

	for ( i = 0; i < numSurfaces; i++ ) {
		int					ofsSurf, ofsVerts, count, index, ofsInfo;
		const mdxmSurface_t	*surf;
		const mdxmVertex_t	*v;

		memcpy( &ofsSurf, data + base + i * 4, 4 );
		ofsSurf = base + LittleLong( ofsSurf );
		if ( ofsSurf < base || ofsSurf > len - (int)sizeof( mdxmSurface_t ) )
			return qfalse;
		surf = (const mdxmSurface_t *)( data + ofsSurf );
		count = LittleLong( surf->numVerts );
		ofsVerts = ofsSurf + LittleLong( surf->ofsVerts );
		if ( count < 0 || count > 65536 || ofsVerts < ofsSurf || ofsVerts > len - count * (int)sizeof( mdxmVertex_t ) )
			return qfalse;

		// its entry in the hierarchy says whether it is drawn
		index = LittleLong( surf->thisSurfaceIndex );
		if ( index >= 0 && index < numSurfaces ) {
			memcpy( &ofsInfo, data + ofsHier + index * 4, 4 );
			ofsInfo = ofsHier + LittleLong( ofsInfo );
			if ( ofsInfo >= ofsHier && ofsInfo <= len - (int)sizeof( mdxmSurfHierarchy_t ) ) {
				const mdxmSurfHierarchy_t *info = (const mdxmSurfHierarchy_t *)( data + ofsInfo );

				if ( LittleLong( info->flags ) & G2SURFACEFLAG_OFF )
					continue;
			}
		}

		v = (const mdxmVertex_t *)( data + ofsVerts );
		for ( k = 0; k < count; k++, v++ ) {
			vec3_t point;

			VectorSet( point, LittleFloat( v->vertCoords[0] ), LittleFloat( v->vertCoords[1] ), LittleFloat( v->vertCoords[2] ) );
			if ( !isfinite( point[0] ) || !isfinite( point[1] ) || !isfinite( point[2] ) )
				return qfalse;
			BG_PropCloudAdd( cloud, point );
		}
	}
	return qtrue;
}

/*
=================
BG_PropCloudFromModel

Goes through the vertices of an md3 or a glm. qfalse: no such file, or none that could be read to its end.
After a qtrue the cloud holds the list of them, which BG_PropCloudFree() gives back.
=================
*/
qboolean BG_PropCloudFromModel( const char *model, propCloud_t *cloud ) {
	fileHandle_t	f = NULL_FILE;
	const int		len = trap->FS_Open( model, &f, FS_READ );
	qboolean		ok;
	byte			*data;
	int				k;

	BG_PropCloudClear( cloud );
	if ( !f )
		return qfalse;
	// the size of a model is not known before it is opened
	if ( len < 16 || len > PROP_MODEL_BYTES || !( data = (byte *)malloc( len ) ) ) {
		trap->FS_Close( f );
		return qfalse;
	}
	trap->FS_Read( data, len, f );
	trap->FS_Close( f );

	ok = Q_stricmp( COM_GetExtension( model ), "glm" ) ? Cloud_Md3( data, len, cloud ) : Cloud_Glm( data, len, cloud );
	free( data );

	for ( k = 0; k < 3 && ok; k++ ) {
		if ( !cloud->count || !( cloud->maxs[k] - cloud->mins[k] >= 0.5f ) )
			ok = qfalse;
	}
	if ( !ok )
		BG_PropCloudFree( cloud );
	return ok;
}

#endif // BG_PROPS_NO_FILES
