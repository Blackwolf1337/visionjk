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

// cg_jpplayer.c -- what the JA+ client plugin does to a player that cg_players.c draws: the other dimension, fading
// players out of the way, holstered sabers, the effects of force powers, the look of duellists. Not from OpenJK.

#include "cg_local.h"
#include "cgame/saber/cg_saberbelt.h"
#include "ghoul2/G2.h"

/*
=================
CG_JPOtherDimension

JA+: the player is in the other alternate dimension than the local player (FFA only)
=================
*/
qboolean CG_JPOtherDimension( int clientNum )
{
	const playerState_t *ps = &cg.snap->ps;

	if ( !JP_FEATURE( JPF_ALTDIM ) || cgs.gametype != GT_FFA || clientNum < 0 || clientNum >= MAX_CLIENTS || clientNum == ps->clientNum )
		return qfalse;
	return (qboolean)( ((ps->eFlags & EF_ALT_DIM) != 0) != ((cg_entities[clientNum].currentState.eFlags & EF_ALT_DIM) != 0) );
}

/*
=================
CG_JPFadePlayer

JA+: fades a player down to minAlpha the way a mind trick does. qtrue when he has faded out and is not drawn
=================
*/
qboolean CG_JPFadePlayer( centity_t *cent, int minAlpha )
{
	if ( cent->trickAlpha > 1 )
	{
		cent->trickAlpha -= (cg.time - cent->trickAlphaTime)*0.5;
		cent->trickAlphaTime = cg.time;

		if ( cent->trickAlpha < minAlpha )
		{
			cent->trickAlpha = minAlpha;
		}
		return qfalse;
	}

	cent->trickAlpha = minAlpha > 1 ? minAlpha : 1;
	cent->trickAlphaTime = cg.time;
	return qtrue;
}

/*
=================
CG_JPMinAlpha

JA+: the alpha the sabers on the body of a player are drawn with: jp_DuelAlpha for a player of another duel (40 while
the local player duels, which is only seen with Plugin_IgnoreAllPlayers_WhenInDuel turned off), cp_altDimAlpha for a
player of the other dimension
=================
*/
static int CG_JPMinAlpha( const centity_t *cent )
{
	if ( CG_JPOtherDuel( cent->currentState.number ) )
		return cg.snap->ps.duelInProgress ? 40 : cgs.jpDuelAlpha;
	if ( CG_JPOtherDimension( cent->currentState.number ) )
		return cp_altDimAlpha.integer;
	return 255;
}

/*
=================
JA+ client plugin: sabers carried on the body (Plugin_holsteredSaberBolted, cp_pluginDisable bit 9 turns it off)

A saber that is not in use hangs on the player: on the right hip (the second one on the left hip), a staff
or a saber with the saber key idleInBack on the back. Sabers with idleInBack also stay there while the saber
is the weapon but switched off, and come to the hand with the draw animations of the new JA+ animations.
The holster model of the player (hm of the clientinfo, cp_holster) is shown for sabers with idleInBack 1.
Rebuilt from cgamex86.dll of the plugin 1.4B4 (end of CG_Player and FUN_3004e2a0).
=================
*/
typedef struct jpHolsterState_s {
	void		*saberG2[MAX_SABERS];			// copies of the hilts for the body
	char		saberModel[MAX_SABERS][MAX_QPATH];
	qhandle_t	saberSkin[MAX_SABERS];
	qboolean	saberOffHand;					// the first saber was taken out of the hand
	int			drawAnim, drawAnimTime;			// the running draw or put away animation and when it started
} jpHolsterState_t;

static jpHolsterState_t jpHolsterState[MAX_CLIENTS];

#define JP_MAX_HOLSTER_MODELS	16

typedef struct jpHolsterModel_s {
	int		id;			// cp_holster of a player, 0: free
	void	*ghoul2;	// NULL if the model could not be loaded
} jpHolsterModel_t;

static jpHolsterModel_t jpHolsterModels[JP_MAX_HOLSTER_MODELS];

