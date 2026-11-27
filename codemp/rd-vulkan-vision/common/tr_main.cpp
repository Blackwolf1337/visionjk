/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// tr_main.c -- main control flow for each frame
#include "tr_local.h"
#include "ghoul2/g2_local.h"

trGlobals_t		tr;

static float	s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};

refimport_t	ri;

// entities that will have procedurally generated surfaces will just
// point at this for their sorting surface
surfaceType_t	entitySurface = SF_ENTITY;

/*
=================
R_CullLocalBox

Returns CULL_IN, CULL_CLIP, or CULL_OUT
=================
*/
int R_CullLocalBox (const vec3_t bounds[2]) {
	int		i, j;
	vec3_t	transformed[8];
	float	dists[8];
	vec3_t	v;
	cplane_t	*frust;
	int			anyBack;
	int			front, back;

	if ( r_nocull->integer==1 ) {
		return CULL_CLIP;
	}

	// transform into world space
	for (i = 0 ; i < 8 ; i++) {
		v[0] = bounds[i&1][0];
		v[1] = bounds[(i>>1)&1][1];
		v[2] = bounds[(i>>2)&1][2];

		VectorCopy( tr.ori.origin, transformed[i] );
		VectorMA( transformed[i], v[0], tr.ori.axis[0], transformed[i] );
		VectorMA( transformed[i], v[1], tr.ori.axis[1], transformed[i] );
		VectorMA( transformed[i], v[2], tr.ori.axis[2], transformed[i] );
	}

	// check against frustum planes
	anyBack = 0;
	for (i = 0 ; i < 4 ; i++) {
		frust = &tr.viewParms.frustum[i];

		front = back = 0;
		for (j = 0 ; j < 8 ; j++) {
			dists[j] = DotProduct(transformed[j], frust->normal);
			if ( dists[j] > frust->dist ) {
				front = 1;
				if ( back ) {
					break;		// a point is in front
				}
			} else {
				back = 1;
			}
		}
		if ( !front ) {
			// all points were behind one of the planes
			return CULL_OUT;
		}
		anyBack |= back;
	}

	if ( !anyBack ) {
		return CULL_IN;		// completely inside frustum
	}

	return CULL_CLIP;		// partially clipped
}

/*
** R_CullLocalPointAndRadius
*/
int R_CullLocalPointAndRadius( const vec3_t pt, float radius )
{
	vec3_t transformed;

	R_LocalPointToWorld( pt, transformed );

	return R_CullPointAndRadius( transformed, radius );
}

/*
** R_CullPointAndRadius
*/
int R_CullPointAndRadius( const vec3_t pt, float radius )
{
	int		i;
	float	dist;
	cplane_t	*frust;
	qboolean mightBeClipped = qfalse;

	if ( r_nocull->integer==1 ) {
		return CULL_CLIP;
	}

	// check against frustum planes
	for (i = 0 ; i < 4 ; i++)
	{
		frust = &tr.viewParms.frustum[i];

		dist = DotProduct( pt, frust->normal) - frust->dist;
		if ( dist < -radius )
		{
			return CULL_OUT;
		}
		else if ( dist <= radius )
		{
			mightBeClipped = qtrue;
		}
	}

	if ( mightBeClipped )
	{
		return CULL_CLIP;
	}

	return CULL_IN;		// completely inside frustum
}

/*
=================
R_LocalNormalToWorld

=================
*/
void R_LocalNormalToWorld (const vec3_t local, vec3_t world) {
	world[0] = local[0] * tr.ori.axis[0][0] + local[1] * tr.ori.axis[1][0] + local[2] * tr.ori.axis[2][0];
	world[1] = local[0] * tr.ori.axis[0][1] + local[1] * tr.ori.axis[1][1] + local[2] * tr.ori.axis[2][1];
	world[2] = local[0] * tr.ori.axis[0][2] + local[1] * tr.ori.axis[1][2] + local[2] * tr.ori.axis[2][2];
}

/*
=================
R_LocalPointToWorld

=================
*/
void R_LocalPointToWorld (const vec3_t local, vec3_t world) {
	world[0] = local[0] * tr.ori.axis[0][0] + local[1] * tr.ori.axis[1][0] + local[2] * tr.ori.axis[2][0] + tr.ori.origin[0];
	world[1] = local[0] * tr.ori.axis[0][1] + local[1] * tr.ori.axis[1][1] + local[2] * tr.ori.axis[2][1] + tr.ori.origin[1];
	world[2] = local[0] * tr.ori.axis[0][2] + local[1] * tr.ori.axis[1][2] + local[2] * tr.ori.axis[2][2] + tr.ori.origin[2];
}

float preTransEntMatrix[16];

/*
=================
R_WorldNormalToEntity

=================
*/
void R_WorldNormalToEntity (const vec3_t worldvec, vec3_t entvec)
{
	entvec[0] = -worldvec[0] * preTransEntMatrix[0] - worldvec[1] * preTransEntMatrix[4] + worldvec[2] * preTransEntMatrix[8];
	entvec[1] = -worldvec[0] * preTransEntMatrix[1] - worldvec[1] * preTransEntMatrix[5] + worldvec[2] * preTransEntMatrix[9];
	entvec[2] = -worldvec[0] * preTransEntMatrix[2] - worldvec[1] * preTransEntMatrix[6] + worldvec[2] * preTransEntMatrix[10];
}

/*
=================
R_WorldPointToEntity

=================
*/
/*void R_WorldPointToEntity (vec3_t worldvec, vec3_t entvec)
{
	entvec[0] = worldvec[0] * preTransEntMatrix[0] + worldvec[1] * preTransEntMatrix[4] + worldvec[2] * preTransEntMatrix[8]+preTransEntMatrix[12];
	entvec[1] = worldvec[0] * preTransEntMatrix[1] + worldvec[1] * preTransEntMatrix[5] + worldvec[2] * preTransEntMatrix[9]+preTransEntMatrix[13];
	entvec[2] = worldvec[0] * preTransEntMatrix[2] + worldvec[1] * preTransEntMatrix[6] + worldvec[2] * preTransEntMatrix[10]+preTransEntMatrix[14];
}
*/

/*
=================
R_WorldToLocal

=================
*/
void R_WorldToLocal (vec3_t world, vec3_t local) {
	local[0] = DotProduct(world, tr.ori.axis[0]);
	local[1] = DotProduct(world, tr.ori.axis[1]);
	local[2] = DotProduct(world, tr.ori.axis[2]);
}

/*
==========================
R_TransformModelToClip

==========================
*/
void R_TransformModelToClip( const vec3_t src, const float *modelMatrix, const float *projectionMatrix,
							vec4_t eye, vec4_t dst ) {
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		eye[i] =
			src[0] * modelMatrix[ i + 0 * 4 ] +
			src[1] * modelMatrix[ i + 1 * 4 ] +
			src[2] * modelMatrix[ i + 2 * 4 ] +
			1 * modelMatrix[ i + 3 * 4 ];
	}

	for ( i = 0 ; i < 4 ; i++ ) {
		dst[i] =
			eye[0] * projectionMatrix[ i + 0 * 4 ] +
			eye[1] * projectionMatrix[ i + 1 * 4 ] +
			eye[2] * projectionMatrix[ i + 2 * 4 ] +
			eye[3] * projectionMatrix[ i + 3 * 4 ];
	}
}

/*
==========================
R_TransformClipToWindow

==========================
*/
void R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view, vec4_t normalized, vec4_t window ) {
	normalized[0] = clip[0] / clip[3];
	normalized[1] = clip[1] / clip[3];
	normalized[2] = ( clip[2] + clip[3] ) / ( 2 * clip[3] );

	window[0] = 0.5f * ( 1.0f + normalized[0] ) * view->viewportWidth;
	window[1] = 0.5f * ( 1.0f + normalized[1] ) * view->viewportHeight;
	window[2] = normalized[2];

	window[0] = (int) ( window[0] + 0.5 );
	window[1] = (int) ( window[1] + 0.5 );
}


/*
==========================
myGlMultMatrix

==========================
*/
void myGlMultMatrix( const float *a, const float *b, float *out ) {
	int		i, j;

	for ( i = 0 ; i < 4 ; i++ ) {
		for ( j = 0 ; j < 4 ; j++ ) {
			out[ i * 4 + j ] =
				a [ i * 4 + 0 ] * b [ 0 * 4 + j ]
				+ a [ i * 4 + 1 ] * b [ 1 * 4 + j ]
				+ a [ i * 4 + 2 ] * b [ 2 * 4 + j ]
				+ a [ i * 4 + 3 ] * b [ 3 * 4 + j ];
		}
	}
}

