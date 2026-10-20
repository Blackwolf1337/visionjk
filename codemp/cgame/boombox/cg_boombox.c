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
// A box is no entity: the server tells this cgame where each one stands and what it plays (vs_box), and the box is
// drawn here, a model of the pack with two speakers that push out with the song. A sound system is a DJ table with
// two platters and two big speakers that stand apart, each placed and moved by itself: its song comes in stereo,
// each speaker plays its channel. The songs come over the relay of
// the voice chat as the voices of speakers behind the players and are played there (voice/cg_voice.c): through
// the sound of the level of this engine where it takes them (v_spatial), as a sound with walls, ways round corners
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

#define BOX_ASK_MSEC		3000		// "vs_box hello" this often until the server answers,
#define BOX_MAX_ASKS		10			//	and this many times at most
#define BOX_SEND_MSEC		1100		// two commands of this file go out at least this far apart
#define BOX_PENDING			6			// commands that wait for their turn
#define BOX_MOVE_MSEC		2500		// a box this player put somewhere stands there this long without an answer of the server
#define BOX_SLIDE_MSEC		250			// a box somebody else moved slides there in this time
#define BOX_LABEL_RANGE		700.0f		// the title over a box is shown this near
#define BOX_LABEL_ABOVE		12.0f		// and this far above its top
#define BOX_NOTE_MSEC		8000		// the last line of the server is shown on the card and in the window this long
#define BOX_BEAT_GAIN		4.0f		// the speakers push out by the loudness of the song times this, at most all the way
#define BOX_BEAT_FALL_MSEC	90.0f		// and come back in about this time
#define BOX_CONE_X			6.02f		// where the speakers sit in the model: in front of its middle,
#define BOX_CONE_Y			9.75f		//	to each side,
#define BOX_CONE_Z			9.0f		//	and above its floor: CONE_X, CONE_Y and CONE_Z of tools/boombox/build.py
#define BOX_MODEL			"models/map_objects/vision/boombox.md3"
#define BOX_CONE_MODEL		"models/map_objects/vision/boombox_cone.md3"
// the sound system: models and pictures of tools/boombox/build_system.py
#define SYS_DESK_MODEL		"models/map_objects/vision/sound_desk.md3"
#define SYS_PLATTER_MODEL	"models/map_objects/vision/sound_platter.md3"
#define SYS_SPEAKER_MODEL	"models/map_objects/vision/sound_speaker.md3"
#define SYS_PLATTER_X		-1.0f		// the two platters on the table: in front of its middle,
#define SYS_PLATTER_Y		17.0f		//	to each side,
#define SYS_PLATTER_Z		40.0f		//	and above its floor: PLATTER_X, PLATTER_Y and PLATTER_Z of build_system.py
#define SYS_PLATTER_DPS		200.0f		// how fast they turn while a song plays, degrees a second (33 a minute)
#define SYS_CONE_X			11.02f		// the cones on the front of a speaker: SPK_FRONT of build_system.py,
#define SYS_WOOFER_SCALE	2.0f		//	the big one the cone of the boombox this much larger, at SYS_SPK_MOUTH,
#define SYS_TWEETER_Z		53.0f		//	the small one here,
#define SYS_TWEETER_SCALE	0.55f		//	this large
#define SYS_SPK_RETRY		3			// a new speaker that has no floor where it would stand comes closer, this often
#define SYS_SPK_BEHIND		0.12f		// how much of a speaker of a sound system is heard behind it, of what is heard in front
#define BOX_BEHIND			0.45f		//	and of a boombox, whose speakers are small and whose box is thin

// where a part of a box stands, and how it gets there
typedef struct cgPlace_s {
	vec3_t		origin;			// where the server says it stands
	float		yaw;
	vec3_t		from;			// where it slides from to there,
	float		fromYaw;
	int			slideTime;		//	since when, 0: it does not
	qboolean	moved;			// this player put it somewhere and the server did not answer yet
	vec3_t		movedTo;
	float		movedYaw;
	int			movedTime;
} cgPlace_t;

typedef struct cgBox_s {
	qboolean	inuse;
	int			rev;
	int			kind;			// BOX_KIND_*
	cgPlace_t	place[BOX_PARTS];	// the box or the table, and the speakers of a sound system
	int			volume, flags, length, waiting;
	int			msec, msecTime;	// how far into the song it was when the server said so, and cg.time then
	char		title[BOX_TITLE], next[BOX_TITLE];
	char		owner[BOX_NAME_MAX + 1];	// who put it down, "": not known (a server or master before 0.1.9)
	char		by[BOX_NAME_MAX + 1];		// who asked for the song that plays
	// its line while this player watches it (CG_BoomboxWatch), songs[0] plays
	struct {
		char	title[BOX_TITLE];
		char	by[BOX_NAME_MAX + 1];
		int		seconds;
	} line[BOX_QUEUE];
	int			lineRev;		// of what is in line, -1: nothing came
	int			lineTotal;		// songs in it as the server says
	int			lineHave;		// of them that came, a bit each
	float		beat[2];		// how far the speakers are out: follows the song, of each channel
	float		spin;			// how far the platters of a sound system have turned, degrees
} cgBox_t;

static struct {
	qboolean	serverHas;		// vs_boombox in the server info
	qboolean	asked;
	int			asks, askTime;
	qboolean	on;				// the server answered: it has boxes,
	qboolean	viaMaster;		//	or the masterserver keeps them, for a server without the game module of this tree
	qboolean	toldProtocol;
	int			most, range;
	qboolean	more;			// the server or the master knows what came after 0.1.8 (BOX_CAP_MORE, VOICE_FLAG_BOXLIST)
	qboolean	systems;		// and sound systems (BOX_CAP_SYSTEM, BOX_SAY_HELLO)
	int			watch;			// the box whose line was asked for, -1: none
	char		pending[BOX_PENDING][BOX_ASK + 64];
	int			numPending;
	int			sendTime;
	qhandle_t	model, cone;
	qhandle_t	desk, platter, speaker;
	qboolean	registered;
	// a piece of a box the pointer carries (select/cg_select.c)
	int			held;			// -1: none
	vec3_t		heldAt;
	float		heldYaw;
	qboolean	heldOk;			// it can be put there
	// the last line of the server or the master for this player, which the card and the window of a box show
	char		note[256];
	int			noteBox;		// it is of this box, -1: of none in particular
	int			noteTime;		// cg.time when it came, 0: none
} bx;

static cgBox_t	cgBoxes[BOX_MAX];

#define BOX_TURN_STEP		15.0f		// one notch of the mouse wheel turns a carried piece this far

// a piece in the pointer that was turned with the wheel keeps that yaw, else it faces the player
static qboolean	boxHeldTurned;
static float	boxHeldYaw;

static qboolean	Box_Spot( const vec3_t start, const vec3_t dir, int kind, int part, int self, vec3_t at, float *yaw );
static void		Box_Forget( void );

