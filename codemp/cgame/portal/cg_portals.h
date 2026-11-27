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

// The portals of the portal gun as this client knows them (game/portal/bg_portal.h). Not from JA+.

#pragma once

#include "game/portal/bg_portal.h"

// the portals of the snapshot the client is in (cg.snap), up to max; returns how many
int CG_Portals( bgPortal_t *out, int max );
// a trace of the view (the third person camera) through the holes of the open portals near start, as the moves of a
// player have them (cg_predict.c): a head right at the face of a portal is in the opening, not in the wall round it.
// With an engine without the traces with holes it is CG_Trace.
void CG_PortalViewTrace( trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
	int skipNumber, int mask );
