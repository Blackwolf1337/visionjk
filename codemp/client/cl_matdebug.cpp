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
// is set here is what the game shows. Save writes the shader with its materials into shaders/zz_mat_<file>.oshader
// of the profile (ext.MaterialSave), which the next level reads after the shader files; how far a material is
// (draft, check, done) is kept in materials/status.txt. Needs the renderer rd-vulkan-vision.

#include "client.h"
#include "cl_imgui.h"
#include "imgui/imgui.h"
#include <map>
#include <string>

#define MAX_LISTED		16384
#define MAX_UNDO		64
#define STATUS_FILE		"materials/status.txt"

typedef enum {
	MS_NONE,
	MS_DRAFT,
	MS_CHECK,
	MS_DONE,
	MS_NUM
} matStatus_t;

static const char *const statusNames[MS_NUM] = { "none", "draft", "check", "done" };

typedef struct {
	qhandle_t	shader;
	char		name[MAX_QPATH];
	float		area;
	int			flags;
	qboolean	material;			// a stage has one
} listedShader_t;

// what a kind of surface is, from the usual values of physically based materials (roughness, metalness); the
// quality control of the materials checks them against measured references
typedef struct {
	const char	*name;
	float		roughness, metalness;
} matPreset_t;

static const matPreset_t presets[] = {
	{ "Concrete", 0.90f, 0.0f },
	{ "Plaster, stucco", 0.85f, 0.0f },
	{ "Brick", 0.85f, 0.0f },
	{ "Rock", 0.80f, 0.0f },
	{ "Sand", 0.95f, 0.0f },
	{ "Polished stone, marble", 0.20f, 0.0f },
	{ "Glazed tile", 0.25f, 0.0f },
	{ "Wood", 0.70f, 0.0f },
	{ "Varnished wood", 0.35f, 0.0f },
	{ "Fabric", 0.95f, 0.0f },
	{ "Rubber", 0.90f, 0.0f },
	{ "Plastic", 0.45f, 0.0f },
	{ "Painted metal", 0.50f, 0.0f },
	{ "Rusty metal", 0.75f, 0.3f },
	{ "Brushed steel", 0.45f, 1.0f },
	{ "Bare steel", 0.35f, 1.0f },
	{ "Aluminium", 0.30f, 1.0f },
	{ "Chrome", 0.08f, 1.0f },
	{ "Gold, brass", 0.25f, 1.0f },
};

static debugWindow_t	matWindow;

static listedShader_t	listed[MAX_LISTED];
static int				numListed = -1;		// -1: made again on the next frame
static int				listedTotal;		// what MaterialList answered for it
static qboolean			listAll, listMaterialOnly;
static int				listStatus = -1;	// -1 all, else matStatus_t
static char				filter[64];

static qhandle_t		selShader;
static char				selName[MAX_QPATH];
static int				selStage = -1;
static float			selLuma;			// how bright its texture is
static materialParms_t	selParms, selOriginal, selStable;
static char				editNormal[MAX_QPATH], editRmo[MAX_QPATH];
static qboolean			showOriginal;

static materialParms_t	undoStack[MAX_UNDO];
static int				undoCount, undoPos;	// undoStack[0 .. undoPos) is undone by Undo, up to undoCount by Redo

static materialParms_t	clipboard;
static qboolean			clipboardSet;

static char				message[256];
static qboolean			messageBad;

static std::map<std::string, int>	statuses;
static qboolean			statusesRead;

/*
===============
Status of the materials (materials/status.txt: a line "<shader> <status>")
===============
*/
static std::string MatDebug_Key( const char *name ) {
	char lower[MAX_QPATH];

	Q_strncpyz( lower, name, sizeof( lower ) );
	Q_strlwr( lower );
	return std::string( lower );
}

static void MatDebug_ReadStatuses( void ) {
	void *buffer = NULL;
	const long length = FS_ReadFile( STATUS_FILE, &buffer );

	statusesRead = qtrue;
	statuses.clear();
	if ( length > 0 && buffer ) {
		const char *p = (const char *)buffer;
		char name[MAX_QPATH];

		while ( 1 ) {
			const char *token = COM_ParseExt( &p, qtrue );

			if ( !token[0] ) {
				break;
			}
			Q_strncpyz( name, token, sizeof( name ) );
			token = COM_ParseExt( &p, qfalse );
			for ( int s = MS_DRAFT; s < MS_NUM; s++ ) {
				if ( !Q_stricmp( token, statusNames[s] ) ) {
					statuses[MatDebug_Key( name )] = s;
				}
			}
		}
	}
	if ( buffer ) {
		FS_FreeFile( buffer );
	}
}

