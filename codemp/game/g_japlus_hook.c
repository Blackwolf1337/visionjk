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
JA+ grappling hook (+button12). Hook missile, rope cutting, fuel and the client think part.
The hook uses ps.lastHitLoc as the grapple point and ps.jetpackFuel as its fuel.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

extern void G_SoundOnEnt( gentity_t *ent, soundChannel_t channel, const char *soundPath );
extern float ShortestLineSegBewteen2LineSegs( vec3_t start1, vec3_t end1, vec3_t start2, vec3_t end2, vec3_t close_pnt1, vec3_t close_pnt2 );

#define JP_HOOK_FUEL_MAX		100
#define JP_HOOK_MIN_FUEL		10		// fuel needed to shoot the hook
#define JP_HOOK_CLIPMASK		0x41301
#define DF_NO_HOOK_FUEL			0x200	// g_dmflags bit: the hook needs no fuel

static qboolean JP_HookOwnerValid( const gentity_t *owner ) {
	return owner && owner->inuse && owner->client && owner->client->pers.connected == CON_CONNECTED;
}

/*
=================
Weapon_HookFree

removes the hook missile (and its client side twin) and releases the owner
=================
*/
void Weapon_HookFree( gentity_t *hook ) {
	gentity_t *owner;
	int twin;

	if ( !hook )
		return;

	owner = hook->parent;
	if ( JP_HookOwnerValid( owner ) && (!owner->client->hook || owner->client->hook == hook) ) {
		owner->client->pers.jp.hooked = qfalse;
		owner->client->ps.pm_flags &= ~PMF_GRAPPLE_PULL;
		owner->client->ps.eFlags &= ~EF_GRAPPLE_SUSPEND;
		owner->client->ps.eFlags2 &= ~EF2_HOOK;
		owner->client->hook = NULL;
	}

	twin = hook->s.userInt1;
	if ( twin > 0 && twin < ENTITYNUM_MAX_NORMAL && g_entities[twin].classname && !strcmp( g_entities[twin].classname, "hook2" ) )
		G_FreeEntity( &g_entities[twin] );

	G_FreeEntity( hook );
}

void JP_HookFree( gentity_t *ent ) {
	if ( ent && ent->client && ent->client->hook )
		Weapon_HookFree( ent->client->hook );
}

/*
=================
Weapon_HookThink

keeps the hook on the player it hit, as long as the owner holds it
=================
*/
void Weapon_HookThink( gentity_t *hook ) {
	gentity_t *owner = hook ? hook->parent : NULL;

	if ( JP_HookOwnerValid( owner ) && owner->client->sess.sessionTeam != TEAM_SPECTATOR && owner->client->hook == hook ) {
		gentity_t *target = hook->enemy;

		if ( target && target->inuse && target->client && target->takedamage ) {
			vec3_t old, pos;

			VectorCopy( hook->r.currentOrigin, old );
			pos[0] = (target->r.mins[0] + target->r.maxs[0]) * 0.5f + target->r.currentOrigin[0];
			pos[1] = (target->r.mins[1] + target->r.maxs[1]) * 0.5f + target->r.currentOrigin[1];
			pos[2] = (target->r.mins[2] + target->r.maxs[2]) * 0.5f + target->r.currentOrigin[2];
			SnapVectorTowards( pos, old );
			G_SetOrigin( hook, pos );
		}

		VectorCopy( hook->r.currentOrigin, owner->client->ps.lastHitLoc );
		hook->think = Weapon_HookThink;
		hook->nextthink = level.time + 300000;
		return;
	}

	Weapon_HookFree( hook );
}

