/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

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

#pragma once

#include "../qcommon/q_shared.h"

#define	MAX_DLIGHTS		32			// can't be increased, because bit flags are used on surfaces
#define	REFENTITYNUM_BITS	12		// 11 in JKA; 12 since the sort key of a drawsurf is 64 bit (2026-10-24, the ambient
									// effects of the JoF server fill 2047 slots and the rest of the scene was dropped)
#define	REFENTITYNUM_MASK	((1<<REFENTITYNUM_BITS) - 1)
// the last N-bit number (2^REFENTITYNUM_BITS - 1) is reserved for the special world refentity,
//  and this is reflected by the value of MAX_REFENTITIES (which therefore is not a power-of-2)
#define	MAX_REFENTITIES		((1<<REFENTITYNUM_BITS) - 1)
#define	REFENTITYNUM_WORLD	((1<<REFENTITYNUM_BITS) - 1)

#define	MAX_MINI_ENTITIES	1024

// renderfx flags
#define	RF_MINLIGHT			0x00001	// allways have some light (viewmodel, some items)
#define	RF_THIRD_PERSON		0x00002	// don't draw through eyes, only mirrors (player bodies, chat sprites)
#define	RF_FIRST_PERSON		0x00004	// only draw through eyes (view weapon, damage blood blob)
#define	RF_DEPTHHACK		0x00008	// for view weapon Z crunching
#define RF_NODEPTH			0x00010	// No depth at all (seeing through walls)

#define RF_VOLUMETRIC		0x00020	// fake volumetric shading

#define	RF_NOSHADOW			0x00040	// don't add stencil shadows

#define RF_LIGHTING_ORIGIN	0x00080	// use refEntity->lightingOrigin instead of refEntity->origin
									// for lighting.  This allows entities to sink into the floor
									// with their origin going solid, and allows all parts of a
									// player to get the same lighting
#define	RF_SHADOW_PLANE		0x00100	// VisionJK: casts a shadow the renderer traces from the lights of the level (cg_shadows 2)
#define	RF_WRAP_FRAMES		0x00200	// mod the model frames by the maxframes to allow continuous
										// animation without needing to know the frame count

#define RF_FORCE_ENT_ALPHA	0x00400 // override shader alpha settings
#define RF_RGB_TINT			0x00800 // override shader rgb settings

#define	RF_SHADOW_ONLY		0x01000	//add surfs for shadowing but don't draw them -rww

#define	RF_DISTORTION		0x02000	//area distortion effect -rww

#define RF_FORKED			0x04000	// override lightning to have forks
#define RF_TAPERED			0x08000	// lightning tapers
#define RF_GROW				0x10000	// lightning grows from start to end during its life

#define RF_DISINTEGRATE1	0x20000	// does a procedural hole-ripping thing.
#define RF_DISINTEGRATE2	0x40000	// does a procedural hole-ripping thing with scaling at the ripping point

#define RF_SETANIMINDEX		0x80000	//use backEnd.currentEntity->e.skinNum for R_BindAnimatedImage

#define RF_ALPHA_DEPTH		0x100000 //depth write on alpha model

#define RF_FORCEPOST		0x200000 //force it to post-render -rww
// Drawn after the dynamic glow has been added to the picture, in a view that adds one (r_DynamicGlow): a light
// that only the glow draws around the entity (RGBA_SHADER_BLUR) then lies behind it and not across it. In a view
// without a glow the flag does nothing.
#define RF_AFTERGLOW		0x400000
// With RF_DISTORTION, in rd-vulkan-vision: the model may lie at any angle, it need not face the view. Its texture
// coordinates say where a vertex lies when nothing bends it, (0, (s - 0.5) * 2r, (t - 0.5) * 2r) in the model,
// r = shaderTexCoord[0], and it shows what the screen has there: where a vertex was moved, the picture under it
// moves with it (the shockwave of the boombox, sound_wave.md3). shaderTexCoord[1]: how many times as far, 0 is 1.
#define RF_REFRACT_REST		0x800000

