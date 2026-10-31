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

// cg_pong.c -- table tennis, a game of issue #9 that only players of this tree see: v_pingpong (Controls, Extras)
// puts a table in front of the player and he plays from its near end; another player who stands at its far end and
// uses v_pingpong there plays against him. While they play the camera stands behind their end, the mouse moves the
// paddle, a click serves, Esc leaves, and their players stand still with the dots of talking (as in the RC car, so
// on a JA+ server nobody can hurt them). Anybody near sees the table, the paddles, the ball and the points.
//
// No server keeps the game. It goes as toys (game/toys/bg_toys.h, the way a snowball goes, toys/cg_toys.c): the
// owner says where the table is and who plays the other end (TOY_TABLE), the paddles say where they are, and a hit
// says where the ball is and how fast (TOY_PONG_BALL), after which every cgame lets it fly on its own. The player the
// ball flies to judges what it does on his side (a return, a miss, a ball out or in the net) and says the points
// (TOY_PONG_SCORE). Eleven points win with two ahead, the serve changes every two points, at ten all every point.
// A hit aims for the other half: where the ball meets the paddle and how fast the mouse moves steer it.
// Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"
#include "ui/ui_shared.h"

// the table, in units, its middle on the floor, its length along its yaw (tools/toys/pingpong.py has the same)
#define PONG_LENGTH			124.0f
#define PONG_WIDTH			70.0f
#define PONG_HEIGHT			31.0f
#define PONG_NET			7.0f
#define PONG_BLADE			5.0f
#define PONG_BALL			1.2f		// the radius of the ball
#define PONG_GRAVITY		420.0f
#define PONG_BOUNCE			0.86f		// of the speed up and down that a bounce on the table keeps
#define PONG_REACH			9.0f		// how far from the middle of the paddle the ball is still hit
#define PONG_PADDLE_X		10.0f		// the paddle moves this far behind the end of the table
#define PONG_JOIN_RANGE		90.0f		// how near the far end of a table a player has to be to play there
#define PONG_JOIN_MSEC		3000		// what the owner says of the far end so soon after a player asked for it is old
#define PONG_ALIVE_MSEC		5000		// the owner says where his table is this often
#define PONG_LOST_MSEC		16000		// a table not heard of for this long is gone
#define PONG_PADDLE_MSEC	165			// a paddle says where it is this often
#define PONG_WIN			11
#define PONG_OVER_MSEC		4000		// the winner of a game is shown this long, then a new one begins
#define PONG_TABLE_MODEL	"models/map_objects/vision/pingpong_table.md3"
#define PONG_PADDLE_MODEL	"models/map_objects/vision/pingpong_paddle.md3"

typedef struct {
	qboolean	active;
	vec3_t		origin;
	float		yaw;
	int			guest;				// the client at the far end, -1: nobody
	int			heard;
	int			score[2];			// of the owner and of the guest
	int			server;				// 0 the owner, 1 the guest
	int			overTime;			// a game was won then, 0: it goes on
	// the ball, in the coordinates of the table: x along it (the owner at -x), y to its left, z up from the floor
	qboolean	ballLive;
	vec3_t		ball, ballVel;
	int			hitter;				// who hit it last: 0 the owner, 1 the guest
	int			bounces;			// on the half of the other one since the hit
	vec3_t		paddle[2];			// as the paddles said (the own one is where the mouse put it)
	qboolean	paddleSeen[2];
} pongTable_t;

static struct {
	pongTable_t	tables[MAX_CLIENTS];	// by the client who put it down
	int			table;					// the table this player plays at (its owner), -1: none
	int			side;					//	at its near end (0) or its far end (1)
	float		paddleY, paddleZ;		// where the mouse put the own paddle, in the coordinates of the table
	float		paddleSpeed;			// how fast the mouse moved it sideways, units a second
	int			paddleTime, aliveTime;
	int			joinTime;				// when this player asked to play at the far end
	qhandle_t	tableModel, paddleModel, ballShader;
	sfxHandle_t	hitSound, bounceSound;
	qboolean	registered;
} pong;

