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
g_portal.c -- the portal gun in the game module (game/portal/bg_portal.h). Not from JA+.

With vs_portalGun 1 the old bryar pistol (WP_BRYAR_OLD) of a player an admin gave the gun (amportalgun) shoots
portals: the primary fire the blue one, the alt fire the orange one (on release, the alt fire of that pistol
charges). A shot is a ray from the eyes. Where it hits a flat piece of the world that is no sky, no SURF_NOIMPACT
and no mover, a portal of 48 by 80 is put, moved by up to 16 units where it sticks out over an edge or into another
portal, and on a wall set down onto the floor right below it so a player walks in without a step. Otherwise the
shot misses with a spark. A player has one pair; a portal stays until he shoots its colour again, and both go when
he leaves, changes team, loses the gun or vs_portalGun goes to 0, and with the level.

The portals are entities of the type ET_INVISIBLE (bg_portal.h says which fields mean what), so a stock cgame draws
nothing for them. What every client sees of them, a legacy one included: the beam of a disruptor for a portal shot
(EV_DISRUPTOR_MAIN_SHOT; s.weapon WP_BRYAR_OLD, s.modelindex PORTAL_MARK and s.generic1 the colour mark it for the
cgame of this tree), and every PORTAL_MARK_TIME a stock effect at each portal (EV_PLAY_EFFECT_ID, portalFxNames).

Who goes through: a player whose cgame predicts it on a server whose engine cuts holes into the traces (G_PortalMoves)
walks in by himself, bg_pmove.c takes him over. Anybody else is put through when his box touches an opening
(G_PortalTouch), with his speed turned the way the other portal faces. Missiles and thermals (G_RunMissile), items
(G_RunItem), props (g_props.c) and the disruptor (g_weapon.c) go through as well.
*/

#include "game/g_local.h"

#define PORTAL_MARK_TIME	1500	// ms between the stock effects at the portals for the legacy clients
#define PORTAL_NUDGE		16.0f	// how far a portal that does not fit where the shot hit is moved to fit
#define PORTAL_NUDGE_STEP	4.0f
#define PORTAL_SHOT_RANGE	8192.0f
#define PORTAL_SHOT_THROUGH	2		// open portals a portal shot goes through on its way to a wall
#define PORTAL_FACE_SLACK	0.5f	// how far off the plane of the wall a spot of a flat piece may be

// The stock effects of base (every legacy client has them, the cgame registers them all): at a blue and at an
// orange portal for the legacy clients, and a portal shot that did not stick. The two marks are the shimmers of a
// spawn and of the spawn of the Jedi Master; they are played once each round, not every frame.
static const char *const portalFxNames[3] = { "mp/spawn", "mp/jedispawn", "sparks/spark_nosnd" };
#define PORTAL_FX_MISS		2

static gentity_t	*portalEnts[MAX_CLIENTS][2];	// his portals, by colour
static int			portalTeams[MAX_CLIENTS];		// the team he was in when he set them
static qboolean		portalGuns[MAX_CLIENTS];		// an admin gave him the gun (amportalgun)
static int			portalFx[3];					// G_EffectIndex of portalFxNames, 0 until first used
static int			portalMarkTime;
// the portals as bgPortal_t, made again after every change
static bgPortal_t	portalList[MAX_PORTALS];
static int			portalCount;
static qboolean		portalListDirty;

static int G_PortalFx( int which ) {
	if ( !portalFx[which] ) {
		portalFx[which] = G_EffectIndex( portalFxNames[which] );
	}
	return portalFx[which];
}

// his portal of that colour, NULL for none
static gentity_t *G_PortalOf( int clientNum, int color ) {
	gentity_t *p = portalEnts[clientNum][color];

	if ( p && ( !p->inuse || !p->classname || strcmp( p->classname, "portal" ) || p->s.otherEntityNum != clientNum ) ) {
		portalEnts[clientNum][color] = NULL;
		return NULL;
	}
	return p;
}

static const bgPortal_t *G_PortalList( int *count ) {
	if ( portalListDirty ) {
		portalCount = 0;
		for ( int i = 0; i < MAX_CLIENTS; i++ ) {
			for ( int color = 0; color < 2; color++ ) {
				const gentity_t *p = G_PortalOf( i, color );

				if ( p && portalCount < MAX_PORTALS ) {
					BG_PortalFromState( &p->s, &portalList[portalCount++] );
				}
			}
		}
		portalListDirty = qfalse;
	}
	*count = portalCount;
	return portalList;
}

int G_Portals( bgPortal_t *out, int max ) {
	int				count;
	const bgPortal_t *list = G_PortalList( &count );

	if ( count > max ) {
		count = max;
	}
	memcpy( out, list, count * sizeof( *out ) );
	return count;
}

