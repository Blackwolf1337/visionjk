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

// cl_matlight.cpp: the light to judge materials by (v_matlight, and a button of v_matdebug): presets of light, up to
// DEBUG_LIGHTS lamps lit per pixel on every surface with a material, the light of the lightmap swapped for an even
// one, another sun for the traced shadows, the exposure, and another sky. Through the renderer (ext.SetLightOverride,
// RemapShader); it stays when the menu is closed, "Light of the level" gives the level its own light back.

#include "client.h"
#include "cl_imgui.h"
#include "imgui/imgui.h"

static debugWindow_t	lightWindow;
static lightOverride_t	light;
static qboolean			lightOn;
static qboolean			torchFollows;
static int				editLight = -1;
static float			sunYaw = 30.0f, sunElevation = 55.0f;
static char				levelSky[MAX_QPATH];	// the sky shader of the level, which a swap remaps
static char				shownSky[MAX_QPATH];	// what it shows now
static char				skyLevel[MAX_QPATH];	// the level they are of

// skies of the game to swap in, one for each box of pictures there is (the others draw black: a skyParms of -, or
// pictures the game does not have)
static const char *const skies[] = {
	"textures/skies/yavin", "textures/skies/desert", "textures/skies/dune", "textures/skies/bespin", "textures/skies/korriban",
	"textures/skies/vjun", "textures/skies/byss_mp", "textures/skies/space", "textures/skies/boba_night", "textures/skies/hevil",
	"textures/skies/Coruscant",
};

static void Light_Send( void ) {
	re->ext.SetLightOverride( lightOn ? &light : NULL );
}

static void Light_View( vec3_t origin, vec3_t forward ) {
	if ( !re->ext.LevelView( origin, forward ) ) {
		VectorClear( origin );
		VectorSet( forward, 1.0f, 0.0f, 0.0f );
	}
}

static void Light_SunFromAngles( void ) {
	const float yaw = DEG2RAD( sunYaw ), elevation = DEG2RAD( sunElevation );

	VectorSet( light.sunDirection, cosf( yaw ) * cosf( elevation ), sinf( yaw ) * cosf( elevation ), sinf( elevation ) );
}

static debugLight_t *Light_Add( int type, const vec3_t color, float intensity ) {
	if ( light.numLights >= DEBUG_LIGHTS ) {
		return NULL;
	}
	debugLight_t *l = &light.lights[light.numLights++];

	memset( l, 0, sizeof( *l ) );
	l->type = type;
	VectorCopy( color, l->color );
	l->intensity = intensity;
	l->innerAngle = 20.0f;
	l->outerAngle = 35.0f;
	VectorSet( l->direction, 0.0f, 0.0f, -1.0f );
	return l;
}

// a directional lamp that comes from the view turned by yaw and raised by elevation (degrees)
static void Light_FromView( debugLight_t *l, float yaw, float elevation ) {
	vec3_t origin, forward, angles, dir;

	Light_View( origin, forward );
	vectoangles( forward, angles );
	angles[PITCH] = -elevation;
	angles[YAW] += 180.0f + yaw;
	AngleVectors( angles, dir, NULL, NULL );
	// it shines away from where it comes from
	VectorScale( dir, -1.0f, l->direction );
}

