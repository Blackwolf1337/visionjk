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

// cg_bubble.c -- chat bubbles above the heads. Not from JA+.
//
// What a player says in the chat stands above his head for a few seconds, and three dots while he types. All
// of it is in the 3D scene, not in the 2D of the HUD: a panel and its text are polygons that face the camera,
// are hidden by what is in front of them and get smaller with the distance down to a size that can still be
// read, then fade out (v_bubbleRange).
//
// It needs an engine of this tree (cvar v_cgameScene): the text comes from its TrueType font
// (trap->ext.R_Text3D), and the pictures of the panel are made here and handed to the renderer
// (trap->ext.R_RegisterShaderRGBA), so no file of a pk3 is needed and a pure server takes nothing away. In any
// other engine, with v_chatBubbles 0 and beyond the range the balloon of the game stays.
//
// A bubble is made from a chat line whose sender is known (CG_ChatSender in chat/cg_chat.c), on any server.
// Private lines never get one. Only the clients of this tree draw them: a legacy client sees the balloon.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "cgame/hud/cg_hudkit.h"

#define BUBBLE_MAX_LINES	3
#define BUBBLE_LINE_BYTES	128
#define BUBBLE_WRAP			15.0f		// the width of a line, in heights of a letter
#define BUBBLE_LINE_SPACE	1.25f		// from one line to the next, in heights of a letter
#define BUBBLE_HEAD			56.0f		// above the feet of a player of normal size
#define BUBBLE_LETTER		4.2f		// how tall a letter is, in units of the level
#define BUBBLE_OWN_DIST		80.0f	// the bubble over the own head is as large as from this far, at most
#define BUBBLE_POP_MSEC		180
#define BUBBLE_OUT_MSEC		400
#define BUBBLE_TEX			64			// the pictures of the panel, and the part of them that is a corner
#define BUBBLE_TEX_CORNER	24
#define BUBBLE_SMALL_TEX	32

typedef struct bubble_s {
	char		lines[BUBBLE_MAX_LINES][BUBBLE_LINE_BYTES];
	float		widths[BUBBLE_MAX_LINES];	// of each line, in heights of a letter
	int			numLines;
	float		width;						// of the widest line
	int			startTime, endTime;
	qboolean	team;
} bubble_t;

static bubble_t bubbles[MAX_CLIENTS];

static struct {
	qboolean	tried;
	qboolean	ready;
	qhandle_t	fill, border, glow, dot, tail;
	qhandle_t	overFill, overBorder, overLight;	// the same pictures, drawn over all of the scene (CG_BubblePanelAt)
} bubbleArt;

/*
===============================================================================

THE PICTURES

Made here, so they need no file: a rectangle with round corners (its inside, its edge and a soft light around
it), a dot and the tip that points at the head. White with the shape in the alpha, the colour comes from the
polygons.

===============================================================================
*/

// how far a point is outside (above 0) or inside (below 0) a rectangle with round corners around the middle of
// a picture
static float Bubble_RoundRect( float x, float y, float size, float half, float radius ) {
	const float	dx = fabsf( x - size * 0.5f ) - ( half - radius );
	const float	dy = fabsf( y - size * 0.5f ) - ( half - radius );
	const float	ox = Q_max( dx, 0.0f ), oy = Q_max( dy, 0.0f );

	return sqrtf( ox * ox + oy * oy ) + Q_min( Q_max( dx, dy ), 0.0f ) - radius;
}

static void Bubble_Pixel( byte *pic, int size, int x, int y, float alpha ) {
	byte *p = pic + ( y * size + x ) * 4;

	p[0] = p[1] = p[2] = 255;
	p[3] = (byte)( Com_Clamp( 0.0f, 1.0f, alpha ) * 255.0f );
}

