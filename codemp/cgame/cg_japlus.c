// JA+ client plugin features: the auto replier
#include "cg_local.h"

#define REPLY_MAX		5
#define REPLY_LEN		150

typedef struct {
	char	list[REPLY_MAX][REPLY_LEN];
	int		count;
} replyList_t;

static replyList_t	replyDuelStart, replyDuelWin, replyDuelLost, replyPlayerJoin;
static char			replySelfEnter[REPLY_LEN];
static int			selfEnterTime;						// 0 not started, -1 done
static int			joinReplyTime[MAX_CLIENTS];			// one reply per player
static int			duelOpponent = -1;
static int			duelEndWindow;

static void AddReply( replyList_t *l, const char *text ) {
	if ( l->count < REPLY_MAX )
		Q_strncpyz( l->list[l->count++], text, REPLY_LEN );
}

// the file: keys DUEL_START, DUEL_END_WIN, DUEL_END_LOST, ON_SELF_ENTER, ON_PLAYER_JOIN, each followed by a "message", %s is the player name
void CG_JPAutoReplierInit( void ) {
	static char buf[10000];
	fileHandle_t f;
	const char *p, *key, *val;
	int len;

	memset( &replyDuelStart, 0, sizeof( replyDuelStart ) );
	memset( &replyDuelWin, 0, sizeof( replyDuelWin ) );
	memset( &replyDuelLost, 0, sizeof( replyDuelLost ) );
	memset( &replyPlayerJoin, 0, sizeof( replyPlayerJoin ) );
	replySelfEnter[0] = '\0';
	selfEnterTime = 0;
	memset( joinReplyTime, 0, sizeof( joinReplyTime ) );
	duelOpponent = -1;

	len = trap->FS_Open( cp_autoReplierFile.string, &f, FS_READ );
	if ( len < 0 ) {
		trap->Print( "error reading file %s\n", cp_autoReplierFile.string );
		return;
	}
	if ( len >= (int)sizeof( buf ) ) {
		trap->Print( "AutoReplier File %s is too large\n", cp_autoReplierFile.string );
		len = sizeof( buf ) - 1;
	}
	trap->FS_Read( buf, len, f );
	buf[len] = '\0';
	trap->FS_Close( f );

	p = buf;
	for ( ;; ) {
		key = COM_ParseExt( &p, qtrue );
		if ( !key[0] )
			return;
		val = COM_ParseExt( &p, qfalse );
		if ( !val[0] ) {
			SkipRestOfLine( &p );
			continue;
		}
		if ( !Q_stricmp( key, "DUEL_END_WIN" ) )			AddReply( &replyDuelWin, val );
		else if ( !Q_stricmp( key, "DUEL_END_LOST" ) )		AddReply( &replyDuelLost, val );
		else if ( !Q_stricmp( key, "DUEL_START" ) )			AddReply( &replyDuelStart, val );
		else if ( !Q_stricmp( key, "ON_SELF_ENTER" ) )		Q_strncpyz( replySelfEnter, val, sizeof( replySelfEnter ) );
		else if ( !Q_stricmp( key, "ON_PLAYER_JOIN" ) )		AddReply( &replyPlayerJoin, val );
		SkipRestOfLine( &p );
	}
}

static qboolean ReplierAllowed( void ) {
	return !JP_FEATURE( JPF_NOAUTOREPLIER ) && !(cp_pluginDisable.integer & (1 << 4)) && cg.snap;
}

// tmpl with its first %s replaced by name, other % and quotes dropped so the text can not break the command
static void FormatReply( const char *tmpl, const char *name, char *out, size_t size ) {
	qboolean named = qfalse;
	size_t n = 0;

	for ( ; *tmpl && n < size - 1; tmpl++ ) {
		if ( tmpl[0] == '%' && tmpl[1] == 's' && !named ) {
			named = qtrue;
			tmpl++;
			for ( ; *name && n < size - 1; name++ ) {
				if ( *name != '"' && *name != '%' )
					out[n++] = *name;
			}
		} else if ( *tmpl != '"' && *tmpl != '%' ) {
			out[n++] = *tmpl;
		}
	}
	out[n] = '\0';
}

static const char *ReplyPick( const replyList_t *l ) {
	return l->count > 0 ? l->list[Q_irand( 0, l->count - 1 )] : NULL;
}