void CG_BoomboxInit( void ) {
	const qboolean serverHas = bx.serverHas;

	memset( &bx, 0, sizeof( bx ) );
	memset( cgBoxes, 0, sizeof( cgBoxes ) );
	bx.held = bx.watch = -1;
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
static const char *const boxVerbs[] = { "", "play", "place", "move", "pause", "resume", "skip", "stop", "remove", "volume", "seek", "drop",
	"watch", "system", "part" };

/*
=================
Box_Do

What this player does with a box (BOX_DO_*): the client command boombox to a server of this tree, a VOICE_BOX to
the masterserver. at and yaw: where it goes (place, move, part, and play into box 255: a new box). value: the
volume, the second to jump to, the place in the line of the song to take out, the part to move. text: what to
play. Box 255 for watch: no box any more. A new sound system is Box_DoSystem().
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
		d.volume = d.value = value;
		if ( text )
			Q_strncpyz( d.text, text, sizeof( d.text ) );
		if ( !CG_VoiceBoxSend( &d ) )
			trap->Print( "Boombox: no answer from the masterserver right now\n" );
		return;
	}
	switch ( what ) {
	case BOX_DO_PLAY:
		// (in quotes: a link has "//" in it, which ends a command outside of them)
		Box_Send( va( "vs_boxaction play %i \"%s\"", ( box == 255 ) ? -1 : box, text ), 0 );
		break;
	case BOX_DO_PLACE:
		Box_Send( va( "vs_boxaction place %i %i %i %i", (int)at[0], (int)at[1], (int)at[2], (int)yaw ), 0 );
		break;
	case BOX_DO_MOVE:
		Box_Send( va( "vs_boxaction move %i %i %i %i %i", box, (int)at[0], (int)at[1], (int)at[2], (int)yaw ),
			(int)strlen( va( "vs_boxaction move %i ", box ) ) );
		break;
	case BOX_DO_PART:
		Box_Send( va( "vs_boxaction part %i %i %i %i %i %i", box, value, (int)at[0], (int)at[1], (int)at[2], (int)yaw ),
			(int)strlen( va( "vs_boxaction part %i %i ", box, value ) ) );
		break;
	case BOX_DO_VOLUME:
		Box_Send( va( "vs_boxaction volume %i %i", box, value ), (int)strlen( va( "vs_boxaction volume %i ", box ) ) );
		break;
	case BOX_DO_SEEK:
		Box_Send( va( "vs_boxaction seek %i %i", box, value ), (int)strlen( va( "vs_boxaction seek %i ", box ) ) );
		break;
	case BOX_DO_DROP:
		Box_Send( va( "vs_boxaction drop %i %i", box, value ), 0 );
		break;
	case BOX_DO_WATCH:
		Box_Send( va( "vs_boxaction watch %i", ( box == 255 ) ? -1 : box ), (int)strlen( "vs_boxaction watch " ) );
		break;
	default:
		Box_Send( va( "vs_boxaction %s %i", boxVerbs[what], box ), 0 );
		break;
	}
}

// a new sound system: its table, its left and its right speaker go there
static void Box_DoSystem( vec3_t at[BOX_PARTS], const float yaw[BOX_PARTS] ) {
	int k;

	if ( bx.viaMaster ) {
		boxDo_t d;

		memset( &d, 0, sizeof( d ) );
		d.what = BOX_DO_SYSTEM;
		d.box = 255;
		Q_strncpyz( d.name, cgs.clientinfo[cg.clientNum].name, sizeof( d.name ) );
		for ( k = 0; k < 3; k++ ) {
			d.origin[k] = (int)at[0][k];
			d.parts[0][k] = (int)at[1][k];
			d.parts[1][k] = (int)at[2][k];
		}
		d.yaw = yaw[0];
		d.partYaw[0] = yaw[1];
		d.partYaw[1] = yaw[2];
		if ( !CG_VoiceBoxSend( &d ) )
			trap->Print( "Boombox: no answer from the masterserver right now\n" );
		return;
	}
	Box_Send( va( "vs_boxaction system %i %i %i %i %i %i %i %i %i %i %i %i", (int)at[0][0], (int)at[0][1], (int)at[0][2], (int)yaw[0],
		(int)at[1][0], (int)at[1][1], (int)at[1][2], (int)yaw[1], (int)at[2][0], (int)at[2][1], (int)at[2][2], (int)yaw[2] ), 0 );
}

// pause, resume, skip, stop or remove, from the card of a box
void CG_BoomboxDo( int box, const char *what ) {
	int i;

	if ( !Box_Valid( box ) )
		return;
	for ( i = 1; i < (int)ARRAY_LEN( boxVerbs ); i++ ) {
		if ( !Q_stricmp( what, boxVerbs[i] ) ) {
			// (these need a place or a text, which a card does not have)
			if ( i == BOX_DO_PLAY || i == BOX_DO_PLACE || i == BOX_DO_MOVE || i == BOX_DO_SYSTEM || i == BOX_DO_PART )
				return;
			Box_Do( i, box, NULL, 0.0f, 0, NULL );
			return;
		}
	}
}

// louder or quieter for everybody by a tenth a step. Shown at once, the server says it to the others.
void CG_BoomboxLouder( int box, int steps ) {
	if ( Box_Valid( box ) )
		CG_BoomboxSetVolume( box, ( cgBoxes[box].volume + 5 ) / 10 * 10 + steps * 10 );
}

void CG_BoomboxSetVolume( int box, int volume ) {
	if ( !Box_Valid( box ) )
		return;
	cgBoxes[box].volume = Com_Clampi( 0, 100, volume );
	Box_Do( BOX_DO_VOLUME, box, NULL, 0.0f, cgBoxes[box].volume, NULL );
}

// can the server or the master jump in a song, take one out of the line and send the line
qboolean CG_BoomboxMore( void ) {
	return ( bx.on && bx.more ) ? qtrue : qfalse;
}

// The song that plays goes on from that second, for everybody. Shown at once.
void CG_BoomboxSeek( int box, int seconds ) {
	cgBox_t *b;

	if ( !Box_Valid( box ) || !CG_BoomboxMore() || !( cgBoxes[box].flags & BOX_PLAYING ) )
		return;
	b = &cgBoxes[box];
	seconds = Com_Clampi( 0, Q_max( 0, b->length - 2 ), seconds );
	b->msec = seconds * 1000;
	b->msecTime = cg.time;
	// (the server begins it again there: it loads)
	b->flags |= BOX_LOADING;
	Box_Do( BOX_DO_SEEK, box, NULL, 0.0f, seconds, NULL );
}

// a song that waits is taken out of the line, place 1 the next one
void CG_BoomboxDropSong( int box, int place ) {
	if ( Box_Valid( box ) && CG_BoomboxMore() && place >= 1 )
		Box_Do( BOX_DO_DROP, box, NULL, 0.0f, place, NULL );
}

// The line of a box is sent to this client whenever it changes, from now on (-1: of no box any more). One box at a time.
void CG_BoomboxWatch( int box ) {
	if ( !Box_Valid( box ) )
		box = -1;
	if ( box == bx.watch || !CG_BoomboxMore() ) {
		bx.watch = box;
		return;
	}
	bx.watch = box;
	if ( box >= 0 ) {
		cgBoxes[box].lineRev = -1;
		cgBoxes[box].lineHave = cgBoxes[box].lineTotal = 0;
	}
	Box_Do( BOX_DO_WATCH, ( box >= 0 ) ? box : 255, NULL, 0.0f, 0, NULL );
}

// How many songs the line of a box has as far as it came, the one that plays among them. 0: none, or not known.
int CG_BoomboxSongs( int box ) {
	const cgBox_t *b;

	if ( !Box_Valid( box ) || box != bx.watch )
		return 0;
	b = &cgBoxes[box];
	if ( !( b->flags & BOX_PLAYING ) )
		return 0;
	return b->lineTotal;
}

// a song of the line, place 0 the one that plays. qfalse: it did not come (yet)
qboolean CG_BoomboxSong( int box, int place, const char **title, const char **by, int *seconds ) {
	const cgBox_t *b;

	if ( place < 0 || place >= CG_BoomboxSongs( box ) || !( cgBoxes[box].lineHave & ( 1 << place ) ) )
		return qfalse;
	b = &cgBoxes[box];
	*title = b->line[place].title;
	*by = b->line[place].by;
	*seconds = b->line[place].seconds;
	return qtrue;
}

// A song of the line of a box came: what its line is now, if it is new, and that one (place -1: only the line).
static void Box_LineSong( int n, int rev, int place, int total, int seconds, const char *title, const char *by ) {
	cgBox_t *b = &cgBoxes[n];

	if ( !b->inuse || place < -1 || place >= BOX_QUEUE || total < 0 || total > BOX_QUEUE )
		return;
	if ( rev != b->lineRev ) {
		b->lineRev = rev;
		b->lineHave = 0;
	}
	b->lineTotal = total;
	if ( place < 0 || place >= total )
		return;
	Q_strncpyz( b->line[place].title, title, sizeof( b->line[place].title ) );
	Q_strncpyz( b->line[place].by, by, sizeof( b->line[place].by ) );
	b->line[place].seconds = seconds;
	b->lineHave |= 1 << place;
}

// a place for a new box in front of the player: looking down a little, on the floor there
static qboolean Box_InFront( vec3_t at, float *yaw ) {
	vec3_t dir;

	VectorSet( dir, cg.refdef.viewaxis[0][0], cg.refdef.viewaxis[0][1], 0.0f );
	VectorNormalize( dir );
	dir[2] = -0.6f;
	VectorNormalize( dir );
	return Box_Spot( cg.refdef.vieworg, dir, BOX_KIND_BOOMBOX, BOX_PART_BODY, -1, at, yaw );
}

// What a player typed into the chat box opened for a box, or after "v_boombox play". Without a box: the one near,
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
		box = CG_BoomboxNear();
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

// does a box have that part: a boombox only its body, a sound system its table and two speakers
static qboolean Box_HasPart( int box, int part ) {
	return ( Box_Valid( box ) && part >= 0 && part < BOX_PARTS && ( part == BOX_PART_BODY || cgBoxes[box].kind == BOX_KIND_SYSTEM ) )
		? qtrue : qfalse;
}

// where a part of a box is drawn now: in the pointer, where this player put it, on its way, or where it stands
static void Box_Where( int box, int part, vec3_t at, float *yaw ) {
	const cgPlace_t *p = &cgBoxes[box].place[part];

	if ( BOX_PIECE( box, part ) == bx.held ) {
		VectorCopy( bx.heldAt, at );
		*yaw = bx.heldYaw;
	} else if ( p->moved ) {
		VectorCopy( p->movedTo, at );
		*yaw = p->movedYaw;
	} else if ( p->slideTime && cg.time - p->slideTime < BOX_SLIDE_MSEC ) {
		const float frac = (float)( cg.time - p->slideTime ) / BOX_SLIDE_MSEC;

		at[0] = p->from[0] + ( p->origin[0] - p->from[0] ) * frac;
		at[1] = p->from[1] + ( p->origin[1] - p->from[1] ) * frac;
		at[2] = p->from[2] + ( p->origin[2] - p->from[2] ) * frac;
		*yaw = LerpAngle( p->fromYaw, p->yaw, frac );
	} else {
		VectorCopy( p->origin, at );
		*yaw = p->yaw;
	}
}

// A line of the server or the master for this player (box: of that box, -1: of none in particular). Not in the
// chat box, where the lines of every box near were too many: in the console, and on the card and in the window
// of the box for a while.
static void Box_Note( int box, const char *text ) {
	char line[sizeof( bx.note )];

	trap->Print( "[skipnotify]%s\n", text );
	// (shown under the name of the box: without the word in front of the colon, in the colour of the card)
	Q_strncpyz( line, text, sizeof( line ) );
	Q_StripColor( line );
	Q_strncpyz( bx.note, Q_stricmpn( line, "Boombox: ", 9 ) ? line : line + 9, sizeof( bx.note ) );
	bx.noteBox = box;
	bx.noteTime = cg.time ? cg.time : 1;
}

// a box is gone
static void Box_Gone( int n ) {
	if ( bx.held >= 0 && BOX_OF( bx.held ) == n )
		bx.held = -1;
	if ( n == bx.watch )
		bx.watch = -1;
	memset( &cgBoxes[n], 0, sizeof( cgBoxes[n] ) );
	CG_VoiceBoxReset( n );
}

// Where a part of a box stands now, from the server or the master. Moved: it slides there from where it is drawn
// now. Moved by this player: it is there already. fresh: the box or the part is new and stands there at once.
static void Box_SetPlace( int n, int part, const vec3_t origin, float yaw, qboolean fresh ) {
	cgPlace_t *p = &cgBoxes[n].place[part];

	if ( fresh ) {
		p->slideTime = 0;
	} else if ( !VectorCompare( origin, p->origin ) || yaw != p->yaw ) {
		Box_Where( n, part, p->from, &p->fromYaw );
		p->slideTime = cg.time ? cg.time : 1;
	}
	if ( p->moved && Distance( origin, p->movedTo ) < 2.0f ) {
		p->moved = qfalse;
		p->slideTime = 0;
	}
	VectorCopy( origin, p->origin );
	p->yaw = yaw;
}

// what kind a box is, and where the speakers of a sound system stand
static void Box_SetRig( int n, int kind, vec3_t parts[2], const float partYaw[2] ) {
	cgBox_t		*b = &cgBoxes[n];
	const int	was = b->kind;
	int			part;

	b->kind = ( kind == BOX_KIND_SYSTEM ) ? BOX_KIND_SYSTEM : BOX_KIND_BOOMBOX;
	if ( b->kind != BOX_KIND_SYSTEM ) {
		if ( bx.held >= 0 && BOX_OF( bx.held ) == n && BOX_PART_OF( bx.held ) != BOX_PART_BODY )
			bx.held = -1;
		return;
	}
	for ( part = BOX_PART_LEFT; part < BOX_PARTS; part++ )
		Box_SetPlace( n, part, parts[part - 1], partYaw[part - 1], (qboolean)( was != BOX_KIND_SYSTEM ) );
}

// what a box is now, from the server or the master
static void Box_Set( int n, int rev, const vec3_t origin, float yaw, int volume, int flags, int msec, int length, int waiting,
	const char *title, const char *next ) {
	cgBox_t *b = &cgBoxes[n];

	if ( !b->inuse ) {
		// a new box: nothing of the one before it is heard
		CG_VoiceBoxReset( n );
		b->kind = BOX_KIND_BOOMBOX;
	}
	Box_SetPlace( n, BOX_PART_BODY, origin, yaw, (qboolean)!b->inuse );
	b->inuse = qtrue;
	b->rev = rev;
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

vs_box on|set|gone|note, see bg_boombox.h
=================
*/
void CG_BoomboxServerCommand( void ) {
	char	what[8], title[BOX_TITLE], next[BOX_TITLE];
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
		// (a server before 0.1.9 says no more)
		bx.more = ( atoi( CG_Argv( 5 ) ) & BOX_CAP_MORE ) ? qtrue : qfalse;
		bx.systems = ( atoi( CG_Argv( 5 ) ) & BOX_CAP_SYSTEM ) ? qtrue : qfalse;
		return;
	}
	if ( !Q_stricmp( what, "note" ) ) {
		Box_Note( -1, CG_Argv( 2 ) );
		return;
	}
	n = atoi( CG_Argv( 2 ) );
	if ( n < 0 || n >= BOX_MAX || bx.viaMaster )
		return;
	if ( !Q_stricmp( what, "gone" ) ) {
		Box_Gone( n );
		return;
	}
	if ( !Q_stricmp( what, "list" ) ) {
		// <box> <lineRev> <first> <total>, then seconds, title and by of every song from first on
		const int	rev = atoi( CG_Argv( 3 ) ), total = atoi( CG_Argv( 5 ) );
		int			place = atoi( CG_Argv( 4 ) ), arg;
		char		by[BOX_NAME_MAX + 1];

		Box_LineSong( n, rev, -1, total, 0, "", "" );
		for ( arg = 6; arg + 2 < trap->Cmd_Argc(); arg += 3, place++ ) {
			// (CG_Argv has one buffer)
			Q_strncpyz( title, CG_Argv( arg + 1 ), sizeof( title ) );
			Q_strncpyz( by, CG_Argv( arg + 2 ), sizeof( by ) );
			Box_LineSong( n, rev, place, total, atoi( CG_Argv( arg ) ), title, by );
		}
		return;
	}
	if ( Q_stricmp( what, "set" ) )
		return;
	VectorSet( origin, (float)atof( CG_Argv( 4 ) ), (float)atof( CG_Argv( 5 ) ), (float)atof( CG_Argv( 6 ) ) );
	// (CG_Argv has one buffer: both titles are copied before anything else is read, also the next one, which as an
	// argument of Box_Set() was overwritten by the numbers read after it and showed one of them)
	Q_strncpyz( title, CG_Argv( 13 ), sizeof( title ) );
	Q_strncpyz( next, CG_Argv( 14 ), sizeof( next ) );
	Box_Set( n, atoi( CG_Argv( 3 ) ), origin, (float)atof( CG_Argv( 7 ) ), atoi( CG_Argv( 8 ) ), atoi( CG_Argv( 9 ) ), atoi( CG_Argv( 10 ) ),
		atoi( CG_Argv( 11 ) ), atoi( CG_Argv( 12 ) ), title, next );
	// (a server before 0.1.9 sends neither: "")
	Q_strncpyz( cgBoxes[n].owner, CG_Argv( 15 ), sizeof( cgBoxes[n].owner ) );
	Q_strncpyz( cgBoxes[n].by, CG_Argv( 16 ), sizeof( cgBoxes[n].by ) );
	// the kind and the speakers of a sound system after them, nothing for a boombox
	{
		vec3_t	parts[2];
		float	partYaw[2];
		int		k;

		for ( k = 0; k < 2; k++ ) {
			VectorSet( parts[k], (float)atof( CG_Argv( 18 + 4 * k ) ), (float)atof( CG_Argv( 19 + 4 * k ) ), (float)atof( CG_Argv( 20 + 4 * k ) ) );
			partYaw[k] = (float)atof( CG_Argv( 21 + 4 * k ) );
		}
		Box_SetRig( n, ( trap->Cmd_Argc() >= 26 ) ? atoi( CG_Argv( 17 ) ) : BOX_KIND_BOOMBOX, parts, partYaw );
	}
}

