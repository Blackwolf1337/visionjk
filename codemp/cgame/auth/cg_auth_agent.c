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

// cg_auth_agent.c -- asks the ssh-agent of the player for his keys and for a signature. Not from JA+.
//
// An agent keeps secret keys and signs with them for programs that ask, so the key never has to lie in a file
// the game reads: the agent of OpenSSH, the one of 1Password, of KeePassXC, of gpg. It listens on a socket of
// the machine (Linux, macOS: the path in SSH_AUTH_SOCK) or on a named pipe (Windows: \\.\pipe\openssh-ssh-agent).
// The protocol is draft-miller-ssh-agent: a question and an answer are a length of four bytes, a type of one
// byte and what belongs to the type.
//
//   SSH_AGENTC_REQUEST_IDENTITIES 11                      ->  SSH_AGENT_IDENTITIES_ANSWER 12, count, (key, comment)...
//   SSH_AGENTC_SIGN_REQUEST 13, key, data, flags          ->  SSH_AGENT_SIGN_RESPONSE 14, signature
//                                                         or  SSH_AGENT_FAILURE 5
//
// An agent may ask the player before it answers (1Password shows a window, and wants to be unlocked first), which
// takes as long as the player takes. So nothing in here waits: the socket does not block, a question is sent and
// CG_AgentPoll() looks once a frame whether the answer is there. The game goes on meanwhile.
//
// The file knows neither the cgame nor the engine, so it can be built by itself and tried against a real agent
// (japlus_port/tools/authtest). Not in a browser: a page has no way to an agent.

#include <string.h>
#include <stdlib.h>

#include "qcommon/q_shared.h"
#include "cg_auth_agent.h"

#define AGENT_FAILURE				5
#define AGENT_REQUEST_IDENTITIES	11
#define AGENT_IDENTITIES_ANSWER		12
#define AGENT_SIGN_REQUEST			13
#define AGENT_SIGN_RESPONSE			14

#define AGENT_KEY_TYPE				"ssh-ed25519"
#define AGENT_KEY_TYPE_LEN			11
#define AGENT_BLOB_SIZE				( 4 + AGENT_KEY_TYPE_LEN + 4 + 32 )
#define AGENT_OUT_SIZE				512
#define AGENT_IN_SIZE				65536		// an agent with a lot of keys, RSA ones among them
#define AGENT_MAX_DATA				256

#if defined(__EMSCRIPTEN__)

qboolean CG_AgentRequestKeys( const char *path, char *why, int whySize ) {
	(void)path;
	Q_strncpyz( why, "a page in a browser has no way to an ssh-agent", whySize );
	return qfalse;
}

qboolean CG_AgentRequestSign( const char *path, const byte *pub, const byte *data, int dataLen, char *why, int whySize ) {
	(void)pub; (void)data; (void)dataLen;
	return CG_AgentRequestKeys( path, why, whySize );
}

agentState_t	CG_AgentPoll( void ) { return AGENT_IDLE; }
int				CG_AgentKeys( agentKey_t *out, int max ) { (void)out; (void)max; return 0; }
qboolean		CG_AgentSignature( byte *signature ) { (void)signature; return qfalse; }
void			CG_AgentClose( void ) {}

#else

#if defined(_WIN32)
	#include <windows.h>
#else
	#include <errno.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <sys/socket.h>
	#include <sys/un.h>
#endif

static struct {
#if defined(_WIN32)
	HANDLE			pipe;
#else
	int				fd;
#endif
	qboolean		open;
	agentState_t	state;
	int				request;			// AGENT_REQUEST_IDENTITIES or AGENT_SIGN_REQUEST
	byte			out[AGENT_OUT_SIZE];
	int				outLen, outSent;
	byte			in[AGENT_IN_SIZE];
	int				inLen;
} agent;

static void Agent_PutLong( byte *out, unsigned int v ) {
	out[0] = (byte)( v >> 24 );
	out[1] = (byte)( v >> 16 );
	out[2] = (byte)( v >> 8 );
	out[3] = (byte)v;
}

static unsigned int Agent_GetLong( const byte *in ) {
	return ( (unsigned int)in[0] << 24 ) | ( (unsigned int)in[1] << 16 ) | ( (unsigned int)in[2] << 8 ) | in[3];
}

