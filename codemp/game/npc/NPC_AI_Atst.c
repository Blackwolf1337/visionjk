/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

#include "b_local.h"

#define	MIN_MELEE_RANGE		640
#define	MIN_MELEE_RANGE_SQR	( MIN_MELEE_RANGE * MIN_MELEE_RANGE )

// VisionJK: how close it walks up to its enemy. 128 in single player: there it came on until it stood on him (and
// crushed him, G_ATSTFrame); here it stops at a distance and shoots
#define MIN_DISTANCE		320
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

#define TURN_OFF			0x00000100//G2SURFACEFLAG_NODESCENDANTS

#define LEFT_ARM_HEALTH 40
#define RIGHT_ARM_HEALTH 40

extern void G_SoundOnEnt( gentity_t *ent, soundChannel_t channel, const char *soundPath );
/*
-------------------------
NPC_ATST_Precache
-------------------------
*/
void NPC_ATST_Precache(void)
{
	G_SoundIndex( "sound/chars/atst/atst_damaged1" );
	G_SoundIndex( "sound/chars/atst/atst_damaged2" );

//	RegisterItem( BG_FindItemForWeapon( WP_ATST_MAIN ));	//precache the weapon
	//rwwFIXMEFIXME: add this weapon
	RegisterItem( BG_FindItemForWeapon( WP_BOWCASTER ));	//precache the weapon
	RegisterItem( BG_FindItemForWeapon( WP_ROCKET_LAUNCHER ));	//precache the weapon

	G_EffectIndex( "env/med_explode2" );
	// VisionJK: its guns (ATST_Fire)
	G_EffectIndex( "atst/shot" );
	G_EffectIndex( "atst/wall_impact" );
	G_EffectIndex( "atst/main_muzzleflash" );
	G_EffectIndex( "atst/side_main_shot" );
	G_EffectIndex( "atst/side_main_impact" );
	G_EffectIndex( "atst/side_alt_shot" );
	G_EffectIndex( "atst/side_alt_explosion" );
	G_EffectIndex( "atst/alt_muzzleflash" );
//	G_EffectIndex( "smaller_chunks" );
	G_EffectIndex( "blaster/smoke_bolton" );
	G_EffectIndex( "explosions/droidexplosion1" );
}

//-----------------------------------------------------------------
#if 0
static void ATST_PlayEffect( gentity_t *self, const int boltID, const char *fx )
{
	if ( boltID >=0 && fx && fx[0] )
	{
		mdxaBone_t	boltMatrix;
		vec3_t		org, dir;

		trap->G2API_GetBoltMatrix( self->ghoul2, 0,
					boltID,
					&boltMatrix, self->r.currentAngles, self->r.currentOrigin, level.time,
					NULL, self->modelScale );

		BG_GiveMeVectorFromMatrix( &boltMatrix, ORIGIN, org );
		BG_GiveMeVectorFromMatrix( &boltMatrix, NEGATIVE_Y, dir );

		G_PlayEffectID( G_EffectIndex((char *)fx), org, dir );
	}
}
#endif

/*
-------------------------
G_ATSTCheckPain

Called by NPC's and player in an ATST
-------------------------
*/

