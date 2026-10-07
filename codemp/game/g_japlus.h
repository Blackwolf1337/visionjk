/*
JA+ Mod v2.4 Build 7 features, rebuilt from the jampgamei386.so for VisionJK.
Original mod by Slider (japlus.net). Everything JA+ specific is declared here.
*/

#pragma once

#define JP_VERSION			"JA+ Mod v2.4 Build 7"
#define JP_GAMENAME			"JA+ Mod v2.4 B7"	// gamename cvar
#define JP_SERVER_VERSION	"2.4B7"				// serverinfo cvar V, parsed by the client plugin as <version>B<build>
#define JP_PLUGIN_VERSION	"1.4B4"		// latest client plugin version known to the server

// clientSession_t.jpAdminLevel
typedef enum {
	JPADM_NONE = 0,
	JPADM_RCON,			// all commands allowed, set through admGrant style access
	JPADM_COUNCIL,
	JPADM_KNIGHT,
	JPADM_INSTRUCTOR
} jpAdminLevel_t;

#define JP_NUM_ADMIN_CMDS	38		// bit index 1..38 in jp_*AllowedCMD

// JA+ per client data. Lives in clientPersistant_t (pers.jp), so it survives respawns but not map changes.
typedef struct jpClient_s {
	// admin gun / admCmdPrev, admCmdNext, playerPrev, playerNext, admCmdEXE
	int		admCmdIndex;			// index in the first admin command list, -1 for none
	int		admTargetNum;			// selected player

	// admin abuse protection
	int		abuseTimer;				// level.time when the current measuring window ends
	int		abuseCmdsLeft;			// sensible admin cmds left in the window
	int		abuseBansLeft;			// abuses left before the admin gets admin-banned

	// motd shown by ammotd, amShowMotd and at connect
	int		motdTime;
	char	motd[1024];

	// admin effects (g_japlus_fx.c)
	qboolean	silenced;				// amsilence, can not use chat
	int			badWordCount;			// detections of the word filter
	char		sayTeamMode[32];		// say_team_mod: "", normal, clan, admin or ampsay
	char		clanPass[64];			// clanpass, clan chat reaches players with the same pass
	int			clanTagWarnTime;		// next warning about a protected clan tag in the name
	int			clanTagWarnState;		// 0..2: warnings sent since clanTagWarnTime was set
	int			testAnim;				// amtest: next animation
	int			burnTime;				// flamethrower: the player burns until then (EF2_BURNING)
	int			burnDebounce;			// next point of burn damage
	qboolean	ignoredBy[MAX_CLIENTS];	// ignoredBy[n]: player n does not want to read this player
	int			pluginDisable;			// cp_pluginDisable bit mask of the client plugin
	qboolean	sleeping;
	qboolean	protectedByAdmin;
	qboolean	empowered;
	qboolean	merc;
	qboolean	blockEditor;			// BlockWallGrantEditor: may use the blocking wall editor
	vec3_t		blockPoint1;			// first corner of the wall being created, zero if none
	int			blockCurrent;			// entity number of the wall the editor is at
	int			userinfoCount;			// jp_userInfoAntiSpam: userinfo changes, lowered every 4 seconds
	int			userinfoDecayTime;
	int			inactProtectTime;		// jp_inactivityProtectTimer: idle players are protected after this
	int			inactProtectState;
	int			inactProtectCount;
	int			hookDebugTime;
	qboolean	welcomed;				// jp_welcomeMSG was shown
	qboolean	balanceSpawn;			// came over a map change in a team: the next spawn does not balance the teams
	qboolean	cheatGod;				// god mode comes from the "god" cheat, jp_allowGodTalk leaves it alone
	qboolean	godTalkDenied;			// a force power was used under the chat bubble, no god mode until it is gone
	int			siegeItemDeadline;		// jp_siegeItemTime: the carried item goes back at this level.time, 0 none
	qboolean	flame;					// toggleFlame: flamethrower instead of force lightning
	qboolean	forcesChanged;
	qboolean	mindtricked;
	qboolean	down;					// knocked down by amflip / amknockmedown
	qboolean	isGhost;
	qboolean	canSeeGhosts;			// admins: see the ghosts
	qboolean	denyVote;
	int			duelEndTime;			// until then the player walks through other players (3 seconds after a duel with jp_DuelAlpha)
	int			hlLives;				// highlander lives
	qboolean	quickening;
	int			quickeningTimer;
	qboolean	fullForceDuel;			// engage_FullForceDuel: force powers hurt in the duel
	int			pushGrabber;			// jp_allowSPForces 2: the player who holds this one in the force push grab, ENTITYNUM_NONE if nobody
	// teleport (g_japlus_tele.c)
	qboolean	refuseTele;				// /refuseTele
	int			teleTime;
	qboolean	teleMarkSet;			// amtelemark
	vec3_t		teleMarkOrigin;
	int			teleMarkYaw;
	qboolean	stActive, stDone;		// star trek teleport state
	int			stCounter;
	vec3_t		stOrigin, stAngles;
	int			saberSwitchTime;		// /saber again after this time
	int			slapTime;				// no slap before level.time
	int			dimension;				// 0 primary, 1 alternate, 2 forced to stay in the alternate dimension
	int			altDimTimer;			// can not change dimension before level.time
	int			voteTime;				// can not callvote before level.time (jp_voteTimer)

	// grappling hook (g_japlus_hook.c)
	float		modelScale;				// jp_allowModelScale: scale of the player model from modelscale.cfg, 1 by default
	qboolean	frozen;					// rcon "freeze": the player can not move
	qboolean	kickHit;				// the first kick of the combo kick (BOTH_JUMP_BACKFLIP_ATCK) hit somebody
	float		pluginVersion;			// version of the client plugin, 0 if none
	qboolean	hooked;					// the hook hit something
	int			hookTime;				// last time the hook was shot, flood protection
	int			hookUseTime;			// last time +use freed the hook

} jpClient_t;

