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

// g_auth.c -- admin login without a password: with a key, or with a code that works once. Not from JA+.
//
// Keys (the protocol is in bg_auth.h). The server has admin_keys.txt in its game folder, a line for each admin:
//
//   <rank> ssh-ed25519 <public key> <comment>		rank: council, knight, instructor or rcon (every right)
//
// The part behind the rank is what ssh-keygen writes into a .pub file, what "v_amkey show" of the client prints
// and what 1Password shows as the public key. A client says which key it has, gets 32 random bytes, and sends
// back a signature over them that only the secret half of the key can make. A wrong signature counts as a
// wrong try like a wrong password did (G_AdminLoginFailed).
//
// Codes are for admins whose client is not of this tree and can not sign: the web panel (or "amcode" on the
// console of the server) makes a code of eight letters and digits for a rank, it is typed as "amlogin <code>"
// within a minute and works once.

#include "game/g_local.h"
#include "game/auth/bg_auth.h"
#include "monocypher-ed25519.h"

#define AUTH_KEYS_FILE		"admin_keys.txt"
#define AUTH_MAX_KEYS		64
#define AUTH_MAX_CODES		8
#define AUTH_CODE_LEN		8
#define AUTH_CODE_MSEC		60000
#define AUTH_HELLOS			8			// a client may name that many keys within AUTH_NONCE_MSEC
// no I, O, 0 and 1: a code is read off a page and typed
#define AUTH_CODE_CHARS		"ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

typedef struct authKey_s {
	byte	pub[AUTH_PUBLIC_SIZE];
	int		adminLevel;
	char	comment[48];
} authKey_t;

typedef struct authClient_s {
	byte		nonce[AUTH_NONCE_SIZE];
	qboolean	asked;				// the nonce waits for its answer
	int			key;				// the key it was made for
	int			time;				// trap->Milliseconds() when it was made
	int			hellos, helloTime;
} authClient_t;

typedef struct authCode_s {
	char	text[AUTH_CODE_LEN + 1];
	int		adminLevel;
	int		time;					// trap->Milliseconds() when it was made, 0: free
} authCode_t;

static authKey_t	authKeys[AUTH_MAX_KEYS];
static int			numAuthKeys;
static authClient_t	authClients[MAX_CLIENTS];
static authCode_t	authCodes[AUTH_MAX_CODES];

static const struct {
	const char	*name;
	int			adminLevel;
} authRanks[] = {
	{ "council",	JPADM_COUNCIL },
	{ "knight",		JPADM_KNIGHT },
	{ "instructor",	JPADM_INSTRUCTOR },
	{ "rcon",		JPADM_RCON },
};

const char *G_AuthRankName( int adminLevel ) {
	size_t i;

	for ( i = 0; i < ARRAY_LEN( authRanks ); i++ ) {
		if ( authRanks[i].adminLevel == adminLevel ) {
			return authRanks[i].name;
		}
	}
	return "none";
}

// JPADM_NONE: there is no rank of that name
int G_AuthRankLevel( const char *name ) {
	size_t i;

	for ( i = 0; i < ARRAY_LEN( authRanks ); i++ ) {
		if ( !Q_stricmp( authRanks[i].name, name ) ) {
			return authRanks[i].adminLevel;
		}
	}
	return JPADM_NONE;
}

