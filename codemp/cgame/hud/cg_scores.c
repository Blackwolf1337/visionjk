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

// cg_scores.c -- the scoreboard in the new look, and its rows as something to click. Not from JA+: the scoreboard
// of the game with what JA+ added to it is cg_scoreboard.c, and it is the one that is drawn with cg_hud 0, in
// another engine and next to the HUD of a mod.
//
// One panel in the middle of the screen: the server and the game at its head, then a row for every player with
// the picture of his model, his score, ping and time, by team in a team game, and who watches at the end. The
// own row is marked. Before the name of a player with a client of this tree stands the icon of the game
// (status/cg_peers.c), and what stands above his head is written after his name. Everybody the server knows is
// listed, also who is not in the scores it sends (it names so many at most).
//
// The rows can be clicked while the mouse is free (select/cg_select.c): the card of that player opens beside
// the board. The key of the scoreboard is held while it shows and the mouse turns the view, so the board can be
// kept: the key pressed twice quickly, or the command scores_pick, leaves it standing with the mouse free until
// the key, Escape or the right button lets go of it. cg_scoresPick 0: no double tap.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "game/bg_saga.h"

#define SCORES_W			470.0f
#define SCORES_CARD_GAP		6.0f		// between the board and the card of a player that was clicked on it,
#define SCORES_CARD_W		154.0f		//	and the room that card needs, with its distance to the edge of the screen
#define SCORES_TOP			64.0f		// of the panel
#define SCORES_BOTTOM		414.0f		// it ends above this: the bars of the HUD and the lines of the chat are below
#define SCORES_HEAD_H		36.0f		// the server and the game
#define SCORES_COLS_H		15.0f		// the names of the columns
#define SCORES_ROW_MAX		17.0f		// a row of a player, with few of them
#define SCORES_ROW_MIN		9.5f		//	and with many
#define SCORES_TEAM_H		17.0f		// the line of a team
#define SCORES_WATCH_H		14.0f		// the line above who watches
#define SCORES_PAD			6.0f
#define SCORES_TAP_MSEC		350			// the key twice within this keeps the board
#define SCORES_ASK_MSEC		2000		// while it is kept the scores are asked for this often
#define SCORES_LINE			18.0f		// a line of text of the new look is this high at a scale of 1

// the columns, from the left edge of the panel: where a number ends
#define SCORES_COL_SCORE	( SCORES_W - 150.0f )
#define SCORES_COL_PING		( SCORES_W - 82.0f )
#define SCORES_COL_TIME		( SCORES_W - 18.0f )

typedef enum {
	LINE_PLAYER,
	LINE_TEAM,
	LINE_WATCH
} lineKind_t;

typedef struct scoreLine_s {
	lineKind_t		kind;
	int				client;				// LINE_PLAYER
	const score_t	*score;				//	what the server said of him, NULL: nothing
	team_t			team;				// LINE_TEAM
} scoreLine_t;

static struct {
	qboolean	kept;					// it stays open with the mouse free
	int			tapTime;				// when its key was last pressed, by the clock of the system
	// Its key is held. While a key is held the system sends it again and again (key repeat), as +scores or,
	// while the cgame has the keys, as the key: none of that is a new press.
	qboolean	keyDown;
	qboolean	keyDone;				//	and this press let go of the kept board: it shows nothing until the key is up
	qboolean	shown;					// it was drawn this frame,
	float		x, y, w, h;				//	the panel there,
	struct {
		int		client;
		float	y, h;
	} rows[MAX_CLIENTS];				//	and its rows, for the pointer
	int			numRows;
} scores;

static const vec4_t scoresFill = { 0.035f, 0.07f, 0.12f, 0.86f }, scoresEdge = { 0.43f, 0.83f, 1.0f, 0.35f };
static const vec4_t scoresText = { 0.86f, 0.92f, 0.97f, 1.0f }, scoresDim = { 0.60f, 0.69f, 0.78f, 1.0f };
static const vec4_t scoresAccent = { 0.43f, 0.83f, 1.0f, 1.0f };

extern int cg_siegeWinTeam;		// cg_scoreboard.c

void CG_ScoresInit( void ) {
	memset( &scores, 0, sizeof( scores ) );
}

/*
===============================================================================

Text

===============================================================================
*/

static float Scores_Width( const char *text, float scale ) {
	return (float)CG_Text_Width( text, scale, SKIN_FONT_SANS );
}

static float Scores_FitWidth( const char *text, void *arg ) {
	return Scores_Width( text, *(const float *)arg );
}