void CG_JPHolsterShutdown( void )
{
	int i, j;

	for ( i = 0; i < JP_MAX_HOLSTER_MODELS; i++ )
	{
		if ( jpHolsterModels[i].ghoul2 )
			trap->G2API_CleanGhoul2Models( &jpHolsterModels[i].ghoul2 );
	}
	memset( jpHolsterModels, 0, sizeof( jpHolsterModels ) );

	for ( i = 0; i < MAX_CLIENTS; i++ )
	{
		for ( j = 0; j < MAX_SABERS; j++ )
		{
			if ( jpHolsterState[i].saberG2[j] )
				trap->G2API_CleanGhoul2Models( &jpHolsterState[i].saberG2[j] );
		}
	}
	memset( jpHolsterState, 0, sizeof( jpHolsterState ) );
}

static qboolean CG_JPHolsterClient( const centity_t *cent )
{
	return cent->currentState.eType != ET_NPC && cent->currentState.number >= 0 && cent->currentState.number < MAX_CLIENTS;
}

// remembers when one of the draw or put away animations started
void CG_JPHolsterTrackAnim( const centity_t *cent, const clientInfo_t *ci )
{
	jpHolsterState_t *hs;
	const int anim = cent->currentState.torsoAnim;

	if ( !CG_JPHolsterClient( cent ) || !ci->infoValid )
		return;
	hs = &jpHolsterState[cent->currentState.number];

	if ( anim < BOTH_STAND1TO2_NEW || anim > BOTH_S1_S6_2ARMS || hs->drawAnim == anim )
	{
		if ( hs->drawAnim != anim )
			hs->drawAnim = 0;
	}
	else
	{
		hs->drawAnimTime = cg.time;
		hs->drawAnim = anim;
	}
}

// a staff that hangs on the back is not put back into the hand by the saber code
qboolean CG_JPStaffOffHand( const centity_t *cent, const clientInfo_t *ci )
{
	return JP_FEATURE( JPF_NEWGLAANIMS ) && CG_JPHolsterClient( cent ) && !(cent->currentState.eFlags & EF_DEAD)
		&& cent->currentState.NPC_class != CLASS_VEHICLE && ci->team != TEAM_SPECTATOR
		&& ci->saber[0].type == SABER_STAFF && jpHolsterState[cent->currentState.number].saberOffHand;
}

qboolean CG_JPSaberOffHand( const centity_t *cent )
{
	return CG_JPHolsterClient( cent ) && jpHolsterState[cent->currentState.number].saberOffHand;
}

// The copy of the hilt for the body, made new when the saber of the player changed. NULL if the hilt cannot be
// loaded. An instance takes one of the 1024 slots of the engine even if its model could not be loaded, and
// G2_HaveWeGhoul2Models() is false for it: it has to be given back all the same, and the model is tried once for
// a saber and not again with every frame, or the client is out of slots within a second.
static void *CG_JPHolsterSaberModel( jpHolsterState_t *hs, const clientInfo_t *ci, int saberNum )
{
	const saberInfo_t *saber = &ci->saber[saberNum];

	if ( strcmp( hs->saberModel[saberNum], saber->model ) || hs->saberSkin[saberNum] != saber->skin )
	{
		if ( hs->saberG2[saberNum] )
			trap->G2API_CleanGhoul2Models( &hs->saberG2[saberNum] );
		Q_strncpyz( hs->saberModel[saberNum], saber->model, sizeof( hs->saberModel[saberNum] ) );
		hs->saberSkin[saberNum] = saber->skin;
		if ( saber->model[0] )
		{
			trap->G2API_InitGhoul2Model( &hs->saberG2[saberNum], saber->model, 0, saber->skin, 0, 0, 0 );
			if ( hs->saberG2[saberNum] && !trap->G2_HaveWeGhoul2Models( hs->saberG2[saberNum] ) )
				trap->G2API_CleanGhoul2Models( &hs->saberG2[saberNum] );
			if ( hs->saberG2[saberNum] && saber->skin )
				trap->G2API_SetSkin( hs->saberG2[saberNum], 0, saber->skin, saber->skin );
		}
	}
	return hs->saberG2[saberNum];
}

