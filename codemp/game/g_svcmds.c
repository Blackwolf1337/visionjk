/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
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

// this file holds commands that can be executed by the server console, but not remote clients

#include "g_local.h"

/*
==============================================================================

PACKET FILTERING


You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and any unspecified digits will match any value, so you can specify an entire class C network with "addip 192.246.40".

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

g_filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that only allows players from your local network.


==============================================================================
*/

typedef struct ipFilter_s {
	uint32_t mask, compare;
} ipFilter_t;

#define	MAX_IPFILTERS (1024)

typedef struct ipFilterList_s {
	ipFilter_t	filters[MAX_IPFILTERS];
	int			num;
	const char	*cvarName;	// cvar that mirrors the list
} ipFilterList_t;

static ipFilterList_t	banList = { {{0}}, 0, NULL };	// JA+: kept in banIPs.txt, no cvar
static ipFilterList_t	adminBanList = { {{0}}, 0, "jp_banAdminIPs" };	// JA+: IPs that may not use amlogin

#define ipFilters	banList.filters
#define numIPFilters	banList.num

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter( char *s, ipFilter_t *f ) {
	char num[128];
	int i, j;
	byteAlias_t b, m;

	b.ui = m.ui = 0u;

	for ( i=0; i<4; i++ ) {
		if ( *s < '0' || *s > '9' ) {
			if ( *s == '*' ) {
				// 'match any'
				// b[i] and m[i] to 0
				s++;
				if ( !*s )
					break;
				s++;
				continue;
			}
			trap->Print( "Bad filter address: %s\n", s );
			return qfalse;
		}

		j = 0;
		while ( *s >= '0' && *s <= '9' )
			num[j++] = *s++;

		num[j] = 0;
		b.b[i] = (byte)atoi( num );
		m.b[i] = 0xFF;

		if ( !*s )
			break;

		s++;
	}

	f->mask = m.ui;
	f->compare = b.ui;

	return qtrue;
}

/*
=================
UpdateIPBans
=================
*/
// JA+: the ban list is kept in this file (one address per line), not in the g_banIPs cvar with its 256 characters
#define JP_BAN_FILE	"banIPs.txt"
static qboolean	jp_loadingBans;	// the file is being read: no need to write it

static void JP_WriteBanFile( const ipFilterList_t *list ) {
	byteAlias_t b, m;
	fileHandle_t f;
	char line[NET_ADDRSTRMAXLEN];
	int i, j;

	if ( jp_loadingBans )
		return;
	trap->FS_Open( JP_BAN_FILE, &f, FS_WRITE );
	if ( !f )
		return;
	for ( i=0; i<list->num; i++ ) {
		if ( list->filters[i].compare == 0xFFFFFFFFu )
			continue;

		b.ui = list->filters[i].compare;
		m.ui = list->filters[i].mask;
		line[0] = 0;
		// (the original writes the four numbers, a subnet comes back as a single address there)
		for ( j=0; j<4; j++ ) {
			if ( m.b[j] != 0xFF )
				Q_strcat( line, sizeof( line ), "*" );
			else
				Q_strcat( line, sizeof( line ), va( "%i", b.b[j] ) );
			Q_strcat( line, sizeof( line ), (j<3) ? "." : "\n" );
		}
		trap->FS_Write( line, strlen( line ), f );
	}
	trap->FS_Close( f );
}

static void UpdateIPBans( ipFilterList_t *list ) {
	byteAlias_t b, m;
	int i, j;
	char ip[NET_ADDRSTRMAXLEN], iplist_final[MAX_CVAR_VALUE_STRING];

	if ( list->cvarName == NULL ) {
		JP_WriteBanFile( list );
		return;
	}

	*iplist_final = 0;
	for ( i=0; i<list->num; i++ ) {
		if ( list->filters[i].compare == 0xFFFFFFFFu )
			continue;

		b.ui = list->filters[i].compare;
		m.ui = list->filters[i].mask;
		*ip = 0;
		for ( j=0; j<4; j++ ) {
			if ( m.b[j] != 0xFF )
				Q_strcat( ip, sizeof( ip ), "*" );
			else
				Q_strcat( ip, sizeof( ip ), va( "%i", b.b[j] ) );
			Q_strcat( ip, sizeof( ip ), (j<3) ? "." : " " );
		}
		if ( strlen( iplist_final )+strlen( ip ) < MAX_CVAR_VALUE_STRING )
			Q_strcat( iplist_final, sizeof( iplist_final ), ip );
		else {
			Com_Printf( "%s overflowed at MAX_CVAR_VALUE_STRING\n", list->cvarName );
			break;
		}
	}

	trap->Cvar_Set( list->cvarName, iplist_final );
}