// a line of text in the middle of a row that begins at y and is h high. align: 0 from x on, 1 ending at x, 2 with
// its middle at x
static void Scores_Text( float x, float y, float h, float scale, const vec4_t color, float fade, int align, const char *text ) {
	vec4_t c;

	Skin_Color( c, color, fade );
	if ( align ) {
		x -= Scores_Width( text, scale ) * ( align == 2 ? 0.5f : 1.0f );
	}
	CG_Text_Paint( x, y + ( h - SCORES_LINE * scale ) * 0.5f, scale, c, text, 0, 0, 0, SKIN_FONT_SANS );
}

// The same for a text that has to end within `most` units: it is set smaller as far as it stays readable, and
// what is still too long ends in three dots. Returns how wide it was drawn.
static float Scores_TextFit( float x, float y, float h, float scale, const vec4_t color, float fade, const char *text, float most ) {
	const float	width = Scores_Width( text, scale );
	char		line[160];

	if ( most <= 0.0f ) {
		return 0.0f;
	}
	// (a little smaller than it has to be: set to fit exactly, it is too wide again by what a float is off)
	if ( width > most ) {
		scale = Q_max( scale * 0.82f, scale * most / width * 0.98f );
	}
	CG_TextCut( text, most, Scores_FitWidth, &scale, line, sizeof( line ) );
	Scores_Text( x, y, h, scale, color, fade, 0, line );
	return Scores_Width( line, scale );
}

// the name of the key of the scoreboard, as a hint says it
static const char *Scores_KeyName( void ) {
	const char *name = CG_KeyLabel( trap->Key_GetKey( "+scores" ) );

	return name ? name : CG_VisionText( "The scores key" );
}

/*
===============================================================================

The lines

===============================================================================
*/

// the players of a team: in the order of the scores, then whom the server left out of them
static void Scores_AddTeam( scoreLine_t *lines, int *count, team_t team ) {
	qboolean	listed[MAX_CLIENTS];
	int			i;

	memset( listed, 0, sizeof( listed ) );
	for ( i = -cg.numScores; i < MAX_CLIENTS; i++ ) {
		const score_t	*score = ( i < 0 ) ? &cg.scores[i + cg.numScores] : NULL;
		const int		n = score ? score->client : i;

		if ( n < 0 || n >= MAX_CLIENTS || listed[n] || !cgs.clientinfo[n].infoValid || cgs.clientinfo[n].team != team ) {
			continue;
		}
		listed[n] = qtrue;
		lines[*count].kind = LINE_PLAYER;
		lines[*count].client = n;
		lines[*count].score = score;
		( *count )++;
	}
}

static int Scores_Lines( scoreLine_t *lines ) {
	int count = 0, before;

	if ( cgs.gametype >= GT_TEAM ) {
		// the team that leads first
		const team_t	first = ( cg.teamScores[1] > cg.teamScores[0] ) ? TEAM_BLUE : TEAM_RED;
		const team_t	second = ( first == TEAM_RED ) ? TEAM_BLUE : TEAM_RED;

		lines[count].kind = LINE_TEAM;
		lines[count++].team = first;
		Scores_AddTeam( lines, &count, first );
		lines[count].kind = LINE_TEAM;
		lines[count++].team = second;
		Scores_AddTeam( lines, &count, second );
	} else {
		Scores_AddTeam( lines, &count, TEAM_FREE );
	}
	before = count;
	lines[count++].kind = LINE_WATCH;
	Scores_AddTeam( lines, &count, TEAM_SPECTATOR );
	if ( count == before + 1 ) {
		count = before;		// nobody watches
	}
	return count;
}

static float Scores_LineHeight( const scoreLine_t *line, float rowH ) {
	return ( line->kind == LINE_TEAM ) ? SCORES_TEAM_H : ( line->kind == LINE_WATCH ) ? SCORES_WATCH_H : rowH;
}

/*
===============================================================================

Drawing

===============================================================================
*/

