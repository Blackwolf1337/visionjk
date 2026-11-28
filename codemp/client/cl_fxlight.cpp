/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

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

// cl_fxlight.cpp -- effects of the level that give a moving light (the fire bowls of mp/ffa2 are the effect
// env/small_fire, no surface a material could make a lamp). Which effects do, and how, is set in the material tool
// (v_matdebug, tab Effects): FXLIGHT_BASE has what VisionJK ships, FXLIGHT_FILE of the profile what the player set
// himself, effect by effect, and only what differs from the shipped is written there. Every play of an effect in the world goes through
// CL_FxLight_Played (fx/FxScheduler.cpp): a place where one plays again and again keeps a light that flickers as the
// small fires an explosion leaves do (cgame/weapons/cg_explosion.c), and goes out a moment after the last play.

#include "qcommon/cm/cm_public.h"
#include "client.h"
#include "cl_imgui.h"
#include "imgui/imgui.h"
#include "fx/FxScheduler.h"
#include <string>

#define FXLIGHT_FILE		"vision/fxlights.txt"
#define FXLIGHT_BASE		"vision/fxlights_base.txt"	// in the assets: the lights as shipped
#define FXLIGHT_EFFECTS		256
#define FXLIGHT_SPOTS		128
#define FXLIGHT_SAME		24.0f		// units: a play this near a spot of the effect is that spot again
#define FXLIGHT_FADE		300			// msec a light takes to come and to go
#define FXLIGHT_SEEN		2500		// msec a spot without a light is kept, for a pick with the mouse
#define FXLIGHT_MOST		8			// lights a frame, the nearest: the renderer has 32 for everything
#define FXLIGHT_SHADOWED	3			// of them with shadows: the renderer has four places for moving lights with shadows
										// in a frame, eight where it traces rays (its cvar v_lightShadowPlaces; then six
										// lights of effects), and a light without a place there shines through its wall
#define FXLIGHT_HIDDEN		2.0f		// a light the eye does not see counts as that much further: the fires in view
										// come before the ones behind a wall
#define FXLIGHT_LOOK		200			// msec between two looks whether the eye sees a light
#define FXLIGHT_STAY		0.85f		// a light that is shown counts as that much nearer, so two do not change places
										// back and forth
#define FXLIGHT_PICK		28.0f		// units from the ray of the mouse a spot may be

typedef struct fxLightEffect_s {
	char		name[MAX_QPATH];	// as the effect is registered: "env/small_fire"
	qboolean	on;
	float		color[3];
	float		radius;				// units the light reaches
	float		flicker;			// 0 steady to 1
	float		offset;				// units along the direction of the effect (a flame stands above where it starts)
	int			hold;				// msec the light stays after the last play
	int			shadows;			// dlight shadowMode: 0 players and level, 1 none, 2 players, 3 level
	qboolean	kept;				// in the file of the player, or changed in the menu: written by a save, unless it is
									// as shipped again
	qboolean	shipped;			// FXLIGHT_BASE has it: base is what it says
	struct fxLightEffect_s *base;	// (a copy of the values as shipped)
	int			plays;				// in this level
	int			lastPlayed;			// cls.realtime, 0: not in this level
} fxLightEffect_t;

typedef struct fxLightSpot_s {
	int			effect;				// -1: free
	vec3_t		origin, dir;
	int			born, lastPlayed;	// cls.realtime
	float		seed;
	float		shown;				// 0 to 1: how far its light is faded in, as one of the nearest
	qboolean	hidden;				// the level stands between it and the eye, as looked at last
	int			nextLook;			// cls.realtime of the next look
} fxLightSpot_t;

static fxLightEffect_t	effects[FXLIGHT_EFFECTS];
static fxLightEffect_t	shippedValues[FXLIGHT_EFFECTS];	// effects[i].base points here
static int				numEffects;
static fxLightSpot_t	spots[FXLIGHT_SPOTS];
static short			effectOfId[FX_MAX_EFFECTS];	// the entry of an effect id of the scheduler, -1 not looked up
static qboolean			fileRead, spotsCleared;
static int				selected = -1;
static qboolean			unsavedChanges;
static char				fxMessage[128];

