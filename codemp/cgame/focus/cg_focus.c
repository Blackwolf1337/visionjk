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

// cg_focus.c -- Focus, the "state of mind". Not from JA+, see docs/japlus/client.md, "Focus".
//
// The command "focus" stops the world on the own screen: the moment it was called stays as a still, and the
// player leaves his body as a camera he steers with his keys and his mouse. The game goes on meanwhile. The
// server is not asked to stop anything, the body stands where it stood and can be hit, chat and votes keep
// coming. A hit, a teleport, the end of the level, Escape or the command again bring him back, and the world
// is then the one of that moment: nothing of the time in between is played after.
//
// How the still is made. While it holds, no new snapshot is taken in (CG_ProcessSnapshots() asks
// CG_FocusSnapshots() first). The newest one is only looked at: its server commands are run, and its player
// state says whether the body was hit or moved. The scene is built with the time of the moment the world
// stopped (CG_FocusFrame() sets it, CG_FocusReal() puts the time of the game back for the 2D), so nothing in it
// moves, no effect and no shader. The way back sets the cgame onto the newest snapshot as after connecting,
// at the start of the next frame, where the snapshots are read anyway.
//
// The camera slides along walls and stays within a leash of the body. The still holds nothing the client was
// not told, and with walls and leash it shows nothing one could not have walked to: no way to look around
// corners while the others play on.
//
// The keys. The cgame holds them (KEYCATCH_CGAME), so the body gets no command, and with an engine of this
// tree it shows no talk sign (cg_catchQuiet, set in select/cg_select.c). The engine runs no bound command
// while somebody holds the keys: the camera reads the keys of the movement commands itself, and the keys of
// "focus" and of the chat are looked up when they are pressed. Escape never arrives as a key, the engine takes
// the keys back and says so (CG_EventHandling() calls CG_FocusEnd()).
//
// The look. The renderer is asked for the scene as a world that stands still looks, dark blue and in drifting
// fog (trap->ext.R_SetSpiritWorld, an engine with the cvar cl_cgameSpirit; rd-vulkan-vision and the client in
// the browser have the pass). Where there is no such pass, rd-vanilla and any other engine, this file lays a
// dark blue tint and two layers of smoke over the picture, made here and plainer. Both come in over half a
// second and go out in a third after the world is back.
//
// The sound. The mix of the game is muffled as it is behind the menu in the level (s_muffle of the engine, which
// menu3d/cg_menu3d.c sets for both), and a low hum lies under it: the sounds of the Force Sight of the game,
// one when it begins and when it ends, its loop in between. Chosen by their names, nobody listened yet. The
// voice chat is not touched.
//
// The body. On a server with the game module of this tree it is protected meanwhile (game/status/g_focus.c):
// where the server info says so (g_visionCaps bit 4) the server is told "jpfocus 1" when the player leaves and
// "jpfocus 0" when he is back, and it answers with a server command of the same name. The flood protection of
// a server may drop a command, so each is said again until its answer is there. The line on the screen says
// what the answer was. On any other server nothing is sent: the body can be hit, and a hit brings one back.
//
// Everything else happens in this client. Another client sees a player who stands still, or meditates where the
// server protects him.
//
// The Vision Temple ("temple") is the same state with another place in it. The level is not drawn at all: the
// scene of the frame is one without a level (RDF_NOWORLDMODEL) with the flag RDF_VISION_TEMPLE, and the renderer
// works a place above the clouds out behind it (trap->ext.R_SetTempleScene, an engine with the cvar
// cl_cgameTemple; so far only the Vulkan device of rd-vulkan-vision can, shaders/temple.frag). The camera
// walks there on the gold: the way, the terrace around the podium and up its five steps, not on the clouds that
// heap up around them (Temple_Keep()). Where those are and how high the steps are stands in that shader and
// in Temple_Ground() and Temple_Keep() here, and the two have to match. Keys, the server, the body and the ways back are the ones of Focus.
//
// Who else is there. Clients of this tree that are in the temple meet at the voice master, whatever server each
// of them plays on (CG_VoiceTempleFrame() of voice/cg_voice.c has the link, cg_templeLink says with whom: 1
// everybody, a word those who know it, 0 nobody). Each says where it stands ten times a second, and the others
// are drawn where they stand as wisps: a ball of blue light with a little smoke of it, and the name over it.
// Nobody is a figure there. The place is not drawn with a depth, so a wisp is seen through a spire it is behind.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

#define FOCUS_LEASH			512.0f		// how far the camera goes from the eyes of the body
#define FOCUS_SPEED			240.0f		// units a second
#define FOCUS_WALK			0.4f		//	and this much of it while the walk key is held
#define FOCUS_ACCEL			8.0f		// how fast it takes up speed and loses it
#define FOCUS_BOX			6.0f		// half the size of what slides along the walls
#define FOCUS_MOVED			48.0f		// the body is this far from where it stood: something moved it
#define FOCUS_SETTLE_MSEC	1000		// the body comes to a stop in this time, where it stands then is its place
#define FOCUS_KEYS			4			// keys of one command that are looked at
#define FOCUS_KEY_MSEC		1500		// the key of "focus" ends it after this time even if it was never let go
#define FOCUS_ASK_MSEC		1300		// a command for the server is said again after this time without an answer,
#define FOCUS_ASKS			4			//	this often at most
#define FOCUS_IN_MSEC		550			// the look comes in this time,
#define FOCUS_OUT_MSEC		320			//	and goes in this one after the world is back
#define FOCUS_MUFFLE		0.7f		// s_muffle while the look is all there
#define TEMPLE_EYE			44.0f		// the Vision Temple: the eye above the ground,
#define TEMPLE_TERRACE		203.0f		//	how far from the middle of the podium the terrace around it goes for a walker,
#define TEMPLE_WAY_HALF		23.0f		//	how wide the way to it is to each side of its middle,
#define TEMPLE_WAY_START	-516.0f		//	and where that way begins
#define TEMPLE_SPEED		190.0f		//	units a second,
#define TEMPLE_IN_MSEC		1800		//	and the time in which the place comes out of light
#define TEMPLE_GO_MSEC		900			// that long in it before the client goes on to the server that is the temple,
#define TEMPLE_ARRIVE_MSEC	1400		//	and the light of such a visit is gone after this long
#define TEMPLE_WISP_MSEC	3000		// nothing of somebody for this long: he left
#define TEMPLE_WISP_SIZE	64			// the picture a wisp is made of, a side of it
#define TEMPLE_NAME			32			// a name there with its end (VOICE_TEMPLE_NAME of bg_voice.h)
#define FOCUS_SMOKE			64			// the picture of the smoke of the plain look, a side of it
#define FOCUS_SMOKE_GRID	8			//	and the cells of the pattern it is made of

typedef enum {
	MOVE_FORWARD,
	MOVE_BACK,
	MOVE_LEFT,
	MOVE_RIGHT,
	MOVE_UP,
	MOVE_DOWN,
	MOVE_WALK,
	MOVE_COUNT
} focusMove_t;

static const char *const focusMoves[MOVE_COUNT] = {
	"+forward", "+back", "+moveleft", "+moveright", "+moveup", "+movedown", "+speed"
};
static const char *const focusChat[] = { "messagemode", "messagemode2", "messagemode3", "messagemode4" };

typedef struct focus_s {
	qboolean	active;					// the player is out of his body
	qboolean	temple;					// and in the Vision Temple, not in the still of the level
	qboolean	watcher;				// he has no body in the level: he only watches there
	qboolean	fromMenu;				// the game behind it was started by the main menu for this and ends with it
	qboolean	visit;					// the engine was told to go to the server that is the temple
	qboolean	instance;				// that server is the temple of the one the player is on, reached through it
	qboolean	arrived;				// the first frame with a game was seen
	int			arriveMsec;				// out of the light of a visit since then: into the temple server, or back from it
	qboolean	resync;					// it ended: the still stays until the snapshots are read anew
	qboolean	reselect;				// the snapshots were read anew this frame: the weapon bar is not to show for it
	int			selectTime;				//	cg.weaponSelectTime as it was
	qboolean	swapped;				// cg.time is the time of the still right now
	qboolean	binds;					// the engine says what a key is bound to
	qboolean	keyUp;					// the key of "focus" was let go since it began
	int			worldTime;				// cg.time of the moment the world stopped
	int			realTime;				// cg.time of the game, while swapped
	int			startMsec, lastMsec;	// trap->Milliseconds()
	int			endMsec;				// when it ended, while the look still fades
	float		look, lookAtEnd;		// how much of the look this frame has, and had when it ended
	qboolean	pass;					// the renderer draws the look, this file does not
	int			peekNum;				// the snapshot that was looked at last
	int			clientNum, pmType, damageEvent;		// of the body when it began
	vec3_t		body, eyes;				// where it stood
	vec3_t		origin, angles, velocity;			// of the camera
	float		turn[2];				// degrees for a count of the mouse, yaw and pitch
	int			keys[MOVE_COUNT][FOCUS_KEYS];		// the keys of the movement commands, -1: none
	qboolean	server;					// this server protects a body
	int			answer;					// what it said: -1 nothing yet, 0 not protected, 1 protected
	focusWhy_t	why;					//	and why not
	int			askMsec, asks;			// "jpfocus 1" and when it was last said
} focus_t;

