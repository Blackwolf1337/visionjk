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

// cg_mapvote.c -- VisionJK: the end of a level in the new HUD. Over the scoreboard who won; under it the vote
// for the next map that a server of this tree runs (game/g_mapvote.c, the configstring CS_VISION_MAPVOTE): a box
// for each map with its key, its votes and a bar, the next map of the rotation as the last one, and the time
// left. The keys 1 to 4 (the weapon keys, CG_Weapon_f) vote while it runs, as does vs_mapvote <n>. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define VOTE_MAX		4

typedef struct {
	int		count, endTime;
	char	map[VOTE_MAX][MAX_QPATH];
	char	name[VOTE_MAX][64];
	int		votes[VOTE_MAX];
} mapVoteState_t;

static int	myVote, myVoteEnd;		// what the player sent, for the vote that ends at myVoteEnd

static qboolean MapVote_Read( mapVoteState_t *out ) {
	const char	*info = CG_ConfigString( CS_VISION_MAPVOTE );
	int			i;

	memset( out, 0, sizeof( *out ) );
	if ( !info || !info[0] ) {
		return qfalse;
	}
	out->count = Q_min( VOTE_MAX, atoi( Info_ValueForKey( info, "n" ) ) );
	out->endTime = atoi( Info_ValueForKey( info, "t" ) );
	for ( i = 0; i < out->count; i++ ) {
		Q_strncpyz( out->map[i], Info_ValueForKey( info, va( "m%i", i ) ), sizeof( out->map[0] ) );
		Q_strncpyz( out->name[i], Info_ValueForKey( info, va( "l%i", i ) ), sizeof( out->name[0] ) );
		out->votes[i] = atoi( Info_ValueForKey( info, va( "v%i", i ) ) );
	}
	if ( myVoteEnd != out->endTime ) {
		myVote = 0;
		myVoteEnd = out->endTime;
	}
	return (qboolean)( out->count > 0 );
}

// a vote runs and the level is over
static qboolean MapVote_Open( mapVoteState_t *state ) {
	return (qboolean)( cg.snap && cg.snap->ps.pm_type == PM_INTERMISSION && MapVote_Read( state ) && cg.time < state->endTime );
}

/*
=================
CG_MapVoteKey

From CG_Weapon_f: the key of a choice while the vote runs. qtrue: it was a vote.
=================
*/
qboolean CG_MapVoteKey( int n ) {
	mapVoteState_t state;

	if ( !MapVote_Open( &state ) || n < 1 || n > state.count ) {
		return qfalse;
	}
	myVote = n;
	trap->SendClientCommand( va( "vs_mapvote %i", n ) );
	return qtrue;
}

// who won: the best of the scores, the better team, or that it is even
static void MapVote_Winner( char *out, int size ) {
	if ( cgs.gametype >= GT_TEAM ) {
		if ( cgs.scores1 == cgs.scores2 ) {
			Q_strncpyz( out, CG_VisionText( "Draw" ), size );
		} else {
			Q_strncpyz( out, CG_VisionTextWith( "%s wins", CG_GetStringEdString( "MP_INGAME", cgs.scores1 > cgs.scores2 ? "RED" : "BLUE" ) ), size );
		}
	} else {
		int i;

		// the best who played (the scores come sorted, the ones who watch among them)
		out[0] = '\0';
		for ( i = 0; i < cg.numScores; i++ ) {
			const int client = cg.scores[i].client;

			if ( client >= 0 && client < MAX_CLIENTS && cgs.clientinfo[client].team != TEAM_SPECTATOR ) {
				Q_strncpyz( out, CG_VisionTextWith( "%s wins", cgs.clientinfo[client].name ), size );
				break;
			}
		}
	}
}

