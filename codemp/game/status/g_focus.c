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

// g_focus.c -- the body of a player who is out of it. Not from JA+, see docs/japlus/server.md, "Focus".
//
// Focus happens in the client (cgame/focus/cg_focus.c of a client of this tree): the world stands still on its
// screen and its player steers a camera, while his body stands in the level. The server does one thing for it:
// the body can not be hurt meanwhile. A client asks for that with the client command "jpfocus 1" where it reads
// bit 4 of g_visionCaps in the server info, and says "jpfocus 0" when its player is back. The answer is a
// server command of the same name, so the client knows that its command arrived (the flood protection of the
// server may drop one): "jpfocus 1 0", the body is protected, or "jpfocus 0 <why>" (focusWhy_t), it is not.
//
// "jpfocus 1 1" is the same from a player who goes to the Vision Temple. Where this server has a temple of its
// own (bit 6 of g_visionCaps, status/g_temple.c) that one is started for him unless it runs, whatever the answer
// about his body is: somebody who only watches gets a no and goes all the same.
//
// It is no way out of a fight. It is granted to somebody who stands on the ground alive, in no duel and with
// no flag, who was not hit by a player and hit none for five seconds, ten seconds after his last time at the
// earliest, and by default in a free for all only (g_visionFocus, as the inactivity protection of JA+). The
// first move or attack of his ends it: who is protected does nothing.
//
// What everybody sees of it, a client that knows nothing of this too: the body meditates (an animation of the
// game, held as the emotes of JA+ are, PM_JPEmoteHeld()), its saber is away, and the shell of a protected
// player is around it, as with the inactivity protection.

#include "game/g_local.h"

#define FOCUS_CALM_MSEC		5000		// no hit by a player and on a player for this long before it is granted
#define FOCUS_AGAIN_MSEC	10000		// and this long between two times

static void Focus_Tell( const gentity_t *ent, qboolean held, focusWhy_t why ) {
	trap->SendServerCommand( ent - g_entities, va( "jpfocus %i %i", held ? 1 : 0, (int)why ) );
}

// the kind of game has it: g_visionFocus 1 a free for all only, 2 every kind
static qboolean Focus_On( void ) {
	// (nobody leaves his body in the Vision Temple: that is where one goes from it, status/g_temple.c)
	if ( g_visionTemple.integer ) {
		return qfalse;
	}
	return (qboolean)( g_visionFocus.integer > 1 || ( g_visionFocus.integer == 1 && level.gametype == GT_FFA ) );
}

