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

// cg_select.c -- players that can be clicked. Not from JA+, see docs/japlus/client.md, "Clickable players".
//
// While the mouse is free (the key of +v_cursor is held, "cursor" switched it on, the chat box is open or the
// scoreboard is kept open) the player under the pointer has a glowing outline, and a click on him opens a card
// beside him: who he is, with what he plays, and what this player can do about him (his voice, his chat lines, a
// whisper, a vote, what an admin may). A click on a row of the scoreboard opens the same card beside the board
// (hud/cg_scores.c), for a player who is not in sight as well.
//
// All of it happens in this client. Nothing is sent that the commands it uses would not send when typed, and
// no other client sees any of it.
//
// The boxes of the boombox (boombox/cg_boombox.c) are pointed at the same way: the one under the pointer has the
// outline, a click opens its card (what it plays, and what can be done with it), and a box that is pressed on and
// dragged follows the pointer over the floor until the button is let go. They have the numbers of the pointer from
// SELECT_BOX on, a number for every piece (BOX_PIECE of cg_boombox.h): the table and each speaker of a sound system
// are pointed at and dragged by themselves, and all of them open the card of the system. "Open the player" on its card opens the window of the box (boombox/cg_boombox_ui.c), which is
// drawn here and gets the keys first while it is open.
//
// The outline is the model of the player drawn a second time with a shader that only shows a rim around it. An
// engine of this tree makes that shader from a flag (RGBA_SHADER_HULL), so it is there on a pure server
// without the pack. Elsewhere the same shader comes from the pack (gfx/vision/select_hull), and where that is
// missing too the player under the pointer gets four corners in the 2D.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cgame/boombox/cg_boombox.h"

qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );		// cg_draw.c

#define SELECT_RANGE		4096.0f		// how far away a player can still be picked
#define SELECT_GROW			6.0f		// the box of a player is this much larger for the pointer
#define SELECT_CARD_W		150.0f
#define SELECT_ROW_H		13.0f
#define SELECT_HEAD_NAME	20.0f		// the name at the head of the card takes this much of it,
#define SELECT_HEAD_LINE	10.0f		//	every line under it this much,
#define SELECT_HEAD_END		4.0f		//	and this much is below the last one
#define SELECT_HEAD_LINES	8
#define SELECT_MAX_ROWS		14
#define SELECT_SCALE		0.6f		// of the text of the card
#define SELECT_CONFIRM_MSEC	4000		// a vote to kick has to be clicked twice within this time
#define SELECT_BOX			MAX_CLIENTS	// piece n of a box of the boombox is SELECT_BOX + n for the pointer
#define SELECT_DRAG			4.0f		// the pointer moved this far with the button down on a box: it is dragged, not clicked

typedef enum {
	ACT_MUTE,
	ACT_QUIETER,
	ACT_LOUDER,
	ACT_HIDE_CHAT,
	ACT_WHISPER,
	ACT_WATCH,
	ACT_COPY,
	ACT_VOTE_KICK,
	ACT_ADMIN_KICK,
	ACT_ADMIN_SILENCE,
	ACT_ADMIN_SLEEP,
	ACT_ADMIN_SLAP,
	ACT_ADMIN_GOTO,
	ACT_ADMIN_BRING,
	ACT_BOX_WINDOW,
	ACT_BOX_PLAY,
	ACT_BOX_PAUSE,
	ACT_BOX_SKIP,
	ACT_BOX_STOP,
	ACT_BOX_QUIETER,
	ACT_BOX_LOUDER,
	ACT_BOX_MUTE,
	ACT_BOX_MOVE,
	ACT_BOX_REMOVE
} selectAct_t;

#define SELECT_BESIDE		21.0f	// how far beside the middle of a player his card and his name stand, in units of the level,
#define SELECT_ABOVE		36.0f	//	how far above his middle their top edge is,
#define SELECT_GAP			5.0f	//	and how far from that place they begin, in units of the card
#define SELECT_UNIT			0.3f	// What a unit of the card is in the level, where the card is a thing there: the name on it
									// has letters as tall as those of a chat bubble.
#define SELECT_LINE			18.0f	// a line of text of the HUD is this high at a scale of 1, in its units,
#define SELECT_BASE			0.857f	//	and its letters stand on this part of it
#define SELECT_FLOOR		60.0f	// a card ends this far above the lower edge of the screen: the bars of the HUD are below
#define SELECT_FLAT_PX		15.0f	// A card whose rows are lower than this on the screen, in pixels, is hard to read and to
#define SELECT_WORLD_PX		17.5f	//	hit: it grows to the size it was made for, and shrinks back when they are higher than this.
#define SELECT_FLAT_MIN_PX	18.5f	// Grown, its rows are at least this high, so growing never makes it smaller.
#define SELECT_POP_MSEC		180		// how long it takes to grow and to shrink
#define SELECT_TAG_PX		14.0f	// the letters of a name are never lower than this on the screen, in pixels
#define SELECT_NEAR_MAX		1.5f	// Very near, a card and a name are never larger than this many times the size they were made
									// for on the screen: a step in front of a box they were larger than the screen.
#define SELECT_FOLLOW_MSEC	90.0f	// how late they follow him: a step of his is no jump of theirs
#define SELECT_SHELLS	6		// the shells of the outline
#define SELECT_BLUR_TENTHS	12	// how far the hull that the dynamic glow blurs stands off the model, in tenths of a unit,
#define SELECT_BLUR_ALPHA	165	//	and how bright it is, of 255

// What the name and the card of a player are drawn on. In the level it is a thing there as a chat bubble is:
// as large at any distance, and it lies in the plane of the screen: turned to the one who looks also when he looks
// down or up at it, so it is read and clicked without a slant.
typedef struct selectBoard_s {
	qboolean	world;					// in the level. qfalse: on the screen
	vec3_t		corner;					// its top left corner: a place in the level, or x and y in the units of the HUD
	float		unit;					// what a unit of the card is on it: units of the level, or of the HUD
	vec3_t		right, up, toward;		// in the level: along its lines, up them, and to the one who looks
} selectBoard_t;

static struct {
	qboolean	held;					// +v_cursor
	qboolean	toggled;				// "cursor"
	qboolean	catching;				// this file asked for the keys and the mouse
	qboolean	quiet;					// and told the engine that its player does not type meanwhile (v_catchQuiet)
	int			hover;					// the player under the pointer, -1: none
	int			selected;				// the player whose card is open, -1: none
	// what the card and the name of the player under the pointer are drawn on: beside their player, a little late
	selectBoard_t	card;
	qboolean	cardPlaced;				// the card has a place: it moves on from there and does not jump
	// the card of a player who is too far away for it to be read has grown
	qboolean	flat;					// it has, or it is growing. qfalse: as large as a thing in the level is, or shrinking
	int			flatTime;				// when it began to
	float		flatFrom;				// what a unit of it was on the screen then, in units of the HUD,
	float		apparent;				//	and what it is now
	float		cardUnit;				// what a unit of it is in the level now, where it stands beside its player
	selectBoard_t	shown;				// what the card was drawn on last: the pointer is looked for on that
	selectBoard_t	tag;
	int			tagFor;					// the player the name has its place for, -1: none
	selectBoard_t	tagShown;			// what the name is drawn on this frame, and how wide it is in units of the card
	float		tagWidth;
	// the card of a player that was clicked in the scoreboard: on the screen, beside the board
	qboolean	anchored;
	float		anchorX, anchorY;
	// the lines under the name at the head of the card, and how high the head is with them
	struct {
		char		text[128];
		const float	*color;
		char		more[64];			// a second part of the line in a colour of its own, "": none
		const float	*moreColor;
	} head[SELECT_HEAD_LINES];
	int			numHead;
	float		headH;
	selectAct_t	rows[SELECT_MAX_ROWS];
	int			numRows;
	int			confirmTime;			// when the vote to kick was clicked first, 0: not yet
	qhandle_t	hull[SELECT_SHELLS];	// the shaders of the outline, from the model outward. The first 0: none
	qhandle_t	blurHull;				// for the dynamic glow of the renderer: a hull that only its buffer gets, and blurs
	qboolean	hullTried;
	qhandle_t	pointer;				// the pointer of the game, where the new one can not be drawn
	// a box of the boombox
	int			press;					// the left button went down on it, and is still down. -1: on none
	float		pressX, pressY;			//	there
	qboolean	dragging;				// it moved with the button down: the box is in the pointer
	qboolean	carrying;				// "Move it" of its card: the box is in the pointer until the next click
} sel;

#define SELECT_GRAB_MSEC	500		// the key let go by the engine when the pointer takes the keys comes this soon

static int selectHeldTime;			// trap->Milliseconds() when the key of +v_cursor went down

static const vec4_t selectText = { 0.86f, 0.92f, 0.97f, 1.0f }, selectDim = { 0.60f, 0.69f, 0.78f, 1.0f };
static const vec4_t selectAccent = { 0.43f, 0.83f, 1.0f, 1.0f }, selectGold = { 0.89f, 0.84f, 0.36f, 1.0f };

void CG_SelectInit( void ) {
	memset( &sel, 0, sizeof( sel ) );
	sel.hover = sel.selected = sel.tagFor = sel.press = -1;
	sel.headH = SELECT_HEAD_NAME + SELECT_HEAD_END;
}

// Is the mouse free for pointing at players. Also for the quick wheel (radial/cg_radial.c), the command menu
// (cmdmenu/cg_cmdmenu.c) and a box that is carried, without the selection of players.
static qboolean Select_Free( void ) {
	return (qboolean)( cg.snap && !cg.demoPlayback && ( CG_RadialOpen() || CG_CmdMenuOpen() || CG_ToolWindowShown() || sel.carrying || ( v_selectPlayers.integer
		&& ( sel.held || sel.toggled || CG_ChatIsOpen() || CG_ScoresPicking() || CG_BoomboxWindowShown() ) ) ) );
}