// a point a little in front of a portal, where the leaves of the level are that see it
static void G_PortalFront( const gentity_t *p, float dist, vec3_t out ) {
	vec3_t forward;

	AngleVectors( p->s.angles, forward, NULL, NULL );
	VectorMA( p->s.origin, dist, forward, out );
}

// the two of a player know each other: partner, and what is behind the other is sent with each (SVF_PORTAL)
static void G_PortalPair( int clientNum ) {
	gentity_t *pair[2];

	pair[PORTAL_BLUE] = G_PortalOf( clientNum, PORTAL_BLUE );
	pair[PORTAL_ORANGE] = G_PortalOf( clientNum, PORTAL_ORANGE );
	for ( int color = 0; color < 2; color++ ) {
		gentity_t *self = pair[color], *other = pair[!color];

		if ( !self ) {
			continue;
		}
		if ( other ) {
			self->s.otherEntityNum2 = other->s.number;
			self->r.svFlags |= SVF_PORTAL;
			// (not on the face of the wall itself: a point there may lie in no leaf one sees anything from)
			G_PortalFront( other, 2.0f, self->s.origin2 );
		} else {
			self->s.otherEntityNum2 = ENTITYNUM_NONE;
			self->r.svFlags &= ~SVF_PORTAL;
			VectorClear( self->s.origin2 );
			self->r.broadcastClients[0] = self->r.broadcastClients[1] = 0;
		}
		trap->LinkEntity( (sharedEntity_t *)self );
	}
	portalListDirty = qtrue;
}

/*
=================
G_PortalEvict

The hole of a portal closes (it goes, moves, or its partner goes). A player who walks through by himself may stand
half in it, in the wall: he is put out in front of it, he would be stuck there.
=================
*/
static void G_PortalEvict( const gentity_t *portal ) {
	bgPortal_t p;

	if ( !portal || portal->s.otherEntityNum2 == ENTITYNUM_NONE ) {
		return;
	}
	BG_PortalFromState( &portal->s, &p );
	for ( int i = 0; i < level.maxclients; i++ ) {
		gentity_t	*ent = &g_entities[i];
		float		middle, reach;

		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || !G_PortalMoves( ent ) ) {
			continue;
		}
		BG_PortalBoxAlong( &p, ent->client->ps.origin, ent->r.mins, ent->r.maxs, 0, &middle, &reach );
		if ( middle - reach >= 0.0f || middle + reach < -PORTAL_HOLE_DEPTH
			|| !BG_PortalBoxFits( &p, ent->client->ps.origin, ent->r.mins, ent->r.maxs, -1.0f ) ) {
			continue;
		}
		VectorMA( ent->client->ps.origin, 1.0f - ( middle - reach ), p.axis[0], ent->client->ps.origin );
		VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
}

static void G_PortalRemove( int clientNum, int color ) {
	gentity_t *p = G_PortalOf( clientNum, color );

	if ( p ) {
		// its hole closes, and that of its partner
		G_PortalEvict( p );
		G_PortalEvict( G_PortalOf( clientNum, !color ) );
		G_FreeEntity( p );
		portalEnts[clientNum][color] = NULL;
		G_PortalPair( clientNum );
	}
}

static void G_PortalRemoveBoth( int clientNum ) {
	G_PortalRemove( clientNum, PORTAL_BLUE );
	G_PortalRemove( clientNum, PORTAL_ORANGE );
}

void G_PortalInit( void ) {
	// vs_caps is read only for the admins; this module says what the engine can (gameImport_t.ext.TraceHoles is
	// NULL where the engine is older, WebAdmin_WrapImport)
	const int caps = trap->ext.TraceHoles ? ( vs_caps.integer | VS_CAPS_PORTAL ) : ( vs_caps.integer & ~VS_CAPS_PORTAL );

	memset( portalEnts, 0, sizeof( portalEnts ) );
	memset( portalTeams, 0, sizeof( portalTeams ) );
	memset( portalGuns, 0, sizeof( portalGuns ) );
	memset( portalFx, 0, sizeof( portalFx ) );
	portalMarkTime = 0;
	portalCount = 0;
	portalListDirty = qfalse;
	if ( caps != vs_caps.integer ) {
		trap->Cvar_Set( "vs_caps", va( "%i", caps ) );
		trap->Cvar_Update( &vs_caps );
	}
}

void G_PortalClientGone( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	G_PortalRemoveBoth( clientNum );
	portalGuns[clientNum] = qfalse;
}

