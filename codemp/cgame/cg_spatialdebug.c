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

// cg_spatialdebug.c -- v_spatialDebug (2026-10-21): what the sound of the level with Steam Audio does, drawn into
// the level and over it. The engine says what (ext.S_SpatialDebug, client/snd_spatial.cpp): over every sound
// how loud it is and what the walls leave of it, the ways round the corners Steam Audio found, rays from the
// listener off the walls in the colour of what they are made of, and the numbers of the passes. 1 all of it, or
// the sum of the SPATIAL_DEBUG_* of qcommon/spatial_debug.h.

#include "cg_local.h"

#define SD_TEXT_W		6
#define SD_TEXT_H		8
#define SD_LINE			10
#define SD_LEAST_GAIN	0.005f		// a sound quieter than this gets no label
#define SD_UNIT			0.0254f		// metres a unit is, as for Steam Audio
#define SD_RAY_SKIP		48.0f		// units of a ray from the head that are not drawn

static spatialDebug_t	sd;
static qboolean			sdHave;

static const char *sdMaterialNames[SPATIAL_DEBUG_MATERIALS] = {
	"generic", "brick", "concrete", "ceramic", "gravel", "carpet", "glass", "plaster", "wood", "metal", "rock", "sky"
};

static const vec3_t sdMaterialColors[SPATIAL_DEBUG_MATERIALS] = {
	{ 0.6f, 0.6f, 0.6f },		// generic
	{ 0.8f, 0.3f, 0.2f },		// brick
	{ 0.8f, 0.8f, 0.7f },		// concrete
	{ 0.6f, 0.9f, 0.9f },		// ceramic
	{ 0.8f, 0.7f, 0.4f },		// gravel
	{ 0.7f, 0.3f, 0.8f },		// carpet
	{ 0.2f, 0.9f, 1.0f },		// glass
	{ 1.0f, 0.9f, 0.7f },		// plaster
	{ 0.9f, 0.5f, 0.1f },		// wood
	{ 0.4f, 0.6f, 1.0f },		// metal
	{ 0.5f, 0.7f, 0.4f },		// rock
	{ 0.3f, 0.5f, 0.9f },		// sky
};

static int SD_Cvar( const char *name ) {
	char value[16];

	trap->Cvar_VariableStringBuffer( name, value, sizeof( value ) );
	return atoi( value );
}

static int SD_Mask( void ) {
	const int v = SD_Cvar( "v_spatialDebug" );

	return ( v == 1 ) ? -1 : v;
}

static void SD_Line( const vec3_t from, const vec3_t to, const vec3_t color, float alpha, float width, qboolean throughWalls ) {
	refEntity_t re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_LINE;
	re.radius = width;
	re.customShader = trap->R_RegisterShader( "gfx/effects/whiteGlow" );
	VectorCopy( from, re.origin );
	VectorCopy( to, re.oldorigin );
	AxisClear( re.axis );
	// the shader adds its light: darker is fainter
	re.shaderRGBA[0] = (byte)( 255 * color[0] * alpha );
	re.shaderRGBA[1] = (byte)( 255 * color[1] * alpha );
	re.shaderRGBA[2] = (byte)( 255 * color[2] * alpha );
	re.shaderRGBA[3] = 255;
	re.shaderTexCoord[0] = re.shaderTexCoord[1] = 1.0f;
	if ( throughWalls ) {
		re.renderfx |= RF_NODEPTH;
	}
	trap->R_AddRefEntityToScene( &re );
}

// a little cross of three lines
static void SD_Mark( const vec3_t at, float size, const vec3_t color, qboolean throughWalls ) {
	int k;

	for ( k = 0; k < 3; k++ ) {
		vec3_t a, b;

		VectorCopy( at, a );
		VectorCopy( at, b );
		a[k] -= size;
		b[k] += size;
		SD_Line( a, b, color, 1.0f, 1.0f, throughWalls );
	}
}

