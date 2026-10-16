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

// cg_boombox.c -- the boombox of the client: boxes in the level that play songs of YouTube and YouTube Music. Not
// from JA+, see docs/japlus/boombox.md and game/boombox/bg_boombox.h.
//
// A box is no entity: the server tells this cgame where each one stands and what it plays (jpbox), and the box is
// drawn here, a model of the pack with two speakers that push out with the song. The songs come over the relay of
// the voice chat as the voices of speakers behind the players and are played there (voice/cg_voice.c): through
// the sound of the level of this engine where it takes them (s_spatial), as a sound with walls, ways round corners
// and the ring of the room, or on the sound device of the voice chat by side and distance.
//
// With the pointer (select/cg_select.c) a box is clicked for its card, and dragged to another place. Everything a
// player does is a client command, of which this file sends one a second at most: the flood protection of the
// server drops a command that comes within a second of another one.
//
// On a server without the game module of this tree the masterserver keeps the boxes of the server (codemp/master,
// ms_boombox.c), when it says it plays boxes (VOICE_FLAG_BOXES). Then all of it goes through the way of the voice
// chat to the master (voice/cg_voice.c): what the player does as a VOICE_BOX, what a box is now back the same way,
// and the boxes this client hears in its state, since the master does not know where anybody stands.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_boombox.h"

qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );		// cg_draw.c

#define BOX_ASK_MSEC		3000		// "jpbox hello" this often until the server answers,
#define BOX_MAX_ASKS		10			//	and this many times at most
#define BOX_SEND_MSEC		1100		// two commands of this file go out at least this far apart
#define BOX_PENDING			6			// commands that wait for their turn
#define BOX_MOVE_MSEC		2500		// a box this player put somewhere stands there this long without an answer of the server
#define BOX_SLIDE_MSEC		250			// a box somebody else moved slides there in this time
#define BOX_LABEL_RANGE		700.0f		// the title over a box is shown this near
#define BOX_LABEL_ABOVE		12.0f		// and this far above its top
#define BOX_BEAT_GAIN		4.0f		// the speakers push out by the loudness of the song times this, at most all the way
#define BOX_BEAT_FALL_MSEC	90.0f		// and come back in about this time
#define BOX_CONE_X			6.02f		// where the speakers sit in the model: in front of its middle,
#define BOX_CONE_Y			9.75f		//	to each side,
#define BOX_CONE_Z			9.0f		//	and above its floor: CONE_X, CONE_Y and CONE_Z of tools/boombox/build.py
#define BOX_MODEL			"models/map_objects/vision/boombox.md3"
#define BOX_CONE_MODEL		"models/map_objects/vision/boombox_cone.md3"

typedef struct cgBox_s {
	qboolean	inuse;
	int			rev;
	vec3_t		origin;			// where the server says it stands
	float		yaw;
	vec3_t		from;			// where it slides from to there,
	float		fromYaw;
	int			slideTime;		//	since when, 0: it does not
	int			volume, flags, length, waiting;
	int			msec, msecTime;	// how far into the song it was when the server said so, and cg.time then
	char		title[BOX_TITLE], next[BOX_TITLE];
	qboolean	moved;			// this player put it somewhere and the server did not answer yet
	vec3_t		movedTo;
	float		movedYaw;
	int			movedTime;
	float		beat;			// how far the speakers are out: follows the song
} cgBox_t;

static struct {
	qboolean	serverHas;		// g_boombox in the server info
	qboolean	asked;
	int			asks, askTime;
	qboolean	on;				// the server answered: it has boxes,
	qboolean	viaMaster;		//	or the masterserver keeps them, for a server without the game module of this tree
	qboolean	toldProtocol;
	int			most, range;
	char		pending[BOX_PENDING][BOX_ASK + 64];
	int			numPending;
	int			sendTime;
	qhandle_t	model, cone;
	qboolean	registered;
	// a box the pointer carries (select/cg_select.c)
	int			held;			// -1: none
	vec3_t		heldAt;
	float		heldYaw;
	qboolean	heldOk;			// it can be put there
} bx;

static cgBox_t	cgBoxes[BOX_MAX];

static qboolean	Box_Spot( const vec3_t start, const vec3_t dir, int self, vec3_t at, float *yaw );
static int		Box_Near( void );
static void		Box_Forget( void );

void CG_BoomboxInit( void ) {
	const qboolean serverHas = bx.serverHas;

	memset( &bx, 0, sizeof( bx ) );
	memset( cgBoxes, 0, sizeof( cgBoxes ) );
	bx.held = -1;
	bx.serverHas = serverHas;
}

void CG_BoomboxServerInfo( const char *value ) {
	bx.serverHas = atoi( value ) ? qtrue : qfalse;
}

/*
===============================================================================

What goes to the server

===============================================================================
*/

static void Box_SendPending( void ) {
	const int now = trap->Milliseconds();

	if ( !bx.numPending || ( bx.sendTime && now - bx.sendTime < BOX_SEND_MSEC ) )
		return;
	trap->SendClientCommand( bx.pending[0] );
	bx.numPending--;
	memmove( bx.pending[0], bx.pending[1], bx.numPending * sizeof( bx.pending[0] ) );
	bx.sendTime = now;
}