// The next string of an answer (a length and that many bytes), qfalse if it does not lie inside of the answer.
// *pos is behind it afterwards.
static qboolean Agent_GetString( const byte *buf, int size, int *pos, const byte **data, int *len ) {
	unsigned int n;

	if ( *pos < 0 || size - *pos < 4 ) {
		return qfalse;
	}
	n = Agent_GetLong( buf + *pos );
	if ( n > (unsigned int)( size - *pos - 4 ) ) {
		return qfalse;
	}
	*data = buf + *pos + 4;
	*len = (int)n;
	*pos += 4 + (int)n;
	return qtrue;
}

// a public Ed25519 key as ssh passes it around: the name of the kind and the 32 bytes
static void Agent_Blob( const byte *pub, byte *blob ) {
	Agent_PutLong( blob, AGENT_KEY_TYPE_LEN );
	memcpy( blob + 4, AGENT_KEY_TYPE, AGENT_KEY_TYPE_LEN );
	Agent_PutLong( blob + 4 + AGENT_KEY_TYPE_LEN, 32 );
	memcpy( blob + 4 + AGENT_KEY_TYPE_LEN + 4, pub, 32 );
}

void CG_AgentClose( void ) {
	if ( agent.open ) {
#if defined(_WIN32)
		CloseHandle( agent.pipe );
#else
		close( agent.fd );
#endif
	}
	memset( &agent, 0, sizeof( agent ) );
}

/*
=================
Agent_Open

Connects to the agent. Never waits: a socket of the machine is connected at once or not at all.
=================
*/
static qboolean Agent_Open( const char *path, char *why, int whySize ) {
	CG_AgentClose();

#if defined(_WIN32)
	if ( !path || !path[0] ) {
		path = "\\\\.\\pipe\\openssh-ssh-agent";
	}
	agent.pipe = CreateFileA( path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
	if ( agent.pipe == INVALID_HANDLE_VALUE ) {
		Com_sprintf( why, whySize, "no ssh-agent answers at %s (is the agent of OpenSSH or of 1Password running?)", path );
		return qfalse;
	}
#else
	struct sockaddr_un	address;
	int					flags;

	if ( !path || !path[0] ) {
		path = getenv( "SSH_AUTH_SOCK" );
	}
	if ( !path || !path[0] ) {
		Q_strncpyz( why, "no ssh-agent is known: SSH_AUTH_SOCK is not set for the game. Set cg_authAgentPath to its socket "
			"(1Password: ~/.1password/agent.sock)", whySize );
		return qfalse;
	}
	memset( &address, 0, sizeof( address ) );
	address.sun_family = AF_UNIX;
	if ( strlen( path ) >= sizeof( address.sun_path ) ) {
		Q_strncpyz( why, "the path of the ssh-agent is too long for a socket", whySize );
		return qfalse;
	}
	Q_strncpyz( address.sun_path, path, sizeof( address.sun_path ) );

	agent.fd = socket( AF_UNIX, SOCK_STREAM, 0 );
	if ( agent.fd < 0 ) {
		Q_strncpyz( why, "the system gave no socket", whySize );
		return qfalse;
	}
	flags = fcntl( agent.fd, F_GETFL, 0 );
	if ( flags < 0 || fcntl( agent.fd, F_SETFL, flags | O_NONBLOCK ) < 0 || fcntl( agent.fd, F_SETFD, FD_CLOEXEC ) < 0 ) {
		close( agent.fd );
		Q_strncpyz( why, "the socket could not be set up", whySize );
		return qfalse;
	}
#if defined(SO_NOSIGPIPE)
	{
		// macOS has no flag for send(): without this a line the agent closed would end the game
		int on = 1;

		setsockopt( agent.fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof( on ) );
	}
#endif
	if ( connect( agent.fd, (struct sockaddr *)&address, sizeof( address ) ) < 0 ) {
		close( agent.fd );
		Com_sprintf( why, whySize, "no ssh-agent answers at %s", path );
		return qfalse;
	}
#endif
	agent.open = qtrue;
	return qtrue;
}

// Sends what is left of the question. qfalse: the line is gone.
static qboolean Agent_Flush( void ) {
	while ( agent.outSent < agent.outLen ) {
#if defined(_WIN32)
		DWORD written = 0;

		// a few hundred bytes into the buffer of the pipe
		if ( !WriteFile( agent.pipe, agent.out + agent.outSent, (DWORD)( agent.outLen - agent.outSent ), &written, NULL ) || !written ) {
			return qfalse;
		}
		agent.outSent += (int)written;
#else
	#if defined(MSG_NOSIGNAL)
		const ssize_t n = send( agent.fd, agent.out + agent.outSent, (size_t)( agent.outLen - agent.outSent ), MSG_NOSIGNAL );
	#else
		const ssize_t n = send( agent.fd, agent.out + agent.outSent, (size_t)( agent.outLen - agent.outSent ), 0 );
	#endif

		if ( n < 0 ) {
			if ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) {
				return qtrue;		// the rest with the next frame
			}
			return qfalse;
		}
		agent.outSent += (int)n;
#endif
	}
	return qtrue;
}