static void FxLight_Defaults( fxLightEffect_t *e ) {
	// the fire an explosion leaves (cg_explosion.c)
	VectorSet( e->color, 1.0f, 0.5f, 0.16f );
	e->radius = 170.0f;
	e->flicker = 0.5f;
	e->offset = 12.0f;
	e->hold = 700;
	e->shadows = 0;
}

static fxLightEffect_t *FxLight_Find( const char *name, qboolean create ) {
	for ( int i = 0; i < numEffects; i++ ) {
		if ( !Q_stricmp( effects[i].name, name ) ) {
			return &effects[i];
		}
	}
	if ( !create || numEffects >= FXLIGHT_EFFECTS ) {
		return NULL;
	}
	fxLightEffect_t *e = &effects[numEffects++];

	memset( e, 0, sizeof( *e ) );
	Q_strncpyz( e->name, name, sizeof( e->name ) );
	FxLight_Defaults( e );
	return e;
}

// the same light as shipped
static qboolean FxLight_AsShipped( const fxLightEffect_t *e ) {
	const fxLightEffect_t *b = e->base;

	return (qboolean)( e->shipped && b && e->on == b->on && VectorCompare( e->color, b->color ) && e->radius == b->radius
		&& e->flicker == b->flicker && e->offset == b->offset && e->hold == b->hold && e->shadows == b->shadows );
}

// a line of a file: <effect> <on> <r> <g> <b> <radius> <flicker> <offset> <hold> <shadows>
static void FxLight_ReadFile( const char *name, qboolean shipped ) {
	void *buffer = NULL;
	const long length = FS_ReadFile( name, &buffer );

	if ( length > 0 && buffer ) {
		const char *p = (const char *)buffer;

		while ( 1 ) {
			const char *token = COM_ParseExt( &p, qtrue );
			float v[9];
			int count = 0;

			if ( !token[0] ) {
				break;
			}
			fxLightEffect_t *e = FxLight_Find( token, qtrue );

			while ( ( token = COM_ParseExt( &p, qfalse ) )[0] ) {
				if ( count < 9 ) {
					v[count++] = atof( token );
				}
			}
			if ( !e || count < 9 ) {
				continue;
			}
			e->on = v[0] ? qtrue : qfalse;
			VectorSet( e->color, Com_Clamp( 0.0f, 1.0f, v[1] ), Com_Clamp( 0.0f, 1.0f, v[2] ), Com_Clamp( 0.0f, 1.0f, v[3] ) );
			e->radius = Com_Clamp( 16.0f, 2000.0f, v[4] );
			e->flicker = Com_Clamp( 0.0f, 1.0f, v[5] );
			e->offset = Com_Clamp( -256.0f, 256.0f, v[6] );
			e->hold = Com_Clampi( 50, 20000, (int)v[7] );
			e->shadows = Com_Clampi( 0, 3, (int)v[8] );
			if ( shipped ) {
				e->shipped = qtrue;
				e->base = &shippedValues[e - effects];
				*e->base = *e;
			} else {
				e->kept = qtrue;
			}
		}
	}
	if ( buffer ) {
		FS_FreeFile( buffer );
	}
}

// what is shipped, then what the player set himself on top of it
static void FxLight_Read( void ) {
	fileRead = qtrue;
	FxLight_ReadFile( FXLIGHT_BASE, qtrue );
	FxLight_ReadFile( FXLIGHT_FILE, qfalse );
}