// who leads, or where the own player stands: the line of the scoreboard of the game
static void Scores_Standing( char *out, int outSize ) {
	out[0] = '\0';
	if ( cgs.gametype == GT_POWERDUEL ) {
		return;
	}
	if ( cgs.gametype < GT_TEAM ) {
		char place[128], of[64], with[64];

		if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
			return;
		}
		trap->SE_GetStringTextString( "MP_INGAME_PLACE", place, sizeof( place ) );
		trap->SE_GetStringTextString( "MP_INGAME_OF", of, sizeof( of ) );
		trap->SE_GetStringTextString( "MP_INGAME_WITH", with, sizeof( with ) );
		Com_sprintf( out, outSize, "%s %s (%s %i) %s %i", CG_PlaceString( cg.snap->ps.persistant[PERS_RANK] + 1 ), place, of,
			cg.numScores, with, cg.snap->ps.persistant[PERS_SCORE] );
		Q_StripColor( out );
	} else if ( cgs.gametype != GT_SIEGE ) {
		if ( cg.teamScores[0] == cg.teamScores[1] ) {
			Com_sprintf( out, outSize, "%s %i", CG_GetStringEdString( "MP_INGAME", "TIEDAT" ), cg.teamScores[0] );
		} else if ( cg.teamScores[0] >= cg.teamScores[1] ) {
			Com_sprintf( out, outSize, "%s, %i / %i", CG_GetStringEdString( "MP_INGAME", "RED_LEADS" ), cg.teamScores[0], cg.teamScores[1] );
		} else {
			Com_sprintf( out, outSize, "%s, %i / %i", CG_GetStringEdString( "MP_INGAME", "BLUE_LEADS" ), cg.teamScores[1], cg.teamScores[0] );
		}
	} else if ( cg_siegeWinTeam == 1 || cg_siegeWinTeam == 2 ) {
		Q_strncpyz( out, CG_GetStringEdString( "MP_INGAME", cg_siegeWinTeam == 1 ? "SIEGETEAM1WIN" : "SIEGETEAM2WIN" ), outSize );
	}
}

// what stands above the board: who won the duel, who fights it, or who killed the own player
static void Scores_Above( char *out, int outSize ) {
	const qboolean duel = (qboolean)( cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL );
	const qboolean over = (qboolean)( cg.predictedPlayerState.pm_type == PM_INTERMISSION );

	out[0] = '\0';
	if ( duel && over && cgs.duelWinner != -1 ) {
		Com_sprintf( out, outSize, "%s^7 %s", cgs.clientinfo[cgs.duelWinner].name, CG_GetStringEdString( "MP_INGAME", "DUEL_WINS" ) );
	} else if ( duel && over && cgs.duelist1 != -1 && cgs.duelist2 != -1 ) {
		if ( cgs.gametype == GT_POWERDUEL && cgs.duelist3 != -1 ) {
			Com_sprintf( out, outSize, "%s^7 %s %s^7 %s %s", cgs.clientinfo[cgs.duelist1].name, CG_GetStringEdString( "MP_INGAME", "SPECHUD_VERSUS" ),
				cgs.clientinfo[cgs.duelist2].name, CG_GetStringEdString( "MP_INGAME", "AND" ), cgs.clientinfo[cgs.duelist3].name );
		} else {
			Com_sprintf( out, outSize, "%s^7 %s %s", cgs.clientinfo[cgs.duelist1].name, CG_GetStringEdString( "MP_INGAME", "SPECHUD_VERSUS" ),
				cgs.clientinfo[cgs.duelist2].name );
		}
	} else if ( cg.killerName[0] ) {
		Com_sprintf( out, outSize, "%s %s", CG_GetStringEdString( "MP_INGAME", "KILLEDBY" ), cg.killerName );
	}
}

// a line of text on a dark pill, its middle at x
static void Scores_Pill( float x, float y, float scale, const vec4_t color, float fade, const char *text ) {
	const vec4_t	back = { 0.02f, 0.04f, 0.08f, 0.62f };
	const float		w = Scores_Width( text, scale ) + 16.0f, h = SCORES_LINE * scale + 5.0f;
	vec4_t			c;

	Skin_Color( c, back, fade );
	Skin_Panel( x - w * 0.5f, y, w, h, h * 0.5f, c, NULL );
	Scores_Text( x, y, h, scale, color, fade, 2, text );
}

