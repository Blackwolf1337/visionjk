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

// cg_hudsiege.c -- VisionJK: the team game parts of the new HUD in place of the stock ones: in siege a card at the
// top with the objective the own team is at, how many there are and the time of the round, and the seconds until
// one comes back; the list of the team (cg_drawTeamOverlay) as glass rows with where each one is and his health;
// and in a vehicle its own two panels (hull and shields, speed, turbo and ammo) in place of the panels of the
// player. Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "game/bg/bg_saga.h"
#include "cg_hudkit.h"

#define SIEGE_OBJECTIVES	16

extern int	cg_siegeDeathTime;		// cg_draw.c: when one comes back after a death in siege
void		CG_SiegeGetObjectiveDescription( int team, int objective, char *buffer, int bufferSize );

int		cg_hudSiegeTime = -1;		// the seconds the round has left as CG_Draw2D works them out, -1 none
qboolean	cg_hudSiegeMine;			// the timed team is the own one

// whether an objective of a team is done (the cvars CG_ParseSiegeObjectiveStatus sets)
static qboolean Siege_Done( int team, int objective ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( va( "team%i_objective%i", team, objective ), value, sizeof( value ) );
	return (qboolean)( atoi( value ) != 0 );
}

static struct {
	int		refreshed;
	int		count, current;
	char	name[256];
} siegeCard;

// the objectives of the own team: how many, the first one not done and its name (once a second, the siege file is
// parsed for it)
static void Siege_Refresh( void ) {
	const int	team = cg.predictedPlayerState.persistant[PERS_TEAM] == TEAM_BLUE ? SIEGETEAM_TEAM2 : SIEGETEAM_TEAM1;
	char		name[256];
	int			i;

	if ( siegeCard.refreshed && cg.time - siegeCard.refreshed < 1000 && cg.time >= siegeCard.refreshed ) {
		return;
	}
	siegeCard.refreshed = cg.time;
	siegeCard.count = 0;
	siegeCard.current = -1;
	siegeCard.name[0] = '\0';
	for ( i = 1; i <= SIEGE_OBJECTIVES; i++ ) {
		CG_SiegeGetObjectiveDescription( team, i, name, sizeof( name ) );
		if ( !name[0] ) {
			break;
		}
		siegeCard.count = i;
		if ( siegeCard.current < 0 && !Siege_Done( team, i ) ) {
			siegeCard.current = i;
			// a name from the string package
			if ( name[0] == '@' && trap->SE_GetStringTextString( name + 1, siegeCard.name, sizeof( siegeCard.name ) ) ) {
				continue;
			}
			Q_strncpyz( siegeCard.name, name, sizeof( siegeCard.name ) );
		}
	}
}

/*
=================
CG_HudSiegeTop

In siege, at the top in the middle: the objective with steps for all of them (done green, the current one in the
accent) and the time of the round; under it the seconds until one comes back after a death. Returns the y under it.
=================
*/
float CG_HudSiegeTop( float y ) {
	const float	w = 310.0f, h = 40.0f, x = SCREEN_WIDTH * 0.5f - w * 0.5f;
	const int	team = cg.predictedPlayerState.persistant[PERS_TEAM] == TEAM_BLUE ? SIEGETEAM_TEAM2 : SIEGETEAM_TEAM1;
	int			i;

	if ( cgs.gametype != GT_SIEGE ) {
		return y;
	}
	Siege_Refresh();
	if ( siegeCard.count > 0 || cg_hudSiegeTime >= 0 ) {
		Hud_Glass( x, y, w, h, 6.0f, 1.0f, NULL, qfalse );
		if ( siegeCard.count > 0 ) {
			const float sw = ( w - 90.0f - ( siegeCard.count - 1 ) * 2.0f ) / siegeCard.count;

			Hud_Caps( x + 8.0f, y + 8.0f, hudAccent, 1.0f, HUD_LEFT, siegeCard.current > 0
				? va( "%s %i / %i", CG_VisionText( "Objective" ), siegeCard.current, siegeCard.count ) : CG_VisionText( "All objectives done" ) );
			Hud_TextMid( x + 8.0f, y + 20.0f, HUD_BODY, hudBright, 1.0f, HUD_LEFT, siegeCard.name, w - 100.0f );
			for ( i = 1; i <= siegeCard.count; i++ ) {
				const qboolean	done = Siege_Done( team, i );
				vec4_t			step;

				Hud_Color( step, done ? hudGreen : i == siegeCard.current ? hudAccent : colorWhite, done || i == siegeCard.current ? 1.0f : 0.14f );
				Skin_Panel( x + 8.0f + ( i - 1 ) * ( sw + 2.0f ), y + 29.0f, sw, 2.5f, 1.25f, step, NULL );
			}
		}
		if ( cg_hudSiegeTime >= 0 ) {
			Hud_TextMid( x + w - 8.0f, y + 17.0f, HUD_LARGE, cg_hudSiegeMine ? hudWarm : hudBright, 1.0f, HUD_RIGHT,
				Hud_Clock( cg_hudSiegeTime * 1000 ), 0.0f );
			Hud_Caps( x + w - 8.0f, y + 30.0f, hudDim, 1.0f, HUD_RIGHT, CG_VisionText( "Round time" ) );
		}
		y += h + HUD_GAP;
	}
	if ( cg_siegeDeathTime > cg.time ) {
		const char *text = CG_VisionTextWith( "Respawn in %i s", va( "%i", ( cg_siegeDeathTime - cg.time ) / 1000 + 1 ) );

		y = CG_HudNoticeCard( y, CG_VisionText( "Respawn" ), text, hudWarm, 0, qfalse );
	}
	return y;
}

