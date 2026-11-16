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

// cg_hudcombat.c -- VisionJK: what the new HUD shows of a fight, in place of the stock pictures and text: the
// name of who is under the crosshair with his health where the client knows it, the bar of a hack or of another
// timed action under the crosshair, a caption under the brackets of a lock, the powerups with the seconds left
// under the card of the match, who hit one last, what was picked up at the right edge, and one line of the
// frames, the ping and the clock (cg_drawFPS, cg_drawTimer, cg_drawSnapshot). Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"
#include "cg_hudkit.h"

#define CROSS_Y			( SCREEN_HEIGHT * 0.5f )

// the health of a player as far as the client knows it, -1 not: a team mate (the team info), a duelist (the
// duel healths the server sends)
static int Combat_KnownHealth( int client ) {
	if ( cgs.gametype >= GT_TEAM && cgs.clientinfo[client].team == cgs.clientinfo[cg.snap->ps.clientNum].team
		&& cgs.clientinfo[client].health > 0 ) {
		return cgs.clientinfo[client].health;
	}
	if ( cgs.gametype == GT_DUEL && cgs.showDuelHealths >= 1 ) {
		if ( client == cgs.duelist1 ) {
			return cgs.duelist1health;
		}
		if ( client == cgs.duelist2 ) {
			return cgs.duelist2health;
		}
	}
	return -1;
}

/*
=================
CG_HudTargetName

The name of who is under the crosshair, under it: tint the colour the game gave it (friend, foe, out of reach in a
duel), alpha its fade. A bar of his health where the client knows it.
=================
*/
void CG_HudTargetName( const char *name, qboolean pilot, const float *tint, float alpha, int client ) {
	const float	y = CROSS_Y + 18.0f + cg_crosshairY.value;
	const int	health = ( client >= 0 && client < MAX_CLIENTS ) ? Combat_KnownHealth( client ) : -1;
	char		text[MAX_STRING_CHARS];

	if ( alpha <= 0.0f ) {
		return;
	}
	Com_sprintf( text, sizeof( text ), pilot ? "%s (pilot)" : "%s", name );
	Hud_TextMid( SCREEN_WIDTH * 0.5f + cg_crosshairX.value, y, HUD_BODY, tint, alpha, HUD_CENTER, text, 200.0f );
	if ( health >= 0 ) {
		Hud_Bar( SCREEN_WIDTH * 0.5f + cg_crosshairX.value - 27.0f, y + 6.0f, 54.0f, 2.2f, health / 100.0f,
			health <= 25 ? hudRed : health <= 50 ? hudWarm : hudGreen, alpha, qfalse );
	}
}

/*
=================
CG_HudProgress

A timed action as a card under the crosshair: a picture (0 none), a caption, the bar and how far it is. slot: 0
the first card, 1 one under it.
=================
*/
void CG_HudProgress( int slot, qhandle_t icon, const char *caption, float fraction, const float *rgb ) {
	const float	w = 130.0f, h = 18.0f, x = SCREEN_WIDTH * 0.5f - w * 0.5f, y = CROSS_Y + 34.0f + slot * ( h + HUD_GAP );
	float		bx = x + 6.0f;

	fraction = Com_Clamp( 0.0f, 1.0f, fraction );
	Hud_Glass( x, y, w, h, 4.0f, 1.0f, NULL, qtrue );
	if ( icon ) {
		trap->R_SetColor( NULL );
		CG_DrawPic( bx, y + 3.0f, h - 6.0f, h - 6.0f, icon );
		bx += h - 2.0f;
	}
	if ( caption && caption[0] ) {
		bx += Hud_Caps( bx, y + h * 0.5f, hudText, 1.0f, HUD_LEFT, caption ) + 5.0f;
	}
	Hud_Bar( bx, y + h * 0.5f - 1.5f, x + w - 28.0f - bx, 3.0f, fraction, rgb ? rgb : hudAccent, 1.0f, qfalse );
	Hud_Caps( x + w - 6.0f, y + h * 0.5f, hudDim, 1.0f, HUD_RIGHT, va( "%i %%", (int)( fraction * 100.0f ) ) );
}

