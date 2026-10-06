/*
JA+ player commands: emotes, amflip, amknockmedown, amdropsaber, jetpack.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

extern qboolean G_InGetUpAnim( playerState_t *ps );
extern qboolean saberKnockOutOfHand( gentity_t *saberent, gentity_t *saberOwner, vec3_t velocity );
extern qboolean PM_InOnGroundAnim( int anim );
extern qboolean BG_SaberInTransitionAny( int move );
extern qboolean BG_SabersOff( playerState_t *ps );

#define JP_GLA_PLUGIN_VERSION	1.3f	// plugin version that knows the new GLA animations
#define JP_PLUGINDISABLE_GLA	9		// bit of cp_pluginDisable that turns them off

// the player is alive, playing and not busy with a special move
static qboolean JP_CanAct( const gentity_t *ent ) {
	const gclient_t *client = ent->client;
	const playerState_t *ps;

	if ( !ent->inuse || !client || client->pers.connected != CON_CONNECTED || client->sess.sessionTeam == TEAM_SPECTATOR )
		return qfalse;

	ps = &client->ps;
	return !BG_InGrappleMove( ps->legsAnim ) && !BG_InGrappleMove( ps->torsoAnim )
		&& !BG_SaberInAttack( ps->saberMove ) && !BG_SaberInTransitionAny( ps->saberMove )
		&& !client->pers.jp.sleeping && !client->pers.jp.down
		&& ps->pm_type != PM_DEAD && ent->health > 0 && ps->stats[STAT_HEALTH] > 0 && !(ps->eFlags & EF_DEAD);
}

// jp_newGLAAnims: 1 the animations are used by everybody with the plugin, 2 only if everybody has it
static qboolean JP_NewGLAAnimsUsable( const gentity_t *ent ) {
	return JP_FEATURE( JPF_NEWGLAANIMS )
		&& ent->client->pers.jp.pluginVersion >= JP_GLA_PLUGIN_VERSION
		&& !(ent->client->pers.jp.pluginDisable & (1 << JP_PLUGINDISABLE_GLA));
}

/*
=================
emotes
=================
*/
void JP_EmoteExec( gentity_t *ent, int anim ) {
	ent->client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	ent->client->ps.forceDodgeAnim = anim;
	ent->client->ps.forceHandExtendTime = level.time + BG_AnimLength( ent->localAnimIndex, (animNumber_t)anim );
}

// emotes that only make sense without teams
static qboolean JP_EmoteNotForTeams( int anim ) {
	switch ( anim ) {
	case BOTH_SLEEP6START: case BOTH_SLEEP1: case BOTH_KNEES2: case TORSO_SURRENDER_START: case BOTH_STAND4:
	case BOTH_CROUCH3: case BOTH_HIT1: case BOTH_STAND5TOSIT3: case BOTH_SIT6: case BOTH_SIT7: case BOTH_STAND10:
	case BOTH_DEATH15: case BOTH_STAND5TOSTAND8:
		return qtrue;
	}
	return qfalse;
}

static void JP_Emote( gentity_t *ent, int anim, int bit ) {
	const gclient_t *client = ent->client;

	if ( !client || client->pers.connected != CON_CONNECTED )
		return;

	// (the answers come first, like in the original: also for a player who could not do the emote right now)
	if ( jp_emotDisallow.integer && ((1 << bit) & jp_emotDisallow.integer) ) {
		trap->SendServerCommand( ent-g_entities, "print \"this Emot is not allowed on this server!.\n\"" );
		return;
	}
	if ( g_gametype.integer >= GT_TEAM && JP_EmoteNotForTeams( anim ) ) {
		trap->SendServerCommand( ent-g_entities, "print \"this Emot is not compatible with this gametype!.\n\"" );
		return;
	}

	if ( !JP_CanAct( ent ) )
		return;

	if ( client->ps.forceHandExtend != HANDEXTEND_NONE || G_InGetUpAnim( (playerState_t *)&client->ps )
		|| PM_InOnGroundAnim( client->ps.legsAnim ) || PM_InOnGroundAnim( client->ps.torsoAnim )
		|| client->jetPackOn )
		return;

	JP_EmoteExec( ent, anim );
}

typedef struct jpEmote_s {
	const char	*name;
	int			anim;
	int			bit;	// bit of the emote in jp_emotDisallow
} jpEmote_t;