/*
=================
G_AuthLoadKeys

Reads admin_keys.txt. A line that starts with # or // and an empty one are skipped, any other line that is not
a rank and a key is named on the console, so a typing error does not silently lock an admin out.
=================
*/
static void G_AuthLoadKeys( void ) {
	static char		text[32768];
	fileHandle_t	f;
	char			*line, *next;
	int				len, lineNum = 0;

	numAuthKeys = 0;
	len = trap->FS_Open( AUTH_KEYS_FILE, &f, FS_READ );
	if ( len < 0 || !f ) {
		return;
	}
	if ( len >= (int)sizeof( text ) ) {
		Com_Printf( S_COLOR_YELLOW AUTH_KEYS_FILE " is too large, only its first %i bytes are read\n", (int)sizeof( text ) - 1 );
		len = sizeof( text ) - 1;
	}
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';

	for ( line = text; line && *line; line = next ) {
		char		rank[16], comment[sizeof( authKeys[0].comment )];
		byte		pub[AUTH_PUBLIC_SIZE];
		const char	*p;
		int			n = 0, adminLevel, i;

		next = strchr( line, '\n' );
		if ( next ) {
			*next++ = '\0';
		}
		lineNum++;
		for ( p = line; *p == ' ' || *p == '\t' || *p == '\r'; p++ ) {
		}
		if ( !*p || *p == '#' || ( p[0] == '/' && p[1] == '/' ) ) {
			continue;
		}
		for ( ; *p && *p != ' ' && *p != '\t' && n < (int)sizeof( rank ) - 1; p++ ) {
			rank[n++] = *p;
		}
		rank[n] = '\0';
		adminLevel = G_AuthRankLevel( rank );
		if ( adminLevel == JPADM_NONE || !BG_AuthParsePublicLine( p, pub, comment, sizeof( comment ) ) ) {
			Com_Printf( S_COLOR_YELLOW AUTH_KEYS_FILE " line %i is not read: it has to be a rank (council, knight, instructor, rcon) and a line "
				"\"ssh-ed25519 <key> <comment>\"\n", lineNum );
			continue;
		}
		for ( i = 0; i < numAuthKeys; i++ ) {
			if ( !memcmp( authKeys[i].pub, pub, AUTH_PUBLIC_SIZE ) ) {
				break;
			}
		}
		if ( i < numAuthKeys ) {
			Com_Printf( S_COLOR_YELLOW AUTH_KEYS_FILE " line %i: the key is there twice, the first line counts\n", lineNum );
			continue;
		}
		if ( numAuthKeys == AUTH_MAX_KEYS ) {
			Com_Printf( S_COLOR_YELLOW AUTH_KEYS_FILE " has more than %i keys, the rest is not read\n", AUTH_MAX_KEYS );
			break;
		}
		memcpy( authKeys[numAuthKeys].pub, pub, AUTH_PUBLIC_SIZE );
		authKeys[numAuthKeys].adminLevel = adminLevel;
		Q_strncpyz( authKeys[numAuthKeys].comment, comment, sizeof( authKeys[0].comment ) );
		numAuthKeys++;
	}
}

void G_AuthInit( void ) {
	memset( authClients, 0, sizeof( authClients ) );
	memset( authCodes, 0, sizeof( authCodes ) );
	G_AuthLoadKeys();
	if ( numAuthKeys ) {
		Com_Printf( "%i admin keys from " AUTH_KEYS_FILE "\n", numAuthKeys );
	}
}

void G_AuthClientGone( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		memset( &authClients[clientNum], 0, sizeof( authClients[0] ) );
	}
}

// The rank of a public key, JPADM_NONE if admin_keys.txt does not have it. The web panel logs a browser in by
// the same file. comment: who it is, for the logs.
int G_AuthKeyLevel( const byte *pub, const char **comment ) {
	int i;

	for ( i = 0; i < numAuthKeys; i++ ) {
		if ( !memcmp( authKeys[i].pub, pub, AUTH_PUBLIC_SIZE ) ) {
			if ( comment ) {
				*comment = authKeys[i].comment;
			}
			return authKeys[i].adminLevel;
		}
	}
	return JPADM_NONE;
}

// a signature of that key over that message
qboolean G_AuthCheck( const byte *signature, const byte *pub, const byte *message, int length ) {
	return (qboolean)( crypto_ed25519_check( signature, pub, message, (size_t)length ) == 0 );
}