/*
=================
R_RotateForEntity

Generates an orientation for an entity and viewParms
Does NOT produce any GL calls
Called by both the front end and the back end
=================
*/
void R_RotateForEntity( const trRefEntity_t *ent, const viewParms_t *viewParms,
					   orientationr_t *ori ) {
//	float	glMatrix[16];
	vec3_t	delta;
	float	axisLength;

	if ( ent->e.reType != RT_MODEL ) {
		*ori = viewParms->world;
		return;
	}

	VectorCopy( ent->e.origin, ori->origin );

	VectorCopy( ent->e.axis[0], ori->axis[0] );
	VectorCopy( ent->e.axis[1], ori->axis[1] );
	VectorCopy( ent->e.axis[2], ori->axis[2] );

	preTransEntMatrix[0] = ori->axis[0][0];
	preTransEntMatrix[4] = ori->axis[1][0];
	preTransEntMatrix[8] = ori->axis[2][0];
	preTransEntMatrix[12] = ori->origin[0];

	preTransEntMatrix[1] = ori->axis[0][1];
	preTransEntMatrix[5] = ori->axis[1][1];
	preTransEntMatrix[9] = ori->axis[2][1];
	preTransEntMatrix[13] = ori->origin[1];

	preTransEntMatrix[2] = ori->axis[0][2];
	preTransEntMatrix[6] = ori->axis[1][2];
	preTransEntMatrix[10] = ori->axis[2][2];
	preTransEntMatrix[14] = ori->origin[2];

	preTransEntMatrix[3] = 0;
	preTransEntMatrix[7] = 0;
	preTransEntMatrix[11] = 0;
	preTransEntMatrix[15] = 1;

	myGlMultMatrix( preTransEntMatrix, viewParms->world.modelMatrix, ori->modelMatrix );

	// calculate the viewer origin in the model's space
	// needed for fog, specular, and environment mapping
	VectorSubtract( viewParms->ori.origin, ori->origin, delta );

	// compensate for scale in the axes if necessary
	if ( ent->e.nonNormalizedAxes ) {
		axisLength = VectorLength( ent->e.axis[0] );
		if ( !axisLength ) {
			axisLength = 0;
		} else {
			axisLength = 1.0f / axisLength;
		}
	} else {
		axisLength = 1.0f;
	}

	ori->viewOrigin[0] = DotProduct( delta, ori->axis[0] ) * axisLength;
	ori->viewOrigin[1] = DotProduct( delta, ori->axis[1] ) * axisLength;
	ori->viewOrigin[2] = DotProduct( delta, ori->axis[2] ) * axisLength;
}

/*
=================
R_RotateForViewer

Sets up the modelview matrix for a given viewParm
=================
*/
void R_RotateForViewer (void)
{
	float	viewerMatrix[16];
	vec3_t	origin;

	memset (&tr.ori, 0, sizeof(tr.ori));
	tr.ori.axis[0][0] = 1;
	tr.ori.axis[1][1] = 1;
	tr.ori.axis[2][2] = 1;
	VectorCopy (tr.viewParms.ori.origin, tr.ori.viewOrigin);

	// transform by the camera placement
	VectorCopy( tr.viewParms.ori.origin, origin );

	viewerMatrix[0] = tr.viewParms.ori.axis[0][0];
	viewerMatrix[4] = tr.viewParms.ori.axis[0][1];
	viewerMatrix[8] = tr.viewParms.ori.axis[0][2];
	viewerMatrix[12] = -origin[0] * viewerMatrix[0] + -origin[1] * viewerMatrix[4] + -origin[2] * viewerMatrix[8];

	viewerMatrix[1] = tr.viewParms.ori.axis[1][0];
	viewerMatrix[5] = tr.viewParms.ori.axis[1][1];
	viewerMatrix[9] = tr.viewParms.ori.axis[1][2];
	viewerMatrix[13] = -origin[0] * viewerMatrix[1] + -origin[1] * viewerMatrix[5] + -origin[2] * viewerMatrix[9];

	viewerMatrix[2] = tr.viewParms.ori.axis[2][0];
	viewerMatrix[6] = tr.viewParms.ori.axis[2][1];
	viewerMatrix[10] = tr.viewParms.ori.axis[2][2];
	viewerMatrix[14] = -origin[0] * viewerMatrix[2] + -origin[1] * viewerMatrix[6] + -origin[2] * viewerMatrix[10];

	viewerMatrix[3] = 0;
	viewerMatrix[7] = 0;
	viewerMatrix[11] = 0;
	viewerMatrix[15] = 1;

	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	myGlMultMatrix( viewerMatrix, s_flipMatrix, tr.ori.modelMatrix );

	tr.viewParms.world = tr.ori;

}

/*
** SetFarClip
*/
static void SetFarClip( void )
{
	float	farthestCornerDistance = 0;
	int		i;

	// if not rendering the world (icons, menus, etc)
	// set a 2k far clip plane
	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		if (tr.refdef.rdflags & RDF_AUTOMAP)
		{ //override the zfar then
			tr.viewParms.zFar = 32768.0f;
		}
		else
		{
			tr.viewParms.zFar = 2048.0f;
		}
		return;
	}

	//
	// set far clipping planes dynamically
	//
	for ( i = 0; i < 8; i++ )
	{
		vec3_t v;
		float distance;

		if ( i & 1 )
		{
			v[0] = tr.viewParms.visBounds[0][0];
		}
		else
		{
			v[0] = tr.viewParms.visBounds[1][0];
		}

		if ( i & 2 )
		{
			v[1] = tr.viewParms.visBounds[0][1];
		}
		else
		{
			v[1] = tr.viewParms.visBounds[1][1];
		}

		if ( i & 4 )
		{
			v[2] = tr.viewParms.visBounds[0][2];
		}
		else
		{
			v[2] = tr.viewParms.visBounds[1][2];
		}

		distance = DistanceSquared(tr.viewParms.ori.origin, v);

		if ( distance > farthestCornerDistance )
		{
			farthestCornerDistance = distance;
		}
	}
	// Bring in the zFar to the distanceCull distance
	// The sky renders at zFar so need to move it out a little
	// ...and make sure there is a minimum zfar to prevent problems
	tr.viewParms.zFar = Com_Clamp(2048.0f, tr.distanceCull * (1.732), sqrtf( farthestCornerDistance ));
}


/*
===============
R_SetupProjection
===============
*/
void R_SetupProjection( void ) {
	float	xmin, xmax, ymin, ymax;
	float	width, height, depth;
	float	zNear, zFar;

	// dynamically compute far clip plane distance
	SetFarClip();

	//
	// set up projection matrix
	//
	zNear	= r_znear->value;
	zFar	= tr.viewParms.zFar;

	ymax = zNear * tan( tr.refdef.fov_y * M_PI / 360.0f );
	ymin = -ymax;

	xmax = zNear * tan( tr.refdef.fov_x * M_PI / 360.0f );
	xmin = -xmax;

	width = xmax - xmin;
	height = ymax - ymin;
	depth = zFar - zNear;

	tr.viewParms.projectionMatrix[0] = 2 * zNear / width;
	tr.viewParms.projectionMatrix[4] = 0;
	tr.viewParms.projectionMatrix[8] = ( xmax + xmin ) / width;	// normally 0
	tr.viewParms.projectionMatrix[12] = 0;

	tr.viewParms.projectionMatrix[1] = 0;
	tr.viewParms.projectionMatrix[5] = 2 * zNear / height;
	tr.viewParms.projectionMatrix[9] = ( ymax + ymin ) / height;	// normally 0
	tr.viewParms.projectionMatrix[13] = 0;

	tr.viewParms.projectionMatrix[2] = 0;
	tr.viewParms.projectionMatrix[6] = 0;
	tr.viewParms.projectionMatrix[10] = -( zFar + zNear ) / depth;
	tr.viewParms.projectionMatrix[14] = -2 * zFar * zNear / depth;

	tr.viewParms.projectionMatrix[3] = 0;
	tr.viewParms.projectionMatrix[7] = 0;
	tr.viewParms.projectionMatrix[11] = -1;
	tr.viewParms.projectionMatrix[15] = 0;
}

