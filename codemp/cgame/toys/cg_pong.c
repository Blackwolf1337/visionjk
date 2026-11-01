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

// cg_pong.c -- table tennis and tennis, games of issue #9 that only players of this tree see: v_pingpong (Controls,
// Extras) puts a table in front of the player and he plays from its near end; another player who stands at its far
// end and uses v_pingpong there plays against him. While they play the camera stands behind their end, the mouse
// moves the paddle, a click serves, Esc leaves, and their players stand still with the dots of talking (as in the RC
// car, so on a JA+ server nobody can hurt them). Anybody near sees the table, the paddles, the ball and the points.
//
// v_tennis does the same with a court on the floor, about half a real one. There the players run about their half
// themselves in the view from behind, with a racket in the right hand: a ball that comes near the racket is hit back
// to where the player looks (higher looks go longer), Use serves, v_tennis again or walking far off leaves. Points
// count as in tennis (15, 30, 40, deuce, advantage), the serve changes every game, three games win.
//
// No server keeps the game. It goes as toys (game/toys/bg_toys.h, the way a snowball goes, toys/cg_toys.c): the
// owner says where the table is and who plays the other end (TOY_TABLE), the paddles say where they are, and a hit
// says where the ball is and how fast (TOY_PONG_BALL), after which every cgame lets it fly on its own. The player the
// ball flies to judges what it does on his side (a return, a miss, a ball out or in the net) and says the points
// (TOY_PONG_SCORE). At the table eleven points win with two ahead, the serve changes every two points, at ten all
// every point. A hit aims for the other half: where the ball meets the paddle and how fast the mouse moves steer it.
// A court goes as a table with z 1 in TOY_TABLE (older builds refuse that and see nothing), its players send no
// paddles: their rackets are in the hands of their players.
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
#define PONG_REACH			9.0f		// how far from the middle of the paddle the ball is still hit
#define PONG_PADDLE_X		10.0f		// the paddle moves this far behind the end of the table
#define PONG_JOIN_MSEC		3000		// what the owner says of the far end so soon after a player asked for it is old
#define PONG_ALIVE_MSEC		5000		// the owner says where his table is this often
#define PONG_LOST_MSEC		16000		// a table not heard of for this long is gone
#define PONG_PADDLE_MSEC	165			// a paddle says where it is this often
#define PONG_OVER_MSEC		4000		// the winner of a game is shown this long, then a new one begins
// the court, in units, its middle on the floor (tools/toys/pingpong.py has the same)
#define COURT_LENGTH		440.0f
#define COURT_WIDTH			200.0f
#define COURT_NET			24.0f
#define COURT_REACH			34.0f		// how far from the racket (over the floor) the ball is still hit
#define COURT_RACKET_SIDE	14.0f		//	the racket: right of the middle of the player, at the height of his origin
#define COURT_AWAY			600.0f		// a player this far from the middle of his court leaves it (more than the corners of
									//	where one joins it)
#define COURT_SWING_MSEC	300
#define COURT_SCORE(games, points)	( (games) * 8 + (points) )	// the points of a player in TOY_PONG_SCORE
#define COURT_GAMES(score)	( (score) >> 3 )
#define COURT_POINTS(score)	( (score) & 7 )

#define PONG_TABLE			0
#define PONG_COURT			1
#define PONG_GAMES			2

typedef struct {
	const char	*name;
	const char	*model;
	float		length, width, height;		// the playing face: along the yaw, across, how high over the floor
	float		net, netSide;				// how high the net is over the face, how far its posts stand out
	float		ball, gravity, bounce;		// the radius of the ball, and of its speed up and down a bounce keeps
	float		flightMin, flightMax;		// a hit flies at least and at most this long (seconds)
	float		farX, farY;					// a ball this far past an end, or from the middle sideways, is down
	float		standX;						// how far from the middle the player who puts it down stands
	float		joinNear, joinFar, joinSide;	// a player plays at the far end from where along it to where, and
											//	how far from the middle sideways
	float		labelZ, labelRange;			// where the points are shown over the middle, and how far they are seen
	int			win;						// points (table) or games (court) that win
} pongGame_t;