// A command for the server, after the ones that wait. One that begins like a waiting one (same: that many
// characters) takes its place: a volume clicked five times goes out once.
static void Box_Send( const char *cmd, int same ) {
	int i;

	for ( i = 0; same > 0 && i < bx.numPending; i++ ) {
		if ( !Q_strncmp( bx.pending[i], cmd, same ) ) {
			Q_strncpyz( bx.pending[i], cmd, sizeof( bx.pending[i] ) );
			return;
		}
	}
	if ( bx.numPending >= BOX_PENDING ) {
		trap->Print( "Boombox: slow down, the server takes one command a second\n" );
		return;
	}
	Q_strncpyz( bx.pending[bx.numPending++], cmd, sizeof( bx.pending[0] ) );
	Box_SendPending();
}

static qboolean Box_Valid( int box ) {
	return ( box >= 0 && box < BOX_MAX && cgBoxes[box].inuse ) ? qtrue : qfalse;
}

// the words of the client command boombox, by BOX_DO_*
static const char *const boxVerbs[] = { "", "play", "place", "move", "pause", "resume", "skip", "stop", "remove", "volume" };

/*
=================
Box_Do

What this player does with a box (BOX_DO_*): the client command boombox to a server of this tree, a VOICE_BOX to
the masterserver. at and yaw: where it goes (place, move, and play into box 255: a new box). value: the volume.
text: what to play.
=================
*/
static void Box_Do( int what, int box, const vec3_t at, float yaw, int value, const char *text ) {
	if ( bx.viaMaster ) {
		boxDo_t	d;
		int		k;

		memset( &d, 0, sizeof( d ) );
		d.what = what;
		d.box = box;
		Q_strncpyz( d.name, cgs.clientinfo[cg.clientNum].name, sizeof( d.name ) );
		if ( at ) {
			for ( k = 0; k < 3; k++ )
				d.origin[k] = (int)at[k];
			d.yaw = yaw;
		}
		d.volume = value;
		if ( text )
			Q_strncpyz( d.text, text, sizeof( d.text ) );
		if ( !CG_VoiceBoxSend( &d ) )
			trap->Print( "Boombox: no answer from the masterserver right now\n" );
		return;
	}
	switch ( what ) {
	case BOX_DO_PLAY:
		// (in quotes: a link has "//" in it, which ends a command outside of them)
		Box_Send( va( "boombox play %i \"%s\"", ( box == 255 ) ? -1 : box, text ), 0 );
		break;
	case BOX_DO_PLACE:
		Box_Send( va( "boombox place %i %i %i %i", (int)at[0], (int)at[1], (int)at[2], (int)yaw ), 0 );
		break;
	case BOX_DO_MOVE:
		Box_Send( va( "boombox move %i %i %i %i %i", box, (int)at[0], (int)at[1], (int)at[2], (int)yaw ),
			(int)strlen( va( "boombox move %i ", box ) ) );
		break;
	case BOX_DO_VOLUME:
		Box_Send( va( "boombox volume %i %i", box, value ), (int)strlen( va( "boombox volume %i ", box ) ) );
		break;
	default:
		Box_Send( va( "boombox %s %i", boxVerbs[what], box ), 0 );
		break;
	}
}

// pause, resume, skip, stop or remove, from the card of a box
void CG_BoomboxDo( int box, const char *what ) {
	int i;

	if ( !Box_Valid( box ) )
		return;
	for ( i = 1; i < (int)ARRAY_LEN( boxVerbs ); i++ ) {
		if ( !Q_stricmp( what, boxVerbs[i] ) ) {
			Box_Do( i, box, NULL, 0.0f, 0, NULL );
			return;
		}
	}
}

// louder or quieter for everybody by a tenth a step. Shown at once, the server says it to the others.
void CG_BoomboxLouder( int box, int steps ) {
	cgBox_t *b;

	if ( !Box_Valid( box ) )
		return;
	b = &cgBoxes[box];
	b->volume = Com_Clampi( 0, 100, ( b->volume + 5 ) / 10 * 10 + steps * 10 );
	Box_Do( BOX_DO_VOLUME, box, NULL, 0.0f, b->volume, NULL );
}

// a place for a new box in front of the player: looking down a little, on the floor there
static qboolean Box_InFront( vec3_t at, float *yaw ) {
	vec3_t dir;

	VectorSet( dir, cg.refdef.viewaxis[0][0], cg.refdef.viewaxis[0][1], 0.0f );
	VectorNormalize( dir );
	dir[2] = -0.6f;
	VectorNormalize( dir );
	return Box_Spot( cg.refdef.vieworg, dir, -1, at, yaw );
}