static void Bubble_InitArt( void ) {
	static byte	pic[BUBBLE_TEX * BUBBLE_TEX * 4];
	char		value[8];
	int			x, y;

	bubbleArt.tried = qtrue;
	trap->Cvar_VariableStringBuffer( "v_cgameScene", value, sizeof( value ) );
	if ( atoi( value ) != 1 ) {
		return;
	}

	// the inside
	for ( y = 0; y < BUBBLE_TEX; y++ ) {
		for ( x = 0; x < BUBBLE_TEX; x++ ) {
			Bubble_Pixel( pic, BUBBLE_TEX, x, y, 0.5f - Bubble_RoundRect( x + 0.5f, y + 0.5f, BUBBLE_TEX, 30.0f, 18.0f ) );
		}
	}
	bubbleArt.fill = trap->ext.R_RegisterShaderRGBA( "bubble_fill", pic, BUBBLE_TEX, BUBBLE_TEX, 0 );
	bubbleArt.overFill = trap->ext.R_RegisterShaderRGBA( "bubble_fill_over", pic, BUBBLE_TEX, BUBBLE_TEX, RGBA_SHADER_OVER );

	// the edge, two and a half pixels just inside the shape
	for ( y = 0; y < BUBBLE_TEX; y++ ) {
		for ( x = 0; x < BUBBLE_TEX; x++ ) {
			Bubble_Pixel( pic, BUBBLE_TEX, x, y, 1.75f - fabsf( Bubble_RoundRect( x + 0.5f, y + 0.5f, BUBBLE_TEX, 30.0f, 18.0f ) + 1.25f ) );
		}
	}
	bubbleArt.border = trap->ext.R_RegisterShaderRGBA( "bubble_border", pic, BUBBLE_TEX, BUBBLE_TEX, 0 );
	bubbleArt.overBorder = trap->ext.R_RegisterShaderRGBA( "bubble_border_over", pic, BUBBLE_TEX, BUBBLE_TEX, RGBA_SHADER_OVER );

	// the light around it: a smaller shape whose outside falls off softly
	for ( y = 0; y < BUBBLE_TEX; y++ ) {
		for ( x = 0; x < BUBBLE_TEX; x++ ) {
			const float d = Q_max( 0.0f, Bubble_RoundRect( x + 0.5f, y + 0.5f, BUBBLE_TEX, 20.0f, 12.0f ) );

			Bubble_Pixel( pic, BUBBLE_TEX, x, y, 0.5f * expf( -( d * d ) / 30.0f ) );
		}
	}
	bubbleArt.glow = trap->ext.R_RegisterShaderRGBA( "bubble_glow", pic, BUBBLE_TEX, BUBBLE_TEX, RGBA_SHADER_GLOW );
	// Not added to what is behind it as the glow of a bubble is: a panel as large as the one of the menu turned
	// white with it wherever a bright wall stood behind its rim.
	bubbleArt.overLight = trap->ext.R_RegisterShaderRGBA( "bubble_light_over", pic, BUBBLE_TEX, BUBBLE_TEX, RGBA_SHADER_OVER | RGBA_SHADER_BEHIND );

	// a dot
	for ( y = 0; y < BUBBLE_SMALL_TEX; y++ ) {
		for ( x = 0; x < BUBBLE_SMALL_TEX; x++ ) {
			const float dx = x + 0.5f - BUBBLE_SMALL_TEX * 0.5f, dy = y + 0.5f - BUBBLE_SMALL_TEX * 0.5f;

			Bubble_Pixel( pic, BUBBLE_SMALL_TEX, x, y, 13.5f - sqrtf( dx * dx + dy * dy ) );
		}
	}
	bubbleArt.dot = trap->ext.R_RegisterShaderRGBA( "bubble_dot", pic, BUBBLE_SMALL_TEX, BUBBLE_SMALL_TEX, 0 );

	// the tip: as wide as the picture at its top, a point at its bottom
	for ( y = 0; y < BUBBLE_SMALL_TEX; y++ ) {
		for ( x = 0; x < BUBBLE_SMALL_TEX; x++ ) {
			const float half = BUBBLE_SMALL_TEX * 0.5f * ( 1.0f - ( y + 0.5f ) / BUBBLE_SMALL_TEX );

			Bubble_Pixel( pic, BUBBLE_SMALL_TEX, x, y, half - fabsf( x + 0.5f - BUBBLE_SMALL_TEX * 0.5f ) + 0.5f );
		}
	}
	bubbleArt.tail = trap->ext.R_RegisterShaderRGBA( "bubble_tail", pic, BUBBLE_SMALL_TEX, BUBBLE_SMALL_TEX, 0 );

	// and a font to write with
	bubbleArt.ready = (qboolean)( bubbleArt.fill && bubbleArt.border && bubbleArt.glow && bubbleArt.dot && bubbleArt.tail
		&& trap->ext.R_Text3DWidth( "M", 1.0f ) > 0.0f );
}

// the cgame was loaded anew, and with it the renderer
void CG_BubbleInit( void ) {
	memset( bubbles, 0, sizeof( bubbles ) );
	memset( &bubbleArt, 0, sizeof( bubbleArt ) );
}

/*
===============================================================================

THE TEXT

===============================================================================
*/

// the width of a text in heights of a letter
static float Bubble_Width( const char *text ) {
	return trap->ext.R_Text3DWidth( text, 1.0f );
}