// What has to outlast the player's time out of his body: the server still has to hear that he is back.
static struct {
	qboolean	owed;					// "jpfocus 0" was not answered yet
	int			msec, asks;
	const char	*ended;					// what brought him back the last time, for "focus_status"
} focusBack;

// for the scripts of tools/clienttests, which have no keys and no mouse: the camera steered by a command
static struct {
	vec3_t		wish;					// forward, right, up, as held keys would say it
	int			until;					// trap->Milliseconds()
} focusTest;

static focus_t		focus;
static snapshot_t	focusPeek;			// the newest snapshot, looked at and not taken in

// somebody else in the Vision Temple, by the number he has at the voice master
typedef struct templeWisp_s {
	int			heardMsec;				// when his place came last, 0: nobody
	vec3_t		target, shown;			// where he said he is, and where he is drawn on his way there
	char		name[TEMPLE_NAME];
} templeWisp_t;

static templeWisp_t	templeWisps[MAX_CLIENTS];
static templeLink_t	templeLink;			// how the link to the others stands
static int			templeMsec;			// when the wisps were moved last

// sounds of the game: it has them loaded for the Force Sight
static sfxHandle_t Focus_Sound( qboolean loop ) {
	static sfxHandle_t	sounds[2];
	static qboolean		loaded;

	if ( !loaded ) {
		loaded = qtrue;
		sounds[0] = trap->S_RegisterSound( "sound/weapons/force/see.wav" );
		sounds[1] = trap->S_RegisterSound( "sound/weapons/force/seeloop.wav" );
	}
	return sounds[loop ? 1 : 0];
}

static float Focus_Cvar( const char *name ) {
	char value[32];

	trap->Cvar_VariableStringBuffer( name, value, sizeof( value ) );
	return atof( value );
}

// is that key bound to that command?
static qboolean Focus_KeyIs( int key, const char *command ) {
	if ( focus.binds ) {
		char bound[MAX_STRING_CHARS];

		trap->ext.Key_GetBindingBuf( key, bound, sizeof( bound ) );
		return (qboolean)( !Q_stricmp( bound, command ) );
	}
	return (qboolean)( trap->Key_GetKey( command ) == key );
}

// the keys the player moves with
static void Focus_FindKeys( void ) {
	int move, key, n;

	for ( move = 0; move < MOVE_COUNT; move++ ) {
		for ( n = 0; n < FOCUS_KEYS; n++ ) {
			focus.keys[move][n] = -1;
		}
		if ( !focus.binds ) {
			focus.keys[move][0] = trap->Key_GetKey( focusMoves[move] );
			continue;
		}
		for ( key = 0, n = 0; key < MAX_KEYS && n < FOCUS_KEYS; key++ ) {
			if ( Focus_KeyIs( key, focusMoves[move] ) ) {
				focus.keys[move][n++] = key;
			}
		}
	}
}

static float Focus_Held( focusMove_t move ) {
	int n;

	for ( n = 0; n < FOCUS_KEYS; n++ ) {
		if ( focus.keys[move][n] >= 0 && trap->Key_IsDown( focus.keys[move][n] ) ) {
			return 1.0f;
		}
	}
	return 0.0f;
}

// does the engine have that call of the import table?
static qboolean Focus_Engine( const char *cvar ) {
	return (qboolean)( Focus_Cvar( cvar ) == 1.0f );
}

// why it can not begin now, NULL: it can
static const char *Focus_Refused( qboolean temple ) {
	static const char *const texts[2][4] = {
		{ "Focus: only while you play.", "Focus: not now.", "Focus: not in a duel.", "Focus: not while a menu is open." },
		{ "Vision Temple: only while you play.", "Vision Temple: not now.", "Vision Temple: not in a duel.",
			"Vision Temple: not while a menu is open." },
	};
	const char *const		*text = texts[temple ? 1 : 0];
	const playerState_t		*ps;

	if ( !cg.snap || cg.demoPlayback || ( cg.snap->snapFlags & SNAPFLAG_NOT_ACTIVE ) ) {
		return text[0];
	}
	ps = &cg.snap->ps;
	// (somebody who only watches has no body to leave in Focus. Into the temple he can go, unless he follows
	// somebody: then what he sees is not his own state.)
	if ( ( ps->pm_flags & PMF_FOLLOW ) || ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR && !temple ) ) {
		return text[0];
	}
	if ( ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		if ( ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL || ps->m_iVehicleNum || ps->zoomMode
			|| ps->groundEntityNum == ENTITYNUM_NONE ) {
			return text[1];
		}
		if ( ps->duelInProgress ) {
			return text[2];
		}
	}
	// (the game the main menu started for the temple opens the menu of a new player: that one is closed for it)
	if ( !( temple && cg_templeStart.integer ) && ( ( trap->Key_GetCatcher() & KEYCATCH_UI ) || CG_Menu3DActive() ) ) {
		return text[3];
	}
	// (asked for nothing yet: whether the renderer of this engine can draw the place)
	if ( temple && !( Focus_Engine( "cl_cgameTemple" ) && trap->ext.R_SetTempleScene( 0.0f, 0.0f ) ) ) {
		return "Vision Temple: only with the Vulkan renderer for now.";
	}
	return NULL;
}

// How high the ground of the Vision Temple is at a place: the clouds, and the five steps of the podium in its
// middle, each 6 high and 10 deep. The numbers of podium() in shaders/temple.frag.
static float Temple_Ground( float x, float y ) {
	const float r = sqrtf( x * x + y * y );

	return r >= 170.0f ? 0.0f : 6.0f * Q_min( 5.0f, floorf( ( 180.0f - r ) / 10.0f ) );
}

// One walks on gold there, not on the clouds: on the terrace around the podium and on the way that leads to it
// (terrace() and way() of shaders/temple.frag). A place outside of both is put to the nearest one inside.
static void Temple_Keep( vec3_t origin ) {
	const float	r = sqrtf( origin[0] * origin[0] + origin[1] * origin[1] );
	vec2_t		onTerrace, onWay;
	float		toTerrace, toWay;

	if ( r <= TEMPLE_TERRACE || ( origin[0] >= TEMPLE_WAY_START && origin[0] <= 0.0f && fabsf( origin[1] ) <= TEMPLE_WAY_HALF ) ) {
		return;
	}
	onTerrace[0] = origin[0] * TEMPLE_TERRACE / r;
	onTerrace[1] = origin[1] * TEMPLE_TERRACE / r;
	onWay[0] = Com_Clamp( TEMPLE_WAY_START, 0.0f, origin[0] );
	onWay[1] = Com_Clamp( -TEMPLE_WAY_HALF, TEMPLE_WAY_HALF, origin[1] );
	toTerrace = r - TEMPLE_TERRACE;
	toWay = sqrtf( ( origin[0] - onWay[0] ) * ( origin[0] - onWay[0] ) + ( origin[1] - onWay[1] ) * ( origin[1] - onWay[1] ) );
	if ( toWay < toTerrace ) {
		origin[0] = onWay[0];
		origin[1] = onWay[1];
	} else {
		origin[0] = onTerrace[0];
		origin[1] = onTerrace[1];
	}
}

