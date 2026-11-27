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

// cg_portals.c -- the portals of the portal gun as this client knows them (cg_portals.h)

#include "cgame/cg_local.h"
#include "cg_portals.h"

int CG_Portals( bgPortal_t *out, int max ) {
	int count = 0;

	if ( !cg.snap ) {
		return 0;
	}
	for ( int i = 0; i < cg.snap->numEntities && count < max; i++ ) {
		const entityState_t *s = &cg.snap->entities[i];

		if ( BG_IsPortal( s ) ) {
			BG_PortalFromState( s, &out[count++] );
		}
	}
	return count;
}
