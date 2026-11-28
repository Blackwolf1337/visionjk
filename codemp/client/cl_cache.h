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

#pragma once

// cl_cache.cpp: the folder cache of the profile. Names are "<kind>/<file>", the kinds: pipelines, textures,
// materials, sound, steamaudio.
void		Cache_Init( void );
long		Cache_Read( const char *name, void **buffer );		// free with FS_FreeFile()
qboolean	Cache_Write( const char *name, const void *data, int size );
qboolean	Cache_Exists( const char *name );				// without reading it
int			Cache_List( char *buf, int size );
qboolean	Cache_Clear( const char *id );