// What a player typed into the chat box opened for a box, or after "boombox play". Without a box: the one near,
// else a new one in front of him (a server of this tree finds both itself).
void CG_BoomboxAsk( int box, const char *text ) {
	char	asked[BOX_ASK];
	vec3_t	at;
	float	yaw;

	Q_strncpyz( asked, text, sizeof( asked ) );
	Q_strstrip( asked, "\"\n\r;", "'  " );
	if ( !bx.on ) {
		trap->Print( "This server has no boombox\n" );
		return;
	}
	if ( !Box_Valid( box ) && bx.viaMaster ) {
		box = Box_Near();
		if ( box < 0 ) {
			if ( !Box_InFront( at, &yaw ) ) {
				trap->Print( "Boombox: there is no floor for a boombox in front of you\n" );
				return;
			}
			Box_Do( BOX_DO_PLAY, 255, at, yaw, 0, asked );
			return;
		}
	}
	Box_Do( BOX_DO_PLAY, Box_Valid( box ) ? box : 255, NULL, 0.0f, 0, asked );
}


/*
===============================================================================

What comes from the server

===============================================================================
*/

// where a box is drawn now: in the pointer, where this player put it, on its way, or where it stands
static void Box_Where( int box, vec3_t at, float *yaw ) {
	const cgBox_t *b = &cgBoxes[box];

	if ( box == bx.held ) {
		VectorCopy( bx.heldAt, at );
		*yaw = bx.heldYaw;
	} else if ( b->moved ) {
		VectorCopy( b->movedTo, at );
		*yaw = b->movedYaw;
	} else if ( b->slideTime && cg.time - b->slideTime < BOX_SLIDE_MSEC ) {
		const float part = (float)( cg.time - b->slideTime ) / BOX_SLIDE_MSEC;

		at[0] = b->from[0] + ( b->origin[0] - b->from[0] ) * part;
		at[1] = b->from[1] + ( b->origin[1] - b->from[1] ) * part;
		at[2] = b->from[2] + ( b->origin[2] - b->from[2] ) * part;
		*yaw = LerpAngle( b->fromYaw, b->yaw, part );
	} else {
		VectorCopy( b->origin, at );
		*yaw = b->yaw;
	}
}

// a line of the server for the chat box
static void Box_Note( const char *text ) {
	char line[MAX_STRING_CHARS];

	Q_strncpyz( line, text, sizeof( line ) );
	trap->Print( "%s\n", line );
	CG_ChatAdd( CHAT_ALL, -1, line );
}

// a box is gone
static void Box_Gone( int n ) {
	if ( n == bx.held )
		bx.held = -1;
	memset( &cgBoxes[n], 0, sizeof( cgBoxes[n] ) );
	CG_VoiceBoxReset( n );
}

// what a box is now, from the server or the master
static void Box_Set( int n, int rev, const vec3_t origin, float yaw, int volume, int flags, int msec, int length, int waiting,
	const char *title, const char *next ) {
	cgBox_t *b = &cgBoxes[n];

	if ( !b->inuse ) {
		// a new box: nothing of the one before it is heard
		CG_VoiceBoxReset( n );
		b->slideTime = 0;
	} else if ( !VectorCompare( origin, b->origin ) || yaw != b->yaw ) {
		// moved: it slides there from where it is drawn now. Moved by this player: it is there already.
		Box_Where( n, b->from, &b->fromYaw );
		b->slideTime = cg.time ? cg.time : 1;
	}
	if ( b->moved && Distance( origin, b->movedTo ) < 2.0f ) {
		b->moved = qfalse;
		b->slideTime = 0;
	}
	b->inuse = qtrue;
	b->rev = rev;
	VectorCopy( origin, b->origin );
	b->yaw = yaw;
	b->volume = Com_Clampi( 0, 100, volume );
	b->flags = flags;
	b->msec = msec;
	b->msecTime = cg.time;
	b->length = length;
	b->waiting = waiting;
	Q_strncpyz( b->title, title, sizeof( b->title ) );
	Q_strncpyz( b->next, next, sizeof( b->next ) );
}

/*
=================
CG_BoomboxServerCommand

jpbox on|set|gone|note, see bg_boombox.h
=================
*/
void CG_BoomboxServerCommand( void ) {
	char	what[8], title[BOX_TITLE];
	vec3_t	origin;
	int		n;

	Q_strncpyz( what, CG_Argv( 1 ), sizeof( what ) );
	if ( !Q_stricmp( what, "on" ) ) {
		if ( atoi( CG_Argv( 2 ) ) != BOX_PROTOCOL ) {
			if ( !bx.toldProtocol )
				trap->Print( "Boombox: the server speaks version %s of it, this client version %i\n", CG_Argv( 2 ), BOX_PROTOCOL );
			bx.toldProtocol = qtrue;
			bx.asks = BOX_MAX_ASKS;
			return;
		}
		bx.on = qtrue;
		bx.viaMaster = qfalse;
		bx.most = atoi( CG_Argv( 3 ) );
		bx.range = atoi( CG_Argv( 4 ) );
		return;
	}
	if ( !Q_stricmp( what, "note" ) ) {
		Box_Note( CG_Argv( 2 ) );
		return;
	}
	n = atoi( CG_Argv( 2 ) );
	if ( n < 0 || n >= BOX_MAX || bx.viaMaster )
		return;
	if ( !Q_stricmp( what, "gone" ) ) {
		Box_Gone( n );
		return;
	}
	if ( Q_stricmp( what, "set" ) )
		return;
	VectorSet( origin, (float)atof( CG_Argv( 4 ) ), (float)atof( CG_Argv( 5 ) ), (float)atof( CG_Argv( 6 ) ) );
	// (CG_Argv has one buffer: the title is copied before the next one is read)
	Q_strncpyz( title, CG_Argv( 13 ), sizeof( title ) );
	Box_Set( n, atoi( CG_Argv( 3 ) ), origin, (float)atof( CG_Argv( 7 ) ), atoi( CG_Argv( 8 ) ), atoi( CG_Argv( 9 ) ), atoi( CG_Argv( 10 ) ),
		atoi( CG_Argv( 11 ) ), atoi( CG_Argv( 12 ) ), title, CG_Argv( 14 ) );
}