static void Focus_Begin( qboolean temple ) {
	const playerState_t	*ps = &cg.snap->ps;
	int					caps;

	// the mouse is the camera's now: no pointer, no kept scoreboard
	CG_SelectLetGo();

	memset( &focus, 0, sizeof( focus ) );
	focus.active = qtrue;
	focus.binds = (qboolean)( Focus_Cvar( "cl_cgameBinds" ) == 1.0f );
	focus.worldTime = cg.time;
	focus.startMsec = focus.lastMsec = trap->Milliseconds();
	focus.peekNum = cg.latestSnapshotNum;
	focus.clientNum = ps->clientNum;
	focus.pmType = ps->pm_type;
	focus.damageEvent = ps->damageEvent;
	VectorCopy( ps->origin, focus.body );
	VectorCopy( ps->origin, focus.eyes );
	focus.eyes[2] += ps->viewheight;
	// it leaves from where the view of the game is
	VectorCopy( cg.refdef.vieworg, focus.origin );
	VectorCopy( cg.refdef.viewangles, focus.angles );
	focus.angles[ROLL] = 0.0f;
	focus.temple = temple;
	if ( temple ) {
		// one arrives on the way to the podium, the great ring and the sun ahead
		VectorSet( focus.origin, -470.0f, 0.0f, TEMPLE_EYE );
		VectorSet( focus.angles, -4.0f, 0.0f, 0.0f );
	}
	// the server protects the body where it says that it does
	caps = atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) );
	focus.watcher = (qboolean)( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR );
	focus.server = (qboolean)( !focus.watcher && ( caps & 16 ) != 0 );
	// The temple of this very server, where it has one and the engine can go there (the tunnel of
	// client/cl_visit.cpp). The server starts it when it is asked, so it is asked also for somebody who only
	// watches. One that did not answer the last time is left out until another visit arrived.
	focus.instance = (qboolean)( temple && ( caps & VISION_CAPS_INSTANCE ) && Focus_Engine( "cl_cgameVisit" )
		&& Focus_Engine( "cl_cgameTunnel" ) && !Focus_Cvar( "sv_running" ) && !Focus_Cvar( "cl_visitFailed" ) );
	if ( focus.instance ) {
		focus.server = qtrue;
	}
	focus.answer = -1;
	focusBack.owed = qfalse;
	Focus_FindKeys();
	// A key that is held stays held for the engine until it is let go: the body would walk on, or strike. Its
	// buttons are let go for it (a button command without a key lets go of all of them).
	trap->SendConsoleCommand( "-forward;-back;-moveleft;-moveright;-moveup;-movedown;-left;-right;-attack;-altattack;-use\n" );
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
	trap->S_StartLocalSound( Focus_Sound( qfalse ), CHAN_LOCAL_SOUND );
}

/*
=================
CG_FocusEnd

Back into the body. The still stays on the screen until the snapshots are read anew (CG_FocusSnapshots()):
between two frames nothing may find the cgame without a snapshot.
=================
*/
static void Focus_End( const char *why ) {
	if ( !focus.active ) {
		return;
	}
	focusBack.ended = why;
	focus.active = qfalse;
	focus.resync = qtrue;
	focus.endMsec = trap->Milliseconds();
	focus.lookAtEnd = focus.look;
	if ( focus.server && focus.asks ) {
		focusBack.owed = qtrue;
		focusBack.msec = focusBack.asks = 0;
	}
	trap->S_StartLocalSound( Focus_Sound( qfalse ), CHAN_LOCAL_SOUND );
	if ( !CG_ChatIsOpen() ) {
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
	}
	// the game the main menu started for the temple has nothing else to show: back to that menu
	if ( focus.fromMenu ) {
		trap->SendConsoleCommand( "disconnect\n" );
	}
}

// what the engine or another file of the cgame ends it with: Escape, a level that begins anew
void CG_FocusEnd( void ) {
	Focus_End( "Escape, or the level began anew" );
}

// how much the sound of the game is muffled, for s_muffle (menu3d/cg_menu3d.c sets it)
float CG_FocusMuffle( void ) {
	return focus.look * FOCUS_MUFFLE;
}

// the still is on the screen: out of the body, or just back and not yet at the newest snapshot
qboolean CG_FocusActive( void ) {
	return (qboolean)( focus.active || focus.resync );
}

// and what is on the screen is the Vision Temple: nothing of the level is drawn
qboolean CG_FocusTemple( void ) {
	return (qboolean)( focus.temple && CG_FocusActive() );
}

// the time the world is drawn with: the one it stopped at, or the one of the game
int CG_FocusTime( int time ) {
	return CG_FocusActive() ? focus.worldTime : time;
}

/*
=================
CG_Focus_f

"focus": out of the body, or back into it. "focus 1" only out of it, "focus 0" only back.
"temple": the same into the Vision Temple.
=================
*/
static void Focus_Command( qboolean temple ) {
	const char	*refused;
	const int	want = trap->Cmd_Argc() > 1 ? ( atoi( CG_Argv( 1 ) ) ? 1 : 0 ) : -1;

	// On the server that is the Vision Temple (game/status/g_temple.c) nobody leaves a body, and the temple is
	// where one is: its command is the way back, to the server the engine kept (client/cl_visit.cpp) or to
	// the main menu.
	if ( bg_visionTemple ) {
		if ( temple && want != 1 ) {
			trap->SendConsoleCommand( "disconnect\n" );
		}
		return;
	}
	if ( focus.active ) {
		if ( want != 1 ) {
			Focus_End( "the command" );
		}
		return;
	}
	if ( focus.resync || want == 0 ) {
		return;
	}
	refused = Focus_Refused( temple );
	if ( refused ) {
		trap->Print( "%s\n", CG_VisionText( refused ) );
		return;
	}
	Focus_Begin( temple );
}

void CG_Focus_f( void ) {
	Focus_Command( qfalse );
}

void CG_Temple_f( void ) {
	Focus_Command( qtrue );
}

/*
=================
CG_FocusServerCommand

"jpfocus <held> <why>" of the server: the body is protected, or it is not (focusWhy_t)
=================
*/
void CG_FocusServerCommand( void ) {
	const qboolean	held = (qboolean)( atoi( CG_Argv( 1 ) ) != 0 );
	const int		why = atoi( CG_Argv( 2 ) );

	if ( Focus_Cvar( "developer" ) ) {
		trap->Print( "Focus: the server says %i (%i)\n", held, why );
	}
	if ( !held ) {
		focusBack.owed = qfalse;
	}
	if ( focus.active ) {
		focus.answer = held ? 1 : 0;
		focus.why = ( why >= FOCUS_ENDED && why <= FOCUS_NOTNOW ) ? (focusWhy_t)why : FOCUS_NOTNOW;
	} else if ( held && !focusBack.owed ) {
		// it protects somebody who is back already: his "jpfocus 0" overtook this answer
		focusBack.owed = qtrue;
		focusBack.msec = focusBack.asks = 0;
	}
}

// what the server still has to hear: that the body is to be protected, or that its player is back
static void Focus_Server( int now ) {
	if ( focus.active && focus.server && focus.answer < 0 && focus.asks < FOCUS_ASKS
		&& ( !focus.asks || now - focus.askMsec >= FOCUS_ASK_MSEC ) ) {
		focus.asks++;
		focus.askMsec = now;
		trap->SendClientCommand( focus.instance ? "jpfocus 1 1" : "jpfocus 1" );
	} else if ( !focus.active && focusBack.owed && ( !focusBack.asks || now - focusBack.msec >= FOCUS_ASK_MSEC ) ) {
		if ( focusBack.asks >= FOCUS_ASKS ) {
			// (the first move of the player ends it for the server too)
			focusBack.owed = qfalse;
			return;
		}
		focusBack.asks++;
		focusBack.msec = now;
		trap->SendClientCommand( "jpfocus 0" );
	}
}

