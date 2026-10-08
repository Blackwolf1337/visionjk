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
JA+ chat protection: word filter, silence, clan chat.
Rebuilt from jampgamei386.so (JA+ v2.4 Build 7).
*/

#include "game/g_local.h"
#include "g_japlus.h"

/*
=================
JP_SanitizeString

JA+ SanitizeString followed by SanitizeString2 / SanitizeString3: lower case copy without control
characters (char is signed in the original, so nothing above 0x7f either) and without color codes.
maxIn > 0: only that many characters of the lower case copy are looked at (names: 31)
=================
*/
void JP_SanitizeString( char *out, const char *in, size_t outSize, int maxIn ) {
	char lower[MAX_STRING_CHARS];
	size_t n = 0;
	int i = 0;

	while ( *in && n < sizeof( lower ) - 1 ) {
		if ( *in == 27 )
			in += in[1] ? 2 : 1;
		else if ( (signed char)*in < ' ' )
			in++;
		else
			lower[n++] = tolower( (unsigned char)*in++ );
	}
	lower[n] = '\0';

	n = 0;
	while ( lower[i] && (maxIn <= 0 || i < maxIn) && n < outSize - 1 ) {
		if ( lower[i] == Q_COLOR_ESCAPE )
			i += (lower[i + 1] >= '0' && lower[i + 1] <= '9') ? 2 : 1;
		else
			out[n++] = lower[i++];
	}
	out[n] = '\0';
}

/*
=================
G_WordFilter

true if text contains one of the words of jp_wordFilterBan. Counts the detections of the
player, after jp_wordFilterNumberB4punish of them the player is silenced.
=================
*/
qboolean G_WordFilter( gentity_t *ent, char *text ) {
	char words[MAX_CVAR_VALUE_STRING], line[150], word[MAX_CVAR_VALUE_STRING];
	char *s, *space;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		return qtrue;
	if ( !jp_wordFilter.integer || !jp_wordFilterBan.string[0] )
		return qfalse;

	JP_SanitizeString( line, text, sizeof( line ), 0 );

	Q_strncpyz( words, jp_wordFilterBan.string, sizeof( words ) );
	for ( s = words; *s; s = space ) {
		space = strchr( s, ' ' );
		if ( space ) {
			while ( *space == ' ' )
				*space++ = '\0';
		}

		if ( *s ) {
			JP_SanitizeString( word, s, sizeof( word ), 0 );
			if ( word[0] && strstr( line, word ) ) {
				jpClient_t *jp = &ent->client->pers.jp;
				const int clientNum = ent-g_entities;

				trap->SendServerCommand( clientNum, "print \"^1Word Filter System has detected bad words!\n\"" );
				if ( jp_wordFilterNumberB4punish.integer ) {
					jp->badWordCount++;
					trap->SendServerCommand( clientNum, va( "print \"^5%i ^7bad words have been detected. After ^5%i ^7you will be auto Silenced!\n\"",
						jp->badWordCount, jp_wordFilterNumberB4punish.integer ) );
					if ( jp->badWordCount >= jp_wordFilterNumberB4punish.integer )
						jp->silenced = qtrue;
				}
				return qtrue;
			}
		}

		if ( !space )
			break;
	}

	return qfalse;
}

/*
=================
name filtering
=================
*/
static int JP_ClientNumberFromPurifiedName( const char *name ) {
	char wanted[MAX_NETNAME], cur[MAX_NETNAME];
	int i;

	JP_SanitizeString( wanted, name, sizeof( wanted ), JP_NAME_CHARS );
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *other = &g_entities[i];

		if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED )
			continue;
		JP_SanitizeString( cur, other->client->pers.netname, sizeof( cur ), JP_NAME_CHARS );
		if ( !Q_stricmp( cur, wanted ) )
			return i;
	}
	return -1;
}

static qboolean JP_NameContains( const char *name, const char *tag ) {
	char n[MAX_NETNAME], t[MAX_NETNAME];

	if ( !name[0] || !tag[0] )
		return qfalse;
	JP_SanitizeString( n, name, sizeof( n ), JP_NAME_CHARS );
	JP_SanitizeString( t, tag, sizeof( t ), JP_NAME_CHARS );
	return n[0] && strstr( n, t ) != NULL;
}