/*
===============================================================================

Through the masterserver

===============================================================================
*/

// The master the voice chat reaches plays boxes, or it no longer does (the way to it closed). On a server of this
// tree the boxes are the server's own and none of this counts.
void CG_BoomboxMasterReady( qboolean ready ) {
	if ( ready && cg_boombox.integer && !bx.serverHas ) {
		bx.on = bx.viaMaster = qtrue;
		bx.range = BOX_RANGE;
		bx.most = BOX_MASTER_MAX;
	} else if ( !ready && bx.viaMaster ) {
		Box_Forget();
		bx.viaMaster = qfalse;
	}
}

// a VOICE_BOX of the master: what a box is now, that it is gone, or a line for the chat box
void CG_BoomboxFromMaster( const byte *data, int len ) {
	boxSay_t	say;
	vec3_t		origin;

	if ( !bx.viaMaster || !BG_BoxSayUnpack( data, len, &say ) )
		return;
	if ( say.what == BOX_SAY_NOTE ) {
		Box_Note( say.text );
		return;
	}
	if ( say.box < 0 || say.box >= BOX_MASTER_MAX )
		return;
	if ( say.what == BOX_SAY_GONE ) {
		if ( cgBoxes[say.box].inuse )
			Box_Gone( say.box );
		return;
	}
	VectorSet( origin, (float)say.origin[0], (float)say.origin[1], (float)say.origin[2] );
	Box_Set( say.box, say.rev, origin, say.yaw, say.volume, say.flags, say.msec, say.length, say.waiting, say.title, say.next );
}

// the boxes of the master this client hears, a bit each: the ones whose speakers are near enough the camera
int CG_BoomboxHearMask( void ) {
	const float	range = CG_BoomboxRange() * 1.1f;
	vec3_t		mouth;
	float		gain;
	int			i, mask = 0;

	if ( !bx.viaMaster || !cg_boombox.integer || cg_boomboxVolume.value <= 0.0f )
		return 0;
	for ( i = 0; i < BOX_MASTER_MAX; i++ ) {
		if ( !CG_VoiceBoxMuted( i ) && CG_BoomboxMouth( i, mouth, &gain ) && Distance( mouth, cg.refdef.vieworg ) <= range )
			mask |= 1 << i;
	}
	return mask;
}

/*
===============================================================================

Once a frame

===============================================================================
*/

// the boxes are gone for this player: switched off (cg_boombox 0), or the server stopped answering
static void Box_Forget( void ) {
	int i;

	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( cgBoxes[i].inuse )
			CG_VoiceBoxReset( i );
	}
	memset( cgBoxes, 0, sizeof( cgBoxes ) );
	bx.held = -1;
	bx.on = bx.asked = qfalse;
	bx.asks = 0;
}

void CG_BoomboxFrame( void ) {
	const int		now = trap->Milliseconds();
	const qboolean	wanted = ( cg_boombox.integer && !cg.demoPlayback ) ? qtrue : qfalse;
	int				i;

	if ( !wanted && bx.on ) {
		if ( !bx.viaMaster )
			Box_Send( "jpbox off", 0 );
		Box_Forget();
		bx.viaMaster = qfalse;
	}
	// switched on again where the master plays the boxes
	if ( wanted && !bx.on && !bx.serverHas && CG_VoiceMasterBoxes() )
		CG_BoomboxMasterReady( qtrue );
	// asked again while there is no answer: the flood protection of the server may have dropped it
	if ( wanted && bx.serverHas && !bx.on && cg.snap && bx.asks < BOX_MAX_ASKS && ( !bx.asked || now - bx.askTime >= BOX_ASK_MSEC ) ) {
		trap->SendClientCommand( va( "jpbox hello %i", BOX_PROTOCOL ) );
		bx.asked = qtrue;
		bx.askTime = bx.sendTime = now;
		bx.asks++;
	}
	Box_SendPending();

	for ( i = 0; i < BOX_MAX; i++ ) {
		cgBox_t		*b = &cgBoxes[i];
		const float	beat = Com_Clamp( 0.0f, 1.0f, CG_VoiceBoxBeat( i ) * BOX_BEAT_GAIN );

		if ( !b->inuse )
			continue;
		// the server did not take the move: it goes back to where it stands
		if ( b->moved && cg.time - b->movedTime > BOX_MOVE_MSEC ) {
			b->moved = qfalse;
			VectorCopy( b->movedTo, b->from );
			b->fromYaw = b->movedYaw;
			b->slideTime = cg.time ? cg.time : 1;
		}
		// out at once, back in a moment
		if ( beat > b->beat )
			b->beat = beat;
		else
			b->beat += ( beat - b->beat ) * ( 1.0f - expf( -cg.frametime / BOX_BEAT_FALL_MSEC ) );
	}
}