// what the newest snapshot says of the body: was it hit, moved, or is the game somewhere else by now
static void Focus_Check( const playerState_t *ps ) {
	vec3_t delta;

	// (who only watches has no body that could be hit or moved, he is back when he no longer watches)
	if ( focus.watcher ) {
		if ( ps->clientNum != focus.clientNum || ( ps->pm_flags & PMF_FOLLOW ) || ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
			Focus_End( "the player no longer watches as he did" );
		}
		return;
	}
	// it ran when the world stopped: where it comes to a stop is where it stands
	if ( trap->Milliseconds() - focus.startMsec < FOCUS_SETTLE_MSEC ) {
		VectorCopy( ps->origin, focus.body );
		VectorCopy( ps->origin, focus.eyes );
		focus.eyes[2] += ps->viewheight;
	}
	VectorSubtract( ps->origin, focus.body, delta );
	if ( ps->clientNum == focus.clientNum && ps->stats[STAT_HEALTH] <= 0 ) {
		Focus_End( "the body died" );
	} else if ( ps->clientNum != focus.clientNum || ( ps->pm_flags & PMF_FOLLOW ) || ps->pm_type != focus.pmType ) {
		Focus_End( "the player no longer plays as he did (watching, the end of the level)" );
	} else if ( ps->duelInProgress ) {
		Focus_End( "a duel" );
	} else if ( ps->damageEvent != focus.damageEvent ) {
		// (a hit, and not health that runs down by itself: the server counts every hit in here)
		Focus_End( "a hit on the body" );
	} else if ( VectorLength( delta ) > FOCUS_MOVED ) {
		Focus_End( "the body was moved" );
	}
}

/*
=================
CG_FocusSnapshots

Called by CG_ProcessSnapshots() before it reads snapshots. qtrue: the world stands still, take none in.
=================
*/
qboolean CG_FocusSnapshots( void ) {
	if ( focus.active && cg.latestSnapshotNum != focus.peekNum && trap->GetSnapshot( cg.latestSnapshotNum, &focusPeek ) ) {
		focus.peekNum = cg.latestSnapshotNum;
		// chat, votes, names and a restart of the level keep coming (the last one ends it, cg_servercmds.c)
		CG_ExecuteNewServerCommands( focusPeek.serverCommandSequence );
		if ( focus.active && !( focusPeek.snapFlags & SNAPFLAG_NOT_ACTIVE ) ) {
			Focus_Check( &focusPeek.ps );
		}
	}
	if ( focus.active ) {
		return qtrue;
	}
	if ( focus.resync ) {
		int i;

		// From the newest snapshot, as after connecting (CG_SetInitialSnapshot()). What the old one held is
		// no longer in sight until the new one says so.
		focus.resync = qfalse;
		if ( cg.snap ) {
			for ( i = 0; i < cg.snap->numEntities; i++ ) {
				cg_entities[cg.snap->entities[i].number].currentValid = qfalse;
			}
		}
		cg.snap = NULL;
		cg.nextSnap = NULL;
		cgs.processedSnapshotNum = cg.latestSnapshotNum - 1;
		// (that way shows the weapons as after a respawn, CG_Respawn(): CG_FocusFrame() takes it back)
		focus.selectTime = cg.weaponSelectTime;
		focus.reselect = qtrue;
	}
	return qfalse;
}

// along the walls, as far as the time of this frame takes it
static void Focus_Slide( float seconds ) {
	static const vec3_t	mins = { -FOCUS_BOX, -FOCUS_BOX, -FOCUS_BOX }, maxs = { FOCUS_BOX, FOCUS_BOX, FOCUS_BOX };
	trace_t	tr;
	vec3_t	end;
	int		bump;

	for ( bump = 0; bump < 3 && seconds > 0.0f; bump++ ) {
		VectorMA( focus.origin, seconds, focus.velocity, end );
		CG_Trace( &tr, focus.origin, mins, maxs, end, focus.clientNum, MASK_SOLID );
		if ( tr.startsolid ) {
			// it stands in something (the camera of the game may, behind a player at a wall): out of it first
			VectorCopy( end, focus.origin );
			return;
		}
		VectorCopy( tr.endpos, focus.origin );
		if ( tr.fraction >= 1.0f ) {
			return;
		}
		seconds *= 1.0f - tr.fraction;
		VectorMA( focus.velocity, -DotProduct( focus.velocity, tr.plane.normal ), tr.plane.normal, focus.velocity );
	}
}

// the camera for this frame: the keys of the movement commands, walls, the leash
static void Focus_Move( void ) {
	const int	now = trap->Milliseconds();
	const float	seconds = Com_Clamp( 0.0f, 0.1f, ( now - focus.lastMsec ) * 0.001f );
	const float	sensitivity = Focus_Cvar( "sensitivity" );
	vec3_t		forward, right, wish, delta;
	float		speed, dist;

	focus.lastMsec = now;
	// (what a count of the mouse turns the view of the game by)
	focus.turn[YAW] = Focus_Cvar( "m_yaw" ) * sensitivity;
	focus.turn[PITCH] = Focus_Cvar( "m_pitch" ) * sensitivity;

	VectorClear( wish );
	AngleVectors( focus.angles, forward, right, NULL );
	// nobody steers while he types
	if ( !CG_ChatIsOpen() && !( trap->Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		VectorMA( wish, Focus_Held( MOVE_FORWARD ) - Focus_Held( MOVE_BACK ), forward, wish );
		VectorMA( wish, Focus_Held( MOVE_RIGHT ) - Focus_Held( MOVE_LEFT ), right, wish );
		wish[2] += Focus_Held( MOVE_UP ) - Focus_Held( MOVE_DOWN );
	}
	if ( now < focusTest.until ) {
		VectorMA( wish, focusTest.wish[0], forward, wish );
		VectorMA( wish, focusTest.wish[1], right, wish );
		wish[2] += focusTest.wish[2];
	}
	if ( focus.temple ) {
		// On the ground of the Vision Temple: where one looks decides only the direction on it, the ground how
		// high the eye is, and nobody leaves the gold (Temple_Keep()).
		float ground;

		wish[2] = 0.0f;
		speed = VectorNormalize( wish ) > 0.0f ? TEMPLE_SPEED * ( Focus_Held( MOVE_WALK ) ? FOCUS_WALK : 1.0f ) : 0.0f;
		VectorScale( wish, speed, wish );
		VectorSubtract( wish, focus.velocity, delta );
		VectorMA( focus.velocity, Q_min( 1.0f, FOCUS_ACCEL * seconds ), delta, focus.velocity );
		focus.velocity[2] = 0.0f;
		focus.origin[0] += focus.velocity[0] * seconds;
		focus.origin[1] += focus.velocity[1] * seconds;
		Temple_Keep( focus.origin );
		// (a step is taken within a moment, and one sways a little as somebody who floats)
		ground = Temple_Ground( focus.origin[0], focus.origin[1] ) + TEMPLE_EYE + 1.5f * sinf( now * 0.0016f );
		focus.origin[2] += ( ground - focus.origin[2] ) * Q_min( 1.0f, 9.0f * seconds );
		return;
	}
	speed = VectorNormalize( wish ) > 0.0f ? FOCUS_SPEED * ( Focus_Held( MOVE_WALK ) ? FOCUS_WALK : 1.0f ) : 0.0f;
	VectorScale( wish, speed, wish );
	VectorSubtract( wish, focus.velocity, delta );
	VectorMA( focus.velocity, Q_min( 1.0f, FOCUS_ACCEL * seconds ), delta, focus.velocity );

	Focus_Slide( seconds );

	// the leash: no farther, and no speed that pulls on it
	VectorSubtract( focus.origin, focus.eyes, delta );
	dist = VectorNormalize( delta );
	if ( dist > FOCUS_LEASH ) {
		const float out = DotProduct( focus.velocity, delta );

		VectorMA( focus.eyes, FOCUS_LEASH, delta, focus.origin );
		if ( out > 0.0f ) {
			VectorMA( focus.velocity, -out, delta, focus.velocity );
		}
	}
}

/*
=================
CG_FocusFrame

Once a frame, before the scene is built. qtrue: the world stands still. cg.time is then the time it stopped
at, until CG_FocusReal().
=================
*/
qboolean CG_FocusFrame( void ) {
	if ( focus.reselect ) {
		focus.reselect = qfalse;
		cg.weaponSelectTime = focus.selectTime;
	}
	// The first frame of a game that is the end of a way through the light: on the server that is the temple, or
	// back from it on the server the engine kept (cl_visitBack, client/cl_visit.cpp). That one may still
	// protect the body: it is told that the player is back.
	if ( !focus.arrived && cg.snap && !( cg.snap->snapFlags & SNAPFLAG_NOT_ACTIVE ) ) {
		const qboolean back = (qboolean)( Focus_Cvar( "cl_visitBack" ) != 0.0f );

		focus.arrived = qtrue;
		if ( back ) {
			trap->Cvar_Set( "cl_visitBack", "0" );
		}
		if ( bg_visionTemple || back ) {
			focus.arriveMsec = trap->Milliseconds();
		}
		if ( back && !bg_visionTemple
			&& ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) ) & 16 ) ) {
			focusBack.owed = qtrue;
			focusBack.msec = focusBack.asks = 0;
		}
	}
	// "Vision Temple" of the main menu: it started this game only to stand behind the temple, and said so in a
	// cvar. Into the temple with the first frame there is, and back to the menu when it can not be.
	if ( cg_templeStart.integer && !focus.active && !focus.resync && cg.snap && !( cg.snap->snapFlags & SNAPFLAG_NOT_ACTIVE ) ) {
		const char *refused = Focus_Refused( qtrue );

		trap->Cvar_Set( "cg_templeStart", "0" );
		trap->OpenUIMenu( 0 );		// UIMENU_NONE: the menu a new player gets
		if ( refused ) {
			trap->Print( "%s\n", CG_VisionText( refused ) );
			trap->SendConsoleCommand( "disconnect\n" );
		} else {
			Focus_Begin( qtrue );
			focus.fromMenu = qtrue;
			trap->S_StopBackgroundTrack();
		}
	}
	if ( focus.active ) {
		const int catcher = trap->Key_GetCatcher();

		if ( ( catcher & KEYCATCH_UI ) && focus.fromMenu ) {
			// in the game the main menu started for the temple no menu has a say: the one a new player gets comes
			// a moment after his first frame
			trap->OpenUIMenu( 0 );
		} else if ( ( catcher & KEYCATCH_UI ) || cg.demoPlayback ) {
			Focus_End( "a menu" );
		} else {
			// (the chat box gives the keys back when it closes, and they are this file's again)
			if ( !( catcher & KEYCATCH_CGAME ) && !CG_ChatIsOpen() ) {
				trap->Key_SetCatcher( catcher | KEYCATCH_CGAME );
			}
			Focus_Move();
		}
	}
	if ( !CG_FocusActive() ) {
		return qfalse;
	}
	// the hum, on the listener himself so it has no place (after the loops of the frame were cleared, before
	// the sound takes them)
	trap->S_AddLoopingSound( cg.snap->ps.clientNum, focus.origin, vec3_origin, Focus_Sound( qtrue ) );
	focus.realTime = cg.time;
	cg.time = focus.worldTime;
	focus.swapped = qtrue;
	return qtrue;
}

