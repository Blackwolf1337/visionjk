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

// cg_portal_draw.c -- the portals of the portal gun on the screen of this client (cg_portal_draw.h). The renderer draws
// an opening with the view through it (ext.R_AddPortal, rd-vulkan-vision vk_portal.cpp); a stock renderer or engine has
// none of that, and the client sees the stock effect the server plays at each portal for the legacy clients instead.
// What stands in an opening, half through, is drawn on both sides: the part in front of the portal at its place, the
// part that went in coming out of the partner (ext.R_AddCutEntity, as the halves of a cut body of cg_slice.c).

#include "cgame/cg_local.h"
#include "cgame/fx_local.h"
#include "cg_portal_draw.h"

qboolean CG_CalcMuzzlePoint( int entityNum, vec3_t muzzle );	// cg_weapons.c

#define PORTAL_MARK_NEAR	12.0f	// an effect this close to the middle of a portal is its mark for the legacy clients
#define PORTAL_IN_OPENING	4.0f	// a box may reach this far past the side of an opening and still be drawn through it

static const vec3_t	portalColors[2] = { { 0.2f, 0.5f, 1.0f }, { 1.0f, 0.55f, 0.1f } };	// PORTAL_BLUE, PORTAL_ORANGE

static bgPortal_t	cg_portalList[MAX_PORTALS];
static int			cg_numPortals;
static int			cg_portalListTime = -1;

qboolean CG_PortalsDrawn( void ) {
	static int has = -1;

	if ( has < 0 ) {
		char value[8];

		// (the cvar first: an older engine ends the table of ext before R_AddPortal)
		trap->Cvar_VariableStringBuffer( "v_cgamePortals", value, sizeof( value ) );
		has = ( atoi( value ) && trap->ext.R_AddPortal ) ? 1 : 0;
	}
	return (qboolean)( has > 0 );
}

const bgPortal_t *CG_PortalList( int *count ) {
	if ( !cg.snap ) {
		*count = 0;
		return cg_portalList;
	}
	if ( cg.snap->serverTime != cg_portalListTime ) {
		cg_portalListTime = cg.snap->serverTime;
		cg_numPortals = CG_Portals( cg_portalList, MAX_PORTALS );
	}
	*count = cg_numPortals;
	return cg_portalList;
}

static const float *CG_PortalColor( const bgPortal_t *p ) {
	return portalColors[p->color == PORTAL_ORANGE ? 1 : 0];
}

void CG_PortalsScene( void ) {
	const bgPortal_t	*list;
	int					count;

	if ( !CG_PortalsDrawn() ) {
		return;
	}
	list = CG_PortalList( &count );
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		const bgPortal_t	*partner = BG_PortalPartner( list, count, p );
		const float			*color = CG_PortalColor( p );
		portalView_t		view;

		memset( &view, 0, sizeof( view ) );
		VectorCopy( p->origin, view.origin );
		VectorCopy( p->axis[0], view.axis[0] );
		VectorCopy( p->axis[1], view.axis[1] );
		VectorCopy( p->axis[2], view.axis[2] );
		if ( partner ) {
			VectorCopy( partner->origin, view.exitOrigin );
			VectorCopy( partner->axis[0], view.exitAxis[0] );
			VectorCopy( partner->axis[1], view.exitAxis[1] );
			VectorCopy( partner->axis[2], view.exitAxis[2] );
			view.open = 1;
		}
		view.halfWidth = PORTAL_HALF_WIDTH;
		view.halfHeight = PORTAL_HALF_HEIGHT;
		view.color[0] = (byte)( color[0] * 255.0f );
		view.color[1] = (byte)( color[1] * 255.0f );
		view.color[2] = (byte)( color[2] * 255.0f );
		view.color[3] = 255;
		view.id = p->entityNum;
		trap->ext.R_AddPortal( &view );
	}
}

