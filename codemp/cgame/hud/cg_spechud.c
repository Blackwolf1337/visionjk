/*
===========================================================================
Copyright (C) 2015 - 2023, TaystJK contributors
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

// cg_spechud.c -- tay_specHud (2026-10-19), TaystJK's cg_specHud: while one watches, as a spectator or following
// somebody, a map of the players around at the upper right, in the place and the picture of the radar of siege.
// Every player the snapshot has is an arrow turned the way he looks, red and blue by team and white without teams;
// higher ones larger, lower ones smaller, as the radar draws them; the one followed (or the own view) in the
// middle. Farther than 1200 units they stay at its edge, so their direction shows. The box of the enemy
// info is left out meanwhile, as TaystJK does. TaystJK's own map puts everybody in the middle (the line that
// works out the distance is commented out there); here they are where they are.

#include "cgame/cg_local.h"

#define SPEC_RANGE		1500.0f		// what the edge of the map is away (the radar of siege has 2500: near players met in its middle)

qboolean CG_SpecHudShown( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( tay_specHud.integer && cg.snap
		&& ( ( ps->pm_flags & PMF_FOLLOW ) || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) );
}

/*
=================
CG_SpecHudDraw

From CG_DrawUpperRight, with the 2D at the right edge (PLACE_RIGHT): y is where it starts, the return where the
next part goes
=================
*/
float CG_SpecHudDraw( float y ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const float			radius = 60.0f * Com_Clamp( 0.5f, 2.0f, tay_specHudScale.value );
	const float			left = SCREEN_WIDTH - 2.0f * radius;
	const float			cx = left + radius, cy = y + radius;
	const vec4_t		back = { 1.0f, 1.0f, 1.0f, 0.6f };
	vec3_t				look;
	float				lookAngle, size;
	int					i;

	if ( !CG_SpecHudShown() ) {
		return y;
	}
	trap->R_SetColor( back );
	CG_DrawPic( left, y, radius * 2.0f, radius * 2.0f, cgs.media.radarShader );

	AngleVectors( ps->viewangles, look, NULL, NULL );
	look[2] = 0.0f;
	VectorNormalize( look );
	lookAngle = atan2f( look[0], look[1] );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t		*cent = &cg_entities[i];
		const clientInfo_t	*ci = &cgs.clientinfo[i];
		vec3_t				to;
		vec4_t				color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float				distance, angle, scale = 1.0f;

		if ( i == ps->clientNum || !cent->currentValid || cent->currentState.eType != ET_PLAYER
			|| ( cent->currentState.eFlags & EF_DEAD ) || !ci->infoValid ) {
			continue;
		}
		VectorSubtract( ps->origin, cent->lerpOrigin, to );
		to[2] = 0.0f;
		distance = VectorNormalize( to );
		distance = Q_min( distance, SPEC_RANGE * 0.8f ) / SPEC_RANGE * radius;
		angle = lookAngle - atan2f( to[0], to[1] );

		if ( ci->team == TEAM_RED ) {
			VectorSet( color, 1.0f, 0.3f, 0.25f );
		} else if ( ci->team == TEAM_BLUE ) {
			VectorSet( color, 0.3f, 0.5f, 1.0f );
		}
		// up to half again as large 512 units above, down to half 512 units below
		scale += Com_Clamp( -0.5f, 0.5f, ( cent->lerpOrigin[2] - ps->origin[2] ) / 1024.0f );
		size = 12.0f * scale * radius / 128.0f;

		trap->R_SetColor( color );
		CG_DrawRotatePic2( cx + sinf( angle ) * distance, cy + cosf( angle ) * distance, size, size,
			( 360.0f - cent->lerpAngles[YAW] ) + ps->viewangles[YAW], cgs.media.mAutomapPlayerIcon );
	}

	// the one in the middle
	size = 16.0f * radius / 128.0f;
	trap->R_SetColor( colorWhite );
	CG_DrawRotatePic2( cx, cy, size, size, 0.0f, cgs.media.mAutomapPlayerIcon );
	trap->R_SetColor( NULL );

	return y + radius * 2.0f;
}