static void Pong_Register( void ) {
	if ( pong.registered )
		return;
	pong.registered = qtrue;
	pong.tableModel = trap->R_RegisterModel( PONG_TABLE_MODEL );
	pong.paddleModel = trap->R_RegisterModel( PONG_PADDLE_MODEL );
	pong.ballShader = trap->R_RegisterShader( "gfx/vision/toys/pingball" );
	pong.hitSound = trap->S_RegisterSound( "sound/vision/toys/pong_hit.wav" );
	pong.bounceSound = trap->S_RegisterSound( "sound/vision/toys/pong_bounce.wav" );
}

void CG_PongInit( void ) {
	memset( &pong, 0, sizeof( pong ) );
	pong.table = -1;
}

static void Pong_Axes( const pongTable_t *t, vec3_t forward, vec3_t left ) {
	vec3_t angles, right;

	VectorSet( angles, 0.0f, t->yaw, 0.0f );
	AngleVectors( angles, forward, right, NULL );
	VectorScale( right, -1.0f, left );
}

static void Pong_ToWorld( const pongTable_t *t, const vec3_t local, vec3_t out ) {
	vec3_t forward, left;

	Pong_Axes( t, forward, left );
	VectorMA( t->origin, local[0], forward, out );
	VectorMA( out, local[1], left, out );
	out[2] += local[2];
}

static void Pong_ToLocal( const pongTable_t *t, const vec3_t world, vec3_t out ) {
	vec3_t forward, left, d;

	Pong_Axes( t, forward, left );
	VectorSubtract( world, t->origin, d );
	VectorSet( out, DotProduct( d, forward ), DotProduct( d, left ), d[2] );
}

// the same for a speed: no origin
static void Pong_DirToWorld( const pongTable_t *t, const vec3_t local, vec3_t out ) {
	vec3_t forward, left;

	Pong_Axes( t, forward, left );
	VectorScale( forward, local[0], out );
	VectorMA( out, local[1], left, out );
	out[2] += local[2];
}

static void Pong_DirToLocal( const pongTable_t *t, const vec3_t world, vec3_t out ) {
	vec3_t forward, left;

	Pong_Axes( t, forward, left );
	VectorSet( out, DotProduct( world, forward ), DotProduct( world, left ), world[2] );
}

// the end of a side: -1 for the owner, 1 for the guest
static float Pong_End( int side ) {
	return side ? 1.0f : -1.0f;
}

static void Pong_Send( int kind, const vec3_t origin, int x, int y, int z ) {
	toyThrow_t	toy;
	int			i;

	toy.kind = kind;
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)floorf( origin[i] + 0.5f );
	toy.velocity[0] = x;
	toy.velocity[1] = y;
	toy.velocity[2] = z;
	if ( BG_ToyValid( &toy ) )
		CG_ToySend( &toy );
}

static void Pong_SendTable( void ) {
	const pongTable_t *t = &pong.tables[cg.clientNum];

	Pong_Send( TOY_TABLE, t->origin, (int)AngleNormalize180( t->yaw ), t->guest < 0 ? MAX_CLIENTS : t->guest, 0 );
	pong.aliveTime = cg.time ? cg.time : 1;
}

static void Pong_SendScore( const pongTable_t *t ) {
	vec3_t mid;

	VectorCopy( t->origin, mid );
	mid[2] += PONG_HEIGHT;
	Pong_Send( TOY_PONG_SCORE, mid, t->score[0], t->score[1], t->server );
}

// who serves after these points: every two points the other one, at ten all every point
static int Pong_Server( const int score[2] ) {
	const int total = score[0] + score[1];

	if ( score[0] >= PONG_WIN - 1 && score[1] >= PONG_WIN - 1 )
		return total & 1;
	return ( total / 2 ) & 1;
}