/*
=================
fire_grapple
=================
*/
static gentity_t *fire_grapple( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t *hook;
	vec3_t angles;

	VectorNormalize( dir );

	hook = CreateMissile( start, dir, (float)jp_hookSpeed.integer, 10000, self, qfalse );
	hook->classname = "hook";
	hook->think = Weapon_HookFree;
	vectoangles( dir, angles );
	G_SetAngles( hook, angles );

	if ( self->client->pers.jp.pluginVersion >= 1.0f ) {
		// the plugin draws the rope from a second missile
		gentity_t *twin = CreateMissile( start, dir, (float)jp_hookSpeed.integer, 10000, self, qfalse );

		twin->classname = "hook2";
		twin->dflags = DAMAGE_NO_KNOCKBACK;
		twin->s.weapon = WP_BRYAR_PISTOL;
		twin->methodOfDeath = MOD_STUN_BATON;
		twin->damage = 0;
		twin->clipmask = JP_HOOK_CLIPMASK;
		twin->s.otherEntityNum = MAX_CLIENTS;	// (32 in the original, not the owner)

		hook->s.userInt1 = twin->s.number;
		hook->s.weapon = WP_STUN_BATON;
	}
	else {
		hook->s.weapon = WP_BRYAR_PISTOL;
	}

	hook->damage = 1;
	hook->methodOfDeath = MOD_STUN_BATON;
	hook->dflags = DAMAGE_NO_KNOCKBACK;
	hook->clipmask = JP_HOOK_CLIPMASK;

	self->client->hook = hook;
	hook->s.otherEntityNum = self->s.number;

	return hook;
}

/*
=================
Weapon_GrapplingHook_Fire
=================
*/
static void Weapon_GrapplingHook_Fire( gentity_t *ent ) {
	vec3_t forward, right, up, muzzle;

	if ( (ent->client->ps.eFlags2 & EF2_HOOK) || ent->client->hook )
		return;

	AngleVectors( ent->client->ps.viewangles, forward, right, up );
	CalcMuzzlePoint( ent, forward, right, up, muzzle );
	fire_grapple( ent, muzzle, forward );

	ent->client->ps.eFlags2 |= EF2_HOOK;
	ent->client->pers.jp.hooked = qfalse;
	G_SetAnim( ent, NULL, SETANIM_BOTH, BOTH_RESISTPUSH, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 0 );
	G_SoundOnEnt( ent, CHAN_ITEM, "sound/weapons/blaster/reflect1.mp3" );
}

/*
=================
G_WeapCutGrappleRope

a saber or projectile moving from "from" to "to" cuts the ropes of the hooks it touches.
"cutOwnToo" lets the owner of the cutting thing cut his own rope.
=================
*/
void G_WeapCutGrappleRope( gentity_t *cutter, vec3_t from, vec3_t to, float radius, qboolean cutOwnToo ) {
	int i;

	if ( !jp_allowHook.integer || (dmflags.integer & DF_NO_HOOK_FUEL) )
		return;
	if ( !cutter || Distance( from, to ) < 1.0f )
		return;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *owner = &g_entities[i];
		gentity_t *hook;
		vec3_t ropeStart, closeOnRope, closeOnCutter, flash;
		float ropeLen, fuelScale;

		if ( !JP_HookOwnerValid( owner ) || (!cutOwnToo && owner == cutter) )
			continue;
		hook = owner->client->hook;
		if ( !hook || hook->parent != owner || !G_canInterferWith( cutter->s.number, i ) )
			continue;

		if ( owner->client->renderInfo.handRBolt == -1 )
			VectorCopy( owner->r.currentOrigin, ropeStart );
		else {
			mdxaBone_t boltMatrix;
			trap->G2API_GetBoltMatrix( owner->ghoul2, 0, owner->client->renderInfo.handRBolt, &boltMatrix, owner->client->ps.viewangles,
				owner->client->ps.origin, level.time, NULL, owner->modelScale );
			BG_GiveMeVectorFromMatrix( &boltMatrix, ORIGIN, ropeStart );
		}

		if ( Distance( hook->r.currentOrigin, ropeStart ) < 1.0f )
			continue;

		ropeLen = ShortestLineSegBewteen2LineSegs( ropeStart, hook->r.currentOrigin, from, to, closeOnRope, closeOnCutter );
		fuelScale = owner->client->ps.iModelScale > 0 ? (float)owner->client->ps.iModelScale / 100.0f : 1.0f;

		if ( ropeLen < radius && (owner != cutter || Distance( closeOnRope, ropeStart ) > fuelScale * 60.0f) ) {
			vec3_t dir;

			owner->client->pers.jp.hookTime = (level.time - jp_hookFloodProtect.integer) + 10000;
			owner->client->ps.jetpackFuel = 0;
			owner->client->jetPackToggleTime = level.time + 3000;
			Weapon_HookFree( hook );

			VectorSubtract( closeOnCutter, closeOnRope, dir );
			VectorCopy( closeOnRope, flash );
			G_PlayEffectID( G_EffectIndex( "blaster/deflect" ), flash, dir );

			owner->client->ps.otherKiller = cutter->s.number;
			owner->client->ps.otherKillerTime = level.time + 10000;
			owner->client->ps.otherKillerDebounceTime = level.time + 5000;
		}
	}
}