/*
=================
CG_BubbleSay

A player said something in the chat. The text is put into up to three lines; what does not fit ends in dots.
=================
*/
void CG_BubbleSay( int clientNum, int kind, const char *message ) {
	bubble_t	*b;
	char		text[MAX_SAY_TEXT], word[BUBBLE_LINE_BYTES];
	const char	*s;
	char		value[8];
	qboolean	utf8;
	int			line = 0;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || kind == CHAT_TELL || !v_chatBubbles.integer ) {
		return;
	}
	if ( !bubbleArt.tried ) {
		Bubble_InitArt();
	}
	if ( !bubbleArt.ready ) {
		return;
	}
	trap->Cvar_VariableStringBuffer( "v_utf8Text", value, sizeof( value ) );
	utf8 = (qboolean)( atoi( value ) == 1 );

	Q_strncpyz( text, message, sizeof( text ) );
	Q_StripColor( text );

	b = &bubbles[clientNum];
	memset( b, 0, sizeof( *b ) );

	for ( s = text; ; ) {
		int		len = 0;
		char	candidate[BUBBLE_LINE_BYTES];

		while ( *s == ' ' ) {
			s++;
		}
		if ( !*s || line >= BUBBLE_MAX_LINES ) {
			break;
		}
		// the next word
		while ( s[len] && s[len] != ' ' && len < (int)sizeof( word ) - 1 ) {
			len++;
		}
		Q_strncpyz( word, s, len + 1 );

		Com_sprintf( candidate, sizeof( candidate ), "%s%s%s", b->lines[line], b->lines[line][0] ? " " : "", word );
		if ( Bubble_Width( candidate ) <= BUBBLE_WRAP ) {
			Q_strncpyz( b->lines[line], candidate, sizeof( b->lines[line] ) );
			s += len;
			continue;
		}
		if ( b->lines[line][0] ) {
			// the word goes to the next line
			line++;
			continue;
		}
		// a word that is wider than a line: as much of it as fits, the rest goes on in the next line
		len = 0;
		while ( s[len] && s[len] != ' ' ) {
			unsigned int	codePoint;
			const int		charLen = utf8 ? Q_UTF8_Decode( s + len, &codePoint ) : 1;

			if ( len + charLen >= (int)sizeof( candidate ) ) {
				break;
			}
			Q_strncpyz( candidate, s, len + charLen + 1 );
			if ( len && Bubble_Width( candidate ) > BUBBLE_WRAP ) {
				break;
			}
			len += charLen;
		}
		Q_strncpyz( b->lines[line], s, Q_min( (int)sizeof( b->lines[line] ), len + 1 ) );
		s += len;
		line++;
	}
	// the lines that have text. s is at what found no room.
	b->numLines = ( line < BUBBLE_MAX_LINES && b->lines[line][0] ) ? line + 1 : line;
	if ( b->numLines < 1 ) {
		return;
	}
	if ( *s ) {
		// more was said than three lines hold
		Q_strcat( b->lines[b->numLines - 1], sizeof( b->lines[0] ), "..." );
	}
	for ( line = 0; line < b->numLines; line++ ) {
		b->widths[line] = Bubble_Width( b->lines[line] );
		if ( b->widths[line] > b->width ) {
			b->width = b->widths[line];
		}
	}
	b->team = (qboolean)( kind == CHAT_TEAM );
	b->startTime = cg.time;
	b->endTime = cg.time + Com_Clampi( 2500, 8000, 2500 + 70 * (int)strlen( text ) );
}

/*
===============================================================================

DRAWING

===============================================================================
*/

typedef struct bubbleView_s {
	vec3_t	center;				// the middle of the panel
	vec3_t	right, up, toward;	// of the panel: upright, and turned to the camera about its upright axis
} bubbleView_t;

static void Bubble_Vert( polyVert_t *vert, const bubbleView_t *view, float x, float y, float depth, float s, float t, const byte *rgba ) {
	VectorMA( view->center, x, view->right, vert->xyz );
	VectorMA( vert->xyz, y, view->up, vert->xyz );
	VectorMA( vert->xyz, depth, view->toward, vert->xyz );
	vert->st[0] = s;
	vert->st[1] = t;
	memcpy( vert->modulate, rgba, 4 );
}

static void Bubble_Color( const float *color, float alpha, byte *rgba ) {
	rgba[0] = (byte)( color[0] * 255.0f );
	rgba[1] = (byte)( color[1] * 255.0f );
	rgba[2] = (byte)( color[2] * 255.0f );
	rgba[3] = (byte)( Com_Clamp( 0.0f, 1.0f, color[3] * alpha ) * 255.0f );
}

// a picture as one rectangle around a point of the panel
static void Bubble_Quad( qhandle_t shader, const bubbleView_t *view, float x, float y, float halfW, float halfH, float depth, const float *color, float alpha ) {
	polyVert_t	verts[4];
	byte		rgba[4];

	Bubble_Color( color, alpha, rgba );
	Bubble_Vert( &verts[0], view, x - halfW, y + halfH, depth, 0.0f, 0.0f, rgba );
	Bubble_Vert( &verts[1], view, x + halfW, y + halfH, depth, 1.0f, 0.0f, rgba );
	Bubble_Vert( &verts[2], view, x + halfW, y - halfH, depth, 1.0f, 1.0f, rgba );
	Bubble_Vert( &verts[3], view, x - halfW, y - halfH, depth, 0.0f, 1.0f, rgba );
	trap->R_AddPolysToScene( shader, 4, verts, 1 );
}

// A picture with round corners over a rectangle of any size: its corners keep their shape, the parts between
// them are stretched (nine rectangles).
static void Bubble_Panel( qhandle_t shader, const bubbleView_t *view, float halfW, float halfH, float corner, float depth, const float *color, float alpha ) {
	const float	st[4] = { 0.0f, (float)BUBBLE_TEX_CORNER / BUBBLE_TEX, 1.0f - (float)BUBBLE_TEX_CORNER / BUBBLE_TEX, 1.0f };
	float		xs[4], ys[4];
	polyVert_t	verts[9 * 4];
	byte		rgba[4];
	int			i, j, n = 0;

	corner = Q_min( corner, Q_min( halfW, halfH ) );
	xs[0] = -halfW;	xs[1] = -halfW + corner;	xs[2] = halfW - corner;		xs[3] = halfW;
	ys[0] = halfH;	ys[1] = halfH - corner;		ys[2] = -halfH + corner;	ys[3] = -halfH;

	Bubble_Color( color, alpha, rgba );
	for ( j = 0; j < 3; j++ ) {
		for ( i = 0; i < 3; i++ ) {
			Bubble_Vert( &verts[n++], view, xs[i], ys[j], depth, st[i], st[j], rgba );
			Bubble_Vert( &verts[n++], view, xs[i + 1], ys[j], depth, st[i + 1], st[j], rgba );
			Bubble_Vert( &verts[n++], view, xs[i + 1], ys[j + 1], depth, st[i + 1], st[j + 1], rgba );
			Bubble_Vert( &verts[n++], view, xs[i], ys[j + 1], depth, st[i], st[j + 1], rgba );
		}
	}
	trap->R_AddPolysToScene( shader, 4, verts, 9 );
}