/*
=================
G_PortalSend

The partner of a portal a client sees is sent to him even where he can not see it: his cgame needs both to know the
portal is open (the holes of his moves, the view through it). Where he sees both, both go the usual way and each
adds what can be seen from the other (SVF_PORTAL); one that is always sent would not (sv_snapshot.cpp).
=================
*/
static void G_PortalSend( int clientNum ) {
	gentity_t	*a = G_PortalOf( clientNum, PORTAL_BLUE ), *b = G_PortalOf( clientNum, PORTAL_ORANGE );
	vec3_t		frontA, frontB;

	if ( !a || !b ) {
		return;
	}
	G_PortalFront( a, 2.0f, frontA );
	G_PortalFront( b, 2.0f, frontB );
	a->r.broadcastClients[0] = a->r.broadcastClients[1] = 0;
	b->r.broadcastClients[0] = b->r.broadcastClients[1] = 0;
	for ( int i = 0; i < level.maxclients; i++ ) {
		const gentity_t	*viewer = &g_entities[i];
		vec3_t			eye;
		qboolean		seeA, seeB;

		if ( !viewer->inuse || !viewer->client || viewer->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		VectorCopy( viewer->client->ps.origin, eye );
		eye[2] += viewer->client->ps.viewheight;
		seeA = trap->InPVS( eye, frontA );
		seeB = trap->InPVS( eye, frontB );
		if ( seeA && !seeB ) {
			b->r.broadcastClients[i >> 5] |= 1u << ( i & 31 );
		} else if ( seeB && !seeA ) {
			a->r.broadcastClients[i >> 5] |= 1u << ( i & 31 );
		}
	}
}

void G_PortalFrame( void ) {
	const qboolean mark = (qboolean)( level.time >= portalMarkTime );

	if ( mark ) {
		portalMarkTime = level.time + PORTAL_MARK_TIME;
	}
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		const gentity_t *owner = &g_entities[i];

		if ( !G_PortalOf( i, PORTAL_BLUE ) && !G_PortalOf( i, PORTAL_ORANGE ) ) {
			continue;
		}
		if ( !vs_portalGun.integer || !portalGuns[i] || !owner->inuse || !owner->client
			|| owner->client->pers.connected != CON_CONNECTED || (int)owner->client->sess.sessionTeam != portalTeams[i] ) {
			G_PortalRemoveBoth( i );
			continue;
		}
		G_PortalSend( i );
		if ( mark ) {
			for ( int color = 0; color < 2; color++ ) {
				const gentity_t	*p = G_PortalOf( i, color );
				vec3_t			at, forward;

				if ( p ) {
					AngleVectors( p->s.angles, forward, NULL, NULL );
					VectorMA( p->s.origin, 2.0f, forward, at );
					G_PlayEffectID( G_PortalFx( color ), at, forward );
				}
			}
		}
	}
}

qboolean G_PortalMoves( const gentity_t *ent ) {
	return (qboolean)( ( vs_caps.integer & VS_CAPS_PORTAL ) && ent->client && ent->s.number < MAX_CLIENTS
		&& !( ent->r.svFlags & SVF_BOT ) && ( ent->client->pers.jp.statusCaps & V_CAPS_PORTAL ) );
}

/*
===============================================================================

Going through

===============================================================================
*/

qboolean G_PortalTouched( const vec3_t origin, const vec3_t mins, const vec3_t maxs, const vec3_t velocity,
	bgPortal_t *in, bgPortal_t *out ) {
	int					count;
	const bgPortal_t	*list = G_PortalList( &count );

	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		const bgPortal_t	*partner = BG_PortalPartner( list, count, p );
		float				middle, reach;

		if ( !partner ) {
			continue;
		}
		// right in front of the face (a unit of slack behind it, the traces stop a little short or deep)
		BG_PortalBoxAlong( p, origin, mins, maxs, 0, &middle, &reach );
		if ( middle - reach < -1.0f || middle - reach > PORTAL_TOUCH ) {
			continue;
		}
		// on its way in, or right at the face (a player who walks into a wall has no speed into it left)
		if ( DotProduct( velocity, p->axis[0] ) >= 0.0f && middle - reach > 1.0f ) {
			continue;
		}
		if ( !BG_PortalBoxFits( p, origin, mins, maxs, 0.0f ) ) {
			continue;
		}
		*in = *p;
		*out = *partner;
		return qtrue;
	}
	return qfalse;
}