// the scene is built: the time of the game again, for the 2D and what else goes on
void CG_FocusReal( void ) {
	if ( focus.swapped ) {
		focus.swapped = qfalse;
		cg.time = focus.realTime;
	}
}

// the view of this frame is the one of the camera
void CG_FocusView( void ) {
	if ( !CG_FocusActive() ) {
		return;
	}
	VectorCopy( focus.origin, cg.refdef.vieworg );
	VectorCopy( focus.angles, cg.refdef.viewangles );
	AnglesToAxis( focus.angles, cg.refdef.viewaxis );
}

/*
=================
CG_FocusPlace

The voice master handed on where somebody else is in the Vision Temple (voice/cg_voice.c)
=================
*/
void CG_FocusPlace( int member, const float *origin, float yaw, const char *name ) {
	templeWisp_t *wisp;

	(void)yaw;		// (a wisp looks nowhere)
	if ( member < 0 || member >= MAX_CLIENTS ) {
		return;
	}
	wisp = &templeWisps[member];
	VectorCopy( origin, wisp->target );
	if ( !wisp->heardMsec || trap->Milliseconds() - wisp->heardMsec > TEMPLE_WISP_MSEC ) {
		// he is new here: he does not come flying from where somebody else with that number stood
		VectorCopy( origin, wisp->shown );
	}
	wisp->heardMsec = trap->Milliseconds();
	Q_strncpyz( wisp->name, name, sizeof( wisp->name ) );
}

// how many others are in the Vision Temple right now
static int Temple_Others( int now ) {
	int i, count = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( templeWisps[i].heardMsec && now - templeWisps[i].heardMsec <= TEMPLE_WISP_MSEC ) {
			count++;
		}
	}
	return count;
}

// The picture a wisp is made of: a ball of light that is bright in its middle and runs out softly. Made once,
// 0: the engine can not make a shader of a picture (cl_cgameScene).
static qhandle_t Temple_WispShader( void ) {
	static qhandle_t	shader;
	static qboolean		tried;
	static byte			rgba[TEMPLE_WISP_SIZE * TEMPLE_WISP_SIZE * 4];
	int					x, y;

	if ( tried ) {
		return shader;
	}
	tried = qtrue;
	if ( !Focus_Engine( "cl_cgameScene" ) ) {
		return 0;
	}
	for ( y = 0; y < TEMPLE_WISP_SIZE; y++ ) {
		for ( x = 0; x < TEMPLE_WISP_SIZE; x++ ) {
			const float	dx = ( x + 0.5f ) / ( TEMPLE_WISP_SIZE * 0.5f ) - 1.0f, dy = ( y + 0.5f ) / ( TEMPLE_WISP_SIZE * 0.5f ) - 1.0f;
			const float	r = sqrtf( dx * dx + dy * dy );
			const float	soft = r < 1.0f ? ( 1.0f - r ) * ( 1.0f - r ) : 0.0f;
			const float	core = r < 0.5f ? ( 1.0f - r / 0.5f ) : 0.0f;
			byte		*out = &rgba[( y * TEMPLE_WISP_SIZE + x ) * 4];

			// Laid over what is behind it by its alpha, not added to it: the place is bright and warm, and
			// light added to that is white whatever its colour. A deep blue that is nearly white in its middle.
			out[0] = (byte)( 255.0f * ( 0.06f + 0.86f * core * core ) );
			out[1] = (byte)( 255.0f * ( 0.34f + 0.64f * core ) );
			out[2] = 255;
			out[3] = (byte)( 255.0f * Q_min( 1.0f, soft * 1.7f ) );
		}
	}
	shader = trap->ext.R_RegisterShaderRGBA( "*temple_wisp", rgba, TEMPLE_WISP_SIZE, TEMPLE_WISP_SIZE, 0 );
	return shader;
}

// one ball of that light into the scene, facing the eye
static void Temple_Ball( qhandle_t shader, const vec3_t at, float radius, float light ) {
	polyVert_t	verts[4];
	const byte	c = (byte)( 255.0f * Com_Clamp( 0.0f, 1.0f, light ) );
	int			i;

	for ( i = 0; i < 4; i++ ) {
		const float side = ( i == 0 || i == 3 ) ? 1.0f : -1.0f, high = ( i < 2 ) ? 1.0f : -1.0f;

		// (axis 1 of a view is to its left)
		VectorMA( at, side * radius, cg.refdef.viewaxis[1], verts[i].xyz );
		VectorMA( verts[i].xyz, high * radius, cg.refdef.viewaxis[2], verts[i].xyz );
		verts[i].st[0] = ( side > 0.0f ) ? 0.0f : 1.0f;
		verts[i].st[1] = ( high > 0.0f ) ? 0.0f : 1.0f;
		verts[i].modulate[0] = verts[i].modulate[1] = verts[i].modulate[2] = 255;
		verts[i].modulate[3] = c;
	}
	trap->R_AddPolysToScene( shader, 4, verts, 1 );
}

