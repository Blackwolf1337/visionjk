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
// All of it happens in this client. Another client sees a player who stands still.

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
	qboolean	resync;					// it ended: the still stays until the snapshots are read anew
	qboolean	swapped;				// cg.time is the time of the still right now
	qboolean	binds;					// the engine says what a key is bound to
	qboolean	keyUp;					// the key of "focus" was let go since it began
	int			worldTime;				// cg.time of the moment the world stopped
	int			realTime;				// cg.time of the game, while swapped
	int			startMsec, lastMsec;	// trap->Milliseconds()
	int			peekNum;				// the snapshot that was looked at last
	int			clientNum, pmType, damageEvent;		// of the body when it began
	vec3_t		body, eyes;				// where it stood
	vec3_t		origin, angles, velocity;			// of the camera
	float		turn[2];				// degrees for a count of the mouse, yaw and pitch
	int			keys[MOVE_COUNT][FOCUS_KEYS];		// the keys of the movement commands, -1: none
} focus_t;

static focus_t		focus;
static snapshot_t	focusPeek;			// the newest snapshot, looked at and not taken in

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

// why it can not begin now, NULL: it can
static const char *Focus_Refused( void ) {
	const playerState_t *ps;

	if ( !cg.snap || cg.demoPlayback || ( cg.snap->snapFlags & SNAPFLAG_NOT_ACTIVE ) ) {
		return "Focus: only while you play.";
	}
	ps = &cg.snap->ps;
	if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ( ps->pm_flags & PMF_FOLLOW ) ) {
		return "Focus: only while you play.";
	}
	if ( ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL || ps->m_iVehicleNum || ps->zoomMode
		|| ps->groundEntityNum == ENTITYNUM_NONE ) {
		return "Focus: not now.";
	}
	if ( ps->duelInProgress ) {
		return "Focus: not in a duel.";
	}
	if ( ( trap->Key_GetCatcher() & KEYCATCH_UI ) || CG_Menu3DActive() ) {
		return "Focus: not while a menu is open.";
	}
	return NULL;
}

static void Focus_Begin( void ) {
	const playerState_t *ps = &cg.snap->ps;

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
	Focus_FindKeys();
	// A key that is held stays held for the engine until it is let go: the body would walk on, or strike. Its
	// buttons are let go for it (a button command without a key lets go of all of them).
	trap->SendConsoleCommand( "-forward;-back;-moveleft;-moveright;-moveup;-movedown;-left;-right;-attack;-altattack;-use\n" );
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
}

/*
=================
CG_FocusEnd

Back into the body. The still stays on the screen until the snapshots are read anew (CG_FocusSnapshots()):
between two frames nothing may find the cgame without a snapshot.
=================
*/
void CG_FocusEnd( void ) {
	if ( !focus.active ) {
		return;
	}
	focus.active = qfalse;
	focus.resync = qtrue;
	if ( !CG_ChatIsOpen() ) {
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
	}
}

// the still is on the screen: out of the body, or just back and not yet at the newest snapshot
qboolean CG_FocusActive( void ) {
	return (qboolean)( focus.active || focus.resync );
}

// the time the world is drawn with: the one it stopped at, or the one of the game
int CG_FocusTime( int time ) {
	return CG_FocusActive() ? focus.worldTime : time;
}

/*
=================
CG_Focus_f

"focus": out of the body, or back into it
=================
*/
void CG_Focus_f( void ) {
	const char *refused;

	if ( focus.active ) {
		CG_FocusEnd();
		return;
	}
	if ( focus.resync ) {
		return;
	}
	refused = Focus_Refused();
	if ( refused ) {
		trap->Print( "%s\n", CG_VisionText( refused ) );
		return;
	}
	Focus_Begin();
}

// what the newest snapshot says of the body: was it hit, moved, or is the game somewhere else by now
static void Focus_Check( const playerState_t *ps ) {
	vec3_t delta;

	// it ran when the world stopped: where it comes to a stop is where it stands
	if ( trap->Milliseconds() - focus.startMsec < FOCUS_SETTLE_MSEC ) {
		VectorCopy( ps->origin, focus.body );
		VectorCopy( ps->origin, focus.eyes );
		focus.eyes[2] += ps->viewheight;
	}
	VectorSubtract( ps->origin, focus.body, delta );
	if ( ps->clientNum != focus.clientNum || ( ps->pm_flags & PMF_FOLLOW ) || ps->pm_type != focus.pmType
		|| ps->stats[STAT_HEALTH] <= 0 || ps->duelInProgress
		// (a hit, and not health that runs down by itself: the server counts every hit in here)
		|| ps->damageEvent != focus.damageEvent
		|| VectorLength( delta ) > FOCUS_MOVED ) {
		CG_FocusEnd();
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
	// nobody steers while he types
	if ( !CG_ChatIsOpen() && !( trap->Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		AngleVectors( focus.angles, forward, right, NULL );
		VectorMA( wish, Focus_Held( MOVE_FORWARD ) - Focus_Held( MOVE_BACK ), forward, wish );
		VectorMA( wish, Focus_Held( MOVE_RIGHT ) - Focus_Held( MOVE_LEFT ), right, wish );
		wish[2] += Focus_Held( MOVE_UP ) - Focus_Held( MOVE_DOWN );
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
	if ( focus.active ) {
		const int catcher = trap->Key_GetCatcher();

		if ( ( catcher & KEYCATCH_UI ) || cg.demoPlayback ) {
			// a menu came up
			CG_FocusEnd();
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
	if ( Focus_KeyIs( key, "focus" ) ) {
		// the key that began it is still down, and a key that is held comes again and again
		if ( !down ) {
			focus.keyUp = qtrue;
		} else if ( focus.keyUp || trap->Milliseconds() - focus.startMsec > FOCUS_KEY_MSEC ) {
			CG_FocusEnd();
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
	const char		*key, *text;
	vec4_t			color = { 0.78f, 0.86f, 1.0f, 0.85f };

	if ( !focus.active ) {
		return;
	}
	key = CG_KeyLabel( trap->Key_GetKey( "focus" ) );
	text = key ? CG_VisionTextWith( "Focus. %s or Esc: back", key ) : CG_VisionText( "Focus. Esc: back" );
	CG_Text_Paint( ( SCREEN_WIDTH - CG_Text_Width( text, scale, font ) ) * 0.5f, 452.0f, scale, color, text, 0, 0, 0, font );
}

// the cgame was loaded anew, or the level begins anew: in the body
void CG_FocusInit( void ) {
	memset( &focus, 0, sizeof( focus ) );
}