/*
=================
CG_HudIntermission

At the end of a level, after the scoreboard: the winner over it, the vote under it.
=================
*/
void CG_HudIntermission( void ) {
	mapVoteState_t	state;
	static int		endTime;
	char			title[160], about[256];
	int				i, total = 0;

	// how long the level ran: from its start to the first frame of its end (a limit ended it)
	{
		static int endStart;

		if ( endStart != cgs.levelStartTime || !endTime || endTime > cg.time ) {
			endStart = cgs.levelStartTime;
			endTime = cg.time;
		}
	}
	MapVote_Winner( title, sizeof( title ) );
	Com_sprintf( about, sizeof( about ), "%s  |  %s  |  %s", CG_VisionText( "Match over" ), BG_GetGametypeString( cgs.gametype ),
		Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "mapname" ) );
	if ( endTime > cgs.levelStartTime ) {
		Q_strcat( about, sizeof( about ), "  |  " );
		Q_strcat( about, sizeof( about ), CG_VisionTextWith( "Limit reached after %s", Hud_Clock( endTime - cgs.levelStartTime ) ) );
	}
	Q_strupr( about );
	Hud_Caps( SCREEN_WIDTH * 0.5f, 22.0f, hudAccent, 1.0f, HUD_CENTER, about );
	if ( title[0] ) {
		Hud_TextMid( SCREEN_WIDTH * 0.5f, 42.0f, HUD_HUGE * 1.2f, hudBright, 1.0f, HUD_CENTER, title, 400.0f );
	}
	// how to write to the others, at the lower left where the chat is
	if ( !cg.demoPlayback ) {
		const char	*key = Hud_KeyFor( "messagemode", "T" ), *what = CG_VisionText( "Write a message" );
		const float	w = Hud_KeyHintWidth( key, what ) + 12.0f, h = 14.0f;

		CG_Place2D( PLACE_LEFT );
		Hud_Glass( HUD_EDGE, SCREEN_HEIGHT - HUD_EDGE - h, w, h, 3.5f, 1.0f, NULL, qtrue );
		Hud_KeyHint( HUD_EDGE + 6.0f, SCREEN_HEIGHT - HUD_EDGE - h * 0.5f, key, what, 1.0f );
		CG_Place2D( PLACE_CENTER );
	}
	// how to say one is ready for the next level, until one has: over the vote, or at the bottom edge
	if ( cg.snap && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR && !cg.demoPlayback
		&& !( cg.snap->ps.stats[STAT_CLIENTS_READY] & ( 1 << cg.snap->ps.clientNum ) ) ) {
		const qboolean	vote = MapVote_Open( &state );
		const char		*key = Hud_KeyFor( "+attack", "ATTACK" ), *what = CG_VisionText( "Ready" );
		const float		w = Hud_KeyHintWidth( key, what ) + 12.0f, h = 14.0f;
		const float		x = SCREEN_WIDTH * 0.5f - w * 0.5f, y = SCREEN_HEIGHT - HUD_EDGE - h - ( vote ? 58.0f + HUD_GAP : 0.0f );

		Hud_Glass( x, y, w, h, 3.5f, 1.0f, NULL, qtrue );
		Hud_KeyHint( x + 6.0f, y + h * 0.5f, key, what, 1.0f );
	}
	if ( !MapVote_Open( &state ) ) {
		return;
	}
	{
		const float	w = 470.0f, h = 58.0f, x = SCREEN_WIDTH * 0.5f - w * 0.5f, y = SCREEN_HEIGHT - HUD_EDGE - h;
		const float	gap = 4.0f, bw = ( w - 16.0f - ( state.count - 1 ) * gap ) / state.count;

		for ( i = 0; i < state.count; i++ ) {
			total += state.votes[i];
		}
		Hud_Glass( x, y, w, h, 7.0f, 1.0f, NULL, qfalse );
		Hud_Caps( x + 8.0f, y + 8.0f, hudAccent, 1.0f, HUD_LEFT, CG_VisionText( "Next map" ) );
		Hud_Caps( x + w - 8.0f, y + 8.0f, hudDim, 1.0f, HUD_RIGHT, va( "%i s", ( state.endTime - cg.time ) / 1000 + 1 ) );
		for ( i = 0; i < state.count; i++ ) {
			const float		bx = x + 8.0f + i * ( bw + gap ), by = y + 15.0f, bh = 36.0f;
			const qboolean	mine = (qboolean)( myVote == i + 1 );
			const vec4_t	fill = { 0.0f, 0.0f, 0.0f, 0.3f }, mineFill = { 0.43f, 0.83f, 1.0f, 0.14f };
			vec4_t			edge;
			char			label[64];

			Hud_Color( edge, hudAccent, mine ? 1.0f : 0.28f );
			Skin_Panel( bx, by, bw, bh, 4.0f, mine ? mineFill : fill, edge );
			if ( state.map[i][0] ) {
				Q_strncpyz( label, state.map[i], sizeof( label ) );
				Q_strupr( label );
			} else {
				Q_strncpyz( label, CG_VisionText( "Next in rotation" ), sizeof( label ) );
			}
			Hud_TextMid( bx + 5.0f, by + 7.0f, HUD_SMALL, mine ? hudBright : hudText, 1.0f, HUD_LEFT, label, bw - 22.0f );
			Hud_TextMid( bx + bw - 5.0f, by + 7.0f, HUD_BODY, hudBright, 1.0f, HUD_RIGHT, va( "%i", state.votes[i] ), 0.0f );
			if ( state.name[i][0] ) {
				Hud_TextMid( bx + 5.0f, by + 16.0f, HUD_CAPS, hudDim, 1.0f, HUD_LEFT, state.name[i], bw - 10.0f );
			}
			Hud_Bar( bx + 5.0f, by + 23.0f, bw - 10.0f, 2.5f, total > 0 ? (float)state.votes[i] / total : 0.0f,
				mine ? hudAccent : hudDim, 1.0f, qfalse );
			if ( mine ) {
				Hud_Caps( bx + 5.0f, by + 31.0f, hudAccent, 1.0f, HUD_LEFT, CG_VisionText( "Your vote" ) );
			} else {
				Hud_Key( bx + 5.0f, by + 31.0f, va( "%i", i + 1 ), 1.0f, HUD_LEFT );
			}
		}
	}
}
