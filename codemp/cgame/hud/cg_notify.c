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

// Notifications (v_notify), after the ones of the VisionJK of 2019 (h_notification*): lines at the left edge that
// slide in, stay a while and slide out again. What comes: a tell to you and a chat line with your name
// (chat/cg_chat.c), a line of a boombox for you (boombox/cg_boombox.c), a newer version of the game (v_updateState
// of the engine) and what the command v_notify_show is given. The chat box and the console keep their lines as before.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

#define NOTIFY_MAX			8			// lines at once, a new one pushes the oldest out
#define NOTIFY_SLIDE		250			// ms to slide in and to slide out
#define NOTIFY_UPDATE_POLL	1000		// ms between two looks at v_updateState

typedef struct {
	notifyKind_t	kind;
	char			text[256];
	int				time;
} notifyLine_t;

static notifyLine_t	notify[NOTIFY_MAX];
static int			notifyCount;
static int			notifyUpdateState, notifyUpdatePoll;

/*
=================
CG_NotifyAdd

Colour codes stay, line breaks become spaces
=================
*/
void CG_NotifyAdd( notifyKind_t kind, const char *text ) {
	notifyLine_t	*line;
	char			*c;

	if ( !v_notify.integer || !text || !text[0] ) {
		return;
	}
	if ( notifyCount == NOTIFY_MAX ) {
		memmove( notify, notify + 1, sizeof( notify[0] ) * ( NOTIFY_MAX - 1 ) );
		notifyCount--;
	}
	line = &notify[notifyCount++];
	line->kind = kind;
	Q_strncpyz( line->text, text, sizeof( line->text ) );
	for ( c = line->text; *c; c++ ) {
		if ( (unsigned char)*c < ' ' ) {
			*c = ' ';
		}
	}
	line->time = cg.time;
	if ( v_notify.integer >= 2 ) {
		trap->S_StartLocalSound( cgs.media.tayNotifySound, CHAN_LOCAL_SOUND );
	}
}

// the command v_notify_show <text>: for scripts and binds (not "v_notify": a command with the name of a cvar
// never reaches the cgame, the engine sets the cvar)
void CG_Notify_f( void ) {
	char text[256];

	trap->Cmd_Args( text, sizeof( text ) );
	if ( !text[0] ) {
		trap->Print( "usage: v_notify_show <text>\n" );
		return;
	}
	CG_NotifyAdd( NOTIFY_EVENT, text );
}

// v_updateState of the engine (client/cl_update.cpp): 1 a newer version is out, its line in v_updateText
static void Notify_CheckUpdate( void ) {
	char	value[16], text[128];
	int		state;

	if ( cg.time < notifyUpdatePoll ) {
		return;
	}
	notifyUpdatePoll = cg.time + NOTIFY_UPDATE_POLL;
	trap->Cvar_VariableStringBuffer( "v_updateState", value, sizeof( value ) );
	state = atoi( value );
	if ( state == 1 && notifyUpdateState != 1 ) {
		trap->Cvar_VariableStringBuffer( "v_updateText", text, sizeof( text ) );
		CG_NotifyAdd( NOTIFY_EVENT, text );
	}
	notifyUpdateState = state;
}

// the bar at the left of a line: what kind of line it is
static const float *Notify_Accent( notifyKind_t kind ) {
	static const vec4_t tell = { 0.85f, 0.45f, 1.0f, 1.0f }, mention = { 1.0f, 0.8f, 0.25f, 1.0f };
	static const vec4_t event = { 0.43f, 0.83f, 1.0f, 1.0f };

	switch ( kind ) {
	case NOTIFY_TELL:		return tell;
	case NOTIFY_MENTION:	return mention;
	default:				return event;
	}
}

/*
=================
CG_NotifyDraw

At the left edge (CG_Place2D( PLACE_LEFT ) is set)
=================
*/
void CG_NotifyDraw( void ) {
	static const vec4_t	fill = { 0.05f, 0.08f, 0.12f, 0.6f }, rim = { 0.43f, 0.83f, 1.0f, 0.25f };
	const float			size = Com_Clamp( 0.5f, 2.0f, v_notifyScale.value );
	const float			lineH = 16.0f * size, pad = 4.0f * size, bar = 3.0f * size, scale = 0.6f * size;
	const int			life = Q_max( 1000, v_notifyTime.integer );
	float				y = v_notifyY.value;
	vec4_t				color, box, edge, accent;
	int					i, keep;

	Notify_CheckUpdate();
	if ( !v_notify.integer || !cg.snap ) {
		return;
	}

	// what is over goes, and a map change leaves nothing from before
	for ( i = keep = 0; i < notifyCount; i++ ) {
		if ( cg.time >= notify[i].time && cg.time - notify[i].time < life + NOTIFY_SLIDE ) {
			notify[keep++] = notify[i];
		}
	}
	notifyCount = keep;

	for ( i = 0; i < notifyCount; i++ ) {
		const notifyLine_t	*line = &notify[i];
		const int			age = cg.time - line->time;
		const float			w = CG_Text_Width( line->text, scale, FONT_SMALL ) + 2.0f * pad + bar;
		const float			textY = y + ( lineH - CG_Text_Height( "A", scale, FONT_SMALL ) ) * 0.5f;
		float				in, x;

		// 0: outside the screen, 1: in place
		if ( age < NOTIFY_SLIDE ) {
			in = (float)age / NOTIFY_SLIDE;
		} else if ( age > life ) {
			in = 1.0f - (float)( age - life ) / NOTIFY_SLIDE;
		} else {
			in = 1.0f;
		}
		in = Com_Clamp( 0.0f, 1.0f, in );
		in = 1.0f - ( 1.0f - in ) * ( 1.0f - in );
		x = v_notifyX.value - ( 1.0f - in ) * ( w + v_notifyX.value );

		Skin_Color( box, fill, in );
		if ( CG_HudNewLook() ) {
			Skin_Color( edge, rim, in );
			Skin_Panel( x, y, w, lineH, 3.0f, box, edge );
		} else {
			CG_FillRect( x, y, w, lineH, box );
		}
		Skin_Color( accent, Notify_Accent( line->kind ), in );
		CG_FillRect( x, y, bar, lineH, accent );

		VectorSet( color, 1.0f, 1.0f, 1.0f );
		color[3] = in;
		CG_Text_Paint( x + bar + pad, textY, scale, color, line->text, 0, 0, 0, FONT_SMALL );
		y += lineH + 2.0f;
	}
}

// a new level: nothing of the last one is still on its way
void CG_NotifyInit( void ) {
	char value[16];

	notifyCount = 0;
	notifyUpdatePoll = 0;
	trap->Cvar_VariableStringBuffer( "v_updateState", value, sizeof( value ) );
	notifyUpdateState = atoi( value );
}
