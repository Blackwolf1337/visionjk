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

// wa_auth.c -- who may use the web admin panel, and how much of it. Not in JKAdmin.
//
// The panel had one password for everybody, sent with every request (HTTP basic auth). Now it has accounts:
//
// - An account is a name, a role and a password, kept as an Argon2id hash in webadmin_users.dat of the game
//   folder. The roles are moderator (the players: kick, ban, team and the like), admin (also the editor, the
//   map, the log) and owner (also the console of the server, its config, its files and the accounts). The
//   first owner is made from vs_webPassword once, or with "webuser add <name> owner" on the console.
// - A login gives a session: 32 random bytes in a cookie the scripts of the page can not read and no other
//   site gets sent (HttpOnly, SameSite=Strict, Secure behind a proxy that speaks HTTPS). It ends after 30
//   minutes without a request and after 12 hours at the latest. The server keeps the SHA-256 of the cookie,
//   also over a change of the map (webadmin_sessions.dat), so nobody is thrown out by one.
// - A second step, if the account has one: a code by time (wa_totp.c), or one of eight recovery codes.
// - A script gets a token of its own account in place of a session ("Authorization: Bearer <token>").
// - A browser can log in with a key instead: it makes an Ed25519 key it can not hand out (WebCrypto), its
//   public line goes into admin_keys.txt, and it signs what the server asks (the same file and the same check
//   as the login in the game, auth/g_auth.c). The rank of the key is the role: rcon owner, council admin,
//   knight and instructor moderator.
//
// A password is hashed with 12 MiB and three passes, which takes some 20 ms. The login hashes it on a thread of
// its own beside the game (anybody can try a login, and a frame must not wait for it); at most one is hashed in a
// second for the whole server (a second try gets "busy"), and guessing runs into the wait of its address
// (WA_AuthThrottle). A wrong code of the second step makes the account wait as well; a wrong password does not,
// or anybody who knows the name of an account could keep its owner out.

#include <time.h>
#ifndef _WIN32
	#include <pthread.h>
#endif

#include "game/webadmin/wa_local.h"
#include "game/auth/bg_auth.h"
#include "monocypher.h"

#define WA_USERS_FILE		"webadmin_users.dat"
#define WA_SESSIONS_FILE	"webadmin_sessions.dat"
#define WA_MAX_USERS		16
#define WA_MAX_SESSIONS		32
#define WA_SESSION_IDLE		( 30 * 60 )			// seconds
#define WA_SESSION_MAX		( 12 * 60 * 60 )
#define WA_COOKIE			"vjk_session"
#define WA_ARGON_KIB		12288				// with three passes one of the settings OWASP names for Argon2id
#define WA_ARGON_PASSES		3
#define WA_HASH_MSEC		1000				// between two passwords that are hashed
#define WA_PASS_MIN			10
#define WA_PASS_MAX			128
#define WA_RECOVERY			8
#define WA_RECOVERY_LEN		10
#define WA_USER_TRIES		5
#define WA_USER_BASE_MS		30000
#define WA_USER_MAX_MS		300000
#define WA_KEY_ASKS			8
#define WA_PANEL_LINE		"VisionJK panel login v1\n"
// no I, O, 0 and 1: these are read off a page and typed
#define WA_CODE_CHARS		"ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

typedef struct waUser_s {
	char		name[WA_USER_NAME];
	waRole_t	role;
	byte		salt[16], hash[32];
	qboolean	hasTotp;
	byte		totp[WA_TOTP_SECRET];
	long long	totpStep;					// the step of the last code that was taken: none counts twice
	qboolean	hasToken;
	byte		token[32];					// the SHA-256 of it
	int			numRecovery;
	byte		recovery[WA_RECOVERY][32];	// and of the recovery codes that are left
	// not in the file
	qboolean	pending;					// a secret that was shown and is not confirmed with a code yet
	byte		pendingTotp[WA_TOTP_SECRET];
	int			fails, until;				// wrong passwords, and trap->Milliseconds() up to which it waits
} waUser_t;

typedef struct waSession_s {
	qboolean	inUse;
	byte		id[32];						// the SHA-256 of the cookie
	char		name[WA_USER_NAME];			// the account, or whose key it is
	qboolean	byKey;
	byte		pub[AUTH_PUBLIC_SIZE];
	long long	created, used;				// time()
} waSession_t;

typedef struct waKeyAsk_s {
	byte			nonce[AUTH_NONCE_SIZE];
	byte			pub[AUTH_PUBLIC_SIZE];
	unsigned int	peer;
	int				time;					// trap->Milliseconds(), 0: free
} waKeyAsk_t;

static waUser_t		waUsers[WA_MAX_USERS];
static int			waNumUsers;
static waSession_t	waSessions[WA_MAX_SESSIONS];
static waKeyAsk_t	waKeyAsks[WA_KEY_ASKS];
static int			waHashTime;
static qboolean		waHashed;

// The password of a login, hashed beside the game (WA_HashThread). One at a time.
static struct {
	volatile int		state;					// 0: none, 1: hashing, 2: done
	char				password[WA_PASS_MAX + 1];
	byte				salt[16];
	byte				hash[32];
	qboolean			ok;						// the memory for it could be had
	waConnection_t		*c;						// the login that waits for it, NULL: it went away
	char				name[WA_USER_NAME];
	char				code[32];
#ifdef _WIN32
	HANDLE				thread;
#else
	pthread_t			thread;
#endif
} waHashJob;

static const byte	waNobody[16] = { 'n', 'o', ' ', 's', 'u', 'c', 'h', ' ', 'a', 'c', 'c', 'o', 'u', 'n', 't', '.' };

static const char *const waRoleNames[] = { "none", "moderator", "admin", "owner" };

const char *WA_RoleName( waRole_t role ) {
	return waRoleNames[( role >= WA_ROLE_NONE && role <= WA_ROLE_OWNER ) ? role : WA_ROLE_NONE];
}

static waRole_t WA_RoleByName( const char *name ) {
	int i;

	for ( i = WA_ROLE_MODERATOR; i <= WA_ROLE_OWNER; i++ ) {
		if ( !Q_stricmp( name, waRoleNames[i] ) ) {
			return (waRole_t)i;
		}
	}
	return WA_ROLE_NONE;
}

/*
==============================================================================

Small things

==============================================================================
*/

// exactly len bytes written as hex, with nothing behind them but what ends a value
static qboolean WA_Unhex( const char *in, byte *out, int len ) {
	int i;

	for ( i = 0; i < len * 2; i++ ) {
		const char	c = in[i];
		int			v;

		if ( c >= '0' && c <= '9' ) v = c - '0';
		else if ( c >= 'a' && c <= 'f' ) v = c - 'a' + 10;
		else if ( c >= 'A' && c <= 'F' ) v = c - 'A' + 10;
		else return qfalse;
		if ( i & 1 ) {
			out[i / 2] |= (byte)v;
		} else {
			out[i / 2] = (byte)( v << 4 );
		}
	}
	return (qboolean)( in[i] == '\0' || in[i] == ';' || in[i] == ' ' || in[i] == '\r' || in[i] == '\n' );
}

// letters and digits to type, len of them
static qboolean WA_RandomCode( char *out, int len ) {
	byte	random[32];
	int		i;

	if ( len > (int)sizeof( random ) || !BG_AuthRandom( random, len ) ) {
		out[0] = '\0';
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		out[i] = WA_CODE_CHARS[random[i] & 31];
	}
	out[len] = '\0';
	memset( random, 0, sizeof( random ) );
	return qtrue;
}

static qboolean WA_NameValid( const char *name ) {
	int i;

	for ( i = 0; name[i]; i++ ) {
		const char c = name[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '.' ) ) {
			return qfalse;
		}
	}
	return (qboolean)( i > 0 && i < WA_USER_NAME );
}