/*
===============
Presets
===============
*/
static void Light_Preset( int preset ) {
	const vec3_t white = { 1.0f, 1.0f, 1.0f }, warm = { 1.0f, 0.93f, 0.82f };
	const vec3_t sunset = { 1.0f, 0.55f, 0.3f }, lamp = { 1.0f, 0.8f, 0.55f };
	vec3_t origin, forward;
	debugLight_t *l;

	memset( &light, 0, sizeof( light ) );
	light.exposure = 1.0f;
	lightOn = qtrue;
	torchFollows = qfalse;
	editLight = -1;
	Light_View( origin, forward );

	switch ( preset ) {
	case 0:		// the light of the level
		lightOn = qfalse;
		break;
	case 1:		// studio: an even grey light, a key light from the upper left, a weak fill from the right
		light.replaceLight = qtrue;
		VectorSet( light.ambient, 0.18f, 0.18f, 0.18f );
		if ( ( l = Light_Add( DLT_DIRECTIONAL, white, 1.2f ) ) != NULL ) {
			Light_FromView( l, -40.0f, 45.0f );
		}
		if ( ( l = Light_Add( DLT_DIRECTIONAL, white, 0.35f ) ) != NULL ) {
			Light_FromView( l, 60.0f, 15.0f );
		}
		break;
	case 2:		// noon: a high warm sun, light of a blue sky around
	case 3:		// sunset: a low orange sun, a dim purple sky
		light.replaceLight = qtrue;
		light.sun = qtrue;
		sunElevation = preset == 2 ? 65.0f : 8.0f;
		if ( preset == 2 ) {
			VectorSet( light.ambient, 0.28f, 0.32f, 0.4f );
			VectorCopy( warm, light.sunColor );
		} else {
			VectorSet( light.ambient, 0.1f, 0.08f, 0.14f );
			VectorCopy( sunset, light.sunColor );
		}
		Light_SunFromAngles();
		if ( ( l = Light_Add( DLT_DIRECTIONAL, light.sunColor, preset == 2 ? 1.6f : 1.1f ) ) != NULL ) {
			VectorScale( light.sunDirection, -1.0f, l->direction );
		}
		break;
	case 4:		// overcast: an even light, nothing more
		light.replaceLight = qtrue;
		VectorSet( light.ambient, 0.5f, 0.5f, 0.52f );
		break;
	case 5:		// night: very dark blue around, two lamps beside the view
		light.replaceLight = qtrue;
		VectorSet( light.ambient, 0.02f, 0.025f, 0.04f );
		for ( int side = -1; side <= 1; side += 2 ) {
			if ( ( l = Light_Add( DLT_POINT, lamp, 2.0f ) ) != NULL ) {
				vec3_t right, angles;

				vectoangles( forward, angles );
				AngleVectors( angles, NULL, right, NULL );
				VectorMA( origin, 160.0f, forward, l->origin );
				VectorMA( l->origin, 120.0f * side, right, l->origin );
				l->origin[2] += 48.0f;
				l->range = 900.0f;
			}
		}
		break;
	case 6:		// torch: dark, a spot from the eye that follows the view
		light.replaceLight = qtrue;
		VectorSet( light.ambient, 0.01f, 0.01f, 0.015f );
		if ( ( l = Light_Add( DLT_SPOT, white, 3.0f ) ) != NULL ) {
			VectorCopy( origin, l->origin );
			VectorCopy( forward, l->direction );
			l->range = 2000.0f;
			l->innerAngle = 12.0f;
			l->outerAngle = 25.0f;
		}
		torchFollows = qtrue;
		break;
	default:
		break;
	}
	Light_Send();
}

/*
===============
The window
===============
*/
static void Light_DrawLamp( int i ) {
	debugLight_t *l = &light.lights[i];
	static const char *types[] = { "Point", "Spot", "Directional (like a sun)" };
	bool changed = false;
	vec3_t origin, forward;

	ImGui::PushID( i );
	changed |= ImGui::Combo( "Type", &l->type, types, (int)ARRAY_LEN( types ) );
	changed |= ImGui::ColorEdit3( "Colour", l->color, ImGuiColorEditFlags_NoInputs );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 220.0f );
	changed |= ImGui::SliderFloat( "Intensity", &l->intensity, 0.0f, 8.0f, "%.2f" );
	if ( l->type != DLT_DIRECTIONAL ) {
		changed |= ImGui::DragFloat3( "Place", l->origin, 4.0f, -65536.0f, 65536.0f, "%.0f" );
		changed |= ImGui::SliderFloat( "Range (0 without end)", &l->range, 0.0f, 4096.0f, "%.0f" );
		if ( ImGui::Button( "To the eye" ) ) {
			Light_View( l->origin, forward );
			changed = true;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "To the crosshair" ) ) {
			materialPick_t pick;

			if ( re->ext.MaterialPick( NULL, NULL, &pick ) ) {
				// a little off the surface, so it lights it and not only the inside of the wall
				VectorMA( pick.point, 24.0f, pick.normal, l->origin );
				changed = true;
			}
		}
	}
	if ( l->type != DLT_POINT ) {
		if ( ImGui::Button( "Shine along the view" ) ) {
			Light_View( origin, l->direction );
			changed = true;
		}
		if ( l->type == DLT_SPOT ) {
			changed |= ImGui::SliderFloat( "Inner angle", &l->innerAngle, 1.0f, 80.0f, "%.0f" );
			changed |= ImGui::SliderFloat( "Outer angle", &l->outerAngle, 1.0f, 89.0f, "%.0f" );
		}
	}
	if ( ImGui::Button( "Remove" ) ) {
		memmove( &light.lights[i], &light.lights[i + 1], sizeof( light.lights[0] ) * ( light.numLights - i - 1 ) );
		light.numLights--;
		editLight = -1;
		changed = true;
	}
	ImGui::PopID();
	if ( changed ) {
		Light_Send();
	}
}