static void FxLight_Write( void ) {
	std::string text = "// effects that give a moving light (v_matdebug, tab Effects), where they differ from what is shipped:\n"
		"// <effect> <on> <r> <g> <b> <radius> <flicker> <offset> <msec it stays> <shadows: 0 players and level, 1 none, 2 players, 3 level>\n";

	for ( int i = 0; i < numEffects; i++ ) {
		const fxLightEffect_t *e = &effects[i];

		// (not what is as shipped: a later version of the shipped light reaches the player then)
		if ( e->kept && !FxLight_AsShipped( e ) ) {
			text += va( "%s %d %g %g %g %g %g %g %d %d\n", e->name, e->on ? 1 : 0, e->color[0], e->color[1], e->color[2], e->radius,
				e->flicker, e->offset, e->hold, e->shadows );
		}
	}
	FS_WriteFile( FXLIGHT_FILE, text.c_str(), (int)text.size() );
	unsavedChanges = qfalse;
	Com_sprintf( fxMessage, sizeof( fxMessage ), "Saved into %s", FXLIGHT_FILE );
}

static void FxLight_ClearSpots( void ) {
	for ( int i = 0; i < FXLIGHT_SPOTS; i++ ) {
		spots[i].effect = -1;
	}
	spotsCleared = qtrue;
}

/*
===============
CL_FxLight_Reset

The scheduler gave up its effects (a new level): their ids mean others from now on, and nothing plays any more
===============
*/
void CL_FxLight_Reset( void ) {
	for ( int i = 0; i < FX_MAX_EFFECTS; i++ ) {
		effectOfId[i] = -1;
	}
	for ( int i = 0; i < numEffects; i++ ) {
		effects[i].plays = 0;
		effects[i].lastPlayed = 0;
	}
	FxLight_ClearSpots();
}

/*
===============
CL_FxLight_Played

An effect plays in the world at origin, along dir (fx/FxScheduler.cpp PlayEffect; not one bolted to a model, whose
origin is not a place of the world, and not one of the sky portal). name: what it is registered as, asked only the
first time an id is seen.
===============
*/
void CL_FxLight_Played( int id, const vec3_t origin, const vec3_t dir ) {
	fxLightSpot_t *spot = NULL, *unused = NULL, *oldest = NULL;

	if ( id < 1 || id >= FX_MAX_EFFECTS ) {
		return;
	}
	if ( !fileRead ) {
		FxLight_Read();
	}
	if ( !spotsCleared ) {
		CL_FxLight_Reset();
	}
	if ( effectOfId[id] < 0 ) {
		// (registered with or without the folder: "effects/env/small_fire" and "env/small_fire" are one file)
		const char *name = FX_EffectName( id );
		if ( name && !Q_stricmpn( name, "effects/", 8 ) ) {
			name += 8;
		}
		const fxLightEffect_t *e = ( name && name[0] ) ? FxLight_Find( name, qtrue ) : NULL;

		if ( !e ) {
			return;
		}
		effectOfId[id] = (short)( e - effects );
	}
	const int index = effectOfId[id];
	fxLightEffect_t *e = &effects[index];

	e->plays++;
	e->lastPlayed = cls.realtime;
	for ( int i = 0; i < FXLIGHT_SPOTS; i++ ) {
		fxLightSpot_t *s = &spots[i];

		if ( s->effect == index && DistanceSquared( s->origin, origin ) < FXLIGHT_SAME * FXLIGHT_SAME ) {
			spot = s;
			break;
		}
		if ( s->effect < 0 ) {
			if ( !unused ) {
				unused = s;
			}
		} else if ( !oldest || s->lastPlayed < oldest->lastPlayed ) {
			oldest = s;
		}
	}
	if ( !spot ) {
		spot = unused ? unused : oldest;
		spot->effect = index;
		spot->born = cls.realtime;
		spot->seed = ( rand() & 1023 ) * 0.0123f;
		spot->shown = 0.0f;
		spot->hidden = qfalse;
		spot->nextLook = 0;
	}
	VectorCopy( origin, spot->origin );
	VectorCopy( dir, spot->dir );
	spot->lastPlayed = cls.realtime;
}