static qboolean Focus_Allowed( const gentity_t *ent, focusWhy_t *why ) {
	const gclient_t		*client = ent->client;
	const playerState_t	*ps = &client->ps;
	const jpClient_t	*jp = &client->pers.jp;

	*why = FOCUS_OFF;
	if ( !Focus_On() ) {
		return qfalse;
	}
	*why = FOCUS_NOTNOW;
	if ( client->sess.sessionTeam == TEAM_SPECTATOR || client->tempSpectate >= level.time
		|| ent->health <= 0 || ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL
		|| ps->duelInProgress || ps->m_iVehicleNum || ps->groundEntityNum == ENTITYNUM_NONE
		// (in the pose of this file he may be: he came back and has not moved since)
		|| ( ps->forceHandExtend != HANDEXTEND_NONE && ps->legsAnim != BOTH_MEDITATE ) || jp->sleeping
		|| ps->powerups[PW_REDFLAG] || ps->powerups[PW_BLUEFLAG] || ps->powerups[PW_NEUTRALFLAG] ) {
		return qfalse;
	}
	// (a time from before the level began anew is no fight of this level)
	*why = FOCUS_FIGHT;
	if ( jp->fightTime && jp->fightTime <= level.time && level.time - jp->fightTime < FOCUS_CALM_MSEC ) {
		return qfalse;
	}
	*why = FOCUS_WAIT;
	if ( jp->focusNext > level.time && jp->focusNext - level.time <= FOCUS_AGAIN_MSEC ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
G_FocusEnd

The body is no longer protected. (The flag of the protection goes with the next frame of the client: its time is
over, ClientThink_real().)
=================
*/
void G_FocusEnd( gentity_t *ent, focusWhy_t why ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( !jp->focus ) {
		return;
	}
	jp->focus = qfalse;
	jp->focusNext = level.time + FOCUS_AGAIN_MSEC;
	ent->client->invulnerableTimer = level.time;
	// Up from the pose, as the first key of a player ends an emote of JA+ (PM_JPEmoteEndAnim()): his player is
	// back and may not touch a key for a while.
	if ( ent->client->ps.legsAnim == BOTH_MEDITATE && ent->health > 0 ) {
		ent->client->ps.forceHandExtend = HANDEXTEND_NONE;
		ent->client->ps.forceHandExtendTime = 0;
		G_SetAnim( ent, NULL, SETANIM_BOTH, BOTH_MEDITATE_END, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 0 );
	}
	Focus_Tell( ent, qfalse, why );
}

/*
=================
Cmd_JPFocus_f

"jpfocus 1": the player of this client left his body, protect it. "jpfocus 0": he is back.
=================
*/
void Cmd_JPFocus_f( gentity_t *ent ) {
	jpClient_t	*jp = &ent->client->pers.jp;
	focusWhy_t	why;
	char		arg[8];

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !atoi( arg ) ) {
		G_FocusEnd( ent, FOCUS_ENDED );
		return;
	}
	trap->Argv( 2, arg, sizeof( arg ) );
	if ( atoi( arg ) && G_TempleInstance() ) {
		trap->SendConsoleCommand( EXEC_APPEND, "templeinstance\n" );
	}
	if ( jp->focus ) {
		// asked again: the answer did not arrive
		Focus_Tell( ent, qtrue, FOCUS_ENDED );
		return;
	}
	if ( !Focus_Allowed( ent, &why ) ) {
		Focus_Tell( ent, qfalse, why );
		return;
	}
	jp->focus = qtrue;
	JP_HolsterNow( ent );
	JP_EmoteExec( ent, BOTH_MEDITATE );
	Focus_Tell( ent, qtrue, FOCUS_ENDED );
}

/*
=================
G_FocusThink

With every command of a client that plays, before it is run: the protection for another moment, or its end
=================
*/
void G_FocusThink( gentity_t *ent, const usercmd_t *ucmd ) {
	gclient_t *client = ent->client;

	if ( !client->pers.jp.focus ) {
		return;
	}
	// He is back: a client that is out of its body sends no move and no attack. (Its "jpfocus 0" may have been
	// dropped, or it is a client that only typed the command.)
	if ( ucmd->forwardmove || ucmd->rightmove || ucmd->upmove
		|| ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE ) ) ) {
		G_FocusEnd( ent, FOCUS_ENDED );
		return;
	}
	if ( ent->health <= 0 || client->ps.stats[STAT_HEALTH] <= 0 || client->ps.duelInProgress || !Focus_On() ) {
		G_FocusEnd( ent, FOCUS_NOTNOW );
		return;
	}
	client->ps.eFlags |= EF_INVULNERABLE;
	client->invulnerableTimer = level.time + 1000;
	// The pose again where something ended it: the hook of another player or the level pulled the body off its
	// feet. Once it stands and does nothing.
	if ( client->ps.forceHandExtend == HANDEXTEND_NONE && client->ps.legsAnim != BOTH_MEDITATE
		&& client->ps.legsTimer <= 0 && client->ps.torsoTimer <= 0 && client->ps.groundEntityNum != ENTITYNUM_NONE ) {
		JP_EmoteExec( ent, BOTH_MEDITATE );
	}
}

// a player was hit by another one: neither of them leaves his body for a while
void G_FocusFight( gentity_t *targ, gentity_t *attacker ) {
	if ( targ && targ->client && attacker && attacker->client && targ != attacker ) {
		targ->client->pers.jp.fightTime = attacker->client->pers.jp.fightTime = level.time;
	}
}
