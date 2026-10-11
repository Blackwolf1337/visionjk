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
// While the mouse is free (the key of +cursor is held, "cursor" switched it on, or the chat box is open) the
// player under the pointer has a glowing outline, and a click on him opens a card beside him: who he is, and
// what this player can do about him (his voice, his chat lines, a whisper, a vote, what an admin may).
//
// All of it happens in this client. Nothing is sent that the commands it uses would not send when typed, and
// no other client sees any of it.
//
// The outline is the model of the player drawn a second time with a shader that only shows a rim around it. An
// engine of this tree makes that shader from a flag (RGBA_SHADER_HULL), so it is there on a pure server
// without the pack. Elsewhere the same shader comes from the pack (gfx/vision/select_hull), and where that is
// missing too the player under the pointer gets four corners in the 2D.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );		// cg_draw.c

#define SELECT_RANGE		4096.0f		// how far away a player can still be picked
#define SELECT_GROW			6.0f		// the box of a player is this much larger for the pointer
#define SELECT_CARD_W		150.0f
#define SELECT_ROW_H		13.0f
#define SELECT_HEAD_H		44.0f
#define SELECT_MAX_ROWS		14
#define SELECT_SCALE		0.6f		// of the text of the card
#define SELECT_CONFIRM_MSEC	4000		// a vote to kick has to be clicked twice within this time

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
	ACT_ADMIN_BRING
} selectAct_t;

#define SELECT_BESIDE		13.0f	// how far beside the middle of a player his card and his name stand, in units of the level,
#define SELECT_ABOVE		36.0f	//	how far above his middle their top edge is,
#define SELECT_GAP			5.0f	//	and how far from that place they begin, in units of the card
#define SELECT_UNIT			0.3f	// What a unit of the card is in the level, where the card is a thing there: the name on it
									// has letters as tall as those of a chat bubble.
#define SELECT_LINE			18.0f	// a line of text of the HUD is this high at a scale of 1, in its units,
#define SELECT_BASE			0.857f	//	and its letters stand on this part of it
#define SELECT_NUDGE		40.0f	// how far a card is lifted or lowered at most to stay on the screen, in units of the level
#define SELECT_FLAT_PX		15.0f	// A card whose rows are lower than this on the screen, in pixels, is hard to read and to
#define SELECT_WORLD_PX		17.5f	//	hit: it grows to the size it was made for, and shrinks back when they are higher than this.
#define SELECT_FLAT_MIN_PX	18.5f	// Grown, its rows are at least this high, so growing never makes it smaller.
#define SELECT_POP_MSEC		180		// how long it takes to grow and to shrink
#define SELECT_TAG_PX		14.0f	// the letters of a name are never lower than this on the screen, in pixels
#define SELECT_FOLLOW_MSEC	90.0f	// how late they follow him: a step of his is no jump of theirs
#define SELECT_SHELLS	6		// the shells of the outline
#define SELECT_BLUR_TENTHS	12	// how far the hull that the dynamic glow blurs stands off the model, in tenths of a unit,
#define SELECT_BLUR_ALPHA	165	//	and how bright it is, of 255

// What the name and the card of a player are drawn on. In the level it is a thing there as a chat bubble is:
// as large at any distance, upright, turned to the one who looks about its upright axis only.
typedef struct selectBoard_s {
	qboolean	world;					// in the level. qfalse: on the screen
	vec3_t		corner;					// its top left corner: a place in the level, or x and y in the units of the HUD
	float		unit;					// what a unit of the card is on it: units of the level, or of the HUD
	vec3_t		right, up, toward;		// in the level: along its lines, up them, and to the one who looks
} selectBoard_t;

static struct {
	qboolean	held;					// +cursor
	qboolean	toggled;				// "cursor"
	qboolean	catching;				// this file asked for the keys and the mouse
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
	qboolean	over;					// it is drawn over the scene and not in it (CG_SelectOverScene())
	selectBoard_t	shown;				// what the card was drawn on last: the pointer is looked for on that
	selectBoard_t	tag;
	int			tagFor;					// the player the name has its place for, -1: none
	selectBoard_t	tagShown;			// what the name is drawn on this frame, and how wide it is in units of the card
	float		tagWidth;
	qboolean	tagOver;				// it has grown and is drawn over the scene
	selectAct_t	rows[SELECT_MAX_ROWS];
	int			numRows;
	int			confirmTime;			// when the vote to kick was clicked first, 0: not yet
	qhandle_t	hull[SELECT_SHELLS];	// the shaders of the outline, from the model outward. The first 0: none
	qhandle_t	blurHull;				// for the dynamic glow of the renderer: a hull that only its buffer gets, and blurs
	qboolean	hullTried;
	qhandle_t	pointer;				// the pointer of the game, where the new one can not be drawn
} sel;