// The others, into the scene of the Vision Temple: each a wisp where he is, with a little smoke that rises from
// it, and his name over it.
static void Temple_Wisps( int now ) {
	const qhandle_t	shader = Temple_WispShader();
	const float		seconds = Com_Clamp( 0.0f, 0.1f, ( now - templeMsec ) * 0.001f );
	const qboolean	text = Focus_Engine( "cl_cgameScene" );
	int				i, puff;

	templeMsec = now;
	if ( !shader ) {
		return;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		templeWisp_t	*wisp = &templeWisps[i];
		const float		beat = now * 0.001f + i * 1.7f;
		vec3_t			at, delta;

		if ( !wisp->heardMsec || now - wisp->heardMsec > TEMPLE_WISP_MSEC ) {
			wisp->heardMsec = 0;
			continue;
		}
		// he is told ten times a second and drawn every frame: on his way to where he said he is
		VectorSubtract( wisp->target, wisp->shown, delta );
		VectorMA( wisp->shown, Q_min( 1.0f, 9.0f * seconds ), delta, wisp->shown );
		VectorCopy( wisp->shown, at );
		at[2] += -8.0f + 1.5f * sinf( beat * 1.3f );
		Temple_Ball( shader, at, 24.0f, 0.50f + 0.10f * sinf( beat * 2.1f ) );
		Temple_Ball( shader, at, 12.0f + 0.8f * sinf( beat * 3.7f ), 1.0f );
		// smoke of the same light that rises from it and thins out
		for ( puff = 0; puff < 5; puff++ ) {
			const float	age = fmodf( beat * 0.45f + puff * 0.2f, 1.0f );
			vec3_t		up;

			VectorCopy( at, up );
			up[0] += 5.0f * sinf( beat * 1.9f + puff * 2.4f ) * age;
			up[1] += 5.0f * cosf( beat * 1.6f + puff * 1.3f ) * age;
			up[2] += 6.0f + 24.0f * age;
			Temple_Ball( shader, up, 8.0f * ( 1.0f - 0.55f * age ), 0.75f * ( 1.0f - age ) );
		}
		if ( text && wisp->name[0] ) {
			// (dark: the sky behind it is bright)
			const float		height = 5.5f, width = trap->ext.R_Text3DWidth( wisp->name, height );
			const vec4_t	color = { 0.03f, 0.10f, 0.32f, 0.95f };
			vec3_t			right, from;

			VectorScale( cg.refdef.viewaxis[1], -1.0f, right );
			VectorCopy( at, from );
			from[2] += 22.0f;
			VectorMA( from, -0.5f * width, right, from );
			trap->ext.R_Text3D( from, right, cg.refdef.viewaxis[2], height, wisp->name, color );
		}
	}
}

/*
=================
Temple_Visit

The temple one sees in a level is drawn into the picture, and the others in it are lights. Where the voice
master names a game server that is the temple (CG_VoiceTempleServer()), that is the start of the way only: the
client goes on to that server, where everybody is a player. The engine keeps the server one is on meanwhile
("visit", client/cl_visit.cpp) and the body stays in it, so that server is asked to protect it first. The game
the main menu started for the temple is nothing to come back to: that one is left.
=================
*/
static void Temple_Visit( int now ) {
	char address[64];

	if ( !focus.temple || !focus.active || focus.visit || now - focus.startMsec < TEMPLE_GO_MSEC ) {
		return;
	}
	if ( !focus.fromMenu && ( !Focus_Engine( "cl_cgameVisit" ) || Focus_Cvar( "sv_running" ) ) ) {
		return;
	}
	// the answer of the server about the body, as long as one can come
	if ( focus.server && focus.answer < 0 && focus.asks < FOCUS_ASKS ) {
		return;
	}
	// the temple of the server one is on: through that server (its address as the engine has it, in numbers)
	if ( focus.instance && !focus.fromMenu ) {
		trap->Cvar_VariableStringBuffer( "cl_currentServerIP", address, sizeof( address ) );
		if ( address[0] ) {
			focus.visit = qtrue;
			trap->SendConsoleCommand( va( "visit %s 1\n", address ) );
			return;
		}
	}
	if ( !CG_VoiceTempleServer( address, sizeof( address ) ) ) {
		return;
	}
	focus.visit = qtrue;
	trap->SendConsoleCommand( va( "%s %s\n", focus.fromMenu ? "connect" : "visit", address ) );
}

/*
=================
CG_FocusScene

Once a frame before the scene is rendered: how much of the look it has, and the renderer is asked for it
=================
*/
void CG_FocusScene( void ) {
	const int now = trap->Milliseconds();

	Focus_Server( now );
	// who else is in the Vision Temple: asked while one is there, and told once that one left
	if ( focus.temple && focus.active ) {
		templeLink = CG_VoiceTempleFrame( qtrue, focus.origin, focus.angles[YAW], cgs.clientinfo[focus.clientNum].name );
		Temple_Visit( now );
	} else if ( templeLink != TEMPLE_LINK_OFF ) {
		templeLink = CG_VoiceTempleFrame( qfalse, vec3_origin, 0.0f, "" );
		memset( templeWisps, 0, sizeof( templeWisps ) );
	}

	if ( CG_FocusActive() ) {
		focus.look = Com_Clamp( 0.0f, 1.0f, ( now - focus.startMsec ) / (float)FOCUS_IN_MSEC );
	} else if ( focus.endMsec ) {
		focus.look = focus.lookAtEnd * ( 1.0f - ( now - focus.endMsec ) / (float)FOCUS_OUT_MSEC );
		if ( focus.look <= 0.0f ) {
			focus.endMsec = 0;
		}
	} else {
		focus.look = 0.0f;
	}
	focus.pass = qfalse;
	// A server that is the Vision Temple (game/status/g_temple.c): its map has no picture, the renderer draws the
	// place behind the players of the scene.
	if ( bg_visionTemple && Focus_Engine( "cl_cgameTemple" ) ) {
		cg.refdef.rdflags |= RDF_VISION_TEMPLE;
		trap->ext.R_SetTempleScene( 1.0f, cg.time * 0.001f );
	}
	if ( focus.look <= 0.0f ) {
		focus.look = 0.0f;
		return;
	}
	if ( focus.temple ) {
		// The Vision Temple: while one is there the scene is one without a level, whatever was put into it, and
		// the renderer draws the place behind it. (Back in the level nothing of it is left but a light that
		// goes, CG_FocusTint().)
		if ( CG_FocusActive() ) {
			trap->R_ClearScene();
			cg.refdef.rdflags = RDF_NOWORLDMODEL | RDF_VISION_TEMPLE;
			trap->ext.R_SetTempleScene( Com_Clamp( 0.0f, 1.0f, ( now - focus.startMsec ) / (float)TEMPLE_IN_MSEC ),
				( now - focus.startMsec ) * 0.001f );
			Temple_Wisps( now );
		}
		focus.pass = qtrue;
		return;
	}
	if ( Focus_Engine( "cl_cgameSpirit" ) ) {
		focus.pass = trap->ext.R_SetSpiritWorld( focus.look, ( now - focus.startMsec ) * 0.001f );
	}
}

// The smoke of the plain look: a picture of soft patches that fits to itself at its edges, made once. 0: the
// engine can not make a shader of a picture (cl_cgameScene).
static qhandle_t Focus_Smoke( void ) {
	static qhandle_t	shader;
	static qboolean		tried;
	static byte			rgba[FOCUS_SMOKE * FOCUS_SMOKE * 4];
	float				grid[FOCUS_SMOKE_GRID][FOCUS_SMOKE_GRID];
	unsigned int		seed = 20261012u;
	int					x, y;

	if ( tried ) {
		return shader;
	}
	tried = qtrue;
	if ( !Focus_Engine( "cl_cgameScene" ) ) {
		return 0;
	}
	for ( y = 0; y < FOCUS_SMOKE_GRID; y++ ) {
		for ( x = 0; x < FOCUS_SMOKE_GRID; x++ ) {
			seed = seed * 1664525u + 1013904223u;
			grid[y][x] = ( ( seed >> 8 ) & 0xFFFF ) / 65535.0f;
		}
	}
	for ( y = 0; y < FOCUS_SMOKE; y++ ) {
		for ( x = 0; x < FOCUS_SMOKE; x++ ) {
			const float	gx = x * ( FOCUS_SMOKE_GRID / (float)FOCUS_SMOKE ), gy = y * ( FOCUS_SMOKE_GRID / (float)FOCUS_SMOKE );
			const int	x0 = (int)gx, y0 = (int)gy;
			const int	x1 = ( x0 + 1 ) % FOCUS_SMOKE_GRID, y1 = ( y0 + 1 ) % FOCUS_SMOKE_GRID;
			float		fx = gx - x0, fy = gy - y0, v;
			byte		*out = &rgba[( y * FOCUS_SMOKE + x ) * 4];

			fx = fx * fx * ( 3.0f - 2.0f * fx );
			fy = fy * fy * ( 3.0f - 2.0f * fy );
			v = ( grid[y0][x0] * ( 1.0f - fx ) + grid[y0][x1] * fx ) * ( 1.0f - fy )
				+ ( grid[y1][x0] * ( 1.0f - fx ) + grid[y1][x1] * fx ) * fy;
			out[0] = 140;
			out[1] = 175;
			out[2] = 255;
			out[3] = (byte)( v * v * 150.0f );
		}
	}
	shader = trap->ext.R_RegisterShaderRGBA( "*focus_smoke", rgba, FOCUS_SMOKE, FOCUS_SMOKE, 0 );
	return shader;
}