static qboolean Pong_Won( const int score[2] ) {
	return (qboolean)( ( score[0] >= PONG_WIN || score[1] >= PONG_WIN ) && abs( score[0] - score[1] ) >= 2 );
}

// the table and the side a client plays at, -1: none
static int Pong_TableOf( int client, int *side ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const pongTable_t *t = &pong.tables[i];

		if ( !t->active )
			continue;
		if ( i == client || t->guest == client ) {
			*side = ( i == client ) ? 0 : 1;
			return i;
		}
	}
	return -1;
}

static void Pong_Catch( qboolean on ) {
	if ( on ) {
		trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
		// he holds a paddle, he is not posing: the dots of talking stay (CL_CmdButtons())
		trap->Cvar_Set( "v_catchQuiet", "0" );
	} else {
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
	}
}

static void Pong_Leave( void ) {
	pongTable_t *t;

	if ( pong.table < 0 )
		return;
	t = &pong.tables[pong.table];
	if ( pong.side == 0 ) {
		Pong_Send( TOY_TABLE_GONE, t->origin, 0, 0, 0 );
		t->active = qfalse;
	} else {
		Pong_Send( TOY_PONG_JOIN, t->origin, pong.table, 1, 0 );
	}
	pong.table = -1;
	Pong_Catch( qfalse );
}

static void Pong_Start( int table, int side ) {
	pong.table = table;
	pong.side = side;
	pong.paddleY = 0.0f;
	pong.paddleZ = PONG_HEIGHT + 10.0f;
	pong.paddleSpeed = 0.0f;
	Pong_Catch( qtrue );
}

/*
=================
CG_PingPong_f

v_pingpong: leaves the game, or plays at the far end of a table near that has room, or puts a new table down in
front of the player
=================
*/
void CG_PingPong_f( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	static const vec3_t	mins = { -PONG_LENGTH * 0.5f, -PONG_WIDTH * 0.5f, 8.0f }, maxs = { PONG_LENGTH * 0.5f, PONG_WIDTH * 0.5f, PONG_HEIGHT + PONG_NET };
	pongTable_t			*t;
	vec3_t				forward, start, end, farEnd;
	trace_t				tr;
	int					i;

	if ( pong.table >= 0 ) {
		Pong_Leave();
		return;
	}
	if ( !CG_ToyCanUse() || cg.clientNum < 0 || cg.clientNum >= MAX_CLIENTS )
		return;
	Pong_Register();
	// the far end of a table of somebody else, near enough and free
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const vec3_t local = { PONG_LENGTH * 0.5f, 0.0f, 0.0f };

		t = &pong.tables[i];
		if ( !t->active || i == cg.clientNum || t->guest >= 0 )
			continue;
		Pong_ToWorld( t, local, farEnd );
		if ( Distance( farEnd, ps->origin ) < PONG_JOIN_RANGE ) {
			Pong_Send( TOY_PONG_JOIN, t->origin, i, 0, 0 );
			t->guest = cg.clientNum;
			pong.joinTime = cg.time;
			Pong_Start( i, 1 );
			return;
		}
	}
	// a new table in front of the feet, down to the floor, with room for it
	t = &pong.tables[cg.clientNum];
	memset( t, 0, sizeof( *t ) );
	VectorSet( start, 0.0f, cg.refdef.viewangles[YAW], 0.0f );
	AngleVectors( start, forward, NULL, NULL );
	VectorMA( ps->origin, PONG_LENGTH * 0.5f + 24.0f, forward, start );
	start[2] += 16.0f;
	VectorCopy( start, end );
	end[2] -= 96.0f;
	CG_Trace( &tr, start, NULL, NULL, end, cg.clientNum, MASK_PLAYERSOLID );
	if ( tr.fraction >= 1.0f || tr.startsolid || tr.plane.normal[2] < 0.9f ) {
		Com_Printf( "No flat floor for a table here\n" );
		return;
	}
	VectorCopy( tr.endpos, t->origin );
	t->yaw = AngleNormalize360( cg.refdef.viewangles[YAW] );
	VectorCopy( t->origin, start );
	start[2] += 1.0f;
	// (the level only: a player who stands in the way steps aside)
	CG_Trace( &tr, start, mins, maxs, start, cg.clientNum, MASK_SOLID );
	if ( tr.startsolid || tr.allsolid ) {
		Com_Printf( "No room for a table here\n" );
		return;
	}
	t->active = qtrue;
	t->guest = -1;
	t->heard = cg.time;
	Pong_Start( cg.clientNum, 0 );
	Pong_SendTable();
}