static void ReplyTell( int clientNum, const char *tmpl, int nameClient ) {
	char name[32], text[REPLY_LEN * 2];

	if ( !tmpl || !tmpl[0] || nameClient < 0 || nameClient >= MAX_CLIENTS || !cgs.clientinfo[nameClient].infoValid )
		return;
	Q_strncpyz( name, cgs.clientinfo[nameClient].name, 31 );
	Q_strcat( name, sizeof( name ), S_COLOR_WHITE );
	FormatReply( tmpl, name, text, sizeof( text ) );
	trap->SendConsoleCommand( va( "tell %i \"%s\"\n", clientNum, text ) );
}

// EV_PRIVATE_DUEL for the local player: start == qtrue when the fight begins
void CG_JPReplyDuelEvent( qboolean start ) {
	if ( start ) {
		duelOpponent = cg.snap->ps.duelIndex;
		if ( ReplierAllowed() && cg.snap->ps.duelInProgress )
			ReplyTell( duelOpponent, ReplyPick( &replyDuelStart ), duelOpponent );
	} else {
		duelEndWindow = cg.time + 2000;
	}
}

// obituary of target, the local player or the duel opponent die at the end of a duel
void CG_JPReplyObituary( int target ) {
	const playerState_t *ps;
	const char *tmpl;

	if ( !ReplierAllowed() || duelOpponent < 0 )
		return;
	ps = &cg.snap->ps;
	if ( target != ps->clientNum && target != duelOpponent )
		return;
	if ( !((ps->duelInProgress && ps->duelIndex == duelOpponent) || cg.time < duelEndWindow) )
		return;
	duelEndWindow = 0;

	tmpl = target == ps->clientNum ? ReplyPick( &replyDuelLost ) : ReplyPick( &replyDuelWin );
	ReplyTell( duelOpponent, tmpl, target == ps->clientNum ? ps->clientNum : duelOpponent );
}

// "print" server command: a player entered the game
void CG_JPReplyPrint( const char *text ) {
	const char *enter, *at;
	char marker[MAX_STRING_CHARS], name[MAX_NETNAME];
	int i;

	if ( !ReplierAllowed() || !replyPlayerJoin.count )
		return;
	enter = CG_GetStringEdString( "MP_SVGAME", "PLENTER" );
	if ( !enter[0] || !strstr( text, enter ) )
		return;
	Com_sprintf( marker, sizeof( marker ), S_COLOR_WHITE " %s", enter );
	at = strstr( text, marker );
	if ( !at || at - text >= MAX_NETNAME )
		return;
	Q_strncpyz( name, text, at - text + 1 );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( i == cg.snap->ps.clientNum || !cgs.clientinfo[i].infoValid || joinReplyTime[i] || strcmp( cgs.clientinfo[i].name, name ) )
			continue;
		joinReplyTime[i] = cg.time + 1000;
		ReplyTell( i, ReplyPick( &replyPlayerJoin ), i );
		return;
	}
}

// every frame: say a line two seconds after entering the server
void CG_JPReplyFrame( void ) {
	if ( selfEnterTime == -1 || !cg.snap || !ReplierAllowed() )
		return;
	if ( !selfEnterTime )
		selfEnterTime = cg.time + 2000;
	else if ( selfEnterTime < cg.time && replySelfEnter[0] ) {
		char text[REPLY_LEN];

		selfEnterTime = -1;
		FormatReply( replySelfEnter, "", text, sizeof( text ) );
		trap->SendConsoleCommand( va( "say \"%s\"\n", text ) );
	}
}

// JA+ macro scan: once a second the ui checks the key bindings while the server asks for it, when the server stops
// asking the bindings are restored once
void CG_JPMacroScanFrame( void ) {
	static int nextScan;
	static qboolean enabled = qtrue;

	if ( !cg.snap )
		return;

	// dmflags 128: no fast turning with the keyboard (cl_yawspeed), checked on pure servers like the macro scan
	if ( (cgs.dmflags & 128) && (cgs.jpPure || JP_FEATURE( JPF_MACROSCAN_ALL )) && (cl_yawspeed.value > 575.0f || cl_yawspeed.value < -575.0f) ) {
		trap->Cvar_Set( "cl_yawspeed", "575" );
		trap->Print( "^1cl_yawspeed ^7has been forced to the value ^2575^7 because yawspeed is fixed on this server.\n" );
	}

	if ( JP_FEATURE( JPF_MACROSCAN ) && (cgs.jpPure || JP_FEATURE( JPF_MACROSCAN_ALL )) ) {
		if ( nextScan + 1000 < cg.time ) {
			trap->SendConsoleCommand( "ui_macroscan\n" );
			nextScan = cg.time;
			enabled = qfalse;
		}
	} else if ( !enabled ) {
		enabled = qtrue;
		trap->SendConsoleCommand( "ui_macroenable\n" );
	}
}

