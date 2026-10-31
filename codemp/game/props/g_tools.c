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

// g_tools.c -- the toolgun of the sandbox (issue #3): the Bryar pistol after amsandbox (g_props.c), whose buttons
// work the tool he chose on the prop he looks at. Not from JA+, see docs/japlus/sandbox.md.
//
//   attack      the tool: remove, colour, material, physical properties, freeze
//   alt attack  the other side of it: take the colour, the material or the properties of the prop into the
//               tool, or free a frozen prop (the remover removes)
//   use         the reload key of that other game: the prop back to what it had, the freeze tool frees all
//
// The tool and its settings come with the command vs_tool, which the tool window of the client sends:
//
//   vs_tool remover | freeze
//   vs_tool color <r> <g> <b>             0 to 255
//   vs_tool material <name>               bg_tools.h
//   vs_tool physprop <mass> <bounce> <grip>
//
// What the server holds is sent back to him the same way (a server command vs_tool with all of it), after a
// change and when alt attack took something from a prop, so his window and his crosshair show it.

#include "game/g_local.h"
#include "bg_props.h"		// PROP_BEAM
#include "bg_tools.h"

#define TOOL_RANGE		4096.0f		// the toolgun reaches this far
#define TOOL_RAY_MSEC	150			// its ray is seen for so long

typedef struct toolState_s {
	qboolean	set;				// it has been set up, else it is the default
	int			tool;				// tool_t
	byte		color[3];
	int			material;			// in toolMaterials
	float		mass, bounce, grip;
} toolState_t;

static toolState_t toolStates[MAX_CLIENTS];

static toolState_t *Tool_State( const gentity_t *ent ) {
	toolState_t *t = &toolStates[ent->s.number];

	if ( !t->set ) {
		memset( t, 0, sizeof( *t ) );
		t->set = qtrue;
		t->tool = TOOL_REMOVER;
		t->color[0] = t->color[1] = t->color[2] = 255;
		t->mass = 1.0f;
		t->bounce = 0.25f;
		t->grip = 0.7f;
	}
	return t;
}

// what the server holds, to his cgame (cg_servercmds.c), which shows it
static void Tool_Tell( const gentity_t *ent ) {
	const toolState_t *t = Tool_State( ent );

	trap->SendServerCommand( ent->s.number, va( "vs_tool %s %i %i %i %s %g %g %g", toolNames[t->tool], t->color[0], t->color[1],
		t->color[2], toolMaterials[t->material].name, t->mass, t->bounce, t->grip ) );
}

// the green ray of a shot of the toolgun, from his weapon to where it hit: a beam entity of g_props.c whose end
// is a point (origin2) and not an entity, for a moment
static void Tool_Ray( gentity_t *ent, const vec3_t end ) {
	gentity_t *ray = G_Spawn();

	ray->classname = "toolgun_ray";
	ray->s.eType = ET_INVISIBLE;
	ray->s.generic1 = PROP_BEAM;
	ray->s.otherEntityNum = ent->s.number;
	ray->s.otherEntityNum2 = ENTITYNUM_NONE;
	ray->s.weapon = WP_BRYAR_PISTOL;
	VectorCopy( end, ray->s.origin2 );
	ray->r.svFlags = SVF_BROADCAST;
	G_SetOrigin( ray, ent->client->ps.origin );
	ray->think = G_FreeEntity;
	ray->nextthink = level.time + TOOL_RAY_MSEC;
	trap->LinkEntity( (sharedEntity_t *)ray );
}

// the prop he looks at within reach, NULL: none. The ray goes out whatever it hits.
static gentity_t *Tool_Aim( gentity_t *ent ) {
	vec3_t	eye, fwd, end;
	trace_t	tr;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( eye, TOOL_RANGE, fwd, end );
	trap->Trace( &tr, eye, NULL, NULL, end, ent->s.number, MASK_SHOT, qfalse, 0, 0 );
	Tool_Ray( ent, tr.endpos );
	if ( tr.fraction >= 1.0f || tr.entityNum >= ENTITYNUM_WORLD || !G_PropIs( &g_entities[tr.entityNum] ) )
		return NULL;
	return &g_entities[tr.entityNum];
}

// what the prop has of the toolgun, for the cgame (bg_tools.h)
static void Tool_Mark( gentity_t *prop, int what, qboolean has ) {
	int mark = ( prop->s.customRGBA[3] & TOOL_MARK ) ? prop->s.customRGBA[3] : TOOL_MARK;

	mark = has ? ( mark | what ) : ( mark & ~what );
	prop->s.customRGBA[3] = ( mark == TOOL_MARK ) ? 0 : (byte)mark;
}

static void Tool_Shot( gentity_t *ent ) {
	G_Sound( ent, CHAN_WEAPON, G_SoundIndex( "sound/weapons/bryar/fire.mp3" ) );
}

// attack: the tool, on the prop he looks at
static void Tool_Use( gentity_t *ent ) {
	const toolState_t	*t = Tool_State( ent );
	gentity_t			*prop = Tool_Aim( ent );

	Tool_Shot( ent );
	if ( !prop )
		return;
	switch ( t->tool ) {
	case TOOL_REMOVER:
		G_PropRemove( prop );
		break;
	case TOOL_COLOR:
		prop->s.customRGBA[0] = t->color[0];
		prop->s.customRGBA[1] = t->color[1];
		prop->s.customRGBA[2] = t->color[2];
		Tool_Mark( prop, TOOL_MARK_COLOR, qtrue );
		break;
	case TOOL_MATERIAL:
		prop->s.genericenemyindex = t->material + 1;
		Tool_Mark( prop, TOOL_MARK_MATERIAL, qtrue );
		break;
	case TOOL_PHYSPROP:
		G_PropPhysics( prop, t->mass, t->bounce, t->grip );
		break;
	case TOOL_FREEZE:
		G_PropFreeze( prop, qtrue );
		break;
	}
}