static waUser_t *WA_FindUser( const char *name ) {
	int i;

	for ( i = 0; i < waNumUsers; i++ ) {
		if ( !Q_stricmp( waUsers[i].name, name ) ) {
			return &waUsers[i];
		}
	}
	return NULL;
}

void WA_AuthAnswer( waConnection_t *c, int status, const char *statusText, const char *why ) {
	char body[96];

	Com_sprintf( body, sizeof( body ), "{\"ok\":false,\"why\":\"%s\"}", why );
	WA_Respond( c, status, statusText, "application/json", "Cache-Control: no-store\r\n", body, (int)strlen( body ) );
}

static void WA_AuthOk( waConnection_t *c ) {
	WA_RespondJsonText( c, "{\"ok\":true}" );
}

/*
==============================================================================

Passwords

==============================================================================
*/

// qtrue: a password was hashed within the last second, this one has to come again
static qboolean WA_HashBusy( void ) {
	const int now = trap->Milliseconds();

	return (qboolean)( waHashed && now - waHashTime < WA_HASH_MSEC && now >= waHashTime );
}

// the hash itself, in the WA_ARGON_KIB KiB of area (no call of the engine: it runs on the thread of a login too)
static void WA_Argon2( const char *password, const byte *salt, byte *hash, void *area ) {
	crypto_argon2_config	config;
	crypto_argon2_inputs	inputs;

	config.algorithm = CRYPTO_ARGON2_ID;
	config.nb_blocks = WA_ARGON_KIB;
	config.nb_passes = WA_ARGON_PASSES;
	config.nb_lanes = 1;
	inputs.pass = (const uint8_t *)password;
	inputs.salt = salt;
	inputs.pass_size = (uint32_t)strlen( password );
	inputs.salt_size = 16;
	crypto_argon2( hash, 32, area, config, inputs, crypto_argon2_no_extras );
}

// in the frame: a new password, and the one an owner gives when he changes his account
static qboolean WA_HashPassword( const char *password, const byte *salt, byte *hash ) {
	void *area = NULL;

	waHashed = qtrue;
	waHashTime = trap->Milliseconds();

	// 12 MiB for the time of one call: from the engine, and given back right away
	trap->TrueMalloc( &area, WA_ARGON_KIB * 1024 );
	if ( !area ) {
		return qfalse;
	}
	WA_Argon2( password, salt, hash, area );
	trap->TrueFree( &area );
	return qtrue;
}

#ifdef _WIN32
static DWORD WINAPI WA_HashThread( LPVOID unused )
#else
static void *WA_HashThread( void *unused )
#endif
{
	void *area = malloc( WA_ARGON_KIB * 1024 );

	(void)unused;
	waHashJob.ok = area ? qtrue : qfalse;
	if ( area ) {
		WA_Argon2( waHashJob.password, waHashJob.salt, waHashJob.hash, area );
		free( area );
	}
	crypto_wipe( waHashJob.password, sizeof( waHashJob.password ) );
	waHashJob.state = 2;
	return 0;
}

// The thread of the last login is over: waited for (it is, or is a moment from it), and the job is free again.
// What it made is read after this, when the thread is surely done with it, and wiped then.
static void WA_HashJoin( void ) {
	if ( !waHashJob.state ) {
		return;
	}
#ifdef _WIN32
	WaitForSingleObject( waHashJob.thread, INFINITE );
	CloseHandle( waHashJob.thread );
#else
	pthread_join( waHashJob.thread, NULL );
#endif
	waHashJob.state = 0;
	waHashJob.c = NULL;
}

static qboolean WA_SetPassword( waUser_t *user, const char *password ) {
	byte salt[16], hash[32];

	if ( !BG_AuthRandom( salt, sizeof( salt ) ) || !WA_HashPassword( password, salt, hash ) ) {
		return qfalse;
	}
	memcpy( user->salt, salt, sizeof( salt ) );
	memcpy( user->hash, hash, sizeof( hash ) );
	return qtrue;
}

// The work is the same whether there is such an account or not, so the time it takes does not tell.
static qboolean WA_CheckPassword( const waUser_t *user, const char *password ) {
	byte				hash[32];
	qboolean			ok;

	if ( !WA_HashPassword( password, user ? user->salt : waNobody, hash ) ) {
		return qfalse;
	}
	ok = (qboolean)( user && crypto_verify32( hash, user->hash ) == 0 );
	crypto_wipe( hash, sizeof( hash ) );
	return ok;
}

/*
==============================================================================

The two files

==============================================================================
*/

static void WA_SaveUsers( void ) {
	waBuffer_t	b = { 0 };
	int			i, k;

	WA_BufPrintf( &b, "// The accounts of the web admin panel, written by the game: name, role, salt and Argon2id hash of the\n"
		"// password, the secret of the codes by time and the step of the last one, the hash of the token, the hashes\n"
		"// of the recovery codes. Change it with the page or with \"webuser\" on the console of the server.\n" );
	for ( i = 0; i < waNumUsers; i++ ) {
		const waUser_t	*u = &waUsers[i];
		char			salt[33], hash[65], totp[WA_TOTP_SECRET * 2 + 1] = "-", token[65] = "-";

		Web_Hex( u->salt, 16, salt );
		Web_Hex( u->hash, 32, hash );
		if ( u->hasTotp ) {
			Web_Hex( u->totp, WA_TOTP_SECRET, totp );
		}
		if ( u->hasToken ) {
			Web_Hex( u->token, 32, token );
		}
		WA_BufPrintf( &b, "user %s %s %s %s %s %lld %s", u->name, WA_RoleName( u->role ), salt, hash, totp, u->totpStep, token );
		for ( k = 0; k < u->numRecovery; k++ ) {
			Web_Hex( u->recovery[k], 32, hash );
			WA_BufPrintf( &b, " %s", hash );
		}
		WA_BufPrintf( &b, "\n" );
	}
	if ( !b.data || !WA_WriteGameFile( WA_USERS_FILE, b.data, b.len ) ) {
		trap->Print( S_COLOR_RED "Web admin: " WA_USERS_FILE " could not be written, a change to the accounts is lost with the next map\n" );
	}
	WA_BufFree( &b );
}

static void WA_SaveSessions( void ) {
	waBuffer_t	b = { 0 };
	int			i;

	WA_BufPrintf( &b, "// the sessions of the web admin panel that are open, written by the game. Of a cookie only its SHA-256.\n" );
	for ( i = 0; i < WA_MAX_SESSIONS; i++ ) {
		const waSession_t	*s = &waSessions[i];
		char				id[65], pub[65] = "-";

		if ( !s->inUse ) {
			continue;
		}
		Web_Hex( s->id, 32, id );
		if ( s->byKey ) {
			Web_Hex( s->pub, AUTH_PUBLIC_SIZE, pub );
		}
		WA_BufPrintf( &b, "session %s %s %lld %lld \"%s\"\n", id, pub, s->created, s->used, s->name );
	}
	if ( b.data ) {
		WA_WriteGameFile( WA_SESSIONS_FILE, b.data, b.len );
	}
	WA_BufFree( &b );
}

// the text of one of the two files with a zero behind it, NULL: there is none. Freed with free().
static char *WA_ReadText( const char *path ) {
	int		len = 0;
	char	*data = WA_ReadGameFile( path, &len ), *text;

	if ( !data ) {
		return NULL;
	}
	text = (char *)realloc( data, (size_t)len + 1 );
	if ( !text ) {
		free( data );
		return NULL;
	}
	text[len] = '\0';
	return text;
}