static void JP_ClanTagWarn( int clientNum ) {
	trap->SendServerCommand( clientNum, va( "print \"^1You have to login your ^5clanpass ^1to use this ^5clanTag %s ^1in your name!\n   ^2use the ^5/clanpass ^2cmd or the plugin autologin ^5cp_clanPwd ^2cvar\n\"", jp_clanTag.string ) );
}

/*
=================
JP_FilterPlayerName

applies the word filter, the unique name rule and the clan tag protection to a new name
=================
*/
void JP_FilterPlayerName( gentity_t *ent, char *name, size_t size ) {
	const int clientNum = ent-g_entities;
	gclient_t *client = ent->client;
	int other;

	if ( !jp_allowBlackName.integer ) {
		// ^0 and ^8 are black, the name would be hard to read
		char *in = name, *out = name;

		while ( *in ) {
			if ( in[0] == '^' && (in[1] == '0' || in[1] == '8') )
				in += 2;
			else
				*out++ = *in++;
		}
		*out = '\0';
	}

	if ( client->pers.connected == CON_CONNECTED && jp_wordFilter.integer > 0 && (jp_wordFilter.integer & 0x40) && G_WordFilter( ent, name ) )
		Q_strncpyz( name, "Padawan", size );

	other = JP_ClientNumberFromPurifiedName( name );
	if ( !jp_allowSamePlayerNames.integer && other != -1 && other != clientNum )
		Q_strncpyz( name, va( "Padawan ^7(^5%i^7)", clientNum ), size );

	// a clan tag is only usable by players who logged in the clan pass
	if ( client->pers.connected == CON_CONNECTED && !(ent->r.svFlags & SVF_BOT) && client->pers.jp.clanTagWarnTime < level.time
		&& jp_clanPass.string[0] && jp_clanTag.string[0] && JP_NameContains( name, jp_clanTag.string )
		&& (!client->pers.jp.clanPass[0] || Q_stricmp( jp_clanPass.string, client->pers.jp.clanPass )) ) {
		client->pers.jp.clanTagWarnTime = level.time + 120000;
		JP_ClanTagWarn( clientNum );
	}
}

/*
=================
JP_ClanTagThink

every client think: 30 and 60 seconds after the clan tag was seen in the name of a player who did not log in
the clan pass he is warned again, after 75 seconds he is renamed
=================
*/
void JP_ClanTagThink( gentity_t *ent ) {
	gclient_t *client = ent->client;
	jpClient_t *jp = &client->pers.jp;
	char userinfo[MAX_INFO_STRING];
	const int clientNum = ent-g_entities;

	if ( !ent->inuse || clientNum >= MAX_CLIENTS || client->pers.connected != CON_CONNECTED || (ent->r.svFlags & SVF_BOT) )
		return;
	if ( level.time >= jp->clanTagWarnTime )
		return;
	if ( jp->clanPass[0] && !Q_stricmp( jp_clanPass.string, jp->clanPass ) )
		return;

	if ( jp->clanTagWarnTime - 60000 < level.time + 30000 && jp->clanTagWarnState == 0 ) {
		jp->clanTagWarnState = 1;
		JP_ClanTagWarn( clientNum );
	}
	else if ( jp->clanTagWarnTime - 60000 < level.time && jp->clanTagWarnState == 1 ) {
		jp->clanTagWarnState = 2;
		JP_ClanTagWarn( clientNum );
	}
	else if ( jp->clanTagWarnTime - 45000 < level.time && jp->clanTagWarnState == 2 ) {
		jp->clanTagWarnTime = level.time;
		jp->clanTagWarnState = 0;
		if ( JP_NameContains( client->pers.netname, jp_clanTag.string ) ) {
			trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
			Info_SetValueForKey( userinfo, "name", "Padawan" );
			trap->SetUserinfo( clientNum, userinfo );
			ClientUserinfoChanged( clientNum );
		}
	}
}

/*
=================
JP_UserinfoChanged: parts of the userinfo only the JA+ client plugin sends
=================
*/
void JP_UserinfoChanged( gentity_t *ent, const char *userinfo ) {
	const char *v = Info_ValueForKey( userinfo, "cp_pluginDisable" );
	int disable;

	if ( v[0] ) {
		disable = atoi( v );
		if ( disable >= 0 )
			ent->client->pers.jp.pluginDisable = disable;
	}
}

/*
=================
chat commands: ignore, clanpass, say_team_mod, clansay, clanwhois
=================
*/
qboolean JP_ChatIgnored( gentity_t *speaker, gentity_t *listener ) {
	return speaker && listener && speaker->client && speaker->client->pers.jp.ignoredBy[listener-g_entities];
}

