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
#define	RF_SHADOW_PLANE		0x00100	// use refEntity->shadowPlane
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
// moves with it (the shockwave of the boombox, sound_wave.md3). shaderTexCoord[1]: how many times as far, 0 is 1. rd-vanilla does not know it: a model that faces
// the view is right there too.
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