/*
===============================================================================

Through the masterserver

===============================================================================
*/

// The master the voice chat reaches plays boxes, or it no longer does (the way to it closed). On a server of this
// tree the boxes are the server's own and none of this counts.
void CG_BoomboxMasterReady( qboolean ready ) {
	if ( ready && v_boomboxes.integer && !bx.serverHas ) {
		bx.on = bx.viaMaster = qtrue;
		bx.more = CG_VoiceMasterBoxList();
		bx.range = BOX_RANGE;
		bx.most = BOX_MASTER_MAX;
	} else if ( !ready && bx.viaMaster ) {
		Box_Forget();
		bx.viaMaster = qfalse;
	}
}

// a VOICE_BOX of the master: what a box is now, who put it down, that it is gone, or a line for this player
void CG_BoomboxFromMaster( const byte *data, int len ) {
	boxSay_t	say;
	vec3_t		origin;

	if ( !bx.viaMaster || !BG_BoxSayUnpack( data, len, &say ) )
		return;
	if ( say.what == BOX_SAY_HELLO ) {
		bx.systems = ( say.caps & BOX_CAP_SYSTEM ) ? qtrue : qfalse;
		return;
	}
	if ( say.what == BOX_SAY_NOTE ) {
		Box_Note( ( say.box < BOX_MASTER_MAX ) ? say.box : -1, say.text );
		return;
	}
	if ( say.box < 0 || say.box >= BOX_MASTER_MAX )
		return;
	if ( say.what == BOX_SAY_GONE ) {
		if ( cgBoxes[say.box].inuse )
			Box_Gone( say.box );
		return;
	}
	if ( say.what == BOX_SAY_INFO ) {
		if ( cgBoxes[say.box].inuse ) {
			Q_strncpyz( cgBoxes[say.box].owner, say.owner, sizeof( cgBoxes[say.box].owner ) );
			Q_strncpyz( cgBoxes[say.box].by, say.by, sizeof( cgBoxes[say.box].by ) );
		}
		return;
	}
	if ( say.what == BOX_SAY_LIST ) {
		Box_LineSong( say.box, say.rev, say.place, say.total, say.length, say.title, say.by );
		return;
	}
	if ( say.what == BOX_SAY_RIG ) {
		vec3_t	parts[2];
		int		k;

		if ( !cgBoxes[say.box].inuse )
			return;
		for ( k = 0; k < 2; k++ )
			VectorSet( parts[k], (float)say.parts[k][0], (float)say.parts[k][1], (float)say.parts[k][2] );
		Box_SetRig( say.box, say.kind, parts, say.partYaw );
		return;
	}
	VectorSet( origin, (float)say.origin[0], (float)say.origin[1], (float)say.origin[2] );
	Box_Set( say.box, say.rev, origin, say.yaw, say.volume, say.flags, say.msec, say.length, say.waiting, say.title, say.next );
}