// refdef flags
// what a shader for a picture a module made is like (RE_RegisterShaderRGBA). The first one is the qtrue the
// call took before it had flags.
#define RGBA_SHADER_GLOW	1		// added to what is behind it, and the dynamic glow takes it
#define RGBA_SHADER_OVER	2		// drawn over all of the scene: nothing in front of it hides it
#define RGBA_SHADER_BEHIND	4		// drawn before the pictures without it, where a glow is drawn, but blended as they are
#define RGBA_SHADER_HULL	8		// for a model as its custom shader: an outline around it. Only what faces away is
									// drawn, pushed out along the normals, in the colour of the entity
#define RGBA_SHADER_BLUR	16		// Drawn into the buffer of the dynamic glow and nowhere else, in the colour of the
									// entity: what is seen of it is the blurred light the renderer makes of that buffer
									// (r_DynamicGlow), added to the picture. Nothing at all with r_DynamicGlow 0.
// with RGBA_SHADER_HULL: how far the outline stands off the model, in tenths of a unit, in the bits above the
// flags. Without it 1.1 units. Several of them around one model, the wider the fainter, are a soft light.
#define RGBA_SHADER_HULL_WIDTH( tenths )	( RGBA_SHADER_HULL | ( ( ( tenths ) & 0xff ) << 8 ) )
#define RGBA_SHADER_DECAL	32		// a mark on the level: drawn among the decals, a little in front of its wall

#define RDF_NOWORLDMODEL	1		// used for player configuration screen
#define RDF_HYPERSPACE		4		// teleportation effect

#define RDF_SKYBOXPORTAL	8
#define RDF_DRAWSKYBOX		16		// the above marks a scene as being a 'portal sky'.  this flag says to draw it or not

#define RDF_AUTOMAP			32		//means this scene is to draw the automap -rww
#define	RDF_NOFOG			64		//no global fog in this scene (but still brush fog) -rww
#define RDF_ForceSightOn	128		//using force sight

// What the cgame asks of the view of the level while a weapon is zoomed (re.ext.SetScopeEffect), once a frame.
#define SCOPEFX_LENS		1		// the lens: bent and coloured towards its rim, the scene behind it out of focus
									// around what the crosshair is on, outside of it soft and dark
#define SCOPEFX_RETICLE		4		// a reticle on the lens, and the ammo, the zoom and the charge as arcs at its rim
#define SCOPEFX_TRACKER		8		// the motion tracker: the lens dim and red with the edges of the level, but the
									// boxes keep its colours
#define SCOPEFX_BINOCULAR	16		// two lenses side by side, the binoculars
#define SCOPEFX_MAX_BOXES	8

typedef struct scopeEffect_s {
	int			flags;				// SCOPEFX_*
	float		center[2];			// the middle of the lens, fractions of the view from its upper left corner
	float		radius;				// of the lens, a fraction of the height of the view
	float		zoom;				// 0 not zoomed in, 1 all the way
	float		motion;				// how far the tracker has faded in, 0 to 1
	float		time;				// seconds, for what moves on the lens
	float		charge;				// of the alt fire, 0 to 1, below 0: it is not charging
	float		ammo;				// what is left of the weapon's ammo, 0 to 1
	int			numBoxes;			// SCOPEFX_TRACKER: the boxes, fractions of the view from its upper left corner:
	float		boxes[SCOPEFX_MAX_BOXES][4];	// left, top, right, bottom
} scopeEffect_t;

// What the cgame asks of the view of the level besides the scope (re.ext.SetScreenEffect), once a frame: places
// where the picture bends or flashes (sources) and how the whole of it is coloured.
#define SCREENFX_RING		1		// a ring that pushes the picture outwards, the wave of an explosion
#define SCREENFX_HEAT		2		// air that shimmers along the line from a to b, hot gas over a flame
#define SCREENFX_SMOKE		3		// a puff of smoke at a
#define SCREENFX_RIPPLE		4		// waves that run out from a (seed 1) or in to it (seed -1): a Force push or pull
#define SCREENFX_FLASH		5		// a bright star of light at a: blades that clash
#define SCREENFX_GHOSTSMOKE	7		// a ghost of Force Sight from a (feet) to b (head): he puts smoke into the smoke of
										// the view, which rises and smears as the eye moves
#define SCREENFX_DROP		8		// a drop of water on the view at a, radius its size, width the trail it left above
										// it as it ran down, strength how wet. Only with an engine that sets the cvar
										// v_screenFxDrops to 1: the renderer of an older one draws it as a flash.