// the head of the panel: the server, the game and the level, and at the right who leads and how the mouse gets
// onto the board, or how it lets go of it
static void Scores_Head( float x, float y, float fade ) {
	const char	*info = CG_ConfigString( CS_SERVERINFO );
	const char	*hint = "";
	char		host[128], about[128], standing[256];
	float		room = SCORES_W - 24.0f, right = 0.0f;
	vec4_t		line;

	if ( cg_selectPlayers.integer && !cg.demoPlayback ) {
		if ( scores.kept ) {
			hint = CG_VisionTextWith( "Click a player. %s or Esc closes.", Scores_KeyName() );
		} else if ( cg_scoresPick.integer ) {
			hint = CG_VisionTextWith( "%s twice: pick a player", Scores_KeyName() );
		}
	}
	Scores_Standing( standing, sizeof( standing ) );
	if ( standing[0] ) {
		const float scale = 0.54f;
		const float w = Q_min( Scores_Width( standing, scale ), SCORES_W * 0.5f );

		// (alone it stands in the middle of the head, with the hint under it in its upper half)
		Scores_TextFit( x + SCORES_W - 12.0f - w, y + ( hint[0] ? 5.0f : 0.0f ), hint[0] ? 14.0f : SCORES_HEAD_H - 2.0f, scale, scoresText, fade, standing, SCORES_W * 0.5f );
		right = w;
	}
	if ( hint[0] ) {
		const float scale = 0.44f;
		const float w = Q_min( Scores_Width( hint, scale ), SCORES_W * 0.5f );

		Scores_TextFit( x + SCORES_W - 12.0f - w, y + ( standing[0] ? 19.0f : 0.0f ), standing[0] ? 11.0f : SCORES_HEAD_H - 2.0f, scale,
			scores.kept ? scoresAccent : scoresDim, fade, hint, SCORES_W * 0.5f );
		right = Q_max( right, w );
	}
	if ( right > 0.0f ) {
		room -= right + 14.0f;
	}
	Q_strncpyz( host, Info_ValueForKey( info, "sv_hostname" ), sizeof( host ) );
	Q_StripColor( host );
	Scores_TextFit( x + 12.0f, y + 5.0f, 14.0f, 0.7f, colorWhite, fade, host, room );
	Com_sprintf( about, sizeof( about ), "%s  |  %s", BG_GetGametypeString( cgs.gametype ), Info_ValueForKey( info, "mapname" ) );
	Q_strupr( about );
	Scores_TextFit( x + 12.0f, y + 19.0f, 11.0f, 0.46f, scoresDim, fade, about, room );

	Skin_Color( line, scoresEdge, fade );
	Skin_Line( x + 8.0f, y + SCORES_HEAD_H - 1.0f, SCORES_W - 16.0f, line );
}

// the names of the columns
static void Scores_Columns( float x, float y, float nameX, float fade ) {
	const qboolean	duel = (qboolean)( cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL );
	const float		scale = 0.44f;
	char			word[64];

	Q_strncpyz( word, CG_GetStringEdString( "MP_INGAME", "NAME" ), sizeof( word ) );
	Scores_Text( nameX, y, SCORES_COLS_H, scale, scoresDim, fade, 0, Q_strupr( word ) );
	if ( duel ) {
		trap->SE_GetStringTextString( "MP_INGAME_W_L", word, sizeof( word ) );
	} else {
		Q_strncpyz( word, CG_GetStringEdString( "MP_INGAME", "SCORE" ), sizeof( word ) );
	}
	Scores_Text( x + SCORES_COL_SCORE, y, SCORES_COLS_H, scale, scoresDim, fade, 1, Q_strupr( word ) );
	Q_strncpyz( word, CG_GetStringEdString( "MP_INGAME", "PING" ), sizeof( word ) );
	Scores_Text( x + SCORES_COL_PING, y, SCORES_COLS_H, scale, scoresDim, fade, 1, Q_strupr( word ) );
	Q_strncpyz( word, CG_GetStringEdString( "MP_INGAME", "TIME" ), sizeof( word ) );
	Scores_Text( x + SCORES_COL_TIME, y, SCORES_COLS_H, scale, scoresDim, fade, 1, Q_strupr( word ) );
}

// the line of a team: its name and what it has
static void Scores_TeamLine( float x, float y, team_t team, float fade ) {
	const vec4_t	red = { 0.86f, 0.22f, 0.18f, 0.24f }, blue = { 0.2f, 0.42f, 0.95f, 0.24f };
	char			word[64];
	vec4_t			c;

	Skin_Color( c, team == TEAM_RED ? red : blue, fade );
	Skin_Panel( x + 4.0f, y + 2.0f, SCORES_W - 8.0f, SCORES_TEAM_H - 3.0f, 3.0f, c, NULL );
	Q_strncpyz( word, CG_GetStringEdString( "MP_INGAME", team == TEAM_RED ? "RED" : "BLUE" ), sizeof( word ) );
	Scores_Text( x + 12.0f, y + 1.0f, SCORES_TEAM_H - 1.0f, 0.5f, colorWhite, fade, 0, Q_strupr( word ) );
	if ( cgs.gametype != GT_SIEGE ) {
		Scores_Text( x + SCORES_COL_SCORE, y + 1.0f, SCORES_TEAM_H - 1.0f, 0.6f, colorWhite, fade, 1, va( "%i", cg.teamScores[team == TEAM_RED ? 0 : 1] ) );
	}
}

