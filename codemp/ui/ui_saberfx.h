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

/*
ui_saberfx.h -- the blade style "SFX" (v_saberStyle 1), from JA++ by way of the VisionJK of 2019: a thin white core
in a glow of the colour, stretched over where the blade is and where it was a moment ago, so a swing is one
smeared surface and not a blade with a trail behind it. Built into the cgame (the blades in the level) and into
the ui module (the saber in the menus), like ui_skin.c.
*/

#pragma once

typedef struct saberFX_s {
	vec3_t		bladeMuzzle, bladeTip;		// the blade now
	vec3_t		trailMuzzle, trailTip;		// the blade a moment ago, the same points for a blade that stands still
	vec3_t		viewOrigin;					// the eye, the glow is thinner seen along the blade
	float		lengthMax, radius;			// of the blade, as in sabers.cfg
	qhandle_t	glow;						// glow shader of the colour
	qboolean	isRGB;						// core and glow in rgb, not white and the colour of the glow
	byte		rgb[3];
	int			renderfx;
	float		coreScale, glowScale;		// v_saberCoreScale, v_saberGlowScale
} saberFX_t;

void		SaberFX_Reset( void );
qboolean	SaberFX_Colour( int color );
void		SaberFX_Draw( const saberFX_t *fx );