static void WA_LoadUsers( void ) {
	char		*text = WA_ReadText( WA_USERS_FILE );
	const char	*p = text, *token;

	waNumUsers = 0;
	memset( waUsers, 0, sizeof( waUsers ) );
	if ( !text ) {
		return;
	}
	// a line is "user" and its fields, the hashes of the recovery codes up to its end
	while ( ( token = COM_ParseExt( &p, qtrue ) ) != NULL && token[0] ) {
		waUser_t	user;
		qboolean	ok;

		if ( Q_stricmp( token, "user" ) ) {
			SkipRestOfLine( &p );
			continue;
		}
		memset( &user, 0, sizeof( user ) );
		token = COM_ParseExt( &p, qfalse );
		ok = WA_NameValid( token );
		Q_strncpyz( user.name, token, sizeof( user.name ) );
		user.role = WA_RoleByName( COM_ParseExt( &p, qfalse ) );
		ok = (qboolean)( ok && user.role != WA_ROLE_NONE && WA_Unhex( COM_ParseExt( &p, qfalse ), user.salt, 16 ) );
		ok = (qboolean)( ok && WA_Unhex( COM_ParseExt( &p, qfalse ), user.hash, 32 ) );
		token = COM_ParseExt( &p, qfalse );
		if ( ok && strcmp( token, "-" ) ) {
			user.hasTotp = ok = WA_Unhex( token, user.totp, WA_TOTP_SECRET );
		}
		user.totpStep = strtoll( COM_ParseExt( &p, qfalse ), NULL, 10 );
		token = COM_ParseExt( &p, qfalse );
		if ( ok && strcmp( token, "-" ) ) {
			user.hasToken = ok = WA_Unhex( token, user.token, 32 );
		}
		while ( ( token = COM_ParseExt( &p, qfalse ) ) != NULL && token[0] ) {
			if ( user.numRecovery < WA_RECOVERY && WA_Unhex( token, user.recovery[user.numRecovery], 32 ) ) {
				user.numRecovery++;
			}
		}
		if ( !ok || WA_FindUser( user.name ) ) {
			trap->Print( S_COLOR_YELLOW "Web admin: a line of " WA_USERS_FILE " is not an account and is left out (%s)\n", user.name );
			continue;
		}
		if ( waNumUsers == WA_MAX_USERS ) {
			trap->Print( S_COLOR_YELLOW "Web admin: " WA_USERS_FILE " has more than %i accounts, the rest is left out\n", WA_MAX_USERS );
			break;
		}
		waUsers[waNumUsers++] = user;
	}
	free( text );
}

static void WA_LoadSessions( void ) {
	char			*text = WA_ReadText( WA_SESSIONS_FILE );
	const char		*p = text, *token;
	const long long	now = (long long)time( NULL );
	int				count = 0;

	memset( waSessions, 0, sizeof( waSessions ) );
	if ( !text ) {
		return;
	}
	while ( ( token = COM_ParseExt( &p, qtrue ) ) != NULL && token[0] && count < WA_MAX_SESSIONS ) {
		waSession_t	s;
		qboolean	ok;

		if ( Q_stricmp( token, "session" ) ) {
			SkipRestOfLine( &p );
			continue;
		}
		memset( &s, 0, sizeof( s ) );
		ok = WA_Unhex( COM_ParseExt( &p, qfalse ), s.id, 32 );
		token = COM_ParseExt( &p, qfalse );
		if ( ok && strcmp( token, "-" ) ) {
			s.byKey = ok = WA_Unhex( token, s.pub, AUTH_PUBLIC_SIZE );
		}
		s.created = strtoll( COM_ParseExt( &p, qfalse ), NULL, 10 );
		s.used = strtoll( COM_ParseExt( &p, qfalse ), NULL, 10 );
		Q_strncpyz( s.name, COM_ParseExt( &p, qfalse ), sizeof( s.name ) );
		SkipRestOfLine( &p );
		if ( !ok || !s.name[0] || now - s.used > WA_SESSION_IDLE || now - s.created > WA_SESSION_MAX || s.used > now + 60 ) {
			continue;
		}
		s.inUse = qtrue;
		waSessions[count++] = s;
	}
	free( text );
}

/*
==============================================================================

Sessions

==============================================================================
*/

static void WA_CloseSessionsOf( const char *name, const waSession_t *but ) {
	int i;

	for ( i = 0; i < WA_MAX_SESSIONS; i++ ) {
		if ( waSessions[i].inUse && !waSessions[i].byKey && &waSessions[i] != but && !Q_stricmp( waSessions[i].name, name ) ) {
			memset( &waSessions[i], 0, sizeof( waSessions[i] ) );
		}
	}
	WA_SaveSessions();
}

// The role a session has right now, WA_ROLE_NONE: its account or its key is gone.
static waRole_t WA_SessionRole( const waSession_t *s ) {
	if ( s->byKey ) {
		switch ( G_AuthKeyLevel( s->pub, NULL ) ) {
		case JPADM_RCON:		return WA_ROLE_OWNER;
		case JPADM_COUNCIL:		return WA_ROLE_ADMIN;
		case JPADM_KNIGHT:
		case JPADM_INSTRUCTOR:	return WA_ROLE_MODERATOR;
		default:				return WA_ROLE_NONE;
		}
	} else {
		const waUser_t *user = WA_FindUser( s->name );

		return user ? user->role : WA_ROLE_NONE;
	}
}

/*
=================
WA_NewSession

Makes a session and the answer that hands its cookie to the browser. qfalse: the system gave no random bytes.
=================
*/
static qboolean WA_NewSession( waConnection_t *c, const char *name, const byte *pub ) {
	const long long	now = (long long)time( NULL );
	waSession_t		*s = &waSessions[0];
	byte			cookie[32];
	char			text[65], header[256], body[96];
	int				i;

	if ( !BG_AuthRandom( cookie, sizeof( cookie ) ) ) {
		WA_AuthAnswer( c, 500, "Internal Server Error", "random" );
		return qfalse;
	}
	// a free place, or the one that was not used for the longest
	for ( i = 0; i < WA_MAX_SESSIONS; i++ ) {
		if ( !waSessions[i].inUse ) {
			s = &waSessions[i];
			break;
		}
		if ( waSessions[i].used < s->used ) {
			s = &waSessions[i];
		}
	}
	memset( s, 0, sizeof( *s ) );
	s->inUse = qtrue;
	Web_SHA256( cookie, sizeof( cookie ), s->id );
	Q_strncpyz( s->name, name, sizeof( s->name ) );
	if ( pub ) {
		s->byKey = qtrue;
		memcpy( s->pub, pub, AUTH_PUBLIC_SIZE );
	}
	s->created = s->used = now;
	WA_SaveSessions();

	Web_Hex( cookie, sizeof( cookie ), text );
	Com_sprintf( header, sizeof( header ), "Set-Cookie: " WA_COOKIE "=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%i%s\r\nCache-Control: no-store\r\n",
		text, WA_SESSION_MAX, c->https ? "; Secure" : "" );
	Com_sprintf( body, sizeof( body ), "{\"ok\":true,\"role\":\"%s\"}", WA_RoleName( WA_SessionRole( s ) ) );
	WA_Respond( c, 200, "OK", "application/json", header, body, (int)strlen( body ) );
	crypto_wipe( cookie, sizeof( cookie ) );
	crypto_wipe( text, sizeof( text ) );
	crypto_wipe( header, sizeof( header ) );
	return qtrue;
}