/*
===============================================================================

In the level

===============================================================================
*/

static void Box_Register( void ) {
	if ( bx.registered )
		return;
	bx.registered = qtrue;
	bx.model = trap->R_RegisterModel( BOX_MODEL );
	bx.cone = trap->R_RegisterModel( BOX_CONE_MODEL );
}

// one speaker, pushed out by the song: longer along the axis it faces, a little wider
static void Box_AddCone( const refEntity_t *body, float side, float beat ) {
	refEntity_t	cone = *body;
	const float	out = 1.0f + 1.4f * beat, wide = 1.0f + 0.06f * beat;

	cone.hModel = bx.cone;
	VectorMA( body->origin, BOX_CONE_X, body->axis[0], cone.origin );
	VectorMA( cone.origin, side * BOX_CONE_Y, body->axis[1], cone.origin );
	VectorMA( cone.origin, BOX_CONE_Z, body->axis[2], cone.origin );
	VectorCopy( cone.origin, cone.oldorigin );
	VectorScale( body->axis[0], out, cone.axis[0] );
	VectorScale( body->axis[1], wide, cone.axis[1] );
	VectorScale( body->axis[2], wide, cone.axis[2] );
	cone.nonNormalizedAxes = qtrue;
	trap->R_AddRefEntityToScene( &cone );
}

/*
=================
CG_BoomboxScene

The boxes into the scene, once a frame while the level is: the one under the pointer and the one whose card is
open with the outline of the pointer, the one in the pointer where it would be put.
=================
*/
void CG_BoomboxScene( void ) {
	refEntity_t	body;
	vec3_t		angles;
	int			i;

	if ( !cg_boombox.integer ) {
		return;
	}
	for ( i = 0; i < BOX_MAX; i++ ) {
		const cgBox_t *b = &cgBoxes[i];

		if ( !b->inuse ) {
			continue;
		}
		Box_Register();
		if ( !bx.model ) {
			return;
		}
		memset( &body, 0, sizeof( body ) );
		body.reType = RT_MODEL;
		body.hModel = bx.model;
		Box_Where( i, body.origin, &angles[YAW] );
		angles[PITCH] = angles[ROLL] = 0.0f;
		AnglesToAxis( angles, body.axis );
		VectorCopy( body.origin, body.oldorigin );
		VectorCopy( body.origin, body.lightingOrigin );
		body.lightingOrigin[2] += BOX_HEIGHT * 0.5f;
		body.renderfx = RF_LIGHTING_ORIGIN;
		body.shaderRGBA[0] = body.shaderRGBA[1] = body.shaderRGBA[2] = body.shaderRGBA[3] = 255;
		// with an outline it is drawn after it and on it, as a player is (select/cg_select.c)
		if ( CG_SelectBoxOutlined( i ) ) {
			body.renderfx |= RF_FORCEPOST | RF_AFTERGLOW;
		}
		trap->R_AddRefEntityToScene( &body );
		if ( bx.cone ) {
			Box_AddCone( &body, 1.0f, b->beat );
			Box_AddCone( &body, -1.0f, b->beat );
		}
		CG_SelectBoxOutline( i, &body );
	}
}