static void CG_JPDrawHolsteredSaber( centity_t *cent, clientInfo_t *ci, int saberNum, qboolean onHip )
{
	jpHolsterState_t *hs = &jpHolsterState[cent->currentState.number];
	const playerState_t *ps = &cg.snap->ps;
	const int num = cent->currentState.number;
	refEntity_t re;
	mdxaBone_t matrix;
	matrix3_t offset, boltAxis;
	vec3_t angles = { 0, 0, 0 };
	float scale = 1.0f;
	int bolt, alpha;

	if ( num == ps->clientNum && !cg.renderingThirdPerson )
		return;
	bolt = !onHip ? ci->bolt_back : (saberNum == 1 ? ci->bolt_hipL : ci->bolt_hipR);
	if ( bolt <= 0 || !CG_JPHolsterSaberModel( hs, ci, saberNum ) )
		return;

	// in a private duel nothing of the players outside of it
	if ( num != ps->clientNum && cgs.jpDuelAlpha >= 0 && ps->duelInProgress && !(cp_pluginDisable.integer & (1 << 1)) && ps->duelIndex != num )
		return;
	alpha = CG_JPMinAlpha( cent );
	if ( alpha <= 0 )
		return;

	if ( cent->currentState.iModelScale > 0 )
		scale = cent->currentState.iModelScale / 100.0f;

	memset( &re, 0, sizeof( re ) );
	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &matrix, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
	BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, re.origin );
	BG_GiveMeVectorFromMatrix( &matrix, POSITIVE_X, boltAxis[0] );
	BG_GiveMeVectorFromMatrix( &matrix, POSITIVE_Y, boltAxis[1] );
	BG_GiveMeVectorFromMatrix( &matrix, POSITIVE_Z, boltAxis[2] );
	if ( !onHip )
	{ // across the back, the second saber the other way round
		angles[ROLL] = saberNum == 1 ? -150.0f : 150.0f;
	}
	else
	{ // a bit away from the body
		VectorMA( re.origin, scale * -5.0f, boltAxis[2], re.origin );
		CG_SaberBeltSwing( cent, saberNum, re.origin, boltAxis, angles );
	}
	AnglesToAxis( angles, offset );
	MatrixMultiply( offset, boltAxis, re.axis );

	re.reType = RT_MODEL;
	re.ghoul2 = hs->saberG2[saberNum];
	VectorSet( re.modelScale, scale, scale, scale );
	ScaleModelAxis( &re );

	if ( alpha < 255 )
	{
		re.renderfx |= RF_FORCE_ENT_ALPHA;
		re.shaderRGBA[3] = alpha;
	}
	else if ( (cent->currentState.powerups & (1 << PW_CLOAKED))
		&& (!(ps->fd.forcePowersActive & (1 << FP_SEE)) || ps->clientNum == num) )
	{ // cloaked like the player
		if ( cgs.glconfig.stencilBits >= 4 && cg_renderToTextureFX.integer )
		{
			re.customShader = 2;
		}
		else
		{
			re.renderfx = 0;
			re.shaderRGBA[0] = re.shaderRGBA[1] = re.shaderRGBA[2] = re.shaderRGBA[3] = 255;
			re.customShader = cgs.media.cloakedShader;
		}
	}
	trap->R_AddRefEntityToScene( &re );
}

#define JP_HOLSTER_BOLT		8	// bolt of the humanoid skeleton the holster model is fixed to without a "*back" tag
#define JP_HOLSTER_G2INDEX	4	// model index of the holster in the Ghoul2 instance of a player, as in the plugin
								// (1 and 2 are the sabers or the weapon, 3 is the jetpack)