/*
=================
Scores_PlayerLine

The row of one player: the picture of his model (or what he carries), the icon of the game if he plays with a
client of this tree, his name with what stands above his head after it, and his numbers.
=================
*/
static void Scores_PlayerLine( float x, float y, float h, const scoreLine_t *line, int index, qboolean pointed, float fade ) {
	const vec4_t		own = { 0.43f, 0.83f, 1.0f, 0.17f }, lit = { 0.43f, 0.83f, 1.0f, 0.12f }, litEdge = { 0.43f, 0.83f, 1.0f, 0.45f };
	const vec4_t		stripe = { 1.0f, 1.0f, 1.0f, 0.025f };
	const int			n = line->client;
	const clientInfo_t	*ci = &cgs.clientinfo[n];
	const score_t		*score = line->score;
	const qboolean		duel = (qboolean)( cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL );
	const float			scale = Com_Clamp( 0.42f, 0.62f, h * 0.62f / 16.0f ), small = scale * 0.8f;
	const float			icon = h - 3.0f, iconY = y + 1.5f;
	const float			nameEnd = x + SCORES_COL_SCORE - 44.0f;
	float				at = x + 10.0f, drawn;
	char				text[128];
	vec4_t				c;

	// the own row, and every other row a little lighter
	if ( n == cg.snap->ps.clientNum ) {
		Skin_Color( c, own, fade );
		Skin_Panel( x + 4.0f, y, SCORES_W - 8.0f, h - 1.0f, 3.0f, c, NULL );
		Skin_Color( c, scoresAccent, fade );
		Skin_Panel( x + 4.0f, y + 2.0f, 1.5f, h - 5.0f, 0.75f, c, NULL );
	} else if ( index & 1 ) {
		Skin_Color( c, stripe, fade );
		Skin_Panel( x + 4.0f, y, SCORES_W - 8.0f, h - 1.0f, 3.0f, c, NULL );
	}
	// the row under the pointer, while the mouse is free: lit, with a rim
	if ( pointed ) {
		vec4_t rim;

		Skin_Color( c, lit, fade );
		Skin_Color( rim, litEdge, fade );
		Skin_Panel( x + 4.0f, y, SCORES_W - 8.0f, h - 1.0f, 3.0f, c, rim );
	}

	// a flag he carries, his side of a power duel, his class in a siege, or the picture of his model
	if ( ci->powerups & ( ( 1 << PW_NEUTRALFLAG ) | ( 1 << PW_REDFLAG ) | ( 1 << PW_BLUEFLAG ) ) ) {
		CG_DrawFlagModel( at, iconY, icon, icon, ( ci->powerups & ( 1 << PW_REDFLAG ) ) ? TEAM_RED
			: ( ci->powerups & ( 1 << PW_BLUEFLAG ) ) ? TEAM_BLUE : TEAM_FREE, qfalse );
	} else {
		qhandle_t picture = ci->modelIcon;

		if ( cgs.gametype == GT_POWERDUEL && ( ci->duelTeam == DUELTEAM_LONE || ci->duelTeam == DUELTEAM_DOUBLE ) ) {
			picture = trap->R_RegisterShaderNoMip( ci->duelTeam == DUELTEAM_LONE ? "gfx/mp/pduel_icon_lone" : "gfx/mp/pduel_icon_double" );
		} else if ( cgs.gametype == GT_SIEGE && ci->siegeIndex != -1 && bgSiegeClasses[ci->siegeIndex].classShader ) {
			picture = bgSiegeClasses[ci->siegeIndex].classShader;
		}
		if ( !picture ) {
			picture = Skin_NoPicture();
		}
		if ( picture ) {
			const vec4_t white = { 1.0f, 1.0f, 1.0f, fade };

			trap->R_SetColor( white );
			CG_DrawPic( at, iconY, icon, icon, picture );
			trap->R_SetColor( NULL );
		}
	}
	at += icon + 5.0f;
	if ( CG_PeerVision( n, NULL ) == 1 ) {
		const float mark = icon * 0.82f;

		Skin_Logo( at, y + ( h - mark ) * 0.5f - 0.5f, mark, mark, fade );
		at += mark + 4.0f;
	}

	drawn = Scores_TextFit( at, y, h - 1.0f, scale, colorWhite, fade, ci->name, nameEnd - at );
	at += drawn + 8.0f;

	// after the name: that he is ready for the next level, that he is on his way in, or what stands above his head
	text[0] = '\0';
	if ( cg.snap->ps.stats[STAT_CLIENTS_READY] & ( 1 << n ) ) {
		Q_strncpyz( text, CG_GetStringEdString( "MP_INGAME", "READY" ), sizeof( text ) );
		Scores_TextFit( at, y, h - 1.0f, small, scoresAccent, fade, text, nameEnd - at );
	} else if ( score && score->ping == -1 ) {
		Scores_TextFit( at, y, h - 1.0f, small, scoresDim, fade, CG_VisionText( "connecting" ), nameEnd - at );
	} else if ( nameEnd - at > 40.0f && CG_StatusLine( n, text, sizeof( text ) ) ) {
		Scores_TextFit( at, y, h - 1.0f, small, scoresDim, fade, text, nameEnd - at );
	}

	if ( !score || score->ping == -1 ) {
		Scores_Text( x + SCORES_COL_SCORE, y, h - 1.0f, scale, scoresDim, fade, 1, "-" );
		Scores_Text( x + SCORES_COL_PING, y, h - 1.0f, scale, scoresDim, fade, 1, "-" );
		Scores_Text( x + SCORES_COL_TIME, y, h - 1.0f, scale, scoresDim, fade, 1, "-" );
		return;
	}
	if ( duel ) {
		Scores_Text( x + SCORES_COL_SCORE, y, h - 1.0f, scale, scoresText, fade, 1, va( "%i/%i", ci->wins, ci->losses ) );
	} else if ( ci->team != TEAM_SPECTATOR ) {
		// score/deaths on JA+ servers from 2.1 on
		Scores_Text( x + SCORES_COL_SCORE, y, h - 1.0f, scale, scoresText, fade, 1,
			( cgs.jpVersion >= 2.1f ) ? va( "%i/%i", score->score, score->deaths ) : va( "%i", score->score ) );
	}
	if ( cg_scoreboardBots.integer && ci->botSkill != -1 ) {
		Scores_Text( x + SCORES_COL_PING, y, h - 1.0f, small, scoresDim, fade, 1, "BOT" );
	} else {
		Scores_Text( x + SCORES_COL_PING, y, h - 1.0f, scale, scoresText, fade, 1, va( "%i", score->ping ) );
	}
	Scores_Text( x + SCORES_COL_TIME, y, h - 1.0f, scale, scoresText, fade, 1, va( "%i", score->time ) );
}