/*
=================
CG_BoomboxDraw

Over every box near enough that is in sight: what it plays, or that it is paused or looks a song up. Not over the
one the pointer is on or whose card is open: the pointer names that one itself.
=================
*/
void CG_BoomboxDraw( void ) {
	const vec4_t	back = { 0.0f, 0.0f, 0.0f, 0.45f };
	const int		font = Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2;
	int				i;

	if ( !cg_boombox.integer || !cg.snap ) {
		return;
	}
	for ( i = 0; i < BOX_MAX; i++ ) {
		const cgBox_t	*b = &cgBoxes[i];
		vec3_t			top;
		float			yaw, distance, x, y, scale, w, h;
		vec4_t			color = { 1.0f, 1.0f, 1.0f, 1.0f };
		char			text[BOX_TITLE + 32];
		trace_t			tr;

		if ( !b->inuse || i == bx.held || CG_SelectBoxOutlined( i ) ) {
			continue;
		}
		if ( b->flags & BOX_LOADING ) {
			Q_strncpyz( text, CG_VisionText( "Loading ..." ), sizeof( text ) );
		} else if ( b->flags & BOX_PLAYING ) {
			Q_strncpyz( text, b->title, sizeof( text ) );
			if ( b->flags & BOX_PAUSED ) {
				Q_strcat( text, sizeof( text ), va( "  (%s)", CG_VisionText( "Paused" ) ) );
			}
		} else if ( b->flags & BOX_LOOKING ) {
			Q_strncpyz( text, CG_VisionText( "Looking a song up ..." ), sizeof( text ) );
		} else {
			continue;
		}
		Box_Where( i, top, &yaw );
		top[2] += BOX_HEIGHT + BOX_LABEL_ABOVE;
		distance = Distance( top, cg.refdef.vieworg );
		if ( distance > BOX_LABEL_RANGE || !CG_WorldCoordToScreenCoordFloat( top, &x, &y ) ) {
			continue;
		}
		CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, top, cg.snap->ps.clientNum, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		scale = Com_Clamp( 0.35f, 0.6f, 0.6f * 250.0f / Q_max( distance, 1.0f ) );
		color[3] = Com_Clamp( 0.0f, 1.0f, ( BOX_LABEL_RANGE - distance ) / 150.0f );
		w = (float)CG_Text_Width( text, scale, font );
		h = 18.0f * scale;
		{
			vec4_t shade = { back[0], back[1], back[2], back[3] * color[3] };

			CG_FillRect( x - w * 0.5f - 4.0f, y - h - 2.0f, w + 8.0f, h + 4.0f, shade );
		}
		CG_Text_Paint( x - w * 0.5f, y - h, scale, color, text, 0, 0, 0, font );
	}
}

/*
===============================================================================

For the voice chat, which plays the songs

===============================================================================
*/

// The way to the relay is wanted for the songs, also with the voice chat off: on a server of this tree once it said
// it has boxes, on any other one to ask the masterserver for them.
qboolean CG_BoomboxWantsLink( void ) {
	return ( cg_boombox.integer && !cg.demoPlayback && ( bx.on || !bx.serverHas ) ) ? qtrue : qfalse;
}

// is the song of this box to be played
qboolean CG_BoomboxHears( int box ) {
	return ( cg_boombox.integer && cg_boomboxVolume.value > 0.0f && Box_Valid( box ) ) ? qtrue : qfalse;
}

// Where the sound of a box comes from, and how loud it is before its distance: as this player wants the songs
// (cg_boomboxVolume) and the box is set for everybody. qfalse: there is no such box.
qboolean CG_BoomboxMouth( int box, vec3_t mouth, float *gain ) {
	float yaw;

	if ( !Box_Valid( box ) )
		return qfalse;
	Box_Where( box, mouth, &yaw );
	mouth[2] += BOX_SPEAKER_HEIGHT;
	*gain = cg_boomboxVolume.value * cgBoxes[box].volume * 0.01f;
	return qtrue;
}

float CG_BoomboxRange( void ) {
	return ( bx.range > 0 ) ? (float)bx.range : (float)BOX_RANGE;
}

/*
===============================================================================

For the pointer

===============================================================================
*/

qboolean CG_BoomboxShown( int box ) {
	return ( cg_boombox.integer && bx.model && Box_Valid( box ) ) ? qtrue : qfalse;
}

/*
=================
CG_BoomboxPick

The box a ray from start meets first, nearer than *bestT, which is then how far away it is. -1: none. The box is a
block turned by its yaw, a little larger than the model for the pointer.
=================
*/
int CG_BoomboxPick( const vec3_t start, const vec3_t dir, float *bestT ) {
	const vec3_t	lo = { -BOX_HALF_DEPTH - 2.0f, -BOX_HALF_WIDTH - 2.0f, -1.0f };
	const vec3_t	hi = { BOX_HALF_DEPTH + 2.0f, BOX_HALF_WIDTH + 2.0f, BOX_HEIGHT + 2.0f };
	int				i, k, found = -1;

	for ( i = 0; i < BOX_MAX; i++ ) {
		vec3_t	at, d, o, r;
		float	yaw, c, s, tNear = 0.0f, tFar = *bestT;

		if ( !CG_BoomboxShown( i ) || i == bx.held ) {
			continue;
		}
		Box_Where( i, at, &yaw );
		c = cosf( DEG2RAD( yaw ) );
		s = sinf( DEG2RAD( yaw ) );
		// the ray in the axes of the box: x out of its front, y to its left
		VectorSubtract( start, at, d );
		VectorSet( o, d[0] * c + d[1] * s, -d[0] * s + d[1] * c, d[2] );
		VectorSet( r, dir[0] * c + dir[1] * s, -dir[0] * s + dir[1] * c, dir[2] );
		for ( k = 0; k < 3; k++ ) {
			float t0, t1;

			if ( fabsf( r[k] ) < 0.0001f ) {
				if ( o[k] < lo[k] || o[k] > hi[k] ) {
					tNear = tFar + 1.0f;
				}
				continue;
			}
			t0 = ( lo[k] - o[k] ) / r[k];
			t1 = ( hi[k] - o[k] ) / r[k];
			if ( t0 > t1 ) {
				const float swap = t0;

				t0 = t1;
				t1 = swap;
			}
			tNear = Q_max( tNear, t0 );
			tFar = Q_min( tFar, t1 );
		}
		if ( tNear <= tFar && tNear < *bestT ) {
			*bestT = tNear;
			found = i;
		}
	}
	return found;
}