static void Light_DrawSky( void ) {
	char mapName[MAX_QPATH];

	// the sky shader of this level, found once a level
	Q_strncpyz( mapName, cl.mapname, sizeof( mapName ) );
	if ( Q_stricmp( mapName, skyLevel ) ) {
		static qhandle_t handles[4096];
		const int count = Q_min( re->ext.MaterialList( qfalse, 0, handles, (int)ARRAY_LEN( handles ) ), (int)ARRAY_LEN( handles ) );

		Q_strncpyz( skyLevel, mapName, sizeof( skyLevel ) );
		levelSky[0] = shownSky[0] = '\0';
		for ( int i = 0; i < count; i++ ) {
			materialInfo_t info;

			if ( re->ext.MaterialInfo( handles[i], &info ) && ( info.flags & MATI_SKY ) ) {
				Q_strncpyz( levelSky, info.name, sizeof( levelSky ) );
				Q_strncpyz( shownSky, info.name, sizeof( shownSky ) );
				break;
			}
		}
	}
	if ( !levelSky[0] ) {
		ImGui::TextDisabled( "This level shows no sky" );
		return;
	}
	ImGui::SetNextItemWidth( 320.0f );
	if ( ImGui::BeginCombo( "Sky", shownSky ) ) {
		if ( ImGui::Selectable( va( "%s (of the level)", levelSky ), !Q_stricmp( shownSky, levelSky ) ) ) {
			re->RemapShader( levelSky, levelSky, "0" );
			Q_strncpyz( shownSky, levelSky, sizeof( shownSky ) );
		}
		for ( int i = 0; i < (int)ARRAY_LEN( skies ); i++ ) {
			if ( ImGui::Selectable( skies[i], !Q_stricmp( shownSky, skies[i] ) ) ) {
				re->RemapShader( levelSky, skies[i], "0" );
				Q_strncpyz( shownSky, skies[i], sizeof( shownSky ) );
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip( "Another sky in place of the one of the level; the light of the lightmap stays the one of its sky" );
}

static void Light_Draw( debugWindow_t *window ) {
	static const char *presets[] = { "Light of the level", "Studio", "Noon", "Sunset", "Overcast", "Night with lamps", "Torch" };
	bool open = true;

	if ( light.exposure <= 0.0f ) {
		light.exposure = 1.0f;
	}
	// the torch goes where the view goes
	if ( lightOn && torchFollows && light.numLights > 0 && light.lights[0].type == DLT_SPOT ) {
		Light_View( light.lights[0].origin, light.lights[0].direction );
		Light_Send();
	}

	ImGui::SetNextWindowSize( ImVec2( 560.0f, 640.0f ), ImGuiCond_FirstUseEver );
	if ( ImGui::Begin( "Light to judge materials by", &open ) ) {
		ImGui::TextUnformatted( "Preset:" );
		for ( int i = 0; i < (int)ARRAY_LEN( presets ); i++ ) {
			if ( i ) {
				ImGui::SameLine();
			}
			if ( ImGui::Button( presets[i] ) ) {
				Light_Preset( i );
			}
		}
		ImGui::TextDisabled( "Lamps and the even light act on surfaces with a material; Vulkan only" );
		ImGui::Separator();

		bool on = lightOn ? true : false;
		if ( ImGui::Checkbox( "Own light on", &on ) ) {
			lightOn = on ? qtrue : qfalse;
			if ( lightOn && light.exposure <= 0.0f ) {
				light.exposure = 1.0f;
			}
			Light_Send();
		}
		ImGui::BeginDisabled( !lightOn );
		bool changed = false;
		bool replace = light.replaceLight ? true : false;
		if ( ImGui::Checkbox( "Even light instead of the lightmap", &replace ) ) {
			light.replaceLight = replace ? qtrue : qfalse;
			changed = true;
		}
		ImGui::SameLine();
		changed |= ImGui::ColorEdit3( "##ambient", light.ambient, ImGuiColorEditFlags_NoInputs );
		changed |= ImGui::SliderFloat( "Exposure", &light.exposure, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic );

		ImGui::SeparatorText( "Sun" );
		bool sun = light.sun ? true : false;
		if ( ImGui::Checkbox( "Another sun (shadows the renderer traces, models)", &sun ) ) {
			light.sun = sun ? qtrue : qfalse;
			Light_SunFromAngles();
			changed = true;
		}
		if ( light.sun ) {
			if ( ImGui::SliderFloat( "From (yaw)", &sunYaw, -180.0f, 180.0f, "%.0f" ) | ImGui::SliderFloat( "Height above the horizon", &sunElevation, 1.0f, 90.0f, "%.0f" ) ) {
				Light_SunFromAngles();
				// a directional lamp of the sun's colour follows it
				for ( int i = 0; i < light.numLights; i++ ) {
					if ( light.lights[i].type == DLT_DIRECTIONAL ) {
						VectorScale( light.sunDirection, -1.0f, light.lights[i].direction );
						break;
					}
				}
				changed = true;
			}
			changed |= ImGui::ColorEdit3( "Sun colour", light.sunColor, ImGuiColorEditFlags_NoInputs );
		}

		ImGui::SeparatorText( va( "Lamps (%d of %d)", light.numLights, DEBUG_LIGHTS ) );
		for ( int i = 0; i < light.numLights; i++ ) {
			const debugLight_t *l = &light.lights[i];
			static const char *names[] = { "point", "spot", "directional" };

			if ( ImGui::Selectable( va( "%d  %s, intensity %.2f##lamp%d", i + 1, names[Com_Clampi( 0, 2, l->type )], l->intensity, i ), editLight == i ) ) {
				editLight = ( editLight == i ) ? -1 : i;
			}
			if ( editLight == i ) {
				ImGui::Indent();
				Light_DrawLamp( i );
				ImGui::Unindent();
			}
		}
		ImGui::BeginDisabled( light.numLights >= DEBUG_LIGHTS );
		if ( ImGui::Button( "Add a lamp at the eye" ) ) {
			const vec3_t white = { 1.0f, 1.0f, 1.0f };
			debugLight_t *l = Light_Add( DLT_POINT, white, 1.5f );
			vec3_t forward;

			if ( l ) {
				Light_View( l->origin, forward );
				l->range = 1200.0f;
				editLight = light.numLights - 1;
				changed = true;
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if ( changed ) {
			Light_Send();
		}

		ImGui::SeparatorText( "Sky" );
		Light_DrawSky();
	}
	ImGui::End();
	if ( !open ) {
		CL_ImGui_Toggle( window );
	}
}

void CL_MatLight_Toggle( void ) {
	if ( !re || !re->ext.SetLightOverride ) {
		Com_Printf( "The light to judge materials by needs the renderer rd-vulkan-vision\n" );
		return;
	}
	CL_ImGui_Toggle( &lightWindow );
}

// v_matlight alone opens or closes the window; v_matlight <preset> sets a preset by its name (for scripts)
static void MatLight_Toggle_f( void ) {
	static const char *names[] = { "level", "studio", "noon", "sunset", "overcast", "night", "torch" };

	if ( Cmd_Argc() < 2 ) {
		CL_MatLight_Toggle();
		return;
	}
	if ( !re || !re->ext.SetLightOverride ) {
		Com_Printf( "The light to judge materials by needs the renderer rd-vulkan-vision\n" );
		return;
	}
	for ( int i = 0; i < (int)ARRAY_LEN( names ); i++ ) {
		if ( !Q_stricmp( Cmd_Argv( 1 ), names[i] ) ) {
			Light_Preset( i );
			Com_Printf( "Light: %s\n", names[i] );
			return;
		}
	}
	Com_Printf( "usage: v_matlight [level|studio|noon|sunset|overcast|night|torch]\n" );
}

void CL_MatLight_Init( void ) {
	lightWindow.name = "Light";
	lightWindow.draw = Light_Draw;
	CL_ImGui_Register( &lightWindow );
	Cmd_AddCommand( "v_matlight", MatLight_Toggle_f, "Opens or closes the light to judge materials by: presets, lamps, an even light, another sun, exposure, sky" );
}