/*
=================
CG_ScoresDraw

The scoreboard, when the one of the game would be drawn: while its key is held or it is kept, while the own
player is dead, at the end of a level, and fading out afterwards. qtrue: it was drawn.
=================
*/
qboolean CG_ScoresDraw( void ) {
	const vec4_t	shade = { 0.0f, 0.0f, 0.0f, 0.42f };
	const float		avail = SCORES_BOTTOM - SCORES_TOP - SCORES_HEAD_H - SCORES_COLS_H - SCORES_PAD;
	float			x = ( SCREEN_WIDTH - SCORES_W ) * 0.5f;
	scoreLine_t		lines[MAX_CLIENTS + 3];
	char			text[256];
	float			fade, rowH, fixed = 0.0f, content = 0.0f, y, height, nameX;
	int				numLines, players = 0, shown, hidden = 0, i, index = 0;
	qboolean		pointer;
	vec4_t			c;

	scores.shown = qfalse;
	scores.numRows = 0;

	// don't draw anything if the menu or console is up
	if ( cl_paused.integer || !cg.snap ) {
		cg.deferredPlayerLoading = 0;
		return qfalse;
	}
	if ( scores.kept ) {
		if ( cg.demoPlayback || !cg_selectPlayers.integer ) {
			CG_ScoresLetGo();
		} else {
			cg.showScores = qtrue;
			if ( cg.scoresRequestTime + SCORES_ASK_MSEC < cg.time || cg.scoresRequestTime > cg.time ) {
				cg.scoresRequestTime = cg.time;
				trap->SendClientCommand( "score" );
			}
		}
	}
	// don't draw scoreboard during death while warmup up
	if ( cg.warmup && !cg.showScores ) {
		return qfalse;
	}
	if ( cg.showScores || cg.predictedPlayerState.pm_type == PM_DEAD || cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		fade = 1.0f;
	} else {
		const float *color = CG_FadeColor( cg.scoreFadeTime, FADE_TIME );

		if ( !color ) {
			// next time scoreboard comes up, don't print killer
			cg.deferredPlayerLoading = 0;
			cg.killerName[0] = 0;
			return qfalse;
		}
		fade = color[3];
	}

	// How high a row is: as high as there is room, within its limits. What still does not fit is left out, with
	// a line that says how many.
	numLines = Scores_Lines( lines );
	for ( i = 0; i < numLines; i++ ) {
		if ( lines[i].kind == LINE_PLAYER ) {
			players++;
		} else {
			fixed += Scores_LineHeight( &lines[i], 0.0f );
		}
	}
	rowH = players ? Com_Clamp( SCORES_ROW_MIN, SCORES_ROW_MAX, ( avail - fixed ) / players ) : SCORES_ROW_MAX;
	shown = numLines;
	if ( fixed + players * rowH > avail ) {
		for ( shown = 0; shown < numLines && content + Scores_LineHeight( &lines[shown], rowH ) + rowH <= avail; shown++ ) {
			content += Scores_LineHeight( &lines[shown], rowH );
		}
		for ( i = shown; i < numLines; i++ ) {
			hidden += ( lines[i].kind == LINE_PLAYER );
		}
		content += rowH;
	} else {
		content = fixed + players * rowH;
	}
	height = SCORES_HEAD_H + SCORES_COLS_H + content + SCORES_PAD;

	// While it is kept for the pointer the card of a player opens at its right. Where the screen has no room
	// for that (a window of 4:3), the board stands as far to the left as the card needs, from the moment it is
	// kept: its rows do not move under the pointer when one is clicked.
	if ( scores.kept ) {
		x = Q_max( 4.0f - cgs.bias2D, Q_min( x, SCREEN_WIDTH + cgs.bias2D - SCORES_CARD_W - SCORES_CARD_GAP - SCORES_W ) );
	}

	CG_Place2D( PLACE_CENTER );
	Scores_Above( text, sizeof( text ) );
	if ( text[0] ) {
		Scores_Pill( x + SCORES_W * 0.5f, SCORES_TOP - 26.0f, 0.66f, colorWhite, fade, text );
	}
	Skin_Color( c, shade, fade );
	Skin_Soft( x, SCORES_TOP + 5.0f, SCORES_W, height, 16.0f, c );
	Skin_Color( c, scoresFill, fade );
	{
		vec4_t edge;

		Skin_Color( edge, scoresEdge, fade );
		Skin_Panel( x, SCORES_TOP, SCORES_W, height, 8.0f, c, edge );
	}
	Scores_Head( x, SCORES_TOP, fade );
	// (the names begin after the picture of a row)
	nameX = x + 10.0f + ( rowH - 3.0f ) + 5.0f;
	Scores_Columns( x, SCORES_TOP + SCORES_HEAD_H, nameX, fade );

	// is there a pointer on the screen: the board is kept, or the mouse is free for another reason
	pointer = (qboolean)( ( trap->Key_GetCatcher() & KEYCATCH_CGAME ) != 0 && cg_selectPlayers.integer );
	y = SCORES_TOP + SCORES_HEAD_H + SCORES_COLS_H;
	for ( i = 0; i < shown; i++ ) {
		const float h = Scores_LineHeight( &lines[i], rowH );

		if ( lines[i].kind == LINE_TEAM ) {
			Scores_TeamLine( x, y, lines[i].team, fade );
			index = 0;
		} else if ( lines[i].kind == LINE_WATCH ) {
			Q_strncpyz( text, CG_VisionText( "Spectators" ), sizeof( text ) );
			Scores_Text( x + 12.0f, y + 2.0f, SCORES_WATCH_H - 2.0f, 0.44f, scoresDim, fade, 0, Q_strupr( text ) );
			Skin_Color( c, scoresEdge, fade * 0.6f );
			Skin_Line( x + 8.0f, y + 1.0f, SCORES_W - 16.0f, c );
			index = 0;
		} else {
			Scores_PlayerLine( x, y, h, &lines[i], index++, (qboolean)( pointer && cgs.cursorX >= x && cgs.cursorX < x + SCORES_W
				&& cgs.cursorY >= y && cgs.cursorY < y + h ), fade );
			scores.rows[scores.numRows].client = lines[i].client;
			scores.rows[scores.numRows].y = y;
			scores.rows[scores.numRows].h = h;
			scores.numRows++;
		}
		y += h;
	}
	if ( hidden ) {
		Scores_Text( x + 12.0f, y, rowH, 0.46f, scoresDim, fade, 0, CG_VisionTextWith( "and %i more", va( "%i", hidden ) ) );
	}

	trap->R_SetColor( NULL );

	scores.x = x;
	scores.y = SCORES_TOP;
	scores.w = SCORES_W;
	scores.h = height;
	scores.shown = qtrue;

	// load any models that have been deferred
	if ( ++cg.deferredPlayerLoading > 10 ) {
		CG_LoadDeferredPlayers();
	}
	return qtrue;
}