// the Ghoul2 instance of holster model <id> the players get their copy from, model 1 if there is no such model
static void *CG_JPHolsterG2( int id )
{
	jpHolsterModel_t *hm;
	int i;

	for ( i = 0, hm = jpHolsterModels; i < JP_MAX_HOLSTER_MODELS; i++, hm++ )
	{
		if ( hm->id == id )
			return hm->ghoul2;
		if ( !hm->id )
			break;
	}
	if ( i == JP_MAX_HOLSTER_MODELS )
		return NULL;

	hm->id = id;
	trap->G2API_InitGhoul2Model( &hm->ghoul2, va( "models/weapons2/holster/model%i.glm", id ), 0, 0, 0, 0, 0 );
	if ( !hm->ghoul2 || !trap->G2_HaveWeGhoul2Models( hm->ghoul2 ) )
		trap->G2API_InitGhoul2Model( &hm->ghoul2, "models/weapons2/holster/model1.glm", 0, 0, 0, 0, 0 );
	if ( hm->ghoul2 && !trap->G2_HaveWeGhoul2Models( hm->ghoul2 ) )
		trap->G2API_CleanGhoul2Models( &hm->ghoul2 );	// (it has a slot of the engine without a model too)
	return hm->ghoul2;
}

// the holster model on the back, for sabers that are drawn from the back
static void CG_JPHolsterModel( centity_t *cent, const clientInfo_t *ci, qboolean want )
{
	qboolean has = trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, JP_HOLSTER_G2INDEX );

	if ( has && cent->jpHolsterId != ci->holsterLoaded )
	{ // the player chose another holster
		trap->G2API_RemoveGhoul2Model( &cent->ghoul2, JP_HOLSTER_G2INDEX );
		has = qfalse;
	}

	want = want && ci->holsterLoaded > 0 && !(cp_pluginDisable.integer & (1 << 9));
	if ( want && !has )
	{
		void *holster = CG_JPHolsterG2( ci->holsterLoaded );

		if ( holster )
		{
			trap->G2API_CopySpecificGhoul2Model( holster, 0, cent->ghoul2, JP_HOLSTER_G2INDEX );
			trap->G2API_SetBoltInfo( cent->ghoul2, JP_HOLSTER_G2INDEX, ci->bolt_back > 0 ? ci->bolt_back : JP_HOLSTER_BOLT );
			cent->jpHolsterId = ci->holsterLoaded;
		}
	}
	else if ( !want && has )
	{
		trap->G2API_RemoveGhoul2Model( &cent->ghoul2, JP_HOLSTER_G2INDEX );
	}
}