static const jpEmote_t jpEmotes[] = {
	{ "amkneel",		BOTH_CROUCH3,			1 },
	{ "amdie",			BOTH_HIT1,				2 },
	{ "amdie2",			BOTH_DEATH15,			3 },
	{ "amfinishinghim",	BOTH_SABERKILLER1,		4 },
	{ "ampower",		BOTH_FORCE_GETUP_F2,	5 },
	{ "amnod",			BOTH_HEADNOD,			6 },
	{ "amshake",		BOTH_HEADSHAKE,			7 },
	{ "amhiltThrow1",	BOTH_SABERTHROW1START,	8 },
	{ "amhiltThrow2",	BOTH_SABERTHROW2START,	9 },
	{ "amcomeon",		BOTH_FORCEGRIP3THROW,	10 },
	{ "amwon",			TORSO_HANDSIGNAL1,		11 },
	{ "amhello",		BOTH_SILENCEGESTURE1,	12 },
	{ "sleep",			BOTH_SLEEP1,			14 },
	{ "amatease",		BOTH_STAND4,			15 },
	{ "amsurrender",	TORSO_SURRENDER_START,	16 },
	{ "amsit",			BOTH_SLEEP6START,		17 },
	{ "amsit2",			BOTH_STAND5TOSIT3,		18 },
	{ "amsit3",			BOTH_SIT6,				19 },
	{ "amsit4",			BOTH_SIT7,				20 },
	{ "ambeg",			BOTH_KNEES2,			21 },
	{ "amwait",			BOTH_STAND10,			22 },
	{ "amhips",			BOTH_STAND5TOSTAND8,	23 },
	{ "amvictory",		BOTH_TAVION_SWORDPOWER,	24 },
	{ "amneo",			BOTH_FORCE_GETUP_B4,	25 },
	{ "ambreakdance",	BOTH_FORCE_GETUP_B5,	26 },
	{ "ambreakdance2",	BOTH_FORCE_GETUP_B6,	27 },
	{ "amnoisy",		BOTH_SONICPAIN_HOLD,	28 },
};

/*
=================
amfinishinghim: holsters the saber, then plays the emote
=================
*/
static void Cmd_amfinishinghim_f( gentity_t *ent ) {
	const playerState_t *ps = &ent->client->ps;

	if ( ps->pm_type == PM_DEAD || ent->health < 1 || ps->stats[STAT_HEALTH] < 1 || (ps->eFlags & EF_DEAD) )
		return;

	if ( ps->weapon == WP_SABER && ps->saberEntityNum && !ps->saberInFlight && ps->saberHolstered )
		Cmd_ToggleSaber_f( ent );

	JP_Emote( ent, BOTH_SABERKILLER1, 4 );
}

/*
=================
amhug, amkiss: an emote for two, the other player is the one right in front
=================
*/
#define JP_EMOTE_BIT_HUG	13
#define JP_EMOTE_BIT_KISS	29

static void JP_ClearSaberMove( playerState_t *ps ) {
	ps->saberMove = LS_NONE;
	ps->saberBlocked = 0;
	ps->saberBlocking = 0;
}

static qboolean JP_SaberOut( const playerState_t *ps ) {
	return ps->weapon == WP_SABER && !ps->saberHolstered;
}

static qboolean JP_EmoteAlive( const gentity_t *ent ) {
	return ent->client->sess.sessionTeam != TEAM_SPECTATOR && ent->health > 0 && ent->client->ps.stats[STAT_HEALTH] > 0;
}

// his saber goes away now, in the middle of a swing too, where the command alone refuses
void JP_HolsterNow( gentity_t *ent ) {
	playerState_t *ps = &ent->client->ps;

	if ( !JP_SaberOut( ps ) || ps->saberInFlight )
		return;
	JP_ClearSaberMove( ps );
	ps->weaponTime = 0;
	Cmd_ToggleSaber_f( ent );
}

// puts the sabers of both away, qfalse if one of them stays out
qboolean JP_HolsterPair( gentity_t *ent, gentity_t *other ) {
	if ( JP_SaberOut( &ent->client->ps ) )
		Cmd_ToggleSaber_f( ent );
	if ( JP_SaberOut( &other->client->ps ) )
		Cmd_ToggleSaber_f( other );
	return ( JP_SaberOut( &ent->client->ps ) || JP_SaberOut( &other->client->ps ) ) ? qfalse : qtrue;
}

// the partner for amhug and amkiss within range units, NULL if the emote can not be done, both sabers get holstered
static gentity_t *JP_EmotePartner( gentity_t *ent, int bit, float range ) {
	gclient_t *client = ent->client;
	gentity_t *other;
	vec3_t fwd, end;
	trace_t tr;

	if ( !JP_EmoteAlive( ent ) )
		return NULL;
	if ( jp_emotDisallow.integer & (1 << bit) ) {
		trap->SendServerCommand( ent-g_entities, "print \"this Emot is not allowed on this server!.\n\"" );
		return NULL;
	}
	if ( level.gametype >= GT_TEAM ) {
		trap->SendServerCommand( ent-g_entities, "print \"this Emot is not compatible with this gametype!.\n\"" );
		return NULL;
	}
	if ( !JP_CanAct( ent ) || client->ps.forceHandExtend != HANDEXTEND_NONE || client->pers.jp.isGhost || client->jetPackOn )
		return NULL;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( client->ps.origin, range, fwd, end );
	trap->Trace( &tr, client->ps.origin, NULL, NULL, end, ent-g_entities, ent->clipmask, qfalse, 0, 0 );
	if ( tr.entityNum >= MAX_CLIENTS || tr.entityNum == ent-g_entities )
		return NULL;

	other = &g_entities[tr.entityNum];
	if ( !other->inuse || !other->client )
		return NULL;
	if ( BG_InGrappleMove( other->client->ps.legsAnim ) || BG_InGrappleMove( other->client->ps.torsoAnim ) )
		return NULL;
	if ( !G_PlInSameDim( ent, other ) || other->client->ps.duelInProgress || client->ps.duelInProgress )
		return NULL;

	return JP_HolsterPair( ent, other ) ? other : NULL;
}