/*
=================
CG_BubblePanelAt

A panel as the bubbles have it, for something else that stands in the level (the menu, menu3d/cg_menu3d.c):
a soft edge of light, its inside and its edge around a point, along right and up. Unlike a bubble it is drawn over all
of the scene: the menu on it is too, and a wall or a pillar between the camera and the panel took a piece out
of the panel and left the menu in the air. qfalse: it can not be drawn (another engine).
=================
*/
qboolean CG_BubblePanelAt( const vec3_t center, const vec3_t right, const vec3_t up, float halfW, float halfH, float corner,
		const float *fill, const float *border, const float *light, float alpha ) {
	bubbleView_t view;

	if ( !bubbleArt.tried ) {
		Bubble_InitArt();
	}
	if ( !bubbleArt.ready ) {
		return qfalse;
	}
	VectorCopy( center, view.center );
	VectorCopy( right, view.right );
	VectorCopy( up, view.up );
	CrossProduct( up, right, view.toward );
	if ( !bubbleArt.overLight || !bubbleArt.overFill || !bubbleArt.overBorder ) {
		return qfalse;
	}
	Bubble_Panel( bubbleArt.overLight, &view, halfW + corner, halfH + corner, corner * 1.8f, 0.0f, light, alpha );
	Bubble_Panel( bubbleArt.overFill, &view, halfW, halfH, corner, 0.0f, fill, alpha );
	Bubble_Panel( bubbleArt.overBorder, &view, halfW, halfH, corner, 0.0f, border, alpha );
	return qtrue;
}

/*
=================
CG_BubbleBoard

A panel as the bubbles have it, for something else that is a thing in the level as they are (the card of a
player, select/cg_select.c, which draws it in a scene of its own): its inside, and its edge where one is given,
around a point, along right and up. qfalse: it can not be drawn (another engine). CG_BubbleReady() says so beforehand.
(Inside and edge lie in one plane: none of them writes depth, and the edge is drawn after the inside.)
=================
*/
qboolean CG_BubbleReady( void ) {
	if ( !bubbleArt.tried ) {
		Bubble_InitArt();
	}
	return bubbleArt.ready;
}

qboolean CG_BubbleBoard( const vec3_t center, const vec3_t right, const vec3_t up, float halfW, float halfH, float corner,
		const float *fill, const float *border ) {
	bubbleView_t view;

	if ( !CG_BubbleReady() ) {
		return qfalse;
	}
	VectorCopy( center, view.center );
	VectorCopy( right, view.right );
	VectorCopy( up, view.up );
	CrossProduct( right, up, view.toward );
	if ( fill ) {
		Bubble_Panel( bubbleArt.fill, &view, halfW, halfH, corner, 0.0f, fill, 1.0f );
	}
	if ( border ) {
		Bubble_Panel( bubbleArt.border, &view, halfW, halfH, corner, 0.0f, border, 1.0f );
	}
	return qtrue;
}

/*
=================
CG_BubbleDraw

Adds what stands above the head of a player to the scene: what he said, or three dots while he types (dots:
the caller has nothing else to show there). qfalse: nothing was added, the caller draws the balloon of the
game if the player talks.
=================
*/
// The status lines that were placed this frame, as they lie on the screen. Two players who stand close have
// their lines at the same height: the one that would lie on another is lifted above it.
#define BUBBLE_LINES	24

static struct {
	int		frame;
	int		count;
	float	rect[BUBBLE_LINES][4];		// left, top, right, bottom, in units of the HUD
} bubbleLines;

qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );		// cg_draw.c

// where a board with its middle at `center` lies on the screen. qfalse: it is not before the camera
static qboolean Bubble_ScreenRect( vec3_t center, float halfW, float halfH, float *rect ) {
	vec3_t	to;
	float	x, y, z, perUnit;

	VectorSubtract( center, cg.refdef.vieworg, to );
	z = DotProduct( to, cg.refdef.viewaxis[0] );
	if ( z < 8.0f || !CG_WorldCoordToScreenCoordFloat( center, &x, &y ) ) {
		return qfalse;
	}
	perUnit = SCREEN_HEIGHT * 0.5f / ( z * tanf( DEG2RAD( cg.refdef.fov_y ) * 0.5f ) );
	rect[0] = x - halfW * perUnit;
	rect[1] = y - halfH * perUnit;
	rect[2] = x + halfW * perUnit;
	rect[3] = y + halfH * perUnit;
	return qtrue;
}