/*
=================
CG_FocusLoading

While a level loads (CG_DrawInformation()): on the way to the server that is the Vision Temple, or back from a
visit to it (client/cl_visit.cpp), the screen is the light one goes through and nothing else. The first frames
of the game come out of that light (focus.arriveMsec). qtrue: it was drawn.
=================
*/
qboolean CG_FocusLoading( void ) {
	static const vec4_t light = { 1.0f, 0.93f, 0.78f, 1.0f };

	if ( !Focus_Cvar( "cl_visit" ) && !Focus_Cvar( "cl_visitBack" )
		&& !( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_visionCaps" ) ) & VISION_CAPS_TEMPLE ) ) {
		return qfalse;
	}
	trap->R_SetColor( light );
	CG_DrawPicWide( trap->R_RegisterShaderNoMip( "white" ) );
	trap->R_SetColor( NULL );
	return qtrue;
}

/*
=================
CG_FocusTint

Once a frame under the 2D: the plain look, where the renderer has no pass for it
=================
*/
void CG_FocusTint( void ) {
	static const float	tiles[2] = { 520.0f, 300.0f }, drift[2][2] = { { 9.0f, -14.0f }, { -17.0f, -23.0f } };
	const float			left = -cgs.bias2D, width = SCREEN_WIDTH + 2.0f * cgs.bias2D;
	const float			seconds = ( trap->Milliseconds() - focus.startMsec ) * 0.001f;
	vec4_t				color = { 0.02f, 0.06f, 0.22f, 0.0f };
	qhandle_t			smoke;
	int					layer;

	if ( focus.arriveMsec ) {
		// out of the light of a visit: the screen was that light while the level loaded
		const float	gone = ( trap->Milliseconds() - focus.arriveMsec ) / (float)TEMPLE_ARRIVE_MSEC;
		vec4_t		light = { 1.0f, 0.93f, 0.78f, 0.0f };

		if ( gone >= 1.0f ) {
			focus.arriveMsec = 0;
		} else {
			light[3] = 1.0f - gone * gone * ( 3.0f - 2.0f * gone );
			CG_Place2D( PLACE_CENTER );
			CG_FillRect( left, 0.0f, width, SCREEN_HEIGHT, light );
		}
	}
	if ( focus.look > 0.0f && focus.temple && !CG_FocusActive() ) {
		// back from the Vision Temple: the light one came out of, once more, and gone within a moment
		vec4_t light = { 1.0f, 0.93f, 0.78f, 0.0f };

		CG_Place2D( PLACE_CENTER );
		light[3] = 0.85f * focus.look;
		CG_FillRect( left, 0.0f, width, SCREEN_HEIGHT, light );
		return;
	}
	if ( focus.look <= 0.0f || focus.pass ) {
		return;
	}
	CG_Place2D( PLACE_CENTER );
	color[3] = 0.46f * focus.look;
	CG_FillRect( left, 0.0f, width, SCREEN_HEIGHT, color );

	smoke = Focus_Smoke();
	if ( !smoke ) {
		return;
	}
	// (thin: it lies over the whole picture and knows no distance, the first try hid the level behind it)
	VectorSet4( color, 1.0f, 1.0f, 1.0f, 0.26f * focus.look );
	trap->R_SetColor( color );
	for ( layer = 0; layer < 2; layer++ ) {
		const float	tile = tiles[layer];
		// the picture fits to itself at its edges: tiles of it that wander, begun one tile outside of the screen
		const float	x0 = left - tile + fmodf( fmodf( seconds * drift[layer][0], tile ) + tile, tile );
		const float	y0 = -tile + fmodf( fmodf( seconds * drift[layer][1], tile ) + tile, tile );
		float		x, y;

		for ( y = y0; y < SCREEN_HEIGHT; y += tile ) {
			for ( x = x0; x < left + width; x += tile ) {
				CG_DrawPic( x, y, tile, tile, smoke );
			}
		}
	}
	trap->R_SetColor( NULL );
}

/*
=================
CG_FocusMouseEvent

The mouse while the cgame holds the keys. qtrue: it turned the camera.
=================
*/
qboolean CG_FocusMouseEvent( int dx, int dy ) {
	if ( !focus.active || CG_ChatIsOpen() ) {
		return qfalse;
	}
	focus.angles[YAW] = AngleNormalize360( focus.angles[YAW] - dx * focus.turn[YAW] );
	focus.angles[PITCH] = Com_Clamp( -89.0f, 89.0f, focus.angles[PITCH] + dy * focus.turn[PITCH] );
	return qtrue;
}

/*
=================
CG_FocusKeyEvent

A key while the cgame holds the keys. qtrue: it was for this file, which is every key while one is out of the
body (the game would give the keys back to the engine on any other one).
=================
*/
qboolean CG_FocusKeyEvent( int key, qboolean down ) {
	size_t i;

	if ( !focus.active ) {
		return qfalse;
	}
	if ( Focus_KeyIs( key, focus.temple ? "temple" : "focus" ) ) {
		// the key that began it is still down, and a key that is held comes again and again
		if ( !down ) {
			focus.keyUp = qtrue;
		} else if ( focus.keyUp || trap->Milliseconds() - focus.startMsec > FOCUS_KEY_MSEC ) {
			Focus_End( "the key" );
		}
		return qtrue;
	}
	if ( !down ) {
		return qtrue;
	}
	for ( i = 0; i < ARRAY_LEN( focusChat ); i++ ) {
		if ( Focus_KeyIs( key, focusChat[i] ) ) {
			trap->SendConsoleCommand( va( "%s\n", focusChat[i] ) );
			break;
		}
	}
	return qtrue;
}

/*
=================
CG_FocusDraw

Once a frame in the 2D while one is out of the body: how to come back
=================
*/
void CG_FocusDraw( void ) {
	const qboolean	skin = CG_HudNewLook();
	const int		font = skin ? SKIN_FONT_SANS : FONT_SMALL;
	const float		scale = skin ? 0.5f : 0.6f;
	const char		*key, *text, *body, *others = NULL;
	vec4_t			color = { 0.82f, 0.90f, 1.0f, 0.95f };
	vec4_t			strip = { 0.02f, 0.04f, 0.10f, 0.55f };
	float			width, top = 439.0f;

	if ( bg_visionTemple ) {
		// the server that is the temple: how to leave it
		key = CG_KeyLabel( trap->Key_GetKey( "temple" ) );
		text = key ? CG_VisionTextWith( "Vision Temple. %s: back", key ) : CG_VisionText( "Vision Temple. /temple: back" );
		width = CG_Text_Width( text, scale, font ) + 20.0f;
		top = 452.0f;
		if ( skin && Skin_Ready() ) {
			Skin_Panel( ( SCREEN_WIDTH - width ) * 0.5f, top, width, 470.0f - top, 5.0f, strip, NULL );
		} else {
			CG_FillRect( ( SCREEN_WIDTH - width ) * 0.5f, top, width, 470.0f - top, strip );
		}
		CG_Text_Paint( ( SCREEN_WIDTH - CG_Text_Width( text, scale, font ) ) * 0.5f, top + 3.0f, scale, color, text, 0, 0, 0, font );
		return;
	}
	if ( !focus.active ) {
		return;
	}
	if ( focus.temple ) {
		key = CG_KeyLabel( trap->Key_GetKey( "temple" ) );
		text = key ? CG_VisionTextWith( "Vision Temple. %s or Esc: back", key ) : CG_VisionText( "Vision Temple. Esc: back" );
	} else {
		key = CG_KeyLabel( trap->Key_GetKey( "focus" ) );
		text = key ? CG_VisionTextWith( "Focus. %s or Esc: back", key ) : CG_VisionText( "Focus. Esc: back" );
	}

	// and what happens to the body meanwhile
	if ( focus.watcher ) {
		body = NULL;
	} else if ( focus.answer > 0 ) {
		body = CG_VisionText( "Your body is protected." );
	} else if ( !focus.server || focus.answer < 0 ) {
		body = CG_VisionText( "A hit on your body brings you back." );
	} else {
		static const char *const why[] = {
			"Your body is not protected.",									// FOCUS_ENDED
			"This server does not protect your body here.",					// FOCUS_OFF
			"Your body is not protected: you were in a fight a moment ago.",	// FOCUS_FIGHT
			"Your body is not protected: your last time was a moment ago.",	// FOCUS_WAIT
			"Your body is not protected right now.",						// FOCUS_NOTNOW
		};

		body = CG_VisionText( why[focus.why] );
	}
	// in the Vision Temple: who else is there, in a line between the two
	if ( focus.temple ) {
		const int count = Temple_Others( trap->Milliseconds() );

		if ( count > 1 ) {
			others = CG_VisionTextWith( "%i others are here.", va( "%i", count ) );
		} else if ( count == 1 ) {
			others = CG_VisionText( "One other is here." );
		} else if ( templeLink == TEMPLE_LINK_ASKING ) {
			others = CG_VisionText( "Looking for the others ..." );
		} else if ( templeLink == TEMPLE_LINK_NONE ) {
			others = CG_VisionText( "The voice server does not answer: you are alone here." );
		} else if ( templeLink == TEMPLE_LINK_FULL ) {
			others = CG_VisionText( "The temple is full: you are alone here." );
		} else {
			others = CG_VisionText( "You are alone here." );
		}
	}
	// all on a dark strip: the own body often stands right behind them
	// (who has no body in the level gets no line about one: what is said of the others stands in its place)
	if ( !body ) {
		body = others ? others : "";
		others = NULL;
	}
	width = Q_max( CG_Text_Width( text, scale, font ), CG_Text_Width( body, scale * 0.85f, font ) );
	if ( others ) {
		width = Q_max( width, CG_Text_Width( others, scale * 0.85f, font ) );
		top -= 12.0f;
	}
	width += 20.0f;
	if ( skin && Skin_Ready() ) {
		Skin_Panel( ( SCREEN_WIDTH - width ) * 0.5f, top, width, 470.0f - top, 5.0f, strip, NULL );
	} else {
		CG_FillRect( ( SCREEN_WIDTH - width ) * 0.5f, top, width, 470.0f - top, strip );
	}
	CG_Text_Paint( ( SCREEN_WIDTH - CG_Text_Width( text, scale, font ) ) * 0.5f, top + 3.0f, scale, color, text, 0, 0, 0, font );
	color[3] = 0.75f;
	if ( others ) {
		CG_Text_Paint( ( SCREEN_WIDTH - CG_Text_Width( others, scale * 0.85f, font ) ) * 0.5f, top + 16.0f, scale * 0.85f, color, others, 0, 0, 0, font );
	}
	CG_Text_Paint( ( SCREEN_WIDTH - CG_Text_Width( body, scale * 0.85f, font ) ) * 0.5f, 455.0f, scale * 0.85f, color, body, 0, 0, 0, font );
}

/*
=================
CG_FocusStatus_f

"focus_status": where the player is and what the server said, as "voice_status" and "peers" say theirs
=================
*/
void CG_FocusStatus_f( void ) {
	// (for whoever looks into it: where the players of the snapshot are and what they do)
	if ( cg.snap && Focus_Cvar( "developer" ) ) {
		int i;

		trap->Print( "  own body: at %.0f %.0f %.0f, legs %i, eFlags %x\n", cg.snap->ps.origin[0], cg.snap->ps.origin[1],
			cg.snap->ps.origin[2], cg.snap->ps.legsAnim, cg.snap->ps.eFlags );
		for ( i = 0; i < cg.snap->numEntities; i++ ) {
			const entityState_t *es = &cg.snap->entities[i];

			if ( es->number < MAX_CLIENTS && es->eType == ET_PLAYER ) {
				trap->Print( "  %2i %-12s at %.0f %.0f %.0f, legs %i, eFlags %x\n", es->number, cgs.clientinfo[es->number].name,
					es->pos.trBase[0], es->pos.trBase[1], es->pos.trBase[2], es->legsAnim, es->eFlags );
			}
		}
	}
	if ( !focus.active ) {
		trap->Print( "Focus: in the body%s%s.\n", focusBack.ended ? ", back by: " : "", focusBack.ended ? focusBack.ended : "" );
		return;
	}
	if ( focus.temple ) {
		static const char *const links[] = { "nobody is asked", "asking the voice master", "open", "the voice master does not answer", "the voice master is full" };
		int i;

		trap->Print( "Vision Temple: at %.0f %.0f %.0f, looking %.0f. The others: %s, %i here.\n", focus.origin[0], focus.origin[1],
			focus.origin[2], focus.angles[YAW], links[templeLink], Temple_Others( trap->Milliseconds() ) );
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			if ( templeWisps[i].heardMsec ) {
				trap->Print( "  %2i %s at %.0f %.0f %.0f\n", i, templeWisps[i].name, templeWisps[i].target[0], templeWisps[i].target[1], templeWisps[i].target[2] );
			}
		}
		{
			char address[64];

			trap->Print( "  the temple as a server: %s, the engine %s a server meanwhile, this one is %s, on the way: %s\n",
				CG_VoiceTempleServer( address, sizeof( address ) ) ? address : "none named",
				Focus_Engine( "cl_cgameVisit" ) ? "keeps" : "can not keep", Focus_Cvar( "sv_running" ) ? "of this program" : "another program",
				focus.visit ? "yes" : "no" );
		}
	}
	trap->Print( "Focus: out of the body for %.1f s, %.0f units from it, the world stands still at %i. The server: %s.\n",
		( trap->Milliseconds() - focus.startMsec ) * 0.001f, Distance( focus.origin, focus.eyes ), focus.worldTime,
		!focus.server ? "protects no body" : focus.answer > 0 ? "protects the body" : focus.answer < 0 ? "has not answered"
		: va( "does not protect the body (%i)", (int)focus.why ) );
	// (for whoever looks into it: the snapshot the still is made of, and the newest one that was looked at)
	if ( Focus_Cvar( "developer" ) ) {
		trap->Print( "  still: snapshot of %i, legs %i torso %i, predicted legs %i torso %i. newest: %i of %i, legs %i torso %i, at %.0f %.0f %.0f, eFlags %x\n",
			cg.snap->serverTime, cg.snap->ps.legsAnim, cg.snap->ps.torsoAnim, cg.predictedPlayerState.legsAnim,
			cg.predictedPlayerState.torsoAnim, focus.peekNum, focusPeek.serverTime, focusPeek.ps.legsAnim, focusPeek.ps.torsoAnim,
			focusPeek.ps.origin[0], focusPeek.ps.origin[1], focusPeek.ps.origin[2], focusPeek.ps.eFlags );
	}
}