static const pongGame_t pongGames[PONG_GAMES] = {
	{ "table", "models/map_objects/vision/pingpong_table.md3", PONG_LENGTH, PONG_WIDTH, PONG_HEIGHT, PONG_NET, 3.0f,
		1.2f, 420.0f, 0.86f, 0.45f, 1.4f, 70.0f, PONG_WIDTH + 40.0f, PONG_LENGTH * 0.5f + 24.0f, PONG_LENGTH * 0.5f - 30.0f,
		PONG_LENGTH * 0.5f + 90.0f, PONG_WIDTH * 0.5f + 30.0f,
		PONG_HEIGHT + PONG_NET + 18.0f, 700.0f, 11 },
	{ "court", "models/map_objects/vision/tennis_court.md3", COURT_LENGTH, COURT_WIDTH, 0.0f, COURT_NET, 12.0f,
		2.0f, 520.0f, 0.72f, 0.5f, 2.4f, 240.0f, COURT_WIDTH * 0.5f + 200.0f, COURT_LENGTH * 0.5f + 30.0f,
		0.0f, COURT_LENGTH * 0.5f + 220.0f, COURT_WIDTH * 0.5f + 120.0f, 80.0f, 1600.0f, 3 },
};

typedef struct {
	qboolean	active;
	int			game;				// PONG_TABLE, PONG_COURT
	vec3_t		origin;
	float		yaw;
	int			guest;				// the client at the far end, -1: nobody
	int			heard;
	int			score[2];			// of the owner and of the guest (on a court COURT_SCORE)
	int			server;				// 0 the owner, 1 the guest
	int			overTime;			// a game was won then, 0: it goes on
	// the ball, in the coordinates of the table: x along it (the owner at -x), y to its left, z up from the floor
	qboolean	ballLive;
	vec3_t		ball, ballVel;
	int			hitter;				// who hit it last: 0 the owner, 1 the guest
	int			bounces;			// on the half of the other one since the hit
	vec3_t		paddle[2];			// as the paddles said (the own one is where the mouse put it)
	qboolean	paddleSeen[2];
	int			swingTime[2];		// when the rackets of a court hit last
} pongTable_t;

static struct {
	pongTable_t	tables[MAX_CLIENTS];	// by the client who put it down
	int			table;					// the table this player plays at (its owner), -1: none
	int			side;					//	at its near end (0) or its far end (1)
	float		paddleY, paddleZ;		// where the mouse put the own paddle, in the coordinates of the table
	float		paddleSpeed;			// how fast the mouse moved it sideways, units a second
	int			paddleTime, aliveTime;
	int			joinTime;				// when this player asked to play at the far end
	qboolean	useDown;				// Use was held the frame before
	qhandle_t	models[PONG_GAMES], paddleModel, racketModel, ballShader;
	sfxHandle_t	hitSounds[PONG_GAMES], bounceSounds[PONG_GAMES];
	qboolean	registered;
} pong;

