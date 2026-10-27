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

// cg_auth.c -- admin login with a key instead of a password. Not from JA+. The protocol is in
// game/auth/bg_auth.h, the server part in game/auth/g_auth.c.
//
//   v_amkey new [comment]   makes a key and writes it to a file only the player can read (v_authKeyFile, by
//                         default admin_ed25519 in the home folder of the game), in the format of OpenSSH
//   v_amkey show            prints the public lines of the keys there are: the owner of a server puts one into
//                         his admin_keys.txt
//   v_amkey login           logs in on this server, and from then on by itself whenever the player joins it
//   v_amkey forget          no longer by itself on this server, and its host key is forgotten
//   v_amkey trust           a login that keeps the host key the server sends now, even if it is another one
//                         than before or one kept for another address
//
// Where the secret half of a key is: in the key file, or in an ssh-agent (v_authAgent 1), which signs without
// handing it out. 1Password is such an agent. It may ask the player first, so everything here is a step that
// is looked at once a frame (auth.step) and nothing waits.
//
// The client signs only for a server the player named with "v_amkey login" (v_authServers), and only right after
// it asked itself: a server can not make it sign by sending a nonce nobody asked for. That, the address in the
// signed message and the host key the server sends with the nonce are against a server that passes on the
// question of another one: the host key of each address is kept in admin_hosts.txt the first time, and a login
// that comes with another one is not signed (bg_auth.h). A server without a host key (version 1) only gets a
// signature when the player typed the login.
//
// Needs no engine of this tree: client commands, a file and a socket. Not in a browser.

#include "cgame/cg_local.h"
#include "game/auth/bg_auth.h"
#include "cgame/auth/cg_auth_agent.h"

#if !defined(__EMSCRIPTEN__)

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
	#include <io.h>
#else
	#include <unistd.h>
#endif

#include "monocypher.h"
#include "monocypher-ed25519.h"

#define AUTH_MAX_CANDIDATES		8
#define AUTH_RESEND_MSEC		3000		// no answer by then: the server dropped the command (Auth_FloodGap), once more
#define AUTH_SENDS				3			// a command goes out that often, then the server does not answer
#define AUTH_FLOOD_MARGIN		300			// on top of the gap of the server: packets do not arrive as evenly as they leave
#define AUTH_AGENT_MSEC			120000		// the player may have to unlock his agent and say yes
#define AUTH_KEY_NAME			"admin_ed25519"
#define AUTH_HOSTS_NAME			"admin_hosts.txt"		// next to it in the home folder of the game

typedef enum authStep_e {
	AUTH_IDLE,
	AUTH_AGENT_KEYS,		// the agent was asked which keys it has
	AUTH_HELLO,				// the server was told a key
	AUTH_AGENT_SIGN,		// the agent was asked to sign
	AUTH_SIGN				// the server was sent the signature
} authStep_t;

typedef struct authCandidate_s {
	byte		pub[AUTH_PUBLIC_SIZE];
	qboolean	fromAgent;
	char		comment[AGENT_COMMENT_SIZE];
} authCandidate_t;

static struct {
	authStep_t		step;
	int				stepTime;			// trap->Milliseconds() when it began
	qboolean		quiet;				// by itself after joining: says nothing unless it works
	qboolean		showOnly;			// "v_amkey show": the keys of the agent are listed, nobody logs in
	qboolean		joined;				// the automatic login of this map was looked at
	qboolean		again;				// the nonce ran out while the agent waited for the player: once more
	qboolean		trust;				// "v_amkey trust": the host key the server sends is kept, whatever was before
	qboolean		withHost;			// the server sent its host key, the signature is of version 2

	authCandidate_t	keys[AUTH_MAX_CANDIDATES];
	int				numKeys, current;
	byte			secret[AUTH_SECRET_SIZE];		// of the key file, while a login runs
	qboolean		haveSecret;
	byte			message[AUTH_MESSAGE_SIZE];
	int				messageLen;
	int				nonceTime;
	char			address[AUTH_ADDRESS_SIZE];

	char			command[AUTH_BASE64_SIZE + AUTH_ADDRESS_SIZE + 16];	// the "vs_auth ..." of the step
	int				sends;				// how often it went out
	int				sendTime;			// trap->Milliseconds() of the last "vs_auth" that went out, 0: none yet
} auth;