void G_ATSTCheckPain( gentity_t *self, gentity_t *other, int damage )
{
	//int newBolt;
	//int hitLoc = gPainHitLoc;

	if ( rand() & 1 )
	{
		G_SoundOnEnt( self, CHAN_LESS_ATTEN, "sound/chars/atst/atst_damaged1" );
	}
	else
	{
		G_SoundOnEnt( self, CHAN_LESS_ATTEN, "sound/chars/atst/atst_damaged2" );
	}

	/*
	if ((hitLoc==HL_ARM_LT) && (self->locationDamage[HL_ARM_LT] > LEFT_ARM_HEALTH))
	{
		if (self->locationDamage[hitLoc] >= LEFT_ARM_HEALTH)	// Blow it up?
		{
			newBolt = trap->G2API_AddBolt( self->ghoul2, 0, "*flash3" );
			if ( newBolt != -1 )
			{
//				G_PlayEffect( "small_chunks", self->playerModel, self->genericBolt1, self->s.number);
				ATST_PlayEffect( self, trap->G2API_AddBolt(self->ghoul2, 0, "*flash3"), "env/med_explode2" );
				//G_PlayEffectID( G_EffectIndex("blaster/smoke_bolton"), self->playerModel, newBolt, self->s.number);
				//Maybe bother with this some other time.
			}

			NPC_SetSurfaceOnOff( self, "head_light_blaster_cann", TURN_OFF );
		}
	}
	else if ((hitLoc==HL_ARM_RT) && (self->locationDamage[HL_ARM_RT] > RIGHT_ARM_HEALTH))	// Blow it up?
	{
		if (self->locationDamage[hitLoc] >= RIGHT_ARM_HEALTH)
		{
			newBolt = trap->G2API_AddBolt( self->ghoul2, 0, "*flash4" );
			if ( newBolt != -1 )
			{
//				G_PlayEffect( "small_chunks", self->playerModel, self->genericBolt2, self->s.number);
				ATST_PlayEffect( self, trap->G2API_AddBolt(self->ghoul2, 0, "*flash4"), "env/med_explode2" );
				//G_PlayEffect( "blaster/smoke_bolton", self->playerModel, newBolt, self->s.number);
			}

			NPC_SetSurfaceOnOff( self, "head_concussion_charger", TURN_OFF );
		}
	}
	*/
}
/*
-------------------------
NPC_ATST_Pain
-------------------------
*/
void NPC_ATST_Pain(gentity_t *self, gentity_t *attacker, int damage)
{
	G_ATSTCheckPain( self, attacker, damage );
	NPC_Pain( self, attacker, damage );
}

/*
-------------------------
ATST_Fire

VisionJK: the guns of the AT-ST of single player (wp_atst.cpp of code/), which multiplayer never had: the twin
cannons under its head in turn, the light blaster on its left side, the concussion launcher on its right. Shots at
what it aims at, from where its model has the muzzle, with the speeds and the damage single player gives an NPC (normal
skill). They are the missiles of a turret (g_turret.c): their trail and their impact are effects the shot names, so
every client draws them, also one that knows nothing of this.
-------------------------
*/
typedef enum {
	ATST_GUN_MAIN,
	ATST_GUN_SIDE,
	ATST_GUN_ROCKET
} atstGun_t;

typedef struct {
	const char		*bolt[2];
	const char		*flash, *shot, *impact;
	float			speed, size;
	int				damage, splashDamage, splashRadius;
	meansOfDeath_t	mod, splashMod;
} atstGunInfo_t;

static const atstGunInfo_t atstGuns[] = {
	{ { "*flash1", "*flash2" }, "atst/main_muzzleflash", "atst/shot", "atst/wall_impact", 4000.0f, 3.0f, 25, 0, 0, MOD_TARGET_LASER, MOD_TARGET_LASER },
	{ { "*flash3", "*flash3" }, "atst/alt_muzzleflash", "atst/side_main_shot", "atst/side_main_impact", 1300.0f, 4.0f, 40, 10, 16, MOD_TARGET_LASER, MOD_TARGET_LASER },
	{ { "*flash4", "*flash4" }, "atst/alt_muzzleflash", "atst/side_alt_shot", "atst/side_alt_explosion", 600.0f, 5.0f, 50, 65, 200, MOD_ROCKET, MOD_ROCKET_SPLASH },
};

static int atstBarrel[MAX_GENTITIES];
float G_ATSTLegsYaw( gentity_t *ent );