qboolean CG_PongPlaying( void ) {
	return (qboolean)( pong.table >= 0 );
}

// a toy of table tennis of somebody else, from the game module or the masterserver
void CG_PongHeard( int client, int kind, const vec3_t origin, const vec3_t velocity ) {
	pongTable_t	*t;
	int			side, table;

	Pong_Register();
	switch ( kind ) {
	case TOY_TABLE:
		t = &pong.tables[client];
		if ( !t->active ) {
			memset( t, 0, sizeof( *t ) );
			t->active = qtrue;
		}
		VectorCopy( origin, t->origin );
		t->yaw = velocity[0];
		t->guest = ( (int)velocity[1] >= MAX_CLIENTS ) ? -1 : (int)velocity[1];
		t->heard = cg.time;
		// the owner gave the far end to somebody else (what he said before he heard the own wish does not count)
		if ( pong.table == client && pong.side == 1 && t->guest != cg.clientNum ) {
			if ( cg.time - pong.joinTime < PONG_JOIN_MSEC && cg.time >= pong.joinTime ) {
				t->guest = cg.clientNum;
			} else {
				pong.table = -1;
				Pong_Catch( qfalse );
			}
		}
		return;
	case TOY_TABLE_GONE:
		pong.tables[client].active = qfalse;
		if ( pong.table == client ) {
			pong.table = -1;
			Pong_Catch( qfalse );
		}
		return;
	case TOY_PONG_JOIN:
		t = &pong.tables[cg.clientNum];
		if ( (int)velocity[0] != cg.clientNum || !t->active || pong.table != cg.clientNum )
			return;
		if ( !velocity[1] && t->guest < 0 ) {
			t->guest = client;
			t->score[0] = t->score[1] = t->server = 0;
			t->ballLive = qfalse;
		} else if ( velocity[1] && t->guest == client ) {
			t->guest = -1;
			t->ballLive = qfalse;
		}
		Pong_SendTable();
		return;
	default:
		break;
	}
	table = Pong_TableOf( client, &side );
	if ( table < 0 )
		return;
	t = &pong.tables[table];
	if ( kind == TOY_PONG_PADDLE ) {
		Pong_ToLocal( t, origin, t->paddle[side] );
		t->paddleSeen[side] = qtrue;
	} else if ( kind == TOY_PONG_BALL ) {
		const float late = Com_Clamp( 0.0f, 0.25f, ( cg.snap ? cg.snap->ping : 0 ) * 0.0005f );

		Pong_ToLocal( t, origin, t->ball );
		Pong_DirToLocal( t, velocity, t->ballVel );
		// (it flew on while the hit was on its way here)
		VectorMA( t->ball, late, t->ballVel, t->ball );
		t->ball[2] -= 0.5f * PONG_GRAVITY * late * late;
		t->ballVel[2] -= PONG_GRAVITY * late;
		t->hitter = side;
		t->bounces = 0;
		t->ballLive = qtrue;
		trap->S_StartSound( (float *)origin, client, CHAN_AUTO, pong.hitSound );
	} else if ( kind == TOY_PONG_SCORE ) {
		t->score[0] = (int)velocity[0];
		t->score[1] = (int)velocity[1];
		t->server = (int)velocity[2];
		t->ballLive = qfalse;
		t->overTime = Pong_Won( t->score ) ? ( cg.time ? cg.time : 1 ) : 0;
	}
}