/*
=================
FilterMatches

true if the address "from" matches one of the entries of the list
=================
*/
static qboolean FilterMatches( const ipFilterList_t *list, const char *from ) {
	int i;
	uint32_t in;
	byteAlias_t m;
	const char *p;

	i = 0;
	p = from;
	m.ui = 0u;
	while ( *p && i < 4 ) {
		m.b[i] = 0;
		while ( *p >= '0' && *p <= '9' ) {
			m.b[i] = m.b[i]*10 + (*p - '0');
			p++;
		}
		if ( !*p || *p == ':' )
			break;
		i++, p++;
	}

	in = m.ui;

	for ( i=0; i<list->num; i++ ) {
		if ( (in & list->filters[i].mask) == list->filters[i].compare )
			return qtrue;
	}

	return qfalse;
}

/*
=================
G_FilterPacket
=================
*/
qboolean G_FilterPacket( char *from ) {
	if ( FilterMatches( &banList, from ) )
		return g_filterBan.integer != 0;

	return g_filterBan.integer == 0;
}

/*
=================
G_AdminFilterPacket

JA+: true if the address is admin-banned (may not use amlogin)
=================
*/
qboolean G_AdminFilterPacket( const char *from ) {
	return FilterMatches( &adminBanList, from );
}

/*
=================
AddIP
=================
*/
static void AddIP( ipFilterList_t *list, char *str ) {
	int i;

	for ( i=0; i<list->num; i++ ) {
		if ( list->filters[i].compare == 0xFFFFFFFFu )
			break; // free spot
	}
	if ( i == list->num ) {
		if ( list->num == MAX_IPFILTERS ) {
			trap->Print( "%sIP filter list is full\n", list == &adminBanList ? "Admin " : "" );
			return;
		}
		list->num++;
	}

	if ( !StringToFilter( str, &list->filters[i] ) )
		list->filters[i].compare = 0xFFFFFFFFu;

	UpdateIPBans( list );
}

/*
=================
RemoveIP
=================
*/
static qboolean RemoveIP( ipFilterList_t *list, char *str ) {
	ipFilter_t	f;
	int			i;

	if ( !StringToFilter( str, &f ) )
		return qfalse;

	for ( i=0; i<list->num; i++ ) {
		if ( list->filters[i].mask == f.mask && list->filters[i].compare == f.compare ) {
			list->filters[i].compare = 0xffffffffu;
			UpdateIPBans( list );
			return qtrue;
		}
	}

	return qfalse;
}

/*
=================
ProcessIPBans

fills the list from the space separated string held by the cvar
=================
*/
static void ProcessIPBans( ipFilterList_t *list, char *cvarString ) {
	char *s = NULL, *t = NULL, str[MAX_CVAR_VALUE_STRING] = {0};

	Q_strncpyz( str, cvarString, sizeof( str ) );

	for ( t=s=cvarString; *t; t=s ) {
		s = strchr( s, ' ' );
		if ( !s )
			break;

		while ( *s == ' ' )
			*s++ = 0;

		if ( *t )
			AddIP( list, t );
	}
}

// JA+: the ban list comes from banIPs.txt, one address per line
void G_ProcessIPBans( void ) {
	static char buf[0x8000 + 1];
	fileHandle_t f;
	const char *p = buf, *token;
	int len, n;

	len = trap->FS_Open( JP_BAN_FILE, &f, FS_READ );
	if ( !f || len < 0 ) {
		Com_Printf( "NO file %s found!\n", JP_BAN_FILE );
		return;
	}
	if ( len > 0x8000 ) {
		len = 0x8000;
		Com_Printf( "IP ban File %s are too large. only first %i caracteres will be read. \n", JP_BAN_FILE, len );
	}
	trap->FS_Read( buf, len, f );
	buf[len] = '\0';
	trap->FS_Close( f );

	jp_loadingBans = qtrue;
	COM_BeginParseSession( "banipsinfo" );
	for ( n = 0; n < 0x800; n++ ) {
		token = COM_ParseExt( &p, qtrue );
		if ( !token[0] )
			break;
		AddIP( &banList, (char *)token );
		SkipRestOfLine( &p );
	}
	jp_loadingBans = qfalse;
}