/*
=================
CG_SelectDrawCursor

The pointer of the mouse in the HUD: the one of the new look where it can be drawn, else the one of the menus
=================
*/
void CG_SelectDrawCursor( void ) {
	if ( Skin_Ready() ) {
		Skin_Cursor( cgs.cursorX, cgs.cursorY );
		return;
	}
	// (cgs.media.cursor is no use: the base game registers a picture of Quake 3 Team Arena for it that this
	// game does not have)
	if ( !sel.pointer ) {
		sel.pointer = trap->R_RegisterShaderNoMip( "cursor" );
	}
	trap->R_SetColor( NULL );
	CG_DrawPic( cgs.cursorX, cgs.cursorY, 40.0f, 40.0f, sel.pointer );
}

// can this player be pointed at: he is in what the server shows, alive, and can be seen
static qboolean Select_Valid( int clientNum ) {
	const centity_t *cent;

	// a piece of a box of the boombox, in the level and not in the pointer
	if ( clientNum >= SELECT_BOX ) {
		return (qboolean)( CG_BoomboxShown( clientNum - SELECT_BOX ) && CG_BoomboxHeld() != clientNum - SELECT_BOX );
	}
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || clientNum == cg.snap->ps.clientNum || !cgs.clientinfo[clientNum].infoValid ) {
		return qfalse;
	}
	cent = &cg_entities[clientNum];
	if ( !cent->currentValid || cent->currentState.eType != ET_PLAYER || ( cent->currentState.eFlags & ( EF_DEAD | EF_NODRAW ) ) ) {
		return qfalse;
	}
	// cloaked, or hidden from this player by a mind trick
	if ( cent->currentState.powerups & ( 1 << PW_CLOAKED ) ) {
		return qfalse;
	}
	return qtrue;
}

// is it there at all: a player who is connected, a box that stands in the level
static qboolean Select_Exists( int n ) {
	if ( n >= SELECT_BOX ) {
		return CG_BoomboxShown( n - SELECT_BOX );
	}
	return (qboolean)( n >= 0 && cgs.clientinfo[n].infoValid );
}

// the piece of a box in the pointer as a number of the pointer, -1: none
static int Select_Held( void ) {
	const int piece = CG_BoomboxHeld();

	return ( piece >= 0 ) ? SELECT_BOX + piece : -1;
}

// what a card and a name stand beside: the middle of a player and how large he is, or the foot of a box, which is
// as tall as a player is from his middle up
static void Select_Middle( int n, vec3_t at, float *scale ) {
	if ( n >= SELECT_BOX ) {
		CG_BoomboxMiddle( n - SELECT_BOX, at );
		*scale = 1.0f;
		return;
	}
	VectorCopy( cg_entities[n].lerpOrigin, at );
	*scale = ( cg_entities[n].modelScale[2] > 0.01f ) ? cg_entities[n].modelScale[2] : 1.0f;
}

// the name on the card and beside the pointer
static const char *Select_Name( int n ) {
	if ( n >= SELECT_BOX ) {
		return CG_BoomboxPieceName( n - SELECT_BOX );
	}
	return cgs.clientinfo[n].name;
}

// the way from the eye through the pointer into the level
static void Select_Ray( vec3_t dir ) {
	const float	nx = ( cgs.cursorX + cgs.bias2D ) / ( SCREEN_WIDTH + 2.0f * cgs.bias2D ) * 2.0f - 1.0f;
	const float	ny = 1.0f - cgs.cursorY / (float)SCREEN_HEIGHT * 2.0f;

	// (axis 1 of a view points to the left)
	VectorMA( cg.refdef.viewaxis[0], -nx * tanf( DEG2RAD( cg.refdef.fov_x ) * 0.5f ), cg.refdef.viewaxis[1], dir );
	VectorMA( dir, ny * tanf( DEG2RAD( cg.refdef.fov_y ) * 0.5f ), cg.refdef.viewaxis[2], dir );
	VectorNormalize( dir );
}

/*
=================
Select_Pick

The player the pointer is on: a ray from the eye through the pointer against the boxes of the players that are
shown, the nearest one wins, and a wall between the eye and him keeps him from being picked. -1: nobody.
=================
*/
static int Select_Pick( void ) {
	vec3_t		dir, end;
	float		bestT = SELECT_RANGE;
	int			i, k, best = -1;
	trace_t		tr;

	Select_Ray( dir );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t	*cent = &cg_entities[i];
		float			tNear = 0.0f, tFar = bestT;

		if ( !Select_Valid( i ) ) {
			continue;
		}
		for ( k = 0; k < 3; k++ ) {
			const float	lo = cent->lerpOrigin[k] + ( k == 2 ? -24.0f : -15.0f ) - SELECT_GROW;
			const float	hi = cent->lerpOrigin[k] + ( k == 2 ? 40.0f : 15.0f ) + SELECT_GROW;
			float		t0, t1;

			if ( fabsf( dir[k] ) < 0.0001f ) {
				if ( cg.refdef.vieworg[k] < lo || cg.refdef.vieworg[k] > hi ) {
					tNear = tFar + 1.0f;
				}
				continue;
			}
			t0 = ( lo - cg.refdef.vieworg[k] ) / dir[k];
			t1 = ( hi - cg.refdef.vieworg[k] ) / dir[k];
			if ( t0 > t1 ) {
				const float swap = t0;

				t0 = t1;
				t1 = swap;
			}
			tNear = Q_max( tNear, t0 );
			tFar = Q_min( tFar, t1 );
		}
		if ( tNear <= tFar && tNear < bestT ) {
			bestT = tNear;
			best = i;
		}
	}
	// the boxes of the boombox, which may be nearer
	i = CG_BoomboxPick( cg.refdef.vieworg, dir, &bestT );
	if ( i >= 0 ) {
		best = SELECT_BOX + i;
	}
	if ( best < 0 ) {
		return -1;
	}
	// nothing of the level between the eye and where the ray meets his box (a box is no entity: nothing may be there)
	VectorMA( cg.refdef.vieworg, bestT, dir, end );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, cg.snap->ps.clientNum, MASK_SOLID );
	if ( tr.fraction < 1.0f && ( best >= SELECT_BOX || tr.entityNum != best ) ) {
		return -1;
	}
	return best;
}

/*
===============================================================================

The outline

===============================================================================
*/

// The outline is a light that fades: the model again in shells that stand off it, the wider the fainter. The
// innermost is added to what is behind it and glows (r_DynamicGlow), the others are blended.
static const struct {
	int		tenths;		// how far the shell stands off the model, in tenths of a unit
	int		flags;
	byte	alpha;
} selectShells[SELECT_SHELLS] = {
	{ 5, RGBA_SHADER_GLOW, 255 },
	{ 10, RGBA_SHADER_BEHIND, 104 },
	{ 16, RGBA_SHADER_BEHIND, 72 },
	{ 23, RGBA_SHADER_BEHIND, 46 },
	{ 31, RGBA_SHADER_BEHIND, 28 },
	{ 40, RGBA_SHADER_BEHIND, 15 },
};

static void Select_Hull( void ) {
	// (not const: the renderer scales the light of a picture in the memory it is given)
	static byte			white[4 * 4 * 4];
	char				value[8];
	int					i;

	if ( sel.hullTried ) {
		return;
	}
	sel.hullTried = qtrue;
	// the flag came with the engine that says v_uiSkin
	trap->Cvar_VariableStringBuffer( "v_uiSkin", value, sizeof( value ) );
	if ( atoi( value ) == 1 && Skin_Ready() ) {
		for ( i = 0; i < SELECT_SHELLS; i++ ) {
			memset( white, 255, sizeof( white ) );
			sel.hull[i] = trap->ext.R_RegisterShaderRGBA( va( "select_hull_%i", selectShells[i].tenths ), white, 4, 4,
				RGBA_SHADER_HULL_WIDTH( selectShells[i].tenths ) | selectShells[i].flags );
		}
		memset( white, 255, sizeof( white ) );
		sel.blurHull = trap->ext.R_RegisterShaderRGBA( "select_blur", white, 4, 4, RGBA_SHADER_HULL_WIDTH( SELECT_BLUR_TENTHS ) | RGBA_SHADER_BLUR );
	}
	if ( !sel.hull[0] ) {
		sel.hull[0] = trap->R_RegisterShader( "gfx/vision/select_hull" );
	}
}

/*
=================
Select_Outlined

Does this player get an outline this frame: the one under the pointer and the one whose card is open. His model
and the blades of his saber are then drawn after everything else (RF_FORCEPOST), so they lie on the shells of
the outline, which are drawn before them and write no depth: what is left of the shells is what stands out
around the figure, and no line of them crosses it.
=================
*/
static qboolean Select_Outlined( int clientNum ) {
	// (not the own figure: the own card, opened in the scoreboard, is no reason to light oneself up)
	// (a box in the pointer has it too, red where it can not be put)
	if ( clientNum < 0 || clientNum >= SELECT_BOX + BOX_PIECES || !v_selectOutline.integer || !cg.snap || clientNum == cg.snap->ps.clientNum
		|| ( clientNum != sel.hover && clientNum != sel.selected && clientNum != Select_Held() ) || !Select_Free() ) {
		return qfalse;
	}
	Select_Hull();
	return (qboolean)( sel.hull[0] != 0 );
}