// called at the end of CG_Player
void CG_JPHolster( centity_t *cent, clientInfo_t *ci )
{
	jpHolsterState_t *hs;
	const entityState_t *es = &cent->currentState;
	const int anim = es->torsoAnim;
	const qboolean saberOut = es->weapon == WP_SABER;
	qboolean has[MAX_SABERS], inBack[MAX_SABERS], onBody[MAX_SABERS] = { qfalse, qfalse };
	qboolean showHolster = qfalse;
	int sinceAnim, i;

	if ( !ci || !ci->infoValid || !CG_JPHolsterClient( cent ) )
		return;
	hs = &jpHolsterState[es->number];

	if ( !cent->ghoul2 || (es->eFlags & EF_DEAD) || es->NPC_class == CLASS_VEHICLE || ci->team == TEAM_SPECTATOR
		|| (es->eFlags & EF_NOT_USED_1) )	// (the server sets this flag for players without a saber)
	{
		if ( (es->eFlags & EF_DEAD) || ci->team == TEAM_SPECTATOR )
			hs->saberOffHand = qfalse;
		return;
	}

	CG_SaberBeltUpdate( cent, ci );	// VisionJK: a switched off saber on the belt (saber/cg_saberbelt.c)
	sinceAnim = cg.time - hs->drawAnimTime;
	for ( i = 0; i < MAX_SABERS; i++ )
	{
		has[i] = ci->saber[i].model[0] && ci->ghoul2Weapons[i];
		inBack[i] = (ci->saber[i].saberFlags2 & SFL2_IDLE_IN_BACK) != 0;
	}

	// the holster model: one of the sabers has idleInBack 1
	if ( has[0] )
	{
		const qboolean first = inBack[0] && !(ci->saber[0].saberFlags2 & SFL2_IDLE_IN_BACK2);
		const qboolean second = has[1] && inBack[1] && !(ci->saber[1].saberFlags2 & SFL2_IDLE_IN_BACK2);

		if ( !has[1] )
			showHolster = first;
		else if ( inBack[0] && inBack[1] && ((ci->saber[0].saberFlags2 & SFL2_IDLE_IN_BACK2) || (ci->saber[1].saberFlags2 & SFL2_IDLE_IN_BACK2)) )
			showHolster = qfalse;
		else
			showHolster = first || second;
	}
	CG_JPHolsterModel( cent, ci, showHolster );

	// which saber hangs on the body
	if ( !has[0] || !has[1] )
	{
		onBody[0] = has[0] && !es->saberInFlight
			&& !(saberOut && es->saberHolstered != 2 && BG_SI_Length( &ci->saber[0] ) > 0.0f);
	}
	else if ( !saberOut )
	{
		onBody[0] = onBody[1] = qtrue;
	}
	else if ( es->saberHolstered == 2 )
	{
		onBody[0] = !es->saberInFlight;
		onBody[1] = qtrue;
	}
	else if ( es->saberHolstered == 1 )
	{
		onBody[1] = qtrue;
	}

	// the draw and put away animations: the saber changes its place in the middle of them
	if ( anim != hs->drawAnim )
		sinceAnim = INT_MAX;	// (the timers below only count for the running animation)
	if ( onBody[0] )
	{
		if ( (anim == BOTH_STAND2TO1_NEW && sinceAnim < 670) || (anim == BOTH_S7_S1_NEW && sinceAnim < 550) )
			onBody[0] = qfalse;
	}
	else if ( (anim == BOTH_STAND1TO2_NEW && sinceAnim < 400) || (anim == BOTH_STAND2TO1_NEW && sinceAnim > 670)
		|| (anim == BOTH_S1_S7_NEW && sinceAnim < 500) || (anim == BOTH_S7_S1_NEW && sinceAnim > 550) )
	{
		onBody[0] = qtrue;
	}
	if ( onBody[1] )
	{
		if ( anim == BOTH_S6_S1_LARM && sinceAnim < 550 )
			onBody[1] = qfalse;
	}
	else if ( (anim == BOTH_S1_S6_LARM && sinceAnim < 400) || (anim == BOTH_S6_S1_LARM && sinceAnim > 550) )
	{
		onBody[1] = qtrue;
	}
	if ( onBody[0] && onBody[1] )
	{
		if ( anim == BOTH_S6_S1_2ARMS && sinceAnim < 600 )
			onBody[0] = onBody[1] = qfalse;
	}
	else if ( (anim == BOTH_S1_S6_2ARMS && sinceAnim < 400) || (anim == BOTH_S6_S1_2ARMS && sinceAnim > 600) )
	{
		onBody[0] = onBody[1] = qtrue;
	}

	if ( (cp_pluginDisable.integer & (1 << 9)) || (es->m_iVehicleNum >= MAX_CLIENTS && es->m_iVehicleNum < ENTITYNUM_WORLD) )
		onBody[0] = onBody[1] = qfalse;

	// first saber. A normal saber only hangs on the hip while another weapon is used
	if ( has[0] && onBody[0]
		&& (inBack[0] || !saberOut || CG_SaberBeltHolds( cent ) || (ci->saber[0].type == SABER_STAFF
			&& (anim == BOTH_S1_S7_NEW || anim == BOTH_S7_S1_NEW || anim == BOTH_STAND2TO1_NEW)))
		&& CG_JPHolsterSaberModel( hs, ci, 0 ) )
	{
		CG_JPDrawHolsteredSaber( cent, ci, 0, !(inBack[0] || ci->saber[0].type == SABER_STAFF) );
		if ( saberOut && trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, 1 ) )
		{
			hs->saberOffHand = qtrue;
			trap->G2API_RemoveGhoul2Model( &cent->ghoul2, 1 );
		}
	}
	else if ( saberOut && has[0] && !onBody[0] && !trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, 1 ) )
	{ // back into the hand
		if ( !es->saberInFlight )
			trap->G2API_CopySpecificGhoul2Model( ci->ghoul2Weapons[0], 0, cent->ghoul2, 1 );
		if ( ci->saber[0].type == SABER_STAFF && JP_FEATURE( JPF_NEWGLAANIMS ) && anim == BOTH_S1_S7_NEW )
			BG_SI_SetLength( &ci->saber[0], 0 );
		hs->saberOffHand = qfalse;
	}

	// second saber
	if ( has[1] && onBody[1] && (inBack[1] || !saberOut || CG_SaberBeltHolds( cent )) && CG_JPHolsterSaberModel( hs, ci, 1 ) )
	{
		CG_JPDrawHolsteredSaber( cent, ci, 1, !(inBack[1] || ci->saber[1].type == SABER_STAFF) );
		if ( saberOut && trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, 2 ) )
			trap->G2API_RemoveGhoul2Model( &cent->ghoul2, 2 );
	}
	else if ( saberOut && has[1] && !onBody[1] && !trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, 2 ) )
	{
		trap->G2API_CopySpecificGhoul2Model( ci->ghoul2Weapons[1], 0, cent->ghoul2, 2 );
	}

	if ( saberOut && trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, 1 ) )
		hs->saberOffHand = qfalse;
}

