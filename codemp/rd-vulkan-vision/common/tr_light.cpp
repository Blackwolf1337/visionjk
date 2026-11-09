/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
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

// tr_light.c

#include "tr_local.h"

#define	DLIGHT_AT_RADIUS		16
// at the edge of a dlight's influence, this amount of light will be added

#define	DLIGHT_MINIMUM_RADIUS	16
// never calculate a range less than this to prevent huge light numbers


/*
===============
R_TransformDlights

Transforms the origins of an array of dlights.
Used by both the front end (for DlightBmodel) and
the back end (before doing the lighting calculation)
===============
*/
void R_TransformDlights( int count, dlight_t *dl, orientationr_t *ori) {
	int		i;
	vec3_t	temp;

	for ( i = 0 ; i < count ; i++, dl++ ) {
		VectorSubtract( dl->origin, ori->origin, temp );
		dl->transformed[0] = DotProduct( temp, ori->axis[0] );
		dl->transformed[1] = DotProduct( temp, ori->axis[1] );
		dl->transformed[2] = DotProduct( temp, ori->axis[2] );
		if ( dl->lineRange > 0.0f ) {
			VectorSubtract( dl->lineStart, ori->origin, temp );
			dl->lineStartTransformed[0] = DotProduct( temp, ori->axis[0] );
			dl->lineStartTransformed[1] = DotProduct( temp, ori->axis[1] );
			dl->lineStartTransformed[2] = DotProduct( temp, ori->axis[2] );
			VectorSubtract( dl->lineEnd, ori->origin, temp );
			dl->lineEndTransformed[0] = DotProduct( temp, ori->axis[0] );
			dl->lineEndTransformed[1] = DotProduct( temp, ori->axis[1] );
			dl->lineEndTransformed[2] = DotProduct( temp, ori->axis[2] );
		}
	}
}