/*
JA+ model scale (jp_allowModelScale): the server sends the size of a player model in iModelScale (percent)
*/
float BG_scaleModelFactor( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cgs.clientinfo[clientNum].infoValid )
		return 0.0f;
	return cg_entities[clientNum].currentState.iModelScale / 100.0f;
}

// CG_Player: when the size of a player changes the saber blades follow, and the camera distance for the own player
void CG_JPModelScale( centity_t *cent, clientInfo_t *ci ) {
	const int clientNum = cent->currentState.clientNum;
	float scale;

	if ( cgs.jpVersion <= 0.0f || cent->currentState.iModelScale <= 0 )
		return;
	scale = cent->currentState.iModelScale / 100.0f;
	if ( scale == cent->jpModelScale )
		return;

	cent->jpModelScale = scale;
	WP_ScaleSaber( clientNum, ci->saber, 0, ci->saberName );
	WP_ScaleSaber( clientNum, ci->saber, 1, ci->saber2Name );
	if ( cg.snap && clientNum == cg.snap->ps.clientNum )
		trap->Cvar_Set( "cg_thirdPersonRange", scale > 1.0f ? va( "%i", (int)(scale * 80.0f) ) : "80" );
}

/*
=================
JA+ client plugin: camera

- Plugin_EndDuel_Rotation (cp_pluginDisable bit 2 turns it off): when the own private duel ends the third
  person camera turns around the player for two seconds. Like the plugin this leaves cg_thirdPerson on.
- Two player animations (kiss, grabs, kick combo victim): the server turns the player, the camera stays.
- More distance for the kick combo and the back attacks.
Rebuilt from cgamex86.dll of the plugin 1.4B4 (EV_PRIVATE_DUEL, CG_CalcViewValues, PmoveSingle).
=================
*/
static qboolean	jpOrbitActive;
static int		jpOrbitEndTime;
static qboolean	jpAngleSaved;
static float	jpSavedAngle, jpStartYaw;
static float	jpSavedRange;

static void CG_JPSetRange( int range ) {
	const int scale = cg.snap ? cg.snap->ps.iModelScale : 0;

	// bigger players get more distance
	if ( scale / 100.0f > 1.0f ) {
		cg_thirdPersonRange.value = scale * range / 100.0f;
		range = (int)cg_thirdPersonRange.value;
	}
	else
		cg_thirdPersonRange.value = range;
	trap->Cvar_Set( "cg_thirdPersonRange", va( "%i", range ) );
}

// EV_PRIVATE_DUEL: the duel of the local player is over
void CG_JPDuelEnded( void ) {
	if ( (cp_pluginDisable.integer & (1 << 2)) || !cg.snap || cg.snap->ps.stats[STAT_HEALTH] <= 1 )
		return;

	trap->Cvar_Set( "cg_cameraOrbit", "6" );
	trap->Cvar_Set( "cg_cameraOrbitDelay", "19" );
	trap->Cvar_Set( "cg_thirdPerson", "1" );
	trap->Cvar_Set( "cg_thirdPersonAngle", "0" );
	trap->Cvar_Set( "cg_thirdPersonRange", "220" );
	jpOrbitEndTime = cg.time + 2000;
	jpOrbitActive = qtrue;
}

