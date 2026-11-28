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

// cg_burnmark.h -- what a bolt of force lightning burns into the level (cg_burnmark.c, issue #19)

#pragma once

#include "cgame/weapons/cg_impact.h"

void CG_BurnMarksInit( void );
void CG_BurnMarksRegister( void );
// a bolt struck the level at origin (normal out of the surface), which is made of kind; size: the radius of the
// figure in units
void CG_BurnMark( const vec3_t origin, const vec3_t normal, impactKind_t kind, float size );
// the marks into the scene with the colours of their age, once a frame of the main view
void CG_BurnMarksAdd( void );