/*
=================
CG_HudTeamList

The own team at the right edge from y: a row each with the name, where he is and his health, the dead ones faint.
From the team info of the server (cg_drawTeamOverlay). Returns the y under it.
=================
*/
float CG_HudTeamList( float y ) {
	const float	w = 136.0f, rowH = 13.0f, x = SCREEN_WIDTH - HUD_EDGE - w;
	const int	team = cg.snap->ps.persistant[PERS_TEAM];
	int			rows[8], count = 0, i;

	if ( !cg_drawTeamOverlay.integer || ( team != TEAM_RED && team != TEAM_BLUE ) ) {
		return y;
	}
	for ( i = 0; i < numSortedTeamPlayers && count < (int)ARRAY_LEN( rows ); i++ ) {
		const clientInfo_t *ci = &cgs.clientinfo[sortedTeamPlayers[i]];

		if ( ci->infoValid && ci->team == team ) {
			rows[count++] = sortedTeamPlayers[i];
		}
	}
	if ( !count ) {
		return y;
	}
	Hud_Glass( x, y, w, count * rowH + 8.0f, 5.0f, 1.0f, Hud_TeamColor( team ), qfalse );
	for ( i = 0; i < count; i++ ) {
		const clientInfo_t	*ci = &cgs.clientinfo[rows[i]];
		const float			mid = y + 4.0f + i * rowH + rowH * 0.5f;
		const float			alpha = ci->health > 0 ? 1.0f : 0.5f;
		const char			*place = ci->location > 0 ? CG_ConfigString( CS_LOCATIONS + ci->location ) : "";
		float				nx = x + 6.0f;
		char				upper[64];

		nx += Hud_TextMid( nx, mid, HUD_SMALL, hudText, alpha, HUD_LEFT, ci->name, 56.0f ) + 4.0f;
		if ( place && place[0] ) {
			Q_strncpyz( upper, place, sizeof( upper ) );
			Q_strupr( upper );
			Hud_TextMid( nx, mid, HUD_CAPS, hudDim, alpha, HUD_LEFT, upper, x + w - 50.0f - nx );
		}
		Hud_Bar( x + w - 44.0f, mid - 1.0f, 22.0f, 2.0f, ci->health / 100.0f,
			ci->health <= 25 ? hudRed : ci->health <= 50 ? hudWarm : hudGreen, alpha, qfalse );
		Hud_TextMid( x + w - 6.0f, mid, HUD_SMALL, ci->health <= 25 ? hudRed : hudText, alpha, HUD_RIGHT, va( "%i", Q_max( 0, ci->health ) ), 0.0f );
	}
	return y + count * rowH + 8.0f + HUD_GAP;
}

static const float *Siege_HullColor( float f ) {
	return f <= 0.25f ? hudRed : f <= 0.5f ? hudWarm : hudBright;
}