// CG_CalcViewValues: the camera orbit of the base game with the end of the rotation above
void CG_JPCameraOrbit( void ) {
	if ( !cg_cameraOrbit.integer || jpOrbitEndTime <= cg.time ) {
		if ( jpOrbitActive && jpOrbitEndTime < cg.time ) {
			trap->Cvar_Set( "cg_cameraOrbit", "0" );
			trap->Cvar_Set( "cg_cameraOrbitDelay", "50" );
			trap->Cvar_Set( "cg_thirdPerson", "1" );
			trap->Cvar_Set( "cg_thirdPersonAngle", "0" );
			cg_thirdPersonAngle.value = 0.0f;
			cg_cameraOrbit.value = 0.0f;
			CG_JPSetRange( 80 );
			jpOrbitActive = qfalse;
		}
		else if ( cg_cameraOrbit.integer && cg.time > cg.nextOrbitTime ) {
			// (base game)
			cg.nextOrbitTime = cg.time + cg_cameraOrbitDelay.integer;
			cg_thirdPersonAngle.value += cg_cameraOrbit.value;
		}
	}
	else if ( cg.nextOrbitTime < cg.time ) {
		cg_thirdPersonAngle.value += cg_cameraOrbit.value;
		cg.nextOrbitTime = cg.time + cg_cameraOrbitDelay.integer;
		if ( jpOrbitActive ) {
			// closer in the first half of the turn, away again in the second
			if ( cg_thirdPersonRange.integer < 121 || cg_thirdPersonAngle.value >= 180.0f )
				cg_thirdPersonRange.value += cg_cameraOrbit.value;
			else
				cg_thirdPersonRange.value -= cg_cameraOrbit.value;
		}
	}
}

static qboolean CG_JPTwoPlayerAnim( int anim ) {
	switch ( anim ) {
	case BOTH_JUMP_BACKFLIP_ATCKEE: case BOTH_JUMP_BACKFLIP_ATCKEE_FALL: case BOTH_KISSEE: case BOTH_KISSER:
	case BOTH_NEW_STABER: case BOTH_NEW_STABEE: case BOTH_KYLE_PA_1: case BOTH_KYLE_PA_2: case BOTH_KYLE_PA_3:
	case BOTH_PLAYER_PA_1: case BOTH_PLAYER_PA_2: case BOTH_PLAYER_PA_3: case BOTH_FORCE_DRAIN_GRAB_START:
	case BOTH_FORCE_DRAIN_GRABBED: case BOTH_ALORA_SPIN_THROW: case BOTH_COWER1_START:
		return qtrue;
	}
	return qfalse;
}

// after the prediction: the yaw the server forces on the player in a two player animation is taken out of the camera
void CG_JPTwoPlayerAnimCamera( const playerState_t *ps ) {
	if ( jpOrbitActive || jpOrbitEndTime >= cg.time )
		return;

	if ( CG_JPTwoPlayerAnim( ps->legsAnim ) || CG_JPTwoPlayerAnim( ps->torsoAnim ) ) {
		const float yaw = (float)(int)(ps->delta_angles[YAW] * (360.0 / 65536));

		if ( !jpAngleSaved ) {
			jpSavedAngle = cg_thirdPersonAngle.value;
			jpStartYaw = yaw;
			jpAngleSaved = qtrue;
		}
		cg_thirdPersonAngle.value = jpStartYaw - yaw;
	}
	else if ( jpAngleSaved ) {
		jpAngleSaved = qfalse;
		cg_thirdPersonAngle.value = jpSavedAngle;
		trap->Cvar_Set( "cg_thirdPersonAngle", va( "%i", (int)jpSavedAngle ) );
	}
}

// CG_CalcViewValues: camera distance in the kick combo and the back attacks
void CG_JPCameraRange( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	int range;

	if ( jpOrbitActive || jpOrbitEndTime >= cg.time )
		return;

	if ( ps->legsAnim == BOTH_JUMP_BACKFLIP_ATCKEE || ps->torsoAnim == BOTH_JUMP_BACKFLIP_ATCKEE )
		range = 175;
	else if ( ps->legsAnim == BOTH_JUMP_BACKFLIP_ATCK || ps->torsoAnim == BOTH_JUMP_BACKFLIP_ATCK )
		range = 140;
	else if ( ps->saberMove == LS_A_BACK || ps->saberMove == LS_A_BACK_CR || ps->saberMove == LS_A_BACKSTAB )
		range = 160;
	else {
		if ( jpSavedRange != 0.0f ) {
			cg_thirdPersonRange.value = jpSavedRange;
			trap->Cvar_Set( "cg_thirdPersonRange", va( "%i", (int)jpSavedRange ) );
			jpSavedRange = 0.0f;
		}
		return;
	}

	if ( jpSavedRange == 0.0f ) {
		jpSavedRange = cg_thirdPersonRange.value;
		CG_JPSetRange( range );
	}
}

