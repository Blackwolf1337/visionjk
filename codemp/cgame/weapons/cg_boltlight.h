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

void CG_BoltLightsInit( void );
// this missile asks for its light; qfalse: it is no bolt or the switch is off, the caller keeps the stock light
qboolean CG_BoltLight( const centity_t *cent );
// a blade deflected a bolt here
void CG_BoltLightFlash( const vec3_t at );
// after the entities of a scene: the nearest bolts go in as lights, the flashes only into the main view
void CG_BoltLightsAdd( qboolean skyPortal );