/*
=================
Select_Outline

Called while a player is added to the scene, with his model: the one under the pointer and the one whose card is
open are added a second time as an outline.
=================
*/
static void Select_Outline( int clientNum, const refEntity_t *model ) {
	refEntity_t			hull;
	const clientInfo_t	*ci;
	const float			strength = ( clientNum == sel.selected || clientNum == Select_Held() ) ? 1.0f : 0.75f;
	char				value[8];
	qboolean			blur;
	int					i;

	if ( !Select_Outlined( clientNum ) ) {
		return;
	}
	ci = ( clientNum < SELECT_BOX ) ? &cgs.clientinfo[clientNum] : NULL;
	hull = *model;
	hull.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA | RF_NODEPTH | RF_FORCEPOST | RF_AFTERGLOW );
	hull.renderfx |= RF_NOSHADOW;
	// light blue, or the colour of his team. A box in the pointer that can not be put where it is: red.
	if ( !ci ) {
		if ( clientNum == Select_Held() && CG_BoomboxHeldBad() ) {
			hull.shaderRGBA[0] = 255; hull.shaderRGBA[1] = 80; hull.shaderRGBA[2] = 70;
		} else {
			hull.shaderRGBA[0] = 110; hull.shaderRGBA[1] = 212; hull.shaderRGBA[2] = 255;
		}
	} else if ( cgs.gametype >= GT_TEAM && ci->team == TEAM_RED ) {
		hull.shaderRGBA[0] = 255; hull.shaderRGBA[1] = 96; hull.shaderRGBA[2] = 84;
	} else if ( cgs.gametype >= GT_TEAM && ci->team == TEAM_BLUE ) {
		hull.shaderRGBA[0] = 96; hull.shaderRGBA[1] = 150; hull.shaderRGBA[2] = 255;
	} else {
		hull.shaderRGBA[0] = 110; hull.shaderRGBA[1] = 212; hull.shaderRGBA[2] = 255;
	}
	// With the dynamic glow of the renderer the light around the figure is a real blur: the figure goes into the
	// buffer of the glow as a hull in the colour of the outline, the renderer blurs the buffer and adds it to the
	// picture, and the player is drawn after that (RF_AFTERGLOW, see CG_SelectOutlined()), so the light lies
	// around him and not on him. In the picture itself only the thin line at his edge is drawn. Without the glow
	// (r_DynamicGlow 0, or an engine that does not know the flag) the fading shells are the light.
	trap->Cvar_VariableStringBuffer( "r_DynamicGlow", value, sizeof( value ) );
	blur = (qboolean)( sel.blurHull && atoi( value ) != 0 );
	for ( i = 0; i < ( blur ? 1 : SELECT_SHELLS ); i++ ) {
		if ( sel.hull[i] ) {
			hull.customShader = sel.hull[i];
			hull.shaderRGBA[3] = (byte)( selectShells[i].alpha * strength );
			trap->R_AddRefEntityToScene( &hull );
		}
	}
	if ( blur ) {
		hull.customShader = sel.blurHull;
		hull.shaderRGBA[3] = (byte)( SELECT_BLUR_ALPHA * strength );
		trap->R_AddRefEntityToScene( &hull );
	}
}

// (the game draws NPCs with these functions too, whose numbers are where the pointer has the boxes)
qboolean CG_SelectOutlined( int clientNum ) {
	return ( clientNum >= 0 && clientNum < MAX_CLIENTS ) ? Select_Outlined( clientNum ) : qfalse;
}

void CG_SelectOutline( int clientNum, const refEntity_t *model ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		Select_Outline( clientNum, model );
	}
}

// the same for a piece of a box of the boombox (boombox/cg_boombox.c)
qboolean CG_SelectBoxOutlined( int piece ) {
	return ( piece >= 0 && piece < BOX_PIECES ) ? Select_Outlined( SELECT_BOX + piece ) : qfalse;
}

void CG_SelectBoxOutline( int piece, const refEntity_t *model ) {
	if ( piece >= 0 && piece < BOX_PIECES ) {
		Select_Outline( SELECT_BOX + piece, model );
	}
}

/*
===============================================================================

The card

===============================================================================
*/

static void Select_Close( void ) {
	sel.selected = -1;
	sel.confirmTime = 0;
	sel.cardPlaced = sel.flat = sel.anchored = qfalse;
	sel.flatTime = 0;
	sel.cardUnit = sel.apparent = 0.0f;
}

// the card of a player or a box opens, where its name stood
static void Select_Open( int n ) {
	sel.selected = n;
	sel.confirmTime = 0;
	sel.flat = qfalse;
	sel.flatTime = 0;
	sel.cardUnit = sel.apparent = 0.0f;
	sel.cardPlaced = (qboolean)( sel.tagFor == n );
	if ( sel.cardPlaced ) {
		sel.card = sel.tag;
	}
}

// a box of the boombox leaves the pointer: put down where it is (place), or back where it stood
static void Select_LetBoxGo( qboolean place ) {
	if ( CG_BoomboxHeld() >= 0 ) {
		CG_BoomboxDrop( place );
	}
	sel.press = -1;
	sel.dragging = sel.carrying = qfalse;
}

/*
=================
Select_Beside

Where something w wide and h high stands on the screen to be beside a player: at his right as the camera sees
him, its top at the height of his head, at his left where the screen ends at his right. The place is one in the
level, so it is as near to him at any distance. qfalse: he is not in front of the camera.
=================
*/
static qboolean Select_Beside( int clientNum, float w, float h, float *x, float *y ) {
	const float		least = 4.0f - cgs.bias2D, most = SCREEN_WIDTH + cgs.bias2D - w - 4.0f;
	vec3_t			at;
	float			sx, sy, other, scale;

	Select_Middle( clientNum, at, &scale );
	at[2] += SELECT_ABOVE * scale;
	VectorMA( at, -SELECT_BESIDE * scale, cg.refdef.viewaxis[1], at );
	if ( !CG_WorldCoordToScreenCoordFloat( at, &sx, &sy ) ) {
		return qfalse;
	}
	*x = sx + SELECT_GAP;
	if ( *x > most ) {
		VectorMA( at, 2.0f * SELECT_BESIDE * scale, cg.refdef.viewaxis[1], at );
		if ( CG_WorldCoordToScreenCoordFloat( at, &sx, &other ) ) {
			*x = sx - SELECT_GAP - w;
		}
	}
	*x = Com_Clamp( least, most, *x );
	*y = Com_Clamp( 30.0f, SCREEN_HEIGHT - h - SELECT_FLOOR, sy );
	return qtrue;
}

/*
=================
Select_BesideWorld

The same for a board that is a thing in the level, w and h in units of the card, of which b->unit says how large
one is there: the place of its top left corner. At the right of the player as the camera sees him, at his left where the screen ends at his right, and
lifted, lowered or moved aside as far as it would reach over an edge of the screen (a box at the feet of the
player is below it): the view can not be turned to it while the mouse is free. qfalse: it is not in front of the
camera.
=================
*/
static qboolean Select_BesideWorld( int clientNum, float w, float h, const selectBoard_t *b, vec3_t corner ) {
	vec3_t			base, far;
	float			sx, sy, z, perUnit, over, scale, beside, width;

	Select_Middle( clientNum, base, &scale );
	beside = SELECT_BESIDE * scale + SELECT_GAP * b->unit;
	base[2] += SELECT_ABOVE * scale;
	VectorMA( base, beside, b->right, corner );
	VectorMA( corner, w * b->unit, b->right, far );
	if ( !CG_WorldCoordToScreenCoordFloat( far, &sx, &sy ) || sx > SCREEN_WIDTH + cgs.bias2D - 4.0f ) {
		VectorMA( base, -beside - w * b->unit, b->right, corner );
	}
	VectorSubtract( corner, cg.refdef.vieworg, far );
	z = DotProduct( far, cg.refdef.viewaxis[0] );
	if ( z < 8.0f || !CG_WorldCoordToScreenCoordFloat( corner, &sx, &sy ) ) {
		return qfalse;
	}
	// (units of the HUD a unit of the level, at its distance)
	perUnit = SCREEN_HEIGHT * 0.5f / ( z * tanf( DEG2RAD( cg.refdef.fov_y ) * 0.5f ) );
	over = sy + h * b->unit * perUnit - ( SCREEN_HEIGHT - SELECT_FLOOR );
	if ( over > 0.0f ) {
		VectorMA( corner, over / perUnit, b->up, corner );
	} else if ( sy < 30.0f ) {
		VectorMA( corner, -( 30.0f - sy ) / perUnit, b->up, corner );
	}
	width = w * b->unit * perUnit;
	if ( sx < 4.0f - cgs.bias2D ) {
		VectorMA( corner, ( 4.0f - cgs.bias2D - sx ) / perUnit, b->right, corner );
	} else if ( sx + width > SCREEN_WIDTH + cgs.bias2D - 4.0f ) {
		VectorMA( corner, -( sx + width - SCREEN_WIDTH - cgs.bias2D + 4.0f ) / perUnit, b->right, corner );
	}
	return qtrue;
}

// moves a place toward where it should be: most of the way within SELECT_FOLLOW_MSEC
static void Select_Follow( vec3_t at, const vec3_t to, qboolean jump ) {
	const float part = jump ? 1.0f : 1.0f - expf( -cg.frametime / SELECT_FOLLOW_MSEC );

	at[0] += ( to[0] - at[0] ) * part;
	at[1] += ( to[1] - at[1] ) * part;
	at[2] += ( to[2] - at[2] ) * part;
}

// a place on the screen on whole pixels: text that moves stays sharp
static void Select_Pixels( float *x, float *y ) {
	const float px = ( SCREEN_WIDTH + 2.0f * cgs.bias2D ) / cgs.glconfig.vidWidth, py = (float)SCREEN_HEIGHT / cgs.glconfig.vidHeight;

	*x = floorf( *x / px + 0.5f ) * px;
	*y = floorf( *y / py + 0.5f ) * py;
}

/*
===============================================================================

What the name and the card are drawn on

===============================================================================
*/

// can they be things in the level? The engine has to write in the scene.
static qboolean Select_World( void ) {
	return CG_BubbleReady();
}

// what a board is drawn on this frame: where the engine can in the level, turned to the camera
static void Board_Begin( selectBoard_t *b ) {
	b->world = Select_World();
	b->unit = b->world ? SELECT_UNIT : 1.0f;
	if ( !b->world ) {
		return;
	}
	// (axis 1 of a view points to the left)
	VectorNegate( cg.refdef.viewaxis[1], b->right );
	VectorCopy( cg.refdef.viewaxis[2], b->up );
	VectorNegate( cg.refdef.viewaxis[0], b->toward );
}

// Puts a board beside a player, w and h in units of the card, and lets it follow him. qfalse: he is not in front
// of the camera, the board stays where it is.
static qboolean Board_Place( selectBoard_t *b, int clientNum, float w, float h, qboolean jump ) {
	vec3_t to;

	if ( b->world ) {
		if ( !Select_BesideWorld( clientNum, w, h, b, to ) ) {
			return qfalse;
		}
	} else {
		if ( !Select_Beside( clientNum, w, h, &to[0], &to[1] ) ) {
			return qfalse;
		}
		to[2] = 0.0f;
	}
	Select_Follow( b->corner, to, jump );
	return qtrue;
}

// A place on a board in the level: x to the right and y down from its top left corner, in units of the card.
// (Everything of a board lies in its one plane: nothing of it writes depth, what is added later is drawn on what
// came before.)
static void Board_Point( const selectBoard_t *b, float x, float y, vec3_t out ) {
	VectorMA( b->corner, x * b->unit, b->right, out );
	VectorMA( out, -y * b->unit, b->up, out );
}

