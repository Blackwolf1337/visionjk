/*
The shape of a prop (g_props.c): the convex hull of its model, for the physics of the server and for the moves
of the players, which the client predicts. Not from JA+.
*/

#pragma once

#define PROP_HULL_DIRS		26		// the model is felt along these directions first, a corner of the hull for each
#define PROP_MAX_VERTS		32		// then the vertices that stick out of that furthest are added up to this many
#define PROP_MAX_PLANES		64		// a hull of V corners has 2V - 4 faces at most, and some room
#define PROP_MAX_LOOP		192		// the corners of all faces: twice its edges, 2 * ( 3V - 6 ) at most
#define PROP_MAX_EDGES		96

#define PROP_BRUSH_PLANES	640		// of a shape as a brush in the world: its faces, six sides and six for each edge

// entityState_t.generic1 of a prop that is in the way of players: how its shape is made of its model, so the
// client can make the same one to predict with. 0: it is in nobody's way.
#define PROP_SOLID_HULL		1		// the hull of the model, or the box below if the model has none
#define PROP_SOLID_BOX		2		// the box around the model
// entityState_t.generic1 of the entity (ET_INVISIBLE) that stands for the beam of a physgun: otherEntityNum is
// the player who holds it, otherEntityNum2 the prop or the player in it. The cgame draws a line between them.
#define PROP_BEAM			4

// A trace of a move that ended at a prop names the world as what it hit, with the contents of a body, which
// the world never has: that is how the code of the moves tells a prop from a wall.
#define BG_PropHit( tr )	( (tr)->fraction < 1.0f && (tr)->entityNum == ENTITYNUM_WORLD && ( (tr)->contents & CONTENTS_BODY ) )
// playerState_t.forceKickFlip after a flip off a prop, where a flip off a player has his number plus one
#define PROP_KICK_FLIP		( ENTITYNUM_WORLD + 1 )

#define PROP_MIN_HALF		2.0f	// nothing is thinner than twice this
#define PROP_MODEL_BYTES	( 32 * 1024 * 1024 )	// of a model file that is read for its corners

typedef struct propShape_s {
	int		numVerts;
	vec3_t	verts[PROP_MAX_VERTS];		// its corners, from its middle, in the model
	int		numPlanes;
	vec4_t	planes[PROP_MAX_PLANES];	// its faces: the normal out of it and the distance from the middle
	byte	loopStart[PROP_MAX_PLANES];	// the corners of each face in loop[], in order around the normal
	byte	loopCount[PROP_MAX_PLANES];
	byte	loop[PROP_MAX_LOOP];
	int		numEdges;
	byte	edges[PROP_MAX_EDGES][2];
	vec3_t	centre;						// from the origin of the model to the middle, which is its centre of mass
	vec3_t	mins, maxs;					// the box around it, from the middle
	float	radius;						// from the middle to its furthest corner
	float	inner;						// and to its nearest face
} propShape_t;

// a shape where it stands, as the planes the move of a box is cut against
typedef struct propBrush_s {
	int		numPlanes;
	vec4_t	planes[PROP_BRUSH_PLANES];	// in the world: the normal out of it and the distance
	vec3_t	mins, maxs;					// the box in the world around it
} propBrush_t;

// The vertices of a model: the furthest one along each direction, and all of them, kept until the hull is made.
// BG_PropCloudFree() gives the list back.
typedef struct propCloud_s {
	int		count;
	vec3_t	mins, maxs;
	vec3_t	far[PROP_HULL_DIRS];
	float	reach[PROP_HULL_DIRS];
	vec3_t	*points;
	int		room;
} propCloud_t;

void		BG_PropCloudClear( propCloud_t *cloud );
void		BG_PropCloudAdd( propCloud_t *cloud, const vec3_t point );
void		BG_PropCloudFree( propCloud_t *cloud );
qboolean	BG_PropCloudFromModel( const char *model, propCloud_t *cloud );

void		BG_PropShapeBox( const vec3_t mins, const vec3_t maxs, float scale, propShape_t *shape );
qboolean	BG_PropShapeHull( propCloud_t *cloud, float scale, propShape_t *shape );
void		BG_PropShapeOfKind( propCloud_t *cloud, int kind, float scale, propShape_t *shape );

float		BG_PropReach( const propShape_t *shape, const vec3_t dir );
qboolean	BG_PropHolds( const propShape_t *shape, const vec3_t point, float skin );
float		BG_PropNearest( const propShape_t *shape, const vec3_t point, vec3_t out );
void		BG_PropBrush( const propShape_t *shape, const vec3_t middle, matrix3_t axis, propBrush_t *brush );
void		BG_PropClipBox( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
				const propBrush_t *brush );
