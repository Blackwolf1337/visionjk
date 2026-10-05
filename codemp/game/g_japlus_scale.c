/*
JA+ model scale (jp_allowModelScale): modelscale.cfg holds one "<model> <scale>" pair per line,
the player model, the eye height and the saber blades follow it, with jp_allowDmgSpeedScale also
the saber damage and speed.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "g_local.h"
#include "g_japlus.h"

#define JP_MODELSCALE_FILE	"modelscale.cfg"
#define JP_MODELSCALE_MIN	0.65f
#define JP_MODELSCALE_MAX	1.3f

static char jp_modelScaleParms[0x80000];

/*
=================
WP_ModelScaleLoadParms
=================
*/
void WP_ModelScaleLoadParms( void ) {
	fileHandle_t f;
	int len = trap->FS_Open( JP_MODELSCALE_FILE, &f, FS_READ );

	jp_modelScaleParms[0] = '\0';
	if ( len < 0 || !f ) {
		Com_Printf( "error reading file %s\n", JP_MODELSCALE_FILE );
		return;
	}
	if ( len >= (int)sizeof( jp_modelScaleParms ) ) {
		Com_Printf( "Model Scale File %s are too large\n", JP_MODELSCALE_FILE );
		len = sizeof( jp_modelScaleParms ) - 1;
	}
	trap->FS_Read( jp_modelScaleParms, len, f );
	jp_modelScaleParms[len] = '\0';
	COM_Compress( jp_modelScaleParms );
	trap->FS_Close( f );
}

/*
=================
WP_ModelScaleParseParm

scale of a model (name without the skin), 1 if the model is not listed or the value is out of range
=================
*/
float WP_ModelScaleParseParm( const char *model ) {
	const char *p = jp_modelScaleParms, *token, *value;
	float scale;

	if ( !model || !model[0] )
		return 1.0f;

	COM_BeginParseSession( "modelscaleinfo" );
	for ( ;; ) {
		token = COM_ParseExt( &p, qtrue );
		if ( !token[0] )
			return 1.0f;
		if ( !Q_stricmp( token, model ) )
			break;
		SkipRestOfLine( &p );
	}
	if ( COM_ParseString( &p, &value ) )
		return 1.0f;

	scale = atof( value );
	if ( scale > JP_MODELSCALE_MAX || scale < JP_MODELSCALE_MIN )
		return 1.0f;
	return scale;
}

/*
=================
BG_scaleModelFactor
=================
*/
float BG_scaleModelFactor( int clientNum ) {
	const gentity_t *ent;

	if ( clientNum < 0 || clientNum >= level.maxclients )
		return 1.0f;
	ent = &g_entities[clientNum];
	if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return 1.0f;
	return ent->client->pers.jp.modelScale;
}

/*
=================
JP_SetModelScale

applies the scale to the model, the player state and both sabers (names from the userinfo)
=================
*/
void JP_ApplyModelScale( gentity_t *ent ) {
	const float scale = ent->client->pers.jp.modelScale;

	ent->client->ps.iModelScale = (int)lrintf( scale * 100.0f );
	VectorSet( ent->modelScale, scale, scale, scale );
}

static void JP_SetModelScale( gentity_t *ent, float scale, const char *userinfo ) {
	int i;

	ent->client->pers.jp.modelScale = scale;
	JP_ApplyModelScale( ent );

	for ( i = 0; i < MAX_SABERS; i++ ) {
		const char *saber = Info_ValueForKey( userinfo, va( "saber%i", i + 1 ) );

		if ( saber[0] && Q_stricmp( saber, "none" ) )
			WP_ScaleSaber( ent-g_entities, ent->client->saber, i, saber );
	}
}

/*
=================
JP_ModelScaleUserinfo

ClientUserinfoChanged: the player picked another model
=================
*/
void JP_ModelScaleUserinfo( gentity_t *ent, const char *model, const char *userinfo ) {
	char name[MAX_QPATH], *skin;

	if ( jp_allowModelScale.integer != 1 || !model[0] || !Q_stricmp( model, ent->client->modelname ) )
		return;

	Q_strncpyz( name, model, sizeof( name ) );
	if ( (skin = strrchr( name, '/' )) != NULL )
		*skin = '\0';
	JP_SetModelScale( ent, WP_ModelScaleParseParm( name ), userinfo );
}

/*
=================
JP_ClientFrozen

rcon "freeze <player>": the player can not move, checked in the pmove
=================
*/
qboolean JP_ClientFrozen( int clientNum ) {
	const gentity_t *ent;

	if ( clientNum < 0 || clientNum >= level.maxclients )
		return qfalse;
	ent = &g_entities[clientNum];
	return ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && ent->client->pers.jp.frozen;
}

/*
=================
size <scale>

rcon admins only: any scale for the own model, no range check like in JA+
=================
*/
void Cmd_size_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS], userinfo[MAX_INFO_STRING];

	if ( trap->Argc() < 2 )
		return;
	trap->Argv( 1, arg, sizeof( arg ) );
	trap->GetUserinfo( ent-g_entities, userinfo, sizeof( userinfo ) );
	JP_SetModelScale( ent, atof( arg ), userinfo );
}