// how far before the camera a player is, and what a unit of the level is on the screen there, in units of the HUD.
// qfalse: he is not before it
static qboolean Select_Away( int clientNum, float *perUnit ) {
	vec3_t	at, to;
	float	z, scale;

	Select_Middle( clientNum, at, &scale );
	VectorSubtract( at, cg.refdef.vieworg, to );
	z = DotProduct( to, cg.refdef.viewaxis[0] );
	if ( z < 8.0f ) {
		return qfalse;
	}
	*perUnit = SCREEN_HEIGHT * 0.5f / ( z * tanf( DEG2RAD( cg.refdef.fov_y ) * 0.5f ) );
	return qtrue;
}

static void Board_Panel( const selectBoard_t *b, float x, float y, float w, float h, float radius, const float *fill, const float *edge ) {
	const float u = b->unit;

	if ( b->world ) {
		vec3_t center;

		Board_Point( b, x + w * 0.5f, y + h * 0.5f, center );
		CG_BubbleBoard( center, b->right, b->up, w * 0.5f * u, h * 0.5f * u, Q_min( radius, Q_min( w, h ) * 0.5f ) * u, fill, edge );
	} else if ( Skin_Ready() ) {
		Skin_Panel( b->corner[0] + x * u, b->corner[1] + y * u, w * u, h * u, radius * u, fill, edge );
	} else {
		CG_FillRect( b->corner[0] + x * u, b->corner[1] + y * u, w * u, h * u, (float *)fill );
		if ( edge ) {
			CG_DrawRect( b->corner[0] + x * u, b->corner[1] + y * u, w * u, h * u, 1.0f, (float *)edge );
		}
	}
}

static void Board_Text( const selectBoard_t *b, float x, float y, float scale, const float *color, const char *text ) {
	if ( b->world ) {
		vec3_t at;

		Board_Point( b, x, y + SELECT_LINE * SELECT_BASE * scale, at );
		trap->ext.R_Text3D( at, b->right, b->up, SELECT_LINE * scale * b->unit, text, color );
	} else {
		CG_Text_Paint( b->corner[0] + x * b->unit, b->corner[1] + y * b->unit, scale * b->unit, (float *)color, text, 0, 0, 0,
			Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2 );
	}
}

static float Board_Width( const selectBoard_t *b, const char *text, float scale ) {
	if ( b->world ) {
		return trap->ext.R_Text3DWidth( text, SELECT_LINE * scale * b->unit ) / b->unit;
	}
	return CG_Text_Width( text, scale * b->unit, Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2 ) / b->unit;
}

typedef struct boardFit_s {
	const selectBoard_t	*board;
	float				scale;
} boardFit_t;

static float Board_FitWidth( const char *text, void *arg ) {
	const boardFit_t *fit = (const boardFit_t *)arg;

	return Board_Width( fit->board, text, fit->scale );
}

// A line that has to stay within `most` units of the card: it is set smaller as far as it stays readable, and
// what is still too long ends in three dots.
static void Board_TextFit( const selectBoard_t *b, float x, float y, float scale, const float *color, const char *text, float most ) {
	const float	width = Board_Width( b, text, scale );
	boardFit_t	fit;
	char		line[160];

	// (a little smaller than it has to be: set to fit exactly, it is too wide again by what a float is off, and
	// was cut although it had room)
	if ( width > most ) {
		scale = Q_max( scale * 0.8f, scale * most / width * 0.98f );
	}
	fit.board = b;
	fit.scale = scale;
	CG_TextCut( text, most, Board_FitWidth, &fit, line, sizeof( line ) );
	Board_Text( b, x, y, scale, color, line );
}

// where a place of a board is on the screen, in units of the HUD. qfalse: not in front of the camera
static qboolean Board_Screen( const selectBoard_t *b, float x, float y, float *sx, float *sy ) {
	vec3_t at;

	if ( !b->world ) {
		*sx = b->corner[0] + x * b->unit;
		*sy = b->corner[1] + y * b->unit;
		return qtrue;
	}
	Board_Point( b, x, y, at );
	return CG_WorldCoordToScreenCoordFloat( at, sx, sy );
}

// where the pointer is on the card, in its units from its top left corner. qfalse: nowhere on it
static qboolean Board_Pointer( const selectBoard_t *b, float *x, float *y ) {
	vec3_t	dir, to, hit;
	float	toward, t;

	if ( !b->world ) {
		*x = ( cgs.cursorX - b->corner[0] ) / b->unit;
		*y = ( cgs.cursorY - b->corner[1] ) / b->unit;
		return qtrue;
	}
	// the way of the pointer through the board, which looks at the camera
	Select_Ray( dir );
	toward = DotProduct( dir, b->toward );
	if ( toward > -0.001f ) {
		return qfalse;
	}
	VectorSubtract( b->corner, cg.refdef.vieworg, to );
	t = DotProduct( to, b->toward ) / toward;
	if ( t <= 0.0f ) {
		return qfalse;
	}
	VectorMA( cg.refdef.vieworg, t, dir, hit );
	VectorSubtract( hit, b->corner, to );
	*x = DotProduct( to, b->right ) / b->unit;
	*y = -DotProduct( to, b->up ) / b->unit;
	return qtrue;
}

// what the card of a box of the boombox offers: everybody may do everything with every box
static void Select_BoxRows( int box ) {
	boomboxInfo_t info;

	if ( !CG_BoomboxInfo( box, &info ) ) {
		return;
	}
	sel.rows[sel.numRows++] = ACT_BOX_WINDOW;
	sel.rows[sel.numRows++] = ACT_BOX_PLAY;
	if ( info.flags & BOX_PLAYING ) {
		sel.rows[sel.numRows++] = ACT_BOX_PAUSE;
		sel.rows[sel.numRows++] = ACT_BOX_SKIP;
		sel.rows[sel.numRows++] = ACT_BOX_STOP;
	}
	sel.rows[sel.numRows++] = ACT_BOX_QUIETER;
	sel.rows[sel.numRows++] = ACT_BOX_LOUDER;
	sel.rows[sel.numRows++] = ACT_BOX_MUTE;
	sel.rows[sel.numRows++] = ACT_BOX_MOVE;
	sel.rows[sel.numRows++] = ACT_BOX_REMOVE;
}

// what the card of a player offers, in this order
static void Select_Rows( int clientNum ) {
	char value[8];

	sel.numRows = 0;
	if ( clientNum >= SELECT_BOX ) {
		Select_BoxRows( BOX_OF( clientNum - SELECT_BOX ) );
		return;
	}
	// (the own card, opened in the scoreboard, says who one is and offers nothing)
	if ( clientNum == cg.clientNum ) {
		return;
	}
	// his voice: only a client of this tree has one, so only who is known to play with it (a bot, another client
	// and one nobody knows about yet get none of these rows)
	if ( CG_PeerVision( clientNum, NULL ) == 1 ) {
		sel.rows[sel.numRows++] = ACT_MUTE;
		sel.rows[sel.numRows++] = ACT_QUIETER;
		sel.rows[sel.numRows++] = ACT_LOUDER;
	}
	sel.rows[sel.numRows++] = ACT_HIDE_CHAT;
	trap->Cvar_VariableStringBuffer( "v_cgameChat", value, sizeof( value ) );
	if ( atoi( value ) == 1 ) {
		if ( v_chatInput.integer ) {
			sel.rows[sel.numRows++] = ACT_WHISPER;
		}
		sel.rows[sel.numRows++] = ACT_COPY;
	}
	if ( cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR && cgs.clientinfo[clientNum].team != TEAM_SPECTATOR ) {
		sel.rows[sel.numRows++] = ACT_WATCH;
	}
	sel.rows[sel.numRows++] = ACT_VOTE_KICK;
	if ( CG_AuthIsAdmin() ) {
		sel.rows[sel.numRows++] = ACT_ADMIN_KICK;
		sel.rows[sel.numRows++] = ACT_ADMIN_SILENCE;
		sel.rows[sel.numRows++] = ACT_ADMIN_SLEEP;
		sel.rows[sel.numRows++] = ACT_ADMIN_SLAP;
		sel.rows[sel.numRows++] = ACT_ADMIN_GOTO;
		sel.rows[sel.numRows++] = ACT_ADMIN_BRING;
	}
}

static const char *Select_Label( selectAct_t act, int clientNum ) {
	boomboxInfo_t info;

	if ( clientNum >= SELECT_BOX && !CG_BoomboxInfo( BOX_OF( clientNum - SELECT_BOX ), &info ) ) {
		return "";
	}
	switch ( act ) {
	case ACT_BOX_WINDOW:	return CG_VisionText( "Open the player" );
	case ACT_BOX_PLAY:		return CG_VisionText( ( info.flags & BOX_PLAYING ) ? "Add a song ..." : "Play a song ..." );
	case ACT_BOX_PAUSE:		return CG_VisionText( ( info.flags & BOX_PAUSED ) ? "Resume" : "Pause" );
	case ACT_BOX_SKIP:		return CG_VisionText( "Skip this song" );
	case ACT_BOX_STOP:		return CG_VisionText( "Stop the music" );
	case ACT_BOX_QUIETER:	return CG_VisionTextWith( "Quieter for everybody (%i %%)", va( "%i", info.volume ) );
	case ACT_BOX_LOUDER:	return CG_VisionTextWith( "Louder for everybody (%i %%)", va( "%i", info.volume ) );
	case ACT_BOX_MUTE:		return CG_VisionText( info.muted ? "Hear it again" : "Mute it for me" );
	case ACT_BOX_MOVE:		return CG_VisionText( "Move it" );
	case ACT_BOX_REMOVE:	return CG_VisionText( ( sel.confirmTime && cg.time - sel.confirmTime < SELECT_CONFIRM_MSEC ) ? "Click again to take it away" : "Take it away" );
	case ACT_MUTE:			return CG_VisionText( CG_VoiceMuted( clientNum ) ? "Hear his voice again" : "Mute his voice" );
	case ACT_QUIETER:		return CG_VisionTextWith( "Voice quieter (%i %%)", va( "%i", CG_VoiceLouder( clientNum, 0 ) ) );
	case ACT_LOUDER:		return CG_VisionTextWith( "Voice louder (%i %%)", va( "%i", CG_VoiceLouder( clientNum, 0 ) ) );
	case ACT_HIDE_CHAT:		return CG_VisionText( CG_ChatHidden( clientNum ) ? "Show his chat lines again" : "Hide his chat lines" );
	case ACT_WHISPER:		return CG_VisionText( "Whisper to him" );
	case ACT_WATCH:			return CG_VisionText( "Watch him" );
	case ACT_COPY:			return CG_VisionText( "Copy his name" );
	case ACT_VOTE_KICK:		return CG_VisionText( ( sel.confirmTime && cg.time - sel.confirmTime < SELECT_CONFIRM_MSEC ) ? "Click again to call the vote" : "Call a vote to kick him" );
	case ACT_ADMIN_KICK:	return CG_VisionText( "Admin: kick" );
	case ACT_ADMIN_SILENCE:	return CG_VisionText( "Admin: silence" );
	case ACT_ADMIN_SLEEP:	return CG_VisionText( "Admin: sleep" );
	case ACT_ADMIN_SLAP:	return CG_VisionText( "Admin: slap" );
	case ACT_ADMIN_GOTO:	return CG_VisionText( "Admin: go to him" );
	case ACT_ADMIN_BRING:	return CG_VisionText( "Admin: bring him here" );
	}
	return "";
}