// what the card of a box stands beside, as the middle of a player
void CG_BoomboxMiddle( int box, vec3_t at ) {
	float yaw;

	Box_Where( box, at, &yaw );
}

qboolean CG_BoomboxInfo( int box, boomboxInfo_t *info ) {
	const cgBox_t *b;

	if ( !Box_Valid( box ) )
		return qfalse;
	b = &cgBoxes[box];
	info->flags = b->flags;
	info->volume = b->volume;
	info->length = b->length;
	info->msec = b->msec;
	// it counts on while it plays
	if ( ( b->flags & BOX_PLAYING ) && !( b->flags & ( BOX_PAUSED | BOX_LOADING ) ) )
		info->msec += cg.time - b->msecTime;
	info->msec = Com_Clampi( 0, b->length * 1000, info->msec );
	info->waiting = b->waiting;
	info->title = b->title;
	info->next = b->next;
	info->muted = CG_VoiceBoxMuted( box );
	return qtrue;
}

// "Play a song": the chat box opens for it, where a link can be pasted with Ctrl+V
void CG_BoomboxPrompt( int box ) {
	if ( !CG_ChatOpenFor( 3, box ) ) {
		trap->Print( "Boombox: type /boombox play \"<link of YouTube or YouTube Music, or what to look for>\" into the console\n" );
	}
}

/*
=================
Box_Spot

Where a box goes that is put where a ray from start meets the level: on the floor there, or under the place in
front of a wall it meets, as the server will put it (Box_Spot of g_boombox.c). qfalse: there is no floor, it is
out of reach of the eye of the player, or another box stands there. at is where it would be all the same.
=================
*/
static qboolean Box_Spot( const vec3_t start, const vec3_t dir, int self, vec3_t at, float *yaw ) {
	const vec3_t	mins = { -12.0f, -12.0f, 0.0f }, maxs = { 12.0f, 12.0f, 20.0f };
	vec3_t			end, eye, from;
	trace_t			tr;
	int				i;

	VectorCopy( cg.predictedPlayerState.origin, eye );
	eye[2] += cg.predictedPlayerState.viewheight;
	VectorMA( start, BOX_REACH + 256.0f, dir, end );
	CG_Trace( &tr, start, NULL, NULL, end, cg.predictedPlayerState.clientNum, MASK_SOLID );
	// down from a little before where it met the level
	VectorMA( tr.endpos, -8.0f, dir, from );
	from[2] += 12.0f;
	VectorCopy( from, end );
	end[2] -= 96.0f + 12.0f;
	CG_Trace( &tr, from, mins, maxs, end, ENTITYNUM_NONE, MASK_SOLID );
	VectorCopy( tr.endpos, at );
	at[0] = floorf( at[0] + 0.5f );
	at[1] = floorf( at[1] + 0.5f );
	at[2] = ceilf( at[2] );
	// turned to the player
	*yaw = RAD2DEG( atan2f( eye[1] - at[1], eye[0] - at[0] ) );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f || Distance( eye, at ) > BOX_REACH ) {
		return qfalse;
	}
	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( i != self && cgBoxes[i].inuse && Distance( cgBoxes[i].origin, at ) < 24.0f ) {
			return qfalse;
		}
	}
	return qtrue;
}

// the pointer takes a box: it follows the pointer over the floor until it is put down
void CG_BoomboxGrab( int box ) {
	if ( !Box_Valid( box ) )
		return;
	Box_Where( box, bx.heldAt, &bx.heldYaw );
	bx.held = box;
	bx.heldOk = qtrue;
}

// once a frame while a box is in the pointer: the ray from the eye through the pointer
void CG_BoomboxCarry( const vec3_t start, const vec3_t dir ) {
	if ( !Box_Valid( bx.held ) ) {
		bx.held = -1;
		return;
	}
	bx.heldOk = Box_Spot( start, dir, bx.held, bx.heldAt, &bx.heldYaw );
}

// put down where it is, if it can be (place), or back where it was
void CG_BoomboxDrop( qboolean place ) {
	cgBox_t *b;

	if ( !Box_Valid( bx.held ) ) {
		bx.held = -1;
		return;
	}
	b = &cgBoxes[bx.held];
	if ( place && bx.heldOk ) {
		b->moved = qtrue;
		VectorCopy( bx.heldAt, b->movedTo );
		b->movedYaw = bx.heldYaw;
		b->movedTime = cg.time;
		Box_Do( BOX_DO_MOVE, bx.held, bx.heldAt, bx.heldYaw, 0, NULL );
	} else if ( place ) {
		trap->Print( "Boombox: it can not be put there\n" );
	}
	bx.held = -1;
}