// The boxes of the master this client hears, a bit each: the ones a speaker of which is near enough the camera.
// And that this client draws sound systems (BOX_STATE_SYSTEMS), which the master reads in the same byte.
int CG_BoomboxHearMask( void ) {
	const float	range = CG_BoomboxRange() * 1.1f;
	vec3_t		mouth;
	float		gain;
	int			i, mask = BOX_STATE_SYSTEMS;

	if ( !bx.viaMaster || !v_boomboxes.integer || v_boomboxVolume.value <= 0.0f )
		return mask;
	for ( i = 0; i < BOX_MASTER_MAX; i++ ) {
		if ( CG_VoiceBoxMuted( i ) )
			continue;
		if ( ( CG_BoomboxMouth( i, mouth, &gain ) && Distance( mouth, cg.refdef.vieworg ) <= range )
			|| ( CG_BoomboxMouth( i + BOX_RIGHT, mouth, &gain ) && Distance( mouth, cg.refdef.vieworg ) <= range ) )
			mask |= 1 << i;
	}
	return mask;
}

/*
===============================================================================

Once a frame

===============================================================================
*/

// the boxes are gone for this player: switched off (v_boomboxes 0), or the server stopped answering
static void Box_Forget( void ) {
	int i;

	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( cgBoxes[i].inuse )
			CG_VoiceBoxReset( i );
	}
	memset( cgBoxes, 0, sizeof( cgBoxes ) );
	bx.held = -1;
	bx.on = bx.asked = bx.systems = qfalse;
	bx.asks = 0;
}