/*
=================
Select_Head

The lines under the name at the head of the card, once a frame: what he is and with what he plays, as far as
that is known (status/cg_peers.c); his score, ping and time if the scoreboard has a line of him; what stands
above his head; his model and saber.
=================
*/
static void Select_HeadLine( const char *text, const float *color ) {
	if ( sel.numHead < SELECT_HEAD_LINES ) {
		Q_strncpyz( sel.head[sel.numHead].text, text, sizeof( sel.head[0].text ) );
		sel.head[sel.numHead].color = color;
		sel.head[sel.numHead].more[0] = '\0';
		sel.numHead++;
	}
}

// a time of a song: minutes and seconds
static const char *Select_Time( int seconds ) {
	return va( "%i:%02i", seconds / 60, seconds % 60 );
}

// The head of the card of a box: what it does, the song, how far into it and who asked for it, the next one, its
// volume, who put it down, and the last line the server had for this player
static void Select_BoxHead( int box ) {
	boomboxInfo_t info;

	sel.numHead = 0;
	sel.headH = SELECT_HEAD_NAME + SELECT_HEAD_END;
	if ( !CG_BoomboxInfo( box, &info ) ) {
		return;
	}
	if ( info.flags & BOX_PLAYING ) {
		Select_HeadLine( CG_VisionText( ( info.flags & BOX_LOADING ) ? "Loading ..." : ( info.flags & BOX_PAUSED ) ? "Paused" : "Plays" ), selectDim );
		Com_sprintf( sel.head[0].more, sizeof( sel.head[0].more ), "%s / %s", Select_Time( info.msec / 1000 ), Select_Time( info.length ) );
		sel.head[0].moreColor = selectGold;
		Select_HeadLine( info.title, selectAccent );
		if ( info.by[0] ) {
			Select_HeadLine( CG_VisionTextWith( "Asked for by %s", info.by ), selectDim );
		}
		if ( info.waiting > 0 ) {
			Select_HeadLine( CG_VisionTextWith( "Next: %s", info.next ), selectDim );
			if ( info.waiting > 1 ) {
				Com_sprintf( sel.head[sel.numHead - 1].more, sizeof( sel.head[0].more ), "+%i", info.waiting - 1 );
				sel.head[sel.numHead - 1].moreColor = selectDim;
			}
		}
	} else {
		Select_HeadLine( CG_VisionText( "Silent" ), selectDim );
	}
	if ( info.flags & BOX_LOOKING ) {
		Select_HeadLine( CG_VisionText( "Looking a song up ..." ), selectDim );
	}
	Select_HeadLine( CG_VisionTextWith( "Volume %i %%", va( "%i", info.volume ) ), selectDim );
	if ( info.muted ) {
		Q_strncpyz( sel.head[sel.numHead - 1].more, CG_VisionText( "muted for you" ), sizeof( sel.head[0].more ) );
		sel.head[sel.numHead - 1].moreColor = selectGold;
	}
	if ( info.owner[0] ) {
		Select_HeadLine( CG_VisionTextWith( "Put down by %s", info.owner ), selectDim );
	}
	if ( info.note[0] ) {
		Select_HeadLine( info.note, selectGold );
	}
	sel.headH = SELECT_HEAD_NAME + sel.numHead * SELECT_HEAD_LINE + SELECT_HEAD_END;
}

static void Select_Head( int n ) {
	const clientInfo_t	*ci;
	const char			*version;
	char				line[128];
	int					i, vision;

	if ( n >= SELECT_BOX ) {
		Select_BoxHead( BOX_OF( n - SELECT_BOX ) );
		return;
	}
	ci = &cgs.clientinfo[n];
	sel.numHead = 0;
	Select_HeadLine( CG_VisionText( ( n == cg.clientNum ) ? "You" : ( ci->botSkill != -1 ) ? "Bot" : ( ci->team == TEAM_RED ) ? "Red"
		: ( ci->team == TEAM_BLUE ) ? "Blue" : ( ci->team == TEAM_SPECTATOR ) ? "Spectator" : "Player" ), selectDim );
	vision = CG_PeerVision( n, &version );
	if ( vision == 1 ) {
		Com_sprintf( sel.head[0].more, sizeof( sel.head[0].more ), "VisionJK%s%s", version[0] ? " " : "", version );
		sel.head[0].moreColor = selectGold;
	} else if ( vision == 0 && ci->botSkill == -1 ) {
		Q_strncpyz( sel.head[0].more, CG_VisionText( "Another client" ), sizeof( sel.head[0].more ) );
		sel.head[0].moreColor = selectDim;
	}

	for ( i = 0; i < cg.numScores; i++ ) {
		if ( cg.scores[i].client == n && cg.scores[i].ping != -1 ) {
			Q_strncpyz( line, CG_VisionTextWith( "Score %i", va( "%i", cg.scores[i].score ) ), sizeof( line ) );
			if ( ci->botSkill == -1 ) {
				Q_strcat( line, sizeof( line ), "   " );
				Q_strcat( line, sizeof( line ), CG_VisionTextWith( "Ping %i", va( "%i", cg.scores[i].ping ) ) );
			}
			Q_strcat( line, sizeof( line ), "   " );
			Q_strcat( line, sizeof( line ), CG_VisionTextWith( "%i min", va( "%i", cg.scores[i].time ) ) );
			Select_HeadLine( line, selectDim );
			break;
		}
	}
	if ( CG_StatusLine( n, line, sizeof( line ) ) ) {
		Select_HeadLine( line, selectAccent );
	}
	// his model, with its skin where that is one word, and his saber or sabers
	if ( ci->modelName[0] ) {
		Q_strncpyz( line, ci->modelName, sizeof( line ) );
		if ( ci->skinName[0] && Q_stricmp( ci->skinName, "default" ) && !strchr( ci->skinName, '|' ) ) {
			Q_strcat( line, sizeof( line ), va( "/%s", ci->skinName ) );
		}
		if ( ci->saberName[0] && Q_stricmp( ci->saberName, "none" ) ) {
			Q_strcat( line, sizeof( line ), va( "  |  %s", ci->saberName ) );
			if ( ci->saber2Name[0] && Q_stricmp( ci->saber2Name, "none" ) ) {
				Q_strcat( line, sizeof( line ), va( " + %s", ci->saber2Name ) );
			}
		}
		Q_StripColor( line );
		Select_HeadLine( line, selectDim );
	}
	sel.headH = SELECT_HEAD_NAME + sel.numHead * SELECT_HEAD_LINE + SELECT_HEAD_END;
}

// One row of the card was chosen. What it does is what the command it runs does when it is typed: the server
// decides as always who may.
static void Select_Run( selectAct_t act, int clientNum ) {
	const int		box = ( clientNum >= SELECT_BOX ) ? BOX_OF( clientNum - SELECT_BOX ) : -1;
	char			name[MAX_NETNAME];
	boomboxInfo_t	info;

	if ( clientNum >= SELECT_BOX && !CG_BoomboxInfo( box, &info ) ) {
		Select_Close();
		return;
	}
	switch ( act ) {
	case ACT_BOX_WINDOW:
		Select_Close();
		CG_BoomboxWindowOpen( box );
		return;
	case ACT_BOX_PLAY:
		Select_Close();
		CG_BoomboxPrompt( box );
		return;
	case ACT_BOX_PAUSE:
		CG_BoomboxDo( box, ( info.flags & BOX_PAUSED ) ? "resume" : "pause" );
		return;
	case ACT_BOX_SKIP:
		CG_BoomboxDo( box, "skip" );
		return;
	case ACT_BOX_STOP:
		CG_BoomboxDo( box, "stop" );
		return;
	case ACT_BOX_QUIETER:
		CG_BoomboxLouder( box, -1 );
		return;
	case ACT_BOX_LOUDER:
		CG_BoomboxLouder( box, 1 );
		return;
	case ACT_BOX_MUTE:
		CG_VoiceBoxMute( box, (qboolean)!info.muted );
		return;
	case ACT_BOX_MOVE:
		// (the piece the card was opened on: a speaker of a sound system moves by itself)
		CG_SelectCarry( clientNum - SELECT_BOX );
		return;
	case ACT_BOX_REMOVE:
		if ( !sel.confirmTime || cg.time - sel.confirmTime >= SELECT_CONFIRM_MSEC ) {
			sel.confirmTime = cg.time ? cg.time : 1;
			return;
		}
		CG_BoomboxDo( box, "remove" );
		break;
	case ACT_MUTE:
		trap->SendConsoleCommand( va( "%s %i\n", CG_VoiceMuted( clientNum ) ? "v_voice_unmute" : "v_voice_mute", clientNum ) );
		return;
	case ACT_QUIETER:
		CG_VoiceLouder( clientNum, -1 );
		return;
	case ACT_LOUDER:
		CG_VoiceLouder( clientNum, 1 );
		return;
	case ACT_HIDE_CHAT:
		CG_ChatHide( clientNum, (qboolean)!CG_ChatHidden( clientNum ) );
		return;
	case ACT_WHISPER:
		Select_Close();
		trap->SendConsoleCommand( va( "v_chatOpen 2 %i\n", clientNum ) );
		return;
	case ACT_WATCH:
		trap->SendClientCommand( va( "follow %i", clientNum ) );
		// (to watch him: a scoreboard that is kept would be in the way)
		CG_ScoresLetGo();
		break;
	case ACT_COPY:
		Q_strncpyz( name, cgs.clientinfo[clientNum].name, sizeof( name ) );
		Q_StripColor( name );
		trap->ext.SetClipboard( name );
		break;
	case ACT_VOTE_KICK:
		if ( !sel.confirmTime || cg.time - sel.confirmTime >= SELECT_CONFIRM_MSEC ) {
			sel.confirmTime = cg.time ? cg.time : 1;
			return;
		}
		trap->SendClientCommand( va( "callvote clientkick %i", clientNum ) );
		break;
	case ACT_ADMIN_KICK:
		trap->SendClientCommand( va( "amkick %i", clientNum ) );
		break;
	case ACT_ADMIN_SILENCE:
		trap->SendClientCommand( va( "amsilence %i", clientNum ) );
		break;
	case ACT_ADMIN_SLEEP:
		trap->SendClientCommand( va( "amsleep %i", clientNum ) );
		break;
	case ACT_ADMIN_SLAP:
		trap->SendClientCommand( va( "amslap %i", clientNum ) );
		break;
	case ACT_ADMIN_GOTO:
		trap->SendClientCommand( va( "amtele %i %i", cg.clientNum, clientNum ) );
		CG_ScoresLetGo();
		break;
	case ACT_ADMIN_BRING:
		trap->SendClientCommand( va( "amtele %i %i", clientNum, cg.clientNum ) );
		CG_ScoresLetGo();
		break;
	}
	Select_Close();
}