static void ATST_Fire( gentity_t *self, atstGun_t gun )
{
	const atstGunInfo_t	*g = &atstGuns[gun];
	const int			bolt = trap->G2API_AddBolt( self->ghoul2, 0, g->bolt[atstBarrel[self->s.number] & 1] );
	gentity_t			*enemy = self->enemy, *missile;
	vec3_t				muzzle, target, dir, angles;
	mdxaBone_t			boltMatrix;

	if ( !enemy || bolt < 0 )
	{
		return;
	}
	atstBarrel[self->s.number]++;
	VectorSet( angles, 0.0f, G_ATSTLegsYaw( self ), 0.0f );
	trap->G2API_GetBoltMatrix( self->ghoul2, 0, bolt, &boltMatrix, angles, self->r.currentOrigin, level.time, NULL, self->modelScale );
	BG_GiveMeVectorFromMatrix( &boltMatrix, ORIGIN, muzzle );

	// at the middle of the enemy, a little off by how well it aims (aim 1 to 5)
	VectorAdd( enemy->r.absmin, enemy->r.absmax, target );
	VectorScale( target, 0.5f, target );
	VectorSubtract( target, muzzle, dir );
	vectoangles( dir, angles );
	{
		const float spread = Com_Clamp( 0.0f, 4.0f, 5.0f - NPCS.NPCInfo->stats.aim ) * 0.5f;

		angles[PITCH] += Q_flrand( -spread, spread );
		angles[YAW] += Q_flrand( -spread, spread );
	}
	AngleVectors( angles, dir, NULL, NULL );

	G_PlayEffectID( G_EffectIndex( g->flash ), muzzle, dir );
	missile = CreateMissile( muzzle, dir, g->speed, 10000, self, (qboolean)( gun != ATST_GUN_MAIN ) );
	missile->classname = "atst_proj";
	missile->s.weapon = WP_EMPLACED_GUN;
	missile->s.otherEntityNum2 = G_EffectIndex( g->shot );
	missile->s.emplacedOwner = G_EffectIndex( g->impact );
	missile->damage = g->damage;
	missile->splashDamage = g->splashDamage;
	missile->splashRadius = g->splashRadius;
	missile->dflags = DAMAGE_DEATH_KNOCKBACK | DAMAGE_HEAVY_WEAP_CLASS;
	missile->methodOfDeath = g->mod;
	missile->splashMethodOfDeath = g->splashMod;
	missile->clipmask = MASK_SHOT | ( gun == ATST_GUN_ROCKET ? 0 : CONTENTS_LIGHTSABER );
	missile->bounceCount = 0;
	VectorSet( missile->r.maxs, g->size, g->size, g->size );
	VectorScale( missile->r.maxs, -1.0f, missile->r.mins );
}

/*
-------------------------
G_ATSTLegsYaw

VisionJK: where the legs of an AT-ST NPC face. Single player draws its legs after where it looks (CG_ATSTLegsYaw of
code/cgame/cg_players.cpp), here the game does it, so every client sees it: the yaw of the entity is its legs, and
its head turns on them by the bones it is sent (thoracic, cranium, G_G2NPCAngles of w_saber.c). Walking, the legs face
where it goes, up to 60 degrees from where it looks and at 300 degrees a second; standing they stay until it looks
more than 45 degrees aside, then the turn of the model plays and they come round during it.
-------------------------
*/
typedef struct {
	int		time;		// level.time it was worked out, 0 never
	float	legs;
	int		turnEnd;	// level.time the turn of the model that plays ends
} atstLegs_t;

static atstLegs_t atstLegs[MAX_GENTITIES];