/*
=================
R_SetupFrustum

Setup that culling frustum planes for the current view
=================
*/
void R_SetupFrustum (void) {
	int		i;
	float	xs, xc;
	float	ang;

	ang = tr.viewParms.fovX / 180 * M_PI * 0.5f;
	xs = sin( ang );
	xc = cos( ang );

	VectorScale( tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[0].normal );
	VectorMA( tr.viewParms.frustum[0].normal, xc, tr.viewParms.ori.axis[1], tr.viewParms.frustum[0].normal );

	VectorScale( tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[1].normal );
	VectorMA( tr.viewParms.frustum[1].normal, -xc, tr.viewParms.ori.axis[1], tr.viewParms.frustum[1].normal );

	ang = tr.viewParms.fovY / 180 * M_PI * 0.5f;
	xs = sin( ang );
	xc = cos( ang );

	VectorScale( tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[2].normal );
	VectorMA( tr.viewParms.frustum[2].normal, xc, tr.viewParms.ori.axis[2], tr.viewParms.frustum[2].normal );

	VectorScale( tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[3].normal );
	VectorMA( tr.viewParms.frustum[3].normal, -xc, tr.viewParms.ori.axis[2], tr.viewParms.frustum[3].normal );

	// VisionJK: a view of what lies behind thick glass (R_GlassViews) is drawn for a part of the main view only: the
	// planes through the borders of that part (right, left, lower, upper, as the ones above)
	if ( tr.viewParms.glassView ) {
		const float	*part = tr.viewParms.glassBounds;
		const float	tanX = tan( tr.viewParms.fovX / 180 * M_PI * 0.5f ), tanY = tan( tr.viewParms.fovY / 180 * M_PI * 0.5f );
		vec3_t		back;

		VectorMA( tr.viewParms.ori.axis[1], part[2] * tanX, tr.viewParms.ori.axis[0], tr.viewParms.frustum[0].normal );
		VectorNegate( tr.viewParms.ori.axis[1], back );
		VectorMA( back, -part[0] * tanX, tr.viewParms.ori.axis[0], tr.viewParms.frustum[1].normal );
		VectorMA( tr.viewParms.ori.axis[2], -part[1] * tanY, tr.viewParms.ori.axis[0], tr.viewParms.frustum[2].normal );
		VectorNegate( tr.viewParms.ori.axis[2], back );
		VectorMA( back, part[3] * tanY, tr.viewParms.ori.axis[0], tr.viewParms.frustum[3].normal );
		for ( i = 0; i < 4; i++ ) {
			VectorNormalize( tr.viewParms.frustum[i].normal );
		}
	}

	for (i=0 ; i<4 ; i++) {
		tr.viewParms.frustum[i].type = PLANE_NON_AXIAL;
		tr.viewParms.frustum[i].dist = DotProduct (tr.viewParms.ori.origin, tr.viewParms.frustum[i].normal);
		SetPlaneSignbits( &tr.viewParms.frustum[i] );
	}
}


/*
=================
R_MirrorPoint
=================
*/
void R_MirrorPoint (vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out) {
	int		i;
	vec3_t	local;
	vec3_t	transformed;
	float	d;

	VectorSubtract( in, surface->origin, local );

	VectorClear( transformed );
	for ( i = 0 ; i < 3 ; i++ ) {
		d = DotProduct(local, surface->axis[i]);
		VectorMA( transformed, d, camera->axis[i], transformed );
	}

	VectorAdd( transformed, camera->origin, out );
}

void R_MirrorVector (vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out) {
	int		i;
	float	d;

	VectorClear( out );
	for ( i = 0 ; i < 3 ; i++ ) {
		d = DotProduct(in, surface->axis[i]);
		VectorMA( out, d, camera->axis[i], out );
	}
}


/*
=============
R_PlaneForSurface
=============
*/
void R_PlaneForSurface (surfaceType_t *surfType, cplane_t *plane) {
	srfTriangles_t	*tri;
	srfPoly_t		*poly;
	drawVert_t		*v1, *v2, *v3;
	vec4_t			plane4;

	if (!surfType) {
		memset (plane, 0, sizeof(*plane));
		plane->normal[0] = 1;
		return;
	}
	switch (*surfType) {
	case SF_FACE:
		*plane = ((srfSurfaceFace_t *)surfType)->plane;
		return;
	case SF_TRIANGLES:
		tri = (srfTriangles_t *)surfType;
		v1 = tri->verts + tri->indexes[0];
		v2 = tri->verts + tri->indexes[1];
		v3 = tri->verts + tri->indexes[2];
		PlaneFromPoints( plane4, v1->xyz, v2->xyz, v3->xyz );
		VectorCopy( plane4, plane->normal );
		plane->dist = plane4[3];
		return;
	case SF_POLY:
		poly = (srfPoly_t *)surfType;
		PlaneFromPoints( plane4, poly->verts[0].xyz, poly->verts[1].xyz, poly->verts[2].xyz );
		VectorCopy( plane4, plane->normal );
		plane->dist = plane4[3];
		return;
	default:
		memset (plane, 0, sizeof(*plane));
		plane->normal[0] = 1;
		return;
	}
}

/*
=================
R_GetPortalOrientation

entityNum is the entity that the portal surface is a part of, which may
be moving and rotating.

Returns qtrue if it should be mirrored
=================
*/
qboolean R_GetPortalOrientations( drawSurf_t *drawSurf, int entityNum,
							 orientation_t *surface, orientation_t *camera,
							 vec3_t pvsOrigin, qboolean *mirror ) {
	int			i;
	cplane_t	originalPlane, plane;
	trRefEntity_t	*e;
	float		d;
	vec3_t		transformed;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	if ( entityNum != REFENTITYNUM_WORLD ) {
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.ori );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.ori.origin );

		// translate the original plane
		originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.ori.origin );
	} else {
		plane = originalPlane;
	}

	VectorCopy( plane.normal, surface->axis[0] );
	PerpendicularVector( surface->axis[1], surface->axis[0] );
	CrossProduct( surface->axis[0], surface->axis[1], surface->axis[2] );

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for ( i = 0 ; i < tr.refdef.num_entities ; i++ ) {
		e = &tr.refdef.entities[i];
		if ( e->e.reType != RT_PORTALSURFACE ) {
			continue;
		}

		d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
		if ( d > 64 || d < -64) {
			continue;
		}

		// get the pvsOrigin from the entity
		VectorCopy( e->e.oldorigin, pvsOrigin );

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[0] == e->e.origin[0] &&
			e->e.oldorigin[1] == e->e.origin[1] &&
			e->e.oldorigin[2] == e->e.origin[2] ) {
			VectorScale( plane.normal, plane.dist, surface->origin );
			VectorCopy( surface->origin, camera->origin );
			VectorSubtract( vec3_origin, surface->axis[0], camera->axis[0] );
			VectorCopy( surface->axis[1], camera->axis[1] );
			VectorCopy( surface->axis[2], camera->axis[2] );

			*mirror = qtrue;
			return qtrue;
		}

		// project the origin onto the surface plane to get
		// an origin point we can rotate around
		d = DotProduct( e->e.origin, plane.normal ) - plane.dist;
		VectorMA( e->e.origin, -d, surface->axis[0], surface->origin );

		// now get the camera origin and orientation
		VectorCopy( e->e.oldorigin, camera->origin );
		AxisCopy( e->e.axis, camera->axis );
		VectorSubtract( vec3_origin, camera->axis[0], camera->axis[0] );
		VectorSubtract( vec3_origin, camera->axis[1], camera->axis[1] );

		// optionally rotate
		if ( e->e.oldframe ) {
			// if a speed is specified
			if ( e->e.frame ) {
				// continuous rotate
				d = (tr.refdef.time/1000.0f) * e->e.frame;
				VectorCopy( camera->axis[1], transformed );
				RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
				CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
			} else {
				// bobbing rotate, with skinNum being the rotation offset
				d = sin( tr.refdef.time * 0.003f );
				d = e->e.skinNum + d * 4;
				VectorCopy( camera->axis[1], transformed );
				RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
				CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
			}
		}
		else if ( e->e.skinNum ) {
			d = e->e.skinNum;
			VectorCopy( camera->axis[1], transformed );
			RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
			CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
		}
		*mirror = qfalse;
		return qtrue;
	}

	// if we didn't locate a portal entity, don't render anything.
	// We don't want to just treat it as a mirror, because without a
	// portal entity the server won't have communicated a proper entity set
	// in the snapshot

	// unfortunately, with local movement prediction it is easily possible
	// to see a surface before the server has communicated the matching
	// portal surface entity, so we don't want to print anything here...

	//ri.Printf( PRINT_ALL, "Portal surface without a portal entity\n" );

	return qfalse;
}