/*
=================
G_PortalTouch

A player who does not walk through by himself (a legacy client, a bot, a server without the holes) is put through
when his box touches an opening: as a teleporter does it (TeleportPlayer, g_misc.c), but at the speed he came with,
turned the way the other portal faces, and a little more than PORTAL_TOUCH in front of it, so he does not touch that
one at once. Where his box would be stuck there he stays.
=================
*/
void G_PortalTouch( gentity_t *ent, const vec3_t mins, const vec3_t maxs ) {
	gclient_t	*client = ent->client;
	bgPortal_t	in, out;
	vec3_t		exit, angles;
	trace_t		tr;

	if ( !client || ent->s.number >= MAX_CLIENTS || client->ps.m_iVehicleNum || client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	if ( client->ps.pm_type != PM_NORMAL && client->ps.pm_type != PM_JETPACK && client->ps.pm_type != PM_FLOAT ) {
		return;
	}
	if ( !G_PortalTouched( client->ps.origin, mins, maxs, client->ps.velocity, &in, &out ) ) {
		return;
	}
	BG_PortalExit( &in, &out, client->ps.origin, mins, maxs, PORTAL_TOUCH + 2.0f, exit );
	trap->Trace( &tr, exit, mins, maxs, exit, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid ) {
		return;
	}

	// the rope of the grappling hook does not go through
	JP_HookFree( ent );
	VectorCopy( exit, client->ps.origin );
	BG_PortalVector( &in, &out, client->ps.velocity, client->ps.velocity );
	BG_PortalViewAngles( &in, &out, client->ps.viewangles, angles );
	SetClientViewAngle( ent, angles );
	client->ps.groundEntityNum = ENTITYNUM_NONE;
	// the others do not draw him on his way from the one to the other
	client->ps.eFlags ^= EF_TELEPORT_BIT;
}

// the first open portal a move from start to end goes into before its trace tr stopped (a box of mins and maxs, NULL
// for a point), where its origin went through the face
static qboolean G_PortalOnWay( const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
	const trace_t *tr, bgPortal_t *in, bgPortal_t *out, vec3_t at ) {
	int					count;
	const bgPortal_t	*list = G_PortalList( &count );
	const float			len = Distance( start, end );
	float				best = 2.0f;

	if ( !count || tr->startsolid || len < 0.001f ) {
		return qfalse;
	}
	if ( !mins || !maxs ) {
		mins = maxs = vec3_origin;
	}
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t	*p = &list[i];
		const bgPortal_t	*partner = BG_PortalPartner( list, count, p );
		float				before, after, f, middle, reach;

		if ( !partner ) {
			continue;
		}
		before = BG_PortalSide( p, start );
		after = BG_PortalSide( p, end );
		if ( before < 0.0f || after >= 0.0f ) {
			continue;
		}
		f = before / ( before - after );
		// The face of the wall stopped the trace a little short of where the origin crosses it, by as far as the
		// box reaches out in front of its origin. Something that stopped it before that stands in the way.
		BG_PortalBoxAlong( p, start, mins, maxs, 0, &middle, &reach );
		if ( f * len > tr->fraction * len + reach + 1.0f ) {
			continue;
		}
		if ( f < best && BG_PortalCrossed( p, start, end, mins, maxs ) ) {
			best = f;
			*in = *p;
			*out = *partner;
		}
	}
	if ( best > 1.0f ) {
		return qfalse;
	}
	VectorSubtract( end, start, at );
	VectorMA( start, best, at, at );
	return qtrue;
}

qboolean G_PortalTrajectory( gentity_t *ent, const vec3_t start, vec3_t origin, trace_t *tr, int passEntityNum,
	int contentMask, int traceFlags, int useLod ) {
	qboolean	through = qfalse;
	vec3_t		from;

	VectorCopy( start, from );
	for ( int n = 0; n < PORTAL_RAY_THROUGH; n++ ) {
		bgPortal_t	in, out;
		vec3_t		at, exit, vel;
		const float	whole = Distance( from, origin );
		float		rest;

		if ( !G_PortalOnWay( from, origin, ent->r.mins, ent->r.maxs, tr, &in, &out, at ) ) {
			break;
		}
		// what is left of this frame is flown out of the other one, at the speed it went in, turned
		rest = whole > 0.001f ? Distance( at, origin ) / whole : 0.0f;
		BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, vel );
		BG_PortalVector( &in, &out, vel, vel );
		BG_PortalExit( &in, &out, at, ent->r.mins, ent->r.maxs, 1.0f, exit );

		VectorCopy( exit, ent->s.pos.trBase );
		VectorCopy( vel, ent->s.pos.trDelta );
		ent->s.pos.trTime = level.time - (int)( rest * ( level.time - level.previousTime ) );
		// the clients do not draw it on its way from the one to the other
		ent->s.eFlags ^= EF_TELEPORT_BIT;
		VectorCopy( exit, ent->r.currentOrigin );
		BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );
		// out of there it may hit who shot it (G_RunMissile asks the same from now on)
		ent->portalPassed = qtrue;
		if ( passEntityNum == ent->r.ownerNum ) {
			passEntityNum = ent->s.number;
		}
		trap->Trace( tr, exit, ent->r.mins, ent->r.maxs, origin, passEntityNum, contentMask, qfalse, traceFlags, useLod );
		VectorCopy( exit, from );
		through = qtrue;
	}
	return through;
}