/*
===============
CL_FxLight_AddToScene

The lights of the spots where effects with a light play, into the scene of this frame (fx/FxScheduler.cpp
AddScheduledEffects, where the effects add theirs). The nearest to the eye are shown, FXLIGHT_MOST of them and of
those with shadows FXLIGHT_SHADOWED: the renderer traces the shadows of four moving lights a frame, and a fire with no
place there lit the other side of its wall (their picture 2026-11-25). Which are shown changes as the eye moves; each
fades in and out, and one that is shown holds its place against one a little nearer.
===============
*/
void CL_FxLight_AddToScene( void ) {
	int			order[FXLIGHT_SPOTS], count = 0, shadowed = 0, plain = 0, sentShadowed = 0;
	float		distances[FXLIGHT_SPOTS];
	const float	step = Q_max( cls.realFrametime, 1 ) / (float)FXLIGHT_FADE;
	// (two places stay for sabers and shots)
	const int	places = Cvar_VariableIntegerValue( "v_lightShadowPlaces" ) >= 8 ? 8 : 4;
	const int	mostShadowed = places >= 8 ? 6 : FXLIGHT_SHADOWED;
	vec3_t		eye, forward;

	if ( !spotsCleared || !re ) {
		return;
	}
	if ( !re->ext.LevelView || !re->ext.LevelView( eye, forward ) ) {
		VectorCopy( cl.snap.ps.origin, eye );
	}
	// the spots with a light, the nearest first
	for ( int i = 0; i < FXLIGHT_SPOTS; i++ ) {
		fxLightSpot_t *s = &spots[i];

		if ( s->effect < 0 ) {
			continue;
		}
		const fxLightEffect_t *e = &effects[s->effect];

		if ( cls.realtime - s->lastPlayed > ( e->on ? e->hold + FXLIGHT_FADE : FXLIGHT_SEEN ) ) {
			s->effect = -1;
			continue;
		}
		if ( !e->on ) {
			s->shown = 0.0f;
			continue;
		}
		if ( cls.realtime >= s->nextLook && cls.state == CA_ACTIVE ) {
			trace_t	tr;
			vec3_t	place;

			VectorMA( s->origin, e->offset, s->dir, place );
			CM_BoxTrace( &tr, eye, place, NULL, NULL, 0, CONTENTS_SOLID, qfalse );
			s->hidden = (qboolean)( tr.fraction < 1.0f && !tr.startsolid );
			// (not all in one frame)
			s->nextLook = cls.realtime + FXLIGHT_LOOK + ( i * 37 ) % FXLIGHT_LOOK;
		}
		const float distance = Distance( s->origin, eye ) * ( s->shown > 0.0f ? FXLIGHT_STAY : 1.0f ) * ( s->hidden ? FXLIGHT_HIDDEN : 1.0f );
		int slot = count++;

		while ( slot > 0 && distances[slot - 1] > distance ) {
			order[slot] = order[slot - 1];
			distances[slot] = distances[slot - 1];
			slot--;
		}
		order[slot] = i;
		distances[slot] = distance;
	}
	for ( int k = 0; k < count; k++ ) {
		fxLightSpot_t	*s = &spots[order[k]];
		const qboolean	shadows = (qboolean)( effects[s->effect].shadows != 1 );
		const qboolean	in = (qboolean)( shadowed + plain < FXLIGHT_MOST && ( !shadows || shadowed < mostShadowed ) );

		if ( in ) {
			if ( shadows ) {
				shadowed++;
			} else {
				plain++;
			}
		}
		s->shown = Com_Clamp( 0.0f, 1.0f, s->shown + ( in ? step : -step ) );
	}
	for ( int k = 0; k < count; k++ ) {
		const fxLightSpot_t		*s = &spots[order[k]];
		const fxLightEffect_t	*e = &effects[s->effect];
		const int				age = cls.realtime - s->lastPlayed;
		const float				t = cls.realtime;
		// as the fires of an explosion flicker (cg_explosion.c)
		const float				flicker = 1.0f + e->flicker * 0.36f * ( 0.55f * sinf( t * 0.031f + s->seed ) + 0.45f * sinf( t * 0.077f + s->seed * 3.0f ) );
		float					fade = s->shown;
		vec3_t					place;

		if ( age > e->hold ) {
			fade *= 1.0f - ( age - e->hold ) / (float)FXLIGHT_FADE;
		}
		if ( fade <= 0.0f ) {
			continue;
		}
		// (one more than are shown may still fade out; past the places of the renderer it would have no shadows)
		if ( e->shadows != 1 && ++sentShadowed > mostShadowed + 1 ) {
			continue;
		}
		VectorMA( s->origin, e->offset, s->dir, place );
		if ( re->ext.AddShadowLightToScene ) {
			re->ext.AddShadowLightToScene( place, e->radius * flicker, e->color[0] * fade, e->color[1] * fade, e->color[2] * fade, e->shadows, NULL );
		} else {
			re->AddLightToScene( place, e->radius * flicker, e->color[0] * fade, e->color[1] * fade, e->color[2] * fade );
		}
	}
}