float G_ATSTLegsYaw( gentity_t *ent )
{
	atstLegs_t		*l = &atstLegs[ent->s.number];
	playerState_t	*ps = &ent->client->ps;
	const float		view = ps->viewangles[YAW];
	const int		msec = level.time - l->time;
	float			diff, step;

	if ( l->time == level.time )
	{
		return l->legs;
	}
	if ( !l->time || msec < 0 || msec > 500 )
	{
		l->time = level.time;
		l->legs = AngleNormalize180( view );
		l->turnEnd = 0;
		return l->legs;
	}
	l->time = level.time;

	if ( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] > 10.0f * 10.0f )
	{
		float swing = AngleSubtract( vectoyaw( ps->velocity ), view );

		swing = fabs( swing ) > 150.0f ? 0.0f : Com_Clamp( -60.0f, 60.0f, swing );		// (backwards: walks back)
		diff = AngleSubtract( view + swing, l->legs );
		step = 300.0f * msec * 0.001f;
		l->turnEnd = 0;
	}
	else
	{
		diff = AngleSubtract( view, l->legs );
		if ( l->turnEnd <= level.time )
		{
			l->turnEnd = 0;
			if ( fabs( diff ) <= 45.0f )
			{
				return l->legs;
			}
			NPC_SetAnim( ent, SETANIM_LEGS, diff > 0.0f ? BOTH_TURN_LEFT1 : BOTH_TURN_RIGHT1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
			l->turnEnd = level.time + ( ps->legsTimer > 0 ? ps->legsTimer : 1800 );
		}
		// round during the turn, the rest of the way in what is left of it
		step = fabs( diff ) * Com_Clamp( 0.0f, 1.0f, msec / (float)Q_max( l->turnEnd - level.time, 50 ) );
	}
	// (never further from where it looks than its head turns)
	if ( fabs( diff ) > 90.0f )
	{
		l->legs += diff > 0.0f ? diff - 90.0f : diff + 90.0f;
		diff = diff > 0.0f ? 90.0f : -90.0f;
	}
	l->legs = AngleNormalize180( l->legs + Com_Clamp( -step, step, diff ) );
	return l->legs;
}

/*
-------------------------
G_ATSTFrame

VisionJK: after the move of an AT-ST NPC (ClientThink_real): the yaw it is sent is the one of its legs
(G_ATSTLegsYaw), and as in single player it crushes what it stands on (g_active.cpp of code/).
-------------------------
*/
void G_ATSTFrame( gentity_t *ent )
{
	ent->s.apos.trBase[YAW] = G_ATSTLegsYaw( ent );
	if ( ent->health > 0 && ent->client->ps.groundEntityNum < ENTITYNUM_WORLD )
	{
		gentity_t *under = &g_entities[ent->client->ps.groundEntityNum];

		if ( under->inuse && under->health > 0 && under->takedamage )
		{
			vec3_t down = { 0.0f, 0.0f, -1.0f };

			G_Damage( under, ent, ent, down, under->r.currentOrigin, 100, 0, MOD_CRUSH );
		}
	}
}

/*
-------------------------
ATST_Hunt
-------------------------`
*/
void ATST_Hunt( qboolean visible, qboolean advance )
{

	if ( NPCS.NPCInfo->goalEntity == NULL )
	{//hunt
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
	}

	NPCS.NPCInfo->combatMove = qtrue;

	NPC_MoveToGoal( qtrue );

}

/*
-------------------------
ATST_Ranged
-------------------------
*/
void ATST_Ranged( qboolean visible, qboolean advance, int gun )
{

	if ( TIMER_Done( NPCS.NPC, "atkDelay" ) && visible && gun >= 0 )	// Attack?
	{
		TIMER_Set( NPCS.NPC, "atkDelay", Q_irand( 500, 3000 ) );
		// VisionJK: its guns fire themselves (ATST_Fire); its weapon of multiplayer is none
		ATST_Fire( NPCS.NPC, (atstGun_t)gun );
	}

	// VisionJK: up to MIN_DISTANCE, not onto its enemy
	if ( ( NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES ) && advance )
	{
		ATST_Hunt( visible, advance );
	}
}