#define SCREENFX_SNOW		9		// a clump of snow on the view at a, radius its size, strength how much of it is
										// left as it melts, seed its shape. Only with an engine that sets the cvar
										// v_screenFxSnow to 1.
#define SCREENFX_BLOOD		10		// a splat of blood on the view at a, radius its size, width the trail above it where it
										// ran down, strength how wet it still is, seed its shape. Only with an engine that sets
										// the cvar v_screenFxBlood to 1 (a cut close by, cgame/cg_slice.c).
#define SCREENFX_MAX_SOURCES	48

typedef struct screenFxSource_s {
	int			kind;				// SCREENFX_*
	float		a[2], b[2];			// fractions of the view from its upper left corner. A ring has its middle at a
	float		radius;				// a fraction of the height of the view: of the ring, of the air around the line
	float		width;				// of the ring, the same unit. Heat: around the line that much stays still
	float		strength;			// 0 to 1
	float		distance;			// from the eye, units of the level. Where the level is nearer, it is not seen
	float		seed;				// smoke: tells one puff from the next, its shape
} screenFxSource_t;

typedef struct screenEffect_s {
	float		time;				// seconds, for what moves
	float		frameTime;			// seconds since the frame before
	float		split;				// the colours part towards the edges, 0 to 1
	float		tint[4];			// the picture towards this colour, a: how far, 0 to 1
	float		desaturate;			// 0 to 1: grey
	float		vignette[4];		// the edges of the view towards this colour, a: how far, 0 to 1
	float		vignetteArc[3];		// and only on the side of this direction on the view (x to the right, y up), z
									// wide: 0 all around, 1 half of it
	float		radialBlur;			// streaks from the middle of the view outwards, 0 to 1
	float		sight;				// Force Sight: the level cold and grey with its edges drawn, 0 to 1
	float		rage;				// the red burns, the rest drains, dark red closes in, 0 to 1, with every beat of
									// ragePulse 0 to 1 more
	float		ragePulse;
	float		protect;			// a dome of energy around the eye, seen at its edge, 0 to 1. A hit sends a ripple
	float		protectHit;			// over it from the side of the arc of the vignette, protectHit 1 to 0
	float		absorb;				// streams of light drawn in from the edges, 0 to 1, brighter by absorbPulse
	float		absorbPulse;
	float		viewShift[2];		// how far the picture moved on the view since the frame before as the eye turned,
									// fractions of the view, x to the right, y down: the smoke of the ghosts trails
	float		water;				// under water, 0 to 1: the picture sways, fades into waterColor with the
	float		waterColor[3];		// distance, light plays on the level
	float		drops;				// drops running down the view after surfacing, 0 to 1
	int			numSources;
	screenFxSource_t	sources[SCREENFX_MAX_SOURCES];
} screenEffect_t;

// A piece of clear water the cgame puts into the scene (re.ext.AddLiquid): a round body from a to b, as thick as the
// two radii, with the ends rounded. A jet is a chain of them, a drop one whose a and b are close. The renderer
// draws them after the scene, bending what lies behind them (rd-vulkan-vision, shaders/liquid.frag).
#define LIQUID_MAX_SEGMENTS	1024	// in one frame, all of them together

typedef struct liquidSegment_s {
	vec3_t		a, b;
	float		radius[2];			// at a and at b
	float		flow[2];			// where the water at a and at b is on the water itself, units: the surface moves with it
	float		breakup;			// 0 a smooth jet, 1 one that is falling apart into drops
} liquidSegment_t;

// A wet place on the level the cgame puts into the scene (re.ext.AddWet): where water came to a floor or a wall. The
// renderer darkens the ground there as wet ground is, makes it shine, and where water stands it mirrors the room
// (rd-vulkan-vision, shaders/wet.frag). Its edge is ragged, a wall gets streaks that run down. Other kinds: the
// print of a wet foot or a piece of the track of a wet tire, a ring that runs over standing water from where
// something came into it, and a splat of snow that melts.
#define WET_MAX_SPOTS	128		// in one frame

