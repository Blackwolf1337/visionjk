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

// cg_forceaura.c -- the lights of Heal, Protect, Absorb and Rage on every player, the grip at the throat (issue #19)

void CG_ForceAuraRegister( void );
// once a frame for a player or NPC that is drawn, after its model was placed
void CG_ForceAuraPlayer( centity_t *cent, clientInfo_t *ci );