/*
===============================================================================

The pointer

===============================================================================
*/

// is the board kept open for the pointer: the mouse is free then
qboolean CG_ScoresPicking( void ) {
	return (qboolean)( scores.kept && cg_selectPlayers.integer && cg.snap && !cg.demoPlayback );
}

qboolean CG_ScoresKept( void ) {
	return scores.kept;
}

// was it drawn this frame
qboolean CG_ScoresShown( void ) {
	return scores.shown;
}

// is the pointer on the board
qboolean CG_ScoresPointerOver( void ) {
	return (qboolean)( scores.shown && cgs.cursorX >= scores.x && cgs.cursorX < scores.x + scores.w
		&& cgs.cursorY >= scores.y && cgs.cursorY < scores.y + scores.h );
}

/*
=================
CG_ScoresClientAt

The player whose row the pointer is on, -1: none. cardX and cardY: where the card of that player stands, beside
the board at the height of his row.
=================
*/
int CG_ScoresClientAt( float *cardX, float *cardY ) {
	int i;

	if ( !CG_ScoresPointerOver() ) {
		return -1;
	}
	for ( i = 0; i < scores.numRows; i++ ) {
		if ( cgs.cursorY >= scores.rows[i].y && cgs.cursorY < scores.rows[i].y + scores.rows[i].h ) {
			if ( cardX && cardY ) {
				*cardX = scores.x + scores.w + SCORES_CARD_GAP;
				*cardY = scores.rows[i].y - 4.0f;
			}
			return scores.rows[i].client;
		}
	}
	return -1;
}