void CG_SelectInit( void ) {
	memset( &sel, 0, sizeof( sel ) );
	sel.hover = sel.selected = sel.tagFor = -1;
}

// is the mouse free for pointing at players
static qboolean Select_Free( void ) {
	return (qboolean)( cg_selectPlayers.integer && cg.snap && !cg.demoPlayback && ( sel.held || sel.toggled || CG_ChatIsOpen() ) );
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
	if ( best < 0 ) {
		return -1;
	}
	// nothing of the level between the eye and where the ray meets his box
	VectorMA( cg.refdef.vieworg, bestT, dir, end );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, cg.snap->ps.clientNum, MASK_SOLID );
	if ( tr.fraction < 1.0f && tr.entityNum != best ) {
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
	// the flag came with the engine that says cl_uiSkin
	trap->Cvar_VariableStringBuffer( "cl_uiSkin", value, sizeof( value ) );
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
CG_SelectOutlined

Does this player get an outline this frame: the one under the pointer and the one whose card is open. His model
and the blades of his saber are then drawn after everything else (RF_FORCEPOST), so they lie on the shells of
the outline, which are drawn before them and write no depth: what is left of the shells is what stands out
around the figure, and no line of them crosses it.
=================
*/
qboolean CG_SelectOutlined( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cg_selectOutline.integer
		|| ( clientNum != sel.hover && clientNum != sel.selected ) || !Select_Free() ) {
		return qfalse;
	}
	Select_Hull();
	return (qboolean)( sel.hull[0] != 0 );
}