// the length in front of what was put into agent.out + 4, and off it goes
static qboolean Agent_Send( int request, int length, char *why, int whySize ) {
	Agent_PutLong( agent.out, (unsigned int)length );
	agent.outLen = 4 + length;
	agent.outSent = 0;
	agent.inLen = 0;
	agent.request = request;
	if ( !Agent_Flush() ) {
		CG_AgentClose();
		Q_strncpyz( why, "the ssh-agent closed the line", whySize );
		return qfalse;
	}
	agent.state = AGENT_BUSY;
	return qtrue;
}

qboolean CG_AgentRequestKeys( const char *path, char *why, int whySize ) {
	if ( !Agent_Open( path, why, whySize ) ) {
		return qfalse;
	}
	agent.out[4] = AGENT_REQUEST_IDENTITIES;
	return Agent_Send( AGENT_REQUEST_IDENTITIES, 1, why, whySize );
}

qboolean CG_AgentRequestSign( const char *path, const byte *pub, const byte *data, int dataLen, char *why, int whySize ) {
	byte	*p;

	if ( dataLen < 1 || dataLen > AGENT_MAX_DATA ) {
		Q_strncpyz( why, "that is too long to be signed", whySize );
		return qfalse;
	}
	if ( !Agent_Open( path, why, whySize ) ) {
		return qfalse;
	}
	p = agent.out + 4;
	*p++ = AGENT_SIGN_REQUEST;
	Agent_PutLong( p, AGENT_BLOB_SIZE );
	Agent_Blob( pub, p + 4 );
	p += 4 + AGENT_BLOB_SIZE;
	Agent_PutLong( p, (unsigned int)dataLen );
	memcpy( p + 4, data, dataLen );
	p += 4 + dataLen;
	Agent_PutLong( p, 0 );		// no flags: they are for RSA
	p += 4;
	return Agent_Send( AGENT_SIGN_REQUEST, (int)( p - agent.out ) - 4, why, whySize );
}

/*
=================
CG_AgentPoll
=================
*/
agentState_t CG_AgentPoll( void ) {
	unsigned int	length;
	qboolean		closed = qfalse;		// by the agent: fine with a whole answer in hand

	if ( agent.state != AGENT_BUSY ) {
		return agent.state;
	}
	if ( !Agent_Flush() ) {
		CG_AgentClose();
		return ( agent.state = AGENT_FAILED );
	}

	// what has come by now
	while ( !closed ) {
		const int room = (int)sizeof( agent.in ) - agent.inLen;
#if defined(_WIN32)
		DWORD there = 0, got = 0;

		if ( !PeekNamedPipe( agent.pipe, NULL, 0, NULL, &there, NULL ) ) {
			closed = qtrue;
			break;
		}
		if ( !there || room <= 0 ) {
			break;
		}
		if ( !ReadFile( agent.pipe, agent.in + agent.inLen, Q_min( there, (DWORD)room ), &got, NULL ) || !got ) {
			closed = qtrue;
			break;
		}
		agent.inLen += (int)got;
#else
		ssize_t n;

		if ( room <= 0 ) {
			break;
		}
		n = recv( agent.fd, agent.in + agent.inLen, (size_t)room, 0 );
		if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) ) {
			break;
		}
		if ( n <= 0 ) {
			closed = qtrue;
			break;
		}
		agent.inLen += (int)n;