static void Cmd_ignore_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	const int me = ent-g_entities;
	int num, i;

	if ( trap->Argc() < 2 ) {
		trap->SendServerCommand( me, "print \"^5List of Ignored Players:\n\"" );
		for ( i = 0; i < level.maxclients; i++ ) {
			const gentity_t *other = &g_entities[i];

			if ( other->inuse && other->client && other->client->pers.connected == CON_CONNECTED && other->client->pers.jp.ignoredBy[me] )
				trap->SendServerCommand( me, va( "print \"\tID %i->%s\n\"", i, other->client->pers.netname ) );
		}
		trap->SendServerCommand( me, "print \"ignore <ID_PLayer or partial names or (-1 for All)>\n\"" );
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );
	num = G_getClientNumberForString( arg );

	if ( num == -1 ) {
		// toggle everybody: the state of the first player decides
		int state = -1;

		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *other = &g_entities[i];

			if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED )
				continue;
			if ( state == -1 )
				state = !other->client->pers.jp.ignoredBy[me];
			other->client->pers.jp.ignoredBy[me] = state;
		}
		trap->SendServerCommand( me, state == 1 ? "print \"^1 you ignore all Players\n\"" : "print \"^2 you Can hear all players\n\"" );
		return;
	}

	if ( num >= 0 && num < level.maxclients ) {
		gentity_t *other = &g_entities[num];

		if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED )
			return;

		other->client->pers.jp.ignoredBy[me] = !other->client->pers.jp.ignoredBy[me];
		trap->SendServerCommand( me, va( other->client->pers.jp.ignoredBy[me] ? "print \"^1 you ignore all SAY from: %s\n\"" : "print \"^2 you STOP ignoring all SAY from: %s\n\"",
			other->client->pers.netname ) );
	}
}

static void Cmd_clanwhois_f( gentity_t *ent ) {
	const int me = ent-g_entities;
	int i;

	if ( !ent->client->pers.jp.clanPass[0] ) {
		trap->SendServerCommand( me, "print \"You must define a clanpass by typeing ^5/clanpass your_say_clanpass\n\"" );
		return;
	}

	trap->SendServerCommand( me, "print \"^1Players logged into your clan:  \n\"" );
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *other = &g_entities[i];

		if ( other->inuse && other->client && other->client->pers.connected == CON_CONNECTED
			&& !Q_stricmp( ent->client->pers.jp.clanPass, other->client->pers.jp.clanPass ) )
			trap->SendServerCommand( me, va( "print \"^1\t-^7 %s  \n\"", other->client->pers.netname ) );
	}
}

static void Cmd_say_clan_f( gentity_t *ent ) {
	char text[150], name[64];
	int i;

	if ( ent->client->pers.jp.silenced )
		return;
	if ( !ent->client->pers.jp.clanPass[0] ) {
		trap->SendServerCommand( ent-g_entities, "print \"You must define a clanpass by typeing ^5/clanpass your_say_clanpass\n\"" );
		return;
	}
	if ( trap->Argc() < 2 )
		return;

	Q_strncpyz( text, ConcatArgs( 1 ), sizeof( text ) );
	G_LogPrintf( "say_clan: %s: %s\n", ent->client->pers.netname, text );
	Com_sprintf( name, sizeof( name ), EC"(%s%c%c"EC")"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );

	if ( jp_wordFilter.integer > 0 && (jp_wordFilter.integer & 0x10) && G_WordFilter( ent, text ) )
		return;
	WebAdmin_Chat( "clan", ent, NULL, text );

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *other = &g_entities[i];

		if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED )
			continue;
		if ( other->client->sess.jpAdminLevel == JPADM_RCON || !Q_stricmp( ent->client->pers.jp.clanPass, other->client->pers.jp.clanPass ) )
			trap->SendServerCommand( i, va( "chat \"^1<Clan>^7%s^1%s\"", name, text ) );
	}
}

