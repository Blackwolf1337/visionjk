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

// cg_tools_ui.c -- the tool window of the toolgun (issue #3), the Q menu of that other game: the tools at the left,
// the settings of the one that is chosen at the right. v_tools opens and closes it (Controls, Extras). While it
// is open the mouse is free (select/cg_select.c draws it and hands it the keys). It only sets the cvars of
// cg_tools.c, which sends them to the server. Not from JA+, see docs/japlus/sandbox.md.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "cgame/cg_window.h"
#include "game/props/bg_tools.h"

#define TW_W			460.0f
#define TW_H			250.0f
#define TW_TOP			80.0f
#define TW_PAD			12.0f
#define TW_LIST_W		140.0f		// the tools at the left
#define TW_BAR_W		180.0f
#define TW_ROW			26.0f		// a setting with its bar
#define TW_MASS_MIN		0.1f		// the bar of the mass goes from this to 1000 times it, evenly in the powers of ten

typedef enum {
	TW_CLOSE,
	TW_TOOL,			// with the tool
	TW_COLOR,			// a bar of the colour, with its channel
	TW_PRESET,			// a colour of the row, with its number
	TW_MATERIAL,		// with the material
	TW_PHYS,			// a bar of the properties, with its number: mass, bounce, grip
	TW_PHYS_PRESET		// with its number
} twPart_t;

static const byte twColors[8][3] = {
	{ 255, 255, 255 }, { 230, 50, 50 }, { 240, 150, 40 }, { 245, 220, 60 },
	{ 70, 200, 80 }, { 60, 140, 240 }, { 160, 80, 220 }, { 40, 40, 40 }
};

// mass, bounce, grip
static const struct {
	const char	*name;
	float		mass, bounce, grip;
} twPhysPresets[4] = {
	{ "Light", 1.0f, 0.3f, 0.7f },
	{ "Heavy", 60.0f, 0.1f, 1.0f },
	{ "Bouncy", 2.0f, 0.9f, 0.5f },
	{ "Slippery", 4.0f, 0.2f, 0.05f },
};

static struct {
	qboolean	open;
	float		x, y;
	winHits_t	hits;
	int			drag;			// the bar that is dragged: TW_COLOR or TW_PHYS, 0 (TW_CLOSE): none
	int			dragArg;
	float		dragX;			// where that bar starts
} tw;

static int TW_Tool( void ) {
	char	name[MAX_QPATH];
	int		tool;

	trap->Cvar_VariableStringBuffer( "v_tool", name, sizeof( name ) );
	for ( tool = 0; tool < NUM_TOOLS && Q_stricmp( name, toolNames[tool] ); tool++ )
		;
	return ( tool < NUM_TOOLS ) ? tool : TOOL_REMOVER;
}

static void TW_Color( int rgb[3] ) {
	char value[64];

	rgb[0] = rgb[1] = rgb[2] = 255;
	trap->Cvar_VariableStringBuffer( "v_toolColor", value, sizeof( value ) );
	sscanf( value, "%3i %3i %3i", &rgb[0], &rgb[1], &rgb[2] );
}

static void TW_Phys( float phys[3] ) {
	char value[64];

	phys[0] = 1.0f;
	phys[1] = 0.25f;
	phys[2] = 0.7f;
	trap->Cvar_VariableStringBuffer( "v_toolPhys", value, sizeof( value ) );
	sscanf( value, "%15f %15f %15f", &phys[0], &phys[1], &phys[2] );
}

void CG_ToolWindowOpen( void ) {
	memset( &tw, 0, sizeof( tw ) );
	tw.open = qtrue;
}

void CG_ToolWindowClose( void ) {
	tw.open = qfalse;
	tw.drag = TW_CLOSE;
}

// v_tools: open and closed
void CG_ToolWindow_f( void ) {
	if ( tw.open )
		CG_ToolWindowClose();
	else
		CG_ToolWindowOpen();
}

qboolean CG_ToolWindowShown( void ) {
	return tw.open;
}

qboolean CG_ToolWindowPointerOver( void ) {
	return ( tw.open && Win_Over( tw.x, tw.y, TW_W, TW_H ) ) ? qtrue : qfalse;
}

// a setting with a name at the left and a bar after it
static void TW_BarRow( int part, int arg, float x, float y, const char *name, const char *value, float fraction ) {
	Win_Text( x, y, 0.5f, winText, 0, name, 80.0f );
	Win_Text( x + 90.0f + TW_BAR_W + 34.0f, y, 0.5f, winDim, 1, value, 40.0f );
	Win_Bar( &tw.hits, part, x + 90.0f, y + 5.0f, TW_BAR_W, fraction, qtrue, (qboolean)( tw.drag == part && tw.dragArg == arg ) );
	// (Win_Bar names the part, the number of the bar is put on it here)
	tw.hits.hit[tw.hits.num - 1].arg = arg;
}