static void Auth_Say( const char *fmt, ... ) {
	va_list	args;
	char	text[512];

	if ( auth.quiet ) {
		return;
	}
	va_start( args, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, args );
	va_end( args );
	trap->Print( va( "%s\n", text ) );
}

static void Auth_End( void ) {
	CG_AgentClose();
	crypto_wipe( auth.secret, sizeof( auth.secret ) );
	crypto_wipe( auth.message, sizeof( auth.message ) );
	auth.haveSecret = qfalse;
	auth.step = AUTH_IDLE;
	auth.quiet = auth.showOnly = auth.again = auth.trust = auth.withHost = qfalse;
	auth.numKeys = auth.current = 0;
}

static void Auth_Step( authStep_t step ) {
	auth.step = step;
	auth.stepTime = trap->Milliseconds();
}

/*
===============================================================================

The key file

===============================================================================
*/

static void Auth_KeyPath( char *out, int outSize ) {
	char		home[MAX_OSPATH];
	const char	*file = v_authKeyFile.string;

	if ( file[0] == '~' && ( file[1] == '/' || file[1] == '\\' ) ) {
#if defined(_WIN32)
		const char *user = getenv( "USERPROFILE" );
#else
		const char *user = getenv( "HOME" );
#endif
		Com_sprintf( out, outSize, "%s%s", user ? user : "", file + 1 );
		return;
	}
	if ( file[0] ) {
		Q_strncpyz( out, file, outSize );
		return;
	}
	trap->Cvar_VariableStringBuffer( "fs_homepath", home, sizeof( home ) );
	if ( home[0] ) {
		Com_sprintf( out, outSize, "%s/" AUTH_KEY_NAME, home );
	} else {
		Q_strncpyz( out, AUTH_KEY_NAME, outSize );
	}
}

// 1: the key of the file is in secret. 0: there is no file. -1: there is one and it can not be used, why says so.
static int Auth_ReadKey( byte *secret, char *comment, int commentSize, char *why, int whySize ) {
	static char	text[AUTH_FILE_SIZE];
	char		path[MAX_OSPATH];
	byte		seed[32], pub[AUTH_PUBLIC_SIZE];
	FILE		*f;
	size_t		n;
	qboolean	ok;

	Auth_KeyPath( path, sizeof( path ) );
	f = fopen( path, "rb" );
	if ( !f ) {
		Com_sprintf( why, whySize, "there is no key file %s", path );
		return 0;
	}
	n = fread( text, 1, sizeof( text ) - 1, f );
	fclose( f );
	text[n] = '\0';
	ok = BG_AuthReadKeyFile( text, secret, comment, commentSize, why, whySize );
	crypto_wipe( text, sizeof( text ) );
	if ( !ok ) {
		char reason[160];

		Q_strncpyz( reason, why, sizeof( reason ) );
		Com_sprintf( why, whySize, "%s: %s", path, reason );
		return -1;
	}
	// The public half is made from the seed, whatever the file says it is: a signature made with a public half
	// that does not belong to the seed gives the seed away.
	memcpy( seed, secret, sizeof( seed ) );
	crypto_ed25519_key_pair( secret, pub, seed );
	return 1;
}

/*
===============================================================================

admin_hosts.txt: the host key of every server address the client logged in on

===============================================================================
*/

static void Auth_HostsPath( char *out, int outSize ) {
	char home[MAX_OSPATH];

	trap->Cvar_VariableStringBuffer( "fs_homepath", home, sizeof( home ) );
	if ( home[0] ) {
		Com_sprintf( out, outSize, "%s/" AUTH_HOSTS_NAME, home );
	} else {
		Q_strncpyz( out, AUTH_HOSTS_NAME, outSize );
	}
}

// the file into text, "" when there is none
static void Auth_ReadHosts( char *text, int textSize ) {
	char	path[MAX_OSPATH];
	FILE	*f;
	size_t	n;

	Auth_HostsPath( path, sizeof( path ) );
	text[0] = '\0';
	f = fopen( path, "rb" );
	if ( !f ) {
		return;
	}
	n = fread( text, 1, (size_t)textSize - 1, f );
	fclose( f );
	text[n] = '\0';
}