/*
-------------------------
ATST_Attack
-------------------------
*/
void ATST_Attack( void )
{
	qboolean	altAttack=qfalse;
	int			blasterTest,chargerTest,weapon;
	int			gun = ATST_GUN_MAIN;
	float		distance;
	distance_e	distRate;
	qboolean	visible;
	qboolean	advance;

	if ( NPC_CheckEnemyExt(qfalse) == qfalse )//!NPC->enemy )//
	{
		NPCS.NPC->enemy = NULL;
		return;
	}

	NPC_FaceEnemy( qtrue );

	// Rate our distance to the target, and our visibilty
	distance	= (int) DistanceHorizontalSquared( NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin );
	distRate	= ( distance > MIN_MELEE_RANGE_SQR ) ? DIST_LONG : DIST_MELEE;
	visible		= NPC_ClearLOS4( NPCS.NPC->enemy );
	advance		= (qboolean)(distance > MIN_DISTANCE_SQR);

	// If we cannot see our target, move to see it
	if ( visible == qfalse )
	{
		if ( NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES )
		{
			ATST_Hunt( visible, advance );
			return;
		}
	}

	// Decide what type of attack to do
	switch ( distRate )
	{
	case DIST_MELEE:
//		NPC_ChangeWeapon( WP_ATST_MAIN );
		break;

	case DIST_LONG:

//		NPC_ChangeWeapon( WP_ATST_SIDE );
		//rwwFIXMEFIXME: make atst weaps work.

		// See if the side weapons are there
		blasterTest = trap->G2API_GetSurfaceRenderStatus( NPCS.NPC->ghoul2, 0, "head_light_blaster_cann" );
		chargerTest = trap->G2API_GetSurfaceRenderStatus( NPCS.NPC->ghoul2, 0, "head_concussion_charger" );

		// It has both side weapons
		if ( blasterTest != -1
			&& !(blasterTest&TURN_OFF)
			&& chargerTest != -1
			&& !(chargerTest&TURN_OFF))
		{
			weapon = Q_irand( 0, 1);	// 0 is blaster, 1 is charger (ALT SIDE)

			if (weapon)				// Fire charger
			{
				altAttack = qtrue;
			}
			else
			{
				altAttack = qfalse;
			}

		}
		else if (blasterTest != -1
			&& !(blasterTest & TURN_OFF))	// Blaster is on
		{
			altAttack = qfalse;
		}
		else if (chargerTest != -1
			&&!(chargerTest & TURN_OFF))	// Blaster is on
		{
			altAttack = qtrue;
		}
		else
		{
			NPC_ChangeWeapon( WP_NONE );
			gun = -1;
		}
		if ( gun >= 0 )
		{
			gun = altAttack ? ATST_GUN_ROCKET : ATST_GUN_SIDE;
		}
		break;
	}

	NPC_FaceEnemy( qtrue );

	ATST_Ranged( visible, advance, gun );
}

/*
-------------------------
ATST_Patrol
-------------------------
*/
void ATST_Patrol( void )
{
	if ( NPC_CheckPlayerTeamStealth() )
	{
		NPC_UpdateAngles( qtrue, qtrue );
		return;
	}

	//If we have somewhere to go, then do that
	if (!NPCS.NPC->enemy)
	{
		if ( UpdateGoal() )
		{
			NPCS.ucmd.buttons |= BUTTON_WALKING;
			NPC_MoveToGoal( qtrue );
			NPC_UpdateAngles( qtrue, qtrue );
		}
	}

}

/*
-------------------------
ATST_Idle
-------------------------
*/
void ATST_Idle( void )
{

	NPC_BSIdle();

	NPC_SetAnim( NPCS.NPC, SETANIM_BOTH, BOTH_STAND1, SETANIM_FLAG_NORMAL );
}

/*
-------------------------
NPC_BSDroid_Default
-------------------------
*/
void NPC_BSATST_Default( void )
{
	if ( NPCS.NPC->enemy )
	{
		if( (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES) )
		{
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		}
		ATST_Attack();
	}
	else if ( NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES )
	{
		ATST_Patrol();
	}
	else
	{
		ATST_Idle();
	}
}
