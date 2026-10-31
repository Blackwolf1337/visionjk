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

// cg_tools.c -- the toolgun of the sandbox on the client (issue #3, game/props/g_tools.c): the tool and its
// settings are cvars, which the tool window (cg_tools_ui.c) sets and this file sends to the server as vs_tool,
// and the line under the crosshair while the toolgun is in hand. Not from JA+, see docs/japlus/sandbox.md.
//
//   v_tool          remover, color, material, physprop or freeze
//   v_toolColor     "<r> <g> <b>", 0 to 255
//   v_toolMaterial  a name of bg_tools.h
//   v_toolPhys      "<mass> <bounce> <grip>"
//
// The server answers every vs_tool with what it holds (a server command vs_tool with all of it), also when alt
// attack took a colour, a material or properties from a prop. That comes into the cvars here, and is not sent
// back: lastSent is what the server holds as far as this client knows.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "game/props/bg_tools.h"

static vmCvar_t	v_tool, v_toolColor, v_toolMaterial, v_toolPhys;
static char		lastSent[MAX_STRING_CHARS];

static const vec4_t toolName = { 0.43f, 0.83f, 1.0f, 1.0f }, toolDim = { 0.86f, 0.92f, 0.97f, 0.85f };
static const vec4_t toolShade = { 0.035f, 0.07f, 0.12f, 0.6f };

void CG_ToolsInit( void ) {
	trap->Cvar_Register( &v_tool, "v_tool", "remover", 0 );
	trap->Cvar_Register( &v_toolColor, "v_toolColor", "255 255 255", 0 );
	trap->Cvar_Register( &v_toolMaterial, "v_toolMaterial", "plain", 0 );
	trap->Cvar_Register( &v_toolPhys, "v_toolPhys", "1 0.25 0.7", 0 );
	lastSent[0] = '\0';
}

// the number of the tool v_tool names, the remover for anything else
static int CG_ToolOf( const char *name ) {
	int tool;

	for ( tool = 0; tool < NUM_TOOLS; tool++ ) {
		if ( !Q_stricmp( name, toolNames[tool] ) )
			return tool;
	}
	return TOOL_REMOVER;
}

// the command that tells the server the tool of these cvars, with what it needs of them
static void CG_ToolCommand( char *out, int outSize, const char *tool, const char *color, const char *material, const char *phys ) {
	switch ( CG_ToolOf( tool ) ) {
	case TOOL_COLOR:
		Com_sprintf( out, outSize, "vs_tool color %s", color );
		break;
	case TOOL_MATERIAL:
		Com_sprintf( out, outSize, "vs_tool material %s", material );
		break;
	case TOOL_PHYSPROP:
		Com_sprintf( out, outSize, "vs_tool physprop %s", phys );
		break;
	default:
		Com_sprintf( out, outSize, "vs_tool %s", toolNames[CG_ToolOf( tool )] );
		break;
	}
}

static qboolean CG_ToolSandbox( void ) {
	return ( cg.snap && ( cg.predictedPlayerState.stats[STAT_CARRY] & CARRY_PHYSGUN ) ) ? qtrue : qfalse;
}

/*
=================
CG_ToolsFrame

Once a frame: a tool or a setting the window changed goes to the server, while he has the sandbox. When he gets
it, the server learns his tool that way too.
=================
*/
void CG_ToolsFrame( void ) {
	char cmd[MAX_STRING_CHARS];

	trap->Cvar_Update( &v_tool );
	trap->Cvar_Update( &v_toolColor );
	trap->Cvar_Update( &v_toolMaterial );
	trap->Cvar_Update( &v_toolPhys );
	if ( !CG_ToolSandbox() ) {
		lastSent[0] = '\0';
		return;
	}
	CG_ToolCommand( cmd, sizeof( cmd ), v_tool.string, v_toolColor.string, v_toolMaterial.string, v_toolPhys.string );
	if ( strcmp( cmd, lastSent ) ) {
		Q_strncpyz( lastSent, cmd, sizeof( lastSent ) );
		trap->SendClientCommand( cmd );
	}
}