/*
=================
JP_HookCanFire
=================
*/
static qboolean JP_HookCanFire( gentity_t *ent, int buttons ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;

	return (buttons & BUTTON_HOOK)
		&& !ps->duelInProgress
		&& !client->pers.jp.sleeping
		&& !client->pers.jp.down
		&& ps->pm_type != PM_DEAD
		&& ent->health > 0
		&& ps->stats[STAT_HEALTH] > 0
		&& !(ps->eFlags & EF_DEAD)
		&& !BG_SaberInAttack( ps->saberMove )
		&& !ps->m_iVehicleNum
		&& client->respawnTime + 1500 < level.time	// not right after the spawn
		&& !BG_InGrappleMove( ps->legsAnim )
		&& !BG_InGrappleMove( ps->torsoAnim )
		&& client->pers.jp.hookTime + jp_hookFloodProtect.integer < level.time
		&& ps->fd.forceGripBeingGripped < level.time
		&& !(ps->eFlags2 & EF2_HOOK)
		&& !client->pers.jp.hooked
		&& !client->hook
		&& !(ps->pm_flags & PMF_GRAPPLE_PULL)
		&& !(ps->eFlags & EF_GRAPPLE_SUSPEND)
		&& !(ps->eFlags & (EF_JETPACK_FLAMING | (1 << 11)))
		&& !(ps->eFlags2 & EF2_FLYING)
		&& (ps->jetpackFuel >= JP_HOOK_MIN_FUEL || (dmflags.integer & DF_NO_HOOK_FUEL));
}

static qboolean JP_HookAllowedHere( gentity_t *ent ) {
	const gclient_t *client = ent->client;

	if ( client->ps.eFlags & EF_ALT_DIM )
		return qfalse;
	if ( g_gametype.integer != GT_FFA && g_gametype.integer != GT_TEAM )
		return qfalse;

	if ( jp_allowHook.integer == 1 )
		return g_gametype.integer == GT_FFA || g_jediVmerc.integer;
	if ( jp_allowHook.integer == 2 )
		return g_jediVmerc.integer && client->ps.trueNonJedi == 1;	// only the mercs may hook
	return qfalse;
}