/*
=================
CG_SpatialDebugAdd

Into the scene, while the refdef is made: asks the engine, draws the lines
=================
*/
void CG_SpatialDebugAdd( void ) {
	static const vec3_t	clear = { 0.2f, 1.0f, 0.3f }, blocked = { 1.0f, 0.15f, 0.1f }, gone = { 0.4f, 0.4f, 0.4f };
	int					mask, i;

	sdHave = qfalse;
	if ( !SD_Cvar( "v_spatialDebug" ) || !SD_Cvar( "v_cgameSpatialDebug" ) ) {
		return;
	}
	trap->ext.S_SpatialDebug( &sd, cg.refdef.vieworg, cg.refdef.viewaxis[0] );
	if ( !sd.active ) {
		return;
	}
	sdHave = qtrue;
	mask = SD_Mask();

	if ( mask & SPATIAL_DEBUG_PATHS ) {
		for ( i = 0; i < sd.numSegments; i++ ) {
			SD_Line( sd.segments[i].from, sd.segments[i].to, sd.segments[i].kind ? blocked : clear, 1.0f, 2.0f, qtrue );
		}
	}
	if ( mask & SPATIAL_DEBUG_REFLECTIONS ) {
		for ( i = 0; i < sd.numRays; i++ ) {
			const spatialDebugSegment_t *seg = &sd.rays[i];
			const float *color = ( seg->kind >= 0 && seg->kind < SPATIAL_DEBUG_MATERIALS ) ? sdMaterialColors[seg->kind] : gone;
			vec3_t from;

			// a ray begins a little away from the head, where all of them would be one patch in front of the camera
			VectorCopy( seg->from, from );
			if ( !seg->bounce ) {
				vec3_t dir;
				float length;

				VectorSubtract( seg->to, seg->from, dir );
				length = VectorNormalize( dir );
				VectorMA( seg->from, Q_min( SD_RAY_SKIP, length * 0.5f ), dir, from );
			}
			// each bounce fainter, as the sound gets weaker off every wall
			SD_Line( from, seg->to, color, 0.8f / ( 1.0f + seg->bounce ), 0.6f, qfalse );
		}
	}
	if ( ( mask & SPATIAL_DEBUG_PANEL ) && sd.aimHit && sd.aimMaterial >= 0 && sd.aimMaterial < SPATIAL_DEBUG_MATERIALS ) {
		SD_Mark( sd.aimPoint, 6.0f, sdMaterialColors[sd.aimMaterial], qfalse );
	}
	if ( mask & SPATIAL_DEBUG_LABELS ) {
		for ( i = 0; i < sd.numFallbacks; i++ ) {
			SD_Mark( sd.fallbacks[i].origin, 8.0f, blocked, qtrue );
		}
	}
}

static void SD_Text( float x, float y, const char *text, const float *color ) {
	CG_DrawStringExt( (int)x, (int)y, text, color, qfalse, qtrue, SD_TEXT_W, SD_TEXT_H, 0 );
}

static float SD_Width( const char *text ) {
	return (float)( CG_DrawStrlen( text ) * SD_TEXT_W );
}

// a label of lines, centred over a place in the level, if it is in front of the camera
static void SD_Label( const vec3_t at, const char *lines[], int numLines, const float *color ) {
	float x, y;
	int i;

	if ( !CG_WorldCoordToScreenCoordFloat( (float *)at, &x, &y ) ) {
		return;
	}
	y -= numLines * SD_LINE;
	for ( i = 0; i < numLines; i++, y += SD_LINE ) {
		SD_Text( x - SD_Width( lines[i] ) * 0.5f, y, lines[i], color );
	}
}

static void SD_Labels( void ) {
	static const vec4_t	white = { 1.0f, 1.0f, 1.0f, 1.0f }, own = { 1.0f, 0.9f, 0.3f, 1.0f }, red = { 1.0f, 0.3f, 0.2f, 1.0f };
	char				line[3][96];
	const char			*lines[3] = { line[0], line[1], line[2] };
	int					i;

	for ( i = 0; i < sd.numSounds; i++ ) {
		const spatialDebugSound_t *s = &sd.sounds[i];

		if ( s->flags & SPATIAL_DEBUG_SEND ) {
			continue;		// the own player, at the listener
		}
		if ( s->gain < SD_LEAST_GAIN && !( s->flags & SPATIAL_DEBUG_STREAM ) ) {
			continue;
		}
		Q_strncpyz( line[0], s->name, sizeof( line[0] ) );
		Com_sprintf( line[1], sizeof( line[1] ), "%.1f m  vol %d%%  walls let %d%%  through %d/%d/%d%%", s->distance * SD_UNIT,
			(int)( s->gain * 100.0f + 0.5f ), (int)( s->occlusion * 100.0f + 0.5f ), (int)( s->transmission[0] * 100.0f + 0.5f ),
			(int)( s->transmission[1] * 100.0f + 0.5f ), (int)( s->transmission[2] * 100.0f + 0.5f ) );
		Com_sprintf( line[2], sizeof( line[2] ), "%s%s%s", ( s->flags & SPATIAL_DEBUG_OWN ) ? "own echo" : "room echo",
			( ( s->flags & SPATIAL_DEBUG_PATHING ) && s->occlusion < 0.99f ) ? ", around corners" : "",
			( s->flags & SPATIAL_DEBUG_LOOP ) ? ", loop" : "" );
		SD_Label( s->origin, lines, 3, ( s->flags & SPATIAL_DEBUG_OWN ) ? own : white );
	}
	for ( i = 0; i < sd.numFallbacks; i++ ) {
		Com_sprintf( line[0], sizeof( line[0] ), "NO VOICE, stock mixer: %s", sd.fallbacks[i].name );
		SD_Label( sd.fallbacks[i].origin, lines, 1, red );
	}
}