/*
=================
ampoop: not from JA+. He sits down in the air, moans and leaves a small rock behind, see g_poop.c. An emote
like the others: jp_emotDisallow has a bit for it and the same rules decide whether he can do it now.
=================
*/
#define JP_EMOTE_BIT_POOP	30

void JP_EmotePoop( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !client || !JP_EmoteAlive( ent ) || client->ps.duelInProgress )
		return;
	// while he moves the legs would walk on and only the upper body would sit
	if ( client->ps.groundEntityNum == ENTITYNUM_NONE || VectorLengthSquared( client->ps.velocity ) > 20.0f * 20.0f ) {
		trap->SendServerCommand( ent-g_entities, "print \"Stand still for that.\n\"" );
		return;
	}

	JP_Emote( ent, BOTH_SIT3, JP_EMOTE_BIT_POOP );

	// JP_Emote let it through: he sits for as long as it takes, not only for the length of the animation
	if ( client->ps.forceHandExtend == HANDEXTEND_TAUNT && client->ps.forceDodgeAnim == BOTH_SIT3 )
		client->ps.forceHandExtendTime = level.time + G_PoopStart( ent );
}

/*
=================
JP_Hug

The two turn to each other and start the animations of amhug, ent is the one who hugs. parts is SETANIM_BOTH
for the emote. An admin who takes a player with +button13 carries him like this, with the animations on the
upper bodies only so the legs can walk, see Prop_Grab in g_props.c.
=================
*/
void JP_Hug( gentity_t *ent, gentity_t *other, int parts ) {
	gentity_t *pair[2];
	int i;

	pair[0] = ent;
	pair[1] = other;
	for ( i = 0; i < 2; i++ ) {
		gentity_t *self = pair[i], *partner = pair[!i];
		vec3_t dir, angles;

		VectorSubtract( partner->client->ps.origin, self->client->ps.origin, dir );
		VectorCopy( self->client->ps.viewangles, angles );
		angles[YAW] = vectoyaw( dir );
		SetClientViewAngle( self, angles );
		G_SetAnim( self, NULL, parts, i ? BOTH_HUGGEE1 : BOTH_HUGGER1, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD|SETANIM_FLAG_HOLDLESS, 0 );
		JP_ClearSaberMove( &self->client->ps );
	}
}

static void Cmd_amhug_f( gentity_t *ent ) {
	gentity_t *other = JP_EmotePartner( ent, JP_EMOTE_BIT_HUG, 40.0f );

	if ( other )
		JP_Hug( ent, other, SETANIM_BOTH );
}

static void Cmd_amkiss_f( gentity_t *ent ) {
	gentity_t *other, *pair[2];
	int i;

	if ( !JP_EmoteAlive( ent ) )
		return;
	if ( !jp_newGLAAnims.integer ) {
		trap->SendServerCommand( ent-g_entities, "print \"the new GLA animations are not loaded on this server or the admin disallowed them!.\n\"" );
		return;
	}
	if ( jp_newGLAAnims.integer == 2 && !JP_AllPlayersHavePlugin() ) {
		trap->SendServerCommand( ent-g_entities, "print \"You can't execute the new JA+ GLA animations because the admin decided that all players must have them thanks to the JA+ plugin!.\n\"" );
		return;
	}
	if ( !(other = JP_EmotePartner( ent, JP_EMOTE_BIT_KISS, 20.0f )) )
		return;

	pair[0] = ent;
	pair[1] = other;
	for ( i = 0; i < 2; i++ ) {
		gentity_t *self = pair[i], *partner = pair[!i];
		vec3_t dir, angles;

		VectorSubtract( partner->client->ps.origin, self->client->ps.origin, dir );
		dir[2] = 0.0f;
		vectoangles( dir, angles );
		SetClientViewAngle( self, angles );
		JP_EmoteExec( self, i ? BOTH_KISSEE : BOTH_KISSER );
		JP_ClearSaberMove( &self->client->ps );
	}
}

/*
=================
amflip: a showoff move depending on the saber style, the JA+ GLA animations for the cartwheel sabers
=================
*/
static void JP_FlipToggleSaber( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const qboolean turnOff = client->ps.saberHolstered == 0;

	// JA+ plays the switch on sound in both directions
	client->ps.saberHolstered = turnOff ? 2 : 0;
	if ( client->saber[0].soundOn )
		G_Sound( ent, CHAN_AUTO, client->saber[0].soundOn );
	if ( client->saber[1].soundOn )
		G_Sound( ent, CHAN_AUTO, client->saber[1].soundOn );
}

