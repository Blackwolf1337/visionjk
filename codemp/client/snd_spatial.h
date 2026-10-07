/*
===========================================================================
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

// snd_spatial.cpp: sound that knows the level, with Steam Audio. See docs/japlus/audio.md.

#define SPATIAL_FRAME	1024	// samples Steam Audio works on at a time, the size of the paint buffer

// of snd_local.h, for the files that only start and stop this
typedef struct channel_s channel_t;
typedef struct sfx_s sfx_t;
typedef struct portable_samplepair_s portable_samplepair_t;

void		S_SpatialInit( void );			// the sound device is open: dma.speed is known
void		S_SpatialShutdown( void );
qboolean	S_SpatialActive( void );		// s_spatial is on, the library is there and a level is loaded
void		S_SpatialLoadMap( const char *mapname );	// the collision model of a level was loaded
void		S_SpatialUpdate( void );		// once a frame: a level that was loaded is taken over when that is safe

// once a frame, from S_Update_(): the listener, every sound with a place in the level, then render ahead
void		S_SpatialBeginFrame( const vec3_t origin, matrix3_t axis );
// volume: how loud the game plays it, 0 to 1. fade: what is left of that at its distance in a straight line
qboolean	S_SpatialAddChannel( channel_t *ch, const vec3_t origin, float volume, float fade, qboolean voiceVolume );
void		S_SpatialAddLoop( int entnum, sfx_t *sfx, const vec3_t origin, float volume, float fade, float step );
void		S_SpatialEndFrame( int from, int target );
float		S_SpatialAhead( void );

// from S_PaintChannels(): adds what the voices have for the samples from to to
void		S_SpatialPaint( int from, int to, portable_samplepair_t *paint );