/*
=================
CG_ToolWindowDraw

Once a frame while it is open, in the 2D of the HUD, before the pointer
=================
*/
void CG_ToolWindowDraw( void ) {
	static const char *const titles[NUM_TOOLS] = { "Remover", "Color", "Material", "Physical Properties", "Freeze" };
	static const char *const about[NUM_TOOLS] = {
		"Left click removes the prop you look at.",
		"Left click paints the prop, right click takes its color.",
		"Left click puts the material on the prop, right click takes it.",
		"Left click gives the prop these properties, right click takes them.",
		"Left click freezes the prop in place, right click frees it, R frees all."
	};
	const int	tool = TW_Tool();
	float		x, y, right, phys[3];
	int			i, rgb[3];

	if ( !tw.open )
		return;
	tw.hits.num = 0;
	tw.x = x = ( SCREEN_WIDTH - TW_W ) * 0.5f;
	tw.y = y = TW_TOP;
	right = x + TW_PAD + TW_LIST_W + TW_PAD;

	// a drag goes on with the pointer while the button is held
	if ( tw.drag == TW_COLOR ) {
		TW_Color( rgb );
		rgb[tw.dragArg] = (int)( Win_BarFraction( tw.dragX, TW_BAR_W ) * 255.0f + 0.5f );
		trap->Cvar_Set( "v_toolColor", va( "%i %i %i", rgb[0], rgb[1], rgb[2] ) );
	} else if ( tw.drag == TW_PHYS ) {
		const float f = Win_BarFraction( tw.dragX, TW_BAR_W );

		TW_Phys( phys );
		phys[tw.dragArg] = ( tw.dragArg == 0 ) ? TW_MASS_MIN * powf( 10.0f, 3.0f * f ) : f * ( tw.dragArg == 1 ? 0.95f : 2.0f );
		trap->Cvar_Set( "v_toolPhys", va( "%.2f %.2f %.2f", phys[0], phys[1], phys[2] ) );
	}

	Win_Panel( x, y, TW_W, TW_H, 6.0f, winFill, winEdge );
	Win_Hit( &tw.hits, -1, 0, x, y, TW_W, TW_H );
	Win_Text( x + TW_PAD, y + 8.0f, 0.7f, winAccent, 0, CG_VisionText( "Tools" ), 0.0f );
	Win_Button( &tw.hits, TW_CLOSE, 0, x + TW_W - TW_PAD - 18.0f, y + 8.0f, 18.0f, "x", winText, qtrue );
	Win_Line( x + TW_PAD, y + 32.0f, TW_W - 2.0f * TW_PAD );

	for ( i = 0; i < NUM_TOOLS; i++ ) {
		Win_Button( &tw.hits, TW_TOOL, i, x + TW_PAD, y + 42.0f + i * ( WIN_BUTTON_H + 6.0f ), TW_LIST_W, CG_VisionText( titles[i] ),
			i == tool ? winGold : winText, qtrue );
	}

	y += 42.0f;
	Win_Text( right, y, 0.6f, winGold, 0, CG_VisionText( titles[tool] ), TW_W - TW_LIST_W - 3.0f * TW_PAD );
	y += 24.0f;
	switch ( tool ) {
	case TOOL_COLOR: {
		static const char *const channels[3] = { "Red", "Green", "Blue" };
		vec4_t swatch;

		TW_Color( rgb );
		for ( i = 0; i < 3; i++ ) {
			TW_BarRow( TW_COLOR, i, right, y, CG_VisionText( channels[i] ), va( "%i", rgb[i] ), rgb[i] / 255.0f );
			y += TW_ROW;
		}
		for ( i = 0; i < 8; i++ ) {
			const float	px = right + i * 26.0f;
			vec4_t		c;

			VectorSet4( c, twColors[i][0] / 255.0f, twColors[i][1] / 255.0f, twColors[i][2] / 255.0f, 1.0f );
			CG_FillRect( px, y, 20.0f, 16.0f, c );
			if ( Win_Over( px, y, 20.0f, 16.0f ) )
				CG_DrawRect( px - 1.0f, y - 1.0f, 22.0f, 18.0f, 1.0f, (float *)winAccent );
			Win_Hit( &tw.hits, TW_PRESET, i, px, y, 20.0f, 16.0f );
		}
		VectorSet4( swatch, rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f );
		CG_FillRect( right + 8 * 26.0f + 10.0f, y - 2.0f, 40.0f, 20.0f, swatch );
		y += 28.0f;
		break;
	}
	case TOOL_MATERIAL: {
		char current[MAX_QPATH];

		trap->Cvar_VariableStringBuffer( "v_toolMaterial", current, sizeof( current ) );
		for ( i = 0; i < NUM_TOOL_MATERIALS; i++ ) {
			const float bx = right + ( i % 3 ) * 96.0f, by = y + ( i / 3 ) * ( WIN_BUTTON_H + 6.0f );

			Win_Button( &tw.hits, TW_MATERIAL, i, bx, by, 90.0f, CG_VisionText( toolMaterials[i].title ),
				!Q_stricmp( current, toolMaterials[i].name ) ? winGold : winText, qtrue );
		}
		y += ( ( NUM_TOOL_MATERIALS + 2 ) / 3 ) * ( WIN_BUTTON_H + 6.0f ) + 6.0f;
		break;
	}
	case TOOL_PHYSPROP: {
		static const char *const names[3] = { "Mass", "Bounce", "Grip" };

		TW_Phys( phys );
		TW_BarRow( TW_PHYS, 0, right, y, CG_VisionText( names[0] ), va( "%.1f", phys[0] ),
			log10f( Q_max( TW_MASS_MIN, phys[0] ) / TW_MASS_MIN ) / 3.0f );
		y += TW_ROW;
		TW_BarRow( TW_PHYS, 1, right, y, CG_VisionText( names[1] ), va( "%.2f", phys[1] ), phys[1] / 0.95f );
		y += TW_ROW;
		TW_BarRow( TW_PHYS, 2, right, y, CG_VisionText( names[2] ), va( "%.2f", phys[2] ), phys[2] / 2.0f );
		y += TW_ROW;
		for ( i = 0; i < 4; i++ ) {
			Win_Button( &tw.hits, TW_PHYS_PRESET, i, right + i * 72.0f, y, 66.0f, CG_VisionText( twPhysPresets[i].name ), winText, qtrue );
		}
		y += WIN_BUTTON_H + 10.0f;
		break;
	}
	}
	Win_Text( right, y, 0.48f, winDim, 0, CG_VisionText( about[tool] ), TW_W - TW_LIST_W - 3.0f * TW_PAD );
	Win_Text( x + TW_W * 0.5f, TW_TOP + TW_H - 20.0f, 0.46f, winDim, 2, CG_VisionText( "Left click: use   Right click: take   R: reset" ),
		TW_W - 2.0f * TW_PAD );
}