static void Cmd_amflip_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;
	const int style = ps->fd.saberAnimLevel, baseStyle = ps->fd.saberAnimLevelBase;
	const saberInfo_t *s1 = &client->saber[0], *s2 = &client->saber[1];
	// sabers that are carried on the back (saber key idleInBack) have their own draw and holster animations
	const qboolean n1 = s1->model[0] && (s1->saberFlags2 & SFL2_IDLE_IN_BACK), n2 = s2->model[0] && (s2->saberFlags2 & SFL2_IDLE_IN_BACK);
	const qboolean hasS2 = s1->model[0] && s2->model[0];
	int anim, holstered = ps->saberHolstered;

	if ( !JP_CanAct( ent ) || ps->forceHandExtend != HANDEXTEND_NONE || client->jetPackOn )
		return;
	if ( ps->weapon != WP_SABER || ps->saberInFlight )
		return;

	anim = BOTH_STAND1TO2;
	if ( style == SS_STAFF ) {
		anim = BOTH_SHOWOFF_STAFF;
		holstered = 0;
		if ( ps->saberHolstered ) {
			holstered = ps->saberHolstered;
			if ( Q_irand( 0, 10 ) >= 4 )
				anim = BOTH_S1_S7;
		}
	}
	if ( style == SS_DUAL )
		anim = BOTH_SHOWOFF_FAST;
	if ( baseStyle == SS_STAFF && style != SS_STAFF )
		anim = BOTH_S1_S7;
	if ( !holstered && style != SS_STAFF && style != SS_DUAL )
		anim = BOTH_STAND2TO1;

	if ( JP_NewGLAAnimsUsable( ent ) ) {
		if ( !ps->saberHolstered ) {
			if ( style == SS_STAFF && n1 )
				anim = BOTH_S7_S1_NEW;
			else if ( style == SS_DUAL && hasS2 ) {
				if ( n1 && n2 )			anim = BOTH_S6_S1_2ARMS;
				else if ( n1 && !n2 )	anim = BOTH_STAND2TO1_NEW;
				else if ( !n1 && n2 )	anim = BOTH_S6_S1_LARM;
			}
			else if ( baseStyle != SS_DUAL && baseStyle != SS_STAFF && n1 )
				anim = BOTH_STAND2TO1_NEW;
		}
		else if ( baseStyle == SS_STAFF && ps->saberHolstered == 2 && n1 )
			anim = BOTH_S1_S7_NEW;
		else if ( baseStyle == SS_DUAL ) {
			if ( hasS2 ) {
				if ( n1 && n2 && ps->saberHolstered == 2 )			anim = BOTH_S1_S6_2ARMS;
				else if ( n1 && !n2 && ps->saberHolstered == 2 )	anim = BOTH_STAND1TO2_NEW;
				else if ( !n1 && n2 )								anim = BOTH_S1_S6_LARM;
			}
		}
		else if ( n1 )
			anim = BOTH_STAND1TO2_NEW;
	}

	if ( ps->groundEntityNum == ENTITYNUM_NONE )
		return;

	if ( (ps->stats[STAT_WEAPONS] & (1 << WP_SABER)) && ps->weapon == WP_SABER && ps->saberEntityNum && !ps->saberInFlight )
		JP_FlipToggleSaber( ent );

	JP_EmoteExec( ent, anim );
}

/*
=================
amknockmedown: knocks the player down
=================
*/
static void Cmd_amknockmedown_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;

	if ( !ent->inuse || client->sess.sessionTeam == TEAM_SPECTATOR )
		return;

	if ( !jp_allowAmKnockmedown.integer || ps->duelInProgress || client->pers.jp.isGhost ) {
		trap->SendServerCommand( ent-g_entities, "amknockmedown is not allowed!\n" );
		return;
	}
	if ( g_gametype.integer != GT_FFA ) {
		trap->SendServerCommand( ent-g_entities, "print \"this is not compatible with this gametype!.\n\"" );
		return;
	}

	if ( !JP_CanAct( ent ) || ps->forceHandExtend != HANDEXTEND_NONE || client->jetPackOn )
		return;

	if ( level.time < ps->forceHandExtendTime )
		ps->forceHandExtendTime = level.time + 200;

	if ( BG_KnockDownable( ps ) && ps->forceHandExtendTime < level.time
		&& (ps->forceHandExtend != HANDEXTEND_KNOCKDOWN || level.time < ps->forceHandExtendTime + 200) ) {
		ps->forceHandExtend = HANDEXTEND_KNOCKDOWN;
		ps->forceDodgeAnim = 0;
		ps->velocity[2] = 400.0f;
		ps->forceHandExtendTime = level.time + 700;
		ps->quickerGetup = qfalse;
		G_PreDefSound( ps->origin, PDSOUND_FORCEJUMP );
	}
}

/*
=================
amdropsaber: throws the own saber away
=================
*/
static void Cmd_amdropsaber_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;

	if ( !ent->inuse || client->sess.sessionTeam == TEAM_SPECTATOR )
		return;
	if ( !jp_allowAmDropSaber.integer ) {
		trap->SendServerCommand( ent-g_entities, "amdropsaber is not allowed!\n" );
		return;
	}
	if ( g_gametype.integer != GT_FFA ) {
		trap->SendServerCommand( ent-g_entities, "print \"this is not compatible with this gametype!.\n\"" );
		return;
	}

	if ( JP_CanAct( ent ) && ps->weapon == WP_SABER && ps->saberEntityNum && !ps->saberInFlight )
		saberKnockOutOfHand( &g_entities[ps->saberEntityNum], ent, vec3_origin );
}