// this player, who the ball flew to, says who got the point
static void Pong_Point( pongTable_t *t, int winner ) {
	t->ballLive = qfalse;
	t->score[winner]++;
	t->server = Pong_Server( t->score );
	t->overTime = Pong_Won( t->score ) ? ( cg.time ? cg.time : 1 ) : 0;
	Pong_SendScore( t );
}

// The own paddle hits the ball toward a place on the other half, in an arc that clears the net. Where it meets the
// paddle and how fast the paddle moves sideways steer it, a ball hit late goes long.
static void Pong_Hit( pongTable_t *t, const vec3_t paddle ) {
	const float	end = Pong_End( pong.side );
	vec3_t		target, world, worldVel;
	float		flight;

	target[0] = -end * Com_Clamp( 12.0f, PONG_LENGTH * 0.5f - 6.0f, PONG_LENGTH * 0.3f + ( paddle[2] - t->ball[2] ) * 1.5f );
	target[1] = Com_Clamp( -PONG_WIDTH * 0.5f + 4.0f, PONG_WIDTH * 0.5f - 4.0f, ( t->ball[1] - paddle[1] ) * 3.0f - end * pong.paddleSpeed * 0.04f );
	target[2] = PONG_HEIGHT + PONG_BALL;
	// the shortest flight that goes over the net
	for ( flight = 0.45f; flight < 1.4f; flight += 0.05f ) {
		const float	vz = ( target[2] - t->ball[2] + 0.5f * PONG_GRAVITY * flight * flight ) / flight;
		const float	toNet = t->ball[0] / ( t->ball[0] - target[0] ) * flight;
		const float	atNet = t->ball[2] + vz * toNet - 0.5f * PONG_GRAVITY * toNet * toNet;

		if ( atNet > PONG_HEIGHT + PONG_NET + 3.0f )
			break;
	}
	VectorSet( t->ballVel, ( target[0] - t->ball[0] ) / flight, ( target[1] - t->ball[1] ) / flight,
		( target[2] - t->ball[2] + 0.5f * PONG_GRAVITY * flight * flight ) / flight );
	t->hitter = pong.side;
	t->bounces = 0;
	t->ballLive = qtrue;
	Pong_ToWorld( t, t->ball, world );
	Pong_DirToWorld( t, t->ballVel, worldVel );
	worldVel[0] = Com_Clamp( -TOY_MAX_SPEED + 1, TOY_MAX_SPEED - 1, worldVel[0] );
	worldVel[1] = Com_Clamp( -TOY_MAX_SPEED + 1, TOY_MAX_SPEED - 1, worldVel[1] );
	worldVel[2] = Com_Clamp( -TOY_MAX_SPEED + 1, TOY_MAX_SPEED - 1, worldVel[2] );
	Pong_Send( TOY_PONG_BALL, world, (int)worldVel[0], (int)worldVel[1], (int)worldVel[2] );
	trap->S_StartSound( world, cg.clientNum, CHAN_AUTO, pong.hitSound );
}

// where the own paddle is, in the coordinates of the table
static void Pong_OwnPaddle( vec3_t out ) {
	VectorSet( out, Pong_End( pong.side ) * ( PONG_LENGTH * 0.5f + PONG_PADDLE_X ), pong.paddleY, pong.paddleZ );
}

