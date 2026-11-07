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

// bg_tools.h -- the tools of the toolgun of the sandbox (issue #3, game/props/g_tools.c), as the game module and
// the cgame both know them: their names in the command vs_tool and the materials of the material tool. Not from
// JA+.
//
// A prop the toolgun changed carries it in its entityState: customRGBA[3] is TOOL_MARK and what it has, the
// colour is in customRGBA[0] to [2] and the material in genericenemyindex, its number in toolMaterials plus one.
// Turrets and siege items keep an icon in genericenemyindex, but no other ET_GENERAL sets customRGBA[3]: the mark
// tells a prop of the toolgun.

#pragma once

#define TOOL_MARK			0x80	// customRGBA[3] of a prop the toolgun changed,
#define TOOL_MARK_COLOR		0x01	//	with these for what it has
#define TOOL_MARK_MATERIAL	0x02

typedef enum {
	TOOL_REMOVER,
	TOOL_COLOR,
	TOOL_MATERIAL,
	TOOL_PHYSPROP,
	TOOL_FREEZE,
	NUM_TOOLS
} tool_t;

// the names in vs_tool <name> ...
static const char *const toolNames[NUM_TOOLS] = { "remover", "color", "material", "physprop", "freeze" };

typedef struct toolMaterial_s {
	const char	*name;		// in vs_tool material <name>
	const char	*title;		// in the tool window and under the crosshair (in English, cgame: CG_VisionText)
	const char	*shader;	// shaders/vision_tools.shader of the client assets, lava the one of the base game
} toolMaterial_t;

static const toolMaterial_t toolMaterials[] = {
	{ "plain",		"Plain",	"vision/tools/plain" },
	{ "chrome",		"Chrome",	"vision/tools/chrome" },
	{ "gold",		"Gold",		"vision/tools/gold" },
	{ "glass",		"Glass",	"vision/tools/glass" },
	{ "ice",		"Ice",		"vision/tools/ice" },
	{ "shield",		"Shield",	"vision/tools/shield" },
	{ "lava",		"Lava",		"gfx/effects/lava" },
};
#define NUM_TOOL_MATERIALS	( (int)( sizeof( toolMaterials ) / sizeof( toolMaterials[0] ) ) )