static void Pong_Register( void ) {
	if ( pong.registered )
		return;
	pong.registered = qtrue;
	pong.models[PONG_TABLE] = trap->R_RegisterModel( pongGames[PONG_TABLE].model );
	pong.models[PONG_COURT] = trap->R_RegisterModel( pongGames[PONG_COURT].model );
	pong.paddleModel = trap->R_RegisterModel( "models/map_objects/vision/pingpong_paddle.md3" );
	pong.racketModel = trap->R_RegisterModel( "models/map_objects/vision/tennis_racket.md3" );
	pong.ballShader = trap->R_RegisterShader( "gfx/vision/toys/pingball" );
	pong.hitSounds[PONG_TABLE] = trap->S_RegisterSound( "sound/vision/toys/pong_hit.wav" );
	pong.bounceSounds[PONG_TABLE] = trap->S_RegisterSound( "sound/vision/toys/pong_bounce.wav" );
	pong.hitSounds[PONG_COURT] = trap->S_RegisterSound( "sound/vision/toys/tennis_hit.wav" );
	pong.bounceSounds[PONG_COURT] = trap->S_RegisterSound( "sound/vision/toys/tennis_bounce.wav" );
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

static const pongGame_t *Pong_Game( const pongTable_t *t ) {
	return &pongGames[t->game];
}

static void Pong_SendTable( void ) {
	const pongTable_t *t = &pong.tables[cg.clientNum];

	Pong_Send( TOY_TABLE, t->origin, (int)AngleNormalize180( t->yaw ), t->guest < 0 ? MAX_CLIENTS : t->guest, t->game );
	pong.aliveTime = cg.time ? cg.time : 1;
}

static void Pong_SendScore( const pongTable_t *t ) {
	vec3_t mid;

	VectorCopy( t->origin, mid );
	mid[2] += Pong_Game( t )->height;
	Pong_Send( TOY_PONG_SCORE, mid, t->score[0], t->score[1], t->server );
}

// Who serves after these points: at the table every two points the other one, at ten all every point; on a court
// every game the other one
static int Pong_Server( const pongTable_t *t ) {
	const int win = Pong_Game( t )->win;
	const int total = t->score[0] + t->score[1];

	if ( t->game == PONG_COURT )
		return ( COURT_GAMES( t->score[0] ) + COURT_GAMES( t->score[1] ) ) & 1;
	if ( t->score[0] >= win - 1 && t->score[1] >= win - 1 )
		return total & 1;
	return ( total / 2 ) & 1;
}

static qboolean Pong_Won( const pongTable_t *t ) {
	const int win = Pong_Game( t )->win;

	if ( t->game == PONG_COURT )
		return (qboolean)( COURT_GAMES( t->score[0] ) >= win || COURT_GAMES( t->score[1] ) >= win );
	return (qboolean)( ( t->score[0] >= win || t->score[1] >= win ) && abs( t->score[0] - t->score[1] ) >= 2 );
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

// this player plays no more (the mouse is caught at a table only)
static void Pong_Stop( void ) {
	if ( pong.table >= 0 && pong.tables[pong.table].game == PONG_TABLE )
		Pong_Catch( qfalse );
	pong.table = -1;
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
	Pong_Stop();
}

static void Pong_Start( int table, int side ) {
	pong.table = table;
	pong.side = side;
	pong.paddleY = 0.0f;
	pong.paddleZ = PONG_HEIGHT + 10.0f;
	pong.paddleSpeed = 0.0f;
	pong.useDown = qtrue;
	if ( pong.tables[table].game == PONG_TABLE )
		Pong_Catch( qtrue );
}

/*
=================
Pong_Place

A new table or court of this player at this yaw, the near end in front of his feet: qtrue if it stands on a flat
floor with room for it (the level only: a player who stands in the way steps aside). A court has to be on the floor
under all the corners of its lines too, no step or hole.
=================
*/
static qboolean Pong_Place( pongTable_t *t, float yaw ) {
	const pongGame_t	*g = Pong_Game( t );
	const float			r = g->width * 0.5f;
	const vec3_t		mins = { -r, -r, 8.0f }, maxs = { r, r, g->height + g->net };
	vec3_t				angles, forward, local, start, end;
	trace_t				tr;
	int					sx, sy;

	VectorSet( angles, 0.0f, yaw, 0.0f );
	AngleVectors( angles, forward, NULL, NULL );
	VectorMA( cg.predictedPlayerState.origin, g->standX, forward, start );
	start[2] += 16.0f;
	VectorCopy( start, end );
	end[2] -= 96.0f;
	CG_Trace( &tr, start, NULL, NULL, end, cg.clientNum, MASK_PLAYERSOLID );
	if ( tr.fraction >= 1.0f || tr.startsolid || tr.plane.normal[2] < 0.9f )
		return qfalse;
	VectorCopy( tr.endpos, t->origin );
	t->yaw = AngleNormalize360( yaw );
	// (a square as wide as it swept along it covers it at any yaw)
	VectorSet( local, -g->length * 0.5f + r, 0.0f, 1.0f );
	Pong_ToWorld( t, local, start );
	VectorSet( local, g->length * 0.5f - r, 0.0f, 1.0f );
	Pong_ToWorld( t, local, end );
	CG_Trace( &tr, start, mins, maxs, end, cg.clientNum, MASK_SOLID );
	if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f )
		return qfalse;
	if ( t->game != PONG_COURT )
		return qtrue;
	for ( sx = -1; sx <= 1; sx += 2 ) {
		for ( sy = -1; sy <= 1; sy += 2 ) {
			VectorSet( local, sx * g->length * 0.5f, sy * g->width * 0.5f, 12.0f );
			Pong_ToWorld( t, local, start );
			VectorCopy( start, end );
			end[2] -= 24.0f;
			CG_Trace( &tr, start, NULL, NULL, end, cg.clientNum, MASK_PLAYERSOLID );
			if ( tr.fraction >= 1.0f || tr.startsolid || fabsf( tr.endpos[2] - t->origin[2] ) > 3.0f )
				return qfalse;
		}
	}
	return qtrue;
}

/*
=================
Pong_Command

v_pingpong and v_tennis: leaves the game, or plays at the far end of a table or court near that has room, or puts
a new one down in front of the player
=================
*/
static void Pong_Command( int game ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const pongGame_t	*g = &pongGames[game];
	const int			tries = ( game == PONG_COURT ) ? 24 : 1;
	pongTable_t			*t;
	vec3_t				local;
	int					i;

	if ( pong.table >= 0 ) {
		Pong_Leave();
		return;
	}
	if ( !CG_ToyCanUse() || cg.clientNum < 0 || cg.clientNum >= MAX_CLIENTS )
		return;
	Pong_Register();
	// at the far end of a table of somebody else (on the far half of a court or behind it), and it is free
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		t = &pong.tables[i];
		if ( !t->active || t->game != game || i == cg.clientNum || t->guest >= 0 )
			continue;
		Pong_ToLocal( t, ps->origin, local );
		if ( local[0] > g->joinNear && local[0] < g->joinFar && fabsf( local[1] ) < g->joinSide && fabsf( local[2] ) < 64.0f ) {
			Pong_Send( TOY_PONG_JOIN, t->origin, i, 0, 0 );
			t->guest = cg.clientNum;
			pong.joinTime = cg.time;
			Pong_Start( i, 1 );
			return;
		}
	}
	// a new one in front of the feet; a court turns round the player, 15 degrees either way at a time, until it fits
	t = &pong.tables[cg.clientNum];
	memset( t, 0, sizeof( *t ) );
	t->game = game;
	for ( i = 0; i < tries; i++ ) {
		const float turn = ( ( i + 1 ) / 2 ) * 15.0f * ( ( i & 1 ) ? 1.0f : -1.0f );

		if ( Pong_Place( t, cg.refdef.viewangles[YAW] + turn ) )
			break;
	}
	if ( i == tries ) {
		Com_Printf( "No room for a %s here\n", g->name );
		return;
	}
	t->active = qtrue;
	t->guest = -1;
	t->heard = cg.time;
	Pong_Start( cg.clientNum, 0 );
	Pong_SendTable();
}