/*
===============
CG_PortalEntityBegin

An entity whose box reaches through the face of an open portal, inside its opening, is drawn on both sides of it. All
the drawing of an entity goes through trap->R_AddRefEntityToScene, from many places (the body, the weapon, the saber
model, the shells of the force powers), so while it is drawn that one call is turned into CG_PortalAddTwice and given
back afterwards (CG_PortalEntityEnd): the table is the engine's, and nothing but this module calls through it. What
the renderer or the effects system add for the entity themselves (saber blades, lights, effects) stays where it is.
===============
*/
static void	( *cg_portalAdd )( const refEntity_t *re );	// the call of the engine while it is turned
static const bgPortal_t	*cg_portalIn, *cg_portalOut;
// (an entity drawn while another one is, a vehicle under its pilot: it goes with the one it is drawn in, and the call is
// given back at the end of the one it was turned for)
static int				cg_portalDepth, cg_portalTurnedAt;

static void CG_PortalAddTwice( const refEntity_t *re ) {
	const bgPortal_t	*in = cg_portalIn, *out = cg_portalOut;
	refEntity_t			copy;
	vec3_t				axis[3], turned[3];
	vec4_t				plane;

	// (the weapon in the hand of a first person view is drawn where the eye is, not in the level: through no portal)
	if ( re->reType == RT_PORTALSURFACE || ( re->renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK ) ) ) {
		cg_portalAdd( re );
		return;
	}
	// at its place, what lies in front of the face of the portal it stands in
	VectorCopy( in->axis[0], plane );
	plane[3] = DotProduct( in->origin, in->axis[0] );
	trap->ext.R_AddCutEntity( re, plane );

	// and moved through to the partner, what lies in front of its face (the part that went in). A ghoul2 model is
	// placed by its angles, the others by the axis; both go through.
	copy = *re;
	BG_PortalPoint( in, out, re->origin, copy.origin );
	BG_PortalPoint( in, out, re->oldorigin, copy.oldorigin );
	BG_PortalPoint( in, out, re->lightingOrigin, copy.lightingOrigin );
	for ( int k = 0; k < 3; k++ ) {
		BG_PortalVector( in, out, re->axis[k], copy.axis[k] );
	}
	AnglesToAxis( re->angles, axis );
	for ( int k = 0; k < 3; k++ ) {
		BG_PortalVector( in, out, axis[k], turned[k] );
	}
	BG_PortalAngles( (const vec3_t *)turned, copy.angles );
	// (cut that much behind the face of the partner: the opening lies over what stands within PORTAL_FACE_RELIEF of the
	// face of in, and there it shows this part of the copy, rd-vulkan-vision vk_shade.cpp keeps it in the view through;
	// in front of the partner that part is behind its face, hidden by its wall or its opening)
	VectorCopy( out->axis[0], plane );
	plane[3] = DotProduct( out->origin, out->axis[0] ) - PORTAL_FACE_RELIEF;
	trap->ext.R_AddCutEntity( &copy, plane );
}

// the box of an entity round its origin: what the server says of it (s.solid), else a guess by its type
static void CG_PortalEntityBox( const centity_t *cent, vec3_t mins, vec3_t maxs ) {
	const entityState_t *s = &cent->currentState;

	if ( s->eType == ET_PLAYER ) {
		VectorSet( mins, -15.0f, -15.0f, DEFAULT_MINS_2 );
		VectorSet( maxs, 15.0f, 15.0f, DEFAULT_MAXS_2 );
	} else if ( s->solid && s->solid != SOLID_BMODEL ) {
		const int x = s->solid & 255, zd = ( s->solid >> 8 ) & 255, zu = ( ( s->solid >> 16 ) & 255 ) - 32;

		VectorSet( mins, (float)-x, (float)-x, (float)-zd );
		VectorSet( maxs, (float)x, (float)x, (float)zu );
	} else {
		// (a missile, a small thing: its model round its origin)
		VectorSet( mins, -8.0f, -8.0f, -8.0f );
		VectorSet( maxs, 8.0f, 8.0f, 8.0f );
	}
}