// g_japlus_admin.c
struct gentity_s;
struct forcedata_s;
typedef struct gentity_s gentity_t;

extern const char *jp_adminCmdsA[];
extern const char *jp_adminCmdsB[];
#define JP_NUM_ADMIN_CMDS_A	17
#define JP_NUM_ADMIN_CMDS_B	21

qboolean	JP_ClientCommand( gentity_t *ent, const char *cmd );
qboolean	JP_ConsoleCommand( const char *cmd );

int			G_CmdBitAllowed( int mask, int bit );
qboolean	G_ClientAdminCmdAllowed( gentity_t *ent, const char *cmd, qboolean notify );
qboolean	G_ClientAdminCmdAllowedOn( gentity_t *ent, gentity_t *target, qboolean notify, qboolean checkAbuse );
qboolean	G_ClientCheckAdminAbuse( gentity_t *ent, qboolean notify );
int			G_getClientNumberForString( const char *s );
int			G_amlogin( gentity_t *ent, const char *password, qboolean notify );
const char	*G_ClientUsingJAPLUSClient( int clientNum );
void		G_seeGhost( gentity_t *ent, qboolean see );
qboolean	JP_AdminGun( gentity_t *ent, const char *cmd );
qboolean	G_WordFilter( gentity_t *ent, char *text );
qboolean	JP_ChatCommand( gentity_t *ent, const char *cmd );
qboolean	JP_PlayerCommand( gentity_t *ent, const char *cmd );
qboolean	JP_IsPlayerCommand( const char *cmd );
void		JP_EmoteExec( gentity_t *ent, int anim );
void		JP_FilterPlayerName( gentity_t *ent, char *name, size_t size );
void		JP_ClanTagThink( gentity_t *ent );
void		JP_SanitizeString( char *out, const char *in, size_t outSize, int maxIn );
#define JP_NAME_CHARS	31	// the original name compares look at this many characters
void		JP_UserinfoChanged( gentity_t *ent, const char *userinfo );
qboolean	JP_ChatIgnored( gentity_t *speaker, gentity_t *listener );
void		JP_ShowMessage( const char *text, int seconds, qboolean pluginlessOnly );
void		JP_NewLines( char *text );
void		JP_ClearPluginlessMessages( void );
void		JP_ClientSecondActions( gentity_t *ent );
void		JP_UpdateServerInfo( void );
qboolean	JP_AllPlayersHavePlugin( void );
void		JP_ClientConnectInit( gclient_t *client, qboolean isBot );
void		JP_ClientBeginInit( gentity_t *ent );