/*
=================
CG_ToolWindowKey

A key while it is open: qtrue, it took it
=================
*/
qboolean CG_ToolWindowKey( int key, qboolean down ) {
	int at;

	if ( !tw.open )
		return qfalse;
	if ( key == A_ESCAPE || key == A_MOUSE2 ) {
		if ( down )
			CG_ToolWindowClose();
		return qtrue;
	}
	if ( key != A_MOUSE1 )
		return qfalse;
	if ( !down ) {
		tw.drag = TW_CLOSE;
		return qtrue;
	}
	if ( ( at = Win_HitAt( &tw.hits ) ) < 0 )
		return qfalse;
	switch ( tw.hits.hit[at].part ) {
	case TW_CLOSE:
		CG_ToolWindowClose();
		break;
	case TW_TOOL:
		trap->Cvar_Set( "v_tool", toolNames[tw.hits.hit[at].arg] );
		break;
	case TW_COLOR:
	case TW_PHYS:
		tw.drag = tw.hits.hit[at].part;
		tw.dragArg = tw.hits.hit[at].arg;
		tw.dragX = tw.hits.hit[at].x + WIN_GRAB;
		break;
	case TW_PRESET: {
		const byte *c = twColors[tw.hits.hit[at].arg];

		trap->Cvar_Set( "v_toolColor", va( "%i %i %i", c[0], c[1], c[2] ) );
		break;
	}
	case TW_MATERIAL:
		trap->Cvar_Set( "v_toolMaterial", toolMaterials[tw.hits.hit[at].arg].name );
		break;
	case TW_PHYS_PRESET: {
		const int n = tw.hits.hit[at].arg;

		trap->Cvar_Set( "v_toolPhys", va( "%g %g %g", twPhysPresets[n].mass, twPhysPresets[n].bounce, twPhysPresets[n].grip ) );
		break;
	}
	}
	return qtrue;
}