/*
=================
JP_HookClientThink

called from ClientThink_real: shoots, holds and releases the hook according to the buttons
=================
*/
void JP_HookClientThink( gentity_t *ent, const usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;
	const int buttons = ucmd->buttons;

	if ( jp_debugClientCMD.integer && (client->hook || (buttons & BUTTON_HOOK)) && client->pers.jp.hookDebugTime < level.time ) {
		client->pers.jp.hookDebugTime = level.time + 250;
		trap->Print( "HOOK dbg: btn=%d hook=%d hooked=%d pmf_pull=%d susp=%d ef2hook=%d fuel=%d allowed=%d canfire=%d vel=%.0f %.0f %.0f plugin=%.2f\n",
			(buttons & BUTTON_HOOK) != 0, client->hook != NULL, client->pers.jp.hooked, (ps->pm_flags & PMF_GRAPPLE_PULL) != 0,
			(ps->eFlags & EF_GRAPPLE_SUSPEND) != 0, (ps->eFlags2 & EF2_HOOK) != 0, ps->jetpackFuel, JP_HookAllowedHere( ent ),
			JP_HookCanFire( ent, buttons ), ps->velocity[0], ps->velocity[1], ps->velocity[2], client->pers.jp.pluginVersion );
	}

	if ( client->pers.connected == CON_CONNECTED && JP_HookAllowedHere( ent ) ) {
		if ( JP_HookCanFire( ent, buttons ) ) {
			client->pers.jp.hookTime = level.time;
			Weapon_GrapplingHook_Fire( ent );
		}

		if ( client->hook && !(buttons & BUTTON_HOOK) ) {
			if ( client->pers.jp.pluginVersion < 1.0f ) {
				// no plugin: letting go of the button drops the hook
				Weapon_HookFree( client->hook );
			}
			else if ( !client->pers.jp.hooked || !(buttons & BUTTON_WALKING) ) {
				if ( !client->pers.jp.hooked )
					Weapon_HookFree( client->hook );
				else {
					// hang on the rope
					ps->pm_flags &= ~PMF_GRAPPLE_PULL;
					ps->eFlags |= EF_GRAPPLE_SUSPEND;
				}
			}
			else {
				ps->pm_flags &= ~PMF_GRAPPLE_PULL;
				ps->eFlags &= ~EF_GRAPPLE_SUSPEND;
			}
		}

		// pressing the button again while hanging pulls again
		if ( client->hook && (buttons & BUTTON_HOOK) && client->pers.jp.hooked && (ps->eFlags & EF_GRAPPLE_SUSPEND) ) {
			ps->eFlags &= ~EF_GRAPPLE_SUSPEND;
			ps->pm_flags |= PMF_GRAPPLE_PULL;
		}
	}

	if ( client->hook && (client->pers.jp.sleeping || client->pers.jp.down || !(ps->eFlags2 & EF2_HOOK)
		|| ps->pm_type == PM_DEAD || ps->duelInProgress) ) {
		Weapon_HookFree( client->hook );
	}

	// +use lets go of the hook
	if ( client->hook && (buttons & BUTTON_USE) && client->pers.jp.hookUseTime + 200 < level.time ) {
		client->pers.jp.hookUseTime = level.time;
		Weapon_HookFree( client->hook );
	}

	if ( !client->hook && (client->pers.jp.hooked || (ps->pm_flags & PMF_GRAPPLE_PULL) || (ps->eFlags & EF_GRAPPLE_SUSPEND) || (ps->eFlags2 & EF2_HOOK)) ) {
		client->pers.jp.hooked = qfalse;
		ps->pm_flags &= ~PMF_GRAPPLE_PULL;
		ps->eFlags &= ~EF_GRAPPLE_SUSPEND;
		ps->eFlags2 &= ~EF2_HOOK;
	}
}

/*
=================
JP_HookFuelThink

called every frame for every client without a running jetpack from G_RunFrame: the fuel (shared with the
jetpack, and its timers) drains while the hook flies and while the rope pulls, and comes back else
=================
*/
void JP_HookFuelThink( gentity_t *ent ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;

	if ( !(dmflags.integer & DF_NO_HOOK_FUEL) && client->hook && (ps->eFlags2 & EF2_HOOK) && !(ps->eFlags & EF_GRAPPLE_SUSPEND) ) {
		if ( client->jetPackDebReduce < level.time ) {
			if ( ps->pm_flags & PMF_GRAPPLE_PULL )
				ps->jetpackFuel -= 1;
			else if ( !client->pers.jp.hooked )
				ps->jetpackFuel -= jp_hookSpeed.integer / 800 + 1;	// the hook is still flying out

			if ( ps->jetpackFuel < 1 ) {
				ps->jetpackFuel = 0;
				Weapon_HookFree( client->hook );
			}
			client->jetPackDebReduce = level.time + 200;
		}
	}
	else if ( ps->jetpackFuel < JP_HOOK_FUEL_MAX && client->jetPackDebRecharge < level.time ) {
		ps->jetpackFuel++;
		client->jetPackDebRecharge = level.time + 150;
	}
}