qboolean JP_ChatCommand( gentity_t *ent, const char *cmd ) {
	jpClient_t *jp = &ent->client->pers.jp;
	char arg[MAX_TOKEN_CHARS];

	if ( !Q_stricmp( cmd, "say_team_mod" ) ) {
		if ( trap->Argc() >= 2 ) {
			trap->Argv( 1, arg, sizeof( arg ) );
			Q_strncpyz( jp->sayTeamMode, arg, sizeof( jp->sayTeamMode ) );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "clanpass" ) ) {
		if ( trap->Argc() >= 2 ) {
			trap->Argv( 1, arg, sizeof( arg ) );
			Q_strncpyz( jp->clanPass, arg, sizeof( jp->clanPass ) );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "clansay" ) )		{ Cmd_say_clan_f( ent );	return qtrue; }
	if ( !Q_stricmp( cmd, "clanwhois" ) )	{ Cmd_clanwhois_f( ent );	return qtrue; }
	if ( !Q_stricmp( cmd, "ignore" ) )		{ Cmd_ignore_f( ent );		return qtrue; }

	return qfalse;
}

/*
=================
Old player name system (jp_oldPlayerName): the first name seen for an IP is kept in ipname.txt,
players who connect with another name from the same IP are reported
=================
*/
#define ANTIFAKE_MAX	100

static char antiFakeIP[ANTIFAKE_MAX][24];
static char antiFakeName[ANTIFAKE_MAX][MAX_NETNAME];
static int antiFakeIndex = -1;	// last written slot, the table is a ring

static void AntiFakeIPOnly( const char *ip, char *out, size_t size ) {
	char *colon;

	Q_strncpyz( out, ip, size );
	colon = strchr( out, ':' );
	if ( colon )
		*colon = '\0';
}

static int AntiFakeSearch( const char *ip ) {
	int i;

	if ( !ip[0] )
		return -1;
	for ( i = 0; i < ANTIFAKE_MAX && antiFakeIP[i][0]; i++ ) {
		if ( !Q_stricmp( antiFakeIP[i], ip ) )
			return i;
	}
	return -1;
}

static void AntiFakeAdd( const char *ip, const char *name ) {
	if ( !ip[0] || !name[0] || AntiFakeSearch( ip ) != -1 )
		return;
	if ( ++antiFakeIndex >= ANTIFAKE_MAX )
		antiFakeIndex = 0;
	Q_strncpyz( antiFakeIP[antiFakeIndex], ip, sizeof( antiFakeIP[0] ) );
	Q_strncpyz( antiFakeName[antiFakeIndex], name, sizeof( antiFakeName[0] ) );
}

void JP_AntiFakeLoad( void ) {
	static char buf[0x1800];
	fileHandle_t f;
	const char *p, *tok;
	char ip[24];
	int len;

	if ( !jp_oldPlayerName.integer )
		return;
	len = trap->FS_Open( "ipname.txt", &f, FS_READ );
	if ( len < 0 ) {
		Com_Printf( "error reading file %s\n", "ipname.txt" );
		return;
	}
	if ( len >= (int)sizeof( buf ) ) {
		Com_Printf( "Anti Fake Ip_Name File %s is too large\n", "ipname.txt" );
		len = sizeof( buf ) - 1;
	}
	trap->FS_Read( buf, len, f );
	buf[len] = '\0';
	trap->FS_Close( f );

	p = buf;
	for ( ;; ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] )
			return;
		Q_strncpyz( ip, tok, sizeof( ip ) );
		tok = COM_ParseExt( &p, qfalse );
		AntiFakeAdd( ip, tok );
		SkipRestOfLine( &p );
	}
}

void JP_AntiFakeStore( void ) {
	fileHandle_t f;
	char line[128];
	int i;

	if ( !jp_oldPlayerName.integer )
		return;
	trap->FS_Open( "ipname.txt", &f, FS_WRITE );
	if ( !f )
		return;
	for ( i = 0; i < ANTIFAKE_MAX && antiFakeIP[i][0]; i++ ) {
		Com_sprintf( line, sizeof( line ), "%s \"%s\"\n", antiFakeIP[i], antiFakeName[i] );
		trap->FS_Write( line, strlen( line ), f );
	}
	trap->FS_Close( f );
}

void JP_AntiFakeConnectMessage( gentity_t *ent, const char *rawIP ) {
	const char *name = ent->client->pers.netname;
	const char *connected = G_GetStringEdString( "MP_SVGAME", "PLCONNECT" );
	char ip[24];
	int idx;

	AntiFakeIPOnly( rawIP, ip, sizeof( ip ) );
	idx = AntiFakeSearch( ip );
	if ( idx == -1 ) {
		AntiFakeAdd( ip, name );
		trap->SendServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " %s\n\"", name, connected ) );
	} else if ( !Q_stricmp( name, antiFakeName[idx] ) ) {
		trap->SendServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " %s\n\"", name, connected ) );
	} else {
		trap->SendServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " %s ...^1Player with same IP " S_COLOR_WHITE "%s\n\"", name, connected, antiFakeName[idx] ) );
		G_LogPrintf( "%s" S_COLOR_WHITE " %s ...^1Player with same IP " S_COLOR_WHITE "%s\n", name, connected, antiFakeName[idx] );
	}
}