// JA+: admin ban list, see jp_banAdminIPs
void G_ProcessAdminIPBans( void ) {
	ProcessIPBans( &adminBanList, jp_banAdminIPs.string );
}

void G_AdminAddIP( char *str ) {
	AddIP( &adminBanList, str );
}

// "all" clears the whole list
void G_AdminRemoveIP( char *str ) {
	int i;

	if ( !Q_stricmp( str, "all" ) ) {
		for ( i=0; i<adminBanList.num; i++ )
			adminBanList.filters[i].compare = 0xffffffffu;
		UpdateIPBans( &adminBanList );
		trap->Print( "Admin-Ban Ips list empty\n" );
		return;
	}

	if ( RemoveIP( &adminBanList, str ) )
		trap->Print( "Removed.\n" );
	else
		trap->Print( "Didn't find %s.\n", str );
}

/*
=================
Svcmd_AddIP_f
=================
*/
void Svcmd_AddIP_f (void)
{
	char		str[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 2 ) {
		trap->Print("Usage: addip <ip-mask>\n");
		return;
	}

	trap->Argv( 1, str, sizeof( str ) );

	AddIP( &banList, str );
}

/*
=================
Svcmd_RemoveIP_f
=================
*/
void Svcmd_RemoveIP_f (void)
{
	char		str[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 2 ) {
		trap->Print("Usage: removeip <ip-mask>\n");
		return;
	}

	trap->Argv( 1, str, sizeof( str ) );

	if ( RemoveIP( &banList, str ) )
		trap->Print( "Removed.\n" );
	else
		trap->Print( "Didn't find %s.\n", str );
}

void Svcmd_ListIP_f (void)
{
	int		i, count = 0;
	byteAlias_t b;

	for(i = 0; i < numIPFilters; i++) {
		if ( ipFilters[i].compare == 0xffffffffu )
			continue;

		b.ui = ipFilters[i].compare;
		trap->Print ("%i.%i.%i.%i\n", b.b[0], b.b[1], b.b[2], b.b[3]);
		count++;
	}
	trap->Print ("%i bans.\n", count);
}

/*
===================
Svcmd_EntityList_f
===================
*/
void	Svcmd_EntityList_f (void) {
	int			e;
	gentity_t		*check;

	check = g_entities;
	for (e = 0; e < level.num_entities ; e++, check++) {
		if ( !check->inuse ) {
			continue;
		}
		trap->Print("%3i:", e);
		switch ( check->s.eType ) {
		case ET_GENERAL:
			trap->Print("ET_GENERAL          ");
			break;
		case ET_PLAYER:
			trap->Print("ET_PLAYER           ");
			break;
		case ET_ITEM:
			trap->Print("ET_ITEM             ");
			break;
		case ET_MISSILE:
			trap->Print("ET_MISSILE          ");
			break;
		case ET_SPECIAL:
			trap->Print("ET_SPECIAL          ");
			break;
		case ET_HOLOCRON:
			trap->Print("ET_HOLOCRON         ");
			break;
		case ET_MOVER:
			trap->Print("ET_MOVER            ");
			break;
		case ET_BEAM:
			trap->Print("ET_BEAM             ");
			break;
		case ET_PORTAL:
			trap->Print("ET_PORTAL           ");
			break;
		case ET_SPEAKER:
			trap->Print("ET_SPEAKER          ");
			break;
		case ET_PUSH_TRIGGER:
			trap->Print("ET_PUSH_TRIGGER     ");
			break;
		case ET_TELEPORT_TRIGGER:
			trap->Print("ET_TELEPORT_TRIGGER ");
			break;
		case ET_INVISIBLE:
			trap->Print("ET_INVISIBLE        ");
			break;
		case ET_NPC:
			trap->Print("ET_NPC              ");
			break;
		case ET_BODY:
			trap->Print("ET_BODY             ");
			break;
		case ET_TERRAIN:
			trap->Print("ET_TERRAIN          ");
			break;
		case ET_FX:
			trap->Print("ET_FX               ");
			break;
		default:
			trap->Print("%-3i                ", check->s.eType);
			break;
		}

		if ( check->classname ) {
			trap->Print("%s", check->classname);
		}
		trap->Print("\n");
	}
}

