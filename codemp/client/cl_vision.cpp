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

// cl_vision.cpp -- small parts of the engine of this tree that several files share: where a file of the home folder
// lies on the disk, and the cvars that tell a cgame of this tree what the engine can do.

#include "client.h"
#include "client/sound/spatial/snd_spatial.h"

// where a file of the home folder (a pak, a part that comes in) lies on the disk
const char *CL_HomeOSPath( const char *local ) {
	char *path = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), local, "" );

	path[strlen( path ) - 1] = '\0';
	return path;
}

/*
====================
CL_VisionCapabilities

Read-only cvars that are 1: the cgame of this tree looks at them to know that this engine has a call or a screen
effect, and draws what it always drew with another engine. Registered in this order by CL_Init().
====================
*/
static const char *const clCapabilities[] = {
	"v_cgameChat",			// the calls of its chat box (ext of cgameImport_t)
	"v_cgameScene",			// the calls for text and pictures of its own in the scene
	"v_cgameSpatialDebug",
	"v_cgameSpatialMovers",
	"v_cgameUIPlane",		// the menus on a plane in the level
	"v_cgameDoF",			// the blur of the scene around a distance
	"v_cgameScope",			// the view through the scope of a weapon (R_SetScopeEffect)
	"v_cgameScreenFx",		// the screen effects (R_SetScreenEffect)
	"v_cgameVideo",			// the videos on the screens of the level (ext.Video_*, cl_video.cpp)
	"v_cgameImage",			// picture files for the cgame (ext.R_LoadImageFit, the drawing board)
	"v_cgameLiquid",		// the water of the toys drawn by the renderer (ext.R_AddLiquid)
	"v_cgameWet",			// the puddles and wet patches of the toys drawn by the renderer (ext.R_AddWet)
	"v_cgamePaint",			// the sprays as paint and their pictures (ext.R_PaintPicture, ext.R_AddPaint, cl_sprays.cpp)
	"v_cgameLoopVolume",	// looping sounds at a volume of their own (ext.S_AddLoopingSoundVolume, the motor of the RC car)
	"v_cgameLineLight",		// tubes of light for the saber blades (ext.R_AddLineLight)
	"v_cgameSaberBlade",	// saber blades drawn by the renderer in 3D (ext.R_AddSaberBlade; NULL with a renderer that has none)
	"v_cgameCutEntity",		// ext.R_AddCutEntity: a body drawn cut by a plane (cgame/cg_slice.c)
	"v_cgameSetBones",		// ext.G2_BoneNumber and ext.G2_SetBonesWorld: the ragdoll of cut bodies (cgame/cg_ragdoll.c)
	"v_cgameBackdrop",		// ext.R_DrawBackdrop: frosted glass behind the HUD panels and the chat box
	"v_cgameSparks",		// ext.R_EmitSparks: sparks the renderer simulates (cgame/cg_effects.c CG_GpuSparks)
	"v_cgameSurfaceMaterial",	// ext.R_SurfaceMaterial: what the texture under a ray is made of (cgame/weapons/cg_impact.c)
	"v_cgameSoundPitch",	// ext.S_StartSoundPitch: a sound started higher (cgame/weapons/cg_boltsound.c, a weapon running empty)
	"v_cgameBandLight",		// ext.R_AddBandLight: a light only round a plane, the ring of a thermal detonator (cgame/weapons/cg_thermal.c)
	"v_cgameGrassPush",		// ext.R_AddGrassPush: the grass of the level parts round players and pushes (cgame/level/cg_gust.c)
	"v_cgameSurfacePick",	// ext.R_SurfacePick: the drawn surface under a ray and the contents of its shader (cgame/level/cg_motes.c)
	"v_cgameLamps",			// ext.R_LampsNear: the lamps of the level near a place (cgame/level/cg_motes.c, dust in their light)
	"v_cgameSmoke",			// ext.R_SetSmoke: smoke as a volume the renderer marches through (cgame/level/cg_smoke.c)
	"v_cgameTraceHoles",	// ext.CM_TraceHoles: traces with holes where the portals of the portal gun are (cgame/cg_predict.c)
	"v_cgameFeed",			// ext.Con_FeedRead and ext.Con_FeedNewest: the notify lines for the feed of the cgame (cl_console.cpp)
	"v_cgamePortals",		// ext.R_AddPortal: the portals of the portal gun drawn by the renderer (cgame/portal/cg_portal_draw.c; NULL with a renderer that has none)
	"v_cgameShadowLight",	// ext.R_AddShadowLight: a light with a say in its shadows (cgame/force/cg_forceaura.c; NULL with a renderer that has none)
	"v_cgameVoteKeys",		// F1 and F2 answer a vote while the cgame sets v_voteKeys (cl_keys.cpp CL_KeyDownEvent)
	"v_screenFxDrops",		// drops of water on the view (SCREENFX_DROP of re.ext.SetScreenEffect)
	"v_screenFxSnow",		// clumps of snow on the view (SCREENFX_SNOW, the snowball)
	"v_screenFxBlood",		// SCREENFX_BLOOD: blood on the view (cgame/screenfx/cg_screenfx.c, a cut close by)
	"v_screenFxDirt",		// SCREENFX_DIRT: dirt on the view (cgame/screenfx/cg_screenfx.c, an explosion close by)
};

// ext.R_SurfaceMaterial of the cgame: MATERIAL_NONE where the renderer can not pick or the table does not know it
int CL_SurfaceMaterial( const vec3_t start, const vec3_t end ) {
#ifdef JP_SPATIAL
	materialPick_t pick;

	if ( re && re->ext.MaterialPick && re->ext.MaterialPick( start, end, &pick ) ) {
		return S_SpatialShaderMaterial( re->ShaderNameFromIndex( pick.shader ) );
	}
#endif
	return MATERIAL_NONE;
}

// ext.R_SurfacePick of the cgame: the first surface of the level that is drawn along the ray. qfalse: none, or the
// renderer can not pick
qboolean CL_SurfacePick( const vec3_t start, const vec3_t end, vec3_t point, int *contents ) {
	materialPick_t pick;

	if ( !re || !re->ext.MaterialPick || !re->ext.MaterialPick( start, end, &pick ) ) {
		return qfalse;
	}
	VectorCopy( pick.point, point );
	*contents = pick.contents;
	return qtrue;
}

void CL_VisionCapabilities( void ) {
	size_t i;

	for ( i = 0; i < ARRAY_LEN( clCapabilities ); i++ ) {
		Cvar_Get( clCapabilities[i], "1", CVAR_ROM, "" );
	}
}