// a bar for a level in dB, -60 to 0
static void SD_Meter( float x, float y, float db, const float *color ) {
	static const vec4_t back = { 0.0f, 0.0f, 0.0f, 0.5f };
	const float w = 120.0f, fill = Com_Clamp( 0.0f, 1.0f, ( db + 60.0f ) / 60.0f );

	CG_FillRect( x, y + 1.0f, w, SD_TEXT_H - 2.0f, back );
	CG_FillRect( x, y + 1.0f, w * fill, SD_TEXT_H - 2.0f, color );
}

static void SD_Panel( void ) {
	static const vec4_t	white = { 1.0f, 1.0f, 1.0f, 1.0f }, yellow = { 1.0f, 0.9f, 0.3f, 1.0f }, red = { 1.0f, 0.3f, 0.2f, 1.0f };
	static const vec4_t	green = { 0.3f, 1.0f, 0.4f, 0.9f }, blue = { 0.4f, 0.6f, 1.0f, 0.9f };
	const float			x = 8.0f;
	float				y = 120.0f;
	char				text[160];
	int					i;

	SD_Text( x, y, va( "Steam Audio: rays by %s, %i threads", sd.embree ? "Embree" : "Steam Audio", sd.threads ), white );
	y += SD_LINE;
	SD_Text( x, y, va( "pass: %i rays, %i bounces, %.1f ms, %i passes", sd.passRays, sd.passBounces, sd.passMsec, sd.passes ), white );
	y += SD_LINE;
	// Steam Audio adds the passes up while the listener stands: a few of them are noisy, many settle
	if ( sd.settledPasses < 4 ) {
		SD_Text( x, y, "echo: starting over (the listener moved)", yellow );
	} else {
		SD_Text( x, y, va( "echo: settled over %i passes", sd.settledPasses ), white );
	}
	y += SD_LINE;
	SD_Text( x, y, va( "voices %i/%i, own echoes %i/%i", sd.voices, sd.maxVoices, sd.reflects, sd.reflectSlots ), white );
	y += SD_LINE;
	SD_Text( x, y, va( "doors and other brush models: %i in the scene of %i, %i moved", sd.brushesIn, sd.brushes, sd.brushesMoved ),
		sd.brushesMoved ? yellow : white );
	y += SD_LINE;
	if ( sd.numFallbacks ) {
		SD_Text( x, y, va( "%i sounds without a voice: the stock mixer plays them, through walls", sd.numFallbacks ), red );
		y += SD_LINE;
	}
	SD_Text( x, y, va( "direct %4.0f dB", sd.direct ), white );
	SD_Meter( x + 100.0f, y, sd.direct, green );
	y += SD_LINE;
	SD_Text( x, y, va( "reverb %4.0f dB", sd.reverb ), white );
	SD_Meter( x + 100.0f, y, sd.reverb, blue );
	SD_Text( x + 230.0f, y, va( "(v_spatialReverb %.2f)", sd.reverbVolume ), white );
	y += SD_LINE;

	if ( sd.aimHit && sd.aimMaterial >= 0 && sd.aimMaterial < SPATIAL_DEBUG_MATERIALS ) {
		Com_sprintf( text, sizeof( text ), "%s: %s, %s, %.1f m", sd.aimBrush ? "brush model (door ...)" : "wall",
			sdMaterialNames[sd.aimMaterial], sd.aimByTable ? "by steamaudio/materials.txt" : "by the level", sd.aimDistance * SD_UNIT );
		SD_Text( x, y, text, white );
		y += SD_LINE;
		SD_Text( x, y, va( "  %s", sd.aimShader ), white );
		y += SD_LINE;
	} else {
		SD_Text( x, y, "wall: none under the crosshair", white );
		y += SD_LINE;
	}

	// what the colours of the rays are
	y += 4.0f;
	for ( i = 0; i < SPATIAL_DEBUG_MATERIALS; i++ ) {
		vec4_t color;

		VectorCopy( sdMaterialColors[i], color );
		color[3] = 1.0f;
		SD_Text( x + ( i % 4 ) * 60.0f, y + ( i / 4 ) * SD_LINE, sdMaterialNames[i], color );
	}
}

/*
=================
CG_SpatialDebugDraw

Over the scene, in 2D: the labels and the panel
=================
*/
void CG_SpatialDebugDraw( void ) {
	int mask;

	if ( !sdHave ) {
		return;
	}
	mask = SD_Mask();
	if ( mask & SPATIAL_DEBUG_LABELS ) {
		SD_Labels();
	}
	if ( mask & SPATIAL_DEBUG_PANEL ) {
		SD_Panel();
	}
}