void CG_PingPong_f( void ) {
	Pong_Command( PONG_TABLE );
}

void CG_Tennis_f( void ) {
	Pong_Command( PONG_COURT );
}

// qtrue: the camera stands where the own player is, at a table (on a court he is seen from behind)
qboolean CG_PongPlaying( void ) {
	return (qboolean)( pong.table >= 0 && pong.tables[pong.table].game == PONG_TABLE );
}

// a toy of table tennis of somebody else, from the game module or the masterserver
void CG_PongHeard( int client, int kind, const vec3_t origin, const vec3_t velocity ) {
	pongTable_t	*t;
	int			side, table;

	Pong_Register();
	switch ( kind ) {
	case TOY_TABLE:
		t = &pong.tables[client];
		if ( !t->active || t->game != (int)velocity[2] ) {
			// (a table this player played at became a court: the end of that game was not heard)
			if ( pong.table == client )
				Pong_Stop();
			memset( t, 0, sizeof( *t ) );
			t->active = qtrue;
			t->game = (int)velocity[2] ? PONG_COURT : PONG_TABLE;
		}
		VectorCopy( origin, t->origin );
		t->yaw = velocity[0];
		t->guest = ( (int)velocity[1] >= MAX_CLIENTS ) ? -1 : (int)velocity[1];
		t->heard = cg.time;
		// the owner gave the far end to somebody else (what he said before he heard the own wish does not count)
		if ( pong.table == client && pong.side == 1 && t->guest != cg.clientNum ) {
			if ( cg.time - pong.joinTime < PONG_JOIN_MSEC && cg.time >= pong.joinTime )
				t->guest = cg.clientNum;
			else
				Pong_Stop();
		}
		return;
	case TOY_TABLE_GONE:
		if ( pong.table == client )
			Pong_Stop();
		pong.tables[client].active = qfalse;
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
		const float gravity = Pong_Game( t )->gravity;

		Pong_ToLocal( t, origin, t->ball );
		Pong_DirToLocal( t, velocity, t->ballVel );
		// (it flew on while the hit was on its way here)
		VectorMA( t->ball, late, t->ballVel, t->ball );
		t->ball[2] -= 0.5f * gravity * late * late;
		t->ballVel[2] -= gravity * late;
		t->hitter = side;
		t->bounces = 0;
		t->ballLive = qtrue;
		t->swingTime[side] = cg.time ? cg.time : 1;
		trap->S_StartSound( (float *)origin, client, CHAN_AUTO, pong.hitSounds[t->game] );
	} else if ( kind == TOY_PONG_SCORE ) {
		t->score[0] = (int)velocity[0];
		t->score[1] = (int)velocity[1];
		t->server = (int)velocity[2];
		t->ballLive = qfalse;
		t->overTime = Pong_Won( t ) ? ( cg.time ? cg.time : 1 ) : 0;
	}
}

// A point on a court: four win a game with two ahead, at four all it is deuce (three all) again
static void Pong_CourtPoint( pongTable_t *t, int winner ) {
	int games[2], points[2], i;

	for ( i = 0; i < 2; i++ ) {
		games[i] = COURT_GAMES( t->score[i] );
		points[i] = COURT_POINTS( t->score[i] );
	}
	points[winner]++;
	if ( points[winner] >= 4 && points[winner] - points[!winner] >= 2 ) {
		games[winner]++;
		points[0] = points[1] = 0;
	} else if ( points[0] == 4 && points[1] == 4 ) {
		points[0] = points[1] = 3;
	}
	for ( i = 0; i < 2; i++ )
		t->score[i] = COURT_SCORE( games[i], points[i] );
}