/*
=================
CG_FocusMove_f, CG_FocusTurn_f

For the scripts of tools/clienttests, which can not hold a key or move the mouse.
"cg_focusmove <forward> <right> <up> <msec>": the camera goes that way for that time, each -1 to 1.
"cg_focusturn <yaw> <pitch>": it turns by that many degrees, left and down are positive.
=================
*/
void CG_FocusMove_f( void ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		focusTest.wish[i] = Com_Clamp( -1.0f, 1.0f, atof( CG_Argv( 1 + i ) ) );
	}
	focusTest.until = trap->Milliseconds() + atoi( CG_Argv( 4 ) );
}

void CG_FocusTurn_f( void ) {
	if ( !focus.active ) {
		return;
	}
	focus.angles[YAW] = AngleNormalize360( focus.angles[YAW] + atof( CG_Argv( 1 ) ) );
	focus.angles[PITCH] = Com_Clamp( -89.0f, 89.0f, focus.angles[PITCH] + atof( CG_Argv( 2 ) ) );
}

// the cgame was loaded anew, or the level begins anew: in the body
void CG_FocusInit( void ) {
	memset( &focus, 0, sizeof( focus ) );
	memset( &focusBack, 0, sizeof( focusBack ) );
	memset( &focusTest, 0, sizeof( focusTest ) );
	memset( templeWisps, 0, sizeof( templeWisps ) );
	// (a link that is open is closed by the voice chat when the module ends, CG_VoiceShutdown())
	templeLink = TEMPLE_LINK_OFF;
}