/*
=================
Pong_Fly

The ball of a table flies on, bounces on the table, stops at the net. The player it flies to hits it back with his
paddle or judges the point.
=================
*/
static void Pong_Fly( pongTable_t *t, int table, float dt ) {
	const int		receiver = !t->hitter;
	const qboolean	judge = (qboolean)( pong.table == table && pong.side == receiver );
	const float		recEnd = Pong_End( receiver );
	vec3_t			paddle, world, meet;
	float			oldX;

	while ( dt > 0.0f && t->ballLive ) {
		const float step = Q_min( dt, 0.01f );

		dt -= step;
		oldX = t->ball[0];
		VectorMA( t->ball, step, t->ballVel, t->ball );
		t->ballVel[2] -= PONG_GRAVITY * step;
		// the net: it falls on the side it came from
		if ( ( oldX < 0.0f ) != ( t->ball[0] < 0.0f ) && t->ball[2] < PONG_HEIGHT + PONG_NET + PONG_BALL
			&& fabsf( t->ball[1] ) < PONG_WIDTH * 0.5f + 3.0f ) {
			t->ball[0] = oldX;
			t->ballVel[0] *= -0.15f;
			t->ballVel[1] *= 0.3f;
		}
		// the table
		if ( t->ballVel[2] < 0.0f && t->ball[2] <= PONG_HEIGHT + PONG_BALL && t->ball[2] > PONG_HEIGHT - 4.0f
			&& fabsf( t->ball[0] ) <= PONG_LENGTH * 0.5f && fabsf( t->ball[1] ) <= PONG_WIDTH * 0.5f ) {
			const int half = ( t->ball[0] > 0.0f ) ? 1 : 0;

			t->ball[2] = PONG_HEIGHT + PONG_BALL;
			t->ballVel[2] = -t->ballVel[2] * PONG_BOUNCE;
			Pong_ToWorld( t, t->ball, world );
			trap->S_StartSound( world, ENTITYNUM_WORLD, CHAN_AUTO, pong.bounceSound );
			if ( judge ) {
				// on the half of who hit it: his fault. Twice on the own half: not returned.
				if ( half != receiver ) {
					Pong_Point( t, receiver );
					return;
				}
				if ( ++t->bounces >= 2 ) {
					Pong_Point( t, t->hitter );
					return;
				}
			} else if ( half == receiver ) {
				t->bounces++;
			}
		}
		// the own paddle, when the ball comes to it
		if ( judge ) {
			Pong_OwnPaddle( paddle );
			VectorSet( meet, paddle[0], t->ball[1], t->ball[2] );
			if ( recEnd * t->ballVel[0] > 0.0f && recEnd * t->ball[0] >= recEnd * paddle[0] - 2.0f
				&& recEnd * oldX < recEnd * paddle[0] + 2.0f && Distance( meet, paddle ) < PONG_REACH ) {
				Pong_Hit( t, paddle );
				return;
			}
		}
		// down on the floor or far off: in if it bounced on the half of the one it flew to, out if not
		if ( t->ball[2] < PONG_BALL || fabsf( t->ball[0] ) > PONG_LENGTH * 0.5f + 70.0f || fabsf( t->ball[1] ) > PONG_WIDTH + 40.0f ) {
			if ( judge ) {
				Pong_Point( t, t->bounces ? t->hitter : receiver );
			} else {
				t->ballLive = qfalse;
			}
			return;
		}
	}
}

/*
=================
CG_PongFrame

Once a frame before the view is worked out: the balls fly, the own paddle and table are said to the others, a
table nobody heard of for long is gone. qtrue: this player plays, the view is his end of the table.
=================
*/
qboolean CG_PongFrame( void ) {
	const float	dt = Q_min( cg.frametime * 0.001f, 0.05f );
	int			i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		pongTable_t *t = &pong.tables[i];

		if ( !t->active )
			continue;
		if ( i != cg.clientNum && ( cg.time - t->heard > PONG_LOST_MSEC || !cgs.clientinfo[i].infoValid ) ) {
			t->active = qfalse;
			if ( pong.table == i ) {
				pong.table = -1;
				Pong_Catch( qfalse );
			}
			continue;
		}
		if ( t->overTime && cg.time - t->overTime > PONG_OVER_MSEC ) {
			t->overTime = 0;
			t->score[0] = t->score[1] = t->server = 0;
		}
		if ( t->ballLive && dt > 0.0f )
			Pong_Fly( t, i, dt );
	}
	if ( pong.table < 0 )
		return qfalse;
	// a menu or the console took the keys, the player died or left the game
	if ( !CG_ToyCanUse() || !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		Pong_Leave();
		return qfalse;
	}
	pong.paddleSpeed *= Q_max( 0.0f, 1.0f - dt * 8.0f );
	if ( !pong.paddleTime || cg.time - pong.paddleTime >= PONG_PADDLE_MSEC || cg.time < pong.paddleTime ) {
		vec3_t local, world;

		Pong_OwnPaddle( local );
		Pong_ToWorld( &pong.tables[pong.table], local, world );
		Pong_Send( TOY_PONG_PADDLE, world, 0, 0, 0 );
		pong.paddleTime = cg.time ? cg.time : 1;
	}
	if ( pong.side == 0 && ( !pong.aliveTime || cg.time - pong.aliveTime >= PONG_ALIVE_MSEC || cg.time < pong.aliveTime ) )
		Pong_SendTable();
	return qtrue;
}