// the row of the card the pointer is on, -1: none
static int Select_RowAt( void ) {
	float	x, y;
	int		row;

	if ( sel.selected < 0 || !sel.cardPlaced || !Board_Pointer( &sel.shown, &x, &y )
		|| x < 0.0f || x >= SELECT_CARD_W || y < sel.headH ) {
		return -1;
	}
	row = (int)( ( y - sel.headH ) / SELECT_ROW_H );
	return ( row < sel.numRows ) ? row : -1;
}

// the card of the player that was clicked, on its board
static void Select_DrawCard( const selectBoard_t *b ) {
	const vec4_t		fill = { 0.035f, 0.07f, 0.12f, 0.88f }, edge = { 0.43f, 0.83f, 1.0f, 0.35f };
	const vec4_t		lit = { 0.43f, 0.83f, 1.0f, 0.15f };
	const int			n = sel.selected;
	const float			height = sel.headH + sel.numRows * SELECT_ROW_H + ( sel.numRows ? 5.0f : 0.0f );
	const int			over = Select_RowAt();
	int					i;

	if ( !b->world && Skin_Ready() ) {
		const vec4_t dark = { 0.0f, 0.0f, 0.0f, 0.4f };

		Skin_Soft( b->corner[0], b->corner[1] + 3.0f * b->unit, SELECT_CARD_W * b->unit, height * b->unit, 10.0f * b->unit, dark );
	}
	Board_Panel( b, 0.0f, 0.0f, SELECT_CARD_W, height, 6.0f, fill, edge );
	Board_TextFit( b, 8.0f, 5.0f, 0.78f, colorWhite, Select_Name( n ), SELECT_CARD_W - 16.0f );
	for ( i = 0; i < sel.numHead; i++ ) {
		const float	lineY = SELECT_HEAD_NAME + i * SELECT_HEAD_LINE;
		float		at = 8.0f;

		if ( sel.head[i].more[0] ) {
			// two parts: the first as far as it leaves room for the second, the second in what is left of the line
			const float first = Q_min( Board_Width( b, sel.head[i].text, 0.52f ),
				SELECT_CARD_W - 16.0f - 7.0f - Q_min( Board_Width( b, sel.head[i].more, 0.52f ), ( SELECT_CARD_W - 16.0f ) * 0.5f ) );

			Board_TextFit( b, at, lineY, 0.52f, sel.head[i].color, sel.head[i].text, first );
			at += first + 7.0f;
			Board_TextFit( b, at, lineY, 0.52f, sel.head[i].moreColor, sel.head[i].more, SELECT_CARD_W - 8.0f - at );
		} else {
			Board_TextFit( b, at, lineY, 0.52f, sel.head[i].color, sel.head[i].text, SELECT_CARD_W - 16.0f );
		}
	}
	if ( sel.numRows ) {
		Board_Panel( b, 6.0f, sel.headH - 2.0f, SELECT_CARD_W - 12.0f, 0.67f, 0.3f, edge, NULL );
	}

	for ( i = 0; i < sel.numRows; i++ ) {
		const float rowY = sel.headH + i * SELECT_ROW_H;

		if ( i == over ) {
			// lit, with a bar at its left
			Board_Panel( b, 3.0f, rowY, SELECT_CARD_W - 6.0f, SELECT_ROW_H, 3.0f, lit, NULL );
			Board_Panel( b, 3.0f, rowY + 2.0f, 1.5f, SELECT_ROW_H - 4.0f, 0.75f, selectAccent, NULL );
		}
		Board_Text( b, 8.0f, rowY + 1.5f, 0.5f, selectDim, va( "%i", ( i + 1 ) % 10 ) );
		Board_TextFit( b, 20.0f, rowY + 1.5f, SELECT_SCALE, ( i == over ) ? colorWhite : selectText, Select_Label( sel.rows[i], n ), SELECT_CARD_W - 26.0f );
	}
}

static void Select_DrawTag( const selectBoard_t *b, const char *name, float w ) {
	const vec4_t back = { 0.0f, 0.0f, 0.0f, 0.55f };

	Board_Panel( b, 0.0f, 0.0f, w, 15.0f, 4.0f, back, NULL );
	Board_Text( b, 5.0f, 2.0f, 0.62f, colorWhite, name );
}

/*
=================
Select_Tag

The name of the player under the pointer, beside him, once a frame. A thing in the level where the engine can.
The name of somebody far away would be too small to read: from where its letters would be lower than
SELECT_TAG_PX pixels it grows as much as he is further away, so it keeps that height. It is a plane beside him,
drawn over the scene (CG_SelectOverScene()), so no floor or wall beside him cuts it. For an engine that writes
nothing in the scene CG_SelectDraw() draws it with the HUD.
=================
*/
static void Select_Tag( void ) {
	const float	least = SELECT_TAG_PX / ( SELECT_LINE * 0.62f ) * SCREEN_HEIGHT / cgs.glconfig.vidHeight;
	const char	*name;
	float		perUnit;

	if ( sel.hover < 0 || sel.hover == sel.selected || !Select_Valid( sel.hover ) ) {
		sel.tagFor = -1;
		return;
	}
	name = Select_Name( sel.hover );
	Board_Begin( &sel.tag );
	// (least: what a unit of it has to be on the screen for letters of that height, in units of the HUD)
	if ( sel.tag.world && Select_Away( sel.hover, &perUnit ) ) {
		if ( SELECT_UNIT * perUnit < least ) {
			sel.tag.unit = least / perUnit;
		} else if ( SELECT_UNIT * perUnit > SELECT_NEAR_MAX ) {
			sel.tag.unit = SELECT_NEAR_MAX / perUnit;
		}
	}
	sel.tagWidth = Board_Width( &sel.tag, name, 0.62f ) + 10.0f;
	if ( Board_Place( &sel.tag, sel.hover, sel.tagWidth, 15.0f, (qboolean)( sel.tagFor != sel.hover ) ) ) {
		sel.tagFor = sel.hover;
	}
	if ( sel.tagFor != sel.hover ) {
		return;
	}
	sel.tagShown = sel.tag;
}

// What a unit of the card that has grown is on the screen, in units of the HUD: 1, the size it was made for. More
// in a window of so few pixels that its rows would then be lower than the rows of a card that shrinks back.
static float Select_FlatUnit( void ) {
	const float row = SELECT_LINE * SELECT_SCALE * cgs.screenYScale;

	return Q_max( 1.0f, SELECT_FLAT_MIN_PX / row );
}

// 0 to 1 over the time of a pop, a little past 1 on the way: what pops is too large for a moment
static float Select_Pop( int age ) {
	const float t = Com_Clamp( 0.0f, 1.0f, (float)age / SELECT_POP_MSEC ) - 1.0f;

	return 1.0f + 2.7f * t * t * t + 1.7f * t * t;
}