#endif
	}

	length = ( agent.inLen >= 4 ) ? Agent_GetLong( agent.in ) : 0;
	// nothing at all, or more keys than there is room for
	if ( agent.inLen >= 4 && ( length < 1 || length > sizeof( agent.in ) - 4 ) ) {
		CG_AgentClose();
		return ( agent.state = AGENT_FAILED );
	}
	if ( agent.inLen < 4 || (unsigned int)agent.inLen < 4 + length ) {
		if ( closed ) {
			CG_AgentClose();
			return ( agent.state = AGENT_FAILED );
		}
		return AGENT_BUSY;
	}

	// The answer is all there, the line is not needed any more. What was read stays for CG_AgentKeys() and
	// CG_AgentSignature().
	if ( agent.open ) {
#if defined(_WIN32)
		CloseHandle( agent.pipe );
#else
		close( agent.fd );
#endif
		agent.open = qfalse;
	}
	agent.inLen = 4 + (int)length;
	return ( agent.state = AGENT_DONE );
}

/*
=================
CG_AgentKeys
=================
*/
int CG_AgentKeys( agentKey_t *out, int max ) {
	const byte		*buf = agent.in + 4, *blob, *comment;
	const int		size = agent.inLen - 4;
	unsigned int	count, i;
	int				pos = 5, blobLen, commentLen, n = 0, c;

	if ( agent.state != AGENT_DONE || agent.request != AGENT_REQUEST_IDENTITIES || size < 5 || buf[0] != AGENT_IDENTITIES_ANSWER ) {
		return 0;
	}
	count = Agent_GetLong( buf + 1 );
	for ( i = 0; i < count && n < max; i++ ) {
		if ( !Agent_GetString( buf, size, &pos, &blob, &blobLen ) || !Agent_GetString( buf, size, &pos, &comment, &commentLen ) ) {
			break;
		}
		// keys of other kinds (RSA, ECDSA) are of no use here
		if ( blobLen != AGENT_BLOB_SIZE || Agent_GetLong( blob ) != AGENT_KEY_TYPE_LEN || memcmp( blob + 4, AGENT_KEY_TYPE, AGENT_KEY_TYPE_LEN )
			|| Agent_GetLong( blob + 4 + AGENT_KEY_TYPE_LEN ) != 32 ) {
			continue;
		}
		memcpy( out[n].pub, blob + 4 + AGENT_KEY_TYPE_LEN + 4, 32 );
		for ( c = 0; c < commentLen && c < AGENT_COMMENT_SIZE - 1; c++ ) {
			// it is printed on the console
			out[n].comment[c] = ( comment[c] >= ' ' && comment[c] < 127 && comment[c] != '"' && comment[c] != '^' && comment[c] != '%' )
				? (char)comment[c] : '_';
		}
		out[n].comment[c] = '\0';
		n++;
	}
	return n;
}

/*
=================
CG_AgentSignature
=================
*/
qboolean CG_AgentSignature( byte *signature ) {
	const byte	*buf = agent.in + 4, *whole, *part;
	const int	size = agent.inLen - 4;
	int			pos = 1, wholeLen, partLen;

	if ( agent.state != AGENT_DONE || agent.request != AGENT_SIGN_REQUEST || size < 1 || buf[0] != AGENT_SIGN_RESPONSE ) {
		return qfalse;
	}
	// a signature is the name of its kind and its bytes
	if ( !Agent_GetString( buf, size, &pos, &whole, &wholeLen ) ) {
		return qfalse;
	}
	pos = 0;
	if ( !Agent_GetString( whole, wholeLen, &pos, &part, &partLen ) || partLen != AGENT_KEY_TYPE_LEN || memcmp( part, AGENT_KEY_TYPE, partLen ) ) {
		return qfalse;
	}
	if ( !Agent_GetString( whole, wholeLen, &pos, &part, &partLen ) || partLen != 64 ) {
		return qfalse;
	}
	memcpy( signature, part, 64 );
	return qtrue;
}

#endif	// !__EMSCRIPTEN__