// How far a status line has to be lifted so that it lies on none that was placed before it this frame, in
// units of the level. Its place is taken note of.
static float Bubble_LiftLine( const vec3_t center, float halfW, float halfH ) {
	const float	step = 2.0f * halfH * 1.12f;
	float		rect[4], lift = 0.0f;
	vec3_t		at;
	int			i, tries;

	if ( bubbleLines.frame != cg.clientFrame ) {
		bubbleLines.frame = cg.clientFrame;
		bubbleLines.count = 0;
	}
	for ( tries = 0; tries < 4; tries++ ) {
		VectorCopy( center, at );
		at[2] += lift;
		if ( !Bubble_ScreenRect( at, halfW, halfH, rect ) ) {
			return 0.0f;
		}
		for ( i = 0; i < bubbleLines.count; i++ ) {
			const float *other = bubbleLines.rect[i];

			if ( rect[0] < other[2] && rect[2] > other[0] && rect[1] < other[3] && rect[3] > other[1] ) {
				break;
			}
		}
		if ( i == bubbleLines.count ) {
			break;
		}
		lift += step;
	}
	if ( bubbleLines.count < BUBBLE_LINES ) {
		memcpy( bubbleLines.rect[bubbleLines.count++], rect, sizeof( rect ) );
	}
	return lift;
}

// The name tags (v_nameTags): the name of a player on a chip right above his head, under his status line and
// his bubble. 1: of who is near the crosshair and of the team mates, 2: of everyone in sight. Unlike a bubble a
// tag keeps one size on the screen at any distance (up to TAG_MAX_LETTER in the level), and only players one
// can see get one: a trace to the head, so no name stands over a wall. The one under the crosshair has a larger
// tag with his health where the client knows it, and the name under the crosshair goes (CG_DrawCrosshairNames).
#define TAG_RANGE		2000.0f
#define TAG_TEAM_RANGE	1400.0f
#define TAG_NEAR		60.0f		// around the crosshair, in units of the HUD: the tag is there in full
#define TAG_NEAR_FADE	50.0f		// and fades out over this much more
#define TAG_SCREEN		9.0f		// how tall a letter is on the screen, in units of the HUD
#define TAG_SCREEN_AIM	12.0f		// of the one under the crosshair
#define TAG_MIN_LETTER	2.6f		// in units of the level, up close
#define TAG_MAX_LETTER	32.0f		// far away
#define TAG_FADE_MSEC	160
#define TAG_BELOW		12.0f		// the tag starts this much lower than a bubble would, right over the hair

static struct {
	float	alpha;
	int		seen;			// cg.time it was last asked for: a player out of view starts again from nothing
	int		drawnFrame;		// cg.clientFrame it was drawn in
} nameTags[MAX_CLIENTS];

qboolean CG_NameTagShown( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && nameTags[clientNum].drawnFrame == cg.clientFrame );
}

static qboolean Tag_Aimed( int clientNum ) {
	return (qboolean)( cg_drawCrosshair.integer && cg_drawCrosshairNames.integer && clientNum == cg.crosshairClientNum
		&& cg.time - cg.crosshairClientTime < 1000 );
}

// how near the crosshair a player stands on the screen, 1 on him to 0 away
static float Tag_Near( const centity_t *cent, float scale ) {
	const float	cx = SCREEN_WIDTH * 0.5f + cg_crosshairX.value, cy = SCREEN_HEIGHT * 0.5f + cg_crosshairY.value;
	vec3_t		top, bottom;
	float		tx, ty, bx, by, dx, dy;

	VectorCopy( cent->lerpOrigin, top );
	VectorCopy( cent->lerpOrigin, bottom );
	top[2] += 40.0f * scale;
	bottom[2] -= 24.0f * scale;
	if ( !CG_WorldCoordToScreenCoordFloat( top, &tx, &ty ) || !CG_WorldCoordToScreenCoordFloat( bottom, &bx, &by ) ) {
		return 0.0f;
	}
	dx = fabsf( cx - ( tx + bx ) * 0.5f );
	dy = ( cy < ty ) ? ty - cy : ( cy > by ) ? cy - by : 0.0f;
	return 1.0f - Com_Clamp( 0.0f, 1.0f, ( sqrtf( dx * dx + dy * dy ) - TAG_NEAR ) / TAG_NEAR_FADE );
}