/*
=================
Select_Card

The card of the player that was clicked, once a frame: where it is, how large, and on what it is drawn
(sel.shown). It is a thing in the level beside him. Too far away to be read there, its rows lower than
SELECT_FLAT_PX pixels, it pops: it grows to the size it was made for, a little too large for a moment, and keeps
that size on the screen however far away he is. It shrinks back the same way when he is near again. Grown, it is
still a plane beside him, seen by the camera of the scene and moving with it (drawn flat on the screen it
trembled against the level with every move of the camera). At any size it is drawn over the scene and not in
it (CG_SelectOverScene()): in the scene the floor and the walls beside him cut it. For an engine that writes
nothing in the scene CG_SelectDraw() draws it with the HUD.
=================
*/
static void Select_Card( void ) {
	const qboolean	seen = Select_Valid( sel.selected );
	float			h, perUnit;

	Select_Rows( sel.selected );
	Select_Head( sel.selected );
	h = sel.headH + sel.numRows * SELECT_ROW_H + 5.0f;
	if ( sel.anchored ) {
		// clicked in the scoreboard: on the screen beside the board, as far to its right as the screen has room
		memset( &sel.card, 0, sizeof( sel.card ) );
		sel.card.unit = 1.0f;
		sel.card.corner[0] = Com_Clamp( 4.0f - cgs.bias2D, SCREEN_WIDTH + cgs.bias2D - SELECT_CARD_W - 4.0f, sel.anchorX );
		sel.card.corner[1] = Com_Clamp( 30.0f, SCREEN_HEIGHT - h - SELECT_FLOOR, sel.anchorY );
		sel.cardPlaced = qtrue;
		sel.flat = qfalse;
		sel.shown = sel.card;
		return;
	}
	Board_Begin( &sel.card );
	if ( sel.card.world && seen && Select_Away( sel.selected, &perUnit ) ) {
		// what a unit of the card is on the screen where it stands in the level, in units of the HUD (no more than
		// SELECT_NEAR_MAX), and a row of it in pixels
		const float	k = Q_min( SELECT_UNIT * perUnit, SELECT_NEAR_MAX );
		const float	row = SELECT_LINE * SELECT_SCALE * k * cgs.screenYScale;
		int			age;

		if ( !sel.flat && row < SELECT_FLAT_PX ) {
			sel.flat = qtrue;
			sel.flatTime = cg.time;
			sel.flatFrom = k;
		} else if ( sel.flat && row > SELECT_WORLD_PX ) {
			sel.flat = qfalse;
			sel.flatTime = cg.time;
			sel.flatFrom = sel.apparent;
		}
		age = cg.time - sel.flatTime;
		if ( !sel.flat && ( age < 0 || age >= SELECT_POP_MSEC ) ) {
			sel.apparent = k;
		} else {
			sel.apparent = Q_max( 0.02f, sel.flatFrom + ( ( sel.flat ? Select_FlatUnit() : k ) - sel.flatFrom ) * Select_Pop( age ) );
		}
		sel.cardUnit = sel.apparent / perUnit;
	}
	if ( sel.card.world && sel.cardUnit > 0.0f ) {
		sel.card.unit = sel.cardUnit;
	}
	// beside the player while he is in front of the camera, where it was otherwise
	if ( seen && Board_Place( &sel.card, sel.selected, SELECT_CARD_W, h, (qboolean)!sel.cardPlaced ) ) {
		sel.cardPlaced = qtrue;
	}
	sel.shown = sel.card;
}

/*
=================
CG_SelectScene

Once a frame while the scene is put together, before it is drawn
=================
*/
void CG_SelectScene( void ) {
	if ( Select_Free() && Select_World() ) {
		Select_Tag();
		if ( sel.selected >= 0 && !sel.anchored ) {
			Select_Card();
		}
	}
}

/*
=================
CG_SelectOverScene

Right after the scene was drawn: the name and the card, in a scene of their own with the same camera and
nothing else in it. They are planes beside their player in the level, and nothing of the level, no figure and
no light that is added to the picture afterwards lies on them.
=================
*/
void CG_SelectOverScene( void ) {
	const qboolean	tag = (qboolean)( sel.hover >= 0 && sel.hover != sel.selected && sel.tagFor == sel.hover && sel.tagShown.world );
	const qboolean	card = (qboolean)( sel.selected >= 0 && !sel.anchored && sel.cardPlaced && sel.shown.world );
	refdef_t		refdef;

	if ( ( !tag && !card ) || !Select_Free() || !Select_World() ) {
		return;
	}
	trap->R_ClearScene();
	if ( tag ) {
		Select_DrawTag( &sel.tagShown, Select_Name( sel.hover ), sel.tagWidth );
	}
	if ( card ) {
		Select_DrawCard( &sel.shown );
	}
	refdef = cg.refdef;
	refdef.rdflags = RDF_NOWORLDMODEL;
	trap->R_RenderScene( &refdef );
}

// four corners around a player, for an engine and a server that leave no shader for the outline
static void Select_DrawCorners( int clientNum ) {
	const vec4_t	color = { 0.43f, 0.83f, 1.0f, 0.9f };
	vec3_t			top, bottom;
	float			x0, y0, x1, y1, half, scale;

	// from the middle of a player, from the foot of a box
	Select_Middle( clientNum, top, &scale );
	VectorCopy( top, bottom );
	top[2] += ( clientNum >= SELECT_BOX ) ? CG_BoomboxHeight( clientNum - SELECT_BOX ) + 2.0f : 44.0f;
	bottom[2] -= ( clientNum >= SELECT_BOX ) ? 2.0f : 26.0f;
	if ( !CG_WorldCoordToScreenCoordFloat( top, &x0, &y0 ) || !CG_WorldCoordToScreenCoordFloat( bottom, &x1, &y1 ) || y1 <= y0 ) {
		return;
	}
	half = ( y1 - y0 ) * 0.28f;
	x0 = ( x0 + x1 ) * 0.5f - half;
	x1 = x0 + 2.0f * half;
	CG_FillRect( x0, y0, 6.0f, 1.0f, (float *)color );		CG_FillRect( x0, y0, 1.0f, 6.0f, (float *)color );
	CG_FillRect( x1 - 6.0f, y0, 6.0f, 1.0f, (float *)color );	CG_FillRect( x1 - 1.0f, y0, 1.0f, 6.0f, (float *)color );
	CG_FillRect( x0, y1 - 1.0f, 6.0f, 1.0f, (float *)color );	CG_FillRect( x0, y1 - 6.0f, 1.0f, 6.0f, (float *)color );
	CG_FillRect( x1 - 6.0f, y1 - 1.0f, 6.0f, 1.0f, (float *)color );	CG_FillRect( x1 - 1.0f, y1 - 6.0f, 1.0f, 6.0f, (float *)color );
}

/*
===============================================================================

Once a frame, keys, commands

===============================================================================
*/

/*
=================
CG_SelectDraw

Once a frame, in the 2D of the HUD: asks for the mouse or gives it back, finds the player under the pointer,
and draws his name, the card and the pointer
=================
*/
void CG_SelectDraw( void ) {
	const qboolean free = Select_Free();

	// the keys and the mouse, while the mouse is free. The chat box asks for them itself while it is open.
	if ( free && !CG_ChatIsOpen() && !sel.catching ) {
		sel.catching = qtrue;
		cgs.cursorX = SCREEN_WIDTH / 2;
		cgs.cursorY = SCREEN_HEIGHT / 2;
		trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
	} else if ( sel.catching && ( !free || CG_ChatIsOpen() ) ) {
		sel.catching = qfalse;
		if ( !CG_ChatIsOpen() ) {
			trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
		}
	}
	// somebody else took the keys away (a menu, the console)
	if ( sel.catching && !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		sel.catching = sel.toggled = qfalse;
		CG_ScoresLetGo();
		CG_BoomboxWindowClose();
		CG_ToolWindowClose();
		CG_RadialClose();
		CG_CmdMenuClose();
	}
	// Who points does not type. An engine of this tree leaves the talk button out of his commands while this
	// says so (CL_CmdButtons()): no dots over his head for anybody, and the server does not hold him still. In
	// another engine every catcher of keys is talking, as it always was.
	if ( sel.quiet != sel.catching ) {
		sel.quiet = sel.catching;
		trap->Cvar_Set( "v_catchQuiet", sel.quiet ? "1" : "0" );
	}
	if ( !free ) {
		sel.hover = -1;
		Select_Close();
		Select_LetBoxGo( qfalse );
		CG_BoomboxWindowClose();
		// (the chat box open with the selection of players switched off: its pointer)
		if ( CG_ChatIsOpen() ) {
			CG_Place2D( PLACE_CENTER );
			CG_SelectDrawCursor();
		}
		return;
	}
	// the quick wheel takes the place of the pointer while it is open, and so does the command menu
	if ( CG_RadialOpen() ) {
		sel.hover = -1;
		CG_Place2D( PLACE_CENTER );
		CG_RadialDraw();
		return;
	}
	if ( CG_CmdMenuOpen() ) {
		sel.hover = -1;
		CG_Place2D( PLACE_CENTER );
		CG_CmdMenuDraw();
		return;
	}
	// he left, the box is gone, or the board his card stands beside is gone
	if ( sel.selected >= 0 && ( !Select_Exists( sel.selected ) || ( sel.anchored && !CG_ScoresShown() ) ) ) {
		Select_Close();
	}

	CG_Place2D( PLACE_CENTER );
	// a box that was pressed on and the pointer moved: it is dragged from now on
	if ( sel.press >= 0 && !sel.dragging
		&& ( fabsf( cgs.cursorX - sel.pressX ) > SELECT_DRAG || fabsf( cgs.cursorY - sel.pressY ) > SELECT_DRAG ) ) {
		sel.dragging = qtrue;
		if ( sel.selected == sel.press ) {
			Select_Close();
		}
		CG_BoomboxGrab( sel.press - SELECT_BOX );
	}
	// the box in the pointer goes over the floor under it
	if ( Select_Held() >= 0 ) {
		vec3_t dir;

		Select_Ray( dir );
		CG_BoomboxCarry( cg.refdef.vieworg, dir );
	} else if ( sel.dragging || sel.carrying ) {
		// (it was taken away meanwhile)
		Select_LetBoxGo( qfalse );
	}
	// (nobody is picked in the level through the card, the scoreboard or the window of a box, or while a box is carried)
	sel.hover = ( Select_RowAt() >= 0 || CG_ScoresPointerOver() || CG_BoomboxWindowPointerOver() || CG_ToolWindowPointerOver()
		|| Select_Held() >= 0 ) ? -1 : Select_Pick();
	if ( sel.hover >= 0 && sel.hover != sel.selected ) {
		Select_Hull();
		if ( !sel.hull[0] || !v_selectOutline.integer ) {
			Select_DrawCorners( sel.hover );
		}
	}
	if ( !Select_World() ) {
		Select_Tag();
	}
	if ( sel.selected >= 0 && ( sel.anchored || !Select_World() ) ) {
		Select_Card();
	}
	// the name and the card on the screen: an engine that writes nothing in the scene, and the card of a player
	// that was clicked in the scoreboard
	if ( sel.hover >= 0 && sel.hover != sel.selected && sel.tagFor == sel.hover && !sel.tagShown.world ) {
		selectBoard_t at = sel.tagShown;

		Select_Pixels( &at.corner[0], &at.corner[1] );
		Select_DrawTag( &at, Select_Name( sel.hover ), sel.tagWidth );
	}
	if ( sel.selected >= 0 && sel.cardPlaced && !sel.shown.world ) {
		selectBoard_t at = sel.shown;

		Select_Pixels( &at.corner[0], &at.corner[1] );
		Select_DrawCard( &at );
	}
	// the window of a box and the tool window, and the pointer on everything, on the card and the windows as well
	CG_BoomboxWindowDraw();
	CG_ToolWindowDraw();
	CG_SelectDrawCursor();
}