/*
===============
CL_FxLight_PickAt

The effect that plays nearest to a ray (a click on the level), before maxDistance along it: its spot is within
FXLIGHT_PICK units of the ray. Selects it for the menu; qfalse when there is none.
===============
*/
qboolean CL_FxLight_PickAt( const vec3_t origin, const vec3_t dir, float maxDistance ) {
	int		best = -1;
	float	bestOff = FXLIGHT_PICK;

	for ( int i = 0; i < FXLIGHT_SPOTS; i++ ) {
		const fxLightSpot_t *s = &spots[i];
		vec3_t to, place, nearest;

		if ( s->effect < 0 ) {
			continue;
		}
		VectorMA( s->origin, effects[s->effect].offset, s->dir, place );
		VectorSubtract( place, origin, to );
		const float along = DotProduct( to, dir );

		if ( along < 8.0f || along > maxDistance + FXLIGHT_PICK ) {
			continue;
		}
		VectorMA( origin, along, dir, nearest );
		const float off = Distance( nearest, place );

		if ( off < bestOff ) {
			bestOff = off;
			best = s->effect;
		}
	}
	if ( best < 0 ) {
		return qfalse;
	}
	selected = best;
	fxMessage[0] = '\0';
	return qtrue;
}

/*
===============
CL_FxLight_DrawMenu

The tab Effects of the material tool: the effects that play in this level, and for the one picked whether it gives a
moving light and how
===============
*/
void CL_FxLight_DrawMenu( void ) {
	static char		filter[64];
	static bool		all;
	int				live = 0;

	if ( !fileRead ) {
		FxLight_Read();
	}
	for ( int i = 0; i < FXLIGHT_SPOTS; i++ ) {
		live += ( spots[i].effect >= 0 && effects[spots[i].effect].on ) ? 1 : 0;
	}
	if ( ImGui::BeginChild( "fxlist", ImVec2( ImGui::GetContentRegionAvail().x * 0.4f, 0.0f ), ImGuiChildFlags_ResizeX ) ) {
		ImGui::Checkbox( "All known", &all );
		ImGui::SetItemTooltip( "Also the effects that do not play in this level: the ones of the saved file" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::InputTextWithHint( "##fxfilter", "filter by name", filter, sizeof( filter ) );
		if ( ImGui::BeginTable( "effects", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) ) {
			ImGui::TableSetupScrollFreeze( 0, 1 );
			ImGui::TableSetupColumn( "Effect", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Light", ImGuiTableColumnFlags_WidthFixed );
			ImGui::TableSetupColumn( "Plays", ImGuiTableColumnFlags_WidthFixed );
			ImGui::TableHeadersRow();
			for ( int i = 0; i < numEffects; i++ ) {
				const fxLightEffect_t *e = &effects[i];

				if ( ( !all && !e->lastPlayed ) || ( filter[0] && !Q_stristr( e->name, filter ) ) ) {
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID( i );
				// (bright while it plays)
				const bool playing = e->lastPlayed && cls.realtime - e->lastPlayed < FXLIGHT_SEEN;
				if ( !playing ) {
					ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
				}
				if ( ImGui::Selectable( e->name, i == selected, ImGuiSelectableFlags_SpanAllColumns ) ) {
					selected = i;
					fxMessage[0] = '\0';
				}
				if ( !playing ) {
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( e->on ? "on" : "" );
				ImGui::TableNextColumn();
				ImGui::Text( "%d", e->plays );
			}
			ImGui::EndTable();
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if ( ImGui::BeginChild( "fxeffect" ) ) {
		if ( selected < 0 || selected >= numEffects ) {
			ImGui::TextDisabled( "Click an effect of the level (a fire), or choose one in the list.\nThe list has the effects that played since the level loaded." );
		} else {
			static const char *const shadowModes[] = { "Players and level", "None", "Players", "Level" };
			fxLightEffect_t *e = &effects[selected];
			bool on = e->on ? true : false, changed = false;

			ImGui::TextUnformatted( e->name );
			ImGui::TextDisabled( "effects/%s.efx  played %d times in this level", e->name, e->plays );
			if ( ImGui::Checkbox( "Gives a moving light", &on ) ) {
				e->on = on ? qtrue : qfalse;
				changed = true;
			}
			ImGui::SetItemTooltip( "Where the effect plays, a light that lights the level and the players around it,\nas the moving lights of sabers and shots do. It goes out a moment after the effect stops." );
			ImGui::BeginDisabled( !on );
			changed |= ImGui::ColorEdit3( "Colour", e->color, ImGuiColorEditFlags_Float );
			changed |= ImGui::SliderFloat( "Reach", &e->radius, 32.0f, 800.0f, "%.0f units" );
			changed |= ImGui::SliderFloat( "Flicker", &e->flicker, 0.0f, 1.0f, "%.2f" );
			ImGui::SetItemTooltip( "0: a steady light. 1: it flickers as a fire does." );
			changed |= ImGui::SliderFloat( "Height", &e->offset, -64.0f, 128.0f, "%.0f units" );
			ImGui::SetItemTooltip( "How far along the direction of the effect the light is from where it starts:\na flame stands above its bowl, and a light inside the bowl is hidden by it." );
			changed |= ImGui::SliderInt( "Stays", &e->hold, 100, 5000, "%d msec" );
			ImGui::SetItemTooltip( "How long the light stays after the effect last played. An effect of the level plays again\nand again: longer than the time between two plays, else the light blinks." );
			ImGui::SetNextItemWidth( 200.0f );
			changed |= ImGui::Combo( "Shadows", &e->shadows, shadowModes, IM_ARRAYSIZE( shadowModes ) );
			ImGui::SetItemTooltip( "Whose shadows the light throws. The renderer has few places for moving lights with shadows (sabers too): the six\nnearest effects with shadows are lit where the graphics card traces rays, else three; the others fade in as you come near.\nThe four nearest lights throw the shadows of players, the others of the level alone.\nNone: the eight nearest are lit, but they shine through walls." );
			if ( ImGui::Button( "Fire" ) ) {
				FxLight_Defaults( e );
				changed = true;
			}
			ImGui::SetItemTooltip( "The light of the small fires an explosion leaves" );
			ImGui::EndDisabled();
			if ( e->shipped ) {
				ImGui::SameLine();
				ImGui::BeginDisabled( FxLight_AsShipped( e ) );
				if ( ImGui::Button( "As shipped" ) ) {
					const fxLightEffect_t b = *e->base;

					e->on = b.on;
					VectorCopy( b.color, e->color );
					e->radius = b.radius;
					e->flicker = b.flicker;
					e->offset = b.offset;
					e->hold = b.hold;
					e->shadows = b.shadows;
					changed = true;
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip( "Back to the light VisionJK ships for this effect" );
			}
			if ( changed ) {
				e->kept = qtrue;
				unsavedChanges = qtrue;
				fxMessage[0] = '\0';
			}
			ImGui::Separator();
			if ( ImGui::Button( "Save" ) ) {
				FxLight_Write();
			}
			ImGui::SetItemTooltip( "Writes the lights of all effects into " FXLIGHT_FILE " of the profile" );
			ImGui::SameLine();
			if ( unsavedChanges ) {
				ImGui::TextColored( ImVec4( 1.0f, 0.7f, 0.3f, 1.0f ), "not saved" );
			} else if ( fxMessage[0] ) {
				ImGui::TextColored( ImVec4( 0.55f, 0.9f, 0.55f, 1.0f ), "%s", fxMessage );
			}
			ImGui::TextDisabled( "%d places with a light now; shown are the nearest %d with shadows, %d without", live,
				Cvar_VariableIntegerValue( "v_lightShadowPlaces" ) >= 8 ? 6 : FXLIGHT_SHADOWED, FXLIGHT_MOST );
		}
	}
	ImGui::EndChild();
}

// v_fxlight <effect> <on|off> [radius] [r g b] [flicker] [height] [msec] [shadows 0-3], and v_fxlight alone lists them
static void FxLight_f( void ) {
	if ( !fileRead ) {
		FxLight_Read();
	}
	if ( Cmd_Argc() < 3 ) {
		if ( Cmd_Argc() == 2 && !Q_stricmp( Cmd_Argv( 1 ), "save" ) ) {
			FxLight_Write();
			Com_Printf( "%s\n", fxMessage );
			return;
		}
		Com_Printf( "usage: v_fxlight <effect> <on|off> [reach] [r g b] [flicker] [height] [msec it stays] [shadows: 0 players and level, 1 none, 2 players, 3 level]\n"
			"       v_fxlight save\n" );
		for ( int i = 0; i < numEffects; i++ ) {
			const fxLightEffect_t *e = &effects[i];

			if ( e->lastPlayed || e->kept ) {
				Com_Printf( "fxlight %s %s reach %g colour %g %g %g flicker %g height %g stays %d shadows %d plays %d\n", e->name, e->on ? "on" : "off",
					e->radius, e->color[0], e->color[1], e->color[2], e->flicker, e->offset, e->hold, e->shadows, e->plays );
			}
		}
		return;
	}
	fxLightEffect_t *e = FxLight_Find( Cmd_Argv( 1 ), qtrue );

	if ( !e ) {
		return;
	}
	e->on = !Q_stricmp( Cmd_Argv( 2 ), "on" ) ? qtrue : qfalse;
	if ( Cmd_Argc() > 3 ) {
		e->radius = Com_Clamp( 16.0f, 2000.0f, atof( Cmd_Argv( 3 ) ) );
	}
	if ( Cmd_Argc() > 6 ) {
		VectorSet( e->color, Com_Clamp( 0.0f, 1.0f, atof( Cmd_Argv( 4 ) ) ), Com_Clamp( 0.0f, 1.0f, atof( Cmd_Argv( 5 ) ) ), Com_Clamp( 0.0f, 1.0f, atof( Cmd_Argv( 6 ) ) ) );
	}
	if ( Cmd_Argc() > 7 ) {
		e->flicker = Com_Clamp( 0.0f, 1.0f, atof( Cmd_Argv( 7 ) ) );
	}
	if ( Cmd_Argc() > 8 ) {
		e->offset = Com_Clamp( -256.0f, 256.0f, atof( Cmd_Argv( 8 ) ) );
	}
	if ( Cmd_Argc() > 9 ) {
		e->hold = Com_Clampi( 50, 20000, atoi( Cmd_Argv( 9 ) ) );
	}
	if ( Cmd_Argc() > 10 ) {
		e->shadows = Com_Clampi( 0, 3, atoi( Cmd_Argv( 10 ) ) );
	}
	e->kept = qtrue;
	unsavedChanges = qtrue;
	Com_Printf( "fxlight %s %s reach %g (v_fxlight save keeps it)\n", e->name, e->on ? "on" : "off", e->radius );
}

void CL_FxLight_Init( void ) {
	Cmd_AddCommand( "v_fxlight", FxLight_f, "Whether an effect gives a moving light where it plays: v_fxlight <effect> <on|off> [reach] ..., v_fxlight alone lists the effects of the level" );
}

void CL_FxLight_Shutdown( void ) {
	Cmd_RemoveCommand( "v_fxlight" );
}