/*
=============
R_DlightBmodel

Determine which dynamic lights may effect this bmodel
=============
*/
void R_DlightBmodel( bmodel_t *bmodel, bool NoLight )
{ //rwwRMG - modified args
	int			i, j;
	dlight_t	*dl;
	int			mask;
	msurface_t	*surf;

	// transform all the lights
	R_TransformDlights( tr.refdef.num_dlights, tr.refdef.dlights, &tr.ori );

	mask = 0;
	if (!NoLight)
	{
		for ( i=0 ; i<tr.refdef.num_dlights ; i++ ) {
			dl = &tr.refdef.dlights[i];

			// see if the point is close enough to the bounds to matter
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( dl->transformed[j] - bmodel->bounds[1][j] > dl->radius ) {
					break;
				}
				if ( bmodel->bounds[0][j] - dl->transformed[j] > dl->radius ) {
					break;
				}
			}
			if ( j < 3 ) {
				continue;
			}

			// we need to check this light
			mask |= 1 << i;
		}
	}

	tr.currentEntity->needDlights = (qboolean)(mask != 0);
	tr.currentEntity->dlightBits = mask;

	// set the dlight bits in all the surfaces
	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
		surf = bmodel->firstSurface + i;
		if ( *surf->data == SF_FACE ) {
			((srfSurfaceFace_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_GRID ) {
			((srfGridMesh_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_TRIANGLES ) {
			((srfTriangles_t *)surf->data)->dlightBits = mask;
		}
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

extern	cvar_t	*r_ambientScale;
extern	cvar_t	*r_directedScale;
extern	cvar_t	*r_debugLight;

//rwwRMG - VectorScaleVector is now a #define

/*
=================
R_SetupEntityLightingGrid

=================
*/
static void R_SetupEntityLightingGrid( trRefEntity_t *ent ) {
	vec3_t			lightOrigin;
	int				pos[3];
	int				i, j;
	float			frac[3];
	int				gridStep[3];
	vec3_t			direction;
	float			totalFactor;
	unsigned short	*startGridPos;

	if (r_fullbright->integer)
	{
		ent->ambientLight[0] = ent->ambientLight[1] = ent->ambientLight[2] = 255.0;
		ent->directedLight[0] = ent->directedLight[1] = ent->directedLight[2] = 255.0;
		VectorCopy( tr.sunDirection, ent->lightDir );
		return;
	}

	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// seperate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	VectorSubtract( lightOrigin, tr.world->lightGridOrigin, lightOrigin );
	for ( i = 0 ; i < 3 ; i++ ) {
		float	v;

		v = lightOrigin[i]*tr.world->lightGridInverseSize[i];
		pos[i] = floor( v );
		frac[i] = v - pos[i];
		if ( pos[i] < 0 ) {
			pos[i] = 0;
		} else if ( pos[i] >= tr.world->lightGridBounds[i] - 1 ) {
			pos[i] = tr.world->lightGridBounds[i] - 1;
		}
	}

	VectorClear( ent->ambientLight );
	VectorClear( ent->directedLight );
	VectorClear( direction );

	// trilerp the light value
	gridStep[0] = 1;
	gridStep[1] = tr.world->lightGridBounds[0];
	gridStep[2] = tr.world->lightGridBounds[0] * tr.world->lightGridBounds[1];
	startGridPos = tr.world->lightGridArray + (pos[0] * gridStep[0] + pos[1] * gridStep[1] + pos[2] * gridStep[2]);

	totalFactor = 0;
	for ( i = 0 ; i < 8 ; i++ ) {
		float			factor;
		mgrid_t			*data;
		unsigned short	*gridPos;
		int				lat, lng;
		vec3_t			normal;

		factor = 1.0;
		gridPos = startGridPos;
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( i & (1<<j) ) {
				factor *= frac[j];
				gridPos += gridStep[j];
			} else {
				factor *= (1.0 - frac[j]);
			}
		}

		if (gridPos >= tr.world->lightGridArray + tr.world->numGridArrayElements)
		{//we've gone off the array somehow
			continue;
		}
		data = tr.world->lightGridData + *gridPos;

		if ( data->styles[0] == LS_LSNONE )
		{
			continue;	// ignore samples in walls
		}

		totalFactor += factor;

		for(j=0;j<MAXLIGHTMAPS;j++)
		{
			if (data->styles[j] != LS_LSNONE)
			{
				const byte	style= data->styles[j];

				ent->ambientLight[0] += factor * data->ambientLight[j][0] * styleColors[style][0] / 255.0f;
				ent->ambientLight[1] += factor * data->ambientLight[j][1] * styleColors[style][1] / 255.0f;
				ent->ambientLight[2] += factor * data->ambientLight[j][2] * styleColors[style][2] / 255.0f;

				ent->directedLight[0] += factor * data->directLight[j][0] * styleColors[style][0] / 255.0f;
				ent->directedLight[1] += factor * data->directLight[j][1] * styleColors[style][1] / 255.0f;
				ent->directedLight[2] += factor * data->directLight[j][2] * styleColors[style][2] / 255.0f;
			}
			else
			{
				break;
			}
		}

		lat = data->latLong[1];
		lng = data->latLong[0];
		lat *= (FUNCTABLE_SIZE/256);
		lng *= (FUNCTABLE_SIZE/256);

		// decode X as cos( lat ) * sin( long )
		// decode Y as sin( lat ) * sin( long )
		// decode Z as cos( long )

		normal[0] = tr.sinTable[(lat + (FUNCTABLE_SIZE / 4)) & FUNCTABLE_MASK] * tr.sinTable[lng];
		normal[1] = tr.sinTable[lat] * tr.sinTable[lng];
		normal[2] = tr.sinTable[(lng + (FUNCTABLE_SIZE / 4)) & FUNCTABLE_MASK];

		VectorMA( direction, factor, normal, direction );
	}

	if ( totalFactor > 0 && totalFactor < 0.99 )
	{
		totalFactor = 1.0 / totalFactor;
		VectorScale( ent->ambientLight, totalFactor, ent->ambientLight );
		VectorScale( ent->directedLight, totalFactor, ent->directedLight );
	}

	VectorScale( ent->ambientLight, r_ambientScale->value, ent->ambientLight );
	VectorScale( ent->directedLight, r_directedScale->value, ent->directedLight );

	VectorNormalize2( direction, ent->lightDir );
}


/*
===============
LogLight
===============
*/
static void LogLight( trRefEntity_t *ent ) {
	int	max1, max2;

	if ( !(ent->e.renderfx & RF_FIRST_PERSON ) ) {
		return;
	}

	max1 = ent->ambientLight[0];
	if ( ent->ambientLight[1] > max1 ) {
		max1 = ent->ambientLight[1];
	} else if ( ent->ambientLight[2] > max1 ) {
		max1 = ent->ambientLight[2];
	}

	max2 = ent->directedLight[0];
	if ( ent->directedLight[1] > max2 ) {
		max2 = ent->directedLight[1];
	} else if ( ent->directedLight[2] > max2 ) {
		max2 = ent->directedLight[2];
	}

	ri.Printf( PRINT_ALL, "amb:%i  dir:%i\n", max1, max2 );
}

/*
=================
R_CreatePbrGrid

VisionJK: the light grid of the level as a picture, for the stages with a material (generic.frag PbrGrid): they
split the light of their lightmap into the part that comes from one direction and the rest, so that bumps and shine
follow the lamps. A pixel per cell, the layers of the grid side by side, pbrGridTiles across: in RGB the direction
the light comes from times the share of that light (directed / (directed + ambient)), as x * 0.5 + 0.5, in alpha
whether the cell lies in the open (1) or in a wall (0), and RGB times alpha, so that what the texture filtering mixes
of open cells and walls comes out right again after dividing by alpha. The picture is a power of two each way, the
rest of it stays empty.
=================
*/
void R_CreatePbrGrid( const char *mapName ) {
	const world_t	*w = tr.world;
	int				tiles, rows, width, height;
	byte			*pic;
	char			name[MAX_QPATH];

	tr.pbrGrid = NULL;
	tr.pbrGridTiles = 0;
	if ( !w || !w->lightGridData || !w->lightGridArray ) {
		return;
	}
	tiles = (int)ceil( sqrt( (double)w->lightGridBounds[2] ) );
	rows = ( w->lightGridBounds[2] + tiles - 1 ) / tiles;
	// (the size of a picture is a power of two: R_CreateImage)
	for ( width = 1; width < w->lightGridBounds[0] * tiles; width <<= 1 ) {
	}
	for ( height = 1; height < w->lightGridBounds[1] * rows; height <<= 1 ) {
	}
	if ( width > glConfig.maxTextureSize || height > glConfig.maxTextureSize ) {
		ri.Printf( PRINT_DEVELOPER, "the light grid of %ix%ix%i is too large for one picture, materials light without it\n",
			w->lightGridBounds[0], w->lightGridBounds[1], w->lightGridBounds[2] );
		return;
	}

	pic = (byte *)Z_Malloc( width * height * 4, TAG_TEMP_WORKSPACE, qtrue );
	for ( int z = 0; z < w->lightGridBounds[2]; z++ ) {
		for ( int y = 0; y < w->lightGridBounds[1]; y++ ) {
			for ( int x = 0; x < w->lightGridBounds[0]; x++ ) {
				const int		cell = x + y * w->lightGridBounds[0] + z * w->lightGridBounds[0] * w->lightGridBounds[1];
				const mgrid_t	*data;
				byte			*out;
				vec3_t			ambient = { 0, 0, 0 }, directed = { 0, 0, 0 }, dir;
				float			a, d, share;
				int				lat, lng;

				if ( cell >= w->numGridArrayElements ) {
					continue;
				}
				data = w->lightGridData + w->lightGridArray[cell];
				if ( data->styles[0] == LS_LSNONE ) {
					continue;	// in a wall: all four bytes stay 0
				}
				for ( int j = 0; j < MAXLIGHTMAPS && data->styles[j] != LS_LSNONE; j++ ) {
					for ( int c = 0; c < 3; c++ ) {
						ambient[c] += data->ambientLight[j][c] * styleColors[data->styles[j]][c] / 255.0f;
						directed[c] += data->directLight[j][c] * styleColors[data->styles[j]][c] / 255.0f;
					}
				}
				// as R_SetupEntityLightingGrid decodes it
				lat = data->latLong[1] * ( FUNCTABLE_SIZE / 256 );
				lng = data->latLong[0] * ( FUNCTABLE_SIZE / 256 );
				dir[0] = tr.sinTable[( lat + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * tr.sinTable[lng];
				dir[1] = tr.sinTable[lat] * tr.sinTable[lng];
				dir[2] = tr.sinTable[( lng + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK];

				a = 0.2126f * ambient[0] + 0.7152f * ambient[1] + 0.0722f * ambient[2];
				d = 0.2126f * directed[0] + 0.7152f * directed[1] + 0.0722f * directed[2];
				share = ( a + d > 0.0f ) ? d / ( a + d ) : 0.0f;

				out = pic + ( ( ( z / tiles ) * w->lightGridBounds[1] + y ) * width + ( z % tiles ) * w->lightGridBounds[0] + x ) * 4;
				for ( int c = 0; c < 3; c++ ) {
					out[c] = (byte)Com_Clamp( 0.0f, 255.0f, ( dir[c] * share * 0.5f + 0.5f ) * 255.0f + 0.5f );
				}
				out[3] = 255;
			}
		}
	}

	R_IssuePendingRenderCommands();
	Com_sprintf( name, sizeof( name ), "*%s/pbrgrid", mapName );
	tr.pbrGrid = R_CreateImage( name, pic, width, height, GL_RGBA, qfalse, qfalse, qfalse, GL_CLAMP );
	tr.pbrGridTiles = tiles;
	Z_Free( pic );
}

// the cluster of the visibility of the level a point lies in, -1 in a wall
static int R_PointCluster( const vec3_t p ) {
	return ri.CM_LeafCluster( ri.CM_PointLeafnum( p ) );
}

// a reflection probe at p, unless it is in a wall or near one there is already
static qboolean R_AddProbe( const vec3_t p, float spacing ) {
	if ( tr.numProbes >= MAX_PROBES || R_PointCluster( p ) < 0 ) {
		return qfalse;
	}
	for ( int i = 0; i < tr.numProbes; i++ ) {
		if ( DistanceSquared( p, tr.probes[i] ) < spacing * spacing ) {
			return qfalse;
		}
	}
	VectorCopy( p, tr.probes[tr.numProbes++] );
	return qtrue;
}

/*
=================
R_CreateProbes

VisionJK: the reflection probes of a level and which one each cell of the light grid takes. A probe is a point the
level is drawn from in all six directions while it loads (R_ProbeViews), and what the stages with a material mirror
(generic.frag PbrStage). The points: where players are and fight (the spawns, weapons and items, a little over the
floor), then where the grid has open cells furthest from all of those, until MAX_PROBES or until none is far from
one. A cell takes the nearest probe in its view (the visibility of the level), else the nearest one; in pbrProbes,
a pixel per cell in the layout of pbrGrid, the number of the probe in red.
=================
*/
void R_CreateProbes( const char *mapName ) {
	const world_t	*w = tr.world;
	const char		*p;
	char			*token, classname[MAX_QPATH];
	vec3_t			origin;
	qboolean		hasOrigin;
	int				probeCluster[MAX_PROBES];
	byte			*pic;
	char			name[MAX_QPATH];

	tr.numProbes = 0;
	tr.probeNext = 0;
	tr.probesReady = qfalse;
	tr.pbrProbes = NULL;
	if ( !w || !tr.pbrGrid || !w->entityString ) {
		return;
	}

	// the places of the game: an entity a block of keys and values in braces
	p = w->entityString;
	while ( 1 ) {
		token = COM_ParseExt( &p, qtrue );
		if ( !token[0] ) {
			break;
		}
		if ( token[0] != '{' ) {
			continue;
		}
		classname[0] = 0;
		hasOrigin = qfalse;
		while ( 1 ) {
			char key[MAX_TOKEN_CHARS];

			token = COM_ParseExt( &p, qtrue );
			if ( !token[0] || token[0] == '}' ) {
				break;
			}
			Q_strncpyz( key, token, sizeof( key ) );
			token = COM_ParseExt( &p, qtrue );
			if ( !Q_stricmp( key, "classname" ) ) {
				Q_strncpyz( classname, token, sizeof( classname ) );
			} else if ( !Q_stricmp( key, "origin" ) ) {
				hasOrigin = (qboolean)( sscanf( token, "%f %f %f", &origin[0], &origin[1], &origin[2] ) == 3 );
			}
		}
		if ( hasOrigin && ( !Q_stricmpn( classname, "info_player", 11 ) || !Q_stricmpn( classname, "team_CTF", 8 )
			|| !Q_stricmpn( classname, "weapon_", 7 ) || !Q_stricmpn( classname, "item_", 5 ) || !Q_stricmpn( classname, "ammo_", 5 ) ) ) {
			origin[2] += 40.0f;		// about the height of the eyes over the floor
			R_AddProbe( origin, 384.0f );
		}
	}

	// then the open cells of the grid furthest from every probe
	while ( tr.numProbes < MAX_PROBES ) {
		vec3_t	best;
		float	bestDist = 0.0f;

		for ( int z = 0; z < w->lightGridBounds[2]; z += 2 ) {
			for ( int y = 0; y < w->lightGridBounds[1]; y += 3 ) {
				for ( int x = 0; x < w->lightGridBounds[0]; x += 3 ) {
					const int cell = x + y * w->lightGridBounds[0] + z * w->lightGridBounds[0] * w->lightGridBounds[1];
					vec3_t c;
					float d = 1e30f;

					if ( w->lightGridData[w->lightGridArray[cell]].styles[0] == LS_LSNONE ) {
						continue;
					}
					for ( int k = 0; k < 3; k++ ) {
						c[k] = w->lightGridOrigin[k] + ( k == 0 ? x : k == 1 ? y : z ) * w->lightGridSize[k];
					}
					for ( int i = 0; i < tr.numProbes; i++ ) {
						d = Q_min( d, DistanceSquared( c, tr.probes[i] ) );
					}
					if ( d > bestDist ) {
						bestDist = d;
						VectorCopy( c, best );
					}
				}
			}
		}
		if ( bestDist < 768.0f * 768.0f || !R_AddProbe( best, 768.0f ) ) {
			break;
		}
	}
	if ( !tr.numProbes ) {
		return;
	}

	// which probe a cell takes
	for ( int i = 0; i < tr.numProbes; i++ ) {
		probeCluster[i] = R_PointCluster( tr.probes[i] );
	}
	pic = (byte *)Z_Malloc( tr.pbrGrid->width * tr.pbrGrid->height * 4, TAG_TEMP_WORKSPACE, qtrue );
	for ( int z = 0; z < w->lightGridBounds[2]; z++ ) {
		for ( int y = 0; y < w->lightGridBounds[1]; y++ ) {
			for ( int x = 0; x < w->lightGridBounds[0]; x++ ) {
				vec3_t		c;
				const byte	*pvs;
				int			cluster, best = 0, seen = -1;
				float		bestDist = 1e30f, seenDist = 1e30f;

				for ( int k = 0; k < 3; k++ ) {
					c[k] = w->lightGridOrigin[k] + ( k == 0 ? x : k == 1 ? y : z ) * w->lightGridSize[k];
				}
				cluster = R_PointCluster( c );
				pvs = cluster >= 0 ? (const byte *)ri.CM_ClusterPVS( cluster ) : NULL;
				for ( int i = 0; i < tr.numProbes; i++ ) {
					const float d = DistanceSquared( c, tr.probes[i] );

					if ( d < bestDist ) {
						bestDist = d;
						best = i;
					}
					if ( d < seenDist && pvs && probeCluster[i] >= 0 && ( pvs[probeCluster[i] >> 3] & ( 1 << ( probeCluster[i] & 7 ) ) ) ) {
						seenDist = d;
						seen = i;
					}
				}
				byte *out = pic + ( ( ( z / tr.pbrGridTiles ) * w->lightGridBounds[1] + y ) * tr.pbrGrid->width
					+ ( z % tr.pbrGridTiles ) * w->lightGridBounds[0] + x ) * 4;
				out[0] = (byte)( seen >= 0 ? seen : best );
				out[3] = 255;
			}
		}
	}
	R_IssuePendingRenderCommands();
	Com_sprintf( name, sizeof( name ), "*%s/pbrprobes", mapName );
	tr.pbrProbes = R_CreateImage( name, pic, tr.pbrGrid->width, tr.pbrGrid->height, GL_RGBA, qfalse, qfalse, qfalse, GL_CLAMP );
	Z_Free( pic );
	ri.Printf( PRINT_DEVELOPER, "%i reflection probes\n", tr.numProbes );
}

/*
=================
R_SetupEntityLighting

Calculates all the lighting values that will be used
by the Calc_* functions
=================
*/
void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent ) {
	int				i;
	dlight_t		*dl;
	float			power;
	vec3_t			dir;
	float			d;
	vec3_t			lightDir;
	vec3_t			lightOrigin;

	// lighting calculations
	if ( ent->lightingCalculated ) {
		return;
	}
	ent->lightingCalculated = qtrue;

	//
	// trace a sample point down to find ambient light
	//
	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// seperate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	// if NOWORLDMODEL, only use dynamic lights (menu system, etc)
	if ( !(refdef->rdflags & RDF_NOWORLDMODEL )
		&& tr.world->lightGridData ) {
		R_SetupEntityLightingGrid( ent );
	} else {
		ent->ambientLight[0] = ent->ambientLight[1] =
			ent->ambientLight[2] = tr.identityLight * 150;
		ent->directedLight[0] = ent->directedLight[1] =
			ent->directedLight[2] = tr.identityLight * 150;
		VectorCopy( tr.sunDirection, ent->lightDir );
	}

	// bonus items and view weapons have a fixed minimum add
	if ( 1 /* ent->e.renderfx & RF_MINLIGHT */ ) {
		// give everything a minimum light add
		ent->ambientLight[0] += tr.identityLight * 32;
		ent->ambientLight[1] += tr.identityLight * 32;
		ent->ambientLight[2] += tr.identityLight * 32;
	}

	if (ent->e.renderfx & RF_MINLIGHT)
	{ //the minlight flag is now for items rotating on their holo thing
		if (ent->e.shaderRGBA[0] == 255 &&
			ent->e.shaderRGBA[1] == 255 &&
			ent->e.shaderRGBA[2] == 0)
		{
			ent->ambientLight[0] += tr.identityLight * 255;
			ent->ambientLight[1] += tr.identityLight * 255;
			ent->ambientLight[2] += tr.identityLight * 0;
		}
		else
		{
			ent->ambientLight[0] += tr.identityLight * 16;
			ent->ambientLight[1] += tr.identityLight * 96;
			ent->ambientLight[2] += tr.identityLight * 150;
		}
	}

	// VisionJK (v_panelLight): a player near the lamp panels his traced shadow comes from (vk_rt.cpp VK_RT_PickLamps)
	// is lit from them too: the direction of the light of the grid turns towards them by their share. The amount stays
	// the grid's, which has their light in it already, so his light and his shadow come from the same side.
	if ( v_panelLight && v_panelLight->integer && !( refdef->rdflags & RDF_NOWORLDMODEL ) ) {
		vec3_t	panels, mixed;
		float	share;

		if ( VK_RT_PlayerLamps( lightOrigin, panels, &share ) ) {
			VectorScale( ent->lightDir, 1.0f - share, mixed );
			VectorMA( mixed, share, panels, mixed );
			if ( VectorNormalize( mixed ) > 0.01f ) {
				VectorCopy( mixed, ent->lightDir );
			}
		}
	}

	//
	// modify the light by dynamic lights
	//
	d = VectorLength( ent->directedLight );
	VectorScale( ent->lightDir, d, lightDir );

	for ( i = 0 ; i < refdef->num_dlights ; i++ ) {
		dl = &refdef->dlights[i];
		VectorSubtract( dl->origin, lightOrigin, dir );
		d = VectorNormalize( dir );

		power = DLIGHT_AT_RADIUS * ( dl->radius * dl->radius );
		if ( d < DLIGHT_MINIMUM_RADIUS ) {
			d = DLIGHT_MINIMUM_RADIUS;
		}
		d = power / ( d * d );

		VectorMA( ent->directedLight, d, dl->color, ent->directedLight );
		VectorMA( lightDir, d, dir, lightDir );
	}

	// clamp ambient
	for ( i = 0 ; i < 3 ; i++ ) {
		if ( ent->ambientLight[i] > tr.identityLightByte ) {
			ent->ambientLight[i] = tr.identityLightByte;
		}
	}

	if ( r_debugLight->integer ) {
		LogLight( ent );
	}

	// save out the byte packet version
	((byte *)&ent->ambientLightInt)[0] = Q_ftol( ent->ambientLight[0] );
	((byte *)&ent->ambientLightInt)[1] = Q_ftol( ent->ambientLight[1] );
	((byte *)&ent->ambientLightInt)[2] = Q_ftol( ent->ambientLight[2] );
	((byte *)&ent->ambientLightInt)[3] = 0xff;

	// transform the direction to local space
	VectorNormalize( lightDir );
	ent->lightDir[0] = DotProduct( lightDir, ent->e.axis[0] );
	ent->lightDir[1] = DotProduct( lightDir, ent->e.axis[1] );
	ent->lightDir[2] = DotProduct( lightDir, ent->e.axis[2] );
}

/*
=================
R_LightForPoint
=================
*/
int R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir )
{
	trRefEntity_t ent;

	if ( tr.world->lightGridData == NULL )
		return qfalse;

	memset(&ent, 0, sizeof(ent));
	VectorCopy( point, ent.e.origin );
	R_SetupEntityLightingGrid( &ent );
	VectorCopy(ent.ambientLight, ambientLight);
	VectorCopy(ent.directedLight, directedLight);
	VectorCopy(ent.lightDir, lightDir);

	return qtrue;
}