int CG_BoomboxHeld( void ) {
	return bx.held;
}

// the box in the pointer can not be put where it is
qboolean CG_BoomboxHeldBad( void ) {
	return ( bx.held >= 0 && !bx.heldOk ) ? qtrue : qfalse;
}

/*
===============================================================================

The console

===============================================================================
*/

// the box near this player, -1: none
static int Box_Near( void ) {
	float	best = BOX_REACH * 2.0f;
	int		i, found = -1;

	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( cgBoxes[i].inuse && Distance( cgBoxes[i].origin, cg.predictedPlayerState.origin ) < best ) {
			best = Distance( cgBoxes[i].origin, cg.predictedPlayerState.origin );
			found = i;
		}
	}
	return found;
}

/*
=================
CG_Boombox_f

boombox play <link or words>: into the line of the box near, or of a new one in front of the player
boombox place: a box where the cross hair points
boombox pause|resume|skip|stop|remove [box]: of that box, or of the one near
boombox volume <0 to 100> [box]
boombox list: the boxes and their lines, in the console
=================
*/
void CG_Boombox_f( void ) {
	char	what[16], words[BOX_ASK];
	vec3_t	at;
	float	yaw;
	int		box;

	Q_strncpyz( what, CG_Argv( 1 ), sizeof( what ) );
	if ( !bx.on ) {
		trap->Print( bx.serverHas ? ( cg_boombox.integer ? "Boombox: the server did not answer yet\n" : "Boombox: cg_boombox is 0\n" )
			: "This server has no boombox\n" );
		return;
	}
	if ( !Q_stricmp( what, "play" ) ) {
		const char *rest;

		// the words after "play", as they were typed
		trap->Cmd_Args( words, sizeof( words ) );
		for ( rest = words; *rest == ' '; rest++ )
			;
		rest += strlen( what );
		while ( *rest == ' ' )
			rest++;
		memmove( words, rest, strlen( rest ) + 1 );
		if ( !words[0] ) {
			trap->Print( "usage: boombox play \"<link of YouTube or YouTube Music, or what to look for>\"\n" );
			return;
		}
		// A link that was not in quotes was cut off by the console where it has "//": the rest is gone.
		if ( !Q_stricmp( words, "https:" ) || !Q_stricmp( words, "http:" ) ) {
			trap->Print( "Boombox: put the link in quotes: boombox play \"https://...\"\n" );
			return;
		}
		CG_BoomboxAsk( -1, words );
		return;
	}
	if ( !Q_stricmp( what, "place" ) ) {
		if ( !Box_Spot( cg.refdef.vieworg, cg.refdef.viewaxis[0], -1, at, &yaw ) ) {
			trap->Print( "Boombox: it can not be put where you look: look at a floor near you\n" );
			return;
		}
		Box_Do( BOX_DO_PLACE, 0, at, yaw, 0, NULL );
		return;
	}
	if ( !Q_stricmp( what, "list" ) ) {
		int i;

		if ( !bx.viaMaster ) {
			Box_Send( "boombox list", 0 );
			return;
		}
		// (the masterserver does not answer it: what this client knows)
		for ( i = 0; i < BOX_MASTER_MAX; i++ ) {
			if ( cgBoxes[i].inuse )
				trap->Print( "Boombox %i, volume %i: %s%s\n", i, cgBoxes[i].volume, ( cgBoxes[i].flags & BOX_PLAYING ) ? cgBoxes[i].title : "silent",
					cgBoxes[i].waiting ? va( ", %i more in the line", cgBoxes[i].waiting ) : "" );
		}
		return;
	}
	if ( !Q_stricmp( what, "volume" ) ) {
		box = CG_Argv( 3 )[0] ? atoi( CG_Argv( 3 ) ) : Box_Near();
		if ( !Box_Valid( box ) || !CG_Argv( 2 )[0] ) {
			trap->Print( "usage: boombox volume <0 to 100> [boombox], near a boombox\n" );
			return;
		}
		cgBoxes[box].volume = Com_Clampi( 0, 100, atoi( CG_Argv( 2 ) ) );
		Box_Do( BOX_DO_VOLUME, box, NULL, 0.0f, cgBoxes[box].volume, NULL );
		return;
	}
	if ( !Q_stricmp( what, "pause" ) || !Q_stricmp( what, "resume" ) || !Q_stricmp( what, "skip" ) || !Q_stricmp( what, "stop" )
		|| !Q_stricmp( what, "remove" ) ) {
		box = CG_Argv( 2 )[0] ? atoi( CG_Argv( 2 ) ) : Box_Near();
		if ( !Box_Valid( box ) ) {
			trap->Print( "Boombox: there is no boombox near you\n" );
			return;
		}
		CG_BoomboxDo( box, what );
		return;
	}
	trap->Print( "usage: boombox play \"<link or what to look for>\" | place | pause | resume | skip | stop | remove | volume <0 to 100> | list\n"
		"Hold the key of the pointer (+cursor) and click a boombox for its card, or drag it somewhere else.\n" );
}