qboolean StringIsInteger( const char *s );
/*
===================
ClientForString
===================
*/
gclient_t	*ClientForString( const char *s ) {
	gclient_t	*cl;
	int			idnum;
	char		cleanInput[MAX_STRING_CHARS];

	// numeric values could be slot numbers
	if ( StringIsInteger( s ) ) {
		idnum = atoi( s );
		if ( idnum >= 0 && idnum < level.maxclients ) {
			cl = &level.clients[idnum];
			if ( cl->pers.connected == CON_CONNECTED ) {
				return cl;
			}
		}
	}

	Q_strncpyz( cleanInput, s, sizeof(cleanInput) );
	Q_StripColor( cleanInput );

	// check for a name match
	for ( idnum=0,cl=level.clients ; idnum < level.maxclients ; idnum++,cl++ ) {
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( !Q_stricmp( cl->pers.netname_nocolor, cleanInput ) ) {
			return cl;
		}
	}

	trap->Print( "User %s is not on the server\n", s );
	return NULL;
}

/*
===================
Svcmd_ForceTeam_f

forceteam <player> <team>
===================
*/
void	Svcmd_ForceTeam_f( void ) {
	gclient_t	*cl;
	char		str[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 3 ) {
		trap->Print("Usage: forceteam <player> <team>\n");
		return;
	}

	// find the player
	trap->Argv( 1, str, sizeof( str ) );
	cl = ClientForString( str );
	if ( !cl ) {
		return;
	}

	// set the team
	trap->Argv( 2, str, sizeof( str ) );
	SetTeam( &g_entities[cl - level.clients], str );
}

char *ConcatArgs( int start );
void Svcmd_Say_f( void ) {
	char *p = NULL;
	// don't let text be too long for malicious reasons
	char text[MAX_SAY_TEXT] = {0};

	if ( trap->Argc () < 2 )
		return;

	p = ConcatArgs( 1 );

	if ( strlen( p ) >= MAX_SAY_TEXT ) {
		p[MAX_SAY_TEXT-1] = '\0';
		G_SecurityLogPrintf( "Cmd_Say_f from -1 (server) has been truncated: %s\n", p );
	}

	Q_strncpyz( text, p, sizeof(text) );
	Q_strstrip( text, "\n\r", "  " );

	//G_LogPrintf( "say: server: %s\n", text );
	trap->SendServerCommand( -1, va("print \"server: %s\n\"", text ) );
}

typedef struct svcmd_s {
	const char	*name;
	void		(*func)(void);
	qboolean	dedicated;
} svcmd_t;

int svcmdcmp( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, ((svcmd_t*)b)->name );
}

svcmd_t svcmds[] = {
	{ "addbot",						Svcmd_AddBot_f,						qfalse },
	{ "addip",						Svcmd_AddIP_f,						qfalse },
	{ "botlist",					Svcmd_BotList_f,					qfalse },
	{ "entitylist",					Svcmd_EntityList_f,					qfalse },
	{ "forceteam",					Svcmd_ForceTeam_f,					qfalse },
	{ "game_memory",				Svcmd_GameMem_f,					qfalse },
	{ "listip",						Svcmd_ListIP_f,						qfalse },
	{ "removeip",					Svcmd_RemoveIP_f,					qfalse },
	{ "say",						Svcmd_Say_f,						qtrue },
	{ "toggleallowvote",			Svcmd_ToggleAllowVote_f,			qfalse },
	{ "toggleuserinfovalidation",	Svcmd_ToggleUserinfoValidation_f,	qfalse },
};
static const size_t numsvcmds = ARRAY_LEN( svcmds );

/*
=================
ConsoleCommand

=================
*/
qboolean	ConsoleCommand( void ) {
	char	cmd[MAX_TOKEN_CHARS] = {0};
	svcmd_t	*command = NULL;

	trap->Argv( 0, cmd, sizeof( cmd ) );

	if ( JP_ConsoleCommand( cmd ) )
		return qtrue;

	command = (svcmd_t *)Q_LinearSearch( cmd, svcmds, numsvcmds, sizeof( svcmds[0] ), svcmdcmp );
	if ( !command )
		return qfalse;

	if ( command->dedicated && !dedicated.integer )
		return qfalse;

	command->func();
	return qtrue;
}