static qboolean IsMirror( const drawSurf_t *drawSurf, int entityNum )
{
	int			i;
	cplane_t	originalPlane, plane;
	trRefEntity_t	*e;
	float		d;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	if ( entityNum != REFENTITYNUM_WORLD )
	{
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.ori );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.ori.origin );

		// translate the original plane
		originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.ori.origin );
	}
	else
	{
		plane = originalPlane;
	}

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for ( i = 0 ; i < tr.refdef.num_entities ; i++ )
	{
		e = &tr.refdef.entities[i];
		if ( e->e.reType != RT_PORTALSURFACE ) {
			continue;
		}

		d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
		if ( d > 64 || d < -64) {
			continue;
		}

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[0] == e->e.origin[0] &&
			e->e.oldorigin[1] == e->e.origin[1] &&
			e->e.oldorigin[2] == e->e.origin[2] )
		{
			return qtrue;
		}

		return qfalse;
	}
	return qfalse;
}

/*
** SurfIsOffscreen
**
** Determines if a surface is completely offscreen.
*/
static qboolean SurfIsOffscreen( const drawSurf_t *drawSurf, vec4_t clipDest[128] ) {
	float shortest = 100000000;
	int entityNum;
	int numTriangles;
	shader_t *shader;
	int		fogNum;
	int dlighted;
	vec4_t clip, eye;
	int i;
	unsigned int pointOr = 0;
	unsigned int pointAnd = (unsigned int)~0;

	R_RotateForViewer();

	R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
	RB_BeginSurface( shader, fogNum );
	rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
	assert( tess.numVertexes < 128 );
	for ( i = 0; i < tess.numVertexes; i++ )
	{
		int j;
		unsigned int pointFlags = 0;

		R_TransformModelToClip( tess.xyz[i], tr.ori.modelMatrix, tr.viewParms.projectionMatrix, eye, clip );

		for ( j = 0; j < 3; j++ )
		{
			if ( clip[j] >= clip[3] )
			{
				pointFlags |= (1 << (j*2));
			}
			else if ( clip[j] <= -clip[3] )
			{
				pointFlags |= ( 1 << (j*2+1));
			}
		}
		pointAnd &= pointFlags;
		pointOr |= pointFlags;
	}

	// trivially reject
	if ( pointAnd )
	{
		return qtrue;
	}

	// determine if this surface is backfaced and also determine the distance
	// to the nearest vertex so we can cull based on portal range.  Culling
	// based on vertex distance isn't 100% correct (we should be checking for
	// range to the surface), but it's good enough for the types of portals
	// we have in the game right now.
	numTriangles = tess.numIndexes / 3;

	for ( i = 0; i < tess.numIndexes; i += 3 )
	{
		vec3_t normal;
		float dot;
		float len;

		VectorSubtract( tess.xyz[tess.indexes[i]], tr.viewParms.ori.origin, normal );

		len = VectorLengthSquared( normal );			// lose the sqrt
		if ( len < shortest )
		{
			shortest = len;
		}

		if ( ( dot = DotProduct( normal, tess.normal[tess.indexes[i]] ) ) >= 0 )
		{
			numTriangles--;
		}
	}
	if ( !numTriangles )
	{
		return qtrue;
	}

	// mirrors can early out at this point, since we don't do a fade over distance
	// with them (although we could)
	if ( IsMirror( drawSurf, entityNum ) )
	{
		return qfalse;
	}

	if ( shortest > (tess.shader->portalRange*tess.shader->portalRange) )
	{
		return qtrue;
	}

	return qfalse;
}

/*
========================
R_MirrorViewBySurface

Returns qtrue if another view has been rendered
========================
*/
qboolean R_MirrorViewBySurface (drawSurf_t *drawSurf, int entityNum) {
	vec4_t			clipDest[128];
	viewParms_t		newParms;
	viewParms_t		oldParms;
	orientation_t	surface, camera;

	// don't recursively mirror
	if (tr.viewParms.isPortal) {
		ri.Printf( PRINT_DEVELOPER, S_COLOR_RED "WARNING: recursive mirror/portal found\n" );
		return qfalse;
	}

	if ( r_noportals->integer || (r_fastsky->integer == 1) ) {
		return qfalse;
	}

	// trivially reject portal/mirror
	if ( SurfIsOffscreen( drawSurf, clipDest ) ) {
		return qfalse;
	}

	// save old viewParms so we can return to it after the mirror view
	oldParms = tr.viewParms;

	newParms = tr.viewParms;
	newParms.isPortal = qtrue;
	if ( !R_GetPortalOrientations( drawSurf, entityNum, &surface, &camera,
		newParms.pvsOrigin, &newParms.isMirror ) ) {
		return qfalse;		// bad portal, no portalentity
	}

	R_MirrorPoint (oldParms.ori.origin, &surface, &camera, newParms.ori.origin );

	VectorSubtract( vec3_origin, camera.axis[0], newParms.portalPlane.normal );
	newParms.portalPlane.dist = DotProduct( camera.origin, newParms.portalPlane.normal );

	R_MirrorVector (oldParms.ori.axis[0], &surface, &camera, newParms.ori.axis[0]);
	R_MirrorVector (oldParms.ori.axis[1], &surface, &camera, newParms.ori.axis[1]);
	R_MirrorVector (oldParms.ori.axis[2], &surface, &camera, newParms.ori.axis[2]);

	// OPTIMIZE: restrict the viewport on the mirrored view

	// render the mirror view
	R_RenderView (&newParms);

	tr.viewParms = oldParms;

	return qtrue;
}

/*
========================
R_PuddleView

VisionJK: standing water of RE_AddWet mirrors the room. The level is drawn once more for the view, mirrored at the
floor of the puddle that matters most (in view, large and near), and clipped there; rd-vulkan-vision draws that
view into targets of its own (VK_BeginPuddle) and the puddles at that height read it. Before the portals of the
level, whose view stays in the frame.
========================
*/
static void R_PuddleView( void ) {
	const wetSpot_t	*best = NULL;
	float			bestScore = 0.0f;
	viewParms_t		newParms, oldParms;
	float			height;
	int				i;

	if ( tr.viewParms.isPortal || ( tr.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) || r_noportals->integer
		|| !VK_PuddleMirror() ) {
		return;
	}
	for ( i = 0; i < r_numWet; i++ ) {
		const wetSpot_t *spot = &r_wet[i];
		float score;

		// (a streak of blood carries its width in puddle, it is no puddle to mirror)
		if ( spot->kind == WET_BLOOD_STREAK || spot->puddle < 0.05f || spot->normal[2] < 0.9f || spot->radius <= 0.0f
			|| R_CullPointAndRadius( spot->origin, spot->radius ) == CULL_OUT ) {
			continue;
		}
		score = spot->puddle * spot->radius * spot->radius / Q_max( Distance( spot->origin, tr.viewParms.ori.origin ), 32.0f );
		if ( score > bestScore ) {
			bestScore = score;
			best = spot;
		}
	}
	// (an eye under the floor sees no mirror in it)
	if ( !best || tr.viewParms.ori.origin[2] <= best->origin[2] + 1.0f ) {
		return;
	}
	height = best->origin[2];

	oldParms = tr.viewParms;
	newParms = tr.viewParms;
	newParms.isPortal = qtrue;
	newParms.isMirror = qtrue;
	newParms.isPuddle = qtrue;
	VectorCopy( best->origin, newParms.pvsOrigin );
	newParms.pvsOrigin[2] += 8.0f;
	VectorSet( newParms.portalPlane.normal, 0.0f, 0.0f, 1.0f );
	newParms.portalPlane.dist = height;
	newParms.ori.origin[2] = 2.0f * height - oldParms.ori.origin[2];
	for ( i = 0; i < 3; i++ ) {
		newParms.ori.axis[i][2] = -oldParms.ori.axis[i][2];
	}
	// Into targets of half the size each way (VK_PuddleSize): the view halved, at the top left of the frame as the
	// device counts (the viewport counts from the bottom, VK_SetViewport turns it with the height of the frame).
	// The wet places read it at their pixel over the size of the frame, so it covers the same part of their picture.
	{
		const int top = glConfig.vidHeight - ( oldParms.viewportY + oldParms.viewportHeight );

		newParms.viewportX = oldParms.viewportX / 2;
		newParms.viewportWidth = VK_PuddleSize( oldParms.viewportWidth );
		newParms.viewportHeight = VK_PuddleSize( oldParms.viewportHeight );
		newParms.viewportY = glConfig.vidHeight - ( top / 2 + newParms.viewportHeight );
	}

	R_RenderView( &newParms );

	tr.viewParms = oldParms;
}