/*
=================
jetpack: takes or gives the jetpack item
=================
*/
static void Cmd_jetpack_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	playerState_t *ps = &client->ps;
	const int clientNum = ent-g_entities;

	if ( g_gametype.integer == GT_SIEGE || g_gametype.integer == GT_DUEL || g_gametype.integer == GT_POWERDUEL || g_gametype.integer == GT_CTF ) {
		trap->SendServerCommand( clientNum, "print \"Cmd jetpack not allowed\n\"" );
		return;
	}

	if ( ps->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_JETPACK) ) {
		if ( client->jetPackOn )
			return;
		ps->stats[STAT_HOLDABLE_ITEMS] &= ~(1 << HI_JETPACK);
		return;
	}

	if ( ps->groundEntityNum == ENTITYNUM_NONE )
		return;

	if ( !jp_allowJetpack.integer || ps->duelInProgress || (ps->eFlags & EF_ALT_DIM) ) {
		trap->SendServerCommand( clientNum, "print \"Cmd jetpack not allowed\n\"" );
		return;
	}
	if ( g_jediVmerc.integer && ps->trueNonJedi == 0 && ps->trueJedi ) {
		trap->SendServerCommand( clientNum, "print \"Cmd jetpack not allowed in JEdiVsMerc mod if you are a jedi\n\"" );
		return;
	}
	if ( g_gametype.integer == GT_TEAM && !g_jediVmerc.integer ) {
		trap->SendServerCommand( clientNum, "print \"Cmd jetpack not allowed in TEAM FFA if you are a JEdi. You have to be a merc.\n\"" );
		return;
	}

	ps->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
}

/*
=================
amcolor, sabercolor and saber: skin color, RGB saber colors and changing the sabers during the game
=================
*/
static void Cmd_amcolor_f( gentity_t *ent ) {
	char arg[3][MAX_TOKEN_CHARS], userinfo[MAX_INFO_STRING];
	int i;

	if ( trap->Argc() < 4 ) {
		trap->SendServerCommand( ent-g_entities, "print \"Usage: amcolor <R> <G> <B> where R,G,B are (Red,Green,Blue) integers between 0 and 255\n\"" );
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		int v;

		trap->Argv( i + 1, arg[i], sizeof( arg[i] ) );
		v = atoi( arg[i] );
		if ( v != 0x300 && (v < 0 || v > 255) ) {	// 0x300 keeps the default of the model
			trap->SendServerCommand( ent-g_entities, "print \"Usage: amcolor <R> <G> <B> where R,G,B are (Red,Green,Blue) integers between 0 and 255\n\"" );
			return;
		}
	}

	trap->GetUserinfo( ent-g_entities, userinfo, sizeof( userinfo ) );
	Info_SetValueForKey( userinfo, "char_color_red", arg[0] );
	Info_SetValueForKey( userinfo, "char_color_green", arg[1] );
	Info_SetValueForKey( userinfo, "char_color_blue", arg[2] );
	trap->SetUserinfo( ent-g_entities, userinfo );
	ClientUserinfoChanged( ent-g_entities );
}

static qboolean JP_SaberInHand( const gentity_t *ent ) {
	const playerState_t *ps = &ent->client->ps;

	return (ps->stats[STAT_WEAPONS] & (1 << WP_SABER)) && ps->weapon == WP_SABER && ps->saberEntityNum && !ps->saberInFlight;
}

static void Cmd_sabercolor_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS], userinfo[MAX_INFO_STRING];
	int num, r, g, b;
	const char *usage = "print \"/sabercolor <saber_num 1 or 2> <R> <G> <B> where (R,G,B) are red, green, blue colors between 0 and 255\n\"";

	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR )
		return;
	if ( ent->client->pers.jp.pluginVersion < 1.1f ) {
		trap->SendServerCommand( ent-g_entities, "print \"/sabercolor is only usable with the JA+ client PLugin greater or equal to 1.1.\n\"" );
		return;
	}
	if ( !JP_SaberInHand( ent ) || BG_SaberInAttack( ent->client->ps.saberMove ) )
		return;
	if ( trap->Argc() != 5 ) {
		trap->SendServerCommand( ent-g_entities, usage );
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );	num = atoi( arg );
	trap->Argv( 2, arg, sizeof( arg ) );	r = atoi( arg );
	trap->Argv( 3, arg, sizeof( arg ) );	g = atoi( arg );
	trap->Argv( 4, arg, sizeof( arg ) );	b = atoi( arg );
	if ( num < 1 || num > 2 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 ) {
		trap->SendServerCommand( ent-g_entities, usage );
		return;
	}

	trap->GetUserinfo( ent-g_entities, userinfo, sizeof( userinfo ) );
	Info_SetValueForKey( userinfo, va( "cp_sbRGB%i", num ), va( "%i", r | (g << 8) | (b << 16) ) );
	Info_SetValueForKey( userinfo, va( "color%i", num ), "6" );	// SABER_RGB
	trap->SetUserinfo( ent-g_entities, userinfo );
	ClientUserinfoChanged( ent-g_entities );
}