static void MatDebug_WriteStatuses( void ) {
	std::string text = "// how far the materials of the material debug menu (v_matdebug) are: draft, check (for a person to look at), done\n";

	for ( const auto &entry : statuses ) {
		text += entry.first + " " + statusNames[entry.second] + "\n";
	}
	FS_WriteFile( STATUS_FILE, text.c_str(), (int)text.size() );
}

static int MatDebug_Status( const char *name ) {
	const auto it = statuses.find( MatDebug_Key( name ) );

	return it == statuses.end() ? MS_NONE : it->second;
}

static void MatDebug_SetStatus( const char *name, int status ) {
	if ( status == MS_NONE ) {
		statuses.erase( MatDebug_Key( name ) );
	} else {
		statuses[MatDebug_Key( name )] = status;
	}
	MatDebug_WriteStatuses();
}

static void MatDebug_Message( qboolean bad, const char *fmt, ... ) {
	va_list args;

	va_start( args, fmt );
	Q_vsnprintf( message, sizeof( message ), fmt, args );
	va_end( args );
	messageBad = bad;
}

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
		l->material = qfalse;
		for ( int s = 0; s < info.numStages; s++ ) {
			if ( info.stages[s].flags & MATS_MATERIAL ) {
				l->material = qtrue;
			}
		}
	}
}

/*
===============
The selection, undo and redo
===============
*/
static void MatDebug_EditFields( void ) {
	Q_strncpyz( editNormal, selParms.normalMap, sizeof( editNormal ) );
	Q_strncpyz( editRmo, selParms.rmoMap, sizeof( editRmo ) );
}

static void MatDebug_SelectStage( int sourceStage ) {
	materialInfo_t info;

	selStage = sourceStage;
	re->ext.MaterialGet( selShader, selStage, &selParms );
	selOriginal = selStable = selParms;
	undoCount = undoPos = 0;
	showOriginal = qfalse;
	selLuma = 0.0f;
	if ( re->ext.MaterialInfo( selShader, &info ) ) {
		for ( int i = 0; i < info.numStages; i++ ) {
			if ( info.stages[i].sourceStage == sourceStage ) {
				selLuma = info.stages[i].luma;
			}
		}
	}
	MatDebug_EditFields();
}

static void MatDebug_Select( qhandle_t shader ) {
	materialInfo_t info;

	selShader = 0;
	selStage = -1;
	selName[0] = '\0';
	message[0] = '\0';
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

static void MatDebug_Set( int fields ) {
	re->ext.MaterialSet( selShader, selStage, &selParms, fields );
}

// all of a material at once (undo, redo, paste, revert); the maps are made again only when what they are made of
// changed
static void MatDebug_Apply( const materialParms_t *parms ) {
	const qboolean genChanged = memcmp( &parms->gen, &selParms.gen, sizeof( parms->gen ) ) ? qtrue : qfalse;

	selParms = *parms;
	MatDebug_Set( genChanged ? MATP_ALL : ( MATP_ALL & ~MATP_GEN ) );
	re->ext.MaterialGet( selShader, selStage, &selParms );
	selStable = selParms;
	MatDebug_EditFields();
}

// once nothing is held any more, a change since the last stable state is one step to undo (a whole drag is one)
static void MatDebug_TrackUndo( void ) {
	if ( ImGui::IsAnyItemActive() || showOriginal || !memcmp( &selStable, &selParms, sizeof( selParms ) ) ) {
		return;
	}
	if ( undoPos == MAX_UNDO ) {
		memmove( undoStack, undoStack + 1, sizeof( undoStack[0] ) * ( MAX_UNDO - 1 ) );
		undoPos--;
	}
	undoStack[undoPos++] = selStable;
	undoCount = undoPos;
	selStable = selParms;
}

static void MatDebug_Undo( void ) {
	if ( undoPos <= 0 ) {
		return;
	}
	const materialParms_t back = undoStack[undoPos - 1];

	undoStack[undoPos - 1] = selParms;
	undoPos--;
	MatDebug_Apply( &back );
}

static void MatDebug_Redo( void ) {
	if ( undoPos >= undoCount ) {
		return;
	}
	const materialParms_t forward = undoStack[undoPos];

	undoStack[undoPos] = selParms;
	undoPos++;
	MatDebug_Apply( &forward );
}

static void MatDebug_Save( void ) {
	char path[MAX_QPATH], err[256];

	if ( showOriginal ) {
		MatDebug_Message( qtrue, "Showing the original: switch it off to save" );
		return;
	}
	if ( !re->ext.MaterialSave( selShader, path, sizeof( path ), err, sizeof( err ) ) ) {
		MatDebug_Message( qtrue, "Not saved: %s", err );
		return;
	}
	if ( MatDebug_Status( selName ) == MS_NONE ) {
		MatDebug_SetStatus( selName, MS_DRAFT );
	}
	selOriginal = selParms;
	MatDebug_Message( qfalse, "Saved into %s, the next level loads it", path );
}

/*
===============
Warnings: values a physically based material does not have
===============
*/
static void MatDebug_Warn( const char *text ) {
	ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "%s", text );
}