qboolean BG_FileExists( const char *fileName );

static int CountSub( const char *s, const char *sub ) {
	int n = 0;
	size_t len = strlen( sub );

	for ( s = strstr( s, sub ); s; s = strstr( s + len, sub ) )
		n++;
	return n;
}

/*
=================
JP_AntiHackModel

jp_antiHackModel: in team games jedi_ models without a model.glm of their own use the red or blue skin, and
"model/head_x|torso_y|lower_z" strings with missing or doubled body parts fall back to the plain model
=================
*/
void JP_AntiHackModel( gentity_t *ent, char *model, size_t size ) {
	gclient_t *client = ent->client;
	char base[MAX_QPATH], *slash;

	if ( jp_antiHackModel.integer != 1 || !model[0] || !Q_stricmp( model, client->modelname ) )
		return;

	if ( level.gametype >= GT_TEAM && level.gametype != GT_SIEGE && !g_jediVmerc.integer && !Q_stricmpn( model, "jedi_", 5 ) ) {
		Q_strncpyz( base, model, sizeof( base ) );
		slash = strrchr( base, '/' );
		if ( slash && slash[0] ) {
			*slash = '\0';
			if ( !BG_FileExists( va( "models/players/%s/model.glm", base ) ) ) {
				if ( client->sess.sessionTeam == TEAM_RED )
					Com_sprintf( model, size, "%s/red", base );
				else if ( client->sess.sessionTeam == TEAM_BLUE )
					Com_sprintf( model, size, "%s/blue", base );
			}
		}
	}

	Q_strncpyz( base, model, sizeof( base ) );
	slash = strrchr( base, '/' );
	if ( slash && strrchr( base, '|' ) ) {
		const char *parts = slash + 1;
		const int bars = CountSub( parts, "|" );

		*slash = '\0';
		if ( bars > 0 && (CountSub( parts, "head_" ) != 1 || CountSub( parts, "torso_" ) != 1 || CountSub( parts, "lower_" ) != 1 || bars != 2) )
			Q_strncpyz( model, base, size );
	}
}

/*
=================
jp_fakeClients "name1;name2;...": bots that join get the first of these names no other bot uses and
show a ping like a player on the scoreboard
=================
*/
void JP_FakeClientName( int clientNum ) {
	char list[MAX_STRING_CHARS], userinfo[MAX_INFO_STRING], *name, *next;
	int i;

	if ( !jp_fakeClients.string[0] )
		return;
	Q_strncpyz( list, jp_fakeClients.string, sizeof( list ) );

	for ( name = list; name && *name; name = next ) {
		qboolean used = qfalse;

		next = strchr( name, ';' );
		if ( next ) {
			*next++ = '\0';
			while ( *next == ' ' )
				next++;
		}
		for ( i = 0; i < level.maxclients; i++ ) {
			const gentity_t *other = &g_entities[i];

			if ( i != clientNum && other->inuse && other->client && other->client->pers.connected == CON_CONNECTED
				&& (other->r.svFlags & SVF_BOT) && other->client->pers.netname[0] && !Q_stricmp( other->client->pers.netname, name ) ) {
				used = qtrue;
				break;
			}
		}
		if ( !used && name[0] ) {
			char fake[36];

			trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
			Q_strncpyz( fake, name, sizeof( fake ) );
			Info_SetValueForKey( userinfo, "name", fake );
			trap->SetUserinfo( clientNum, userinfo );
			return;
		}
	}
}

int JP_FakeClientPing( int index, int prevPing ) {
	(void)prevPing;
	int ping = Q_irand( 0, 7 );

	if ( index < 8 )
		ping += 40;	// prevPing is not used, JA+ kept two running values here that are not known
	else if ( Q_irand( 0, 2 ) < 2 )
		ping += (index / 2) * 2 + 40;
	else
		ping += 40 - index / 2;
	return ping < 0 ? Q_irand( 0, 7 ) + 40 : ping;
}