/*
=================
WA_AuthRequest

Who asks: from the cookie of a session, or from the token of a script. Fills in the role, the name and, for a
session of an account, the account.
=================
*/
void WA_AuthRequest( waConnection_t *c, const char *head ) {
	const long long	now = (long long)time( NULL );
	char			value[1024];
	byte			secret[32], id[32];
	const char		*p;
	int				i;

	c->role = WA_ROLE_NONE;
	c->user[0] = '\0';
	c->session = -1;

	if ( Web_HeaderValue( head, "Cookie", value, sizeof( value ) ) ) {
		for ( p = value; ( p = strstr( p, WA_COOKIE "=" ) ) != NULL; p += sizeof( WA_COOKIE ) ) {
			// the name of a cookie, not the end of another one's
			if ( p != value && p[-1] != ' ' && p[-1] != ';' ) {
				continue;
			}
			if ( !WA_Unhex( p + sizeof( WA_COOKIE ), secret, sizeof( secret ) ) ) {
				continue;
			}
			Web_SHA256( secret, sizeof( secret ), id );
			for ( i = 0; i < WA_MAX_SESSIONS; i++ ) {
				waSession_t *s = &waSessions[i];

				if ( !s->inUse || crypto_verify32( s->id, id ) ) {
					continue;
				}
				if ( now - s->used > WA_SESSION_IDLE || now - s->created > WA_SESSION_MAX || WA_SessionRole( s ) == WA_ROLE_NONE ) {
					memset( s, 0, sizeof( *s ) );
					WA_SaveSessions();
					break;
				}
				s->used = now;
				c->role = WA_SessionRole( s );
				c->session = i;
				Q_strncpyz( c->user, s->name, sizeof( c->user ) );
				return;
			}
		}
	}

	// A script: the token of its account as "Bearer <token>", or with vs_webBasicAuth 1 as the password of basic
	// auth behind the name of the account. Never the password of the account itself.
	if ( Web_HeaderValue( head, "Authorization", value, sizeof( value ) ) ) {
		char		decoded[256];
		const char	*name = NULL, *token = NULL;

		if ( !Q_stricmpn( value, "Bearer ", 7 ) ) {
			token = value + 7;
		} else if ( !vs_webBasicAuth.integer ) {
			// A browser that knew the panel when it had one password keeps sending that with every request, until
			// it is closed. That is no try at anything.
			return;
		} else if ( !Q_stricmpn( value, "Basic ", 6 ) && Web_Base64Decode( value + 6, decoded, sizeof( decoded ) ) > 0 ) {
			char *colon = strchr( decoded, ':' );

			if ( colon ) {
				*colon = '\0';
				name = decoded;
				token = colon + 1;
			}
		}
		if ( token && WA_Unhex( token, secret, sizeof( secret ) ) ) {
			Web_SHA256( secret, sizeof( secret ), id );
			for ( i = 0; i < waNumUsers; i++ ) {
				if ( waUsers[i].hasToken && !crypto_verify32( waUsers[i].token, id ) && ( !name || !Q_stricmp( name, waUsers[i].name ) ) ) {
					c->role = waUsers[i].role;
					Q_strncpyz( c->user, waUsers[i].name, sizeof( c->user ) );
					return;
				}
			}
		}
		// what was sent opens nothing: a wrong try of that address
		WA_AuthResult( c->peer, qfalse );
		WA_LogUse( c, "wrong token" );
	}
}

// the account of who asks, NULL: he came with a key, or as nobody
static waUser_t *WA_RequestUser( const waConnection_t *c ) {
	if ( c->role == WA_ROLE_NONE || ( c->session >= 0 && waSessions[c->session].byKey ) ) {
		return NULL;
	}
	return WA_FindUser( c->user );
}

/*
==============================================================================

Logging in

==============================================================================
*/

static void WA_UserFailed( waUser_t *user ) {
	if ( !user ) {
		return;
	}
	user->fails++;
	if ( user->fails >= WA_USER_TRIES ) {
		const int	steps = user->fails - WA_USER_TRIES;
		int			wait = WA_USER_BASE_MS;

		if ( steps < 8 ) {
			wait <<= steps;
		}
		if ( wait > WA_USER_MAX_MS || wait <= 0 || steps >= 8 ) {
			wait = WA_USER_MAX_MS;
		}
		user->until = trap->Milliseconds() + wait;
	}
}