// /saber [<saber1> [<saber2>]]: the sabers are changed during the game, no more often than jp_allowSaberSwitch ms
static void Cmd_saber_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	char saber1[MAX_QPATH], saber2[MAX_QPATH], userinfo[MAX_INFO_STRING];

	if ( client->sess.sessionTeam == TEAM_SPECTATOR )
		return;
	if ( !jp_allowSaberSwitch.integer ) {
		trap->SendServerCommand( ent-g_entities, "print \"/saber ^1cmd is not allowed on this server!.\n\"" );
		return;
	}
	if ( g_gametype.integer == GT_SIEGE || g_gametype.integer == GT_POWERDUEL || g_gametype.integer == GT_DUEL ) {
		trap->SendServerCommand( ent-g_entities, "print \"this is not compatible with this gametype!.\n\"" );
		return;
	}
	if ( !JP_SaberInHand( ent ) || client->pers.jp.saberSwitchTime >= level.time )
		return;

	client->pers.jp.saberSwitchTime = level.time + jp_allowSaberSwitch.integer;
	if ( BG_SaberInAttack( client->ps.saberMove ) || BG_SaberInTransitionAny( client->ps.saberMove ) )
		return;

	trap->GetUserinfo( ent-g_entities, userinfo, sizeof( userinfo ) );
	if ( trap->Argc() > 1 ) {
		trap->Argv( 1, saber1, sizeof( saber1 ) );
		Q_strncpyz( saber2, "none", sizeof( saber2 ) );
		if ( trap->Argc() == 3 )
			trap->Argv( 2, saber2, sizeof( saber2 ) );
		Info_SetValueForKey( userinfo, "saber1", saber1 );
		Info_SetValueForKey( userinfo, "saber2", saber2 );
		trap->SetUserinfo( ent-g_entities, userinfo );
		if ( !ClientUserinfoChanged( ent-g_entities ) )
			return;
		trap->GetUserinfo( ent-g_entities, userinfo, sizeof( userinfo ) );
	}

	if ( G_UpdateSaberFromUserinfo( ent, userinfo ) )
		G_ClampSaberLevel( ent );
}

/*
=================
JP_PlayerCommand: returns qtrue if the command was one of these
=================
*/
extern void Jedi_Decloak( gentity_t *self );
int WP_AbsorbConversion( gentity_t *attacked, int atdAbsLevel, gentity_t *attacker, int atPower, int atPowerLevel, int atForceSpent );
qboolean G_CanBeEnemy( gentity_t *self, gentity_t *enemy );

/*
=================
Flamethrower (toggleFlame): replaces force lightning, cone of 150 units in front of the player
=================
*/
qboolean JP_FlameActive( gentity_t *ent ) {
	return ent && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && ent->client->pers.jp.flame;
}

void Cmd_toggleFlame_f( gentity_t *ent ) {
	jpClient_t *jp = &ent->client->pers.jp;

	if ( g_jediVmerc.integer ) {
		trap->SendServerCommand( ent - g_entities, "print \"^5Toggle FlameThrower on/off not allowed in JEdiVsMErc mod\n\"" );
		return;
	}
	if ( jp_allowFlameThrower.integer == 0 ) {
		trap->SendServerCommand( ent - g_entities, "print \"^5FlameThrower not allowed\n\"" );
		return;
	}
	if ( !ent->inuse || ent->client->pers.connected != CON_CONNECTED || ent->client->ps.fd.forceSide == FORCE_LIGHTSIDE ) {
		trap->SendServerCommand( ent - g_entities, "print \"^5FlameThrower is dark force power!\n\"" );
		return;
	}
	if ( jp->empowered || jp->merc ) {
		trap->SendServerCommand( ent - g_entities, "print \"^5FlameThrower manual activation not allowed if merced or empowered\n\"" );
		return;
	}
	jp->flame = !jp->flame;
	if ( !jp->flame ) {
		trap->SendServerCommand( ent - g_entities, "print \"^5FlameThrower desactivated !\n\"" );
		return;
	}
	trap->SendServerCommand( ent - g_entities, "print \"^5FlameThrower activated ! ^7instead of ^2FORCE_LIGHTNING\n\"" );
	if ( jp->pluginVersion < 1.0f )
		trap->SendServerCommand( ent - g_entities, "print \"^1The visual flame effect is only available using the client side JA+ plugin!\n\"" );
}