/*
=================
CG_SelectOutline

Called while a player is added to the scene, with his model: the one under the pointer and the one whose card is
open are added a second time as an outline.
=================
*/
void CG_SelectOutline( int clientNum, const refEntity_t *model ) {
	refEntity_t			hull;
	const clientInfo_t	*ci;
	const float			strength = ( clientNum == sel.selected ) ? 1.0f : 0.75f;
	char				value[8];
	qboolean			blur;
	int					i;

	if ( !CG_SelectOutlined( clientNum ) ) {
		return;
	}
	ci = &cgs.clientinfo[clientNum];
	hull = *model;
	hull.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA | RF_NODEPTH | RF_FORCEPOST | RF_AFTERGLOW );
	hull.renderfx |= RF_NOSHADOW;
	// light blue, or the colour of his team
	if ( cgs.gametype >= GT_TEAM && ci->team == TEAM_RED ) {
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

/*
===============================================================================

The card

===============================================================================
*/

static void Select_Close( void ) {
	sel.selected = -1;
	sel.confirmTime = 0;
	sel.cardPlaced = sel.flat = sel.over = qfalse;
	sel.flatTime = 0;
	sel.cardUnit = sel.apparent = 0.0f;
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
	const centity_t	*cent = &cg_entities[clientNum];
	const float		scale = ( cent->modelScale[2] > 0.01f ) ? cent->modelScale[2] : 1.0f;
	const float		least = 4.0f - cgs.bias2D, most = SCREEN_WIDTH + cgs.bias2D - w - 4.0f;
	vec3_t			at;
	float			sx, sy, other;

	VectorCopy( cent->lerpOrigin, at );
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
	*y = Com_Clamp( 30.0f, SCREEN_HEIGHT - h - 44.0f, sy );
	return qtrue;
}

/*
=================
Select_BesideWorld

The same for a board that is a thing in the level, w and h in units of the card, of which b->unit says how large
one is there: the place of its top left corner. At the right of the player as the camera sees him, at his left where the screen ends at his right, and
lifted or lowered a little where it would reach over the top or the bottom of the screen: the view can not be
turned to it while the mouse is free. qfalse: it is not in front of the camera.
=================
*/
static qboolean Select_BesideWorld( int clientNum, float w, float h, const selectBoard_t *b, vec3_t corner ) {
	const centity_t	*cent = &cg_entities[clientNum];
	const float		scale = ( cent->modelScale[2] > 0.01f ) ? cent->modelScale[2] : 1.0f;
	const float		beside = SELECT_BESIDE * scale + SELECT_GAP * b->unit;
	vec3_t			base, far;
	float			sx, sy, z, perUnit, over;

	VectorCopy( cent->lerpOrigin, base );
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
	over = sy + h * b->unit * perUnit - ( SCREEN_HEIGHT - 44.0f );
	// (a board that has grown may be moved further: it is that much larger there)
	if ( over > 0.0f ) {
		corner[2] += Q_min( SELECT_NUDGE * b->unit / SELECT_UNIT, over / perUnit );
	} else if ( sy < 30.0f ) {
		corner[2] -= Q_min( SELECT_NUDGE * b->unit / SELECT_UNIT, ( 30.0f - sy ) / perUnit );
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

// what a board is drawn on this frame: where the engine can in the level, upright and turned to the camera
static void Board_Begin( selectBoard_t *b ) {
	b->world = Select_World();
	b->unit = b->world ? SELECT_UNIT : 1.0f;
	if ( !b->world ) {
		return;
	}
	VectorSet( b->up, 0.0f, 0.0f, 1.0f );
	VectorNegate( cg.refdef.viewaxis[1], b->right );
	b->right[2] = 0.0f;
	if ( VectorNormalize( b->right ) < 0.01f ) {
		VectorSet( b->right, 0.0f, -1.0f, 0.0f );
	}
	CrossProduct( b->right, b->up, b->toward );
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
	vec3_t	to;
	float	z;

	VectorSubtract( cg_entities[clientNum].lerpOrigin, cg.refdef.vieworg, to );
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

// what the card of a player offers, in this order
static void Select_Rows( int clientNum ) {
	char value[8];

	sel.numRows = 0;
	sel.rows[sel.numRows++] = ACT_MUTE;
	sel.rows[sel.numRows++] = ACT_QUIETER;
	sel.rows[sel.numRows++] = ACT_LOUDER;
	sel.rows[sel.numRows++] = ACT_HIDE_CHAT;
	trap->Cvar_VariableStringBuffer( "cl_cgameChat", value, sizeof( value ) );
	if ( atoi( value ) == 1 ) {
		if ( cg_chatInput.integer ) {
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
	switch ( act ) {
	case ACT_MUTE:			return CG_VoiceMuted( clientNum ) ? "Hear his voice again" : "Mute his voice";
	case ACT_QUIETER:		return va( "Voice quieter (%i %%)", CG_VoiceLouder( clientNum, 0 ) );
	case ACT_LOUDER:		return va( "Voice louder (%i %%)", CG_VoiceLouder( clientNum, 0 ) );
	case ACT_HIDE_CHAT:		return CG_ChatHidden( clientNum ) ? "Show his chat lines again" : "Hide his chat lines";
	case ACT_WHISPER:		return "Whisper to him";
	case ACT_WATCH:			return "Watch him";
	case ACT_COPY:			return "Copy his name";
	case ACT_VOTE_KICK:		return ( sel.confirmTime && cg.time - sel.confirmTime < SELECT_CONFIRM_MSEC ) ? "Click again to call the vote" : "Call a vote to kick him";
	case ACT_ADMIN_KICK:	return "Admin: kick";
	case ACT_ADMIN_SILENCE:	return "Admin: silence";
	case ACT_ADMIN_SLEEP:	return "Admin: sleep";
	case ACT_ADMIN_SLAP:	return "Admin: slap";
	case ACT_ADMIN_GOTO:	return "Admin: go to him";
	case ACT_ADMIN_BRING:	return "Admin: bring him here";
	}
	return "";
}

// One row of the card was chosen. What it does is what the command it runs does when it is typed: the server
// decides as always who may.
static void Select_Run( selectAct_t act, int clientNum ) {
	char name[MAX_NETNAME];

	switch ( act ) {
	case ACT_MUTE:
		trap->SendConsoleCommand( va( "%s %i\n", CG_VoiceMuted( clientNum ) ? "voice_unmute" : "voice_mute", clientNum ) );
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
		trap->SendConsoleCommand( va( "cg_chatopen 2 %i\n", clientNum ) );
		return;
	case ACT_WATCH:
		trap->SendClientCommand( va( "follow %i", clientNum ) );
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
		break;
	case ACT_ADMIN_BRING:
		trap->SendClientCommand( va( "amtele %i %i", clientNum, cg.clientNum ) );
		break;
	}
	Select_Close();
}

// the row of the card the pointer is on, -1: none
static int Select_RowAt( void ) {
	float	x, y;
	int		row;

	if ( sel.selected < 0 || !sel.cardPlaced || !Board_Pointer( &sel.shown, &x, &y )
		|| x < 0.0f || x >= SELECT_CARD_W || y < SELECT_HEAD_H ) {
		return -1;
	}
	row = (int)( ( y - SELECT_HEAD_H ) / SELECT_ROW_H );
	return ( row < sel.numRows ) ? row : -1;
}

// the card of the player that was clicked, on its board
static void Select_DrawCard( const selectBoard_t *b ) {
	const vec4_t		fill = { 0.035f, 0.07f, 0.12f, 0.88f }, edge = { 0.43f, 0.83f, 1.0f, 0.35f };
	const vec4_t		text = { 0.86f, 0.92f, 0.97f, 1.0f }, dim = { 0.60f, 0.69f, 0.78f, 1.0f }, accent = { 0.43f, 0.83f, 1.0f, 1.0f };
	const vec4_t		lit = { 0.43f, 0.83f, 1.0f, 0.15f };
	const int			n = sel.selected;
	const clientInfo_t	*ci = &cgs.clientinfo[n];
	const float			height = SELECT_HEAD_H + sel.numRows * SELECT_ROW_H + 5.0f;
	const int			over = Select_RowAt();
	char				line[128];
	const char			*kind;
	int					i;

	if ( !b->world && Skin_Ready() ) {
		const vec4_t dark = { 0.0f, 0.0f, 0.0f, 0.4f };

		Skin_Soft( b->corner[0], b->corner[1] + 3.0f * b->unit, SELECT_CARD_W * b->unit, height * b->unit, 10.0f * b->unit, dark );
	}
	Board_Panel( b, 0.0f, 0.0f, SELECT_CARD_W, height, 6.0f, fill, edge );
	Board_Text( b, 8.0f, 5.0f, 0.78f, colorWhite, ci->name );
	// what he is, and his score and ping if the scoreboard has a line of him
	kind = ( ci->team == TEAM_RED ) ? "Red" : ( ci->team == TEAM_BLUE ) ? "Blue" : ( ci->team == TEAM_SPECTATOR ) ? "Spectator"
		: ( ci->botSkill != -1 ) ? "Bot" : "Player";
	Q_strncpyz( line, kind, sizeof( line ) );
	for ( i = 0; i < cg.numScores; i++ ) {
		if ( cg.scores[i].client == n ) {
			Com_sprintf( line, sizeof( line ), "%s   score %i   ping %i", kind, cg.scores[i].score, cg.scores[i].ping );
			break;
		}
	}
	Board_Text( b, 8.0f, 20.0f, 0.52f, dim, line );
	if ( CG_StatusLine( n, line, sizeof( line ) ) ) {
		Board_Text( b, 8.0f, 30.0f, 0.52f, accent, line );
	}
	Board_Panel( b, 6.0f, SELECT_HEAD_H - 3.0f, SELECT_CARD_W - 12.0f, 0.67f, 0.3f, edge, NULL );

	for ( i = 0; i < sel.numRows; i++ ) {
		const float rowY = SELECT_HEAD_H + i * SELECT_ROW_H;

		if ( i == over ) {
			// lit, with a bar at its left
			Board_Panel( b, 3.0f, rowY, SELECT_CARD_W - 6.0f, SELECT_ROW_H, 3.0f, lit, NULL );
			Board_Panel( b, 3.0f, rowY + 2.0f, 1.5f, SELECT_ROW_H - 4.0f, 0.75f, accent, NULL );
		}
		Board_Text( b, 8.0f, rowY + 1.5f, 0.5f, dim, va( "%i", ( i + 1 ) % 10 ) );
		Board_Text( b, 20.0f, rowY + 1.5f, SELECT_SCALE, ( i == over ) ? colorWhite : text, Select_Label( sel.rows[i], n ) );
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
SELECT_TAG_PX pixels it grows as much as he is further away, so it keeps that height. It stays a plane beside
him, drawn over the scene then (CG_SelectOverScene()). For an engine that writes nothing in the scene
CG_SelectDraw() draws it with the HUD.
=================
*/
static void Select_Tag( void ) {
	const float	least = SELECT_TAG_PX / ( SELECT_LINE * 0.62f ) * SCREEN_HEIGHT / cgs.glconfig.vidHeight;
	const char	*name;
	float		perUnit;

	sel.tagOver = qfalse;
	if ( sel.hover < 0 || sel.hover == sel.selected || !Select_Valid( sel.hover ) ) {
		sel.tagFor = -1;
		return;
	}
	name = cgs.clientinfo[sel.hover].name;
	Board_Begin( &sel.tag );
	// (least: what a unit of it has to be on the screen for letters of that height, in units of the HUD)
	if ( sel.tag.world && Select_Away( sel.hover, &perUnit ) && SELECT_UNIT * perUnit < least ) {
		sel.tag.unit = least / perUnit;
		sel.tagOver = qtrue;
	}
	sel.tagWidth = Board_Width( &sel.tag, name, 0.62f ) + 10.0f;
	if ( Board_Place( &sel.tag, sel.hover, sel.tagWidth, 15.0f, (qboolean)( sel.tagFor != sel.hover ) ) ) {
		sel.tagFor = sel.hover;
	}
	if ( sel.tagFor != sel.hover ) {
		return;
	}
	sel.tagShown = sel.tag;
	if ( sel.tag.world && !sel.tagOver ) {
		Select_DrawTag( &sel.tagShown, name, sel.tagWidth );
	}
}

// What a unit of the card that has grown is on the screen, in units of the HUD: 1, the size it was made for. More
// in a window of so few pixels that its rows would then be lower than the rows of a card that shrinks back.
static float Select_FlatUnit( void ) {
	const float row = SELECT_LINE * SELECT_SCALE * cgs.glconfig.vidHeight / SCREEN_HEIGHT;

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
trembled against the level with every move of the camera). But where it stands it would reach into floors and
walls at that size, so it is drawn over the scene and not in it (CG_SelectOverScene()). For an engine that
writes nothing in the scene CG_SelectDraw() draws it with the HUD.
=================
*/
static void Select_Card( void ) {
	const qboolean	seen = Select_Valid( sel.selected );
	float			h, perUnit;

	Select_Rows( sel.selected );
	h = SELECT_HEAD_H + sel.numRows * SELECT_ROW_H + 5.0f;
	Board_Begin( &sel.card );
	if ( sel.card.world && seen && Select_Away( sel.selected, &perUnit ) ) {
		// what a unit of the card is on the screen where it stands in the level, in units of the HUD, and a row
		// of it in pixels
		const float	k = SELECT_UNIT * perUnit;
		const float	row = SELECT_LINE * SELECT_SCALE * k * cgs.glconfig.vidHeight / SCREEN_HEIGHT;
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
			sel.over = qfalse;
		} else {
			sel.apparent = Q_max( 0.02f, sel.flatFrom + ( ( sel.flat ? Select_FlatUnit() : k ) - sel.flatFrom ) * Select_Pop( age ) );
			sel.over = qtrue;
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
	if ( sel.card.world && sel.cardPlaced && !sel.over ) {
		Select_DrawCard( &sel.shown );
	}
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
		if ( sel.selected >= 0 ) {
			Select_Card();
		}
	}
}

/*
=================
CG_SelectOverScene

Right after the scene was drawn: the name and the card that have grown for a player far away, in a scene of
their own with the same camera and nothing else in it. They are the planes they were, and nothing of the level,
no figure and no light that is added to the picture afterwards lies on them.
=================
*/
void CG_SelectOverScene( void ) {
	const qboolean	tag = (qboolean)( sel.tagOver && sel.hover >= 0 && sel.hover != sel.selected && sel.tagFor == sel.hover && sel.tagShown.world );
	const qboolean	card = (qboolean)( sel.over && sel.selected >= 0 && sel.cardPlaced && sel.shown.world );
	refdef_t		refdef;

	if ( ( !tag && !card ) || !Select_Free() || !Select_World() ) {
		return;
	}
	trap->R_ClearScene();
	if ( tag ) {
		Select_DrawTag( &sel.tagShown, cgs.clientinfo[sel.hover].name, sel.tagWidth );
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
	float			x0, y0, x1, y1, half;

	VectorCopy( cg_entities[clientNum].lerpOrigin, top );
	VectorCopy( top, bottom );
	top[2] += 44.0f;
	bottom[2] -= 26.0f;
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
	}
	if ( !free ) {
		sel.hover = -1;
		Select_Close();
		// (the chat box open with the selection of players switched off: its pointer)
		if ( CG_ChatIsOpen() ) {
			CG_Place2D( PLACE_CENTER );
			CG_SelectDrawCursor();
		}
		return;
	}
	if ( sel.selected >= 0 && !cgs.clientinfo[sel.selected].infoValid ) {
		Select_Close();
	}

	CG_Place2D( PLACE_CENTER );
	sel.hover = ( Select_RowAt() >= 0 ) ? -1 : Select_Pick();
	if ( sel.hover >= 0 && sel.hover != sel.selected ) {
		Select_Hull();
		if ( !sel.hull[0] || !cg_selectOutline.integer ) {
			Select_DrawCorners( sel.hover );
		}
	}
	if ( !Select_World() ) {
		Select_Tag();
		if ( sel.selected >= 0 ) {
			Select_Card();
		}
	}
	// the name and the card on the screen: an engine that writes nothing in the scene
	if ( sel.hover >= 0 && sel.hover != sel.selected && sel.tagFor == sel.hover && !sel.tagShown.world ) {
		selectBoard_t at = sel.tagShown;

		Select_Pixels( &at.corner[0], &at.corner[1] );
		Select_DrawTag( &at, cgs.clientinfo[sel.hover].name, sel.tagWidth );
	}
	if ( sel.selected >= 0 && sel.cardPlaced && !sel.shown.world ) {
		selectBoard_t at = sel.shown;

		Select_Pixels( &at.corner[0], &at.corner[1] );
		Select_DrawCard( &at );
	}
	// the pointer lies on everything, on the card as well
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
	if ( !down ) {
		return (qboolean)!CG_ChatIsOpen();
	}
	if ( key == A_MOUSE1 ) {
		const int row = Select_RowAt();

		if ( row >= 0 ) {
			Select_Run( sel.rows[row], sel.selected );
			return qtrue;
		}
		if ( sel.hover >= 0 ) {
			sel.selected = sel.hover;
			sel.confirmTime = 0;
			sel.flat = sel.over = qfalse;
			sel.flatTime = 0;
			sel.cardUnit = sel.apparent = 0.0f;
			// the card opens where his name stood
			sel.cardPlaced = (qboolean)( sel.tagFor == sel.hover );
			if ( sel.cardPlaced ) {
				sel.card = sel.tag;
			}
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
	if ( key == A_ESCAPE || key == A_MOUSE2 ) {
		sel.toggled = qfalse;
	}
	// every other key is swallowed while the mouse is free: the game does not get it
	return qtrue;
}

void CG_SelectCursorDown_f( void ) {
	sel.held = qtrue;
}

void CG_SelectCursorUp_f( void ) {
	sel.held = qfalse;
}

/*
=================
CG_SelectCursor_f

cursor: the mouse is free, or the game has it back. cursor at <x> <y>: the pointer goes there, in the 640 x 480
of the HUD. cursor on <player>: onto that player. cursor click: a click of the left button where the pointer
is. cursor row <n>: that row of the card that is open. All but the first are for a key or a script in place of
a mouse.
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
	} else if ( !Q_stricmp( what, "click" ) ) {
		CG_SelectKeyEvent( A_MOUSE1, qtrue );
	} else if ( !Q_stricmp( what, "over" ) ) {
		// the pointer onto the middle of a row of the card that is open, wherever the card is (for scripts)
		const int row = atoi( CG_Argv( 2 ) ) - 1;

		float x, y;

		if ( sel.selected >= 0 && row >= 0 && row < sel.numRows
			&& Board_Screen( &sel.shown, SELECT_CARD_W * 0.5f, SELECT_HEAD_H + ( row + 0.5f ) * SELECT_ROW_H, &x, &y ) ) {
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