#define WET_PLACE		0
#define WET_FOOT_LEFT	1		// radius: half the length of the foot, wet: how wet the print still is, seed: yaw / 360
#define WET_FOOT_RIGHT	2
#define WET_RING		3		// radius: how far it runs out, wet: how strong, flow: 0 to 1 of its life
#define WET_TRACK		4		// a piece of the track of a wet tire from 2 origin - hit to hit, seed: half its width
#define WET_SNOW		5		// a splat of snow, slid down to origin from hit, wet: how much of it is left
#define WET_BLOOD		6		// a pool of blood with droplets round it (cgame/cg_slice.c), wet: how far it has run out
#define WET_BLOOD_STREAK	7	// a smear of blood from 2 origin - hit to hit where a cut part was dragged, puddle: half
								// its width, wet: how fresh and full, seed: the shape of its edge

typedef struct wetSpot_s {
	vec3_t		origin;
	vec3_t		normal;				// of the surface, away from it
	vec3_t		hit;				// where the water lands now: the rings of a puddle run out from there
	float		radius;				// units, the most the water reaches
	float		wet;				// 0 to 1: how far towards radius it is wet, it dries from the edge in
	float		puddle;				// 0 to 1: how far towards radius water stands (a floor only)
	float		flow;				// 0 to 1: how much water lands at hit right now
	float		seed;				// 0 to 1, the shape of its edge
	int			kind;				// WET_PLACE, ...
} wetSpot_t;

// A picture sprayed onto the level (re.ext.AddPaint, cgame/toys/cg_spray.c). The renderer keeps a sheet of pictures
// for each of PAINT_SLOTS slots (re.ext.PaintImage): a still picture, or the frames of an animated one in a grid.
// rd-vulkan-vision lays a decal as paint onto the surface (shaders/paint.frag): the relief of the wall shows through,
// the edge is overspray, fresh paint shines and runs down a wall in drips until it dries. A renderer
// that can not says qfalse and the cgame draws a mark with the shader that PaintImage returned.
#define PAINT_MAX_DECALS	64		// in one frame
#define PAINT_SLOTS			40		// pictures the renderer keeps: one a player and some over
#define PAINT_SHEET			512		// a sheet is this many pixels each way at most
#define PAINT_FRAMES		16		// frames of an animated picture at most

typedef struct paintDecal_s {
	vec3_t		origin;
	vec3_t		normal;				// of the surface, away from it
	vec3_t		up;					// along the surface: where the top of the picture is
	float		radius;				// half the side of the picture, units
	float		reveal;				// 0 to 1: how much of it is sprayed on yet
	float		fresh;				// 1 just sprayed, wet paint, down to 0 dry
	float		seed;				// 0 to 1, the shape of its overspray and drips
	int			slot;				// of PaintImage
	float		frame[4];			// the part of the sheet shown now: left, top, width, height, 0 to 1
} paintDecal_t;

// a picture as the engine made a sheet of it (ext.R_PaintPicture of the cgame)
typedef struct paintPicture_s {
	int			frames;				// 1: a still picture
	int			columns;			// of the sheet: frame n is at column n % columns, row n / columns
	int			cell;				// pixels of a frame each way
	int			sheet;				// pixels of the sheet each way
	int			delays[PAINT_FRAMES];	// msec each frame is shown
} paintPicture_t;

// VisionJK: the material debug menu (refexport_t ext.Material*). A material belongs to a stage of a shader text,
// named by the number of that stage in the text (sourceStage), and all shaders of that name share it.
#define MATERIAL_MAX_STAGES		8

// pictures of the debug menu of the client (refexport_t ext.UiTexture): its font atlas and such
#define UI_TEXTURES				8

// the fields of materialParms_t that ext.MaterialSet writes
#define MATP_PBR				(1 << 0)
#define MATP_ROUGHNESS			(1 << 1)
#define MATP_METALNESS			(1 << 2)
#define MATP_PARALLAX			(1 << 3)	// parallaxDepth and parallaxPlane
#define MATP_SPECULAR			(1 << 4)
#define MATP_WRAPLIGHT			(1 << 5)
#define MATP_SPARKLE			(1 << 6)
#define MATP_NORMALMAP			(1 << 7)	// normalMap and normalHeight
#define MATP_RMOMAP				(1 << 8)	// rmoMap and ormOrder
#define MATP_GEN				(1 << 9)	// gen: the maps are made again
#define MATP_GLASS				(1 << 10)	// transmission, ior, thickness, absorption
#define MATP_SURFACE			(1 << 11)	// clearcoat, sheen, anisotropy, emission, specGlossMap, specularReflectance, normalScale
#define MATP_TESS				(1 << 12)	// tessHeight, tessPlane, tessDensity
#define MATP_ALL				0x1fff