/*
========================
R_ProbeViews

VisionJK: one face of the next reflection probe of the level (R_CreateProbes) is drawn before the view of the level,
a square of PROBE_SIZE at the corner of the frame, 90 degrees each way, the level alone (no entity, no light of an
entity, no mark): rd-vulkan-vision draws it into targets of its own and copies it to its place among the faces
(VK_BeginProbe, VK_EndProbe). One face a frame, so a frame keeps all of its room for the view itself; with 32
probes all of them are drawn in 192 frames. The faces look along +x, -x, +y, -y, +z, -z (the same table in
generic.frag ProbeFace). Done again from the start when the device lost them (a new window size).
========================
*/
static const float probeAxes[6][3][3] = {	// forward, left, up
	{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
	{ { -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } },
	{ { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } },
	{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
	{ { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 } },
	{ { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 } },
};

void R_ProbeViews( const viewParms_t *main ) {
	viewParms_t	parms;
	int			face, probe, entities, dlights, polys;

	if ( !tr.world || !tr.numProbes || !VK_ProbesSupported() ) {
		return;
	}
	if ( VK_ProbesLost() ) {
		tr.probeNext = 0;
		tr.probesReady = qfalse;
	}
	if ( tr.probesReady ) {
		return;
	}
	probe = tr.probeNext / 6;
	face = tr.probeNext % 6;

	memset( &parms, 0, sizeof( parms ) );
	parms.viewportX = 0;
	parms.viewportY = glConfig.vidHeight - PROBE_SIZE;
	parms.viewportWidth = parms.viewportHeight = PROBE_SIZE;
	parms.fovX = parms.fovY = 90.0f;
	parms.isPortal = qtrue;
	parms.probeFace = face + 1;
	parms.probeIndex = probe;
	VectorCopy( tr.probes[probe], parms.ori.origin );
	VectorCopy( tr.probes[probe], parms.pvsOrigin );
	for ( int i = 0; i < 3; i++ ) {
		VectorCopy( probeAxes[face][i], parms.ori.axis[i] );
	}

	entities = tr.refdef.num_entities;
	dlights = tr.refdef.num_dlights;
	polys = tr.refdef.numPolys;
	tr.refdef.num_entities = tr.refdef.num_dlights = tr.refdef.numPolys = 0;
	R_RenderView( &parms );
	tr.refdef.num_entities = entities;
	tr.refdef.num_dlights = dlights;
	tr.refdef.numPolys = polys;
	tr.viewParms = *main;

	if ( ++tr.probeNext == tr.numProbes * 6 ) {
		tr.probesReady = qtrue;
	}
}

/*
=================
R_SpriteFogNum

See if a sprite is inside a fog volume
=================
*/
int R_SpriteFogNum( trRefEntity_t *ent ) {
	int				i, j;
	fog_t			*fog;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( ent->e.origin[j] - ent->e.radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( ent->e.origin[j] + ent->e.radius <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}

	return 0;
}

/*
==========================================================================================

DRAWSURF SORTING

==========================================================================================
*/

/*
===============
R_Radix
===============
*/
static QINLINE void R_Radix( int byte, int size, drawSurf_t *source, drawSurf_t *dest )
{
  int           count[ 256 ] = { 0 };
  int           index[ 256 ];
  int           i;
  unsigned char *sortKey = NULL;
  unsigned char *end = NULL;

  sortKey = ( (unsigned char *)&source[ 0 ].sort ) + byte;
  end = sortKey + ( size * sizeof( drawSurf_t ) );
  for( ; sortKey < end; sortKey += sizeof( drawSurf_t ) )
    ++count[ *sortKey ];

  index[ 0 ] = 0;

  for( i = 1; i < 256; ++i )
    index[ i ] = index[ i - 1 ] + count[ i - 1 ];

  sortKey = ( (unsigned char *)&source[ 0 ].sort ) + byte;
  for( i = 0; i < size; ++i, sortKey += sizeof( drawSurf_t ) )
    dest[ index[ *sortKey ]++ ] = source[ i ];
}

/*
===============
R_RadixSort

Radix sort with 1 byte buckets, over the QSORT_BYTES low bytes of the key
(an even number, so the result ends up in source)
===============
*/
static void R_RadixSort( drawSurf_t *source, int size )
{
  static drawSurf_t scratch[ MAX_DRAWSURFS ];
  int i;

  for ( i = 0; i < QSORT_BYTES; i += 2 ) {
#ifdef Q3_LITTLE_ENDIAN
    R_Radix( i, size, source, scratch );
    R_Radix( i + 1, size, scratch, source );
#else
    R_Radix( 7 - i, size, source, scratch );
    R_Radix( 6 - i, size, scratch, source );
#endif //Q3_LITTLE_ENDIAN
  }
}

//==========================================================================================

/*
=================
R_AddDrawSurf
=================
*/
void R_AddDrawSurf( surfaceType_t *surface, shader_t *shader,
				   int fogIndex, int dlightMap ) {
	int			index;

	if (tr.refdef.rdflags & RDF_NOFOG)
	{
		fogIndex = 0;
	}

	if ( (shader->surfaceFlags & SURF_FORCESIGHT) && !(tr.refdef.rdflags & RDF_ForceSightOn) )
	{	//if shader is only seen with ForceSight and we don't have ForceSight on, then don't draw
		return;
	}

	// instead of checking for overflow, we just mask the index
	// so it wraps around
	index = tr.refdef.numDrawSurfs & DRAWSURF_MASK;
	// the sort data is packed into a single 64 bit value so it can be
	// compared quickly during the qsorting process
	tr.refdef.drawSurfs[index].sort = ((uint64_t)shader->sortedIndex << QSORT_SHADERNUM_SHIFT)
		| tr.shiftedEntityNum | ( fogIndex << QSORT_FOGNUM_SHIFT ) | (int)dlightMap;
	tr.refdef.drawSurfs[index].surface = surface;
	tr.refdef.drawSurfs[index].dlightBits = 0;
	tr.refdef.numDrawSurfs++;
}

/*
=================
R_AddModelDrawSurf

VisionJK: a surface of a model (MD3, Ghoul2) with the moving lights that reach its entity (R_ModelDlights): it gets
their light passes as the level does (vk_shade.cpp VK_DynamicLights), lit by pixel with its normals, not only by the
one direction of the entity's light.
=================
*/
void R_AddModelDrawSurf( surfaceType_t *surface, shader_t *shader, int fogIndex, const trRefEntity_t *ent ) {
	const int before = tr.refdef.numDrawSurfs;

	R_AddDrawSurf( surface, shader, fogIndex, qfalse );
	if ( tr.refdef.numDrawSurfs != before ) {
		tr.refdef.drawSurfs[before & DRAWSURF_MASK].dlightBits = ent->dlightBits;
	}
}

/*
=================
R_DecomposeSort
=================
*/
void R_DecomposeSort( uint64_t sort, int *entityNum, shader_t **shader,
					 int *fogNum, int *dlightMap ) {
	*fogNum = (int)( sort >> QSORT_FOGNUM_SHIFT ) & 31;
	*shader = tr.sortedShaders[ (int)( sort >> QSORT_SHADERNUM_SHIFT ) & (MAX_SHADERS-1) ];
	*entityNum = (int)( sort >> QSORT_REFENTITYNUM_SHIFT ) & REFENTITYNUM_MASK;
	*dlightMap = (int)sort & 3;
}

/*
=================
R_SortDrawSurfs
=================
*/
/*
=================
R_SortGlass

VisionJK: the surfaces whose material is glass (VK_GlassDraw) shows the scene drawn before them and are drawn
without blending, so they go far to near, together: the glass behind glass shows through the nearer. Only they move;
the order of everything else stays as it was.

Glass writes its depth, so of the other transparent things (the sprites of shots, sparks, smoke) those drawn after a
pane are not drawn behind it, and those drawn before it are in its picture, also the ones in front of it, which it
then bends where it should not. Nothing sorts them by pixel, so the place of the glass picks the smaller fault:
- A pane without a thickness (a window, the material keyword at 0) goes to the end of the list, after everything else
  transparent: what lies behind it shows (their word 2026-11-26, "wieso sehe ich keine lichtschwerter und co hinter dem
  glas"), and it moves nothing to the side, so what lies in front is as it was.
- Glass with a thickness (a block, water; any above 0, their word 2026-11-26: a little of it doubled the player as
  well) stays where the first glass was in the order of the shaders, before the sprites: it
  moves what it shows by many pixels, so what lies in front must be drawn over it untouched; what is transparent and
  lies behind it is not seen.
Without v_pbr a pane is blended as its shader says and all of them stay where they were.
=================
*/
#define MAX_SORTED_GLASS	1024
#define GLASS_THIN			0.0f	// units of thickness up to which glass is a pane (the material keyword, 0: a pane)

extern cvar_t	*v_pbr;		// vk_shade.cpp

static qboolean R_IsGlass( const shader_t *shader ) {
	const shader_t *state = shader->remappedShader ? shader->remappedShader : shader;

	for ( int i = 0; i < state->numUnfoggedPasses; i++ ) {
		if ( state->stages[i].active && state->stages[i].mat && state->stages[i].mat->transmission > 0.0f ) {
			return qtrue;
		}
	}
	return qfalse;
}

// where a surface is, for its distance from the view
static void R_DrawSurfCenter( const drawSurf_t *drawSurf, int entityNum, vec3_t center ) {
	const surfaceType_t *surface = drawSurf->surface;

	VectorClear( center );
	if ( entityNum != REFENTITYNUM_WORLD ) {
		VectorCopy( tr.refdef.entities[entityNum].e.origin, center );
		return;
	}
	switch ( *surface ) {
	case SF_FACE: {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)surface;

		for ( int i = 0; i < face->numPoints; i++ ) {
			VectorAdd( center, face->points[i], center );
		}
		if ( face->numPoints ) {
			VectorScale( center, 1.0f / face->numPoints, center );
		}
		break;
	}
	case SF_GRID: {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)surface;

		VectorAdd( grid->meshBounds[0], grid->meshBounds[1], center );
		VectorScale( center, 0.5f, center );
		break;
	}
	case SF_TRIANGLES: {
		const srfTriangles_t *tris = (const srfTriangles_t *)surface;

		VectorAdd( tris->bounds[0], tris->bounds[1], center );
		VectorScale( center, 0.5f, center );
		break;
	}
	default:
		break;
	}
}

// all its glass is a thin pane
qboolean R_IsThinGlass( const shader_t *shader ) {
	const shader_t *state = shader->remappedShader ? shader->remappedShader : shader;

	for ( int i = 0; i < state->numUnfoggedPasses; i++ ) {
		if ( state->stages[i].active && state->stages[i].mat && state->stages[i].mat->transmission > 0.0f
			&& state->stages[i].mat->thickness > GLASS_THIN ) {
			return qfalse;
		}
	}
	return qtrue;
}

static void R_SortGlass( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	static drawSurf_t	glass[MAX_SORTED_GLASS];
	static float		distance[MAX_SORTED_GLASS];
	static qboolean		thin[MAX_SORTED_GLASS];
	const qboolean		drawnAsGlass = (qboolean)( v_pbr && v_pbr->integer );
	int					numGlass = 0, numThick = 0, first = -1, others, out;

	for ( int i = 0; i < numDrawSurfs && numGlass < MAX_SORTED_GLASS; i++ ) {
		shader_t *shader;
		int entityNum, fogNum, dlighted;
		vec3_t center;

		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( !R_IsGlass( shader ) ) {
			continue;
		}
		if ( first < 0 ) {
			first = i;
		}
		R_DrawSurfCenter( &drawSurfs[i], entityNum, center );
		// (insertion, far first: there are few)
		const float d = DistanceSquared( center, tr.viewParms.ori.origin );
		int at = numGlass;

		while ( at > 0 && distance[at - 1] < d ) {
			glass[at] = glass[at - 1];
			distance[at] = distance[at - 1];
			thin[at] = thin[at - 1];
			at--;
		}
		glass[at] = drawSurfs[i];
		distance[at] = d;
		thin[at] = (qboolean)( drawnAsGlass && R_IsThinGlass( shader ) );
		if ( !thin[at] ) {
			numThick++;
		}
		numGlass++;
		// taken out: marked by a surface of NULL until the list is put together again
		drawSurfs[i].surface = NULL;
	}
	if ( !numGlass ) {
		return;
	}
	// the others close up from the first glass on, then make room there for the thick glass; the thin goes in after them
	others = first;
	for ( int i = first; i < numDrawSurfs; i++ ) {
		if ( drawSurfs[i].surface ) {
			drawSurfs[others++] = drawSurfs[i];
		}
	}
	others -= first;
	memmove( &drawSurfs[first + numThick], &drawSurfs[first], sizeof( drawSurfs[0] ) * others );
	out = first;
	for ( int i = 0; i < numGlass; i++ ) {
		if ( !thin[i] ) {
			drawSurfs[out++] = glass[i];
		}
	}
	out = first + numThick + others;
	for ( int i = 0; i < numGlass; i++ ) {
		if ( thin[i] ) {
			drawSurfs[out++] = glass[i];
		}
	}
}

/*
========================
R_GlassViews

VisionJK: glass shows what lies behind it from a copy of the frame, read to the side (generic.frag GlassStage). The
copy is one flat picture: what stands in front of the glass is in it, and what that hides is not. Every way of
making that up in the shader showed (a second player, patches, a squeezed picture round his outline), and drawing
the models in front after the glass took their shadows and the smoke in front of them (2026-11-26 and 27). Other
engines either live with that fringe (Unity, Godot, Unreal say so) or draw what lies behind the surface once more,
cut at it, into a picture of its own (Source: the refraction of its water and of its glass). That is done here, as
the puddles do it (R_PuddleView), for the part of the view where it is needed alone:

For a plane of thick, flat glass of the level that has a model in front of one of its panes (the pane goes through
it or close by it, or lies beyond it as the eye looks), the level is drawn once more with the same eye, cut at the
plane (the portal plane), for the part of the view those models cover. The glass reads that picture wherever its copy of the frame shows something in front of it. Up to
GLASS_VIEWS planes a view, those first whose glass the models stand nearest to; a plane close behind one that has a
view takes that one's (the two sides of one pane).
========================
*/
#define GLASS_VIEW_PLANES	32
#define GLASS_VIEW_FACES	128
#define GLASS_VIEW_REACH	0.2f	// how far glass reads to the side, of half the height of the view (GLASS_MOST of generic.frag and the blur of frosted glass)
#define GLASS_VIEW_RADIUS	48.0f	// of a model, round its origin: what it covers of the view
#define GLASS_VIEW_FRONT	24.0f	//	and how far its origin may lie behind a plane with a part of it still in front
#define GLASS_VIEW_SAME		20.0f	// planes that lie no further apart share a view

typedef struct {
	vec4_t		plane;		// xyz normal away from the eye, w distance
	float		eye;		// how far the eye is from it
	float		part[4];	// what the models in front of its glass cover of the view, with what the glass reads to the side
	float		score;		// how much its glass has to show of what the models hide
	qboolean	wanted;
} glassPlane_t;

static void R_GrowBounds( float *bounds, const float *add ) {
	bounds[0] = Q_min( bounds[0], add[0] );
	bounds[1] = Q_min( bounds[1], add[1] );
	bounds[2] = Q_max( bounds[2], add[2] );
	bounds[3] = Q_max( bounds[3], add[3] );
}

// one pane of that glass: its box, and what it covers of the view
typedef struct {
	int		plane;
	vec3_t	mins, maxs;
	float	bounds[4];
} glassFace_t;

static qboolean R_InGlassFace( const glassFace_t *face, const vec3_t point, float margin ) {
	for ( int k = 0; k < 3; k++ ) {
		if ( point[k] < face->mins[k] - margin || point[k] > face->maxs[k] + margin ) {
			return qfalse;
		}
	}
	return qtrue;
}

static void R_GlassViews( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	static glassPlane_t	planes[GLASS_VIEW_PLANES];
	static glassFace_t	faces[GLASS_VIEW_FACES];
	const float			*eye = tr.viewParms.ori.origin;
	const float			tanX = tan( tr.viewParms.fovX / 180 * M_PI * 0.5f ), tanY = tan( tr.viewParms.fovY / 180 * M_PI * 0.5f );
	const float			reachX = GLASS_VIEW_REACH * tr.viewParms.viewportHeight / Q_max( tr.viewParms.viewportWidth, 1 );
	int					numPlanes = 0, numFaces = 0, numViews = 0;
	int					chosen[GLASS_VIEWS];
	viewParms_t			oldParms;

	tr.viewParms.numGlassViews = 0;
	if ( tr.viewParms.isPortal || tr.viewParms.probeFace || ( tr.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) )
		|| r_noportals->integer || !VK_GlassViews() ) {
		return;
	}
	// the panes of the thick glass of the level and their planes: the list has what is transparent at its end
	for ( int i = numDrawSurfs - 1; i >= 0 && numFaces < GLASS_VIEW_FACES; i-- ) {
		shader_t	*shader;
		int			entityNum, fogNum, dlighted;

		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( shader->sort <= SS_OPAQUE ) {
			break;
		}
		if ( entityNum != REFENTITYNUM_WORLD || *drawSurfs[i].surface != SF_FACE || !R_IsGlass( shader ) || R_IsThinGlass( shader ) ) {
			continue;
		}
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)drawSurfs[i].surface;

		if ( face->numPoints < 3 ) {
			continue;
		}
		const float side = DotProduct( eye, face->plane.normal ) - face->plane.dist;

		if ( fabsf( side ) < 1.0f ) {
			continue;
		}
		const float	turn = side > 0.0f ? -1.0f : 1.0f;
		vec4_t		plane;
		int			found = -1;

		VectorScale( face->plane.normal, turn, plane );
		plane[3] = face->plane.dist * turn;
		for ( int k = 0; k < numPlanes; k++ ) {
			if ( DotProduct( plane, planes[k].plane ) > 0.999f && fabsf( plane[3] - planes[k].plane[3] ) < 1.0f ) {
				found = k;
				break;
			}
		}
		if ( found < 0 ) {
			if ( numPlanes == GLASS_VIEW_PLANES ) {
				continue;
			}
			found = numPlanes++;
			VectorCopy4( plane, planes[found].plane );
			planes[found].eye = fabsf( side );
			planes[found].score = 0.0f;
			planes[found].wanted = qfalse;
			planes[found].part[0] = planes[found].part[1] = 1.0f;
			planes[found].part[2] = planes[found].part[3] = -1.0f;
		}
		// its box, and what it covers of the view (a point beside or behind the eye: as far to its side as the view goes)
		glassFace_t *pane = &faces[numFaces++];

		pane->plane = found;
		ClearBounds( pane->mins, pane->maxs );
		pane->bounds[0] = pane->bounds[1] = 1.0f;
		pane->bounds[2] = pane->bounds[3] = -1.0f;
		for ( int k = 0; k < face->numPoints; k++ ) {
			vec3_t	to;
			float	at[4];

			AddPointToBounds( face->points[k], pane->mins, pane->maxs );
			VectorSubtract( face->points[k], eye, to );
			const float ahead = Q_max( DotProduct( to, tr.viewParms.ori.axis[0] ), 4.0f );

			at[0] = at[2] = Com_Clamp( -1.0f, 1.0f, -DotProduct( to, tr.viewParms.ori.axis[1] ) / ( ahead * tanX ) );
			at[1] = at[3] = Com_Clamp( -1.0f, 1.0f, DotProduct( to, tr.viewParms.ori.axis[2] ) / ( ahead * tanY ) );
			R_GrowBounds( pane->bounds, at );
		}
	}
	if ( !numFaces ) {
		return;
	}
	// the models that stand in front of a pane, and what they cover of the view where its glass reads
	for ( int e = 0; e < tr.refdef.num_entities; e++ ) {
		const trRefEntity_t	*ent = &tr.refdef.entities[e];
		float				covers[4];
		vec3_t				to;

		// (not what the list draws after the glass anyway, which is in no copy of the frame, nor the own body the view
		// does not show)
		if ( ent->e.reType != RT_MODEL
			|| ( ent->e.renderfx & ( RF_DISTORTION | RF_FORCEPOST | RF_FORCE_ENT_ALPHA | RF_AFTERGLOW | RF_THIRD_PERSON ) ) ) {
			continue;
		}
		const qboolean weapon = (qboolean)( ( ent->e.renderfx & ( RF_DEPTHHACK | RF_FIRST_PERSON ) ) != 0 );

		if ( weapon ) {
			// the view weapon: in front of everything, in the lower part of the picture
			VectorSet4( covers, -1.0f, -1.0f, 1.0f, 0.3f );
		} else {
			// (a brush model, a door, is a piece of the level: its origin says nothing of where it is)
			if ( !ent->e.ghoul2 && ent->e.hModel ) {
				const model_t *model = R_GetModelByHandle( ent->e.hModel );

				if ( model && model->type == MOD_BRUSH ) {
					continue;
				}
			}
			if ( !VK_ViewBallBounds( &tr.viewParms, ent->e.origin, GLASS_VIEW_RADIUS, covers ) ) {
				continue;
			}
		}
		VectorSubtract( ent->e.origin, eye, to );
		for ( int k = 0; k < numFaces; k++ ) {
			const glassFace_t	*pane = &faces[k];
			glassPlane_t		*plane = &planes[pane->plane];
			float				part[4];
			// how far in front of the plane the eye and the model are
			const float			eyeFront = -( DotProduct( eye, plane->plane ) - plane->plane[3] );
			const float			front = weapon ? eyeFront : -( DotProduct( ent->e.origin, plane->plane ) - plane->plane[3] );

			if ( !weapon ) {
				qboolean before = qfalse;

				if ( front <= -GLASS_VIEW_FRONT ) {
					continue;	// behind the plane
				}
				// the pane goes through the model or runs close by it
				if ( front < GLASS_VIEW_RADIUS ) {
					vec3_t on;

					VectorMA( ent->e.origin, front, plane->plane, on );
					before = R_InGlassFace( pane, on, GLASS_VIEW_RADIUS );
				}
				// or the line from the eye through the model meets it beyond the model (what the glass reads to the
				// side, a part of the view, counts with the pane)
				if ( !before && front > 0.0f && eyeFront - front > 1e-3f ) {
					const float	t = eyeFront / ( eyeFront - front );
					vec3_t		at;

					VectorMA( eye, t, to, at );
					before = R_InGlassFace( pane, at, GLASS_VIEW_RADIUS + 0.15f * t * VectorLength( to ) );
				}
				if ( !before ) {
					continue;
				}
			}
			part[0] = Q_max( covers[0], pane->bounds[0] - reachX );
			part[1] = Q_max( covers[1], pane->bounds[1] - GLASS_VIEW_REACH );
			part[2] = Q_min( covers[2], pane->bounds[2] + reachX );
			part[3] = Q_min( covers[3], pane->bounds[3] + GLASS_VIEW_REACH );
			if ( part[0] >= part[2] || part[1] >= part[3] ) {
				continue;
			}
			R_GrowBounds( plane->part, part );
			plane->wanted = qtrue;
			// (the nearer the model stands to the glass the more that glass shows of what it hides)
			plane->score += weapon ? 0.01f : 1.0f / ( 16.0f + Q_max( front, 0.0f ) );
		}
	}
	// the planes that have the most to show, of those that want one
	while ( numViews < GLASS_VIEWS ) {
		int best = -1;

		for ( int k = 0; k < numPlanes; k++ ) {
			if ( planes[k].wanted && ( best < 0 || planes[k].score > planes[best].score ) ) {
				best = k;
			}
		}
		if ( best < 0 ) {
			break;
		}
		// (of the two sides of one pane the one nearer to the eye: the other reads its picture)
		for ( int k = 0; k < numPlanes; k++ ) {
			const float apart = planes[k].plane[3] - planes[best].plane[3];

			if ( planes[k].wanted && DotProduct( planes[k].plane, planes[best].plane ) > 0.99f && apart < 0.0f && apart > -GLASS_VIEW_SAME ) {
				best = k;
			}
		}
		planes[best].wanted = qfalse;
		chosen[numViews++] = best;
		// (what lies close behind it reads its picture, and adds what it needs of the view to it)
		for ( int k = 0; k < numPlanes; k++ ) {
			const float apart = planes[k].plane[3] - planes[best].plane[3];

			if ( planes[k].wanted && DotProduct( planes[k].plane, planes[best].plane ) > 0.99f && apart >= 0.0f && apart < GLASS_VIEW_SAME ) {
				planes[k].wanted = qfalse;
				R_GrowBounds( planes[best].part, planes[k].part );
			}
		}
	}
	// v_glassDebug 3: what was found, once a second
	if ( VK_GlassDebug() == 3 ) {
		static int printed;

		if ( tr.refdef.time < printed || tr.refdef.time > printed + 1000 ) {
			printed = tr.refdef.time;
			for ( int k = 0; k < numPlanes; k++ ) {
				int view = -1;

				for ( int i = 0; i < numViews; i++ ) {
					if ( chosen[i] == k ) {
						view = i;
					}
				}
				ri.Printf( PRINT_ALL, "glass plane %d: normal %.2f %.2f %.2f dist %.0f, eye %.0f away, score %.3f, view %d\n",
					k, planes[k].plane[0], planes[k].plane[1], planes[k].plane[2], planes[k].plane[3], planes[k].eye,
					planes[k].score, view );
			}
		}
	}
	if ( !numViews ) {
		return;
	}
	oldParms = tr.viewParms;
	for ( int i = 0; i < numViews; i++ ) {
		const glassPlane_t	*plane = &planes[chosen[i]];
		viewParms_t			newParms = oldParms;

		newParms.isPortal = qtrue;
		newParms.glassView = i + 1;
		// (a little behind the plane: the glass in it is no part of what lies behind it)
		VectorCopy( plane->plane, newParms.portalPlane.normal );
		newParms.portalPlane.dist = plane->plane[3] + 0.5f;
		for ( int k = 0; k < 4; k++ ) {
			newParms.glassBounds[k] = plane->part[k];
		}
		VectorCopy4( plane->plane, oldParms.glassPlanes[i] );
		for ( int k = 0; k < 4; k++ ) {
			oldParms.glassParts[i][k] = plane->part[k];
		}
		R_RenderView( &newParms );
	}
	oldParms.numGlassViews = numViews;
	tr.viewParms = oldParms;
}