/*
=================
CG_HudLockOn

The lock of the rocket launcher in the new HUD (scope/cg_scope.c CG_ScopeDrawLockOn), on the box of the target
(its middle and half its size, in units of the screen): a bracket at each corner, in the middle a ring that closes
with the lock (fraction 0 to 1), and under the box how far it is on a chip of glass. Locked: red, with a pulse.
=================
*/
#define LOCK_RING_DOTS	48

void CG_HudLockOn( float cx, float cy, float hw, float hh, float fraction, qboolean locked ) {
	static const vec4_t	pale = { 1.0f, 0.62f, 0.59f, 1.0f };
	const float			pulse = locked ? 0.7f + 0.3f * sinf( (float)cg.time * 0.03f ) : 1.0f;
	const float			*tint = locked ? hudRed : pale;
	const float			leg = Com_Clamp( 4.0f, 10.0f, Q_min( hw, hh ) * 0.45f ), thick = 1.2f;
	const float			radius = Com_Clamp( 7.0f, 20.0f, Q_min( hw, hh ) * 0.55f ), dot = 1.5f;
	const int			lit = (int)( Com_Clamp( 0.0f, 1.0f, fraction ) * LOCK_RING_DOTS + 0.5f );
	const char			*text = locked ? CG_VisionText( "Locked" ) : CG_VisionTextWith( "Locking %i %%", va( "%i", (int)( fraction * 100.0f ) ) );
	char				upper[128];
	vec4_t				c, faint;
	int					i;

	Hud_Color( c, tint, pulse );
	// the brackets: two legs at each corner
	for ( i = 0; i < 4; i++ ) {
		const float	x = ( i & 1 ) ? cx + hw : cx - hw, y = ( i & 2 ) ? cy + hh : cy - hh;

		Skin_Panel( ( i & 1 ) ? x - leg : x, ( i & 2 ) ? y - thick : y, leg, thick, 0.0f, c, NULL );
		Skin_Panel( ( i & 1 ) ? x - thick : x, ( i & 2 ) ? y - leg : y, thick, leg, 0.0f, c, NULL );
	}
	// the ring: from the top, clockwise, as far as the lock is
	Hud_Color( faint, colorWhite, 0.18f );
	for ( i = 0; i < LOCK_RING_DOTS; i++ ) {
		const float angle = 2.0f * M_PI * i / LOCK_RING_DOTS;

		Skin_Panel( cx + sinf( angle ) * radius - dot * 0.5f, cy - cosf( angle ) * radius - dot * 0.5f, dot, dot, dot * 0.5f,
			i < lit ? c : faint, NULL );
	}
	Q_strncpyz( upper, text, sizeof( upper ) );
	Q_strupr( upper );
	Hud_Glass( cx - Hud_TextWidth( upper, HUD_CAPS ) * 0.5f - 5.0f, cy + hh + 4.0f, Hud_TextWidth( upper, HUD_CAPS ) + 10.0f, 10.0f, 3.0f, 1.0f,
		locked ? hudRed : NULL, qtrue );
	Hud_TextMid( cx, cy + hh + 9.0f, HUD_CAPS, tint, 1.0f, HUD_CENTER, upper, 0.0f );
}

/*
=================
CG_HudPowerups

The powerups the player has as chips from the right edge, from y down, each with the seconds it has left (a flag
has no end). Returns the y under them. PLACE_RIGHT is set.
=================
*/
float CG_HudPowerups( float y ) {
	const float	h = 17.0f;
	int			j;

	for ( j = 0; j < PW_NUM_POWERUPS; j++ ) {
		const gitem_t	*item;
		qhandle_t		icon;
		const char		*left = NULL;

		if ( cg.snap->ps.powerups[j] <= cg.time || !( item = BG_FindItemForPowerup( j ) ) ) {
			continue;
		}
		if ( cgs.gametype == GT_CTY && ( j == PW_REDFLAG || j == PW_BLUEFLAG ) ) {
			icon = trap->R_RegisterShaderNoMip( j == PW_REDFLAG ? "gfx/hud/mpi_rflag_ys" : "gfx/hud/mpi_bflag_ys" );
		} else {
			icon = trap->R_RegisterShader( item->icon );
		}
		if ( j != PW_REDFLAG && j != PW_BLUEFLAG && j != PW_NEUTRALFLAG && ( cg.snap->ps.powerups[j] - cg.time ) / 1000 < 999 ) {
			left = va( "%i s", ( cg.snap->ps.powerups[j] - cg.time ) / 1000 );
		}
		Hud_Chip( SCREEN_WIDTH - HUD_EDGE, y, h, icon, left, j == PW_REDFLAG ? hudRed : j == PW_BLUEFLAG ? hudBlue : hudWarm,
			1.0f, HUD_RIGHT, -1.0f );
		y += h + HUD_GAP;
	}
	return y;
}