// the board is no longer kept
void CG_ScoresLetGo( void ) {
	if ( scores.kept ) {
		scores.kept = qfalse;
		cg.showScores = qfalse;
		cg.scoreFadeTime = cg.time;
	}
}

static qboolean Scores_Keep( void ) {
	if ( !CG_HudNewLook() || !cg_selectPlayers.integer || !cg.snap || cg.demoPlayback ) {
		return qfalse;
	}
	scores.kept = qtrue;
	cg.showScores = qtrue;
	return qtrue;
}

/*
=================
CG_ScoresPress

+scores: the key of the scoreboard is down. A press of it, twice quickly, keeps the board. What a held key sends
after its press is no press: it used to count as one, so a key that was held was a double tap, and from then on
kept the board and let go of it by turns for as long as it was held. qfalse: nothing is to be shown for this,
the press let go of the kept board.
=================
*/
qboolean CG_ScoresPress( void ) {
	const int	now = trap->Milliseconds();
	qboolean	twice;

	if ( scores.keyDown ) {
		return (qboolean)!scores.keyDone;
	}
	scores.keyDown = qtrue;
	scores.keyDone = qfalse;
	twice = (qboolean)( scores.tapTime && now >= scores.tapTime && now - scores.tapTime < SCORES_TAP_MSEC );
	scores.tapTime = twice ? 0 : now;
	if ( twice && cg_scoresPick.integer ) {
		Scores_Keep();
	}
	return qtrue;
}

// -scores: the key is up. The engine says so also while the cgame has the keys.
void CG_ScoresRelease( void ) {
	scores.keyDown = scores.keyDone = qfalse;
}

/*
=================
CG_ScoresKeyEvent

A key while the board is kept and no card is open. qtrue: it was one of the board's (its key, Escape, the right
button) and let go of it, unless it is the key of the board still held from the press that kept it.
=================
*/
qboolean CG_ScoresKeyEvent( int key ) {
	const qboolean own = (qboolean)( key == trap->Key_GetKey( "+scores" ) );

	if ( !scores.kept || ( key != A_ESCAPE && key != A_MOUSE2 && !own ) ) {
		return qfalse;
	}
	if ( own ) {
		if ( scores.keyDown ) {
			return qtrue;
		}
		// this press is held too, and what it sends from here on comes as +scores again
		scores.keyDown = scores.keyDone = qtrue;
	}
	CG_ScoresLetGo();
	return qtrue;
}

// scores_pick: the scoreboard with the mouse free, or back to the game
void CG_ScoresPick_f( void ) {
	if ( scores.kept ) {
		CG_ScoresLetGo();
	} else if ( !Scores_Keep() ) {
		trap->Print( "The scoreboard can not be kept open here (cg_hud, cg_selectPlayers, or a demo).\n" );
	}
}
