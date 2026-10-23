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

// g_policy.h -- the learned policy (g_policy.c): a small neural network that makes the usercmd of a bot

#pragma once

#include "g_clone.h"

#define POLICY_PROBES_LOGGED	8		// POLICY_PROBES of policy_spec.h: the columns wall0-7 and drop0-7 of g_movelog.c
#define POLICY_SLOTS		2		// 0: vs_policyModel, the one that learns; 1: vs_policyOpponent, an older one

// how an episode of the rollouts ends
#define POLICY_DONE_END		1		// a kill or a death
#define POLICY_DONE_TIME	2		// the round ran out

qboolean	Policy_Ready( int slot );
void		Policy_Reset( int clientNum, int slot );
void		Policy_Decide( const gentity_t *self, const gentity_t *enemy, int elapsed, cloneCmd_t *out );
void		Policy_Damage( const gentity_t *targ, const gentity_t *attacker, int take, int mod );
void		Policy_Reward( int clientNum, float kills, float deaths );
void		Policy_EndEpisode( int clientNum, int done );
void		Policy_MatchStart( int clientNum, int opponentLevel );
void		Policy_MatchEnd( int clientNum );
void		Policy_Probes( const gentity_t *ent, const vec3_t toward, float *wall, float *drop );