/*
=================
CG_HudAttacker

Who hit the player last, for the seconds the game keeps it (ATTACKER_HEAD_TIME): his picture and name under the
card of the match. Returns the y under it.
=================
*/
float CG_HudAttacker( float y ) {
	const int	attacker = cg.predictedPlayerState.persistant[PERS_ATTACKER];
	const float	w = 136.0f, h = 24.0f, x = SCREEN_WIDTH - HUD_EDGE - w;
	const clientInfo_t *ci;

	if ( !cg_drawEnemyInfo.integer || attacker < 0 || attacker >= MAX_CLIENTS || attacker == cg.snap->ps.clientNum
		|| !cg.attackerTime || cg.time - cg.attackerTime > ATTACKER_HEAD_TIME || cg.snap->ps.stats[STAT_HEALTH] <= 0
		|| !cgs.clientinfo[attacker].infoValid ) {
		return y;
	}
	ci = &cgs.clientinfo[attacker];
	Hud_Glass( x, y, w, h, 5.0f, 1.0f, NULL, qfalse );
	if ( ci->modelIcon ) {
		trap->R_SetColor( NULL );
		CG_DrawPic( x + 4.0f, y + 4.0f, h - 8.0f, h - 8.0f, ci->modelIcon );
	}
	Hud_Caps( x + h + 2.0f, y + 7.0f, hudRed, 1.0f, HUD_LEFT, CG_VisionText( "Last hit by" ) );
	Hud_TextMid( x + h + 2.0f, y + 16.5f, HUD_BODY, hudBright, 1.0f, HUD_LEFT, ci->name, w - h - 8.0f );
	return y + h + HUD_GAP;
}

/*
=================
CG_HudPickup

What was picked up last as a card at the right edge, fading out over three seconds: its picture and its name.
PLACE_RIGHT is set.
=================
*/
void CG_HudPickup( void ) {
	const int	value = cg.itemPickup;
	const float	w = 110.0f, h = 24.0f, x = SCREEN_WIDTH - HUD_EDGE - w, y = 300.0f;
	float		*fade;
	char		name[128];

	if ( !value || cg_items[value].icon == -1 || !( fade = CG_FadeColor( cg.itemPickupTime, 3000 ) ) ) {
		return;
	}
	CG_RegisterItemVisuals( value );
	Hud_Glass( x, y, w, h, 5.0f, fade[3], NULL, qfalse );
	Hud_Color( fade, colorWhite, fade[3] );
	trap->R_SetColor( fade );
	CG_DrawPic( x + 4.0f, y + 4.0f, h - 8.0f, h - 8.0f, cg_items[value].icon );
	trap->R_SetColor( NULL );
	Q_strncpyz( name, CG_ItemName( &bg_itemlist[value] ), sizeof( name ) );
	Hud_TextMid( x + h + 2.0f, y + h * 0.5f, HUD_BODY, hudBright, fade[3], HUD_LEFT, name, w - h - 8.0f );
}

// The course of the ping in the line of frames and ping, in place of the lagometer: a track as the bars of the HUD
// have, w units wide, the newest snapshot at the right, each a thin bar from the bottom as high as its ping (in the
// colour of how good it is); one the server held back for the rate amber, one that never came red to the top.
#define PERF_GRAPH_W	34.0f		// in the line of its own; in the card it fills the row

