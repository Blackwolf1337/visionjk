/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// cl_matdebug.cpp: the material debug menu (v_matdebug). The shaders of the level by area or the one under the
// crosshair, their stages, and the material of a stage changed live through the renderer (ext.Material*), so what
// is set here is what the game shows. Needs the renderer rd-vulkan-vision.

#include "client.h"
#include "cl_imgui.h"
#include "imgui/imgui.h"

#define MAX_LISTED		16384

typedef struct {
	qhandle_t	shader;
	char		name[MAX_QPATH];
	float		area;
	int			flags;
} listedShader_t;

static debugWindow_t	matWindow;

static listedShader_t	listed[MAX_LISTED];
static int				numListed = -1;		// -1: made again on the next frame
static int				listedTotal;		// what MaterialList answered for it
static qboolean			listAll;
static char				filter[64];

static qhandle_t		selShader;
static char				selName[MAX_QPATH];
static int				selStage = -1;
static materialParms_t	selParms, selOriginal;
static char				editNormal[MAX_QPATH], editRmo[MAX_QPATH];

/*
===============
The list
===============
*/
static void MatDebug_MakeList( void ) {
	static qhandle_t handles[MAX_LISTED];
	listedTotal = re->ext.MaterialList( listAll, 0, handles, MAX_LISTED );

	const int count = Q_min( listedTotal, MAX_LISTED );

	numListed = 0;
	for ( int i = 0; i < count; i++ ) {
		materialInfo_t info;

		if ( !re->ext.MaterialInfo( handles[i], &info ) ) {
			continue;
		}
		listedShader_t *l = &listed[numListed++];

		l->shader = handles[i];
		Q_strncpyz( l->name, info.name, sizeof( l->name ) );
		l->area = info.area;
		l->flags = info.flags;
	}
}

static void MatDebug_SelectStage( int sourceStage ) {
	selStage = sourceStage;
	re->ext.MaterialGet( selShader, selStage, &selParms );
	selOriginal = selParms;
	Q_strncpyz( editNormal, selParms.normalMap, sizeof( editNormal ) );
	Q_strncpyz( editRmo, selParms.rmoMap, sizeof( editRmo ) );
}

static void MatDebug_Select( qhandle_t shader ) {
	materialInfo_t info;

	selShader = 0;
	selStage = -1;
	selName[0] = '\0';
	if ( !shader || !re->ext.MaterialInfo( shader, &info ) ) {
		return;
	}
	selShader = shader;
	Q_strncpyz( selName, info.name, sizeof( selName ) );
	// the stage with a material, else the first that is not the lightmap
	for ( int i = 0; i < info.numStages; i++ ) {
		if ( info.stages[i].flags & MATS_MATERIAL ) {
			MatDebug_SelectStage( info.stages[i].sourceStage );
			return;
		}
	}
	for ( int i = 0; i < info.numStages; i++ ) {
		if ( Q_stricmp( info.stages[i].image, "$lightmap" ) ) {
			MatDebug_SelectStage( info.stages[i].sourceStage );
			return;
		}
	}
}

static void MatDebug_Pick( void ) {
	materialPick_t pick;

	if ( re->ext.MaterialPick( NULL, NULL, &pick ) ) {
		MatDebug_Select( pick.shader );
	}
}

static void MatDebug_DrawList( void ) {
	if ( ImGui::Button( "Pick under crosshair (P)" ) ) {
		MatDebug_Pick();
	}
	ImGui::SameLine();
	bool all = listAll ? true : false;
	if ( ImGui::Checkbox( "All loaded", &all ) ) {
		listAll = all ? qtrue : qfalse;
		numListed = -1;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Refresh" ) ) {
		numListed = -1;
	}
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputTextWithHint( "##filter", "filter by name", filter, sizeof( filter ) );

	if ( ImGui::BeginTable( "shaders", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) ) {
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "Shader", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "Area m2", ImGuiTableColumnFlags_WidthFixed );
		ImGui::TableHeadersRow();
		for ( int i = 0; i < numListed; i++ ) {
			const listedShader_t *l = &listed[i];

			if ( filter[0] && !Q_stristr( l->name, filter ) ) {
				continue;
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID( i );
			if ( ImGui::Selectable( l->name, l->shader == selShader, ImGuiSelectableFlags_SpanAllColumns ) ) {
				MatDebug_Select( l->shader );
			}
			ImGui::PopID();
			ImGui::TableNextColumn();
			// a unit of the level is about 2.5 cm (a player is 64 high): 1600 square units are a square metre
			ImGui::Text( "%.1f", l->area / 1600.0f );
		}
		ImGui::EndTable();
	}
}