// the line of the address set to this host key, or taken away (host NULL)
static qboolean Auth_SetHost( const char *address, const byte *host ) {
	static char	text[AUTH_HOSTS_FILE_SIZE], out[AUTH_HOSTS_FILE_SIZE];
	char		path[MAX_OSPATH];
	int			len;
	FILE		*f;

	Auth_ReadHosts( text, sizeof( text ) );
	len = BG_AuthHostsSet( text, address, host, out, sizeof( out ) );
	Auth_HostsPath( path, sizeof( path ) );
	f = fopen( path, "wb" );
	if ( !f ) {
		return qfalse;
	}
	if ( fwrite( out, 1, (size_t)len, f ) != (size_t)len ) {
		fclose( f );
		return qfalse;
	}
	fclose( f );
	return qtrue;
}

static void Auth_PrintPublic( const byte *pub, const char *comment, const char *where ) {
	char line[AUTH_LINE_SIZE], fingerprint[64];

	BG_AuthPublicLine( pub, comment, line, sizeof( line ) );
	BG_AuthFingerprint( pub, fingerprint, sizeof( fingerprint ) );
	trap->Print( va( S_COLOR_CYAN "%s" S_COLOR_WHITE " (%s):\n%s\n", where, fingerprint, line ) );
}

/*
=================
Auth_New

"v_amkey new [comment]"
=================
*/
static void Auth_New( void ) {
	static char	text[AUTH_FILE_SIZE];
	char		path[MAX_OSPATH], pubPath[MAX_OSPATH], comment[48], line[AUTH_LINE_SIZE + 2];
	byte		seed[32], secret[AUTH_SECRET_SIZE], pub[AUTH_PUBLIC_SIZE];
	const char	*arg = CG_Argv( 2 );
	int			len, fd, n = 0;
	FILE		*f;

	// a comment says whose key it is in the admin_keys.txt of a server: letters, digits and a few signs
	for ( ; *arg && n < (int)sizeof( comment ) - 1; arg++ ) {
		if ( ( *arg >= 'a' && *arg <= 'z' ) || ( *arg >= 'A' && *arg <= 'Z' ) || ( *arg >= '0' && *arg <= '9' ) || strchr( "-_.@", *arg ) ) {
			comment[n++] = *arg;
		}
	}
	comment[n] = '\0';
	if ( !n ) {
		Q_strncpyz( comment, "visionjk-admin", sizeof( comment ) );
	}

	Auth_KeyPath( path, sizeof( path ) );
	if ( !BG_AuthRandom( seed, sizeof( seed ) ) ) {
		trap->Print( "The system gave no random bytes, no key was made.\n" );
		return;
	}
	crypto_ed25519_key_pair( secret, pub, seed );		// the seed is wiped by it
	len = BG_AuthWriteKeyFile( secret, comment, text, sizeof( text ) );
	crypto_wipe( secret, sizeof( secret ) );
	if ( !len ) {
		trap->Print( "The key could not be written down.\n" );
		return;
	}

	// only made if it is not there, and for the player alone to read
#if defined(_WIN32)
	fd = _open( path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE );
#else
	fd = open( path, O_WRONLY | O_CREAT | O_EXCL, 0600 );
#endif
	if ( fd < 0 ) {
		crypto_wipe( text, sizeof( text ) );
		if ( errno == EEXIST ) {
			trap->Print( va( "There is a key file already: %s\nIt is kept. \"v_amkey show\" prints its public line, another file is named with v_authKeyFile.\n", path ) );
		} else {
			trap->Print( va( "The key file %s could not be made.\n", path ) );
		}
		return;
	}
#if defined(_WIN32)
	n = _write( fd, text, (unsigned int)len );
	_close( fd );
#else
	n = (int)write( fd, text, (size_t)len );
	close( fd );
#endif
	crypto_wipe( text, sizeof( text ) );
	if ( n != len ) {
		trap->Print( va( "The key file %s could not be written, delete what is there of it.\n", path ) );
		return;
	}

	// the public line next to it, as ssh-keygen leaves it
	BG_AuthPublicLine( pub, comment, line, sizeof( line ) - 2 );
	Com_sprintf( pubPath, sizeof( pubPath ), "%s.pub", path );
	f = fopen( pubPath, "wb" );
	if ( f ) {
		fputs( line, f );
		fputs( "\n", f );
		fclose( f );
	}

	trap->Print( va( "A new key is in %s\nNobody else may read that file. Its public line is no secret, the owner of a server puts it into his admin_keys.txt behind your rank:\n", path ) );
	Auth_PrintPublic( pub, comment, "public line" );
	trap->Print( "Then \"v_amkey login\" on that server.\n" );
}