static void Combat_PingGraph( float x, float y, float w, float h ) {
	const vec4_t	track = { 1.0f, 1.0f, 1.0f, 0.06f };
	const float		step = 1.5f, inner = h - 2.0f;
	const int		samples = (int)( ( w - 2.0f ) / step );
	int				i, flags, most = 100;

	Skin_Panel( x, y, w, h, Q_min( 3.0f, h * 0.5f ), track, NULL );
	for ( i = 0; i < samples; i++ ) {
		most = Q_max( most, CG_LagSnapshot( i, &flags ) );
	}
	for ( i = 0; i < samples; i++ ) {
		const int	ping = CG_LagSnapshot( i, &flags );
		const float	sx = x + w - 1.0f - ( i + 1 ) * step;
		float		bh;
		vec4_t		c;

		if ( ping == -2 ) {
			break;
		}
		if ( ping < 0 ) {
			Hud_Color( c, hudRed, 0.9f );
			bh = inner;
		} else {
			Hud_Color( c, ( flags & SNAPFLAG_RATE_DELAYED ) ? hudWarm : ping >= 150 ? hudRed : ping >= 80 ? hudWarm : hudGreen, 0.85f );
			bh = Q_max( 0.8f, inner * (float)ping / most );
		}
		Skin_Panel( sx, y + 1.0f + inner - bh, step - 0.4f, bh, 0.0f, c, NULL );
	}
}

// the parts of the line of frames and ping that are switched on: a number and its word; clock: the timer too
static int Perf_Parts( char parts[4][2][64], qboolean clock ) {
	int count = 0;

	if ( cg_drawFPS.integer ) {
		Com_sprintf( parts[count][0], sizeof( parts[0][0] ), "%i", CG_FpsValue() );
		Q_strncpyz( parts[count++][1], "FPS", sizeof( parts[0][1] ) );
		Com_sprintf( parts[count][0], sizeof( parts[0][0] ), "%i", cg.snap->ping );
		Q_strncpyz( parts[count++][1], "MS", sizeof( parts[0][1] ) );
	}
	if ( clock && cg_drawTimer.integer ) {
		Q_strncpyz( parts[count][0], CG_TimerText(), sizeof( parts[0][0] ) );
		parts[count++][1][0] = '\0';
	}
	if ( cg_drawSnapshot.integer ) {
		Com_sprintf( parts[count][0], sizeof( parts[0][0] ), "%i", cg.latestSnapshotNum );
		Q_strncpyz( parts[count++][1], "SNAP", sizeof( parts[0][1] ) );
	}
	return count;
}

// the parts from x on, their middle at midY; the course of the ping after the ping, or ending at graphRight (> 0)
static float Perf_Draw( char parts[4][2][64], int count, float x, float midY, float h, float graphRight ) {
	const float	gap = 8.0f, start = x;
	int			i;

	for ( i = 0; i < count; i++ ) {
		x += Hud_TextMid( x, midY, HUD_SMALL, hudBright, 1.0f, HUD_LEFT, parts[i][0], 0.0f );
		if ( parts[i][1][0] ) {
			x += 3.0f + Hud_Caps( x + 3.0f, midY, hudDim, 1.0f, HUD_LEFT, parts[i][1] );
		}
		x += gap;
		// (after the ping its course: in the card from there to the end of the row)
		if ( cg_drawFPS.integer && i == 1 ) {
			float	rest = 0.0f, gw;
			int		k;

			// (the parts after it, the snapshot number, keep their room at the end)
			for ( k = i + 1; k < count; k++ ) {
				rest += gap + Hud_TextWidth( parts[k][0], HUD_SMALL ) + ( parts[k][1][0] ? Hud_TextWidth( parts[k][1], HUD_CAPS ) + 3.0f : 0.0f );
			}
			gw = graphRight > 0.0f ? Q_max( PERF_GRAPH_W, graphRight - rest - x ) : PERF_GRAPH_W;

			Combat_PingGraph( x, midY - h * 0.5f, gw, h );
			x += gw + gap;
		}
	}
	return x - start;
}