static void MatDebug_DrawWarnings( void ) {
	const materialParms_t *m = &selParms;
	const float srgb = selLuma * 255.0f;

	if ( !m->pbr ) {
		return;
	}
	if ( selLuma > 0.0f && m->metalness >= 0.5f && srgb < 186.0f ) {
		MatDebug_Warn( va( "Metal with a dark texture (average %.0f of 255): real metals reflect 186 and more", srgb ) );
	}
	if ( selLuma > 0.0f && m->metalness < 0.5f && srgb < 30.0f ) {
		MatDebug_Warn( va( "Texture darker than any real surface (average %.0f of 255, at least 30)", srgb ) );
	}
	if ( selLuma > 0.0f && m->metalness < 0.5f && srgb > 240.0f ) {
		MatDebug_Warn( va( "Texture brighter than any real surface (average %.0f of 255, at most 240)", srgb ) );
	}
	if ( m->metalness > 0.1f && m->metalness < 0.9f && !( m->gen.flags & MATGEN_RMO ) && !m->rmoMap[0] ) {
		MatDebug_Warn( "Metalness between 0 and 1 for the whole surface: a surface is metal or not, mixed only where rust or paint covers it" );
	}
	if ( m->roughness < 0.1f && m->metalness < 0.5f ) {
		MatDebug_Warn( "Very glossy for something that is not metal: only polished stone, glass, water and lacquer are" );
	}
	if ( m->parallaxDepth > 0.04f ) {
		MatDebug_Warn( "Deep parallax: on a floor feet seem to stand in or above it, keep floors at 0.02 or less" );
	}
}

/*
===============
The material of the stage
===============
*/

// the maps are made again (a moment for a large texture), what the stage has now is read back
static void MatDebug_SetGen( void ) {
	if ( selParms.gen.flags ) {
		selParms.pbr = qtrue;
	}
	MatDebug_Set( MATP_GEN | MATP_PBR | MATP_ROUGHNESS );
	re->ext.MaterialGet( selShader, selStage, &selParms );
	MatDebug_EditFields();
}

static void MatDebug_GenFlag( const char *label, int flag ) {
	bool on = ( selParms.gen.flags & flag ) ? true : false;

	if ( ImGui::Checkbox( label, &on ) ) {
		selParms.gen.flags = on ? ( selParms.gen.flags | flag ) : ( selParms.gen.flags & ~flag );
		MatDebug_SetGen();
	}
}

// a value of the made maps: they are made again when the slider is let go
static void MatDebug_GenSlider( const char *label, float *value, float low, float high ) {
	ImGui::SliderFloat( label, value, low, high, "%.2f" );
	if ( ImGui::IsItemDeactivatedAfterEdit() ) {
		MatDebug_SetGen();
	}
}