// alt attack: what the prop has comes into the tool, or the other way of the tool
static void Tool_Take( gentity_t *ent ) {
	toolState_t	*t = Tool_State( ent );
	gentity_t	*prop = Tool_Aim( ent );

	Tool_Shot( ent );
	if ( !prop )
		return;
	switch ( t->tool ) {
	case TOOL_REMOVER:
		G_PropRemove( prop );
		return;
	case TOOL_COLOR:
		if ( !( prop->s.customRGBA[3] & TOOL_MARK_COLOR ) )
			return;
		t->color[0] = prop->s.customRGBA[0];
		t->color[1] = prop->s.customRGBA[1];
		t->color[2] = prop->s.customRGBA[2];
		break;
	case TOOL_MATERIAL:
		if ( !( prop->s.customRGBA[3] & TOOL_MARK_MATERIAL ) || prop->s.genericenemyindex < 1 || prop->s.genericenemyindex > NUM_TOOL_MATERIALS )
			return;
		t->material = prop->s.genericenemyindex - 1;
		break;
	case TOOL_PHYSPROP:
		G_PropGetPhysics( prop, qfalse, &t->mass, &t->bounce, &t->grip );
		break;
	case TOOL_FREEZE:
		G_PropFreeze( prop, qfalse );
		return;
	}
	Tool_Tell( ent );
}

/*
=================
G_ToolFrame

For the frame of g_props.c, with the toolgun in his hand and his buttons of this frame and the ones that are
new in it
=================
*/
void G_ToolFrame( gentity_t *ent, int buttons, int pressed ) {
	(void)buttons;
	if ( pressed & BUTTON_ATTACK )
		Tool_Use( ent );
	else if ( pressed & BUTTON_ALT_ATTACK )
		Tool_Take( ent );
}

/*
=================
G_ToolReload

Use with the toolgun (G_PropGunCmd): the prop he looks at gets back what it had, the freeze tool frees every
frozen prop
=================
*/
void G_ToolReload( gentity_t *ent ) {
	const toolState_t	*t = Tool_State( ent );
	gentity_t			*prop;
	float				mass, bounce, grip;

	if ( t->tool == TOOL_FREEZE ) {
		const int count = G_PropThawAll();

		if ( count )
			trap->SendServerCommand( ent->s.number, va( "print \"^7Toolgun: %i frozen props are free again.\n\"", count ) );
		return;
	}
	if ( !( prop = Tool_Aim( ent ) ) )
		return;
	switch ( t->tool ) {
	case TOOL_COLOR:
		Tool_Mark( prop, TOOL_MARK_COLOR, qfalse );
		break;
	case TOOL_MATERIAL:
		prop->s.genericenemyindex = 0;
		Tool_Mark( prop, TOOL_MARK_MATERIAL, qfalse );
		break;
	case TOOL_PHYSPROP:
		G_PropGetPhysics( prop, qtrue, &mass, &bounce, &grip );
		G_PropPhysics( prop, mass, bounce, grip );
		break;
	}
}

/*
=================
Cmd_Tool_f

vs_tool <tool> [settings]: the tool window of the client chose a tool and its settings. Anybody may set them,
they only work with the toolgun in hand (which needs the sandbox).
=================
*/
void Cmd_Tool_f( gentity_t *ent ) {
	toolState_t	*t;
	char		name[MAX_TOKEN_CHARS], arg[MAX_TOKEN_CHARS];
	int			tool, i;

	if ( !ent->client || ent->s.number >= MAX_CLIENTS )
		return;
	t = Tool_State( ent );
	trap->Argv( 1, name, sizeof( name ) );
	for ( tool = 0; tool < NUM_TOOLS && Q_stricmp( name, toolNames[tool] ); tool++ )
		;
	if ( tool == NUM_TOOLS ) {
		// without a tool he is told what he has
		Tool_Tell( ent );
		return;
	}
	t->tool = tool;

	switch ( tool ) {
	case TOOL_COLOR:
		if ( trap->Argc() >= 5 ) {
			for ( i = 0; i < 3; i++ ) {
				trap->Argv( 2 + i, arg, sizeof( arg ) );
				t->color[i] = (byte)Com_Clampi( 0, 255, atoi( arg ) );
			}
		}
		break;
	case TOOL_MATERIAL:
		if ( trap->Argc() >= 3 ) {
			trap->Argv( 2, arg, sizeof( arg ) );
			for ( i = 0; i < NUM_TOOL_MATERIALS; i++ ) {
				if ( !Q_stricmp( arg, toolMaterials[i].name ) ) {
					t->material = i;
					break;
				}
			}
		}
		break;
	case TOOL_PHYSPROP:
		if ( trap->Argc() >= 5 ) {
			trap->Argv( 2, arg, sizeof( arg ) );
			t->mass = Com_Clamp( 0.1f, 100.0f, atof( arg ) );
			trap->Argv( 3, arg, sizeof( arg ) );
			t->bounce = Com_Clamp( 0.0f, 0.95f, atof( arg ) );
			trap->Argv( 4, arg, sizeof( arg ) );
			t->grip = Com_Clamp( 0.0f, 2.0f, atof( arg ) );
		}
		break;
	}
	Tool_Tell( ent );
}

// a player who leaves: the next one in his slot has the default tool
void G_ToolClear( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS )
		toolStates[clientNum].set = qfalse;
}