// this player, who the ball flew to, says who got the point
static void Pong_Point( pongTable_t *t, int winner ) {
	t->ballLive = qfalse;
	if ( t->game == PONG_COURT )
		Pong_CourtPoint( t, winner );
	else
		t->score[winner]++;
	t->server = Pong_Server( t );
	t->overTime = Pong_Won( t ) ? ( cg.time ? cg.time : 1 ) : 0;
	Pong_SendScore( t );
}

// The own hit sends the ball toward target (on the face of the other half) in the shortest arc that clears the net
static void Pong_Launch( pongTable_t *t, const vec3_t target ) {
	const pongGame_t	*g = Pong_Game( t );
	vec3_t				world, worldVel;
	float				flight;

	for ( flight = g->flightMin; flight < g->flightMax; flight += 0.05f ) {
		const float	vz = ( target[2] - t->ball[2] + 0.5f * g->gravity * flight * flight ) / flight;
		float		toNet, atNet;

		// (a player who ran past the net has none between him and the target)
		if ( t->ball[0] * target[0] >= 0.0f )
			break;
		toNet = t->ball[0] / ( t->ball[0] - target[0] ) * flight;
		atNet = t->ball[2] + vz * toNet - 0.5f * g->gravity * toNet * toNet;
		if ( atNet > g->height + g->net + g->ball * 2.5f )
			break;
	}
	VectorSet( t->ballVel, ( target[0] - t->ball[0] ) / flight, ( target[1] - t->ball[1] ) / flight,
		( target[2] - t->ball[2] + 0.5f * g->gravity * flight * flight ) / flight );
	t->hitter = pong.side;
	t->swingTime[pong.side] = cg.time ? cg.time : 1;
	t->bounces = 0;
	t->ballLive = qtrue;
	Pong_ToWorld( t, t->ball, world );
	Pong_DirToWorld( t, t->ballVel, worldVel );
	worldVel[0] = Com_Clamp( -TOY_MAX_SPEED + 1, TOY_MAX_SPEED - 1, worldVel[0] );
	worldVel[1] = Com_Clamp( -TOY_MAX_SPEED + 1, TOY_MAX_SPEED - 1, worldVel[1] );
	worldVel[2] = Com_Clamp( -TOY_MAX_SPEED + 1, TOY_MAX_SPEED - 1, worldVel[2] );
	Pong_Send( TOY_PONG_BALL, world, (int)worldVel[0], (int)worldVel[1], (int)worldVel[2] );
	trap->S_StartSound( world, cg.clientNum, CHAN_AUTO, pong.hitSounds[t->game] );
}

// The own paddle hits the ball toward a place on the other half. Where it meets the paddle and how fast the paddle
// moves sideways steer it, a ball hit late goes long.
static void Pong_Hit( pongTable_t *t, const vec3_t paddle ) {
	const float	end = Pong_End( pong.side );
	vec3_t		target;

	target[0] = -end * Com_Clamp( 12.0f, PONG_LENGTH * 0.5f - 6.0f, PONG_LENGTH * 0.3f + ( paddle[2] - t->ball[2] ) * 1.5f );
	target[1] = Com_Clamp( -PONG_WIDTH * 0.5f + 4.0f, PONG_WIDTH * 0.5f - 4.0f, ( t->ball[1] - paddle[1] ) * 3.0f - end * pong.paddleSpeed * 0.04f );
	target[2] = PONG_HEIGHT + Pong_Game( t )->ball;
	Pong_Launch( t, target );
}

// The own racket hits the ball where the player looks: across by the yaw, long by the pitch (looking up goes long).
// Too far off goes out.
static void Pong_CourtHit( pongTable_t *t ) {
	const pongGame_t	*g = Pong_Game( t );
	const float			end = Pong_End( pong.side );
	const float			pitch = AngleNormalize180( cg.predictedPlayerState.viewangles[PITCH] );
	vec3_t				angles, forward, dir, target;

	VectorSet( angles, 0.0f, cg.predictedPlayerState.viewangles[YAW], 0.0f );
	AngleVectors( angles, forward, NULL, NULL );
	Pong_DirToLocal( t, forward, dir );
	target[0] = -end * Com_Clamp( 50.0f, g->length * 0.5f + 50.0f, g->length * 0.3f - pitch * 4.0f );
	if ( -end * dir[0] > 0.25f )
		target[1] = t->ball[1] + ( target[0] - t->ball[0] ) / dir[0] * dir[1];
	else
		target[1] = t->ball[1] * 0.5f;
	target[1] = Com_Clamp( -g->width * 0.5f - 50.0f, g->width * 0.5f + 50.0f, target[1] );
	target[2] = g->height + g->ball;
	Pong_Launch( t, target );
}

