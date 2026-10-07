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

void		S_SpatialInit( void );			// the sound device is open: dma.speed is known
void		S_SpatialShutdown( void );
qboolean	S_SpatialActive( void );		// s_spatial is on and the library is there