/*
=================
CG_HudPerfLine

The frames, the ping with its course, the clock and the snapshot numbers that are switched on, as one glass line at
the right edge from y, where the card of the match is not there to take them (CG_HudPerfRow). Returns the y under it.
=================
*/
float CG_HudPerfLine( float y, qboolean clock ) {
	const float	h = 14.0f, gap = 8.0f;
	char		parts[4][2][64];
	const int	count = Perf_Parts( parts, clock );
	float		w = 12.0f;
	int			i;

	if ( !count ) {
		return y;
	}
	if ( cg_drawFPS.integer ) {
		w += PERF_GRAPH_W + gap;
	}
	for ( i = 0; i < count; i++ ) {
		w += Hud_TextWidth( parts[i][0], HUD_SMALL ) + ( parts[i][1][0] ? Hud_TextWidth( parts[i][1], HUD_CAPS ) + 3.0f : 0.0f ) + ( i ? gap : 0.0f );
	}
	Hud_Glass( SCREEN_WIDTH - HUD_EDGE - w, y, w, h, 3.5f, 1.0f, NULL, qtrue );
	Perf_Draw( parts, count, SCREEN_WIDTH - HUD_EDGE - w + 6.0f, y + h * 0.5f, h - 6.0f, 0.0f );
	return y + h + HUD_GAP;
}

// is there anything for the row of frames and ping in the card of the match (the clock is on the card already)
qboolean CG_HudPerfAny( void ) {
	return (qboolean)( cg_drawFPS.integer || cg_drawSnapshot.integer );
}

// that row in the card, from x for w units, its middle at midY: the numbers from the left, the course of the ping
// at the right end
void CG_HudPerfRow( float x, float midY, float w ) {
	char		parts[4][2][64];
	const int	count = Perf_Parts( parts, qfalse );

	if ( count ) {
		Perf_Draw( parts, count, x, midY, 7.0f, x + w );
	}
}

// the warning of the ammo as a chip, NULL when there is nothing to warn of
const char *CG_HudAmmoWarning( void ) {
	if ( !cg.lowAmmoWarning || cg.snap->ps.weapon == WP_SABER || cg.snap->ps.weapon == WP_MELEE ) {
		return NULL;
	}
	return CG_VisionText( cg.lowAmmoWarning == 2 ? "Out of ammo" : "Low ammo" );
}

// a readout beside the lens: a caption, the value large, a bar (fraction < 0: none)
static float Combat_Readout( float x, float y, int align, const char *caption, const char *value, const float *rgb, float fraction ) {
	Hud_Caps( x, y, hudDim, 1.0f, align, caption );
	Hud_TextMid( x, y + 13.0f, HUD_HUGE, rgb, 1.0f, align, value, 0.0f );
	if ( fraction >= 0.0f ) {
		Hud_Bar( align == HUD_RIGHT ? x - 70.0f : x, y + 25.0f, 70.0f, 2.0f, fraction, rgb, 1.0f, align == HUD_RIGHT );
	}
	return y + 38.0f;
}

/*
=================
CG_HudScopeReadouts

With the lens of a scope (scope/cg_scope.c) on the dark beside it: the zoom and the range to what is in the middle
at the left. Ammo, health and shield stay in the panels at the bottom, which the lens leaves alone.
=================
*/
void CG_HudScopeReadouts( float radius, float times, int meters ) {
	const float	left = SCREEN_WIDTH * 0.5f - radius * 1.12f - 12.0f;
	float		y = 200.0f;

	y = Combat_Readout( left, y, HUD_RIGHT, CG_VisionText( "Zoom" ), va( "%.1fx", times ), hudBright, Com_Clamp( 0.0f, 1.0f, times / 8.0f ) );
	Combat_Readout( left, y, HUD_RIGHT, CG_VisionText( "Range" ), meters >= 0 ? va( "%i m", meters ) : "---", hudBright, -1.0f );
}

// the line of the photo mode on a strip of glass at the bottom
void CG_HudPhotoLine( const char *text ) {
	const float w = Hud_TextWidth( text, HUD_SMALL ) + 20.0f, h = 18.0f;

	Hud_Glass( SCREEN_WIDTH * 0.5f - w * 0.5f, SCREEN_HEIGHT - HUD_EDGE - h, w, h, 5.0f, 1.0f, NULL, qtrue );
	Hud_TextMid( SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT - HUD_EDGE - h * 0.5f, HUD_SMALL, hudText, 1.0f, HUD_CENTER, text, 0.0f );
}
