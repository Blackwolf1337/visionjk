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

// cg_ribbon.h -- lines of light as ribbons that face the eye, for the force powers (cg_ribbon.c): the bolts of
// lightning, the streams of drain

#pragma once

#define RIBBON_POINTS	65

// a line through points, with a width (times the half width of each layer) and a brightness at each
typedef struct {
	int		numPoints;
	vec3_t	p[RIBBON_POINTS];
	float	width[RIBBON_POINTS];
	float	bright[RIBBON_POINTS];
	int		kind;			// the caller's
} ribbon_t;

// one layer it is drawn in: shader (added), half width in units, never thinner than minPixels, colour times level
typedef struct {
	qhandle_t	shader;
	float		halfWidth;
	float		minPixels;
	vec3_t		color;
	float		level;
} ribbonLayer_t;

void CG_RibbonDraw( const ribbon_t *r, const ribbonLayer_t *layers, int numLayers );
// what CG_RibbonDraw gathered into the scene; before the scene is rendered
void CG_RibbonFlush( void );
// a glow that faces the eye
void CG_RibbonSprite( qhandle_t shader, const vec3_t at, float radius, const vec3_t color, float level );
// two directions across fwd, the same for the same fwd
void CG_RibbonBasis( const vec3_t fwd, vec3_t right, vec3_t up );