// g_japlus_fx.c
void		G_GoInSleep( gentity_t *ent, qboolean on );
void		G_GoInProtect( gentity_t *ent, qboolean on );
void		G_GoInEmpower( gentity_t *ent, qboolean on );
void		G_GoInMerc( gentity_t *ent, qboolean on );
void		G_GoInMindtrick( gentity_t *ent, qboolean on );
void		G_GoInAltDim( gentity_t *ent, int mode );
void		Cmd_amAltDim_f( gentity_t *ent );
void		G_becomeGhost( gentity_t *ent );
void		G_RemoveGhost( gentity_t *ent );
void		G_ADMRemoveGod( gentity_t *ent );
qboolean	G_TouchOtherPlayer( gentity_t *ent );
void		G_AdminCSDisplay( int target, const char *targetMsg, const char *broadcastMsg );
void		Cmd_amempower_f( gentity_t *ent, qboolean rcon );
void		G_TestLine( vec3_t start, vec3_t end, int color, int time );
void		JP_FakeClientName( int clientNum );
int			JP_FakeClientPing( int index, int prevPing );
void		JP_AntiHackModel( gentity_t *ent, char *model, size_t size );
void		JP_AntiFakeLoad( void );
void		JP_AntiFakeStore( void );
void		JP_AntiFakeConnectMessage( gentity_t *ent, const char *ip );
void		JP_GodTalk( playerState_t *ps, qboolean talking );
void		JP_ClientSpawnEnd( gentity_t *ent, int wDisable );
void		JP_UpdateWeaponSwitchFlag( gclient_t *client );
void		JP_MercLoadout( gentity_t *ent );
void		JP_DisplayLatestPluginVersion( gentity_t *ent );
void		JP_KissSlap( gentity_t *ent, const usercmd_t *ucmd );
void		JP_WelcomeSpawn( gentity_t *ent );
void		JP_InactivityProtect( gentity_t *ent );
void		JP_AutoQuitInit( void );
void		JP_AutoQuitFrame( void );
qboolean	JP_UserinfoMayChange( int clientNum );
void		JP_LoadBlockWalls( void );
void		JP_WriteBlockWalls( void );
qboolean	JP_BlockWallConsoleCommand( const char *cmd );
void		Cmd_BlockWallCreate_f( gentity_t *ent );
void		Cmd_BlockWallDel_f( gentity_t *ent );
void		Cmd_BlockWallNext_f( gentity_t *ent );
void		Cmd_BlockWallPrev_f( gentity_t *ent );
void		Cmd_toggleFlame_f( gentity_t *ent );
qboolean	JP_FlameActive( gentity_t *ent );
void		JP_ForceFlame( gentity_t *self );
void		JP_BurnThink( gentity_t *ent );
void		Cmd_ammerc_f( gentity_t *ent, qboolean rcon );
void		Cmd_amprotect_f( gentity_t *ent, qboolean rcon );
void		Cmd_ammindtrick_f( gentity_t *ent, qboolean rcon );
void		Cmd_amghost_f( gentity_t *ent, qboolean rcon );
void		Cmd_amsleep_f( gentity_t *ent, qboolean rcon );
void		Cmd_amwake_f( gentity_t *ent, qboolean rcon );
void		Cmd_amsilence_f( gentity_t *ent, qboolean rcon );
void		Cmd_amunsilence_f( gentity_t *ent, qboolean rcon );
void		Cmd_amForceAltDim_f( gentity_t *ent, qboolean rcon );
void		Cmd_amUnForceAltDim_f( gentity_t *ent, qboolean rcon );
void		Cmd_amdenyvote_f( gentity_t *ent, qboolean rcon );
void		Cmd_amslap_f( gentity_t *ent, qboolean rcon );

// g_japlus_dim.c
void		JP_ClientSpawnReset( struct gclient_s *client );
void		JP_RemovePlayerExplosives( gentity_t *ent );
void		JP_PushGrabThink( gentity_t *self );	// w_force.c
qboolean	JP_IsConnectedClient( const gentity_t *ent );
qboolean	G_CanBeEnemy( gentity_t *self, gentity_t *enemy );	// w_saber.c
qboolean	G_PlInSameDim( const gentity_t *a, const gentity_t *b );
qboolean	G_canInterferWith( int a, int b );
void		G_TempUnlink( int clientNum );
void		JP_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentMask, int capsule, int traceFlags, int useLod );
qboolean	JP_Untargetable( const gentity_t *ent );
qboolean	JP_DamageBlocked( gentity_t *targ, gentity_t *attacker );