static void MatDebug_DrawGen( void ) {
	materialGen_t *g = &selParms.gen;

	ImGui::SeparatorText( "Made from the texture" );
	ImGui::TextDisabled( "Maps the renderer makes when it loads (albedoGen, normalGen, rmoGen): no picture files" );

	MatDebug_GenFlag( "Albedo: painted light taken out, tinted", MATGEN_ALBEDO );
	if ( g->flags & MATGEN_ALBEDO ) {
		ImGui::Indent();
		ImGui::ColorEdit3( "Tint", g->tint, ImGuiColorEditFlags_NoInputs );
		if ( ImGui::IsItemDeactivatedAfterEdit() ) {
			MatDebug_SetGen();
		}
		MatDebug_GenSlider( "Delight", &g->delight, 0.0f, 1.0f );
		MatDebug_GenSlider( "Saturation", &g->saturation, 0.0f, 2.0f );
		ImGui::Unindent();
	}

	MatDebug_GenFlag( "Normal map from the brightness", MATGEN_NORMAL );
	if ( g->flags & MATGEN_NORMAL ) {
		ImGui::Indent();
		MatDebug_GenSlider( "Strength", &g->normalStrength, 0.0f, 8.0f );
		MatDebug_GenSlider( "Smooth", &g->normalBlur, 0.0f, 8.0f );
		MatDebug_GenSlider( "Fine detail", &g->normalDetail, 0.0f, 1.0f );
		bool invert = g->normalInvert ? true : false;
		if ( ImGui::Checkbox( "Dark stands out", &invert ) ) {
			g->normalInvert = invert ? qtrue : qfalse;
			MatDebug_SetGen();
		}
		ImGui::Unindent();
	}

	MatDebug_GenFlag( "Roughness varies with it, occlusion in its cracks", MATGEN_RMO );
	if ( g->flags & MATGEN_RMO ) {
		ImGui::Indent();
		MatDebug_GenSlider( "Variation", &g->rmoVariation, 0.0f, 1.0f );
		MatDebug_GenSlider( "Occlusion", &g->rmoOcclusion, 0.0f, 1.0f );
		ImGui::Unindent();
	}
}