// How much of the tag of a player is to be seen this frame, with its fade. aimed: he is under the crosshair.
static float Tag_Alpha( const centity_t *cent, int clientNum, float dist, float scale, qboolean *aimed ) {
	const clientInfo_t	*ci = &cgs.clientinfo[clientNum];
	const int			mode = v_nameTags.integer;
	const int			team = cg.snap->ps.persistant[PERS_TEAM];
	float				target = 0.0f, step;

	*aimed = (qboolean)( mode && Tag_Aimed( clientNum ) );
	if ( mode && ci->infoValid && clientNum != cg.snap->ps.clientNum && dist < TAG_RANGE ) {
		if ( *aimed || mode >= 2 ) {
			target = 1.0f;
		} else {
			if ( cgs.gametype >= GT_TEAM && team != TEAM_SPECTATOR && ci->team == team && dist < TAG_TEAM_RANGE ) {
				target = 1.0f;
			}
			if ( target < 1.0f ) {
				target = Q_max( target, Tag_Near( cent, scale ) );
			}
		}
		if ( dist > TAG_RANGE * 0.75f ) {
			target *= ( TAG_RANGE - dist ) / ( TAG_RANGE * 0.25f );
		}
		if ( target > 0.0f ) {
			// only who can be seen: his head from the eye, not in fog (as the name under the crosshair)
			trace_t	tr;
			vec3_t	eyes;

			VectorCopy( cent->lerpOrigin, eyes );
			eyes[2] += 30.0f * scale;
			CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, eyes, cg.snap->ps.clientNum, CONTENTS_SOLID | CONTENTS_TERRAIN );
			if ( tr.fraction < 1.0f || ( CG_PointContents( eyes, 0 ) & CONTENTS_FOG ) ) {
				target = 0.0f;
			}
		}
	}

	if ( cg.time - nameTags[clientNum].seen > 300 ) {
		nameTags[clientNum].alpha = 0.0f;
	}
	nameTags[clientNum].seen = cg.time;
	step = (float)cg.frametime / TAG_FADE_MSEC;
	if ( nameTags[clientNum].alpha < target ) {
		nameTags[clientNum].alpha = Q_min( target, nameTags[clientNum].alpha + step );
	} else {
		nameTags[clientNum].alpha = Q_max( target, nameTags[clientNum].alpha - step );
	}
	return nameTags[clientNum].alpha;
}

// the name on the tag and its colour, as the name under the crosshair has them
static const char *Tag_Name( const centity_t *cent, int clientNum, const float **tint ) {
	const clientInfo_t *ci = &cgs.clientinfo[clientNum];

	if ( cg.snap->ps.duelInProgress ? clientNum != cg.snap->ps.duelIndex : cent->currentState.bolt1 != 0 ) {
		*tint = hudDim;		// out of reach in a duel
	} else if ( cgs.gametype >= GT_TEAM ) {
		*tint = ( ci->team == cg.predictedPlayerState.persistant[PERS_TEAM] ) ? hudGreen : hudRed;
	} else if ( cgs.gametype == GT_POWERDUEL && cgs.clientinfo[cg.snap->ps.clientNum].team != TEAM_SPECTATOR
		&& ci->duelTeam == cgs.clientinfo[cg.predictedPlayerState.clientNum].duelTeam ) {
		*tint = hudGreen;
	} else if ( cgs.gametype == GT_FFA ) {
		*tint = hudBright;
		return ci->name;	// JA+: in FFA the name in its own colours
	} else {
		*tint = hudRed;
	}
	return ci->cleanname;
}

// the tag over head; how much higher what stands above it has to start
static float Tag_Draw( const centity_t *cent, int clientNum, bubbleView_t *view, const vec3_t head, float scale, float alpha, qboolean aimed ) {
	const float			below = TAG_BELOW * scale;
	static const vec4_t	track = { 1.0f, 1.0f, 1.0f, 0.16f };
	const float			*tint;
	const char			*name = Tag_Name( cent, clientNum, &tint );
	vec3_t				to, origin;
	float				z, letter, width, halfW, halfH, lift, depth, textY;
	int					health;
	vec4_t				fill = { 0.04f, 0.06f, 0.1f, 0.0f }, edge, ink;

	VectorSubtract( head, cg.refdef.vieworg, to );
	z = DotProduct( to, cg.refdef.viewaxis[0] );
	if ( z < 8.0f ) {
		return 0.0f;
	}
	// a letter as tall in the level as TAG_SCREEN units of the HUD are on the screen at this distance
	letter = ( aimed ? TAG_SCREEN_AIM : TAG_SCREEN ) * z * tanf( DEG2RAD( cg.refdef.fov_y ) * 0.5f ) / ( SCREEN_HEIGHT * 0.5f );
	letter = Com_Clamp( TAG_MIN_LETTER, TAG_MAX_LETTER, letter );
	health = aimed ? CG_HudKnownHealth( clientNum ) : -1;
	width = trap->ext.R_Text3DWidth( name, letter );
	depth = letter * 0.025f;
	halfW = width * 0.5f + letter * 0.55f;
	halfH = letter * 0.72f + ( health >= 0 ? letter * 0.28f : 0.0f );

	VectorCopy( head, view->center );
	view->center[2] += halfH - below;
	lift = Bubble_LiftLine( view->center, halfW, halfH );
	view->center[2] += lift;

	fill[3] = aimed ? 0.74f : 0.52f;
	VectorCopy( tint, edge );
	edge[3] = aimed ? 0.85f : 0.3f;
	Bubble_Panel( bubbleArt.fill, view, halfW, halfH, letter * 0.6f, depth, fill, alpha );
	Bubble_Panel( bubbleArt.border, view, halfW, halfH, letter * 0.6f, depth * 2.0f, edge, alpha );

	textY = ( health >= 0 ) ? letter * 0.24f : 0.0f;
	VectorCopy( tint, ink );
	ink[3] = alpha;
	VectorMA( view->center, -width * 0.5f, view->right, origin );
	VectorMA( origin, textY - letter * 0.36f, view->up, origin );
	VectorMA( origin, depth * 4.0f, view->toward, origin );
	trap->ext.R_Text3D( origin, view->right, view->up, letter, name, ink );

	if ( health >= 0 ) {
		// his health as a thin bar under the name
		const float		barHalfW = halfW - letter * 0.6f, barHalfH = letter * 0.08f;
		const float		frac = Com_Clamp( 0.0f, 1.0f, health / 100.0f );
		bubbleView_t	bar = *view;

		VectorMA( view->center, -halfH + letter * 0.38f, view->up, bar.center );
		Bubble_Panel( bubbleArt.fill, &bar, barHalfW, barHalfH, barHalfH, depth * 3.0f, track, alpha );
		if ( frac > 0.0f ) {
			VectorMA( bar.center, -barHalfW * ( 1.0f - frac ), view->right, bar.center );
			Bubble_Panel( bubbleArt.fill, &bar, barHalfW * frac, barHalfH, barHalfH, depth * 3.5f,
				health <= 25 ? hudRed : health <= 50 ? hudWarm : hudGreen, alpha );
		}
	}
	nameTags[clientNum].drawnFrame = cg.clientFrame;
	return lift + 2.0f * halfH + letter * 0.3f - below;
}