/*
===============================================================================

The servers the player logs in to by himself

===============================================================================
*/

static void Auth_Address( char *out, int outSize ) {
	// the address as numbers where the engine has it (OpenJK and this tree), else as the player typed it
	trap->Cvar_VariableStringBuffer( "cl_currentServerIP", out, outSize );
	if ( !out[0] ) {
		trap->Cvar_VariableStringBuffer( "cl_currentServerAddress", out, outSize );
	}
}

static qboolean Auth_ServerListed( const char *address ) {
	char		list[MAX_CVAR_VALUE_STRING];
	const char	*p = list, *token;

	Q_strncpyz( list, v_authServers.string, sizeof( list ) );
	while ( ( token = COM_ParseExt( &p, qfalse ) ) != NULL && token[0] ) {
		if ( !Q_stricmp( token, address ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// add: the address is in the list afterwards (the oldest ones go when there is no room), else it is not
static void Auth_ServerSet( const char *address, qboolean add ) {
	char		list[MAX_CVAR_VALUE_STRING], out[MAX_CVAR_VALUE_STRING];
	const char	*p = list, *token;

	Q_strncpyz( list, v_authServers.string, sizeof( list ) );
	out[0] = '\0';
	while ( ( token = COM_ParseExt( &p, qfalse ) ) != NULL && token[0] ) {
		if ( Q_stricmp( token, address ) ) {
			Q_strcat( out, sizeof( out ), token );
			Q_strcat( out, sizeof( out ), " " );
		}
	}
	if ( add ) {
		while ( out[0] && strlen( out ) + strlen( address ) >= sizeof( out ) - 1 ) {
			char *space = strchr( out, ' ' );

			memmove( out, space ? space + 1 : out + strlen( out ), space ? strlen( space + 1 ) + 1 : 1 );
		}
		Q_strcat( out, sizeof( out ), address );
	}
	else if ( out[0] ) {
		out[strlen( out ) - 1] = '\0';
	}
	trap->Cvar_Set( "v_authServers", out );
	trap->Cvar_Update( &v_authServers );
}

/*
===============================================================================

The login

===============================================================================
*/

static qboolean Auth_ServerKnows( void ) {
	return (qboolean)( ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vs_caps" ) ) & AUTH_CAPS_BIT ) != 0 );
}

/*
=================
Auth_FloodGap

A server with flood protection drops a client command that comes within a second of the one before and says
nothing (sv_floodProtect in SV_ClientCommand, every engine since Quake 3). The server answers a "vs_auth" much
faster than that, so the next one would always be dropped. The cvar is in the serverinfo: 0 is off, 1 is a
second, any other number is msec in the engine of this tree.
=================
*/
static int Auth_FloodGap( void ) {
	const char	*value = Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "sv_floodProtect" );
	int			gap = atoi( value );

	if ( !value[0] || gap == 1 ) {
		gap = 1000;
	}
	return ( gap > 0 ) ? gap + AUTH_FLOOD_MARGIN : 0;
}

// A "vs_auth" for the server. It goes out in CG_AuthFrame, as soon as the server takes one again.
static void Auth_Send( authStep_t step, const char *command ) {
	Q_strncpyz( auth.command, command, sizeof( auth.command ) );
	auth.sends = 0;
	Auth_Step( step );
}

// the server is told the next key the player has
static void Auth_Hello( void ) {
	char text[AUTH_BASE64_SIZE];

	if ( auth.current == 1 && auth.numKeys > 1 && Auth_FloodGap() ) {
		Auth_Say( "The server does not know your first key, asking for the other %i one after the other "
			"(v_authAgentKey names the right one)...", auth.numKeys - 1 );
	}
	if ( auth.current >= auth.numKeys ) {
		if ( !auth.numKeys ) {
			Auth_Say( "You have no key. \"v_amkey new\" makes one, or set v_authAgent 1 to use the keys of your ssh-agent." );
		} else {
			Auth_Say( "This server has none of your %i keys in its admin_keys.txt. \"v_amkey show\" prints the lines to give to its owner.", auth.numKeys );
		}
		Auth_End();
		return;
	}
	BG_AuthEncode( auth.keys[auth.current].pub, AUTH_PUBLIC_SIZE, text, sizeof( text ) );
	Auth_Send( AUTH_HELLO, va( "vs_auth hello %s", text ) );
}

static void Auth_SendSignature( const byte *signature ) {
	char text[AUTH_BASE64_SIZE];

	BG_AuthEncode( signature, AUTH_SIGNATURE_SIZE, text, sizeof( text ) );
	Auth_Send( AUTH_SIGN, va( auth.withHost ? "vs_auth sign %s %s 2" : "vs_auth sign %s %s", text, auth.address ) );
}

// the keys of the agent join the one of the file, if v_authAgentKey lets them
static void Auth_AddAgentKeys( void ) {
	static agentKey_t	keys[AGENT_MAX_KEYS];
	int					total;
	const int			count = CG_AgentKeys( keys, AGENT_MAX_KEYS, &total );
	int					i, k;

	// 1Password hands out RSA keys as well, and the server only checks Ed25519: without this line the agent
	// looks as if it was never asked
	if ( !count && total ) {
		Auth_Say( "Your ssh-agent has %i key%s, but no Ed25519 key: this login takes no other kind (no RSA, no ECDSA).",
			total, total == 1 ? "" : "s" );
	} else if ( !total ) {
		Auth_Say( "Your ssh-agent has no keys." );
	}
	for ( i = 0; i < count && auth.numKeys < AUTH_MAX_CANDIDATES; i++ ) {
		char line[AUTH_LINE_SIZE];

		// a part of its comment or of its public line picks one
		BG_AuthPublicLine( keys[i].pub, keys[i].comment, line, sizeof( line ) );
		if ( v_authAgentKey.string[0] && !Q_stristr( line, v_authAgentKey.string ) ) {
			continue;
		}
		for ( k = 0; k < auth.numKeys; k++ ) {
			if ( !memcmp( auth.keys[k].pub, keys[i].pub, AUTH_PUBLIC_SIZE ) ) {
				break;
			}
		}
		if ( k < auth.numKeys ) {
			continue;		// the key of the file is in the agent as well
		}
		memcpy( auth.keys[auth.numKeys].pub, keys[i].pub, AUTH_PUBLIC_SIZE );
		auth.keys[auth.numKeys].fromAgent = qtrue;
		Q_strncpyz( auth.keys[auth.numKeys].comment, keys[i].comment, sizeof( auth.keys[0].comment ) );
		auth.numKeys++;
	}
}

/*
=================
Auth_Start

Gathers the keys there are and begins: with the agent if it is to be asked, else with the server (login) or
with the list (show).
=================
*/
static void Auth_Start( qboolean quiet, qboolean showOnly ) {
	char why[256], comment[AGENT_COMMENT_SIZE];
	int found;

	if ( auth.step != AUTH_IDLE ) {
		if ( !quiet ) {
			trap->Print( "A login is on its way already.\n" );
		}
		return;
	}
	Auth_End();
	auth.quiet = quiet;
	auth.showOnly = showOnly;

	found = Auth_ReadKey( auth.secret, comment, sizeof( comment ), why, sizeof( why ) );
	if ( found > 0 ) {
		memcpy( auth.keys[0].pub, auth.secret + AUTH_PUBLIC_SIZE, AUTH_PUBLIC_SIZE );
		auth.keys[0].fromAgent = qfalse;
		Q_strncpyz( auth.keys[0].comment, comment, sizeof( auth.keys[0].comment ) );
		auth.numKeys = 1;
		auth.haveSecret = qtrue;
	} else if ( found < 0 || ( showOnly && !v_authAgent.integer ) ) {
		Auth_Say( "%s", why );
	}

	if ( v_authAgent.integer ) {
		if ( CG_AgentRequestKeys( v_authAgentPath.string, why, sizeof( why ) ) ) {
			Auth_Step( AUTH_AGENT_KEYS );
			return;
		}
		Auth_Say( "ssh-agent: %s", why );
	}
	if ( showOnly ) {
		if ( auth.numKeys ) {
			Auth_PrintPublic( auth.keys[0].pub, auth.keys[0].comment, "key file" );
		}
		Auth_End();
		return;
	}
	Auth_Hello();
}

// The server said this client is an admin by its key, in this level: the card of a player then offers what an
// admin can do with him (select/cg_select.c).
static qboolean authIsAdmin;

qboolean CG_AuthIsAdmin( void ) {
	return authIsAdmin;
}

/*
=================
CG_AuthServerCommand

"vs_auth ..." from the server
=================
*/
void CG_AuthServerCommand( void ) {
	const char *what = CG_Argv( 1 );

	// only as an answer: a server that sends one by itself gets nothing signed. Before the command of the step
	// went out it can only be a second answer to the one before, which was sent twice.
	if ( ( auth.step != AUTH_HELLO && auth.step != AUTH_SIGN ) || !auth.sends ) {
		return;
	}

	if ( auth.step == AUTH_HELLO && !Q_stricmp( what, "unknown" ) ) {
		auth.current++;
		Auth_Hello();
		return;
	}

	if ( auth.step == AUTH_HELLO && !Q_stricmp( what, "nonce" ) ) {
		const authCandidate_t	*key = &auth.keys[auth.current];
		byte					nonce[AUTH_NONCE_SIZE], signature[AUTH_SIGNATURE_SIZE];
		char					why[256];

		byte					host[AUTH_HOST_SIZE];

		if ( !BG_AuthDecode( CG_Argv( 2 ), nonce, sizeof( nonce ) ) ) {
			Auth_Say( "The server sent something that is no question for a key." );
			Auth_End();
			return;
		}
		auth.withHost = (qboolean)( trap->Cmd_Argc() > 3 && BG_AuthDecode( CG_Argv( 3 ), host, sizeof( host ) ) );
		if ( !auth.withHost ) {
			// version 1: by itself the client signs only where the server says who it is
			if ( auth.quiet ) {
				Auth_End();
				return;
			}
		} else {
			static char		text[AUTH_HOSTS_FILE_SIZE];
			char			other[AUTH_ADDRESS_SIZE];
			authHostState_t	state;

			Auth_ReadHosts( text, sizeof( text ) );
			state = BG_AuthHostsFind( text, auth.address, host, other, sizeof( other ) );
			if ( state == AUTH_HOST_CHANGED && !auth.trust ) {
				auth.quiet = qfalse;		// also after joining: the player has to know why he is no admin
				Auth_Say( "Not signed: the server at %s has another host key than the last time you logged in there. It may be "
					"a fake server. If its owner says it was set up again, \"v_amkey trust\" logs in and keeps the new key.", auth.address );
				Auth_End();
				return;
			}
			if ( state == AUTH_HOST_ELSEWHERE && !auth.trust ) {
				auth.quiet = qfalse;
				Auth_Say( "Not signed: the server at %s has the host key of the server at %s, where you logged in before. It may be "
					"a fake server. If it is the same server with a second address, \"v_amkey trust\" logs in.", auth.address, other );
				Auth_End();
				return;
			}
			if ( state != AUTH_HOST_SAME && !Auth_SetHost( auth.address, host ) ) {
				Auth_Say( "The host key of the server could not be saved to " AUTH_HOSTS_NAME ", so nothing was signed." );
				Auth_End();
				return;
			}
		}
		auth.messageLen = BG_AuthMessage( auth.message, sizeof( auth.message ), nonce, auth.withHost ? host : NULL, auth.address, cg.clientNum );
		auth.nonceTime = trap->Milliseconds();
		if ( !auth.messageLen ) {
			Auth_End();
			return;
		}
		if ( !key->fromAgent ) {
			if ( !auth.haveSecret ) {
				Auth_End();
				return;
			}
			crypto_ed25519_sign( signature, auth.secret, auth.message, (size_t)auth.messageLen );
			Auth_SendSignature( signature );
			return;
		}
		if ( !CG_AgentRequestSign( v_authAgentPath.string, key->pub, auth.message, auth.messageLen, why, sizeof( why ) ) ) {
			Auth_Say( "ssh-agent: %s", why );
			Auth_End();
			return;
		}
		Auth_Say( "Waiting for your ssh-agent to sign (it may ask you)..." );
		Auth_Step( AUTH_AGENT_SIGN );
		return;
	}

	if ( !Q_stricmp( what, "ok" ) ) {
		authIsAdmin = qtrue;
		// said even after a login that ran by itself, but not after every map: "kept" is the rank from before
		if ( !auth.quiet || Q_stricmp( CG_Argv( 3 ), "kept" ) ) {
			trap->Print( va( "Logged in with your key as admin (%s).\n", CG_Argv( 2 ) ) );
		}
		Auth_End();
		return;
	}

	if ( !Q_stricmp( what, "fail" ) ) {
		const char *why = CG_Argv( 2 );

		if ( !Q_stricmp( why, "late" ) && auth.again ) {
			// the agent is unlocked by now and answers at once
			auth.again = qfalse;
			Auth_Hello();
			return;
		}
		if ( !Q_stricmp( why, "address" ) ) {
			Auth_Say( "The server refused: it does not know itself by the address %s you reach it by (its vs_authHosts lacks it).", auth.address );
		} else if ( !Q_stricmp( why, "version" ) ) {
			Auth_Say( "The server refused: the signature has no host key in it, and this server takes that only when its addresses are set (vs_authHosts)." );
		} else if ( !Q_stricmp( why, "blocked" ) ) {
			Auth_Say( "The server refused: too many wrong tries, or admin-banned." );
		} else if ( !Q_stricmp( why, "late" ) ) {
			Auth_Say( "The answer came too late for the server. Type \"v_amkey login\" again." );
		} else if ( !Q_stricmp( why, "signature" ) ) {
			Auth_Say( "The server refused the signature." );
		} else {
			Auth_Say( "The server refused the login (%s).", why );
		}
		Auth_End();
		return;
	}
}

/*
=================
CG_AuthFrame

Once a frame: the automatic login after joining, and the step that waits
=================
*/
void CG_AuthFrame( void ) {
	const int now = trap->Milliseconds();

	if ( !cg.snap || cg.demoPlayback ) {
		return;
	}

	if ( !auth.joined ) {
		char address[AUTH_ADDRESS_SIZE];

		auth.joined = qtrue;
		Auth_Address( address, sizeof( address ) );
		if ( auth.step == AUTH_IDLE && address[0] && Auth_ServerKnows() && Auth_ServerListed( address ) ) {
			Q_strncpyz( auth.address, address, sizeof( auth.address ) );
			Auth_Start( qtrue, qfalse );
		}
		return;
	}

	switch ( auth.step ) {
	case AUTH_AGENT_KEYS:
	case AUTH_AGENT_SIGN: {
		const agentState_t state = CG_AgentPoll();

		if ( state == AGENT_BUSY ) {
			if ( now - auth.stepTime > AUTH_AGENT_MSEC ) {
				Auth_Say( "Your ssh-agent did not answer." );
				Auth_End();
			}
			return;
		}
		if ( state != AGENT_DONE ) {
			Auth_Say( "Your ssh-agent closed the line without an answer." );
			if ( auth.step == AUTH_AGENT_KEYS && auth.numKeys && !auth.showOnly ) {
				CG_AgentClose();
				Auth_Hello();		// the key of the file is still there
				return;
			}
			Auth_End();
			return;
		}
		if ( auth.step == AUTH_AGENT_KEYS ) {
			Auth_AddAgentKeys();
			CG_AgentClose();
			if ( auth.showOnly ) {
				int i;

				for ( i = 0; i < auth.numKeys; i++ ) {
					Auth_PrintPublic( auth.keys[i].pub, auth.keys[i].comment, auth.keys[i].fromAgent ? "ssh-agent" : "key file" );
				}
				if ( !auth.numKeys ) {
					trap->Print( "Your ssh-agent has no Ed25519 key (or none that v_authAgentKey lets through), and there is no key file.\n" );
				}
				Auth_End();
				return;
			}
			Auth_Hello();
			return;
		} else {
			byte signature[AUTH_SIGNATURE_SIZE];

			if ( !CG_AgentSignature( signature ) ) {
				Auth_Say( "Your ssh-agent did not sign (not allowed, or the key is gone)." );
				Auth_End();
				return;
			}
			CG_AgentClose();
			// The player took his time with the agent and the question of the server is too old by now: it is
			// sent anyway, and when the server says so the whole thing runs once more.
			auth.again = (qboolean)( now - auth.nonceTime > AUTH_NONCE_MSEC - 2000 );
			Auth_SendSignature( signature );
		}
		return;
	}
	case AUTH_HELLO:
	case AUTH_SIGN:
		if ( auth.sends && now - auth.sendTime <= AUTH_RESEND_MSEC ) {
			return;		// the answer is on its way
		}
		// Sent and nothing came back: another command of the player was too close to it (chat, a vote), and
		// the server dropped this one.
		if ( auth.sends == AUTH_SENDS ) {
			Auth_Say( "The server did not answer the login." );
			Auth_End();
			return;
		}
		if ( auth.sendTime && now - auth.sendTime < Auth_FloodGap() ) {
			return;
		}
		trap->SendClientCommand( auth.command );
		auth.sends++;
		auth.sendTime = now;
		return;
	default:
		return;
	}
}

// a new map, or another server
void CG_AuthInit( void ) {
	Auth_End();
	auth.joined = qfalse;
	authIsAdmin = qfalse;
}

/*
=================
CG_AuthCommand_f

The console command "v_amkey"
=================
*/
void CG_AuthCommand_f( void ) {
	const char	*what = CG_Argv( 1 );
	char		address[AUTH_ADDRESS_SIZE], path[MAX_OSPATH];

	Auth_Address( address, sizeof( address ) );

	if ( !Q_stricmp( what, "new" ) ) {
		Auth_New();
		return;
	}
	if ( !Q_stricmp( what, "show" ) ) {
		Auth_Start( qfalse, qtrue );
		return;
	}
	if ( !Q_stricmp( what, "forget" ) ) {
		Auth_ServerSet( address, qfalse );
		Auth_SetHost( address, NULL );
		trap->Print( va( "No login by itself on %s any more, and its host key is forgotten.\n", address ) );
		return;
	}
	if ( !Q_stricmp( what, "login" ) || !Q_stricmp( what, "trust" ) ) {
		if ( !cg.snap || !address[0] ) {
			return;
		}
		if ( !Auth_ServerKnows() ) {
			trap->Print( "This server has no login with a key (it does not run the game module of VisionJK, or an older one).\n" );
			return;
		}
		if ( auth.step != AUTH_IDLE ) {
			trap->Print( "A login is on its way already.\n" );
			return;
		}
		// from now on the client signs for this server, also by itself when the player joins it
		Auth_ServerSet( address, qtrue );
		Q_strncpyz( auth.address, address, sizeof( auth.address ) );
		Auth_Start( qfalse, qfalse );
		auth.trust = (qboolean)!Q_stricmp( what, "trust" );	// after Auth_Start, which clears it: the nonce comes later
		return;
	}

	Auth_KeyPath( path, sizeof( path ) );
	trap->Print( va( "Admin login with a key instead of a password.\n"
		"  v_amkey new [comment]   make a key (file %s)\n"
		"  v_amkey show            print the public lines of your keys, one of them goes into the admin_keys.txt of a server\n"
		"  v_amkey login           log in on this server, and by itself from now on when you join it\n"
		"  v_amkey forget          no login by itself on this server any more, and forget its host key\n"
		"  v_amkey trust           log in and keep the host key the server sends now, even if it is another one than before\n"
		"v_authAgent %i (1: the keys of your ssh-agent are used too, 1Password is one), by itself on: %s\n",
		path, v_authAgent.integer, v_authServers.string[0] ? v_authServers.string : "no server" ) );
}

#else	// __EMSCRIPTEN__

void CG_AuthInit( void ) {}
qboolean CG_AuthIsAdmin( void ) { return qfalse; }
void CG_AuthFrame( void ) {}
void CG_AuthServerCommand( void ) {}

void CG_AuthCommand_f( void ) {
	trap->Print( "The login with a key is not there in a browser: a page has no key file and no way to an ssh-agent.\n" );
}

#endif