/*
=================
CG_SelectKeyEvent

A key or a button of the mouse while the mouse is free. qtrue: it was for this file.
=================
*/
qboolean CG_SelectKeyEvent( int key, qboolean down ) {
	if ( !Select_Free() ) {
		return qfalse;
	}
	if ( CG_RadialKey( key, down ) || CG_CmdMenuKey( key, down ) ) {
		return qtrue;
	}
	// a box that was pressed on: dragged, it is put down where the button is let go, else its card opens
	if ( !down && key == A_MOUSE1 && sel.press >= 0 ) {
		if ( sel.dragging ) {
			Select_LetBoxGo( qtrue );
		} else {
			if ( Select_Valid( sel.press ) ) {
				Select_Open( sel.press );
			}
			sel.press = -1;
		}
		return qtrue;
	}
	// the window of a box, while it is open: what is on it, and Escape and the right button for it
	if ( CG_BoomboxWindowKey( key, down ) || CG_ToolWindowKey( key, down ) ) {
		return qtrue;
	}
	if ( !down ) {
		return (qboolean)!CG_ChatIsOpen();
	}
	// a box in the pointer: the left button puts it down, the right one or Escape puts it back, the wheel turns it
	if ( Select_Held() >= 0 ) {
		if ( key == A_MWHEELUP || key == A_MWHEELDOWN ) {
			CG_BoomboxTurnHeld( ( key == A_MWHEELUP ) ? 1 : -1 );
		} else if ( key == A_MOUSE1 ) {
			Select_LetBoxGo( qtrue );
		} else if ( key == A_MOUSE2 || key == A_ESCAPE ) {
			Select_LetBoxGo( qfalse );
		}
		return qtrue;
	}
	if ( key == A_MOUSE1 ) {
		const int row = Select_RowAt();

		if ( row >= 0 ) {
			Select_Run( sel.rows[row], sel.selected );
			return qtrue;
		}
		// a row of the scoreboard: the card of that player beside the board. The board beside its rows: nothing.
		if ( CG_ScoresPointerOver() ) {
			float		x, y;
			const int	n = CG_ScoresClientAt( &x, &y );

			Select_Close();
			if ( n >= 0 ) {
				sel.selected = n;
				sel.anchored = qtrue;
				sel.anchorX = x;
				sel.anchorY = y;
			}
			return qtrue;
		}
		// a box: its card opens when the button is let go there, it is dragged if the pointer moves first
		if ( sel.hover >= SELECT_BOX ) {
			sel.press = sel.hover;
			sel.pressX = cgs.cursorX;
			sel.pressY = cgs.cursorY;
			sel.dragging = qfalse;
			return qtrue;
		}
		if ( sel.hover >= 0 ) {
			Select_Open( sel.hover );
			return qtrue;
		}
		if ( sel.selected >= 0 ) {
			Select_Close();
			return qtrue;
		}
		return (qboolean)!CG_ChatIsOpen();
	}
	if ( sel.selected >= 0 ) {
		if ( key == A_MOUSE2 || key == A_ESCAPE ) {
			Select_Close();
			return qtrue;
		}
		// the rows by their number: 1 to 9, then 0
		if ( key >= A_0 && key <= A_9 && !CG_ChatIsOpen() ) {
			const int row = ( key == A_0 ) ? 9 : key - A_1;

			if ( row < sel.numRows ) {
				Select_Run( sel.rows[row], sel.selected );
			}
			return qtrue;
		}
	}
	if ( CG_ChatIsOpen() ) {
		return qfalse;
	}
	// the scoreboard that is kept open goes with its key, Escape or the right button
	if ( CG_ScoresKeyEvent( key ) ) {
		return qtrue;
	}
	if ( key == A_ESCAPE || key == A_MOUSE2 ) {
		sel.toggled = qfalse;
	}
	// every other key is swallowed while the mouse is free: the game does not get it
	return qtrue;
}

/*
=================
CG_SelectLetGo

The level restarts: a pointer that was switched on stays with nobody (the card is closed, "cursor" is off, the
scoreboard is no longer kept). A key that is held for it (+v_cursor) is still held. The keys go back to the game
with the next frame.
=================
*/
void CG_SelectLetGo( void ) {
	CG_RadialClose();
	CG_CmdMenuClose();
	sel.toggled = qfalse;
	sel.hover = -1;
	Select_Close();
	Select_LetBoxGo( qfalse );
	CG_ScoresLetGo();
	CG_BoomboxWindowClose();
	CG_ToolWindowClose();
}

// "Move it" of the card or the window of a box: the piece hangs in the pointer until the next click puts it down
void CG_SelectCarry( int piece ) {
	Select_Close();
	CG_BoomboxGrab( piece );
	sel.carrying = (qboolean)( CG_BoomboxHeld() == piece );
}

void CG_SelectCursorDown_f( void ) {
	sel.held = qtrue;
	selectHeldTime = trap->Milliseconds();
}

// the piece of a box under the pointer while the mouse is free, -1: none (for the quick wheel)
int CG_SelectHoveredPiece( void ) {
	return ( Select_Free() && sel.hover >= SELECT_BOX ) ? sel.hover - SELECT_BOX : -1;
}

void CG_SelectCursorUp_f( void ) {
	// When the pointer takes the keys (Key_SetCatcher) the engine lets go of every key that is held, this one too,
	// and sends "-v_cursor <key> 0": without a time. That one is not the player letting go (taken for one, the
	// pointer let the keys go again, the key repeat took them back, and the mouse did nothing). A key let go
	// without a time later on is the window losing the focus.
	if ( !atoi( CG_Argv( 2 ) ) && trap->Milliseconds() - selectHeldTime < SELECT_GRAB_MSEC ) {
		return;
	}
	sel.held = qfalse;
}

/*
=================
CG_SelectCursor_f

cursor: the mouse is free, or the game has it back. cursor at <x> <y>: the pointer goes there, in the 640 x 480
of the HUD. cursor on <player>: onto that player. cursor box <n>: onto that box of the boombox. cursor click: a
click of the left button where the pointer is, cursor release: the button let go there (the end of a drag).
cursor row <n>: that row of the card that is open. All but the first are for a key or a script in place of a
mouse.
=================
*/
void CG_SelectCursor_f( void ) {
	const char *what = CG_Argv( 1 );

	if ( !Q_stricmp( what, "at" ) ) {
		cgs.cursorX = Com_Clampi( -(int)cgs.bias2D, SCREEN_WIDTH + (int)cgs.bias2D, atoi( CG_Argv( 2 ) ) );
		cgs.cursorY = Com_Clampi( 0, SCREEN_HEIGHT, atoi( CG_Argv( 3 ) ) );
	} else if ( !Q_stricmp( what, "on" ) ) {
		// onto a player, by his name or number, if he is on the screen
		const int	n = ( CG_Argv( 2 )[0] >= '0' && CG_Argv( 2 )[0] <= '9' ) ? atoi( CG_Argv( 2 ) ) : CG_ClientNumFromName( CG_Argv( 2 ) );
		vec3_t		chest;
		float		x, y;

		if ( n < 0 || n >= MAX_CLIENTS || !cg.snap || !cgs.clientinfo[n].infoValid ) {
			trap->Print( "cursor on: no such player\n" );
		} else if ( !Select_Valid( n ) ) {
			trap->Print( "cursor on: %s^7 can not be pointed at (%s)\n", cgs.clientinfo[n].name, n == cg.snap->ps.clientNum ? "that is you"
				: !cg_entities[n].currentValid ? "the server does not show him here"
				: ( cg_entities[n].currentState.eFlags & EF_DEAD ) ? "he is dead" : "he can not be seen" );
		} else {
			VectorCopy( cg_entities[n].lerpOrigin, chest );
			chest[2] += 8.0f;
			if ( CG_WorldCoordToScreenCoordFloat( chest, &x, &y ) ) {
				cgs.cursorX = (int)x;
				cgs.cursorY = (int)y;
			} else {
				trap->Print( "cursor on: %s^7 is not on the screen\n", cgs.clientinfo[n].name );
			}
		}
	} else if ( !Q_stricmp( what, "box" ) ) {
		vec3_t	middle;
		float	x, y;

		if ( !CG_BoomboxShown( atoi( CG_Argv( 2 ) ) ) ) {
			trap->Print( "cursor box: no such boombox\n" );
			return;
		}
		CG_BoomboxMiddle( atoi( CG_Argv( 2 ) ), middle );
		middle[2] += CG_BoomboxHeight( atoi( CG_Argv( 2 ) ) ) * 0.5f;
		if ( CG_WorldCoordToScreenCoordFloat( middle, &x, &y ) ) {
			cgs.cursorX = (int)x;
			cgs.cursorY = (int)y;
		} else {
			trap->Print( "cursor box: it is not on the screen\n" );
		}
	} else if ( !Q_stricmp( what, "click" ) ) {
		CG_SelectKeyEvent( A_MOUSE1, qtrue );
	} else if ( !Q_stricmp( what, "release" ) ) {
		CG_SelectKeyEvent( A_MOUSE1, qfalse );
	} else if ( !Q_stricmp( what, "over" ) ) {
		// the pointer onto the middle of a row of the card that is open, wherever the card is (for scripts)
		const int row = atoi( CG_Argv( 2 ) ) - 1;

		float x, y;

		if ( sel.selected >= 0 && row >= 0 && row < sel.numRows
			&& Board_Screen( &sel.shown, SELECT_CARD_W * 0.5f, sel.headH + ( row + 0.5f ) * SELECT_ROW_H, &x, &y ) ) {
			cgs.cursorX = (int)x;
			cgs.cursorY = (int)y;
		}
	} else if ( !Q_stricmp( what, "row" ) ) {
		// a row of the card that is open, by its number
		const int row = atoi( CG_Argv( 2 ) ) - 1;

		if ( sel.selected >= 0 && row >= 0 && row < sel.numRows ) {
			Select_Run( sel.rows[row], sel.selected );
		}
	} else {
		sel.toggled = (qboolean)!sel.toggled;
	}
}