// the maps the renderer makes from the texture of a stage when it loads (shader keywords albedoGen, normalGen,
// rmoGen of .oshader files, tr_matgen.cpp), so a material needs no picture files
#define MATGEN_ALBEDO			(1 << 0)
#define MATGEN_NORMAL			(1 << 1)
#define MATGEN_RMO				(1 << 2)

typedef struct materialGen_s {
	int			flags;				// MATGEN_*
	// albedoGen <rrggbb> <delight> <saturation>: the light painted into the texture taken out (0 to 1), tinted,
	// its colour stronger or weaker (0 to 2)
	float		tint[3];
	float		delight;
	float		saturation;
	// normalGen <strength> <blur> <detail> [invert]: the bumps from the brightness of the texture (bright stands
	// out), the height in the alpha for parallax
	float		normalStrength;		// 0 to 8
	float		normalBlur;			// 0 to 8 pixels
	float		normalDetail;		// 0 to 1, the fine detail mixed into the blurred height
	qboolean	normalInvert;		// dark stands out
	// rmoGen <variation> <occlusion>: roughness that varies with the texture around the roughness of the material
	// (darker cracks rougher), and occlusion in what lies lower than its surroundings
	float		rmoVariation;		// 0 to 1
	float		rmoOcclusion;		// 0 to 1
} materialGen_t;

typedef struct materialParms_s {
	qboolean	pbr;				// lit per pixel with GGX
	float		roughness;			// 0 to 1
	float		metalness;			// 0 to 1
	float		parallaxDepth;		// 0 to 0.2, needs normalHeight
	float		parallaxPlane;		// where the polygon lies in the height, 0 bottom to 1 top
	float		tessHeight;			// tessellation: units the height moves the surface, 0 none (needs normalHeight)
	float		tessPlane;			//	where the polygon lies in that height, 0 bottom (the surface rises out of it) to 1 top
	float		tessDensity;		//	pixels a triangle edge is cut down to on the screen
	float		specular[3];		// strength, exponent, clear coat (stages of models, not pbr)
	float		wrapLight;
	float		sparkle;
	char		normalMap[MAX_QPATH];	// image name, "" none
	qboolean	normalHeight;		// the height is in the alpha of the normal map
	char		rmoMap[MAX_QPATH];	// image name, "" none
	qboolean	ormOrder;			// occlusion, roughness, metalness instead of roughness, metalness, occlusion
	materialGen_t	gen;			// what is made from the texture; a made map stands before the named one
	// glass: the screen behind shows through, bent and tinted (transmission > 0)
	float		transmission;		// 0 to 1
	float		ior;				// 1 to 3, 1.5 glass
	float		thickness;			// units, 0 a thin pane
	float		absorption[3];		// what is left of each colour after absorptionDistance units in it
	float		absorptionDistance;
	// more of the surface
	float		clearcoat;			// 0 to 1, a clear coat over it
	float		clearcoatRoughness;
	float		sheen[3];			// cloth: the colour of its sheen, 0 none
	float		sheenRoughness;
	qboolean	sheenAlbedo;		// the sheen has the colour of the albedo (rend2's cloth)
	float		anisotropy;			// -1 to 1, brushed metal
	float		anisotropyRotation;	// degrees
	float		emission[3];		// the colour the bright parts give off
	float		emissionStrength;	// 0 none
	float		emissionThreshold;	// 0 to 1, how bright a part must be to give off light
	char		specGlossMap[MAX_QPATH];	// rend2's specular map (F0 in rgb, gloss in alpha), "" none
	float		specularReflectance;	// F0 of what is not metal, 0 for 0.04
	float		normalScale;		// strength of the bumps, 1 as the map has them
} materialParms_t;