// ForceFireDamage of the original: like ForceLightningDamage, with its own pauses and damage
static void JP_ForceFireDamage( gentity_t *self, gentity_t *targ, vec3_t dir, vec3_t point ) {
	int dmg, mod;

	self->client->dangerTime = level.time;
	self->client->ps.eFlags &= ~EF_INVULNERABLE;
	self->client->invulnerableTimer = 0;
	self->client->pers.jp.protectedByAdmin = qfalse;

	if ( !targ || !targ->takedamage )
		return;
	if ( !targ->client && targ->s.eType == ET_NPC && targ->s.genericenemyindex < level.time )
		targ->s.genericenemyindex = level.time + 2000;
	if ( !targ->client )
		return;

	if ( targ->client->noLightningTime >= level.time ) {
		targ->client->ps.fd.forcePower++;
		if ( targ->client->ps.fd.forcePower > 99 )
			targ->client->ps.fd.forcePower = 100;
		return;
	}
	if ( !ForcePowerUsableOn( self, targ, FP_LIGHTNING ) )
		return;

	dmg = Q_irand( 1, 3 );
	mod = WP_AbsorbConversion( targ, targ->client->ps.fd.forcePowerLevel[FP_ABSORB], self, FP_LIGHTNING, self->client->ps.fd.forcePowerLevel[FP_LIGHTNING], 1 );
	if ( mod != -1 ) {
		if ( mod == 0 ) {
			dmg = 0;
			targ->client->noLightningTime = level.time + 180;
		} else if ( mod == 1 ) {
			dmg = 1;
			targ->client->noLightningTime = level.time + 50;
		} else if ( mod == 2 ) {
			dmg = 2;
			targ->client->noLightningTime = level.time + 50;
		}
	} else {
		dmg = 3;
		targ->client->noLightningTime = level.time + 50;
	}
	if ( self->client->ps.weapon == WP_MELEE && self->client->ps.fd.forcePowerLevel[FP_LIGHTNING] > FORCE_LEVEL_2 )
		dmg *= 2;
	if ( g_jediVmerc.integer && self->client->ps.trueNonJedi && !self->client->ps.trueJedi )
		dmg = (int)( dmg * 1.5f );

	if ( dmg ) {
		G_Damage( targ, self, self, dir, point, dmg, DAMAGE_NO_ARMOR|DAMAGE_NO_KNOCKBACK, MOD_LAVA );
		if ( targ->inuse && targ->client ) {
			targ->client->pers.jp.burnTime = level.time + 1600;
			targ->client->ps.eFlags2 |= EF2_BURNING;
			targ->client->ps.otherKiller = self->s.number;
			targ->client->ps.otherKillerTime = level.time + 2000;
			targ->client->ps.otherKillerDebounceTime = level.time + 2000;
		}
	}
	if ( targ->client && targ->client->ps.powerups[PW_CLOAKED] ) {
		Jedi_Decloak( targ );
		targ->client->cloakToggleTime = level.time + Q_irand( 3000, 10000 );
	}
}

/*
=================
JP_BurnThink

from ClientThink_real: a player the flamethrower set on fire burns on for 1.6 seconds. He loses a point of health
every 265 ms as long as the player who lit him is still his "other killer", and under water the fire is out.
=================
*/
void JP_BurnThink( gentity_t *ent ) {
	gclient_t *client = ent->client;
	jpClient_t *jp = &client->pers.jp;

	if ( level.time >= jp->burnTime ) {
		if ( jp->burnTime < level.time )
			client->ps.eFlags2 &= ~EF2_BURNING;
		return;
	}
	if ( !(client->ps.eFlags2 & EF2_BURNING) || jp->burnDebounce >= level.time )
		return;

	jp->burnDebounce = level.time + 265;
	if ( level.time < client->ps.otherKillerTime && client->ps.otherKiller != ENTITYNUM_NONE ) {
		gentity_t *burner = &g_entities[client->ps.otherKiller];

		if ( burner->inuse )
			G_Damage( ent, burner, burner, NULL, client->ps.origin, 1, DAMAGE_NO_ARMOR, MOD_LAVA );
	}
	if ( ent->waterlevel == 3 )
		jp->burnTime = 0;
}

// Cmd_BobaFire_f of the original: called instead of ForceShootLightning while the lightning button is held
void JP_ForceFlame( gentity_t *self ) {
	const float radius = 150.0f;
	gentity_t *traceEnt;
	int iEntityList[MAX_GENTITIES], numListedEntities, e, i;
	vec3_t forward, center, mins, maxs, v, size, ent_org, dir;
	trace_t tr;

	if ( !self || !self->inuse || !self->client || self->client->pers.connected != CON_CONNECTED )
		return;
	self->client->ps.eFlags &= ~EF_INVULNERABLE;
	self->client->invulnerableTimer = 0;
	self->client->pers.jp.protectedByAdmin = qfalse;

	if ( self->waterlevel >= 3 ) {
		self->client->ps.eFlags &= ~EF_FLAME;
		self->client->ps.activeForcePass = 0;
		return;
	}
	self->client->ps.eFlags |= EF_FLAME;
	self->client->ps.activeForcePass = FORCE_LEVEL_3;

	AngleVectors( self->client->ps.viewangles, forward, NULL, NULL );
	VectorNormalize( forward );
	VectorCopy( self->client->ps.origin, center );
	for ( i = 0; i < 3; i++ ) {
		mins[i] = center[i] - radius;
		maxs[i] = center[i] + radius;
	}
	numListedEntities = trap->EntitiesInBox( mins, maxs, iEntityList, MAX_GENTITIES );
	for ( e = 0; e < numListedEntities; e++ ) {
		traceEnt = &g_entities[iEntityList[e]];

		if ( traceEnt == self )
			continue;
		if ( traceEnt->r.ownerNum == self->s.number && traceEnt->s.weapon != WP_THERMAL )
			continue;
		if ( !traceEnt->inuse || !traceEnt->takedamage || traceEnt->health <= 0 )
			continue;
		if ( !g_friendlyFire.integer && OnSameTeam( self, traceEnt ) )
			continue;
		// the distance from the edge of the bounding box
		for ( i = 0; i < 3; i++ ) {
			if ( center[i] < traceEnt->r.absmin[i] )
				v[i] = traceEnt->r.absmin[i] - center[i];
			else if ( center[i] > traceEnt->r.absmax[i] )
				v[i] = center[i] - traceEnt->r.absmax[i];
			else
				v[i] = 0;
		}
		VectorSubtract( traceEnt->r.absmax, traceEnt->r.absmin, size );
		VectorMA( traceEnt->r.absmin, 0.5f, size, ent_org );
		VectorSubtract( ent_org, center, dir );
		VectorNormalize( dir );
		if ( DotProduct( dir, forward ) < 0.85f )
			continue;
		if ( VectorLength( v ) >= radius )
			continue;
		if ( !traceEnt->r.bmodel && !trap->InPVS( ent_org, self->client->ps.origin ) )
			continue;
		JP_Trace( &tr, self->client->ps.origin, vec3_origin, vec3_origin, ent_org, self->s.number, MASK_SHOT, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != traceEnt->s.number )
			continue;
		JP_ForceFireDamage( self, traceEnt, dir, ent_org );
	}
}