// the second step: a code by time that was not taken before, or a recovery code, which is used up by this
static qboolean WA_CheckCode( waUser_t *user, const char *typed ) {
	const long long	step = WA_TotpMatch( user->totp, WA_TOTP_SECRET, typed, (long long)time( NULL ) );
	char			code[WA_RECOVERY_LEN + 1];
	byte			hash[32];
	int				n = 0, i;

	if ( step >= 0 ) {
		if ( step <= user->totpStep ) {
			return qfalse;		// that code, or a later one, opened a session already
		}
		user->totpStep = step;
		WA_SaveUsers();
		return qtrue;
	}
	for ( ; *typed; typed++ ) {
		if ( *typed == '-' || *typed == ' ' ) {
			continue;
		}
		if ( n == WA_RECOVERY_LEN ) {
			return qfalse;
		}
		code[n++] = (char)toupper( (unsigned char)*typed );
	}
	if ( n != WA_RECOVERY_LEN ) {
		return qfalse;
	}
	Web_SHA256( code, WA_RECOVERY_LEN, hash );
	for ( i = 0; i < user->numRecovery; i++ ) {
		if ( !crypto_verify32( user->recovery[i], hash ) ) {
			memmove( user->recovery[i], user->recovery[i + 1], (size_t)( user->numRecovery - i - 1 ) * 32 );
			user->numRecovery--;
			WA_SaveUsers();
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
WA_ApiLogin

POST /api/login: name, password and, where the account has a second step, code
=================
*/
static void WA_ApiLogin( waConnection_t *c, const char *body ) {
	char		name[WA_USER_NAME], password[WA_PASS_MAX + 1], code[32];
	waUser_t	*user;

	if ( !WA_FormValue( body, "name", name, sizeof( name ) ) || !WA_FormValue( body, "password", password, sizeof( password ) ) ) {
		WA_AuthAnswer( c, 400, "Bad Request", "form" );
		return;
	}
	if ( !WA_FormValue( body, "code", code, sizeof( code ) ) ) {
		code[0] = '\0';
	}
	// (a job whose login went away is done with here)
	if ( waHashJob.state == 2 && !waHashJob.c ) {
		WA_HashJoin();
		crypto_wipe( waHashJob.hash, sizeof( waHashJob.hash ) );
	}
	if ( waHashJob.state || WA_HashBusy() ) {
		crypto_wipe( password, sizeof( password ) );
		WA_AuthAnswer( c, 429, "Too Many Requests", "busy" );
		return;
	}
	// An unknown name is hashed all the same, with a salt of nobody's: the answer takes as long either way.
	user = WA_FindUser( name );
	Q_strncpyz( waHashJob.password, password, sizeof( waHashJob.password ) );
	crypto_wipe( password, sizeof( password ) );
	memcpy( waHashJob.salt, user ? user->salt : waNobody, sizeof( waHashJob.salt ) );
	Q_strncpyz( waHashJob.name, name, sizeof( waHashJob.name ) );
	Q_strncpyz( waHashJob.code, code, sizeof( waHashJob.code ) );
	waHashJob.c = c;
	waHashJob.ok = qfalse;
	waHashJob.state = 1;
	waHashed = qtrue;
	waHashTime = trap->Milliseconds();
#ifdef _WIN32
	waHashJob.thread = CreateThread( NULL, 0, WA_HashThread, NULL, 0, NULL );
	if ( !waHashJob.thread )
#else
	if ( pthread_create( &waHashJob.thread, NULL, WA_HashThread, NULL ) != 0 )
#endif
	{
		crypto_wipe( waHashJob.password, sizeof( waHashJob.password ) );
		waHashJob.state = 0;
		waHashJob.c = NULL;
		WA_AuthAnswer( c, 500, "Internal Server Error", "thread" );
		return;
	}
	// answered by WA_AuthLoginFrame when the hash is there
	c->loginWait = qtrue;
}

/*
=================
WA_AuthLoginFrame

Every frame for a connection whose login waits for its hash. When it is there, the login goes on as it would have
right away: the password, the second step, the session.
=================
*/
void WA_AuthLoginFrame( waConnection_t *c ) {
	waUser_t	*user;
	qboolean	right;

	if ( waHashJob.c != c || waHashJob.state != 2 ) {
		// (a login whose job is not its own any more: it can not be answered)
		if ( waHashJob.c != c ) {
			c->loginWait = qfalse;
			WA_AuthAnswer( c, 500, "Internal Server Error", "lost" );
		}
		return;
	}
	c->loginWait = qfalse;
	WA_HashJoin();
	user = WA_FindUser( waHashJob.name );
	// (the account may have been removed, or got another password, while it was hashed: then it is wrong)
	right = (qboolean)( waHashJob.ok && user && !memcmp( waHashJob.salt, user->salt, sizeof( user->salt ) )
		&& crypto_verify32( waHashJob.hash, user->hash ) == 0 );
	crypto_wipe( waHashJob.hash, sizeof( waHashJob.hash ) );

	if ( !right ) {
		WA_AuthResult( c->peer, qfalse );
		WA_LogUse( c, va( "wrong password for the account %.24s", waHashJob.name ) );
		WA_AuthAnswer( c, 401, "Unauthorized", "wrong" );
		return;
	}
	if ( user->hasTotp ) {
		const int now = trap->Milliseconds();

		if ( !waHashJob.code[0] ) {
			WA_AuthAnswer( c, 401, "Unauthorized", "code" );		// the page asks for it and sends all three
			return;
		}
		// Who comes this far has the password. Codes are six digits: after wrong ones the account waits.
		if ( user->until > now ) {
			WA_AuthAnswer( c, 429, "Too Many Requests", "wait" );
			return;
		}
		if ( !WA_CheckCode( user, waHashJob.code ) ) {
			WA_UserFailed( user );
			WA_AuthResult( c->peer, qfalse );
			WA_LogUse( c, va( "wrong code for the account %s", user->name ) );
			WA_AuthAnswer( c, 401, "Unauthorized", "badcode" );
			return;
		}
	}
	user->fails = user->until = 0;
	WA_AuthResult( c->peer, qtrue );
	if ( WA_NewSession( c, user->name, NULL ) ) {
		WA_LogUse( c, va( "%s logged in (%s)", user->name, WA_RoleName( user->role ) ) );
	}
}

// a connection whose login waits for its hash is closed: the hash is made to the end and thrown away
void WA_AuthLoginGone( waConnection_t *c ) {
	if ( waHashJob.c == c ) {
		waHashJob.c = NULL;
	}
}

// what a browser signs: a first line of its own (a signature for the game is none for the panel), what the
// server asked, and the name the browser reaches the panel by
static int WA_PanelMessage( byte *out, int outSize, const byte *nonce, const char *host ) {
	const int first = (int)strlen( WA_PANEL_LINE ), hostLen = (int)strlen( host );

	if ( first + AUTH_NONCE_SIZE + hostLen > outSize ) {
		return 0;
	}
	memcpy( out, WA_PANEL_LINE, first );
	memcpy( out + first, nonce, AUTH_NONCE_SIZE );
	memcpy( out + first + AUTH_NONCE_SIZE, host, hostLen );
	return first + AUTH_NONCE_SIZE + hostLen;
}

/*
=================
WA_ApiLoginKey

POST /api/login/key, twice: with pub alone the server answers with what is to be signed, with pub and sig it
checks the signature and opens a session. The key has to be in admin_keys.txt.
=================
*/
static void WA_ApiLoginKey( waConnection_t *c, const char *head, const char *body ) {
	char		text[AUTH_BASE64_SIZE + 8], host[128];
	byte		pub[AUTH_PUBLIC_SIZE], signature[AUTH_SIGNATURE_SIZE], message[AUTH_MESSAGE_SIZE];
	const char	*comment = "";
	const int	now = trap->Milliseconds();
	waKeyAsk_t	*ask = NULL;
	int			i, length;

	if ( !WA_FormValue( body, "pub", text, sizeof( text ) ) || !BG_AuthDecode( text, pub, sizeof( pub ) ) ) {
		WA_AuthAnswer( c, 400, "Bad Request", "form" );
		return;
	}
	if ( G_AuthKeyLevel( pub, &comment ) == JPADM_NONE ) {
		WA_AuthResult( c->peer, qfalse );
		WA_AuthAnswer( c, 401, "Unauthorized", "unknown" );
		return;
	}

	if ( !WA_FormValue( body, "sig", text, sizeof( text ) ) ) {
		char reply[160];

		// a free place, or the oldest question
		ask = &waKeyAsks[0];
		for ( i = 0; i < WA_KEY_ASKS; i++ ) {
			if ( !waKeyAsks[i].time || now - waKeyAsks[i].time > AUTH_NONCE_MSEC || now < waKeyAsks[i].time ) {
				ask = &waKeyAsks[i];
				break;
			}
			if ( waKeyAsks[i].time < ask->time ) {
				ask = &waKeyAsks[i];
			}
		}
		if ( !BG_AuthRandom( ask->nonce, sizeof( ask->nonce ) ) ) {
			memset( ask, 0, sizeof( *ask ) );
			WA_AuthAnswer( c, 500, "Internal Server Error", "random" );
			return;
		}
		memcpy( ask->pub, pub, sizeof( ask->pub ) );
		ask->peer = c->peer;
		ask->time = now ? now : 1;
		BG_AuthEncode( ask->nonce, sizeof( ask->nonce ), text, sizeof( text ) );
		Com_sprintf( reply, sizeof( reply ), "{\"ok\":true,\"nonce\":\"%s\"}", text );
		WA_RespondJsonText( c, reply );
		return;
	}

	for ( i = 0; i < WA_KEY_ASKS; i++ ) {
		if ( waKeyAsks[i].time && waKeyAsks[i].peer == c->peer && !memcmp( waKeyAsks[i].pub, pub, sizeof( pub ) )
			&& now - waKeyAsks[i].time <= AUTH_NONCE_MSEC && now >= waKeyAsks[i].time ) {
			ask = &waKeyAsks[i];
			break;
		}
	}
	if ( !ask ) {
		WA_AuthAnswer( c, 401, "Unauthorized", "late" );
		return;
	}
	// the name the browser has for the panel: what it asked for, or behind the proxy what it asked the proxy for
	if ( !( c->proxied && Web_HeaderValue( head, "X-Forwarded-Host", host, sizeof( host ) ) ) && !Web_HeaderValue( head, "Host", host, sizeof( host ) ) ) {
		host[0] = '\0';
	}
	length = WA_PanelMessage( message, sizeof( message ), ask->nonce, host );
	// a question is good for one answer
	memset( ask, 0, sizeof( *ask ) );
	if ( !length || !BG_AuthDecode( text, signature, sizeof( signature ) ) || !G_AuthCheck( signature, pub, message, length ) ) {
		WA_AuthResult( c->peer, qfalse );
		WA_LogUse( c, va( "bad signature for the key of %.48s", comment ) );
		WA_AuthAnswer( c, 401, "Unauthorized", "signature" );
		return;
	}
	WA_AuthResult( c->peer, qtrue );
	if ( WA_NewSession( c, comment[0] ? comment : "key", pub ) ) {
		WA_LogUse( c, va( "logged in with the key of %.48s", comment ) );
	}
}

/*
==============================================================================

The own account

==============================================================================
*/

// GET /api/me: who the page is logged in as and what he may see, for an owner also the accounts
static void WA_ApiMe( waConnection_t *c ) {
	const waUser_t	*user = WA_RequestUser( c );
	waBuffer_t		b = { 0 };
	int				i;

	WA_BufPrintf( &b, "{\"name\":" );
	WA_BufJsonString( &b, c->user );
	WA_BufPrintf( &b, ",\"role\":\"%s\",\"account\":%s,\"totp\":%s,\"token\":%s,\"recovery\":%i,\"keys\":%i,\"passwords\":%s,\"proxy\":%s",
		WA_RoleName( c->role ), WA_BOOL( user ), WA_BOOL( user && user->hasTotp ), WA_BOOL( user && user->hasToken ),
		user ? user->numRecovery : 0, G_AuthNumKeys(), WA_BOOL( vs_adminPasswords.integer ), WA_BOOL( c->proxied ) );
	if ( c->role == WA_ROLE_OWNER ) {
		WA_BufPrintf( &b, ",\"users\":[" );
		for ( i = 0; i < waNumUsers; i++ ) {
			WA_BufPrintf( &b, "%s{\"name\":\"%s\",\"role\":\"%s\",\"totp\":%s,\"token\":%s}", i ? "," : "", waUsers[i].name,
				WA_RoleName( waUsers[i].role ), WA_BOOL( waUsers[i].hasTotp ), WA_BOOL( waUsers[i].hasToken ) );
		}
		WA_BufPrintf( &b, "],\"adminKeys\":[" );
		for ( i = 0; i < G_AuthNumKeys(); i++ ) {
			char		fingerprint[64];
			const char	*comment;
			int			adminLevel;

			G_AuthKeyInfo( i, &adminLevel, fingerprint, sizeof( fingerprint ), &comment );
			WA_BufPrintf( &b, "%s{\"rank\":\"%s\",\"fingerprint\":\"%s\",\"comment\":", i ? "," : "", G_AuthRankName( adminLevel ), fingerprint );
			WA_BufJsonString( &b, comment );
			WA_BufPrintf( &b, "}" );
		}
		WA_BufPrintf( &b, "]" );
	}
	WA_BufPrintf( &b, "}" );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// POST /api/account/...: only for who is logged in with an account, and with its password where that changes
// how one gets in
static void WA_ApiAccount( waConnection_t *c, const char *what, const char *body ) {
	waUser_t	*user = WA_RequestUser( c );
	char		password[WA_PASS_MAX + 1], fresh[WA_PASS_MAX + 1], code[32];
	const qboolean needsPassword = (qboolean)( !strcmp( what, "password" ) || !strcmp( what, "totp/start" ) || !strcmp( what, "totp/off" )
		|| !strcmp( what, "token" ) );

	if ( !user ) {
		WA_AuthAnswer( c, 400, "Bad Request", "noaccount" );
		return;
	}
	if ( needsPassword ) {
		if ( !WA_FormValue( body, "password", password, sizeof( password ) ) ) {
			WA_AuthAnswer( c, 400, "Bad Request", "form" );
			return;
		}
		if ( WA_HashBusy() ) {
			crypto_wipe( password, sizeof( password ) );
			WA_AuthAnswer( c, 429, "Too Many Requests", "busy" );
			return;
		}
		if ( user->until > trap->Milliseconds() || !WA_CheckPassword( user, password ) ) {
			crypto_wipe( password, sizeof( password ) );
			WA_UserFailed( user );
			WA_AuthAnswer( c, 403, "Forbidden", "wrong" );
			return;
		}
		crypto_wipe( password, sizeof( password ) );
	}

	if ( !strcmp( what, "password" ) ) {
		if ( !WA_FormValue( body, "new", fresh, sizeof( fresh ) ) || strlen( fresh ) < WA_PASS_MIN ) {
			WA_AuthAnswer( c, 400, "Bad Request", "short" );
			return;
		}
		// the second hash of this request, the one a second apart is the next login
		if ( !WA_SetPassword( user, fresh ) ) {
			crypto_wipe( fresh, sizeof( fresh ) );
			WA_AuthAnswer( c, 500, "Internal Server Error", "random" );
			return;
		}
		crypto_wipe( fresh, sizeof( fresh ) );
		WA_SaveUsers();
		WA_CloseSessionsOf( user->name, c->session >= 0 ? &waSessions[c->session] : NULL );
		WA_LogUse( c, va( "%s changed his password", user->name ) );
		WA_AuthOk( c );
		return;
	}

	if ( !strcmp( what, "totp/start" ) ) {
		char		secret[40], reply[256];

		if ( !BG_AuthRandom( user->pendingTotp, WA_TOTP_SECRET ) ) {
			WA_AuthAnswer( c, 500, "Internal Server Error", "random" );
			return;
		}
		user->pending = qtrue;
		WA_Base32( user->pendingTotp, WA_TOTP_SECRET, secret, sizeof( secret ) );
		Com_sprintf( reply, sizeof( reply ), "{\"ok\":true,\"secret\":\"%s\",\"uri\":\"otpauth://totp/VisionJK:%s?secret=%s&issuer=VisionJK&algorithm=SHA1&digits=6&period=30\"}",
			secret, user->name, secret );
		WA_RespondJsonText( c, reply );
		crypto_wipe( secret, sizeof( secret ) );
		crypto_wipe( reply, sizeof( reply ) );
		return;
	}

	if ( !strcmp( what, "totp/confirm" ) ) {
		waBuffer_t	b = { 0 };
		long long	step;
		int			i;

		// the code shows that the app has the secret: only then does the account ask for one
		if ( !user->pending || !WA_FormValue( body, "code", code, sizeof( code ) )
			|| ( step = WA_TotpMatch( user->pendingTotp, WA_TOTP_SECRET, code, (long long)time( NULL ) ) ) < 0 ) {
			WA_AuthAnswer( c, 400, "Bad Request", "badcode" );
			return;
		}
		memcpy( user->totp, user->pendingTotp, WA_TOTP_SECRET );
		crypto_wipe( user->pendingTotp, WA_TOTP_SECRET );
		user->pending = qfalse;
		user->hasTotp = qtrue;
		user->totpStep = step;
		user->numRecovery = 0;
		WA_BufPrintf( &b, "{\"ok\":true,\"recovery\":[" );
		for ( i = 0; i < WA_RECOVERY; i++ ) {
			char text[WA_RECOVERY_LEN + 1];

			if ( !WA_RandomCode( text, WA_RECOVERY_LEN ) ) {
				break;
			}
			Web_SHA256( text, WA_RECOVERY_LEN, user->recovery[user->numRecovery++] );
			WA_BufPrintf( &b, "%s\"%.5s-%s\"", i ? "," : "", text, text + 5 );
			crypto_wipe( text, sizeof( text ) );
		}
		WA_BufPrintf( &b, "]}" );
		WA_SaveUsers();
		WA_LogUse( c, va( "%s switched the second step of his login on", user->name ) );
		WA_RespondJson( c, &b );
		if ( b.data ) {
			crypto_wipe( b.data, (size_t)b.len );
		}
		WA_BufFree( &b );
		return;
	}

	if ( !strcmp( what, "totp/off" ) ) {
		user->hasTotp = user->pending = qfalse;
		user->numRecovery = 0;
		crypto_wipe( user->totp, sizeof( user->totp ) );
		WA_SaveUsers();
		WA_LogUse( c, va( "%s switched the second step of his login off", user->name ) );
		WA_AuthOk( c );
		return;
	}

	if ( !strcmp( what, "token" ) ) {
		byte token[32];
		char text[65], reply[128];

		if ( !BG_AuthRandom( token, sizeof( token ) ) ) {
			WA_AuthAnswer( c, 500, "Internal Server Error", "random" );
			return;
		}
		Web_SHA256( token, sizeof( token ), user->token );
		user->hasToken = qtrue;
		WA_SaveUsers();
		Web_Hex( token, sizeof( token ), text );
		Com_sprintf( reply, sizeof( reply ), "{\"ok\":true,\"token\":\"%s\"}", text );
		WA_RespondJsonText( c, reply );
		crypto_wipe( token, sizeof( token ) );
		crypto_wipe( text, sizeof( text ) );
		crypto_wipe( reply, sizeof( reply ) );
		WA_LogUse( c, va( "%s made a token for scripts", user->name ) );
		return;
	}

	if ( !strcmp( what, "token/off" ) ) {
		user->hasToken = qfalse;
		crypto_wipe( user->token, sizeof( user->token ) );
		WA_SaveUsers();
		WA_AuthOk( c );
		return;
	}

	WA_RespondText( c, 404, "Not Found", "not found" );
}

/*
==============================================================================

The accounts, for an owner

==============================================================================
*/

// Can there be an account of that name and role? qfalse and a reason: no.
static qboolean WA_UserAddable( const char *name, waRole_t role, const char **why ) {
	if ( !WA_NameValid( name ) ) {
		*why = "name";		// letters, digits, _ - . and 23 of them at most
		return qfalse;
	}
	if ( role == WA_ROLE_NONE ) {
		*why = "role";
		return qfalse;
	}
	if ( WA_FindUser( name ) ) {
		*why = "exists";
		return qfalse;
	}
	if ( waNumUsers == WA_MAX_USERS ) {
		*why = "full";
		return qfalse;
	}
	return qtrue;
}

// A new account with a password the game makes: the owner hands it on, the new one changes it. NULL and a
// reason: there is none.
static waUser_t *WA_AddUser( const char *name, waRole_t role, char *password, const char **why ) {
	waUser_t *user;

	if ( !WA_UserAddable( name, role, why ) ) {
		return NULL;
	}
	user = &waUsers[waNumUsers];
	memset( user, 0, sizeof( *user ) );
	Q_strncpyz( user->name, name, sizeof( user->name ) );
	user->role = role;
	if ( !WA_RandomCode( password, 16 ) || !WA_SetPassword( user, password ) ) {
		*why = "random";
		return NULL;
	}
	waNumUsers++;
	WA_SaveUsers();
	return user;
}

static void WA_RemoveUser( waUser_t *user ) {
	const int index = (int)( user - waUsers );

	WA_CloseSessionsOf( user->name, NULL );
	memmove( user, user + 1, (size_t)( waNumUsers - index - 1 ) * sizeof( *user ) );
	waNumUsers--;
	memset( &waUsers[waNumUsers], 0, sizeof( waUsers[0] ) );
	WA_SaveUsers();
}

static int WA_CountOwners( void ) {
	int i, n = 0;

	for ( i = 0; i < waNumUsers; i++ ) {
		n += ( waUsers[i].role == WA_ROLE_OWNER );
	}
	return n;
}

// a new password from the game, the second step and the token off, every session closed
static qboolean WA_ResetUser( waUser_t *user, char *password ) {
	if ( !WA_RandomCode( password, 16 ) || !WA_SetPassword( user, password ) ) {
		return qfalse;
	}
	user->hasTotp = user->hasToken = user->pending = qfalse;
	user->numRecovery = 0;
	user->fails = user->until = 0;
	WA_SaveUsers();
	WA_CloseSessionsOf( user->name, NULL );
	return qtrue;
}

// POST /api/users/add, remove, role, reset
static void WA_ApiUsers( waConnection_t *c, const char *what, const char *body ) {
	char		name[WA_USER_NAME + 8], roleName[16], password[20], reply[128];
	const char	*why = "form";
	waUser_t	*user;
	waRole_t	role;

	if ( !WA_FormValue( body, "name", name, sizeof( name ) ) ) {
		WA_AuthAnswer( c, 400, "Bad Request", "form" );
		return;
	}
	if ( !WA_FormValue( body, "role", roleName, sizeof( roleName ) ) ) {
		roleName[0] = '\0';
	}
	role = WA_RoleByName( roleName );
	user = WA_FindUser( name );

	if ( !strcmp( what, "add" ) ) {
		// what is wrong with the request is said first: "come again" is for one that would work
		if ( !WA_UserAddable( name, role, &why ) ) {
			WA_AuthAnswer( c, 400, "Bad Request", why );
			return;
		}
		if ( WA_HashBusy() ) {
			WA_AuthAnswer( c, 429, "Too Many Requests", "busy" );
			return;
		}
		if ( !WA_AddUser( name, role, password, &why ) ) {
			WA_AuthAnswer( c, 400, "Bad Request", why );
			return;
		}
		WA_LogUse( c, va( "%s made the account %s (%s)", c->user, name, roleName ) );
	} else if ( !user ) {
		WA_AuthAnswer( c, 404, "Not Found", "nouser" );
		return;
	} else if ( !strcmp( what, "remove" ) || ( !strcmp( what, "role" ) && role != WA_ROLE_OWNER ) ) {
		// the panel is never left without somebody who can make accounts
		if ( user->role == WA_ROLE_OWNER && WA_CountOwners() < 2 ) {
			WA_AuthAnswer( c, 400, "Bad Request", "lastowner" );
			return;
		}
		if ( !strcmp( what, "remove" ) ) {
			WA_LogUse( c, va( "%s removed the account %s", c->user, user->name ) );
			WA_RemoveUser( user );
		} else if ( role == WA_ROLE_NONE ) {
			WA_AuthAnswer( c, 400, "Bad Request", "role" );
			return;
		} else {
			user->role = role;
			WA_SaveUsers();
			WA_LogUse( c, va( "%s made the account %s a %s", c->user, user->name, roleName ) );
		}
		WA_AuthOk( c );
		return;
	} else if ( !strcmp( what, "role" ) ) {
		user->role = role;
		WA_SaveUsers();
		WA_LogUse( c, va( "%s made the account %s a %s", c->user, user->name, roleName ) );
		WA_AuthOk( c );
		return;
	} else if ( !strcmp( what, "reset" ) ) {
		if ( WA_HashBusy() ) {
			WA_AuthAnswer( c, 429, "Too Many Requests", "busy" );
			return;
		}
		if ( !WA_ResetUser( user, password ) ) {
			WA_AuthAnswer( c, 500, "Internal Server Error", "random" );
			return;
		}
		WA_LogUse( c, va( "%s gave the account %s a new password", c->user, user->name ) );
	} else {
		WA_RespondText( c, 404, "Not Found", "not found" );
		return;
	}

	// add and reset: the password, shown once
	Com_sprintf( reply, sizeof( reply ), "{\"ok\":true,\"password\":\"%s\"}", password );
	WA_RespondJsonText( c, reply );
	crypto_wipe( password, sizeof( password ) );
	crypto_wipe( reply, sizeof( reply ) );
}

// POST /api/admincode: a code for one login in the game (amlogin <code>), for an admin whose client can not
// sign. An admin makes them for the three ranks of JA+, the rank with every right is for an owner.
static void WA_ApiAdminCode( waConnection_t *c, const char *body ) {
	char		rank[16], code[16], reply[96];
	int			adminLevel;

	if ( !WA_FormValue( body, "rank", rank, sizeof( rank ) ) || ( adminLevel = G_AuthRankLevel( rank ) ) == JPADM_NONE ) {
		WA_AuthAnswer( c, 400, "Bad Request", "rank" );
		return;
	}
	if ( adminLevel == JPADM_RCON && c->role < WA_ROLE_OWNER ) {
		WA_AuthAnswer( c, 403, "Forbidden", "role" );
		return;
	}
	if ( !G_AuthNewCode( adminLevel, code, sizeof( code ) ) ) {
		WA_AuthAnswer( c, 500, "Internal Server Error", "random" );
		return;
	}
	WA_LogUse( c, va( "%s made a one-time code for the rank %s", c->user, G_AuthRankName( adminLevel ) ) );
	Com_sprintf( reply, sizeof( reply ), "{\"ok\":true,\"code\":\"%s\",\"seconds\":60}", code );
	WA_RespondJsonText( c, reply );
}

/*
=================
WA_AuthRoute

The requests of this file. qtrue: it was one of them and is answered. The login is the only thing here that
somebody gets who is not logged in.
=================
*/
qboolean WA_AuthRoute( waConnection_t *c, const char *method, const char *path, const char *head, const char *body ) {
	const qboolean post = (qboolean)!strcmp( method, "POST" );

	if ( post && !strcmp( path, "/api/login" ) ) {
		WA_ApiLogin( c, body );
		return qtrue;
	}
	if ( post && !strcmp( path, "/api/login/key" ) ) {
		WA_ApiLoginKey( c, head, body );
		return qtrue;
	}
	if ( c->role == WA_ROLE_NONE ) {
		return qfalse;
	}

	if ( post && !strcmp( path, "/api/logout" ) ) {
		if ( c->session >= 0 ) {
			memset( &waSessions[c->session], 0, sizeof( waSessions[0] ) );
			WA_SaveSessions();
		}
		WA_Respond( c, 200, "OK", "application/json", "Set-Cookie: " WA_COOKIE "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0\r\nCache-Control: no-store\r\n",
			"{\"ok\":true}", 11 );
		return qtrue;
	}
	if ( !post && !strcmp( path, "/api/me" ) ) {
		WA_ApiMe( c );
		return qtrue;
	}
	if ( post && !strncmp( path, "/api/account/", 13 ) ) {
		WA_ApiAccount( c, path + 13, body );
		return qtrue;
	}
	if ( post && !strncmp( path, "/api/users/", 11 ) ) {
		if ( c->role < WA_ROLE_OWNER ) {
			WA_AuthAnswer( c, 403, "Forbidden", "role" );
		} else {
			WA_ApiUsers( c, path + 11, body );
		}
		return qtrue;
	}
	if ( post && !strcmp( path, "/api/admincode" ) ) {
		if ( c->role < WA_ROLE_ADMIN ) {
			WA_AuthAnswer( c, 403, "Forbidden", "role" );
		} else {
			WA_ApiAdminCode( c, body );
		}
		return qtrue;
	}
	return qfalse;
}

/*
==============================================================================

What the game calls

==============================================================================
*/

qboolean WA_AuthHasUsers( void ) {
	return (qboolean)( waNumUsers > 0 );
}

/*
=================
WA_AuthInit

Reads the accounts and the sessions that are open. A server that had the one password of before (vs_webPassword)
and has no account yet gets its first one from it: the owner "owner" with that password.
=================
*/
void WA_AuthInit( void ) {
	memset( waKeyAsks, 0, sizeof( waKeyAsks ) );
	waHashed = qfalse;
	WA_LoadUsers();
	WA_LoadSessions();

	if ( !waNumUsers && vs_webPassword.string[0] ) {
		waUser_t *user = &waUsers[0];

		memset( user, 0, sizeof( *user ) );
		Q_strncpyz( user->name, "owner", sizeof( user->name ) );
		user->role = WA_ROLE_OWNER;
		if ( WA_SetPassword( user, vs_webPassword.string ) ) {
			waNumUsers = 1;
			WA_SaveUsers();
			trap->Print( S_COLOR_YELLOW "Web admin: the panel has accounts now. Its first one is \"owner\" with the password vs_webPassword has.\n"
				"Log in, change the password on the Account tab, and take vs_webPassword out of the config: it is not read again.\n" );
		}
		waHashed = qfalse;		// the first login does not have to wait for this
	}
}

void WA_AuthShutdown( void ) {
	// (the thread runs code of this module: it is over before the module goes)
	WA_HashJoin();
	crypto_wipe( waHashJob.hash, sizeof( waHashJob.hash ) );
	WA_SaveSessions();
}

/*
=================
WA_AuthConsoleCommand

"webuser" on the console of the server: the accounts of the panel
=================
*/
qboolean WA_AuthConsoleCommand( const char *cmd ) {
	char		what[16], name[WA_USER_NAME + 8], roleName[16], password[20] = "";
	const char	*why = "";
	waUser_t	*user;
	int			i;

	if ( Q_stricmp( cmd, "webuser" ) ) {
		return qfalse;
	}
	trap->Argv( 1, what, sizeof( what ) );
	trap->Argv( 2, name, sizeof( name ) );
	trap->Argv( 3, roleName, sizeof( roleName ) );
	user = WA_FindUser( name );

	if ( !Q_stricmp( what, "add" ) ) {
		if ( !WA_AddUser( name, WA_RoleByName( roleName ), password, &why ) ) {
			trap->Print( "No account was made (%s). usage: webuser add <name> <moderator|admin|owner>, the name of letters, digits and _ - .\n", why );
			return qtrue;
		}
		trap->Print( "The account %s (%s) is made. Its password, shown this once, to be changed on the Account tab of the page:\n%s\n", name, roleName, password );
	} else if ( !Q_stricmp( what, "token" ) && user ) {
		// for a script on the machine of the server, which has no page to ask for one
		byte token[32];
		char text[65];

		if ( BG_AuthRandom( token, sizeof( token ) ) ) {
			Web_SHA256( token, sizeof( token ), user->token );
			user->hasToken = qtrue;
			WA_SaveUsers();
			Web_Hex( token, sizeof( token ), text );
			trap->Print( "The token of the account %s, shown this once (a script sends \"Authorization: Bearer <token>\"):\n%s\n", user->name, text );
			crypto_wipe( token, sizeof( token ) );
			crypto_wipe( text, sizeof( text ) );
		}
	} else if ( !Q_stricmp( what, "reset" ) && user ) {
		if ( WA_ResetUser( user, password ) ) {
			trap->Print( "The account %s has a new password, shown this once. Its second step and its token are off, its sessions are closed:\n%s\n", user->name, password );
		}
	} else if ( !Q_stricmp( what, "remove" ) && user ) {
		trap->Print( "The account %s is removed.\n", user->name );
		WA_RemoveUser( user );
	} else if ( !Q_stricmp( what, "role" ) && user && WA_RoleByName( roleName ) != WA_ROLE_NONE ) {
		user->role = WA_RoleByName( roleName );
		WA_SaveUsers();
		trap->Print( "The account %s is a %s now.\n", user->name, WA_RoleName( user->role ) );
	} else {
		trap->Print( "The accounts of the web admin panel (%i):\n", waNumUsers );
		for ( i = 0; i < waNumUsers; i++ ) {
			trap->Print( "  %-24s %-10s%s%s\n", waUsers[i].name, WA_RoleName( waUsers[i].role ), waUsers[i].hasTotp ? " second step" : "",
				waUsers[i].hasToken ? " token" : "" );
		}
		trap->Print( "webuser add <name> <moderator|admin|owner>, webuser reset <name>, webuser role <name> <role>, webuser token <name>, webuser remove <name>\n" );
	}
	crypto_wipe( password, sizeof( password ) );
	// The wait between two passwords that are hashed is against a visitor who makes the server work. What was
	// typed on its console is not his: the first login after it does not have to come again.
	waHashed = qfalse;
	return qtrue;
}