void CG_PortalEntityBegin( centity_t *cent ) {
	const bgPortal_t	*list;
	int					count;
	vec3_t				mins, maxs, center, half;

	cg_portalDepth++;
	if ( cg_portalAdd || !CG_PortalsDrawn() || !trap->ext.R_AddCutEntity ) {
		return;
	}
	switch ( cent->currentState.eType ) {
	case ET_PLAYER:
	case ET_NPC:
	case ET_MISSILE:
	case ET_GENERAL:
	case ET_ITEM:
	case ET_BODY:
		break;
	default:
		return;
	}
	list = CG_PortalList( &count );
	if ( !count ) {
		return;
	}
	CG_PortalEntityBox( cent, mins, maxs );
	for ( int k = 0; k < 3; k++ ) {
		center[k] = cent->lerpOrigin[k] + ( mins[k] + maxs[k] ) * 0.5f;
		half[k] = ( maxs[k] - mins[k] ) * 0.5f;
	}
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		const bgPortal_t	*partner = BG_PortalPartner( list, count, p );

		if ( !partner ) {
			continue;
		}
		// the box reaches through the face: some of it in front, some behind, and across it lies inside the opening
		const float side = BG_PortalSide( p, center );
		const float reach = fabsf( p->axis[0][0] ) * half[0] + fabsf( p->axis[0][1] ) * half[1] + fabsf( p->axis[0][2] ) * half[2];

		// (or comes that close to it: the opening covers what stands within PORTAL_FACE_RELIEF of its face, and there the
		// picture shows the copy)
		if ( side - reach >= PORTAL_FACE_RELIEF || side + reach <= 0.0f || !BG_PortalBoxFits( p, cent->lerpOrigin, mins, maxs, -PORTAL_IN_OPENING ) ) {
			continue;
		}
		cg_portalIn = p;
		cg_portalOut = partner;
		cg_portalAdd = trap->R_AddRefEntityToScene;
		cg_portalTurnedAt = cg_portalDepth;
		trap->R_AddRefEntityToScene = CG_PortalAddTwice;
		return;
	}
}

void CG_PortalEntityEnd( void ) {
	if ( cg_portalAdd && cg_portalDepth == cg_portalTurnedAt ) {
		trap->R_AddRefEntityToScene = cg_portalAdd;
		cg_portalAdd = NULL;
	}
	if ( cg_portalDepth > 0 ) {
		cg_portalDepth--;
	}
}

qboolean CG_PortalMarker( const vec3_t origin ) {
	const bgPortal_t	*list;
	int					count;

	if ( !CG_PortalsDrawn() ) {
		return qfalse;
	}
	list = CG_PortalList( &count );
	for ( int i = 0; i < count; i++ ) {
		if ( DistanceSquared( origin, list[i].origin ) < Square( PORTAL_MARK_NEAR ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
===============
CG_PortalShotBeam

The server sets a portal where the shot of the portal gun hits (WP_BRYAR_OLD while vs_portalGun is on) and sends the
shot as a disruptor shot (game/portal/g_portal.c), which a legacy client draws as one; s.modelindex PORTAL_MARK says
it is a shot of the portal gun, s.generic1 its colour. This client draws it as a beam of that colour, from start (the
muzzle, cg_event.c) to end.
===============
*/
void CG_PortalShotBeam( vec3_t start, vec3_t end, int color ) {
	const qhandle_t	shader = trap->R_RegisterShader( "gfx/misc/whiteline2" );
	vec3_t			white = { 1.0f, 1.0f, 1.0f }, tint;

	VectorCopy( portalColors[color == PORTAL_ORANGE ? 1 : 0], tint );
	// a glow of its colour round a thin white core, both gone in a moment
	trap->FX_AddLine( start, end, 0.1f, 9.0f, 0.0f, 0.9f, 0.0f, 0.0f, tint, tint, 0.0f, 220, shader, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	trap->FX_AddLine( start, end, 0.1f, 2.5f, 0.0f, 1.0f, 0.0f, 0.0f, white, tint, 0.0f, 160, shader, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
}