static void MatDebug_DrawFiles( void ) {
	bool height = selParms.normalHeight ? true : false;
	bool orm = selParms.ormOrder ? true : false;

	ImGui::SeparatorText( "Map files" );
	ImGui::BeginDisabled( ( selParms.gen.flags & MATGEN_NORMAL ) != 0 );
	if ( ImGui::InputText( "Normal map", editNormal, sizeof( editNormal ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
		Q_strncpyz( selParms.normalMap, editNormal, sizeof( selParms.normalMap ) );
		MatDebug_Set( MATP_NORMALMAP );
		re->ext.MaterialGet( selShader, selStage, &selParms );
	}
	if ( ImGui::Checkbox( "Height in its alpha (for parallax)", &height ) ) {
		selParms.normalHeight = height ? qtrue : qfalse;
		MatDebug_Set( MATP_NORMALMAP );
	}
	ImGui::EndDisabled();
	ImGui::BeginDisabled( ( selParms.gen.flags & MATGEN_RMO ) != 0 );
	if ( ImGui::InputText( "RMO map", editRmo, sizeof( editRmo ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
		Q_strncpyz( selParms.rmoMap, editRmo, sizeof( selParms.rmoMap ) );
		MatDebug_Set( MATP_RMOMAP );
		re->ext.MaterialGet( selShader, selStage, &selParms );
	}
	if ( ImGui::Checkbox( "Occlusion first (ORM)", &orm ) ) {
		selParms.ormOrder = orm ? qtrue : qfalse;
		MatDebug_Set( MATP_RMOMAP );
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled( "Enter applies a map name, empty takes it off; a made map stands before a file" );
}

static void MatDebug_DrawMaterial( void ) {
	bool pbr = selParms.pbr ? true : false;

	if ( ImGui::Checkbox( "Material (lit per pixel, GGX)", &pbr ) ) {
		selParms.pbr = pbr ? qtrue : qfalse;
		MatDebug_Set( MATP_PBR | MATP_ROUGHNESS );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 200.0f );
	if ( ImGui::BeginCombo( "##preset", "Preset", ImGuiComboFlags_HeightLarge ) ) {
		for ( int i = 0; i < (int)ARRAY_LEN( presets ); i++ ) {
			if ( ImGui::Selectable( va( "%s  (%.2f, %.0f)", presets[i].name, presets[i].roughness, presets[i].metalness ) ) ) {
				selParms.pbr = qtrue;
				selParms.roughness = presets[i].roughness;
				selParms.metalness = presets[i].metalness;
				MatDebug_Set( MATP_PBR | MATP_ROUGHNESS | MATP_METALNESS );
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip( "Roughness and metalness of a kind of surface, the usual values of physically based materials" );

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
	MatDebug_DrawWarnings();

	MatDebug_DrawGen();
	MatDebug_DrawFiles();

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
}

/*
===============
The shader: its stages, the actions, its text
===============
*/
static void MatDebug_DrawActions( void ) {
	ImGuiIO &io = ImGui::GetIO();
	const bool ctrl = io.KeyCtrl;

	if ( ImGui::Button( "Save (Ctrl+S)" ) || ( ctrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_S, false ) ) ) {
		MatDebug_Save();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( undoPos <= 0 );
	if ( ImGui::Button( "Undo" ) ) {
		MatDebug_Undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( undoPos >= undoCount );
	if ( ImGui::Button( "Redo" ) ) {
		MatDebug_Redo();
	}
	ImGui::EndDisabled();
	if ( ctrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Z, false ) ) {
		MatDebug_Undo();
	}
	if ( ctrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Y, false ) ) {
		MatDebug_Redo();
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Copy" ) || ( ctrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_C, false ) ) ) {
		clipboard = selParms;
		clipboardSet = qtrue;
		MatDebug_Message( qfalse, "Material copied" );
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( !clipboardSet );
	if ( ImGui::Button( "Paste" ) || ( clipboardSet && ctrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_V, false ) ) ) {
		// what is made from a texture is made from this one; the map files of the other surface are not taken
		materialParms_t pasted = clipboard;

		Q_strncpyz( pasted.normalMap, selParms.normalMap, sizeof( pasted.normalMap ) );
		Q_strncpyz( pasted.rmoMap, selParms.rmoMap, sizeof( pasted.rmoMap ) );
		MatDebug_Apply( &pasted );
		MatDebug_Message( qfalse, "Material pasted, the map files of this surface stay" );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if ( ImGui::Button( "Revert" ) ) {
		MatDebug_Apply( &selOriginal );
	}
	ImGui::SetItemTooltip( "Back to what it was when picked or last saved" );
	ImGui::SameLine();
	bool original = showOriginal ? true : false;
	if ( ImGui::Checkbox( "Original", &original ) ) {
		showOriginal = original ? qtrue : qfalse;
		if ( showOriginal ) {
			const materialParms_t now = selParms;

			re->ext.MaterialSet( selShader, selStage, &selOriginal, MATP_ALL );
			selParms = now;
		} else {
			MatDebug_Set( MATP_ALL );
		}
	}
	ImGui::SetItemTooltip( "Shows the material as it was when picked, to compare; editing waits until it is off" );

	int status = MatDebug_Status( selName );
	ImGui::SetNextItemWidth( 120.0f );
	if ( ImGui::Combo( "Status", &status, statusNames, MS_NUM ) ) {
		MatDebug_SetStatus( selName, status );
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "check: for a person to look at" );

	if ( message[0] ) {
		ImGui::TextColored( messageBad ? ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ) : ImVec4( 0.55f, 0.9f, 0.55f, 1.0f ), "%s", message );
	}
}

// In the test map vjk_matlab (tools/maps/matlab.py): its test objects carry the shaders object_a (studio), object_b
// (court) and object_c (dark hall), which are remapped to the shader that is judged, so they show its texture and
// material in each light; Back gives them their own again.
static qboolean labObjectsShow;

static qboolean MatDebug_InLab( void ) {
	return (qboolean)( Q_stristr( cl.mapname, "vjk_matlab" ) != NULL );
}

static void MatDebug_LabObjects( const char *shader ) {
	static const char *const objects[] = { "textures/vjk_matlab/object_a", "textures/vjk_matlab/object_b", "textures/vjk_matlab/object_c" };

	for ( int i = 0; i < (int)ARRAY_LEN( objects ); i++ ) {
		re->RemapShader( objects[i], shader ? shader : objects[i], "0" );
	}
	labObjectsShow = shader ? qtrue : qfalse;
}

static void MatDebug_DrawLab( void ) {
	if ( !MatDebug_InLab() ) {
		return;
	}
	ImGui::SeparatorText( "Test map" );
	if ( ImGui::Button( "Show on the test objects" ) ) {
		MatDebug_LabObjects( selName );
		MatDebug_Message( qfalse, "The test objects show %s", selName );
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( !labObjectsShow );
	if ( ImGui::Button( "Back" ) ) {
		MatDebug_LabObjects( NULL );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled( "studio, court and dark hall" );
}

static void MatDebug_DrawText( void ) {
	static char text[32768];
	char file[MAX_QPATH];

	if ( !ImGui::CollapsingHeader( "Shader text" ) ) {
		return;
	}
	if ( !re->ext.MaterialText( selShader, text, sizeof( text ), file, sizeof( file ) ) ) {
		ImGui::TextDisabled( "The text does not fit" );
		return;
	}
	ImGui::TextDisabled( "From %s; Save writes this", file[0] ? file : "no shader file (a texture alone)" );
	if ( ImGui::Button( "Copy text" ) ) {
		ImGui::SetClipboardText( text );
	}
	ImGui::PushFont( ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : NULL, 0.0f );
	ImGui::InputTextMultiline( "##text", text, sizeof( text ), ImVec2( -FLT_MIN, ImGui::GetTextLineHeight() * 16 ), ImGuiInputTextFlags_ReadOnly );
	ImGui::PopFont();
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
	ImGui::Separator();
	MatDebug_DrawActions();
	MatDebug_DrawLab();
	ImGui::SeparatorText( "Material of the stage" );
	ImGui::BeginDisabled( showOriginal != qfalse );
	MatDebug_DrawMaterial();
	ImGui::EndDisabled();
	MatDebug_DrawText();
	MatDebug_TrackUndo();
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
	bool withMaterial = listMaterialOnly ? true : false;
	if ( ImGui::Checkbox( "With material", &withMaterial ) ) {
		listMaterialOnly = withMaterial ? qtrue : qfalse;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Refresh" ) ) {
		numListed = -1;
	}
	// a shader this level does not use, loaded by name (to judge it in the test map)
	static char openName[MAX_QPATH];
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.6f );
	if ( ImGui::InputTextWithHint( "##open", "open a shader by name", openName, sizeof( openName ), ImGuiInputTextFlags_EnterReturnsTrue ) && openName[0] ) {
		re->RegisterShader( openName );
		const qhandle_t opened = re->ext.MaterialFind( openName );

		if ( opened ) {
			numListed = -1;
			listAll = qtrue;
			MatDebug_Select( opened );
		} else {
			MatDebug_Message( qtrue, "No shader %s", openName );
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "Enter opens it" );
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.6f );
	ImGui::InputTextWithHint( "##filter", "filter by name", filter, sizeof( filter ) );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -FLT_MIN );
	const char *statusFilter[] = { "any status", "none", "draft", "check", "done" };
	int shown = listStatus + 1;
	if ( ImGui::Combo( "##status", &shown, statusFilter, (int)ARRAY_LEN( statusFilter ) ) ) {
		listStatus = shown - 1;
	}

	if ( ImGui::BeginTable( "shaders", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) ) {
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "Shader", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthFixed );
		ImGui::TableSetupColumn( "Area m2", ImGuiTableColumnFlags_WidthFixed );
		ImGui::TableHeadersRow();
		for ( int i = 0; i < numListed; i++ ) {
			const listedShader_t *l = &listed[i];
			const int status = MatDebug_Status( l->name );

			if ( ( filter[0] && !Q_stristr( l->name, filter ) ) || ( listMaterialOnly && !l->material )
				|| ( listStatus >= 0 && status != listStatus ) ) {
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
			ImGui::TextUnformatted( status ? statusNames[status] : ( l->material ? "material" : "" ) );
			ImGui::TableNextColumn();
			// a unit of the level is about 2.5 cm (a player is 64 high): 1600 square units are a square metre
			ImGui::Text( "%.1f", l->area / 1600.0f );
		}
		ImGui::EndTable();
	}
}

static void MatDebug_DrawViews( void ) {
	static const char *views[] = { "Off (as without materials)", "Materials", "Light alone", "Shine alone", "Normals",
		"Light direction of the grid", "Roughness, metalness, occlusion", "Which maps", "Mirror of the probes",
		"Albedo checked (blue dark, red bright, magenta dark metal)", "Roughness alone", "Metalness alone" };
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
	ImGui::SameLine();
	if ( ImGui::Button( "Light..." ) ) {
		CL_MatLight_Toggle();
	}
	ImGui::SetItemTooltip( "Light to judge materials by: presets, lamps, another sun, exposure, sky (v_matlight)" );
}

static void MatDebug_Draw( debugWindow_t *window ) {
	bool open = true;
	ImGuiIO &io = ImGui::GetIO();

	if ( !statusesRead ) {
		MatDebug_ReadStatuses();
	}
	// made again when the level or the shaders changed
	if ( numListed < 0 || re->ext.MaterialList( listAll, 0, NULL, 0 ) != listedTotal ) {
		MatDebug_MakeList();
	}
	if ( !io.WantTextInput && !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_P, false ) ) {
		MatDebug_Pick();
	}

	ImGui::SetNextWindowSize( ImVec2( 1100.0f, 760.0f ), ImGuiCond_FirstUseEver );
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
	CL_MatLight_Init();
	Cmd_AddCommand( "v_matdebug", MatDebug_Toggle_f, "Opens or closes the material debug menu: the material of the surfaces live, needs rd-vulkan-vision" );
}