// g_japlus_highlander.c
qboolean	JP_HighlanderOn( void );
int			JP_MaxForceRank( void );
int			CheckEndHighlanderGame( qboolean afterDeath );
qboolean	JP_HighlanderTimelimitHit( void );
void		JP_HighlanderMapStart( void );
void		JP_HighlanderDeath( gentity_t *self, gentity_t *attacker );
void		JP_HighlanderSpawn( gentity_t *ent );
void		JP_HighlanderConnect( gclient_t *client );
void		JP_HighlanderSecond( gentity_t *ent );
void		G_GetEnemyForces( gentity_t *killer, gentity_t *victim );
void		Cmd_immortal_f( gentity_t *ent );

// g_japlus_info.c
void		Cmd_ammove_f( gentity_t *ent );
void		Cmd_amDMGs_f( gentity_t *ent );
void		Cmd_duelWhois_f( gentity_t *ent );

// g_japlus_admin2.c
void		Cmd_ammap_f( gentity_t *ent, qboolean rcon );
void		Cmd_amvstr_f( gentity_t *ent, qboolean rcon );
void		Cmd_amrename_f( gentity_t *ent, qboolean rcon );
void		Cmd_amlockteam_f( gentity_t *ent, qboolean rcon );
void		Cmd_amforceteam_f( gentity_t *ent, qboolean rcon );
void		JP_ForceTeam( gentity_t *ent, char *team );
void		Cmd_amkillvote_f( gentity_t *ent, qboolean rcon );
void		Cmd_ampoll_f( gentity_t *ent, qboolean rcon );
void		Cmd_amweather_f( gentity_t *ent, qboolean rcon );
void		Cmd_amplayefx_f( gentity_t *ent, qboolean rcon );
void		Cmd_amtest_f( gentity_t *ent, qboolean rcon );
qboolean	JP_TeamLockRefused( gentity_t *ent, team_t *team, qboolean autoPick );
qboolean	JP_TeamBalanceApplies( void );

// g_japlus_tele.c
void		JP_ManualTeleport( gentity_t *ent, const vec3_t origin, const vec3_t angles );
void		JP_StarTrekSecond( gentity_t *ent );
void		Cmd_amtele_f( gentity_t *ent, qboolean rcon );
void		Cmd_amtelemark_f( gentity_t *ent, qboolean rcon );
void		Cmd_amorigin_f( gentity_t *ent, qboolean rcon );
void		Cmd_refuseTele_f( gentity_t *ent );

// g_japlus_scale.c
void		WP_ModelScaleLoadParms( void );
float		WP_ModelScaleParseParm( const char *model );
void		JP_ApplyModelScale( gentity_t *ent );
void		JP_SetModelScale( gentity_t *ent, float scale, const char *userinfo );
void		JP_ModelScaleUserinfo( gentity_t *ent, const char *model, const char *userinfo );
void		Cmd_size_f( gentity_t *ent );
qboolean	JP_ClientFrozen( int clientNum );

// w_saber.c
qboolean	JP_KickHitSomebody( int clientNum );
void		JP_AttackEndsProtect( gentity_t *ent );
void		G_meleeSystem( gentity_t *self );
qboolean	G_CheckNewStabMove( gentity_t *self );
void		JP_FlipKickStun( int clientNum );
void		JP_BackflipKick( int clientNum );

// g_japlus_hook.c
void		JP_HookFree( gentity_t *ent );
void		Weapon_HookFree( gentity_t *hook );
void		Weapon_HookThink( gentity_t *hook );
void		JP_HookClientThink( gentity_t *ent, const usercmd_t *ucmd );
void		JP_HookFuelThink( gentity_t *ent );
qboolean	JP_HookMissileImpact( gentity_t *ent, gentity_t *other, trace_t *trace );
qboolean	JP_MissileIgnoresTarget( gentity_t *missile, gentity_t *other );
void		G_WeapCutGrappleRope( gentity_t *cutter, vec3_t from, vec3_t to, float radius, qboolean cutOwnToo );


// g_svcmds.c
qboolean	G_AdminFilterPacket( const char *from );
void		G_ProcessAdminIPBans( void );
void		G_AdminAddIP( char *str );
void		G_AdminRemoveIP( char *str );

// g_cmds.c
char *ConcatArgs( int start );

// w_force.c
void WP_AddAsMindtricked( struct forcedata_s *fd, int entNum );
void WP_RemoveTrickedEnt( struct forcedata_s *fd, int client );