// After the view is worked out: the camera behind the own end of the table, high enough to see the whole of it
void CG_PongView( void ) {
	const pongTable_t	*t;
	vec3_t				local, eye, target, dir;

	if ( pong.table < 0 )
		return;
	t = &pong.tables[pong.table];
	// (where the head of the own player is, who is not drawn for himself while he plays, a little behind the paddle)
	VectorSet( local, Pong_End( pong.side ) * ( PONG_LENGTH * 0.5f + 44.0f ), pong.paddleY * 0.3f, PONG_HEIGHT + 40.0f );
	Pong_ToWorld( t, local, eye );
	VectorSet( local, -Pong_End( pong.side ) * PONG_LENGTH * 0.15f, 0.0f, PONG_HEIGHT );
	Pong_ToWorld( t, local, target );
	VectorSubtract( target, eye, dir );
	VectorCopy( eye, cg.refdef.vieworg );
	vectoangles( dir, cg.refdef.viewangles );
	AnglesToAxis( cg.refdef.viewangles, cg.refdef.viewaxis );
}

// the mouse moves the paddle: sideways across the end of the table, up and down above it
qboolean CG_PongMouseEvent( int x, int y ) {
	float move;

	if ( pong.table < 0 )
		return qfalse;
	move = x * 0.08f * Pong_End( pong.side );
	pong.paddleY = Com_Clamp( -PONG_WIDTH * 0.5f - 16.0f, PONG_WIDTH * 0.5f + 16.0f, pong.paddleY + move );
	if ( cg.frametime > 0 )
		pong.paddleSpeed = pong.paddleSpeed * 0.5f + 0.5f * move / ( cg.frametime * 0.001f );
	pong.paddleZ = Com_Clamp( PONG_HEIGHT - 2.0f, PONG_HEIGHT + 34.0f, pong.paddleZ - y * 0.06f );
	return qtrue;
}

qboolean CG_PongKeyEvent( int key, qboolean down ) {
	pongTable_t *t;

	if ( pong.table < 0 )
		return qfalse;
	t = &pong.tables[pong.table];
	if ( !down )
		return qtrue;
	if ( key == A_ESCAPE ) {
		Pong_Leave();
	} else if ( key == A_MOUSE1 && !t->ballLive && !t->overTime && t->guest >= 0 && t->server == pong.side ) {
		vec3_t paddle;

		// the serve: the ball is thrown up in front of the paddle and hit
		Pong_OwnPaddle( paddle );
		VectorSet( t->ball, paddle[0] - Pong_End( pong.side ) * 2.0f, paddle[1], Q_max( paddle[2], PONG_HEIGHT + 6.0f ) );
		Pong_Hit( t, paddle );
	}
	return qtrue;
}

static void Pong_AddModel( const pongTable_t *t, qhandle_t model, const vec3_t local, float turn ) {
	refEntity_t	ent;
	vec3_t		angles;

	if ( !model )
		return;
	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_MODEL;
	ent.hModel = model;
	Pong_ToWorld( t, local, ent.origin );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	VectorSet( angles, 0.0f, t->yaw + turn, 0.0f );
	AnglesToAxis( angles, ent.axis );
	trap->R_AddRefEntityToScene( &ent );
}

