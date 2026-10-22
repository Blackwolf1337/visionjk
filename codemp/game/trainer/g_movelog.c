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
Movement log ("vs_movelog <player>" on the server console and rcon, "Record moves" in the web panel): every usercmd of a player with the player
state after its pmove and the nearest other player, one CSV line each, into movelog/<time>_<slot>.csv of the
home folder of the server. Made to see how players hook and flip kick, for the training bot (g_trainer.c).

The command toggles it. It stops by itself after MOVELOG_MAX_MSEC, when the player leaves and with the map.
Everybody on the server is told in the chat when it starts and stops, and every MOVELOG_NOTICE_MSEC while it
runs.

Not from JA+.
*/

#include "game/g_local.h"

#define MOVELOG_MAX_MSEC	(15 * 60 * 1000)
#define MOVELOG_NOTICE_MSEC	(2 * 60 * 1000)

typedef struct {
	fileHandle_t	f;
	int				startTime;
	int				noticeTime;		// level.time of the next reminder in the chat
} moveLog_t;

static moveLog_t moveLogs[MAX_CLIENTS];

static void MoveLog_Print( fileHandle_t f, const char *fmt, ... ) {
	char	line[1024];
	va_list	ap;
	int		len;

	va_start( ap, fmt );
	len = Q_vsnprintf( line, sizeof( line ), fmt, ap );
	va_end( ap );
	if ( len > 0 )
		trap->FS_Write( line, Q_min( len, (int)sizeof( line ) - 1 ), f );
}

// a line in the chat of everybody
static void MoveLog_Announce( int clientNum, const char *what ) {
	trap->SendServerCommand( -1, va( "chat \"^3<Server>^7 The movements of %s^7 %s\"",
		level.clients[clientNum].pers.netname, what ) );
}

static void MoveLog_Stop( int clientNum ) {
	if ( moveLogs[clientNum].f ) {
		trap->FS_Close( moveLogs[clientNum].f );
		moveLogs[clientNum].f = 0;
		MoveLog_Announce( clientNum, "are not recorded any more." );
	}
}

static qboolean MoveLog_Start( gentity_t *ent ) {
	const int	clientNum = ent - g_entities;
	char		name[MAX_QPATH];
	qtime_t		now;

	trap->RealTime( &now );
	Com_sprintf( name, sizeof( name ), "movelog/%04i%02i%02i_%02i%02i%02i_%i.csv", now.tm_year + 1900, now.tm_mon + 1,
		now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, clientNum );
	trap->FS_Open( name, &moveLogs[clientNum].f, FS_WRITE );
	if ( !moveLogs[clientNum].f ) {
		trap->Print( "vs_movelog: can not write %s\n", name );
		return qfalse;
	}
	moveLogs[clientNum].startTime = level.time;
	moveLogs[clientNum].noticeTime = level.time + MOVELOG_NOTICE_MSEC;
	trap->Print( "vs_movelog: %s\n", name );
	MoveLog_Announce( clientNum, "are recorded now (for the training bot)." );
	MoveLog_Print( moveLogs[clientNum].f, "# %s\n", ent->client->pers.netname_nocolor );
	MoveLog_Print( moveLogs[clientNum].f,
		"time,cmdTime,buttons,fwd,right,up,pitch,yaw,x,y,z,vx,vy,vz,ground,legsAnim,legsTimer,torsoAnim,torsoTimer,"
		"pmFlags,eFlags,eFlags2,kick,hook,hooked,weapon,weaponTime,saberMove,style,holstered,force,forceActive,levitation,"
		"health,other,dist,dx,dy,dz,ovx,ovy,ovz,oground,olegs,otorso,osaberMove,oeFlags2,ohp\n" );
	return qtrue;
}

qboolean G_MoveLogActive( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && moveLogs[clientNum].f );
}

// vs_movelog <player> and the button of the web panel: on or off for that player
qboolean G_MoveLogToggle( gentity_t *ent ) {
	const int clientNum = ent - g_entities;

	if ( moveLogs[clientNum].f ) {
		MoveLog_Stop( clientNum );
		return qfalse;
	}
	return MoveLog_Start( ent );
}

// the other player nearest to ent, -1 if none
static int MoveLog_Nearest( const gentity_t *ent, float *bestDist ) {
	int i, best = -1;

	*bestDist = 0.0f;
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *other = &g_entities[i];
		float dist;

		if ( other == ent || !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		dist = Distance( ent->client->ps.origin, other->client->ps.origin );
		if ( best < 0 || dist < *bestDist ) {
			best = i;
			*bestDist = dist;
		}
	}
	return best;
}

// from ClientThink_real after its pmove: one line for this usercmd
void G_MoveLogCmd( gentity_t *ent, const usercmd_t *ucmd ) {
	const int			clientNum = ent - g_entities;
	const playerState_t	*ps = &ent->client->ps;
	fileHandle_t		f;
	int					other;
	float				dist;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !(f = moveLogs[clientNum].f) )
		return;
	if ( level.time - moveLogs[clientNum].startTime > MOVELOG_MAX_MSEC ) {
		MoveLog_Stop( clientNum );
		trap->Print( "vs_movelog: stopped for %s after %i minutes\n", ent->client->pers.netname, MOVELOG_MAX_MSEC / 60000 );
		return;
	}
	if ( level.time >= moveLogs[clientNum].noticeTime ) {
		moveLogs[clientNum].noticeTime = level.time + MOVELOG_NOTICE_MSEC;
		MoveLog_Announce( clientNum, "are still recorded." );
	}

	MoveLog_Print( f, "%i,%i,%i,%i,%i,%i,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i",
		level.time, ucmd->serverTime, ucmd->buttons, ucmd->forwardmove, ucmd->rightmove, ucmd->upmove,
		ps->viewangles[PITCH], ps->viewangles[YAW], ps->origin[0], ps->origin[1], ps->origin[2],
		ps->velocity[0], ps->velocity[1], ps->velocity[2], ps->groundEntityNum != ENTITYNUM_NONE,
		ps->legsAnim, ps->legsTimer, ps->torsoAnim, ps->torsoTimer, ps->pm_flags, ps->eFlags, ps->eFlags2,
		ps->forceKickFlip, ent->client->hook != NULL, ent->client->pers.jp.hooked, ps->weapon, ps->weaponTime,
		ps->saberMove, ps->fd.saberAnimLevel, ps->saberHolstered, ps->fd.forcePower, ps->fd.forcePowersActive,
		ps->fd.forcePowerLevel[FP_LEVITATION], ent->health );

	other = MoveLog_Nearest( ent, &dist );
	if ( other >= 0 ) {
		const playerState_t *ops = &g_entities[other].client->ps;

		MoveLog_Print( f, ",%i,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%i,%i,%i,%i,%i,%i\n", other, dist,
			ops->origin[0] - ps->origin[0], ops->origin[1] - ps->origin[1], ops->origin[2] - ps->origin[2],
			ops->velocity[0], ops->velocity[1], ops->velocity[2], ops->groundEntityNum != ENTITYNUM_NONE,
			ops->legsAnim, ops->torsoAnim, ops->saberMove, ops->eFlags2, g_entities[other].health );
	}
	else
		MoveLog_Print( f, ",-1,0,0,0,0,0,0,0,0,0,0,0,0,0\n" );
}

// from ClientDisconnect
void G_MoveLogDisconnect( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS )
		MoveLog_Stop( clientNum );
}

// from G_ShutdownGame: the files are closed with the map
void G_MoveLogShutdown( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ )
		MoveLog_Stop( i );
}
