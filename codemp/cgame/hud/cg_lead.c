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

// cg_lead.c -- tay_leadIndicator (2026-10-19), jaPRO's cg_leadIndicator as TaystJK has it: with a weapon whose
// shot flies, a dotted line from every moving opponent in sight to where a shot fired now would meet him, and a
// mark there to aim at. The speeds are the ones of the game module (g_weapon.c). The alt fire of the repeater
// falls (TR_GRAVITY with DEFAULT_GRAVITY in BG_EvaluateTrajectory, 40 upwards to start with), so its mark is
// raised by that much.
// The lag: the opponent is drawn where he was a moment ago. A server of this tree starts a missile as far along
// as the shooter's lag (tay_unlagged 2), so there only the flight counts. Elsewhere the own ping is added to the
// flight, as jaPRO does with cg_leadIndicator 1. tay_leadIndicator 2 adds it everywhere.

#include "cgame/cg_local.h"

qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );		// cg_draw.c

// the speed of the shot of a weapon, 0 when it hits at once or bounces about
static float Lead_Speed( int weapon, qboolean alt, qboolean *falls ) {
	*falls = qfalse;
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return 1600.0f;
	case WP_BLASTER:
		return 2300.0f;
	case WP_BOWCASTER:
		return alt ? 0.0f : 1300.0f;	// the alt fire bounces
	case WP_REPEATER:
		*falls = alt;
		return alt ? 1100.0f : 1600.0f;
	case WP_DEMP2:
		return alt ? 0.0f : 1800.0f;
	case WP_FLECHETTE:
		return alt ? 0.0f : 3500.0f;
	case WP_ROCKET_LAUNCHER:
		return 900.0f;
	case WP_CONCUSSION:
		return alt ? 0.0f : 3000.0f;
	default:
		return 0.0f;
	}
}

// dots every few units of the 640 x 480 grid
static void Lead_DottedLine( float x1, float y1, float x2, float y2, const vec4_t color ) {
	const float	length = sqrtf( ( x2 - x1 ) * ( x2 - x1 ) + ( y2 - y1 ) * ( y2 - y1 ) );
	// an aim point next to the plane of the camera lies far off the screen: no more dots than the screen holds,
	// and none for a length that is no number
	const int	dots = ( length < 2000.0f ) ? (int)( length / 5.0f ) : 400;
	int			i;

	for ( i = 0; i <= dots; i++ ) {
		const float f = dots ? (float)i / dots : 0.0f;

		CG_FillRect( x1 + ( x2 - x1 ) * f - 0.5f, y1 + ( y2 - y1 ) * f - 0.5f, 1.0f, 1.0f, color );
	}
}

/*
=================
CG_LeadDraw

From CG_Draw2D, with the 2D in the middle (PLACE_CENTER)
=================
*/
void CG_LeadDraw( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const vec4_t		color = { 1.0f, 0.25f, 0.2f, 0.85f };
	usercmd_t			cmd;
	qboolean			alt = qfalse, falls;
	float				speed, lag = 0.0f;
	vec3_t				eye;
	int					i;

	if ( !tay_leadIndicator.integer || !cg.snap || cg.demoPlayback ) {
		return;
	}
	if ( ps->stats[STAT_HEALTH] <= 0 || ( ps->pm_flags & PMF_FOLLOW ) || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	if ( trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) ) {
		alt = (qboolean)( ( cmd.buttons & BUTTON_ALT_ATTACK ) != 0 );
	}
	speed = Lead_Speed( ps->weapon, alt, &falls );
	if ( speed <= 0.0f ) {
		return;
	}
	if ( tay_leadIndicator.integer >= 2 || !atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vs_caps" ) ) ) {
		lag = Q_max( 0, cg.snap->ping ) * 0.001f;
	}
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t	*cent = &cg_entities[i];
		const float		*v = cent->currentState.pos.trDelta;
		vec3_t			aim, chest;
		trace_t			tr;
		float			t, x, y, ax, ay;
		int				pass;

		if ( i == ps->clientNum || !cent->currentValid || cent->currentState.eType != ET_PLAYER
			|| ( cent->currentState.eFlags & EF_DEAD ) ) {
			continue;
		}
		if ( ( cgs.gametype >= GT_TEAM && cgs.clientinfo[i].team == ps->persistant[PERS_TEAM] ) || CG_JPOtherDimension( i ) ) {
			continue;
		}
		if ( VectorLengthSquared( v ) < 1.0f && !falls ) {
			continue;		// standing: the crosshair on him is right
		}
		VectorCopy( cent->lerpOrigin, chest );
		CG_Trace( &tr, eye, NULL, NULL, chest, ps->clientNum, CONTENTS_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}

		// where he will be when the shot gets there, the flight worked out twice to the place it aims at
		t = Distance( eye, chest ) / speed;
		for ( pass = 0; pass < 2; pass++ ) {
			VectorMA( chest, t + lag, v, aim );
			t = Distance( eye, aim ) / speed;
		}
		if ( falls ) {
			aim[2] += 0.5f * DEFAULT_GRAVITY * t * t - 40.0f * t;
		}

		if ( !CG_WorldCoordToScreenCoordFloat( chest, &x, &y ) || !CG_WorldCoordToScreenCoordFloat( aim, &ax, &ay ) ) {
			continue;
		}
		Lead_DottedLine( x, y, ax, ay, color );
		CG_FillRect( ax - 2.0f, ay - 2.0f, 4.0f, 4.0f, color );
	}
}