qboolean G_PortalRay( const vec3_t start, const vec3_t end, const trace_t *tr, vec3_t at, vec3_t outStart, vec3_t outEnd ) {
	bgPortal_t	in, out;
	vec3_t		dir;
	float		rest;

	if ( !G_PortalOnWay( start, end, NULL, NULL, tr, &in, &out, at ) ) {
		return qfalse;
	}
	rest = Distance( at, end );
	VectorSubtract( end, start, dir );
	VectorNormalize( dir );
	BG_PortalVector( &in, &out, dir, dir );
	// a unit out of the other face, so the rest of the ray does not start in its wall
	BG_PortalPoint( &in, &out, at, outStart );
	VectorMA( outStart, 1.0f, out.axis[0], outStart );
	VectorMA( outStart, rest, dir, outEnd );
	return qtrue;
}

/*
===============================================================================

The gun

===============================================================================
*/

qboolean G_PortalGunReady( const gentity_t *ent ) {
	return (qboolean)( vs_portalGun.integer && ent->client && ent->s.number < MAX_CLIENTS && portalGuns[ent->s.number] );
}

void G_PortalGunSpawn( gentity_t *ent ) {
	if ( G_PortalGunReady( ent ) && ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		ent->client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_BRYAR_OLD );
	}
}

// whether two portals lie in one plane and overlap there (the sides of the one and of the other as the axes to part
// them by, the opening a rectangle in that plane)
static qboolean G_PortalsOverlap( const vec3_t originA, const vec3_t axisA[3], const vec3_t originB, const vec3_t axisB[3] ) {
	vec3_t d;

	VectorSubtract( originB, originA, d );
	if ( DotProduct( axisA[0], axisB[0] ) < 0.99f || fabsf( DotProduct( d, axisA[0] ) ) > 2.0f ) {
		return qfalse;
	}
	for ( int i = 0; i < 4; i++ ) {
		const float	*u = ( i < 2 ) ? axisA[1 + i] : axisB[i - 1];
		const float	ra = PORTAL_HALF_WIDTH * fabsf( DotProduct( axisA[1], u ) ) + PORTAL_HALF_HEIGHT * fabsf( DotProduct( axisA[2], u ) );
		const float	rb = PORTAL_HALF_WIDTH * fabsf( DotProduct( axisB[1], u ) ) + PORTAL_HALF_HEIGHT * fabsf( DotProduct( axisB[2], u ) );

		if ( fabsf( DotProduct( d, u ) ) >= ra + rb ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
=================
G_PortalSpotFits

The corners of the opening, the middles of its sides and its middle, each a unit inside: right behind each the face
of the world, in the same plane, no sky and nothing that takes no marks of impact; nothing solid in front of any.
And no other portal in the way (his own of the same colour is the one that moves).
=================
*/
static qboolean G_PortalSpotFits( const vec3_t center, const vec3_t axis[3], int clientNum, int color ) {
	static const float	spots[9][2] = { { 0, 0 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	int					count;
	const bgPortal_t	*list = G_PortalList( &count );

	for ( int i = 0; i < 9; i++ ) {
		vec3_t	spot, from, to, off;
		trace_t	tr;

		VectorMA( center, spots[i][0] * ( PORTAL_HALF_WIDTH - 1.0f ), axis[1], spot );
		VectorMA( spot, spots[i][1] * ( PORTAL_HALF_HEIGHT - 1.0f ), axis[2], spot );
		VectorMA( spot, 4.0f, axis[0], from );
		VectorMA( spot, -4.0f, axis[0], to );
		trap->Trace( &tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD
			|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) || DotProduct( tr.plane.normal, axis[0] ) < 0.999f ) {
			return qfalse;
		}
		VectorSubtract( tr.endpos, center, off );
		if ( fabsf( DotProduct( off, axis[0] ) ) > PORTAL_FACE_SLACK ) {
			return qfalse;
		}
	}
	for ( int i = 0; i < count; i++ ) {
		const bgPortal_t *p = &list[i];

		if ( p->owner == clientNum && p->color == color ) {
			continue;
		}
		if ( G_PortalsOverlap( center, axis, p->origin, (const vec3_t *)p->axis ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// The middle of a portal near where the shot hit that fits, nearest first, in rings of PORTAL_NUDGE_STEP up to
// PORTAL_NUDGE out. qfalse: none.
static qboolean G_PortalFit( vec3_t center, const vec3_t axis[3], int clientNum, int color ) {
	const int rings = (int)( PORTAL_NUDGE / PORTAL_NUDGE_STEP );

	for ( int ring = 0; ring <= rings; ring++ ) {
		for ( int u = -ring; u <= ring; u++ ) {
			for ( int v = -ring; v <= ring; v++ ) {
				vec3_t at;

				if ( abs( u ) != ring && abs( v ) != ring ) {
					continue;
				}
				VectorMA( center, u * PORTAL_NUDGE_STEP, axis[1], at );
				VectorMA( at, v * PORTAL_NUDGE_STEP, axis[2], at );
				if ( G_PortalSpotFits( at, axis, clientNum, color ) ) {
					VectorCopy( at, center );
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

/*
=================
G_PortalOnFloor

A portal on a wall with a floor right below its lower edge (up to PORTAL_NUDGE) goes down onto it, to the exact plane
of the floor. A player standing there fits through the opening only with no more than 8 units to spare above and
below (64 of 80), so an edge a little over the floor would be a step he could not take into it.
=================
*/
static void G_PortalOnFloor( vec3_t center, const vec3_t axis[3], int clientNum, int color ) {
	vec3_t	from, to, at;
	trace_t	tr;
	float	down, along;

	if ( fabsf( axis[0][2] ) >= 0.7f ) {
		return;
	}
	VectorMA( center, 2.0f, axis[0], from );
	VectorMA( from, -( PORTAL_HALF_HEIGHT + PORTAL_NUDGE ), axis[2], to );
	trap->Trace( &tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD ) {
		return;
	}
	along = DotProduct( axis[2], tr.plane.normal );
	if ( along < 0.7f ) {
		return;
	}
	// how far down from the middle the plane of the floor is, exactly (the trace stops a little short of it)
	down = ( DotProduct( from, tr.plane.normal ) - tr.plane.dist ) / along;
	// (a shot low on the wall lifts the opening onto the floor as well: its middle stood below the height of a man)
	if ( down - PORTAL_HALF_HEIGHT > PORTAL_NUDGE ) {
		return;
	}
	VectorMA( center, PORTAL_HALF_HEIGHT - down, axis[2], at );
	if ( G_PortalSpotFits( at, axis, clientNum, color ) ) {
		VectorCopy( at, center );
	}
}

// the shot did not stick: a spark where it hit
static void G_PortalMiss( const vec3_t at, const vec3_t normal ) {
	G_PlayEffectID( G_PortalFx( PORTAL_FX_MISS ), (float *)at, (float *)normal );
}

// the beam of a portal shot, a disruptor beam for any cgame, marked for the cgame of this tree
static void G_PortalBeam( const vec3_t from, const vec3_t to, int shooter, int color ) {
	gentity_t *tent = G_TempEntity( (float *)to, EV_DISRUPTOR_MAIN_SHOT );

	VectorCopy( from, tent->s.origin2 );
	// the shooter for the first piece: the cgame starts the beam at his muzzle; a piece out of a portal starts where
	// it starts (any number no client has)
	tent->s.eventParm = shooter;
	tent->s.weapon = WP_BRYAR_OLD;
	tent->s.modelindex = PORTAL_MARK;
	tent->s.generic1 = color;
}

// puts his portal of that colour there, the old one of that colour goes
static void G_PortalPlace( gentity_t *ent, int color, const vec3_t center, const vec3_t axis[3] ) {
	const int	clientNum = ent->s.number;
	gentity_t	*p = G_PortalOf( clientNum, color );
	vec3_t		angles;

	if ( !p ) {
		p = G_Spawn();
		if ( !p ) {
			return;
		}
		p->classname = "portal";
		portalEnts[clientNum][color] = p;
	} else {
		G_PortalEvict( p );
		trap->UnlinkEntity( (sharedEntity_t *)p );
		// moved: the clients do not draw it on its way from the old place to the new one
		p->s.eFlags ^= EF_TELEPORT_BIT;
	}
	portalTeams[clientNum] = ent->client->sess.sessionTeam;

	BG_PortalAngles( axis, angles );
	p->s.eType = ET_INVISIBLE;
	p->s.weapon = WP_BRYAR_OLD;
	p->s.modelindex = PORTAL_MARK;
	p->s.eventParm = color;
	p->s.otherEntityNum = clientNum;
	p->s.otherEntityNum2 = ENTITYNUM_NONE;
	p->s.generic1 = 0;
	p->r.contents = 0;
	p->clipmask = 0;
	p->r.ownerNum = clientNum;
	G_SetOrigin( p, (float *)center );
	G_SetAngles( p, angles );
	// (s.origin and s.angles are what the portal says about itself, bg_portal.h BG_PortalFromState; G_SetOrigin sets
	// only the trajectory)
	VectorCopy( center, p->s.origin );
	VectorCopy( angles, p->s.angles );
	// the bounds hold the opening, a little out in front, for the leaves of the level the server sends it into
	for ( int i = 0; i < 3; i++ ) {
		const float reach = fabsf( axis[1][i] ) * PORTAL_HALF_WIDTH + fabsf( axis[2][i] ) * PORTAL_HALF_HEIGHT + fabsf( axis[0][i] ) * 4.0f;

		p->r.mins[i] = -reach;
		p->r.maxs[i] = reach;
	}
	trap->LinkEntity( (sharedEntity_t *)p );
	G_PortalPair( clientNum );

	// the mark of the legacy clients at once, not with the next round of them
	{
		vec3_t at;

		VectorMA( center, 2.0f, axis[0], at );
		G_PlayEffectID( G_PortalFx( color ), at, (float *)axis[0] );
	}
}

// The portal gun takes no ammunition: back what the move code took for a shot of the old bryar (FireWeapon)
void G_PortalRefund( gentity_t *ent, qboolean altFire ) {
	const int ammo = weaponData[WP_BRYAR_OLD].ammoIndex;

	if ( ammo > AMMO_NONE && ammo < AMMO_MAX ) {
		ent->client->ps.ammo[ammo] += altFire ? weaponData[WP_BRYAR_OLD].altEnergyPerShot : weaponData[WP_BRYAR_OLD].energyPerShot;
		if ( ent->client->ps.ammo[ammo] > ammoData[ammo].max ) {
			ent->client->ps.ammo[ammo] = ammoData[ammo].max;
		}
	}
}

/*
The buttons of the portal gun that are not its primary fire. The alt fire sets the orange portal the moment it is
pressed, as the primary fire sets the blue one: the old bryar charges its alt fire in the shared move code (bg_pmove.c,
which a legacy client predicts as well) and fires on the release, and that release sets nothing (FireWeapon); a first
press often set no portal, and a charge let go late, on a change of weapons, set one where the player looked then
(their test 2026-11-27). The use key takes both portals of the player away (their word the same day).
*/
#define PORTAL_ALT_GAP		300		// msec between two orange portals

static int portalAltTime[MAX_CLIENTS];

void G_PortalButtons( gentity_t *ent, int buttons ) {
	gclient_t	*client = ent->client;
	vec3_t		forward, right, up, muzzle;

	if ( !client || ent->s.number >= MAX_CLIENTS || !G_PortalGunReady( ent ) || client->ps.weapon != WP_BRYAR_OLD
		|| client->ps.weaponstate == WEAPON_RAISING || client->ps.weaponstate == WEAPON_DROPPING
		|| client->ps.stats[STAT_HEALTH] <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	// (client->buttons are still those of the command before)
	if ( ( buttons & BUTTON_USE ) && !( client->buttons & BUTTON_USE ) ) {
		G_PortalRemoveBoth( ent->s.number );
		return;
	}
	if ( !( buttons & BUTTON_ALT_ATTACK ) || ( client->buttons & BUTTON_ALT_ATTACK ) || level.time < portalAltTime[ent->s.number] ) {
		return;
	}
	portalAltTime[ent->s.number] = level.time + PORTAL_ALT_GAP;
	AngleVectors( client->ps.viewangles, forward, right, up );
	CalcMuzzlePoint( ent, forward, right, up, muzzle );
	G_PortalFire( ent, qtrue, muzzle, forward );
}

void G_PortalFire( gentity_t *ent, qboolean altFire, const vec3_t muzzle, const vec3_t forward ) {
	const int	color = altFire ? PORTAL_ORANGE : PORTAL_BLUE;
	vec3_t		start, end, beamFrom, dir, center, axis[3];
	int			shooter = ent->s.number, skip = ent->s.number;
	trace_t		tr;

	VectorCopy( ent->client->ps.origin, start );
	start[2] += ent->client->ps.viewheight;
	VectorCopy( forward, dir );
	VectorCopy( muzzle, beamFrom );
	for ( int n = 0; ; n++ ) {
		vec3_t at, outStart, outEnd;

		VectorMA( start, PORTAL_SHOT_RANGE, dir, end );
		trap->Trace( &tr, start, NULL, NULL, end, skip, MASK_SOLID, qfalse, 0, 0 );
		// a portal shot through an open portal goes on out of the other one
		if ( n < PORTAL_SHOT_THROUGH && G_PortalRay( start, end, &tr, at, outStart, outEnd ) ) {
			G_PortalBeam( beamFrom, at, shooter, color );
			VectorCopy( outStart, start );
			VectorCopy( outStart, beamFrom );
			VectorSubtract( outEnd, outStart, dir );
			VectorNormalize( dir );
			shooter = ENTITYNUM_NONE;
			skip = ENTITYNUM_NONE;
			continue;
		}
		break;
	}
	G_PortalBeam( beamFrom, tr.endpos, shooter, color );

	if ( tr.fraction >= 1.0f ) {
		return;
	}
	if ( tr.startsolid || tr.entityNum != ENTITYNUM_WORLD || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) )
		|| !( tr.contents & ( CONTENTS_SOLID | CONTENTS_TERRAIN ) ) ) {
		G_PortalMiss( tr.endpos, tr.plane.normal );
		return;
	}

	BG_PortalAxis( tr.plane.normal, dir, axis );
	// the middle on the face of the wall (the trace stopped a little short of it)
	VectorMA( tr.endpos, -( DotProduct( tr.endpos, tr.plane.normal ) - tr.plane.dist ), tr.plane.normal, center );
	// (onto the floor first: a shot low on a wall would not fit where it hit, its opening reaching into the floor)
	G_PortalOnFloor( center, (const vec3_t *)axis, ent->s.number, color );
	if ( !G_PortalFit( center, (const vec3_t *)axis, ent->s.number, color ) ) {
		G_PortalMiss( tr.endpos, tr.plane.normal );
		return;
	}
	G_PortalPlace( ent, color, center, (const vec3_t *)axis );
}

/*
=================
amportalgun <player> [0|1]

The portal gun for a player (the old bryar pistol, vs_portalGun), 1 gives it, 0 takes it and his portals, without
a number it changes. Like amfirerate for rcon level admins and the server console: the masks of the ranks have no bit
left for a new command.
=================
*/
void Cmd_amportalgun_f( gentity_t *ent, qboolean rcon ) {
	char		arg[MAX_TOKEN_CHARS];
	gentity_t	*target;
	qboolean	give;

	if ( !rcon && ent->client->sess.jpAdminLevel != JPADM_RCON ) {
		trap->Argv( 0, arg, sizeof( arg ) );
		trap->SendServerCommand( ent - g_entities, va( "print \"unknown cmd %s\n\"", arg ) );
		return;
	}
	if ( trap->Argc() < 2 ) {
		JP_Reply( ent, rcon, "Usage: ^2amportalgun ^7<player> [0 or 1]\n" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !( target = JP_ValidClient( G_getClientNumberForString( arg ) ) ) ) {
		JP_Reply( ent, rcon, va( "^1No player %s.\n", arg ) );
		return;
	}
	give = !portalGuns[target->s.number];
	if ( trap->Argc() > 2 ) {
		trap->Argv( 2, arg, sizeof( arg ) );
		give = (qboolean)( atoi( arg ) != 0 );
	}

	portalGuns[target->s.number] = give;
	if ( give ) {
		if ( target->client->sess.sessionTeam != TEAM_SPECTATOR ) {
			target->client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_BRYAR_OLD );
		}
	} else {
		G_PortalRemoveBoth( target->s.number );
		target->client->ps.stats[STAT_WEAPONS] &= ~( 1 << WP_BRYAR_OLD );
		if ( target->client->ps.weapon == WP_BRYAR_OLD ) {
			// another weapon he has up, and his cgame picks the next as when one runs out of ammunition
			// (TossClientWeapon in g_combat.c does the same)
			int weapon = WP_NONE;

			for ( int i = WP_NONE + 1; i < WP_NUM_WEAPONS; i++ ) {
				if ( target->client->ps.stats[STAT_WEAPONS] & ( 1 << i ) ) {
					weapon = i;
					break;
				}
			}
			target->client->ps.weapon = weapon;
			target->s.weapon = weapon;
			G_AddEvent( target, EV_NOAMMO, WP_BRYAR_OLD );
		}
	}
	JP_Reply( ent, rcon, va( "%s^7 %s the portal gun%s\n", target->client->pers.netname, give ? "has" : "no longer has",
		give && !vs_portalGun.integer ? " (^3vs_portalGun is 0^7, it fires as the old bryar until it is 1)" : "" ) );
	if ( give && target != ent ) {
		trap->SendServerCommand( target->s.number, "print \"You have the portal gun: the old bryar pistol, primary fire blue, alt fire orange.\n\"" );
	}
}