/*
=================
CG_ToolServerCommand

vs_tool <tool> <r> <g> <b> <material> <mass> <bounce> <grip>: what the server holds
=================
*/
void CG_ToolServerCommand( void ) {
	char tool[MAX_QPATH], color[64], material[MAX_QPATH], phys[64];

	if ( trap->Cmd_Argc() < 9 )
		return;
	Q_strncpyz( tool, CG_Argv( 1 ), sizeof( tool ) );
	Com_sprintf( color, sizeof( color ), "%i %i %i", atoi( CG_Argv( 2 ) ), atoi( CG_Argv( 3 ) ), atoi( CG_Argv( 4 ) ) );
	Q_strncpyz( material, CG_Argv( 5 ), sizeof( material ) );
	Com_sprintf( phys, sizeof( phys ), "%g %g %g", atof( CG_Argv( 6 ) ), atof( CG_Argv( 7 ) ), atof( CG_Argv( 8 ) ) );
	trap->Cvar_Set( "v_tool", tool );
	trap->Cvar_Set( "v_toolColor", color );
	trap->Cvar_Set( "v_toolMaterial", material );
	trap->Cvar_Set( "v_toolPhys", phys );
	CG_ToolCommand( lastSent, sizeof( lastSent ), tool, color, material, phys );
}

// the name of a tool in the language of the game
static const char *CG_ToolTitle( int tool ) {
	static const char *const titles[NUM_TOOLS] = { "Remover", "Color", "Material", "Physical Properties", "Freeze" };

	return CG_VisionText( titles[tool] );
}

/*
=================
CG_ToolsDraw

Under the crosshair while the toolgun is in hand: the tool and what it does now
=================
*/
void CG_ToolsDraw( void ) {
	const int	font = Skin_Ready() ? SKIN_FONT_SANS : FONT_SMALL2;
	const float	scale = 0.55f, x = 320.0f, y = 262.0f;
	int			tool, rgb[3] = { 255, 255, 255 };
	float		mass = 1.0f, bounce = 0.25f, grip = 0.7f;
	const char	*title, *detail = "";
	vec4_t		swatch;
	float		w;

	if ( !CG_ToolSandbox() || cg.predictedPlayerState.weapon != WP_BRYAR_PISTOL || cg.showScores || ( trap->Key_GetCatcher() & KEYCATCH_UI ) )
		return;
	tool = CG_ToolOf( v_tool.string );
	title = CG_ToolTitle( tool );
	switch ( tool ) {
	case TOOL_COLOR:
		sscanf( v_toolColor.string, "%3i %3i %3i", &rgb[0], &rgb[1], &rgb[2] );
		break;
	case TOOL_MATERIAL:
		detail = CG_VisionText( v_toolMaterial.string );
		break;
	case TOOL_PHYSPROP:
		sscanf( v_toolPhys.string, "%15f %15f %15f", &mass, &bounce, &grip );
		detail = va( "%s %g   %s %g   %s %g", CG_VisionText( "Mass" ), mass, CG_VisionText( "Bounce" ), bounce,
			CG_VisionText( "Grip" ), grip );
		break;
	}

	w = CG_Text_Width( title, scale, font ) + ( detail[0] ? 12.0f + CG_Text_Width( detail, scale, font ) : 0.0f )
		+ ( tool == TOOL_COLOR ? 22.0f : 0.0f );
	CG_FillRect( x - w * 0.5f - 6.0f, y - 2.0f, w + 12.0f, 18.0f, toolShade );
	CG_Text_Paint( x - w * 0.5f, y, scale, (float *)toolName, title, 0, 0, ITEM_TEXTSTYLE_SHADOWED, font );
	if ( tool == TOOL_COLOR ) {
		VectorSet4( swatch, rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f );
		CG_FillRect( x + w * 0.5f - 14.0f, y + 2.0f, 14.0f, 11.0f, swatch );
	} else if ( detail[0] ) {
		CG_Text_Paint( x + w * 0.5f - CG_Text_Width( detail, scale, font ), y, scale, (float *)toolDim, detail, 0, 0,
			ITEM_TEXTSTYLE_SHADOWED, font );
	}
}