// what a stage is (materialStage_t flags)
#define MATS_LIGHTMAP			(1 << 0)	// lit by the lightmap of the level
#define MATS_ADD				(1 << 1)	// adds its colour to what is under it (a sign, a glow)
#define MATS_BLEND				(1 << 2)	// blended in some other way
#define MATS_GLOW				(1 << 3)	// in the pass of the dynamic glow
#define MATS_MATERIAL			(1 << 4)	// it has a material
#define MATS_ENTITY				(1 << 5)	// lit by the light of its entity (models)

typedef struct materialStage_s {
	int			sourceStage;
	int			flags;				// MATS_*
	char		image[MAX_QPATH];	// its texture
	float		luma;				// how bright the texture is on average, of its sRGB values, 0 to 1
} materialStage_t;

// what a shader is (materialInfo_t flags)
#define MATI_SCRIPT				(1 << 0)	// from the text of a .shader or .oshader file
#define MATI_SKY				(1 << 1)
#define MATI_FOG				(1 << 2)
#define MATI_NODRAW				(1 << 3)	// no stage is drawn
#define MATI_LEVEL				(1 << 4)	// surfaces of the level have it

typedef struct materialInfo_s {
	qhandle_t	shader;
	char		name[MAX_QPATH];
	int			flags;				// MATI_*
	float		area;				// square units of the surfaces of the level with it
	float		emissive[4];		// emissive light: r, g, b and the light, all 0 when it gives none
	float		surfaceLight;		// q3map_surfacelight
	int			numStages;
	materialStage_t	stages[MATERIAL_MAX_STAGES];
} materialInfo_t;

typedef struct materialPick_s {
	qhandle_t	shader;
	vec3_t		point;
	vec3_t		normal;				// of the surface, towards the start of the ray
	float		distance;			// from the start of the ray, units
} materialPick_t;

// VisionJK: light to judge materials by (refexport_t ext.SetLightOverride): lamps lit per pixel on every surface
// with a material, the light of the lightmap swapped for an even one, another sun, another exposure
#define DEBUG_LIGHTS			8

typedef enum {
	DLT_POINT,
	DLT_SPOT,
	DLT_DIRECTIONAL
} debugLightType_t;

typedef struct debugLight_s {
	int			type;				// debugLightType_t
	vec3_t		origin;				// point and spot
	vec3_t		direction;			// where it shines (spot, directional), length 1
	vec3_t		color;				// 0 to 1
	float		intensity;			// the light it gives at 100 units, 1 as bright as white at full light
	float		range;				// units it reaches, 0 without end (point, spot)
	float		innerAngle;			// spot: full light within this half angle, degrees
	float		outerAngle;			//	none beyond this one
} debugLight_t;

typedef struct lightOverride_s {
	int			numLights;
	debugLight_t lights[DEBUG_LIGHTS];
	qboolean	replaceLight;		// the lightmap and the light grid of the surfaces with a material give way to ambient
	vec3_t		ambient;			//	an even light from all round, 0 to 1
	qboolean	sun;				// another sun for the shadows the renderer traces and the models
	vec3_t		sunDirection;		//	towards the sun, length 1
	vec3_t		sunColor;			//	0 to 1
	float		exposure;			// the whole picture times this, 1 as it is
} lightOverride_t;

extern int	skyboxportal;
extern int	drawskyboxportal;

typedef byte color4ub_t[4];

typedef struct polyVert_s {
	vec3_t		xyz;
	float		st[2];
	byte		modulate[4];
} polyVert_t;

typedef struct poly_s {
	qhandle_t			hShader;
	int					numVerts;
	polyVert_t			*verts;
} poly_t;

typedef enum {
	RT_MODEL,
	RT_POLY,
	RT_SPRITE,
	RT_ORIENTED_QUAD,
	RT_BEAM,
	RT_SABER_GLOW,
	RT_ELECTRICITY,
	RT_PORTALSURFACE,		// doesn't draw anything, just info for portals
	RT_LINE,
	RT_ORIENTEDLINE,
	RT_CYLINDER,
	RT_ENT_CHAIN,

	RT_MAX_REF_ENTITY_TYPE
} refEntityType_t;

