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
// read, then fade out (cg_bubbleRange).
//
// It needs an engine of this tree (cvar cl_cgameScene): the text comes from its TrueType font
// (trap->ext.R_Text3D), and the pictures of the panel are made here and handed to the renderer
// (trap->ext.R_RegisterShaderRGBA), so no file of a pk3 is needed and a pure server takes nothing away. In any
// other engine, with cg_chatBubbles 0 and beyond the range the balloon of the game stays.
//
// A bubble is made from a chat line whose sender is known (CG_ChatSender in chat/cg_chat.c), on any server.
// Private lines never get one. Only the clients of this tree draw them: a legacy client sees the balloon.

#include "cgame/cg_local.h"

#define BUBBLE_MAX_LINES	3
#define BUBBLE_LINE_BYTES	128
#define BUBBLE_WRAP			15.0f		// the width of a line, in heights of a letter
#define BUBBLE_LINE_SPACE	1.25f		// from one line to the next, in heights of a letter
#define BUBBLE_HEAD			56.0f		// above the feet of a player of normal size
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
	trap->Cvar_VariableStringBuffer( "cl_cgameScene", value, sizeof( value ) );
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

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || kind == CHAT_TELL || !cg_chatBubbles.integer ) {
		return;
	}
	if ( !bubbleArt.tried ) {
		Bubble_InitArt();
	}
	if ( !bubbleArt.ready ) {
		return;
	}
	trap->Cvar_VariableStringBuffer( "cl_utf8Text", value, sizeof( value ) );
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
	vec3_t	right, up, toward;	// of the camera: the panel faces it
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
CG_BubbleDraw

Adds what stands above the head of a player to the scene: what he said, or three dots while he types (dots:
the caller has nothing else to show there). qfalse: nothing was added, the caller draws the balloon of the
game if the player talks.
=================
*/
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
	float			halfW, halfH, pad, tail, scale, stack = 0.0f;
	qboolean		talking, typing, hasStatus;
	char			statusLine[128];
	int				i;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cg.snap ) {
		return qfalse;
	}
	b = &bubbles[clientNum];
	talking = (qboolean)( cg_chatBubbles.integer && b->numLines > 0 && cg.time < b->endTime && cg.time >= b->startTime );
	typing = (qboolean)( cg_chatBubbles.integer && !talking && dots && ( cent->currentState.eFlags & EF_TALK ) );
	// away, or the music he listens to (status/cg_status.c)
	hasStatus = CG_StatusLine( clientNum, statusLine, sizeof( statusLine ) );
	if ( !talking && !typing && !hasStatus ) {
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
	range = Q_max( 100.0f, cg_bubbleRange.value );
	if ( dist > range ) {
		return qfalse;
	}
	if ( dist > range * 0.75f ) {
		alpha = ( range - dist ) / ( range * 0.25f );
	}

	// A letter is as tall as it has to be to take about the same room on the screen at any distance, within
	// limits: up close a bubble does not shrink to nothing, far away it does not grow over the player.
	height = dist * tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) ) * 2.0f / 54.0f;
	height = Com_Clamp( 2.2f, 12.0f, height );

	VectorNegate( cg.refdef.viewaxis[1], view.right );
	VectorCopy( cg.refdef.viewaxis[2], view.up );
	VectorNegate( cg.refdef.viewaxis[0], view.toward );

	if ( hasStatus ) {
		// one line in smaller letters right above the head, the bubble stands on top of it
		static vec4_t	statusFill = { 0.04f, 0.06f, 0.1f, 0.6f };
		static vec4_t	statusText = { 0.8f, 0.88f, 1.0f, 1.0f };
		const float		letter = height * 0.72f;
		const float		width = trap->ext.R_Text3DWidth( statusLine, letter );
		vec3_t			origin;

		halfW = width * 0.5f + letter * 0.6f;
		halfH = letter * 0.75f;
		VectorCopy( head, view.center );
		view.center[2] += halfH;
		Bubble_Panel( bubbleArt.fill, &view, halfW, halfH, letter * 0.7f, 0.1f, statusFill, alpha );

		VectorCopy4( statusText, color );
		color[3] = alpha;
		VectorMA( view.center, -width * 0.5f, view.right, origin );
		VectorMA( origin, -letter * 0.36f, view.up, origin );
		VectorMA( origin, 0.4f, view.toward, origin );
		trap->ext.R_Text3D( origin, view.right, view.up, letter, statusLine, color );

		stack = 2.0f * halfH + letter * 0.3f;
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