void R_SortDrawSurfs( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t		*shader;
	int				fogNum;
	int				entityNum;
	int				dlighted;
	int				i;

	// it is possible for some views to not have any surfaces
	if ( numDrawSurfs < 1 ) {
		// we still need to add it for hyperspace cases
		R_AddDrawSurfCmd( drawSurfs, numDrawSurfs );
		return;
	}

	// if we overflowed MAX_DRAWSURFS, the drawsurfs
	// wrapped around in the buffer and we will be missing
	// the first surfaces, not the last ones
	if ( numDrawSurfs > MAX_DRAWSURFS ) {
		numDrawSurfs = MAX_DRAWSURFS;
	}

	// sort the drawsurfs by sort type, then orientation, then shader
	R_RadixSort( drawSurfs, numDrawSurfs );
	R_SortGlass( drawSurfs, numDrawSurfs );

	R_GlassViews( drawSurfs, numDrawSurfs );
	R_PuddleView();

	// check for any pass through drawing, which
	// may cause another view to be rendered first
	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		R_DecomposeSort( (drawSurfs+i)->sort, &entityNum, &shader, &fogNum, &dlighted );

		if ( shader->sort > SS_PORTAL ) {
			break;
		}

		// no shader should ever have this sort type
		if ( shader->sort == SS_BAD ) {
			Com_Error (ERR_DROP, "Shader '%s'with sort == SS_BAD", shader->name );
		}

		// if the mirror was completely clipped away, we may need to check another surface
		if ( R_MirrorViewBySurface( (drawSurfs+i), entityNum) ) {
			// this is a debug option to see exactly what is being mirrored
			if ( r_portalOnly->integer ) {
				return;
			}
			break;		// only one mirror view at a time
		}
	}

	R_AddDrawSurfCmd( drawSurfs, numDrawSurfs );
}