qboolean CG_BubbleDraw( centity_t *cent, qboolean dots ) {
	static vec4_t	fillColor = { 0.04f, 0.06f, 0.1f, 0.78f };
	static vec4_t	borderColor = { 0.55f, 0.75f, 1.0f, 0.95f };
	static vec4_t	glowColor = { 0.25f, 0.5f, 1.0f, 0.35f };
	static vec4_t	textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	const int		clientNum = cent->currentState.number;
	bubble_t		*b;
	bubbleView_t	view;
	vec3_t			head, delta;
	vec4_t			border, glow, color;
	float			dist, range, height, alpha = 1.0f, grow = 1.0f, rise = 0.0f;
	float			halfW, halfH, pad, tail, scale, stack = 0.0f, tagAlpha = 0.0f;
	qboolean		talking, typing, hasStatus, tagged, aimed = qfalse;
	char			statusLine[128];
	int				i;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cg.snap ) {
		return qfalse;
	}
	b = &bubbles[clientNum];
	talking = (qboolean)( v_chatBubbles.integer && b->numLines > 0 && cg.time < b->endTime && cg.time >= b->startTime );
	typing = (qboolean)( v_chatBubbles.integer && !talking && dots && ( cent->currentState.eFlags & EF_TALK ) );
	// Away, or the music he listens to (status/cg_status.c). Not of the player the view is behind: his line
	// stood right before the camera, as large as a wall, and he knows what it says.
	hasStatus = (qboolean)( clientNum != cg.snap->ps.clientNum && CG_StatusLine( clientNum, statusLine, sizeof( statusLine ) ) );
	tagged = (qboolean)( clientNum != cg.snap->ps.clientNum && ( v_nameTags.integer || nameTags[clientNum].alpha > 0.0f ) );
	if ( !talking && !typing && !hasStatus && !tagged ) {
		return qfalse;
	}
	if ( !bubbleArt.tried ) {
		Bubble_InitArt();
	}
	if ( !bubbleArt.ready ) {
		return qfalse;
	}
	// not on somebody who is dead or can not be seen, and not on oneself where one does not see oneself
	if ( ( cent->currentState.eFlags & EF_DEAD ) || ( cent->currentState.powerups & ( 1 << PW_CLOAKED ) )
		|| ( clientNum == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) ) {
		return qfalse;
	}

	scale = ( cent->modelScale[2] > 0.01f ) ? cent->modelScale[2] : 1.0f;
	VectorCopy( cent->lerpOrigin, head );
	head[2] += BUBBLE_HEAD * scale;
	VectorSubtract( head, cg.refdef.vieworg, delta );
	dist = VectorLength( delta );
	range = Q_max( 100.0f, v_bubbleRange.value );
	if ( dist > range ) {
		talking = typing = hasStatus = qfalse;
	} else if ( dist > range * 0.75f ) {
		alpha = ( range - dist ) / ( range * 0.25f );
	}
	if ( tagged ) {
		tagAlpha = Tag_Alpha( cent, clientNum, dist, scale, &aimed );
	}
	if ( !talking && !typing && !hasStatus && tagAlpha <= 0.0f ) {
		return qfalse;
	}

	// A bubble is a thing in the level: its letters are as tall there at any distance, so it is small far away
	// and large up close as the player under it is. And it stands upright: it turns to the one who looks about
	// its upright axis only, and does not lean with a look up or down.
	height = BUBBLE_LETTER;
	// Over the own head it is no larger on the screen than it is with the camera where it usually is: a wall
	// behind the player pushes the camera up to his neck, and what he just wrote stood across the whole screen.
	if ( clientNum == cg.snap->ps.clientNum && dist < BUBBLE_OWN_DIST ) {
		height *= Q_max( 0.25f, dist / BUBBLE_OWN_DIST );
	}
	VectorSet( view.up, 0.0f, 0.0f, 1.0f );
	VectorNegate( cg.refdef.viewaxis[1], view.right );
	view.right[2] = 0.0f;
	if ( VectorNormalize( view.right ) < 0.01f ) {
		// (a camera that lies on its side)
		VectorSet( view.right, 0.0f, -1.0f, 0.0f );
	}
	CrossProduct( view.right, view.up, view.toward );

	if ( tagAlpha > 0.0f ) {
		stack = Tag_Draw( cent, clientNum, &view, head, scale, tagAlpha, aimed );
	}
	if ( hasStatus ) {
		// one line in smaller letters right above the head, the bubble stands on top of it
		static vec4_t	statusFill = { 0.04f, 0.06f, 0.1f, 0.6f };
		static vec4_t	statusText = { 0.8f, 0.88f, 1.0f, 1.0f };
		const float		letter = height * 0.72f;
		const float		width = trap->ext.R_Text3DWidth( statusLine, letter );
		vec3_t			origin;

		float			lift;

		halfW = width * 0.5f + letter * 0.6f;
		halfH = letter * 0.75f;
		VectorCopy( head, view.center );
		view.center[2] += stack + halfH;
		// (above the line of somebody who stands close, where it would lie on it)
		lift = Bubble_LiftLine( view.center, halfW, halfH );
		view.center[2] += lift;
		Bubble_Panel( bubbleArt.fill, &view, halfW, halfH, letter * 0.7f, 0.1f, statusFill, alpha );

		VectorCopy4( statusText, color );
		color[3] = alpha;
		VectorMA( view.center, -width * 0.5f, view.right, origin );
		VectorMA( origin, -letter * 0.36f, view.up, origin );
		VectorMA( origin, 0.4f, view.toward, origin );
		trap->ext.R_Text3D( origin, view.right, view.up, letter, statusLine, color );

		stack += lift + 2.0f * halfH + letter * 0.3f;
	}
	if ( !talking && !typing ) {
		return qfalse;		// the caller may still have a sprite to show
	}

	if ( talking ) {
		const int age = cg.time - b->startTime, left = b->endTime - cg.time;

		if ( age < BUBBLE_POP_MSEC ) {
			// pops up: a little too large first, then to its size
			const float t = (float)age / BUBBLE_POP_MSEC - 1.0f;

			grow = 1.0f + 2.7f * t * t * t + 1.7f * t * t;
		}
		if ( left < BUBBLE_OUT_MSEC ) {
			// rises and fades
			const float t = 1.0f - (float)left / BUBBLE_OUT_MSEC;

			alpha *= 1.0f - t;
			rise = t * height * 0.8f;
		}
	}
	height *= grow;

	pad = height * 0.55f;
	tail = height * 0.7f;
	if ( talking ) {
		halfW = b->width * height * 0.5f + pad;
		halfH = b->numLines * BUBBLE_LINE_SPACE * height * 0.5f + pad * 0.7f;
	} else {
		halfW = height * 1.9f;
		halfH = height * 0.8f;
	}

	VectorCopy( head, view.center );
	view.center[2] += stack + tail + halfH + rise;

	VectorCopy4( borderColor, border );
	VectorCopy4( glowColor, glow );
	if ( talking && b->team ) {
		// the colour of the team around what was said to the team
		static vec3_t	redTeam = { 1.0f, 0.38f, 0.32f }, blueTeam = { 0.35f, 0.55f, 1.0f };
		const team_t	team = cgs.clientinfo[clientNum].team;

		if ( team == TEAM_RED || team == TEAM_BLUE ) {
			VectorCopy( team == TEAM_RED ? redTeam : blueTeam, border );
			VectorCopy( team == TEAM_RED ? redTeam : blueTeam, glow );
		}
	}

	// from the back to the front: the light, the inside with its tip, the edge, and what stands on it
	Bubble_Panel( bubbleArt.glow, &view, halfW + height * 0.9f, halfH + height * 0.9f, height * 1.4f, 0.0f, glow, alpha );
	Bubble_Quad( bubbleArt.tail, &view, 0.0f, -halfH - tail * 0.5f + height * 0.08f, tail * 0.5f, tail * 0.5f, 0.1f, fillColor, alpha );
	Bubble_Panel( bubbleArt.fill, &view, halfW, halfH, height * 0.75f, 0.1f, fillColor, alpha );
	Bubble_Panel( bubbleArt.border, &view, halfW, halfH, height * 0.75f, 0.2f, border, alpha );

	if ( talking ) {
		VectorCopy4( textColor, color );
		color[3] = alpha;
		for ( i = 0; i < b->numLines; i++ ) {
			vec3_t origin;

			// the line the letters of row i stand on, each row in the middle of the panel
			VectorMA( view.center, -b->widths[i] * height * 0.5f, view.right, origin );
			VectorMA( origin, ( ( b->numLines - 1 ) * 0.5f - i ) * BUBBLE_LINE_SPACE * height - height * 0.36f, view.up, origin );
			VectorMA( origin, 0.4f, view.toward, origin );
			trap->ext.R_Text3D( origin, view.right, view.up, height, b->lines[i], color );
		}
	} else {
		// he types: three dots that swell one after the other
		for ( i = 0; i < 3; i++ ) {
			const float phase = ( cg.time % 1200 ) / 1200.0f - i * 0.18f;
			const float pulse = 0.5f + 0.5f * sinf( phase * 2.0f * M_PI );
			const float size = height * ( 0.16f + 0.1f * pulse );

			VectorCopy4( textColor, color );
			color[3] = 0.55f + 0.45f * pulse;
			Bubble_Quad( bubbleArt.dot, &view, ( i - 1 ) * height * 0.8f, 0.0f, size, size, 0.4f, color, alpha );
		}
	}
	return qtrue;
}
