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

// tr_automat.cpp -- VisionJK: materials made at load for the surfaces of a level that have none (v_autoMaterials).
//
// Only ffa1 to ffa5 shipped with materials (the vmat_ files, chosen by eye per shader). Every other level was drawn as
// without them. Here a texture of the level gets a class from the words of its path (sand, rust, marble, ...) and
// from how bright it is, and the class gives its roughness, its metalness and the maps tr_matgen.cpp makes from the
// texture. The numbers are those the classes of ffa1 to ffa5 ended with after their checks in the game (blur 2 and
// gentler bumps on the old textures, painted metal rather than bare metal). A material of a shader file always wins:
// a stage that has one never comes here.

#include "tr_local.h"

cvar_t *v_autoMaterials;

typedef struct {
	const char	*name;
	float		roughness, metalness;
	float		normalStrength;		// normalGen, blur 2 and detail 0.3 for all
	float		rmoVariation, rmoOcclusion;	// rmoGen, 0 0: none
} autoClass_t;

enum {
	AC_GENERIC, AC_STONE, AC_ROCK, AC_FLOOR, AC_MARBLE, AC_PLASTER, AC_CONCRETE, AC_SAND, AC_RUST, AC_PAINTED,
	AC_METAL, AC_GRATE, AC_WOOD, AC_PANEL, AC_NUM
};

static const autoClass_t autoClasses[AC_NUM] = {
	//  name			rough	metal	normal	rmo var	rmo occ
	{ "generic",		0.80f,	0.00f,	0.40f,	0.0f,	0.0f },
	{ "stone",			0.85f,	0.00f,	0.45f,	0.0f,	0.0f },
	{ "rock",			0.90f,	0.00f,	0.55f,	0.0f,	0.0f },
	{ "floor",			0.80f,	0.05f,	0.40f,	0.2f,	0.4f },
	{ "marble",			0.35f,	0.00f,	0.20f,	0.0f,	0.0f },
	{ "plaster",		0.85f,	0.00f,	0.40f,	0.0f,	0.0f },
	{ "concrete",		0.85f,	0.00f,	0.45f,	0.0f,	0.0f },
	{ "sand",			0.95f,	0.00f,	0.35f,	0.0f,	0.0f },
	{ "rust",			0.80f,	0.00f,	0.55f,	0.3f,	0.5f },
	{ "painted",		0.60f,	0.10f,	0.40f,	0.2f,	0.4f },
	{ "metal",			0.50f,	0.30f,	0.40f,	0.2f,	0.4f },
	{ "grate",			0.55f,	0.50f,	0.70f,	0.0f,	0.0f },
	{ "wood",			0.65f,	0.00f,	0.45f,	0.0f,	0.0f },
	{ "panel",			0.45f,	0.00f,	0.35f,	0.0f,	0.0f },
};

// words of the path and the class they give, the first that is found wins (so rust before metal, marble before floor)
static const struct {
	const char	*word;
	int			cls;
} autoWords[] = {
	{ "grate", AC_GRATE }, { "grating", AC_GRATE }, { "mesh", AC_GRATE },
	{ "rust", AC_RUST },
	{ "marble", AC_MARBLE }, { "granite", AC_MARBLE }, { "polish", AC_MARBLE },
	{ "sand", AC_SAND }, { "dirt", AC_SAND }, { "mud", AC_SAND }, { "dust", AC_SAND }, { "ground", AC_SAND },
	{ "rock", AC_ROCK }, { "cliff", AC_ROCK }, { "cave", AC_ROCK }, { "boulder", AC_ROCK },
	{ "brick", AC_STONE }, { "stone", AC_STONE }, { "block", AC_STONE }, { "cobble", AC_STONE }, { "temple", AC_STONE },
	{ "concrete", AC_CONCRETE }, { "cement", AC_CONCRETE },
	{ "plaster", AC_PLASTER }, { "stucco", AC_PLASTER },
	{ "wood", AC_WOOD }, { "plank", AC_WOOD }, { "bark", AC_WOOD },
	{ "steel", AC_METAL }, { "chrome", AC_METAL }, { "silver", AC_METAL }, { "brass", AC_METAL }, { "copper", AC_METAL },
	{ "gold", AC_METAL }, { "alum", AC_METAL },
	{ "computer", AC_PANEL }, { "console", AC_PANEL }, { "screen", AC_PANEL }, { "monitor", AC_PANEL },
	// metal, panels and pipes of the old levels are painted (the metal 0.6 of the first classes was too much)
	{ "metal", AC_PAINTED }, { "pipe", AC_PAINTED }, { "panel", AC_PAINTED }, { "imperial", AC_PAINTED }, { "tech", AC_PAINTED },
	{ "door", AC_PAINTED }, { "vent", AC_PAINTED }, { "trim", AC_PAINTED }, { "beam", AC_PAINTED }, { "girder", AC_PAINTED },
	{ "floor", AC_FLOOR }, { "tile", AC_FLOOR }, { "plate", AC_FLOOR },
};

// what is no lit surface, or what the lit path can not take (the kinds left out of ffa1 to ffa5 by eye)
static const char *autoSkipWords[] = {
	"glass", "crystal", "window", "water", "lava", "slime", "fog", "force", "field", "sky", "flare", "light", "lamp",
	"glow", "neon", "sign", "decal", "blood", "scorch", "shadow", "caulk", "trigger", "clip", "hint", "nodraw",
	"system/", "common/", "sfx/", "effects/", "gfx/", "models/", "skies/", "/env",
};

/*
===============
R_AutoMaterialClass

The class of the texture of a lit surface of the level by the name of its shader and how bright its texture is, into
out. qfalse: none for it (not a surface the lit path is made for).
===============
*/
qboolean R_AutoMaterialClass( const char *shaderName, const image_t *image, autoMaterial_t *out ) {
	char name[MAX_QPATH];
	int cls = AC_GENERIC;

	Q_strncpyz( name, shaderName, sizeof( name ) );
	Q_strlwr( name );
	for ( size_t i = 0; i < ARRAY_LEN( autoSkipWords ); i++ ) {
		if ( strstr( name, autoSkipWords[i] ) ) {
			return qfalse;
		}
	}
	for ( size_t i = 0; i < ARRAY_LEN( autoWords ); i++ ) {
		if ( strstr( name, autoWords[i].word ) ) {
			cls = autoWords[i].cls;
			break;
		}
	}

	const autoClass_t *c = &autoClasses[cls];

	out->className = c->name;
	out->roughness = c->roughness;
	out->metalness = c->metalness;
	out->normalStrength = c->normalStrength;
	out->rmoVariation = c->rmoVariation;
	out->rmoOcclusion = c->rmoOcclusion;
	// A texture that is nearly black: the shine at a flat angle, which no F0 scales, outshone its light (floor_byss of
	// ffa1, roughness 1 then); a dark one a little rougher
	if ( image && image->averageLuma < 0.08f ) {
		out->roughness = 1.0f;
		out->metalness = 0.0f;
	} else if ( image && image->averageLuma < 0.15f ) {
		out->roughness = Q_min( 1.0f, out->roughness + 0.1f );
	}
	return qtrue;
}