/*
=============
R_AddEntitySurfaces
=============
*/
void R_AddEntitySurfaces (void) {
	trRefEntity_t	*ent;
	shader_t		*shader;

	if ( !r_drawentities->integer ) {
		return;
	}

	for ( tr.currentEntityNum = 0;
	      tr.currentEntityNum < tr.refdef.num_entities;
		  tr.currentEntityNum++ ) {
		ent = tr.currentEntity = &tr.refdef.entities[tr.currentEntityNum];

		assert(ent->e.renderfx >= 0);

		ent->needDlights = qfalse;

		// preshift the value we are going to OR into the drawsurf sort
		tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

		//
		// the weapon model must be handled special --
		// we don't want the hacked weapon position showing in
		// mirrors, because the true body position will already be drawn
		//
		if ( (ent->e.renderfx & RF_FIRST_PERSON) && tr.viewParms.isPortal) {
			continue;
		}

		// simple generated models, like sprites and beams, are not culled
		switch ( ent->e.reType ) {
		case RT_PORTALSURFACE:
			break;		// don't draw anything
		case RT_SPRITE:
		case RT_BEAM:
		case RT_ORIENTED_QUAD:
		case RT_ELECTRICITY:
		case RT_LINE:
		case RT_ORIENTEDLINE:
		case RT_CYLINDER:
		case RT_SABER_GLOW:
			// self blood sprites, talk balloons, etc should not be drawn in the primary
			// view.  We can't just do this check for all entities, because md3
			// entities may still want to cast shadows from them
			if ( (ent->e.renderfx & RF_THIRD_PERSON) && R_VIEW_HIDES_OWN() ) {
				continue;
			}
			shader = R_GetShaderByHandle( ent->e.customShader );
			R_AddDrawSurf( &entitySurface, shader, R_SpriteFogNum( ent ), 0 );
			break;

		case RT_MODEL:
			// we must set up parts of tr.ori for model culling
			R_RotateForEntity( ent, &tr.viewParms, &tr.ori );

			tr.currentModel = R_GetModelByHandle( ent->e.hModel );
			if (!tr.currentModel) {
				R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, 0 );
			} else {
				switch ( tr.currentModel->type ) {
				case MOD_MESH:
					R_AddMD3Surfaces( ent );
					break;
				case MOD_BRUSH:
					R_AddBrushModelSurfaces( ent );
					break;
/*
Ghoul2 Insert Start
*/
				case MOD_MDXM:
  					//g2r
					if (ent->e.ghoul2)
					{
						R_AddGhoulSurfaces( ent);
					}
  					break;
				case MOD_BAD:		// null model axis
					if ( (ent->e.renderfx & RF_THIRD_PERSON) && R_VIEW_HIDES_OWN() )
					{
						break;
					}

  					if (ent->e.ghoul2 && G2API_HaveWeGhoul2Models(*((CGhoul2Info_v *)ent->e.ghoul2)))
  					{
  						R_AddGhoulSurfaces( ent);
  						break;
  					}

					R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, false );
					break;
/*
Ghoul2 Insert End
*/
				default:
					Com_Error( ERR_DROP, "R_AddEntitySurfaces: Bad modeltype" );
					break;
				}
			}
			break;

		case RT_ENT_CHAIN:
			shader = R_GetShaderByHandle( ent->e.customShader );
			R_AddDrawSurf( &entitySurface, shader, R_SpriteFogNum( ent ), false );
			break;

		default:
			Com_Error( ERR_DROP, "R_AddEntitySurfaces: Bad reType" );
		}
	}

}