/*
=================
CG_JPForceFxVisible

JA+ client plugin: in a private duel the force effects of the players outside of it are not drawn (with the
duel isolation of the server, jp_DuelAlpha, and without Plugin_IgnoreAllPlayers_WhenInDuel)
=================
*/
qboolean CG_JPForceFxVisible( const centity_t *cent )
{
	const playerState_t *ps;

	if ( !cg.snap )
		return qtrue;
	ps = &cg.snap->ps;

	return ps->clientNum == cent->currentState.number || cgs.jpDuelAlpha < 0 || !ps->duelInProgress
		|| (cp_pluginDisable.integer & (1 << 1)) || ps->duelIndex == cent->currentState.number;
}

// half way between the feet and the head bolt
static void CG_JPBodyCenter( centity_t *cent, const clientInfo_t *ci, vec3_t out )
{
	mdxaBone_t matrix;
	vec3_t head;

	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_head, &matrix, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
	BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, head );
	VectorSubtract( head, cent->lerpOrigin, out );
	VectorMA( cent->lerpOrigin, 0.5f, out, out );
}

/*
=================
CG_JPForceEffects

JA+ client plugin: the fire on a player hit by the flamethrower (EF2_BURNING) and, with Plugin_NewForceEffect
(cp_pluginDisable bit 5 turns it off), the effects of the single player force animations and of the grabs
=================
*/
void CG_JPForceEffects( centity_t *cent, clientInfo_t *ci )
{
	static int nextTime[MAX_GENTITIES];
	const int anim = cent->currentState.torsoAnim;
	const int num = cent->currentState.number;
	mdxaBone_t matrix;
	matrix3_t axis;
	vec3_t fAng, org;

	if ( cent->currentState.NPC_class == CLASS_VEHICLE || !cent->ghoul2 || !ci || num < 0 || num >= MAX_GENTITIES
		|| !CG_JPForceFxVisible( cent ) )
		return;

	VectorSet( fAng, cent->pe.torso.pitchAngle, cent->pe.torso.yawAngle, 0 );
	AnglesToAxis( fAng, axis );

	if ( cent->currentState.eFlags2 & EF2_BURNING )
	{
		CG_JPBodyCenter( cent, ci, org );
		trap->FX_PlayEntityEffectID( cgs.effects.jpFire, org, axis, -1, -1, -1, -1 );
	}

	if ( cp_pluginDisable.integer & (1 << 5) )
		return;

	switch ( anim )
	{
	case BOTH_FORCE_RAGE:
	case BOTH_FORCEHEAL_START:
		if ( !ci->infoValid || nextTime[num] >= cg.time )
			break;
		nextTime[num] = cg.time + 150;
		CG_JPBodyCenter( cent, ci, org );
		trap->FX_PlayEntityEffectID( anim == BOTH_FORCE_RAGE ? cgs.effects.jpRageEffect : cgs.effects.jpHealEffect, org, axis, -1, -1, -1, -1 );
		break;

	case BOTH_FORCEHEAL_QUICK:
		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_lhand, &matrix, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, org );
		trap->FX_PlayEntityEffectID( cgs.effects.jpHealQuickEffect, org, axis, -1, -1, -1, -1 );
		break;

	case BOTH_NEW_STABEE:
		if ( !ci->infoValid || nextTime[num] >= cg.time )
			break;
		{
			vec3_t up = { 0, 0, 1 }, angles;
			float scale = 1.0f;

			nextTime[num] = cg.time + 150;
			trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_llumbar, &matrix, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
			BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, org );
			vectoangles( up, angles );
			AnglesToAxis( angles, axis );

			// the effect grows with the model, a bit less in width for big ones
			if ( cent->currentState.iModelScale > 0 )
				scale = cent->currentState.iModelScale / 100.0f;
			VectorScale( axis[0], scale, axis[0] );
			if ( scale > 1.4 )
				scale = scale / 1.3;
			VectorScale( axis[1], scale, axis[1] );
			VectorScale( axis[2], scale, axis[2] );
			trap->FX_PlayEntityEffectID( cgs.effects.jpStabee, org, axis, -1, -1, -1, -1 );
		}
		break;

	case BOTH_ALORA_SPIN_THROW:
		// both hands
		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_lhand, &matrix, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, org );
		trap->FX_PlayEntityEffectID( cgs.effects.jpKothosBeam, org, axis, -1, -1, -1, -1 );
		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_rhand, &matrix, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, org );
		trap->FX_PlayEntityEffectID( cgs.effects.jpKothosBeam, org, axis, -1, -1, -1, -1 );
		break;

	default:
		break;
	}
}