// where the own paddle or racket is, in the coordinates of the table or court
static void Pong_OwnPaddle( const pongTable_t *t, vec3_t out ) {
	vec3_t angles, right, world;

	if ( t->game == PONG_TABLE ) {
		VectorSet( out, Pong_End( pong.side ) * ( PONG_LENGTH * 0.5f + PONG_PADDLE_X ), pong.paddleY, pong.paddleZ );
		return;
	}
	VectorSet( angles, 0.0f, cg.predictedPlayerState.viewangles[YAW], 0.0f );
	AngleVectors( angles, NULL, right, NULL );
	VectorMA( cg.predictedPlayerState.origin, COURT_RACKET_SIDE, right, world );
	Pong_ToLocal( t, world, out );
}

// qtrue: the own racket is near enough to the ball on a court (around it over the floor, from the feet to above the head)
static qboolean Pong_CourtReach( const pongTable_t *t ) {
	vec3_t racket;

	Pong_OwnPaddle( t, racket );
	return (qboolean)( Square( t->ball[0] - racket[0] ) + Square( t->ball[1] - racket[1] ) < Square( COURT_REACH )
		&& t->ball[2] > racket[2] + DEFAULT_MINS_2 - 2.0f && t->ball[2] < racket[2] + DEFAULT_MAXS_2 + 16.0f );
}

/*
=================
Pong_Fly

The ball of a table flies on, bounces on the table, stops at the net. The player it flies to hits it back with his
paddle or judges the point.
=================
*/
static void Pong_Fly( pongTable_t *t, int table, float dt ) {
	const pongGame_t	*g = Pong_Game( t );
	const int			receiver = !t->hitter;
	const qboolean		judge = (qboolean)( pong.table == table && pong.side == receiver );
	const float			recEnd = Pong_End( receiver );
	const float			face = g->height + g->ball;
	vec3_t				paddle, world, meet;
	float				oldX, oldZ;

	while ( dt > 0.0f && t->ballLive ) {
		const float step = Q_min( dt, 0.01f );

		dt -= step;
		oldX = t->ball[0];
		oldZ = t->ball[2];
		VectorMA( t->ball, step, t->ballVel, t->ball );
		t->ballVel[2] -= g->gravity * step;
		// the net: it falls on the side it came from
		if ( ( oldX < 0.0f ) != ( t->ball[0] < 0.0f ) && t->ball[2] < g->height + g->net + g->ball
			&& fabsf( t->ball[1] ) < g->width * 0.5f + g->netSide ) {
			t->ball[0] = oldX;
			t->ballVel[0] *= -0.15f;
			t->ballVel[1] *= 0.3f;
		}
		// the table, or the court inside its lines (it went down through the face in this step)
		if ( t->ballVel[2] < 0.0f && t->ball[2] <= face && oldZ >= face - 0.01f
			&& fabsf( t->ball[0] ) <= g->length * 0.5f && fabsf( t->ball[1] ) <= g->width * 0.5f ) {
			const int half = ( t->ball[0] > 0.0f ) ? 1 : 0;

			t->ball[2] = face;
			t->ballVel[2] = -t->ballVel[2] * g->bounce;
			Pong_ToWorld( t, t->ball, world );
			trap->S_StartSound( world, ENTITYNUM_WORLD, CHAN_AUTO, pong.bounceSounds[t->game] );
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
		// the own paddle, when the ball comes to it, or the own racket, when the ball comes near it
		if ( judge && t->game == PONG_TABLE ) {
			Pong_OwnPaddle( t, paddle );
			VectorSet( meet, paddle[0], t->ball[1], t->ball[2] );
			if ( recEnd * t->ballVel[0] > 0.0f && recEnd * t->ball[0] >= recEnd * paddle[0] - 2.0f
				&& recEnd * oldX < recEnd * paddle[0] + 2.0f && Distance( meet, paddle ) < PONG_REACH ) {
				Pong_Hit( t, paddle );
				return;
			}
		} else if ( judge && recEnd * t->ballVel[0] > 0.0f && Pong_CourtReach( t ) ) {
			Pong_CourtHit( t );
			return;
		}
		// down on the floor (outside the lines of a court) or far off: in if it bounced on the half of the one it
		// flew to, out if not
		if ( t->ball[2] < g->ball || fabsf( t->ball[0] ) > g->length * 0.5f + g->farX || fabsf( t->ball[1] ) > g->farY ) {
			if ( judge ) {
				Pong_Point( t, t->bounces ? t->hitter : receiver );
			} else {
				t->ballLive = qfalse;
			}
			return;
		}
	}
}

// qtrue: the own player stands on the own half of a court
static qboolean Pong_OwnHalf( const pongTable_t *t ) {
	vec3_t local;

	Pong_ToLocal( t, cg.predictedPlayerState.origin, local );
	return (qboolean)( Pong_End( pong.side ) * local[0] > 0.0f );
}

// Once a frame on a court: the player left it if he died, went far off or opened a menu; Use on the own half serves
static qboolean Pong_CourtFrame( pongTable_t *t ) {
	usercmd_t	cmd;
	qboolean	use;

	if ( !CG_ToyCanUse() || Distance( cg.predictedPlayerState.origin, t->origin ) > COURT_AWAY ) {
		Pong_Leave();
		return qfalse;
	}
	use = (qboolean)( trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) && ( cmd.buttons & BUTTON_USE ) );
	if ( use && !pong.useDown && !t->ballLive && !t->overTime && t->guest >= 0 && t->server == pong.side && Pong_OwnHalf( t ) ) {
		// the serve: the ball is thrown up over the racket and hit
		Pong_OwnPaddle( t, t->ball );
		t->ball[2] += DEFAULT_MAXS_2 + 8.0f;
		Pong_CourtHit( t );
	}
	pong.useDown = use;
	if ( pong.side == 0 && ( !pong.aliveTime || cg.time - pong.aliveTime >= PONG_ALIVE_MSEC || cg.time < pong.aliveTime ) )
		Pong_SendTable();
	return qtrue;
}