static qboolean G_AuthHostKnown( const char *address ) {
	char		list[MAX_CVAR_VALUE_STRING];
	const char	*p = list, *token;

	if ( !vs_authHosts.string[0] ) {
		return qtrue;		// the server was not told its addresses
	}
	Q_strncpyz( list, vs_authHosts.string, sizeof( list ) );
	while ( ( token = COM_ParseExt( &p, qfalse ) ) != NULL && token[0] ) {
		if ( !Q_stricmp( token, address ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
Cmd_JPAuth_f

The client command "vs_auth", only ever sent by a cgame of this tree
=================
*/
void Cmd_JPAuth_f( gentity_t *ent ) {
	const int		clientNum = ent - g_entities;
	const int		now = trap->Milliseconds();
	authClient_t	*ac;
	char			what[16], arg[AUTH_BASE64_SIZE + 8], text[AUTH_BASE64_SIZE];

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || trap->Argc() < 3 ) {
		return;
	}
	ac = &authClients[clientNum];
	trap->Argv( 1, what, sizeof( what ) );
	trap->Argv( 2, arg, sizeof( arg ) );

	if ( !Q_stricmp( what, "hello" ) ) {
		byte pub[AUTH_PUBLIC_SIZE];
		int i;

		ac->asked = qfalse;
		if ( G_AdminLoginBlocked( ent, qtrue ) ) {
			trap->SendServerCommand( clientNum, "vs_auth fail blocked" );
			return;
		}
		// a client with several keys names them one after the other, nobody needs to name hundreds
		if ( now - ac->helloTime > AUTH_NONCE_MSEC || now < ac->helloTime ) {
			ac->helloTime = now;
			ac->hellos = 0;
		}
		if ( ++ac->hellos > AUTH_HELLOS ) {
			trap->SendServerCommand( clientNum, "vs_auth fail busy" );
			return;
		}
		if ( !BG_AuthDecode( arg, pub, sizeof( pub ) ) ) {
			trap->SendServerCommand( clientNum, "vs_auth fail malformed" );
			return;
		}
		for ( i = 0; i < numAuthKeys; i++ ) {
			if ( !memcmp( authKeys[i].pub, pub, AUTH_PUBLIC_SIZE ) ) {
				break;
			}
		}
		if ( i == numAuthKeys ) {
			trap->SendServerCommand( clientNum, "vs_auth unknown" );
			return;
		}
		// The rank is kept over a change of the map, and the client of this tree asks again after each one: he
		// who has the rank of his key already gets nothing new by it and is not announced once more.
		if ( ent->client->sess.jpAdminLevel == (jpAdminLevel_t)authKeys[i].adminLevel ) {
			trap->SendServerCommand( clientNum, va( "vs_auth ok %s kept", G_AuthRankName( authKeys[i].adminLevel ) ) );
			return;
		}
		if ( !BG_AuthRandom( ac->nonce, sizeof( ac->nonce ) ) ) {
			Com_Printf( S_COLOR_RED "vs_auth: the system gave no random bytes, nobody can log in with a key\n" );
			trap->SendServerCommand( clientNum, "vs_auth fail server" );
			return;
		}
		ac->asked = qtrue;
		ac->key = i;
		ac->time = now;
		BG_AuthEncode( ac->nonce, sizeof( ac->nonce ), text, sizeof( text ) );
		trap->SendServerCommand( clientNum, va( "vs_auth nonce %s", text ) );
		return;
	}

	if ( !Q_stricmp( what, "sign" ) ) {
		byte		signature[AUTH_SIGNATURE_SIZE], message[AUTH_MESSAGE_SIZE];
		char		address[AUTH_ADDRESS_SIZE], fingerprint[64], how[160];
		const int	key = ac->key;
		int			length;

		trap->Argv( 3, address, sizeof( address ) );
		// a nonce is good for one answer, whatever the answer is
		if ( !ac->asked || now - ac->time > AUTH_NONCE_MSEC || now < ac->time || key < 0 || key >= numAuthKeys ) {
			ac->asked = qfalse;
			trap->SendServerCommand( clientNum, "vs_auth fail late" );
			return;
		}
		ac->asked = qfalse;
		if ( G_AdminLoginBlocked( ent, qtrue ) ) {
			trap->SendServerCommand( clientNum, "vs_auth fail blocked" );
			return;
		}
		length = BG_AuthMessage( message, sizeof( message ), ac->nonce, address, clientNum );
		memset( ac->nonce, 0, sizeof( ac->nonce ) );
		if ( !length || !BG_AuthDecode( arg, signature, sizeof( signature ) )
			|| !G_AuthCheck( signature, authKeys[key].pub, message, length ) ) {
			G_AdminLoginFailed( ent, "bad signature" );
			trap->SendServerCommand( clientNum, "vs_auth fail signature" );
			return;
		}
		// The signature is right, but it was made for a server of another address: somebody passes on what this
		// server asked, or vs_authHosts lacks the address this client came by.
		if ( !G_AuthHostKnown( address ) ) {
			G_SecurityLogPrintf( "vs_auth: %s [%s] signed for the address %s, which vs_authHosts does not have\n",
				ent->client->pers.netname, ent->client->sess.IP, address );
			G_AdminLoginFailed( ent, "signature for another server" );
			trap->SendServerCommand( clientNum, "vs_auth fail address" );
			return;
		}
		BG_AuthFingerprint( authKeys[key].pub, fingerprint, sizeof( fingerprint ) );
		Com_sprintf( how, sizeof( how ), "key %s (%s)", fingerprint, authKeys[key].comment );
		G_AdminLoginDone( ent, authKeys[key].adminLevel, qtrue, how );
		G_AdminLoginEffect( ent );
		trap->SendServerCommand( clientNum, va( "vs_auth ok %s", G_AuthRankName( authKeys[key].adminLevel ) ) );
		return;
	}
}

/*
=================
G_AuthNewCode

A code for one login with that rank. qfalse: the system gave no random bytes.
=================
*/
qboolean G_AuthNewCode( int adminLevel, char *out, int outSize ) {
	const int	now = trap->Milliseconds();
	byte		random[AUTH_CODE_LEN];
	authCode_t	*code = &authCodes[0];
	int			i;

	if ( outSize > 0 ) {
		out[0] = '\0';
	}
	if ( G_AuthRankLevel( G_AuthRankName( adminLevel ) ) == JPADM_NONE || !BG_AuthRandom( random, sizeof( random ) ) ) {
		return qfalse;
	}
	// a free place, or the oldest
	for ( i = 0; i < AUTH_MAX_CODES; i++ ) {
		if ( !authCodes[i].time || now - authCodes[i].time > AUTH_CODE_MSEC || now < authCodes[i].time ) {
			code = &authCodes[i];
			break;
		}
		if ( authCodes[i].time < code->time ) {
			code = &authCodes[i];
		}
	}
	for ( i = 0; i < AUTH_CODE_LEN; i++ ) {
		code->text[i] = AUTH_CODE_CHARS[random[i] & 31];
	}
	code->text[AUTH_CODE_LEN] = '\0';
	code->adminLevel = adminLevel;
	code->time = now ? now : 1;
	Q_strncpyz( out, code->text, outSize );
	return qtrue;
}

/*
=================
G_AuthTakeCode

What a player typed behind amlogin: the rank of the code it is, JPADM_NONE if it is none. A code is used up by
that.
=================
*/
int G_AuthTakeCode( const char *text ) {
	const int	now = trap->Milliseconds();
	int			i, adminLevel = JPADM_NONE;

	if ( strlen( text ) != AUTH_CODE_LEN ) {
		return JPADM_NONE;
	}
	// every code is looked at, whether one fitted already or not
	for ( i = 0; i < AUTH_MAX_CODES; i++ ) {
		authCode_t	*code = &authCodes[i];
		int			n, diff = 0;

		if ( !code->time ) {
			continue;
		}
		if ( now - code->time > AUTH_CODE_MSEC || now < code->time ) {
			memset( code, 0, sizeof( *code ) );
			continue;
		}
		for ( n = 0; n < AUTH_CODE_LEN; n++ ) {
			diff |= toupper( (unsigned char)text[n] ) ^ code->text[n];
		}
		if ( !diff ) {
			adminLevel = code->adminLevel;
			memset( code, 0, sizeof( *code ) );
		}
	}
	return adminLevel;
}

/*
=================
G_AuthConsoleCommand

On the console of the server: "amkeys" reads admin_keys.txt again and lists it, "amcode <rank>" makes a code.
=================
*/
qboolean G_AuthConsoleCommand( const char *cmd ) {
	if ( !Q_stricmp( cmd, "amkeys" ) ) {
		int i;

		G_AuthLoadKeys();
		Com_Printf( "%i admin keys in " AUTH_KEYS_FILE "%s\n", numAuthKeys,
			numAuthKeys ? ":" : " (a line is: <rank> ssh-ed25519 <key> <comment>, the rank council, knight, instructor or rcon)" );
		for ( i = 0; i < numAuthKeys; i++ ) {
			char fingerprint[64];

			BG_AuthFingerprint( authKeys[i].pub, fingerprint, sizeof( fingerprint ) );
			Com_Printf( "  %-10s %s  %s\n", G_AuthRankName( authKeys[i].adminLevel ), fingerprint, authKeys[i].comment );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "amcode" ) ) {
		char rank[16], code[AUTH_CODE_LEN + 1];
		int adminLevel;

		trap->Argv( 1, rank, sizeof( rank ) );
		adminLevel = G_AuthRankLevel( rank );
		if ( adminLevel == JPADM_NONE ) {
			Com_Printf( "usage: amcode <council|knight|instructor|rcon>: a code for one login with that rank, typed as \"amlogin <code>\" within a minute\n" );
		} else if ( !G_AuthNewCode( adminLevel, code, sizeof( code ) ) ) {
			Com_Printf( "the system gave no random bytes, no code was made\n" );
		} else {
			Com_Printf( "amlogin %s    (%s, once, for %i seconds)\n", code, rank, AUTH_CODE_MSEC / 1000 );
		}
		return qtrue;
	}
	return qfalse;
}

// for the web panel: the keys of admin_keys.txt, 0 to G_AuthNumKeys() - 1
int G_AuthNumKeys( void ) {
	return numAuthKeys;
}

qboolean G_AuthKeyInfo( int index, int *adminLevel, char *fingerprint, int fingerprintSize, const char **comment ) {
	if ( index < 0 || index >= numAuthKeys ) {
		return qfalse;
	}
	*adminLevel = authKeys[index].adminLevel;
	BG_AuthFingerprint( authKeys[index].pub, fingerprint, fingerprintSize );
	*comment = authKeys[index].comment;
	return qtrue;
}