static const char *jpPlayerCommands[] = {
	"amcolor", "sabercolor", "saber", "amflip", "amknockmedown", "amdropsaber", "BlockWallCreate", "BlockWallDel",
	"BlockWallNext", "BlockWallPrev", "amAltDim", "toggleFlame", "jetpack", "amhug", "amkiss", "engage_FullForceDuel",
	"ampoop", "amphysgun"
};

qboolean JP_IsPlayerCommand( const char *cmd ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( jpEmotes ); i++ ) {
		if ( !Q_stricmp( cmd, jpEmotes[i].name ) )
			return qtrue;
	}
	for ( i = 0; i < (int)ARRAY_LEN( jpPlayerCommands ); i++ ) {
		if ( !Q_stricmp( cmd, jpPlayerCommands[i] ) )
			return qtrue;
	}
	return qfalse;
}

qboolean JP_PlayerCommand( gentity_t *ent, const char *cmd ) {
	int i;

	if ( !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return qfalse;

	for ( i = 0; i < (int)ARRAY_LEN( jpEmotes ); i++ ) {
		if ( !Q_stricmp( cmd, jpEmotes[i].name ) ) {
			const int anim = jpEmotes[i].anim;

			if ( anim == BOTH_SABERKILLER1 )
				Cmd_amfinishinghim_f( ent );
			else if ( anim == BOTH_HIT1 || anim == BOTH_DEATH15 ) {
				// amdie, amdie2: free for all only and not in a duel
				if ( level.gametype == GT_FFA && !ent->client->ps.duelInProgress )
					JP_Emote( ent, anim, jpEmotes[i].bit );
			}
			else if ( anim == BOTH_TAVION_SWORDPOWER ) {
				// amvictory: with the saber on and the gloat sound
				if ( ent->client->ps.weapon == WP_SABER ) {
					if ( BG_SabersOff( &ent->client->ps ) )
						Cmd_ToggleSaber_f( ent );
					JP_Emote( ent, anim, jpEmotes[i].bit );
					G_AddEvent( ent, EV_TAUNT, 4 );	// TAUNT_GLOAT
				}
			}
			else
				JP_Emote( ent, anim, jpEmotes[i].bit );
			return qtrue;
		}
	}

	if ( !Q_stricmp( cmd, "amhug" ) )			{ Cmd_amhug_f( ent );			return qtrue; }
	if ( !Q_stricmp( cmd, "amkiss" ) )			{ Cmd_amkiss_f( ent );			return qtrue; }
	if ( !Q_stricmp( cmd, "ampoop" ) )			{ JP_EmotePoop( ent );			return qtrue; }
	if ( !Q_stricmp( cmd, "amphysgun" ) )		{ G_PropGunToggle( ent );		return qtrue; }	// not from JA+, g_props.c
	if ( !Q_stricmp( cmd, "amcolor" ) )			{ Cmd_amcolor_f( ent );			return qtrue; }
	if ( !Q_stricmp( cmd, "sabercolor" ) )		{ Cmd_sabercolor_f( ent );		return qtrue; }
	if ( !Q_stricmp( cmd, "saber" ) ) {
		if ( !ent->client->ps.duelInProgress )
			Cmd_saber_f( ent );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "amflip" ) )			{ Cmd_amflip_f( ent );			return qtrue; }
	if ( !Q_stricmp( cmd, "amknockmedown" ) )	{ Cmd_amknockmedown_f( ent );	return qtrue; }
	if ( !Q_stricmp( cmd, "amdropsaber" ) )		{ Cmd_amdropsaber_f( ent );		return qtrue; }
	if ( !Q_stricmp( cmd, "BlockWallCreate" ) )	{ Cmd_BlockWallCreate_f( ent );	return qtrue; }
	if ( !Q_stricmp( cmd, "BlockWallDel" ) )	{ Cmd_BlockWallDel_f( ent );	return qtrue; }
	if ( !Q_stricmp( cmd, "BlockWallNext" ) )	{ Cmd_BlockWallNext_f( ent );	return qtrue; }
	if ( !Q_stricmp( cmd, "BlockWallPrev" ) )	{ Cmd_BlockWallPrev_f( ent );	return qtrue; }
	if ( !Q_stricmp( cmd, "amAltDim" ) )		{ Cmd_amAltDim_f( ent );		return qtrue; }
	if ( !Q_stricmp( cmd, "toggleFlame" ) )		{ Cmd_toggleFlame_f( ent );		return qtrue; }
	if ( !Q_stricmp( cmd, "jetpack" ) )			{ Cmd_jetpack_f( ent );			return qtrue; }

	return qfalse;
}