/*
=================
CG_HudVehicle

In a vehicle: where it hides its rider (a fighter, a walker) two panels in place of the player's, the hull with
its shields at the left, the speed, the turbo and the ammo at the right; where the rider is seen (a swoop, an
animal) a small card above the selection row, since the panels of the player stay. Returns whether the panels of
the player are drawn.
=================
*/
qboolean CG_HudVehicle( const centity_t *veh ) {
	const vehicleInfo_t	*info = veh->m_pVehicle->m_pVehicleInfo;
	const playerState_t	*vs = &cg.predictedVehicleState;
	const float			hull = info->armor > 0 ? (float)vs->stats[STAT_HEALTH] / info->armor : 0.0f;
	const float			shields = info->shields > 0 ? (float)vs->stats[STAT_ARMOR] / info->shields : -1.0f;
	const float			speed = info->speedMax > 0.0f ? vs->speed / info->speedMax : 0.0f;
	const float			turbo = info->turboRecharge > 0 ? Com_Clamp( 0.0f, 1.0f, (float)( cg.time - veh->m_pVehicle->m_iTurboTime ) / info->turboRecharge ) : -1.0f;
	rectDef_t			left, right;
	char				name[64];

	Q_strncpyz( name, info->name ? info->name : "", sizeof( name ) );
	if ( !info->hideRider ) {
		const float w = 150.0f, h = 22.0f, x = SCREEN_WIDTH * 0.5f - w * 0.5f, y = 340.0f;

		Hud_Glass( x, y, w, h, 5.0f, 1.0f, NULL, qtrue );
		Hud_Caps( x + 6.0f, y + 7.0f, hudWarm, 1.0f, HUD_LEFT, name );
		Hud_Caps( x + w - 6.0f, y + 7.0f, hudDim, 1.0f, HUD_RIGHT, va( "%i", (int)vs->speed ) );
		Hud_Bar( x + 6.0f, y + 13.0f, w - 12.0f, 2.5f, hull, hull <= 0.25f ? hudRed : hull <= 0.5f ? hudWarm : hudGreen, 1.0f, qfalse );
		Hud_Bar( x + 6.0f, y + 17.5f, w - 12.0f, 1.5f, speed, hudText, 1.0f, qfalse );
		return qtrue;
	}

	Hud_PanelRect( qfalse, &left );
	Hud_PanelRect( qtrue, &right );
	CG_Place2D( PLACE_LEFT );
	Hud_Chip( left.x, left.y - HUD_GAP - 17.0f, 17.0f, 0, name, hudWarm, 1.0f, HUD_LEFT, -1.0f );
	Hud_Glass( left.x, left.y, left.w, left.h, 7.0f, 1.0f, NULL, qfalse );
	Hud_TextMid( left.x + 40.0f, left.y + 13.0f, HUD_HUGE, Siege_HullColor( hull ), 1.0f, HUD_RIGHT, va( "%i", (int)( hull * 100.0f + 0.5f ) ), 0.0f );
	Hud_Bar( left.x + 48.0f, left.y + 10.0f, left.w - 56.0f, 6.0f, hull, Siege_HullColor( hull ), 1.0f, qfalse );
	if ( shields >= 0.0f ) {
		Hud_TextMid( left.x + 40.0f, left.y + 31.0f, HUD_BODY, hudAccent, 1.0f, HUD_RIGHT, va( "%i", (int)( shields * 100.0f + 0.5f ) ), 0.0f );
		Hud_Bar( left.x + 48.0f, left.y + 29.0f, left.w - 56.0f, 4.0f, shields, hudAccent, 1.0f, qfalse );
	}
	CG_Place2D( PLACE_RIGHT );
	Hud_Glass( right.x, right.y, right.w, right.h, 7.0f, 1.0f, NULL, qfalse );
	Hud_Bar( right.x + 8.0f, right.y + 10.0f, right.w - 60.0f, 6.0f, speed, hudText, 1.0f, qtrue );
	Hud_TextMid( right.x + right.w - 8.0f, right.y + 12.0f, HUD_LARGE, hudBright, 1.0f, HUD_RIGHT, va( "%i", (int)vs->speed ), 0.0f );
	Hud_Caps( right.x + right.w - 8.0f, right.y + 24.0f, hudDim, 1.0f, HUD_RIGHT, CG_VisionText( "Speed" ) );
	if ( turbo >= 0.0f ) {
		Hud_Bar( right.x + 8.0f, right.y + 20.0f, right.w - 60.0f, 2.0f, turbo, turbo >= 1.0f ? hudGreen : hudWarm, 1.0f, qtrue );
	}
	if ( info->weapon[0].ID && info->weapon[0].ammoMax > 0 ) {
		Hud_Bar( right.x + 8.0f, right.y + 30.0f, info->weapon[1].ID ? ( right.w - 64.0f ) * 0.5f : right.w - 60.0f, 4.0f,
			(float)vs->ammo[0] / info->weapon[0].ammoMax, hudAmmo, 1.0f, qfalse );
	}
	if ( info->weapon[1].ID && info->weapon[1].ammoMax > 0 ) {
		Hud_Bar( right.x + 12.0f + ( right.w - 64.0f ) * 0.5f, right.y + 30.0f, ( right.w - 64.0f ) * 0.5f, 4.0f,
			(float)vs->ammo[1] / info->weapon[1].ammoMax, hudRed, 1.0f, qfalse );
		Hud_TextMid( right.x + right.w - 8.0f, right.y + 34.0f, HUD_SMALL, hudAmmo, 1.0f, HUD_RIGHT, va( "%i", vs->ammo[1] ), 0.0f );
	}
	CG_Place2D( PLACE_CENTER );
	return qfalse;
}