// While the scene is built: every table, its paddles and its ball
void CG_PongScene( void ) {
	static const vec3_t	zero = { 0.0f, 0.0f, 0.0f };
	int					i, side;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const pongTable_t *t = &pong.tables[i];

		if ( !t->active || ( i != cg.clientNum && !v_toys.integer ) )
			continue;
		Pong_Register();
		Pong_AddModel( t, pong.tableModel, zero, 0.0f );
		for ( side = 0; side < 2; side++ ) {
			vec3_t paddle;

			if ( pong.table == i && pong.side == side ) {
				Pong_OwnPaddle( paddle );
			} else if ( ( side == 0 || t->guest >= 0 ) && t->paddleSeen[side] ) {
				VectorCopy( t->paddle[side], paddle );
			} else {
				continue;
			}
			Pong_AddModel( t, pong.paddleModel, paddle, 0.0f );
		}
		if ( t->ballLive ) {
			refEntity_t ent;
			vec3_t		light;

			memset( &ent, 0, sizeof( ent ) );
			ent.reType = RT_SPRITE;
			Pong_ToWorld( t, t->ball, ent.origin );
			ent.radius = PONG_BALL + 0.3f;
			ent.customShader = pong.ballShader;
			CG_ToyLight( ent.origin, light );
			ent.shaderRGBA[0] = (byte)( light[0] * 255.0f );
			ent.shaderRGBA[1] = (byte)( light[1] * 255.0f );
			ent.shaderRGBA[2] = (byte)( light[2] * 255.0f );
			ent.shaderRGBA[3] = 255;
			trap->R_AddRefEntityToScene( &ent );
		}
	}
}

/*
=================
CG_PongDraw

In the 2D of the HUD: the points over every table near that has two players, and for the own game who serves
=================
*/
void CG_PongDraw( void ) {
	vec4_t			color = { 1.0f, 1.0f, 1.0f, 1.0f }, gold = { 1.0f, 0.85f, 0.3f, 1.0f };
	int				i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const pongTable_t	*t = &pong.tables[i];
		vec3_t				local, at;
		float				x, y;
		const char			*text;

		if ( !t->active || t->guest < 0 || ( i != cg.clientNum && !v_toys.integer ) )
			continue;
		VectorSet( local, 0.0f, 0.0f, PONG_HEIGHT + PONG_NET + 18.0f );
		Pong_ToWorld( t, local, at );
		if ( Distance( at, cg.refdef.vieworg ) > 700.0f || !CG_WorldCoordToScreenCoordFloat( at, &x, &y ) )
			continue;
		if ( t->overTime )
			text = va( "%s^7 wins %i : %i", cgs.clientinfo[t->score[0] > t->score[1] ? i : t->guest].name, t->score[0], t->score[1] );
		else
			text = va( "%s^7  %i : %i  %s", cgs.clientinfo[i].name, t->score[0], t->score[1], cgs.clientinfo[t->guest].name );
		CG_Text_Paint( x - CG_Text_Width( text, 0.6f, FONT_SMALL ) * 0.5f, y, 0.6f, t->overTime ? gold : color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
	}
	if ( pong.table >= 0 ) {
		const pongTable_t	*t = &pong.tables[pong.table];
		const char			*text = NULL;

		if ( t->guest < 0 )
			text = "Waiting for a player at the far end";
		else if ( !t->ballLive && !t->overTime && t->server == pong.side )
			text = "Click to serve";
		if ( text )
			CG_Text_Paint( SCREEN_WIDTH * 0.5f - CG_Text_Width( text, 0.7f, FONT_MEDIUM ) * 0.5f, SCREEN_HEIGHT * 0.78f, 0.7f, color, text, 0, 0,
				ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}