void CG_BoomboxFrame( void ) {
	const int		now = trap->Milliseconds();
	const qboolean	wanted = ( v_boomboxes.integer && !cg.demoPlayback ) ? qtrue : qfalse;
	int				i;

	if ( !wanted && bx.on ) {
		if ( !bx.viaMaster )
			Box_Send( "vs_box off", 0 );
		Box_Forget();
		bx.viaMaster = qfalse;
	}
	// switched on again where the master plays the boxes
	if ( wanted && !bx.on && !bx.serverHas && CG_VoiceMasterBoxes() )
		CG_BoomboxMasterReady( qtrue );
	// asked again while there is no answer: the flood protection of the server may have dropped it
	if ( wanted && bx.serverHas && !bx.on && cg.snap && bx.asks < BOX_MAX_ASKS && ( !bx.asked || now - bx.askTime >= BOX_ASK_MSEC ) ) {
		trap->SendClientCommand( va( "vs_box hello %i %i", BOX_PROTOCOL, BOX_CAP_SYSTEM ) );
		bx.asked = qtrue;
		bx.askTime = bx.sendTime = now;
		bx.asks++;
	}
	Box_SendPending();

	for ( i = 0; i < BOX_MAX; i++ ) {
		cgBox_t	*b = &cgBoxes[i];
		int		part, channel;

		if ( !b->inuse )
			continue;
		// the server did not take a move: it goes back to where it stands
		for ( part = 0; part < BOX_PARTS; part++ ) {
			cgPlace_t *p = &b->place[part];

			if ( p->moved && cg.time - p->movedTime > BOX_MOVE_MSEC ) {
				p->moved = qfalse;
				VectorCopy( p->movedTo, p->from );
				p->fromYaw = p->movedYaw;
				p->slideTime = cg.time ? cg.time : 1;
			}
		}
		// out at once, back in a moment: each speaker with its channel
		for ( channel = 0; channel < 2; channel++ ) {
			const float beat = Com_Clamp( 0.0f, 1.0f, CG_VoiceBoxBeat( i + channel * BOX_RIGHT ) * BOX_BEAT_GAIN );

			if ( beat > b->beat[channel] )
				b->beat[channel] = beat;
			else
				b->beat[channel] += ( beat - b->beat[channel] ) * ( 1.0f - expf( -cg.frametime / BOX_BEAT_FALL_MSEC ) );
		}
		// the platters of a sound system turn while a song plays
		if ( ( b->flags & BOX_PLAYING ) && !( b->flags & ( BOX_PAUSED | BOX_LOADING ) ) )
			b->spin = AngleNormalize360( b->spin + SYS_PLATTER_DPS * cg.frametime * 0.001f );
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
	bx.desk = trap->R_RegisterModel( SYS_DESK_MODEL );
	bx.platter = trap->R_RegisterModel( SYS_PLATTER_MODEL );
	bx.speaker = trap->R_RegisterModel( SYS_SPEAKER_MODEL );
}

// A speaker cone, pushed out by the song: longer along the axis it faces, a little wider. x, y, z: where it sits
// on the model, size: the cone of the boombox this much larger.
static void Box_AddCone( const refEntity_t *body, float x, float y, float z, float size, float beat ) {
	refEntity_t	cone = *body;
	const float	out = size * ( 1.0f + 1.4f * beat ), wide = size * ( 1.0f + 0.06f * beat );

	cone.hModel = bx.cone;
	VectorMA( body->origin, x, body->axis[0], cone.origin );
	VectorMA( cone.origin, y, body->axis[1], cone.origin );
	VectorMA( cone.origin, z, body->axis[2], cone.origin );
	VectorCopy( cone.origin, cone.oldorigin );
	VectorScale( body->axis[0], out, cone.axis[0] );
	VectorScale( body->axis[1], wide, cone.axis[1] );
	VectorScale( body->axis[2], wide, cone.axis[2] );
	cone.nonNormalizedAxes = qtrue;
	trap->R_AddRefEntityToScene( &cone );
}

// a platter of a sound system, turned by the song
static void Box_AddPlatter( const refEntity_t *body, float side, float spin ) {
	refEntity_t	platter = *body;
	vec3_t		angles;

	platter.hModel = bx.platter;
	VectorMA( body->origin, SYS_PLATTER_X, body->axis[0], platter.origin );
	VectorMA( platter.origin, side * SYS_PLATTER_Y, body->axis[1], platter.origin );
	VectorMA( platter.origin, SYS_PLATTER_Z, body->axis[2], platter.origin );
	VectorCopy( platter.origin, platter.oldorigin );
	vectoangles( body->axis[0], angles );
	// (the right one the other way round, as a DJ has them)
	angles[YAW] += side * spin;
	AnglesToAxis( angles, platter.axis );
	platter.renderfx &= ~( RF_FORCEPOST | RF_AFTERGLOW );
	trap->R_AddRefEntityToScene( &platter );
}

// a part of a box as a model in the scene, where it is drawn now. qfalse: it has no model (yet).
static qboolean Box_Body( int box, int part, refEntity_t *body ) {
	vec3_t		angles;
	qhandle_t	model;
	float		height;

	if ( cgBoxes[box].kind != BOX_KIND_SYSTEM ) {
		model = bx.model;
		height = BOX_HEIGHT;
	} else if ( part == BOX_PART_BODY ) {
		model = bx.desk;
		height = SYS_DESK_HEIGHT;
	} else {
		model = bx.speaker;
		height = SYS_SPK_HEIGHT;
	}
	if ( !model )
		return qfalse;
	memset( body, 0, sizeof( *body ) );
	body->reType = RT_MODEL;
	body->hModel = model;
	Box_Where( box, part, body->origin, &angles[YAW] );
	angles[PITCH] = angles[ROLL] = 0.0f;
	AnglesToAxis( angles, body->axis );
	VectorCopy( body->origin, body->oldorigin );
	VectorCopy( body->origin, body->lightingOrigin );
	body->lightingOrigin[2] += height * 0.5f;
	body->renderfx = RF_LIGHTING_ORIGIN;
	body->shaderRGBA[0] = body->shaderRGBA[1] = body->shaderRGBA[2] = body->shaderRGBA[3] = 255;
	return qtrue;
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
	int			i, part;

	if ( !v_boomboxes.integer ) {
		return;
	}
	for ( i = 0; i < BOX_MAX; i++ ) {
		const cgBox_t *b = &cgBoxes[i];

		if ( !b->inuse ) {
			continue;
		}
		Box_Register();
		for ( part = 0; part < BOX_PARTS; part++ ) {
			const int piece = BOX_PIECE( i, part );

			if ( !Box_HasPart( i, part ) || !Box_Body( i, part, &body ) ) {
				continue;
			}
			// with an outline it is drawn after it and on it, as a player is (select/cg_select.c)
			if ( CG_SelectBoxOutlined( piece ) ) {
				body.renderfx |= RF_FORCEPOST | RF_AFTERGLOW;
			}
			trap->R_AddRefEntityToScene( &body );
			if ( b->kind != BOX_KIND_SYSTEM ) {
				if ( bx.cone ) {
					Box_AddCone( &body, BOX_CONE_X, BOX_CONE_Y, BOX_CONE_Z, 1.0f, b->beat[0] );
					Box_AddCone( &body, BOX_CONE_X, -BOX_CONE_Y, BOX_CONE_Z, 1.0f, b->beat[0] );
				}
			} else if ( part == BOX_PART_BODY ) {
				if ( bx.platter ) {
					Box_AddPlatter( &body, 1.0f, b->spin );
					Box_AddPlatter( &body, -1.0f, b->spin );
				}
			} else if ( bx.cone ) {
				const float beat = b->beat[part == BOX_PART_RIGHT];

				Box_AddCone( &body, SYS_CONE_X, 0.0f, SYS_SPK_MOUTH, SYS_WOOFER_SCALE, beat );
				Box_AddCone( &body, SYS_CONE_X, 0.0f, SYS_TWEETER_Z, SYS_TWEETER_SCALE, beat * 0.5f );
			}
			CG_SelectBoxOutline( piece, &body );
		}
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

	if ( !v_boomboxes.integer || !cg.snap ) {
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
		Box_Where( i, BOX_PART_BODY, top, &yaw );
		top[2] += CG_BoomboxHeight( i ) + BOX_LABEL_ABOVE;
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
	return ( v_boomboxes.integer && !cg.demoPlayback && ( bx.on || !bx.serverHas ) ) ? qtrue : qfalse;
}

// the box of a channel and which speaker of it plays the channel, qfalse: there is none
static qboolean Box_Channel( int channel, int *box, int *part ) {
	if ( channel < 0 || channel >= BOX_CHANNELS )
		return qfalse;
	*box = channel % BOX_MAX;
	if ( !Box_Valid( *box ) )
		return qfalse;
	if ( cgBoxes[*box].kind != BOX_KIND_SYSTEM ) {
		*part = BOX_PART_BODY;
		return ( channel < BOX_RIGHT ) ? qtrue : qfalse;
	}
	*part = ( channel < BOX_RIGHT ) ? BOX_PART_LEFT : BOX_PART_RIGHT;
	return qtrue;
}

// is the song of this channel to be played
qboolean CG_BoomboxHears( int channel ) {
	int box, part;

	return ( v_boomboxes.integer && v_boomboxVolume.value > 0.0f && Box_Channel( channel, &box, &part ) ) ? qtrue : qfalse;
}

// How much of a speaker reaches the camera by the way it faces: all of it in front, half or so beside it, little
// behind it (behind: that part, at the back). A cardioid, as a speaker in a box has.
static float Box_Facing( const vec3_t mouth, float yaw, float behind ) {
	vec3_t	angles, forward, to;
	float	front;

	VectorSet( angles, 0.0f, yaw, 0.0f );
	AngleVectors( angles, forward, NULL, NULL );
	VectorSubtract( cg.refdef.vieworg, mouth, to );
	if ( VectorNormalize( to ) < 1.0f ) {
		return 1.0f;
	}
	front = 0.5f * ( 1.0f + DotProduct( forward, to ) );
	return behind + ( 1.0f - behind ) * front;
}

// Where the sound of a channel comes from (the box, a speaker of a sound system), and how loud it is before its
// distance: as this player wants the songs (v_boomboxVolume), the box is set for everybody, and the speaker faces
// the camera or not. qfalse: there is no such channel.
qboolean CG_BoomboxMouth( int channel, vec3_t mouth, float *gain ) {
	float	yaw;
	int		box, part;

	if ( !Box_Channel( channel, &box, &part ) )
		return qfalse;
	Box_Where( box, part, mouth, &yaw );
	mouth[2] += ( part == BOX_PART_BODY ) ? BOX_SPEAKER_HEIGHT : SYS_SPK_MOUTH;
	*gain = v_boomboxVolume.value * cgBoxes[box].volume * 0.01f
		* Box_Facing( mouth, yaw, ( part == BOX_PART_BODY ) ? BOX_BEHIND : SYS_SPK_BEHIND );
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

qboolean CG_BoomboxShown( int piece ) {
	return ( v_boomboxes.integer && bx.model && piece >= 0 && piece < BOX_PIECES && Box_HasPart( BOX_OF( piece ), BOX_PART_OF( piece ) ) )
		? qtrue : qfalse;
}

// how a piece is for the pointer: from its middle to its front and back, its sides, and its top
static void Box_Size( int piece, float *halfDepth, float *halfWidth, float *height ) {
	if ( cgBoxes[BOX_OF( piece )].kind != BOX_KIND_SYSTEM ) {
		*halfDepth = BOX_HALF_DEPTH;
		*halfWidth = BOX_HALF_WIDTH;
		*height = BOX_HEIGHT;
	} else if ( BOX_PART_OF( piece ) == BOX_PART_BODY ) {
		*halfDepth = SYS_DESK_HALF_DEPTH;
		*halfWidth = SYS_DESK_HALF_WIDTH;
		*height = SYS_DESK_HEIGHT;
	} else {
		*halfDepth = SYS_SPK_HALF_DEPTH;
		*halfWidth = SYS_SPK_HALF_WIDTH;
		*height = SYS_SPK_HEIGHT;
	}
}

float CG_BoomboxHeight( int piece ) {
	float halfDepth, halfWidth, height;

	if ( !CG_BoomboxShown( piece ) )
		return BOX_HEIGHT;
	Box_Size( piece, &halfDepth, &halfWidth, &height );
	return height;
}

// what the card and the pointer call a box
const char *CG_BoomboxName( int box ) {
	return CG_VisionText( ( Box_Valid( box ) && cgBoxes[box].kind == BOX_KIND_SYSTEM ) ? "Sound System" : "Boombox" );
}

/*
=================
CG_BoomboxPick

The piece of a box a ray from start meets first, nearer than *bestT, which is then how far away it is. -1: none.
A piece is a block turned by its yaw, a little larger than the model for the pointer.
=================
*/
int CG_BoomboxPick( const vec3_t start, const vec3_t dir, float *bestT ) {
	int i, k, found = -1;

	for ( i = 0; i < BOX_PIECES; i++ ) {
		vec3_t	at, d, o, r, lo, hi;
		float	yaw, c, s, tNear = 0.0f, tFar = *bestT, halfDepth, halfWidth, height;

		if ( !CG_BoomboxShown( i ) || i == bx.held ) {
			continue;
		}
		Box_Size( i, &halfDepth, &halfWidth, &height );
		VectorSet( lo, -halfDepth - 2.0f, -halfWidth - 2.0f, -1.0f );
		VectorSet( hi, halfDepth + 2.0f, halfWidth + 2.0f, height + 2.0f );
		Box_Where( BOX_OF( i ), BOX_PART_OF( i ), at, &yaw );
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

// what the card of a piece stands beside, as the middle of a player
void CG_BoomboxMiddle( int piece, vec3_t at ) {
	float yaw;

	if ( piece < 0 || piece >= BOX_PIECES || !Box_Valid( BOX_OF( piece ) ) ) {
		VectorClear( at );
		return;
	}
	Box_Where( BOX_OF( piece ), BOX_PART_OF( piece ), at, &yaw );
}

qboolean CG_BoomboxInfo( int box, boomboxInfo_t *info ) {
	const cgBox_t *b;

	if ( !Box_Valid( box ) )
		return qfalse;
	b = &cgBoxes[box];
	info->kind = b->kind;
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
	info->owner = b->owner;
	info->by = b->by;
	info->muted = CG_VoiceBoxMuted( box );
	// the last line for this player, while it is new
	info->note = ( bx.noteTime && cg.time - bx.noteTime < BOX_NOTE_MSEC && ( bx.noteBox < 0 || bx.noteBox == box ) ) ? bx.note : "";
	return qtrue;
}

// "Play a song": the chat box opens for it, where a link can be pasted with Ctrl+V
void CG_BoomboxPrompt( int box ) {
	if ( !CG_ChatOpenFor( 3, box ) ) {
		trap->Print( "Boombox: type /boombox play \"<link of YouTube or YouTube Music, or what to look for>\" into the console\n" );
	}
}

// how much floor a part of a box needs round its middle, as Box_Foot of g_boombox.c
static float Box_Foot( int kind, int part ) {
	if ( kind != BOX_KIND_SYSTEM )
		return BOX_FOOT;
	return ( part == BOX_PART_BODY ) ? SYS_FOOT_DESK : SYS_FOOT_SPK;
}

// Does a part at spot stand too near another part of a box but itself (piece self, -1: a new one)? As
// Box_Crowded of bg_boombox_core.c, which the server and the master ask.
static qboolean Box_Crowded( const vec3_t spot, qboolean big, int self ) {
	int i;

	for ( i = 0; i < BOX_PIECES; i++ ) {
		if ( i == self || !Box_HasPart( BOX_OF( i ), BOX_PART_OF( i ) ) )
			continue;
		if ( Distance( cgBoxes[BOX_OF( i )].place[BOX_PART_OF( i )].origin, spot ) < ( ( big || cgBoxes[BOX_OF( i )].kind == BOX_KIND_SYSTEM ) ? SYS_APART : BOX_APART ) )
			return qtrue;
	}
	return qfalse;
}

/*
=================
Box_Spot

Where a part of a box (of that kind) goes that is put where a ray from start meets the level: on the floor there,
or under the place in front of a wall it meets, as the server will put it (Box_Spot of g_boombox.c). qfalse:
there is no floor, it is out of reach of the eye of the player, or another box stands there (self: the piece that
is moved, -1 for a new one). at is where it would be all the same.
=================
*/
static qboolean Box_Spot( const vec3_t start, const vec3_t dir, int kind, int part, int self, vec3_t at, float *yaw ) {
	const float		foot = Box_Foot( kind, part );
	const vec3_t	mins = { -foot, -foot, 0.0f }, maxs = { foot, foot, BOX_FOOT_HEIGHT };
	vec3_t			end, eye, from;
	trace_t			tr;

	VectorCopy( cg.predictedPlayerState.origin, eye );
	eye[2] += cg.predictedPlayerState.viewheight;
	VectorMA( start, BOX_REACH + 256.0f, dir, end );
	CG_Trace( &tr, start, NULL, NULL, end, cg.predictedPlayerState.clientNum, MASK_SOLID );
	// down from a little before where it met the level
	VectorMA( tr.endpos, -8.0f, dir, from );
	from[2] += 12.0f;
	VectorCopy( from, end );
	end[2] -= BOX_DROP + 12.0f;
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
	return (qboolean)!Box_Crowded( at, (qboolean)( kind == BOX_KIND_SYSTEM ), self );
}

// the pointer takes a piece of a box: it follows the pointer over the floor until it is put down
void CG_BoomboxGrab( int piece ) {
	if ( !CG_BoomboxShown( piece ) )
		return;
	Box_Where( BOX_OF( piece ), BOX_PART_OF( piece ), bx.heldAt, &bx.heldYaw );
	bx.held = piece;
	bx.heldOk = qtrue;
	boxHeldTurned = qfalse;
}

// the wheel of the mouse turns the piece in the pointer, a notch each, from the way it faces now
void CG_BoomboxTurnHeld( int notches ) {
	if ( !CG_BoomboxShown( bx.held ) )
		return;
	if ( !boxHeldTurned ) {
		boxHeldTurned = qtrue;
		boxHeldYaw = bx.heldYaw;
	}
	boxHeldYaw = AngleNormalize360( boxHeldYaw + notches * BOX_TURN_STEP );
	bx.heldYaw = boxHeldYaw;
}

// once a frame while a piece is in the pointer: the ray from the eye through the pointer
void CG_BoomboxCarry( const vec3_t start, const vec3_t dir ) {
	if ( !CG_BoomboxShown( bx.held ) ) {
		bx.held = -1;
		return;
	}
	bx.heldOk = Box_Spot( start, dir, cgBoxes[BOX_OF( bx.held )].kind, BOX_PART_OF( bx.held ), bx.held, bx.heldAt, &bx.heldYaw );
	if ( boxHeldTurned )
		bx.heldYaw = boxHeldYaw;
}

// put down where it is, if it can be (place), or back where it was
void CG_BoomboxDrop( qboolean place ) {
	const int	piece = bx.held;
	cgPlace_t	*p;

	if ( !CG_BoomboxShown( piece ) ) {
		bx.held = -1;
		return;
	}
	p = &cgBoxes[BOX_OF( piece )].place[BOX_PART_OF( piece )];
	if ( place && bx.heldOk ) {
		p->moved = qtrue;
		VectorCopy( bx.heldAt, p->movedTo );
		p->movedYaw = bx.heldYaw;
		p->movedTime = cg.time;
		if ( BOX_PART_OF( piece ) == BOX_PART_BODY )
			Box_Do( BOX_DO_MOVE, BOX_OF( piece ), bx.heldAt, bx.heldYaw, 0, NULL );
		else
			Box_Do( BOX_DO_PART, BOX_OF( piece ), bx.heldAt, bx.heldYaw, BOX_PART_OF( piece ), NULL );
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

// the box near this player (its body or the table), -1: none
int CG_BoomboxNear( void ) {
	float	best = BOX_REACH * 2.0f;
	int		i, found = -1;

	for ( i = 0; i < BOX_MAX; i++ ) {
		if ( cgBoxes[i].inuse && Distance( cgBoxes[i].place[BOX_PART_BODY].origin, cg.predictedPlayerState.origin ) < best ) {
			best = Distance( cgBoxes[i].place[BOX_PART_BODY].origin, cg.predictedPlayerState.origin );
			found = i;
		}
	}
	return found;
}

// the piece of a box under the cross hair, no further away than range, -1: none
int CG_BoomboxAimed( float range ) {
	float	bestT = range;
	trace_t	tr;
	vec3_t	end;
	int		piece;

	piece = CG_BoomboxPick( cg.refdef.vieworg, cg.refdef.viewaxis[0], &bestT );
	if ( piece < 0 )
		return -1;
	// (not through a wall)
	VectorMA( cg.refdef.vieworg, bestT, cg.refdef.viewaxis[0], end );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, cg.predictedPlayerState.clientNum, MASK_SOLID );
	return ( tr.fraction >= 1.0f ) ? piece : -1;
}

qboolean CG_BoomboxOn( void ) {
	return bx.on;
}

// can a sound system be put down here
qboolean CG_BoomboxSystems( void ) {
	return ( bx.on && bx.systems ) ? qtrue : qfalse;
}

// a boombox where the cross hair points
void CG_BoomboxPlace( void ) {
	vec3_t	at;
	float	yaw;

	if ( !bx.on ) {
		trap->Print( "This server has no boombox\n" );
		return;
	}
	if ( !Box_Spot( cg.refdef.vieworg, cg.refdef.viewaxis[0], BOX_KIND_BOOMBOX, BOX_PART_BODY, -1, at, &yaw ) ) {
		trap->Print( "Boombox: it can not be put where you look: look at a floor near you\n" );
		return;
	}
	Box_Do( BOX_DO_PLACE, 0, at, yaw, 0, NULL );
}

/*
=================
CG_BoomboxPlaceSystem

A sound system where the cross hair points: its table turned to the player, its speakers to the left and the
right of it as he sees it, turned the same way, on the floor there. A speaker that has no floor there comes
closer to the table.
=================
*/
void CG_BoomboxPlaceSystem( void ) {
	vec3_t	at[BOX_PARTS], angles, forward, right, eye, want, dir;
	float	yaw[BOX_PARTS], side;
	int		part, k;

	if ( !CG_BoomboxSystems() ) {
		trap->Print( bx.on ? "Boombox: this server has no sound systems (it is older)\n" : "This server has no boombox\n" );
		return;
	}
	if ( !Box_Spot( cg.refdef.vieworg, cg.refdef.viewaxis[0], BOX_KIND_SYSTEM, BOX_PART_BODY, -1, at[0], &yaw[0] ) ) {
		trap->Print( "Boombox: the sound system can not be put where you look: look at a floor near you with room round it\n" );
		return;
	}
	VectorCopy( cg.predictedPlayerState.origin, eye );
	eye[2] += cg.predictedPlayerState.viewheight;
	VectorSet( angles, 0.0f, yaw[0], 0.0f );
	AngleVectors( angles, forward, right, NULL );
	// (the table faces the player: his left is its right)
	for ( part = BOX_PART_LEFT; part < BOX_PARTS; part++ ) {
		yaw[part] = yaw[0];
		for ( k = 0, side = SYS_SPK_SIDE; k < SYS_SPK_RETRY; k++, side *= 0.75f ) {
			VectorMA( at[0], ( part == BOX_PART_LEFT ) ? side : -side, right, want );
			want[2] += 4.0f;
			VectorSubtract( want, eye, dir );
			VectorNormalize( dir );
			// (crowded by nothing of the new system yet: the table and the other speaker are checked here)
			if ( Box_Spot( eye, dir, BOX_KIND_SYSTEM, part, -1, at[part], &yaw[part] ) && Distance( at[part], at[0] ) >= SYS_APART
				&& ( part == BOX_PART_LEFT || Distance( at[part], at[BOX_PART_LEFT] ) >= SYS_APART ) ) {
				break;
			}
		}
		if ( k == SYS_SPK_RETRY ) {
			trap->Print( "Boombox: there is no room for the speakers beside the table there\n" );
			return;
		}
		yaw[part] = yaw[0];
	}
	Box_DoSystem( at, yaw );
}

/*
=================
CG_Boombox_f

boombox play <link or words>: into the line of the box near, or of a new one in front of the player
boombox place: a box where the cross hair points
boombox system: a sound system there, its speakers beside it
boombox pause|resume|skip|stop|remove [box]: of that box, or of the one near
boombox volume <0 to 100> [box]
boombox window [box]: the window of that box, or of the one near
boombox list: the boxes and their lines, in the console
=================
*/
void CG_Boombox_f( void ) {
	char	what[16], words[BOX_ASK];
	int		box;

	Q_strncpyz( what, CG_Argv( 1 ), sizeof( what ) );
	if ( !bx.on ) {
		trap->Print( bx.serverHas ? ( v_boomboxes.integer ? "Boombox: the server did not answer yet\n" : "Boombox: v_boomboxes is 0\n" )
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
			trap->Print( "usage: v_boombox play \"<link of YouTube or YouTube Music, or what to look for>\"\n" );
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
		CG_BoomboxPlace();
		return;
	}
	if ( !Q_stricmp( what, "system" ) ) {
		CG_BoomboxPlaceSystem();
		return;
	}
	if ( !Q_stricmp( what, "list" ) ) {
		int i;

		if ( !bx.viaMaster ) {
			Box_Send( "vs_boxaction list", 0 );
			return;
		}
		// (the masterserver does not answer it: what this client knows)
		for ( i = 0; i < BOX_MASTER_MAX; i++ ) {
			if ( cgBoxes[i].inuse )
				trap->Print( "%s %i, volume %i: %s%s\n", ( cgBoxes[i].kind == BOX_KIND_SYSTEM ) ? "Sound system" : "Boombox", i, cgBoxes[i].volume,
					( cgBoxes[i].flags & BOX_PLAYING ) ? cgBoxes[i].title : "silent",
					cgBoxes[i].waiting ? va( ", %i more in the line", cgBoxes[i].waiting ) : "" );
		}
		return;
	}
	if ( !Q_stricmp( what, "window" ) ) {
		box = CG_Argv( 2 )[0] ? atoi( CG_Argv( 2 ) ) : CG_BoomboxNear();
		if ( !Box_Valid( box ) ) {
			trap->Print( "Boombox: there is no boombox near you\n" );
			return;
		}
		CG_BoomboxWindowOpen( box );
		return;
	}
	if ( !Q_stricmp( what, "volume" ) ) {
		box = CG_Argv( 3 )[0] ? atoi( CG_Argv( 3 ) ) : CG_BoomboxNear();
		if ( !Box_Valid( box ) || !CG_Argv( 2 )[0] ) {
			trap->Print( "usage: v_boombox volume <0 to 100> [boombox], near a boombox\n" );
			return;
		}
		cgBoxes[box].volume = Com_Clampi( 0, 100, atoi( CG_Argv( 2 ) ) );
		Box_Do( BOX_DO_VOLUME, box, NULL, 0.0f, cgBoxes[box].volume, NULL );
		return;
	}
	if ( !Q_stricmp( what, "pause" ) || !Q_stricmp( what, "resume" ) || !Q_stricmp( what, "skip" ) || !Q_stricmp( what, "stop" )
		|| !Q_stricmp( what, "remove" ) ) {
		box = CG_Argv( 2 )[0] ? atoi( CG_Argv( 2 ) ) : CG_BoomboxNear();
		if ( !Box_Valid( box ) ) {
			trap->Print( "Boombox: there is no boombox near you\n" );
			return;
		}
		CG_BoomboxDo( box, what );
		return;
	}
	trap->Print( "usage: v_boombox play \"<link or what to look for>\" | place | system | pause | resume | skip | stop | remove | volume <0 to 100> | window | list\n"
		"Hold the key of the pointer (+v_cursor) and click a boombox for its card, or drag it somewhere else.\n" );
}
