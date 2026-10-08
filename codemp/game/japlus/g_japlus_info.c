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
JA+ information commands: ammove, amDMGs, duelWhois.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "game/g_local.h"
#include "g_japlus.h"

static void JP_Print( int clientNum, const char *text ) {
	trap->SendServerCommand( clientNum, va( "print \"%s\"", text ) );
}

static void JP_ByLine( int clientNum ) {
	trap->SendServerCommand( clientNum, va( "print \"\n^5---------------------\n^5%s by Slider\n---------------------\n\"", JP_VERSION ) );
}

void Cmd_ammove_f( gentity_t *ent ) {
	const int c = ent-g_entities;

	trap->SendServerCommand( c, va( "print \"^5Normal g_forceRegenTime:^2%i  ^1-  ^5jp_privateDuelForceRegen:^2%i\n \"", g_forceRegenTime.integer, jp_privateDuelForceRegen.integer ) );
	JP_Print( c, "\n^5Player Total NEW MOVES LIST: \n" );
	if ( jp_noKATA.integer == 1 )			JP_Print( c, "^5Saber kata^2 are desactivated\n " );
	if ( jp_jk2RedDFA.integer == 1 )		JP_Print( c, "^5JK2 Single RED DFA^2 is activated\n " );
	if ( jp_allowflipkick.integer == 0 )	JP_Print( c, "^2FlipKicks are desactivated\n " );
	if ( jp_allowSPattacks.integer == 0 )	JP_Print( c, "^2All SP Attacks desactivated\n " );

	JP_Print( c, "^3STAFF:\n        ^5DOUBLE KICK: ^2 Alt Attack ^72 ennemis to your left and right or back and front\n        ^5SPIN KICK: ^2 Alt Attack ^7when enemis are close to you\n        ^5KICK FORWARD: ^2forward + alt attack ^7if enemy very close to you in front of you : \n" );
	JP_Print( c, "^3SINGLE + first stance of STAFF and DUAL:\n        ^5PULL/SLASH ATTACK : ^2pull + attack+ back\n" );
	JP_Print( c, "^3LONG JUMP :  ^2: forward +jump+ speed ^7 you can also add attack action\n" );
	if ( jp_allowSPForces.integer != 2 )	JP_Print( c, "^2 desactivated :" );
	JP_Print( c, "^3FORCE DRAIN GRAB (sith kiss):  ^2: force drain with melee weapon^7 in front of your ennemy \n" );
	if ( jp_allowSPForces.integer != 2 )	JP_Print( c, "^2 desactivated :" );
	JP_Print( c, "^3FORCE PUSH GRAB (jedi tornado):  ^2: force push with melee weapon ^7 in front of your ennemy (only for Jedi)\n" );
	if ( jp_allowNewDFA.integer == 0 )		JP_Print( c, "^2All new DFA Attacks Desactivated by admin :\n " );
	JP_Print( c, "^3NEW DFA MOVES:  ^2Forward + Jump + attack ^7when you are close to an enemy..... It works with the following sabers:\n        ^5SINGLE SABER  : ^2Red STANCE\n        ^5DUAL  : ^2Yellow second STANCE\n        ^5STAFF : ^2Yellow second STANCE\n" );
	JP_Print( c, "^3NEW DFA JUMP BACK SPIN KICK: ^2 Alt Attack + forward + Jump using STAFF stance, Dual fast Stance\n" );
	JP_Print( c, "^3NEW DFA SABER FLIP STAB: ^2 Alt Attack + forward + Jump using single strong stance\n" );
	JP_Print( c, "^3NEW DFA HIGH JUMP FLIP BACK KICK: ^2 Alt Attack + forward + Jump using Dual Stance or single medium Stance in front of an enemy\n" );
	JP_Print( c, "^3NEW DFA MELEE/STAFF MOVES:  ^5JA+ SPIN KICK: ^2 Alt Attack + forward + left\n        ^5JA+ BACK KICK: ^2 Alt Attack + forward + right\n" );
	JP_Print( c, "^3NEW DFA Pull impale MOVES:  ^2 While pulling press Attack ^7in front of an enemy\n" );
	JP_ByLine( c );
}

// one line of the damage list: the range from the cvars and the built in default
static void JP_DmgRange( int c, const char *name, const vmCvar_t *mn, const vmCvar_t *mx, int defMin, int defMax, qboolean lead ) {
	trap->SendServerCommand( c, va( "print \"%s     - %s : [%i,%i] ^5Default : ^2[%i,%i]\n \"", lead ? "\n" : "", name, mn->integer, mx->integer, defMin, defMax ) );
}

static void JP_DmgFlat( int c, const char *name, const vmCvar_t *v, int def, qboolean lead ) {
	trap->SendServerCommand( c, va( "print \"%s     - %s : %i ^5Default : ^2%i\n \"", lead ? "\n" : "", name, v->integer, def ) );
}