typedef struct miniRefEntity_s
{
	refEntityType_t		reType;
	int					renderfx;

	qhandle_t			hModel;				// opaque type outside refresh

	// most recent data
	matrix3_t			axis;			// rotation vectors
	qboolean			nonNormalizedAxes;	// axis are not normalized, i.e. they have scale
	vec3_t				origin;				// also used as MODEL_BEAM's "from"

	// previous data for frame interpolation
	vec3_t				oldorigin;			// also used as MODEL_BEAM's "to"

	// texturing
	qhandle_t			customShader;		// use one image for the entire thing

	// misc
	byte				shaderRGBA[4];		// colors used by rgbgen entity shaders
	vec2_t				shaderTexCoord;		// texture coordinates used by tcMod entity modifiers

	// extra sprite information
	float				radius;
	float				rotation;			// size 2 for RT_CYLINDER or number of verts in RT_ELECTRICITY

	// misc
	float		shaderTime;			// subtracted from refdef time to control effect start times
	int			frame;				// also used as MODEL_BEAM's diameter

} miniRefEntity_t;

// VisionJK: a saber blade the renderer draws in 3D (refexport_t ext.AddSaberBladeToScene)
// The GPU profiler of the client (ext.GpuProfile, v_profiler): what the device took for the parts of the frames of
// the last window of about half a second
#define GPU_PROFILE_PASSES		32
#define GPU_PROFILE_HISTORY		120
#define GPU_PROFILE_NAME		24

typedef struct gpuProfile_s {
	int		serial;								// windows closed so far, 0: none yet
	int		frames;								// in the last window
	int		width, height, samples;				// of the frame
	char	device[64];
	int		numPasses;
	char	passNames[GPU_PROFILE_PASSES][GPU_PROFILE_NAME];
	float	passMs[GPU_PROFILE_PASSES];			// average a frame
	float	passMaxMs[GPU_PROFILE_PASSES];		// the most of one frame
	float	frameMs, frameMaxMs;				// the whole frame on the device
	float	history[GPU_PROFILE_HISTORY];		// the whole frame on the device of the last frames, oldest first
	int		draws, triangles, cpuTriangles, transforms, bones, skeletons;	// of the last frame
} gpuProfile_t;

typedef struct saberBlade3D_s {
	vec3_t		start, end;			// the blade now, from the hilt to the tip
	vec3_t		oldStart, oldEnd;	// where it was a moment ago, the place it swept glows between (the same: it rests)
	float		core, glow;			// the radius of the white core, of the coloured glow round it
	float		sweep;				// how bright the swept place is, 0 to 1
	vec3_t		rgb;				// 0 to 1
	int			renderfx;			// RF_THIRD_PERSON: only in mirrors and portals, RF_FIRST_PERSON: not in them
} saberBlade3D_t;

typedef struct refEntity_s {
	// this stucture must remain identical as the miniRefEntity_t
	//
	//
	refEntityType_t		reType;
	int					renderfx;

	qhandle_t			hModel;				// opaque type outside refresh

	// most recent data
	matrix3_t			axis;			// rotation vectors
	qboolean			nonNormalizedAxes;	// axis are not normalized, i.e. they have scale
	vec3_t				origin;				// also used as MODEL_BEAM's "from"

	// previous data for frame interpolation
	vec3_t				oldorigin;			// also used as MODEL_BEAM's "to"

	// texturing
	qhandle_t			customShader;		// use one image for the entire thing

	// misc
	byte				shaderRGBA[4];		// colors used by rgbgen entity shaders
	vec2_t				shaderTexCoord;		// texture coordinates used by tcMod entity modifiers

	// extra sprite information
	float				radius;
	float				rotation;

	// misc
	float		shaderTime;			// subtracted from refdef time to control effect start times
	int			frame;				// also used as MODEL_BEAM's diameter
	//
	//
	// end miniRefEntity_t

	//
	//
	// specific full refEntity_t data
	//
	//

	// most recent data
	vec3_t		lightingOrigin;		// so multi-part models can be lit identically (RF_LIGHTING_ORIGIN)
	float		shadowPlane;		// projection shadows go here, stencils go slightly lower

	// previous data for frame interpolation
	int			oldframe;
	float		backlerp;			// 0.0 = current, 1.0 = old

	// texturing
	int			skinNum;			// inline skin index
	qhandle_t	customSkin;			// NULL for default skin

	// texturing
	union
	{
//		int			skinNum;		// inline skin index
//		ivec3_t		terxelCoords;	// coords of patch for RT_TERXELS
		struct
		{
			int		miniStart;
			int		miniCount;
		} uMini;
	} uRefEnt;

	// extra sprite information
	union {
		struct
		{
			float rotation;
			float radius;
			byte  vertRGBA[4][4];
		} sprite;
		struct
		{
			float width;
			float width2;
			float stscale;
		} line;
		struct	// that whole put-the-opening-brace-on-the-same-line-as-the-beginning-of-the-definition coding style is fecal
		{
			float	width;
			vec3_t	control1;
			vec3_t	control2;
		} bezier;
		struct
		{
			float width;
			float width2;
			float stscale;
			float height;
			float bias;
			qboolean wrap;
		} cylinder;
		struct
		{
			float width;
			float deviation;
			float stscale;
			qboolean wrap;
			qboolean taper;
		} electricity;
	} data;

	float		endTime;
	float		saberLength;

/*
Ghoul2 Insert Start
*/
	vec3_t		angles;				// rotation angles - used for Ghoul2

	vec3_t		modelScale;			// axis scale for models
//	CGhoul2Info_v	*ghoul2;  		// has to be at the end of the ref-ent in order for it to be created properly
	void		*ghoul2;  		// has to be at the end of the ref-ent in order for it to be created properly
/*
Ghoul2 Insert End
*/
} refEntity_t;