/*
====================
R_GenerateDrawSurfs
====================
*/
void R_GenerateDrawSurfs( void ) {
	R_AddWorldSurfaces ();

	R_AddPolygonSurfaces();

	// set the projection matrix with the minimum zfar
	// now that we have the world bounded
	// this needs to be done before entities are
	// added, because they use the projection
	// matrix for lod calculation
	R_SetupProjection ();

	R_AddEntitySurfaces ();
}

/*
================
R_DebugPolygon
================
*/
void R_DebugPolygon( int color, int numPoints, float *points ) {
	int		i;

	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

	// draw solid shade

	qglColor3f( color&1, (color>>1)&1, (color>>2)&1 );
	qglBegin( GL_POLYGON );
	for ( i = 0 ; i < numPoints ; i++ ) {
		qglVertex3fv( points + i * 3 );
	}
	qglEnd();

	// draw wireframe outline
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	qglDepthRange( 0, 0 );
	qglColor3f( 1, 1, 1 );
	qglBegin( GL_POLYGON );
	for ( i = 0 ; i < numPoints ; i++ ) {
		qglVertex3fv( points + i * 3 );
	}
	qglEnd();
	qglDepthRange( 0, 1 );
}

/*
====================
R_DebugGraphics

Visualization aid for movement clipping debugging
====================
*/
void R_DebugGraphics( void ) {
	if ( !r_debugSurface->integer ) {
		return;
	}

	// the render thread can't make callbacks to the main thread
	R_IssuePendingRenderCommands();

	GL_Bind( tr.whiteImage);
	GL_Cull( CT_FRONT_SIDED );
	ri.CM_DrawDebugSurface( R_DebugPolygon );
}


/*
================
R_RenderView

A view may be either the actual camera view,
or a mirror / remote location
================
*/
void R_RenderView (viewParms_t *parms) {
	int		firstDrawSurf;

	if ( parms->viewportWidth <= 0 || parms->viewportHeight <= 0 ) {
		return;
	}

	tr.viewCount++;

	tr.viewParms = *parms;
	tr.viewParms.frameSceneNum = tr.frameSceneNum;
	tr.viewParms.frameCount = tr.frameCount;

	firstDrawSurf = tr.refdef.numDrawSurfs;

	tr.viewCount++;

	// set viewParms.world
	R_RotateForViewer ();

	R_SetupFrustum ();

	R_GenerateDrawSurfs();

	R_SortDrawSurfs( tr.refdef.drawSurfs + firstDrawSurf, tr.refdef.numDrawSurfs - firstDrawSurf );

	// draw main system development information (surface outlines, etc)
	R_DebugGraphics();
}