/*
=================
JP_HookMissileImpact

called when a missile hit something. Returns qtrue if the missile was a hook (or its client
side twin) and is handled: it sticks to the world or to the player it hit.
=================
*/
static void JP_HookImpactPoint( gentity_t *other, trace_t *trace, qboolean hitPlayer, vec3_t out ) {
	if ( !hitPlayer ) {
		VectorCopy( trace->endpos, out );
		return;
	}
	out[0] = (other->r.mins[0] + other->r.maxs[0]) * 0.5f + other->r.currentOrigin[0];
	out[1] = (other->r.mins[1] + other->r.maxs[1]) * 0.5f + other->r.currentOrigin[1];
	out[2] = (other->r.mins[2] + other->r.maxs[2]) * 0.5f + other->r.currentOrigin[2];
}

qboolean JP_HookMissileImpact( gentity_t *ent, gentity_t *other, trace_t *trace ) {
	const qboolean hitPlayer = other->inuse && other->takedamage && other->client;
	vec3_t pos;

	if ( !ent->classname )
		return qfalse;

	if ( !strcmp( ent->classname, "hook" ) ) {
		gentity_t *effect = G_Spawn();
		gentity_t *owner = ent->parent;

		if ( !hitPlayer ) {
			effect->s.weapon = WP_STUN_BATON;
			G_AddEvent( effect, EV_MISSILE_MISS, DirToByte( trace->plane.normal ) );
			ent->enemy = NULL;
		}
		else {
			G_AddEvent( effect, EV_MISSILE_HIT, DirToByte( trace->plane.normal ) );
			effect->s.otherEntityNum = other->s.number;
			ent->enemy = other;
		}
		JP_HookImpactPoint( other, trace, hitPlayer, pos );

		SnapVectorTowards( pos, ent->s.pos.trBase );
		effect->freeAfterEvent = qtrue;
		effect->s.eType = ET_GENERAL;
		G_SetOrigin( ent, pos );
		G_SetOrigin( effect, pos );

		ent->think = Weapon_HookThink;
		ent->nextthink = level.time + 100;

		if ( JP_HookOwnerValid( owner ) && owner->client->hook == ent ) {
			VectorCopy( ent->r.currentOrigin, owner->client->ps.lastHitLoc );
			if ( !owner->client->pers.jp.hooked ) {
				owner->client->ps.pm_flags |= PMF_GRAPPLE_PULL;
				owner->client->pers.jp.hooked = qtrue;
			}
		}

		trap->LinkEntity( (sharedEntity_t *)ent );
		trap->LinkEntity( (sharedEntity_t *)effect );
		return qtrue;
	}

	if ( !strcmp( ent->classname, "hook2" ) ) {
		JP_HookImpactPoint( other, trace, hitPlayer, pos );
		SnapVectorTowards( pos, ent->s.pos.trBase );
		ent->s.eType = ET_GENERAL;
		G_SetOrigin( ent, pos );
		trap->LinkEntity( (sharedEntity_t *)ent );
		return qtrue;
	}

	return qfalse;
}

/*
=================
JP_MissileIgnoresTarget

projectiles fly through players that are ghosts, mindtricked or asleep, or that are in the other dimension
=================
*/
qboolean JP_MissileIgnoresTarget( gentity_t *missile, gentity_t *other ) {
	gentity_t *owner;

	if ( !JP_HookOwnerValid( other ) )
		return qfalse;

	if ( other->client->pers.jp.isGhost || other->client->pers.jp.mindtricked || other->client->pers.jp.sleeping )
		return qtrue;

	owner = &g_entities[missile->r.ownerNum];
	return jp_altDim.integer && missile->r.ownerNum < MAX_CLIENTS && JP_HookOwnerValid( owner )
		&& !G_PlInSameDim( other, owner );
}