/*
Ghoul2 Insert Start
*/
#define MDXABONEDEF
#include "rd-common/mdx_format.h"
#include "qcommon/qfiles.h"

// skins allow models to be retextured without modifying the model file
//this is a mock copy, renderers may have their own implementation.
// try not to break the ghoul2 code which is very implicit :/
typedef struct _skinSurface_s {
	char		name[MAX_QPATH];
	void	*shader;
} _skinSurface_t;

typedef struct skin_s {
	char		name[MAX_QPATH];		// game path, including extension
	int			numSurfaces;
	_skinSurface_t	*surfaces[128];
} skin_t;

/*
Ghoul2 Insert End
*/

#define	MAX_RENDER_STRINGS			8
#define	MAX_RENDER_STRING_LENGTH	32

typedef struct refdef_s {
	int			x, y, width, height;
	float		fov_x, fov_y;
	vec3_t		vieworg;
	vec3_t		viewangles;
	matrix3_t	viewaxis;		// transformation matrix
	int			viewContents;		// world contents at vieworg

	// time in milliseconds for shader effects and other time dependent rendering issues
	int			time;

	int			rdflags;			// RDF_NOWORLDMODEL, etc

	// 1 bits will prevent the associated area from rendering at all
	byte		areamask[MAX_MAP_AREA_BYTES];

	// text messages for deform text shaders
	char		text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];
} refdef_t;


enum {
	STEREO_CENTER,
	STEREO_LEFT,
	STEREO_RIGHT
};
typedef int stereoFrame_t;


/*
** glconfig_t
**
** Contains variables specific to the OpenGL configuration
** being run right now.  These are constant once the OpenGL
** subsystem is initialized.
*/
typedef enum { // r_ext_preferred_tc_method
	TC_NONE,
	TC_S3TC,
	TC_S3TC_DXT,
	TC_S3TC_ARB
} textureCompression_t;

typedef struct glconfig_s {
	const char				*renderer_string;
	const char				*vendor_string;
	const char				*version_string;
	const char				*extensions_string;

	int						maxTextureSize;			// queried from GL
	int						maxActiveTextures;		// multitexture ability
	float					maxTextureFilterAnisotropy;

	int						colorBits, depthBits, stencilBits;

	qboolean				deviceSupportsGamma;
	textureCompression_t	textureCompression;
	qboolean				textureEnvAddAvailable;
	qboolean				clampToEdgeAvailable;

	int						vidWidth, vidHeight;

	int						displayFrequency;

	// synonymous with "does rendering consume the entire screen?", therefore
	// a Voodoo or Voodoo2 will have this set to TRUE, as will a Win32 ICD that
	// used CDS.
	qboolean				isFullscreen;
	qboolean				stereoEnabled;
} glconfig_t;