/*
=================
CG_PongFrame

Once a frame before the view is worked out: the balls fly, the own paddle and table are said to the others, a
table nobody heard of for long is gone. qtrue: this player plays, the view is his end of the table or from behind
him on a court.
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
			if ( pong.table == i )
				Pong_Stop();
			t->active = qfalse;
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
	if ( pong.tables[pong.table].game == PONG_COURT )
		return Pong_CourtFrame( &pong.tables[pong.table] );
	// a menu or the console took the keys, the player died or left the game
	if ( !CG_ToyCanUse() || !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		Pong_Leave();
		return qfalse;
	}
	pong.paddleSpeed *= Q_max( 0.0f, 1.0f - dt * 8.0f );
	if ( !pong.paddleTime || cg.time - pong.paddleTime >= PONG_PADDLE_MSEC || cg.time < pong.paddleTime ) {
		vec3_t local, world;

		Pong_OwnPaddle( &pong.tables[pong.table], local );
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

	if ( pong.table < 0 || pong.tables[pong.table].game != PONG_TABLE )
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

	if ( !CG_PongPlaying() )
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

	if ( !CG_PongPlaying() )
		return qfalse;
	t = &pong.tables[pong.table];
	if ( !down )
		return qtrue;
	if ( key == A_ESCAPE ) {
		Pong_Leave();
	} else if ( key == A_MOUSE1 && !t->ballLive && !t->overTime && t->guest >= 0 && t->server == pong.side ) {
		vec3_t paddle;

		// the serve: the ball is thrown up in front of the paddle and hit
		Pong_OwnPaddle( t, paddle );
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

// A racket in the right hand of a player of a court, its head up and forward, swung across him after a hit
static void Pong_AddRacket( const pongTable_t *t, int client, int side ) {
	centity_t			*cent = &cg_entities[client];
	const clientInfo_t	*ci = &cgs.clientinfo[client];
	refEntity_t			ent;
	vec3_t				angles, right;
	mdxaBone_t			matrix;
	float				turn = -25.0f, pitch = 35.0f;

	if ( !pong.racketModel || ( client != cg.clientNum && !cent->currentValid ) )
		return;
	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_MODEL;
	ent.hModel = pong.racketModel;
	if ( cent->ghoul2 && ci->bolt_rhand >= 0
		&& trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_rhand, &matrix, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, ent.origin );
	} else {
		VectorSet( angles, 0.0f, cent->lerpAngles[YAW], 0.0f );
		AngleVectors( angles, NULL, right, NULL );
		VectorMA( cent->lerpOrigin, COURT_RACKET_SIDE, right, ent.origin );
	}
	if ( t->swingTime[side] && cg.time - t->swingTime[side] < COURT_SWING_MSEC && cg.time >= t->swingTime[side] ) {
		const float frac = (float)( cg.time - t->swingTime[side] ) / COURT_SWING_MSEC;

		turn = -80.0f + 150.0f * frac;
		pitch = 70.0f;
	}
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	VectorSet( angles, pitch, cent->lerpAngles[YAW] + turn, 0.0f );
	AnglesToAxis( angles, ent.axis );
	trap->R_AddRefEntityToScene( &ent );
}

// While the scene is built: every table and court, its paddles or rackets and its ball
void CG_PongScene( void ) {
	static const vec3_t	zero = { 0.0f, 0.0f, 0.0f };
	int					i, side;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const pongTable_t *t = &pong.tables[i];

		if ( !t->active || ( i != cg.clientNum && !v_toys.integer ) )
			continue;
		Pong_Register();
		Pong_AddModel( t, pong.models[t->game], zero, 0.0f );
		for ( side = 0; side < 2; side++ ) {
			vec3_t paddle;

			if ( t->game == PONG_COURT ) {
				if ( side == 0 || t->guest >= 0 )
					Pong_AddRacket( t, side ? t->guest : i, side );
				continue;
			}
			if ( pong.table == i && pong.side == side ) {
				Pong_OwnPaddle( t, paddle );
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
			ent.radius = Pong_Game( t )->ball + 0.3f;
			ent.customShader = pong.ballShader;
			CG_ToyLight( ent.origin, light );
			// (a tennis ball is yellow)
			if ( t->game == PONG_COURT ) {
				light[0] *= 0.85f;
				light[2] *= 0.3f;
			}
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

In the 2D of the HUD: the points over every table and court near that has two players, and for the own game who
serves
=================
*/
// the points of a player in the game now on a court, as tennis says them
static const char *Pong_CourtPoints( const pongTable_t *t, int side ) {
	static const char	*names[] = { "0", "15", "30", "40" };
	const int			own = COURT_POINTS( t->score[side] ), other = COURT_POINTS( t->score[!side] );

	if ( own >= 3 && other >= 3 )
		return own > other ? "Ad" : "40";
	return names[Q_min( own, 3 )];
}