void Cmd_amDMGs_f( gentity_t *ent ) {
	const int c = ent-g_entities;

	JP_Print( c, "\n\n^5Damages LIST: \n" );
	trap->SendServerCommand( c, va( "print \"^2g_saberDamageScale: ^5%f.\n \"", g_saberDamageScale.value ) );

	if ( d_saberSPStyleDamage.integer == 1 && jp_alterDMG.integer == 0 )
		JP_Print( c, "^2Single Player Saber Damages (default in BaseJKA) are ^5Activated.\n " );
	else
		JP_Print( c, "^2Multiplayer Saber Damages are ^5Activated. (the new JA+ damage system looking like the JK2 one.)\n " );

	if ( jp_flipkickScaleDMG.value != 1.0f )
		trap->SendServerCommand( c, va( "print \"^2FlipKick scale factor altered ^5%f ^2default 1\n \"", jp_flipkickScaleDMG.value ) );
	if ( jp_allowModelScale.integer == 1 && jp_allowDmgSpeedScale.integer == 1 )
		JP_Print( c, "^2Damages and speed are scaled according to your model size.\n " );

	if ( jp_alterDMG.integer != 0 ) {
		trap->SendServerCommand( c, va( "print \"     - Saber Throw Scale Factor DMG : %f ^5Default : ^2%f\n \"", jp_SaberThrowScaleDMG.value, 1.0f ) );
		JP_DmgRange( c, "StabDown Attack", &jp_StabDownAttackMin, &jp_StabDownAttackMax, 2, 50, qfalse );
		JP_DmgRange( c, "RollStab Attack", &jp_RollStabAttackMin, &jp_RollStabAttackMax, 2, 12, qfalse );
		JP_DmgRange( c, "NewSpin DfaAttack", &jp_NewSpinDfaAttackMin, &jp_NewSpinDfaAttackMax, 2, 60, qfalse );
		JP_DmgRange( c, "NewPull Attack", &jp_NewPullAttackMin, &jp_NewPullAttackMax, 2, 50, qfalse );

		JP_Print( c, "\n^3DUAL & STAFF DMGs:\n " );
		JP_DmgFlat( c, "StaffDual SpinAttack", &jp_StaffDualSpinAttack, 10, qfalse );
		JP_DmgFlat( c, "Dual KataAttack", &jp_DualKataAttack, 50, qfalse );
		JP_DmgRange( c, "Staff KataAttack", &jp_StaffKataAttackMin, &jp_StaffKataAttackMax, 30, 50, qfalse );
		JP_DmgRange( c, "Staff BackAttack", &jp_StaffBackAttackMin, &jp_StaffBackAttackMax, 2, 30, qfalse );
		JP_DmgRange( c, "Dual BackAttack", &jp_DualBackAttackMin, &jp_DualBackAttackMax, 2, 25, qfalse );
		JP_DmgRange( c, "StaffDual OtherAttack", &jp_StaffDualOtherAttackMin, &jp_StaffDualOtherAttackMax, 2, 45, qfalse );
		JP_DmgRange( c, "StaffDual NormalAttack", &jp_StaffDualNormalAttackMin, &jp_StaffDualNormalAttackMax, 2, 60, qfalse );

		JP_Print( c, "^3SINGLE DMGs:\n " );
		JP_DmgRange( c, "Single KataAttack", &jp_SingleKataAttackMin, &jp_SingleKataAttackMax, 30, 50, qfalse );
		JP_DmgRange( c, "Strong NormalAttack", &jp_SingleStrongNormalAttackMin, &jp_SingleStrongNormalAttackMax, 2, 110, qtrue );
		JP_DmgRange( c, "Strong DfaAttack", &jp_SingleStrongDfaAttackMin, &jp_SingleStrongDfaAttackMax, 2, 180, qfalse );
		JP_DmgRange( c, "Strong BackAttack", &jp_SingleStrongBackAttackMin, &jp_SingleStrongBackAttackMax, 2, 50, qfalse );
		JP_DmgFlat( c, "Strong OtherAttack", &jp_SingleStrongOtherAttack, 100, qfalse );
		JP_DmgRange( c, "Medium DfaAttack", &jp_SingleMediumDfaAttackMin, &jp_SingleMediumDfaAttackMax, 2, 70, qtrue );
		JP_DmgRange( c, "Medium BackAttack", &jp_SingleMediumBackAttackMin, &jp_SingleMediumBackAttackMax, 2, 35, qfalse );
		JP_DmgFlat( c, "Medium OtherAttack", &jp_SingleMediumOtherAttack, 60, qfalse );
		JP_DmgRange( c, "Fast LungeAttack", &jp_SingleFastLungeAttackMin, &jp_SingleFastLungeAttackMax, 2, 30, qtrue );
		JP_DmgRange( c, "Fast BackAttack", &jp_SingleFastBackAttackMin, &jp_SingleFastBackAttackMax, 2, 30, qfalse );
		JP_DmgFlat( c, "Fast OtherAttack", &jp_SingleFastOtherAttack, 35, qfalse );
		JP_ByLine( c );
		return;
	}

	if ( d_saberSPStyleDamage.integer == 0 )
		JP_Print( c, "^2The Saber Damages of the MP Style are ^5NOT ^7altered.\n " );
}

void Cmd_duelWhois_f( gentity_t *ent ) {
	const int c = ent-g_entities;
	int i;

	JP_Print( c, "^5All Duelers :\n" );
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *a = &g_entities[i], *b;

		if ( !a->inuse || !a->client || a->client->pers.connected != CON_CONNECTED || !a->client->ps.duelInProgress )
			continue;
		if ( a->client->ps.duelIndex < 0 || a->client->ps.duelIndex >= level.maxclients || a->client->ps.duelIndex < i )
			continue;	// every pair once
		b = &g_entities[a->client->ps.duelIndex];
		if ( !b->inuse || !b->client )
			continue;
		trap->SendServerCommand( c, va( "print \"\t--->%s ^3VS ^7%s\n\"", a->client->pers.netname, b->client->pers.netname ) );
	}
}