// v_duelEffect above 0 (from the VisionJK of 2019): a player in a duel you are not part of is drawn in the colour of
// v_duelColour ("r g b a forceAlpha", 0..255, forceAlpha 1 makes the alpha count) and, from 2 on, in a shader
// instead of his skin. 0 is the look of the game: dark with a blue bubble.
void CG_DuelLook( refEntity_t *ent )
{
	static int		modCount = -1;
	static byte		rgba[4];
	static qboolean	forceAlpha;
	int				c[5], i;

	if ( modCount != v_duelColour.modificationCount )
	{
		modCount = v_duelColour.modificationCount;
		if ( sscanf( v_duelColour.string, "%i %i %i %i %i", &c[0], &c[1], &c[2], &c[3], &c[4] ) != 5 )
		{
			c[0] = c[1] = c[2] = 50;
			c[3] = 100;
			c[4] = 0;
		}
		for ( i = 0; i < 4; i++ )
		{
			rgba[i] = (byte)Com_Clampi( 0, 255, c[i] );
		}
		forceAlpha = (qboolean)( c[4] != 0 );
	}

	for ( i = 0; i < 4; i++ )
	{
		ent->shaderRGBA[i] = rgba[i];
	}
	ent->renderfx |= RF_RGB_TINT;
	if ( forceAlpha )
	{
		ent->renderfx |= RF_FORCE_ENT_ALPHA;
	}

	switch ( v_duelEffect.integer )
	{
		case 2:		ent->customShader = cgs.media.protectShader;		break;
		case 3:		ent->customShader = cgs.media.playerShieldDamage;	break;
		case 4:		ent->customShader = cgs.media.forceShell;			break;
		case 5:		ent->customShader = cgs.media.sightShell;			break;
		case 6:		ent->customShader = cgs.media.halfShieldShader;		break;
		case 7:		ent->customShader = cgs.media.cloakedShader;		break;
		case 8:		ent->customShader = cgs.media.forceSightBubble;		break;
		default:	break;
	}
}