void CG_PongDraw( void ) {
	vec4_t			color = { 1.0f, 1.0f, 1.0f, 1.0f }, gold = { 1.0f, 0.85f, 0.3f, 1.0f };
	int				i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const pongTable_t	*t = &pong.tables[i];
		const pongGame_t	*g = Pong_Game( t );
		vec3_t				local, at;
		float				x, y;
		int					total[2];
		const char			*text;

		if ( !t->active || t->guest < 0 || ( i != cg.clientNum && !v_toys.integer ) )
			continue;
		VectorSet( local, 0.0f, 0.0f, g->labelZ );
		Pong_ToWorld( t, local, at );
		if ( Distance( at, cg.refdef.vieworg ) > g->labelRange || !CG_WorldCoordToScreenCoordFloat( at, &x, &y ) )
			continue;
		// (games on a court, points at a table)
		total[0] = ( t->game == PONG_COURT ) ? COURT_GAMES( t->score[0] ) : t->score[0];
		total[1] = ( t->game == PONG_COURT ) ? COURT_GAMES( t->score[1] ) : t->score[1];
		if ( t->overTime )
			text = va( "%s^7 wins %i : %i", cgs.clientinfo[total[0] > total[1] ? i : t->guest].name, total[0], total[1] );
		else if ( t->game == PONG_COURT )
			text = va( "%s^7  %i   %s : %s   %i  %s", cgs.clientinfo[i].name, total[0], Pong_CourtPoints( t, 0 ),
				Pong_CourtPoints( t, 1 ), total[1], cgs.clientinfo[t->guest].name );
		else
			text = va( "%s^7  %i : %i  %s", cgs.clientinfo[i].name, total[0], total[1], cgs.clientinfo[t->guest].name );
		CG_Text_Paint( x - CG_Text_Width( text, 0.6f, FONT_SMALL ) * 0.5f, y, 0.6f, t->overTime ? gold : color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
	}
	if ( pong.table >= 0 ) {
		const pongTable_t	*t = &pong.tables[pong.table];
		const char			*text = NULL;

		if ( t->guest < 0 )
			text = "Waiting for a player at the far end";
		else if ( t->ballLive || t->overTime || t->server != pong.side )
			text = NULL;
		else if ( t->game == PONG_TABLE )
			text = "Click to serve";
		else if ( Pong_OwnHalf( t ) )
			text = "Press Use to serve";
		else
			text = "Go back to your half to serve";
		if ( text )
			CG_Text_Paint( SCREEN_WIDTH * 0.5f - CG_Text_Width( text, 0.7f, FONT_MEDIUM ) * 0.5f, SCREEN_HEIGHT * 0.78f, 0.7f, color, text, 0, 0,
				ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}