/*
===============
The material of the stage
===============
*/
static void MatDebug_Set( int fields ) {
	re->ext.MaterialSet( selShader, selStage, &selParms, fields );
}

static void MatDebug_DrawMaterial( void ) {
	bool pbr = selParms.pbr ? true : false;

	if ( ImGui::Checkbox( "Material (lit per pixel, GGX)", &pbr ) ) {
		selParms.pbr = pbr ? qtrue : qfalse;
		MatDebug_Set( MATP_PBR | MATP_ROUGHNESS );
	}
	if ( ImGui::SliderFloat( "Roughness", &selParms.roughness, 0.0f, 1.0f, "%.2f" ) ) {
		MatDebug_Set( MATP_ROUGHNESS );
	}
	if ( ImGui::SliderFloat( "Metalness", &selParms.metalness, 0.0f, 1.0f, "%.2f" ) ) {
		MatDebug_Set( MATP_METALNESS );
	}
	if ( ImGui::SliderFloat( "Parallax depth", &selParms.parallaxDepth, 0.0f, 0.2f, "%.3f" ) ) {
		MatDebug_Set( MATP_PARALLAX );
	}
	if ( ImGui::SliderFloat( "Parallax plane", &selParms.parallaxPlane, 0.0f, 1.0f, "%.2f" ) ) {
		MatDebug_Set( MATP_PARALLAX );
	}
	ImGui::SetItemTooltip( "Where the polygon lies in the height of the parallax.\n1: the surface is carved into it, feet float over its hollows.\n0: the surface rises out of it, feet stand in the bumps." );

	ImGui::SeparatorText( "Maps" );
	bool height = selParms.normalHeight ? true : false;
	if ( ImGui::InputText( "Normal map", editNormal, sizeof( editNormal ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
		Q_strncpyz( selParms.normalMap, editNormal, sizeof( selParms.normalMap ) );
		MatDebug_Set( MATP_NORMALMAP );
		re->ext.MaterialGet( selShader, selStage, &selParms );
	}
	if ( ImGui::Checkbox( "Height in its alpha (for parallax)", &height ) ) {
		selParms.normalHeight = height ? qtrue : qfalse;
		MatDebug_Set( MATP_NORMALMAP );
	}
	bool orm = selParms.ormOrder ? true : false;
	if ( ImGui::InputText( "RMO map", editRmo, sizeof( editRmo ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
		Q_strncpyz( selParms.rmoMap, editRmo, sizeof( selParms.rmoMap ) );
		MatDebug_Set( MATP_RMOMAP );
		re->ext.MaterialGet( selShader, selStage, &selParms );
	}
	if ( ImGui::Checkbox( "Occlusion first (ORM)", &orm ) ) {
		selParms.ormOrder = orm ? qtrue : qfalse;
		MatDebug_Set( MATP_RMOMAP );
	}
	ImGui::TextDisabled( "Enter applies a map name, empty takes it off" );

	if ( ImGui::CollapsingHeader( "Shine of model stages (not GGX)" ) ) {
		if ( ImGui::SliderFloat3( "Strength, exponent, coat", selParms.specular, 0.0f, 64.0f, "%.2f" ) ) {
			MatDebug_Set( MATP_SPECULAR );
		}
		if ( ImGui::SliderFloat( "Wrap light", &selParms.wrapLight, 0.0f, 1.0f, "%.2f" ) ) {
			MatDebug_Set( MATP_WRAPLIGHT );
		}
		if ( ImGui::SliderFloat( "Sparkle", &selParms.sparkle, 0.0f, 4.0f, "%.2f" ) ) {
			MatDebug_Set( MATP_SPARKLE );
		}
	}

	ImGui::Separator();
	if ( ImGui::Button( "Revert" ) ) {
		selParms = selOriginal;
		Q_strncpyz( editNormal, selParms.normalMap, sizeof( editNormal ) );
		Q_strncpyz( editRmo, selParms.rmoMap, sizeof( editRmo ) );
		MatDebug_Set( MATP_ALL );
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "back to what it was when picked" );
}

static void MatDebug_DrawShader( void ) {
	materialInfo_t info;

	if ( !selShader || !re->ext.MaterialInfo( selShader, &info ) || Q_stricmp( info.name, selName ) ) {
		// nothing picked, or the level changed under it
		selShader = 0;
		ImGui::TextDisabled( "Pick a surface (P) or choose a shader in the list" );
		return;
	}
	ImGui::TextUnformatted( info.name );
	ImGui::TextDisabled( "%s%s%s%s  %.1f m2", ( info.flags & MATI_SCRIPT ) ? "shader text" : "no shader text (implicit)",
		( info.flags & MATI_SKY ) ? ", sky" : "", ( info.flags & MATI_FOG ) ? ", fog" : "", ( info.flags & MATI_NODRAW ) ? ", not drawn" : "",
		info.area / 1600.0f );
	if ( info.emissive[3] > 0.0f || info.surfaceLight > 0.0f ) {
		ImGui::TextDisabled( "gives light: emissive %.0f, surfacelight %.0f", info.emissive[3], info.surfaceLight );
	}

	ImGui::SeparatorText( "Stages" );
	for ( int i = 0; i < info.numStages; i++ ) {
		const materialStage_t *st = &info.stages[i];
		char label[MAX_QPATH + 64];

		Com_sprintf( label, sizeof( label ), "%d  %s%s%s%s%s%s", st->sourceStage, st->image,
			( st->flags & MATS_MATERIAL ) ? "  [material]" : "", ( st->flags & MATS_LIGHTMAP ) ? "  lightmap" : "",
			( st->flags & MATS_ENTITY ) ? "  entity light" : "", ( st->flags & MATS_ADD ) ? "  adds" : "",
			( st->flags & MATS_GLOW ) ? "  glow" : "" );
		if ( ImGui::RadioButton( label, st->sourceStage == selStage ) ) {
			MatDebug_SelectStage( st->sourceStage );
		}
	}
	if ( selStage < 0 ) {
		return;
	}
	ImGui::SeparatorText( "Material of the stage" );
	MatDebug_DrawMaterial();
}

static void MatDebug_DrawViews( void ) {
	static const char *views[] = { "Off (as without materials)", "Materials", "Light alone", "Shine alone", "Normals",
		"Light direction of the grid", "Roughness, metalness, occlusion", "Which maps", "Mirror of the probes" };
	int view = Com_Clampi( 0, (int)ARRAY_LEN( views ) - 1, Cvar_VariableIntegerValue( "v_pbr" ) );

	ImGui::SetNextItemWidth( 260.0f );
	if ( ImGui::Combo( "View (v_pbr)", &view, views, (int)ARRAY_LEN( views ) ) ) {
		Cvar_Set( "v_pbr", va( "%d", view ) );
	}
	ImGui::SameLine();
	bool probes = Cvar_VariableIntegerValue( "v_pbrProbes" ) ? true : false;
	if ( ImGui::Checkbox( "Reflection probes", &probes ) ) {
		Cvar_Set( "v_pbrProbes", probes ? "1" : "0" );
	}
}

static void MatDebug_Draw( debugWindow_t *window ) {
	bool open = true;
	ImGuiIO &io = ImGui::GetIO();

	// made again when the level or the shaders changed
	if ( numListed < 0 || re->ext.MaterialList( listAll, 0, NULL, 0 ) != listedTotal ) {
		MatDebug_MakeList();
	}
	if ( !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_P, false ) ) {
		MatDebug_Pick();
	}

	ImGui::SetNextWindowSize( ImVec2( 980.0f, 640.0f ), ImGuiCond_FirstUseEver );
	if ( ImGui::Begin( "Materials", &open ) ) {
		MatDebug_DrawViews();
		ImGui::Separator();
		if ( ImGui::BeginChild( "list", ImVec2( ImGui::GetContentRegionAvail().x * 0.4f, 0.0f ), ImGuiChildFlags_ResizeX ) ) {
			MatDebug_DrawList();
		}
		ImGui::EndChild();
		ImGui::SameLine();
		if ( ImGui::BeginChild( "shader" ) ) {
			MatDebug_DrawShader();
		}
		ImGui::EndChild();
	}
	ImGui::End();
	if ( !open ) {
		CL_ImGui_Toggle( window );
	}
}

static void MatDebug_Toggle_f( void ) {
	if ( !re || !re->ext.MaterialList ) {
		Com_Printf( "The material debug menu needs the renderer rd-vulkan-vision\n" );
		return;
	}
	numListed = -1;
	CL_ImGui_Toggle( &matWindow );
}

void CL_MatDebug_Init( void ) {
	matWindow.name = "Materials";
	matWindow.draw = MatDebug_Draw;
	CL_ImGui_Register( &matWindow );
	Cmd_AddCommand( "v_matdebug", MatDebug_Toggle_f, "Opens or closes the material debug menu: the material of the surfaces live, needs rd-vulkan-vision" );
}
