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

#pragma once

// cg_auth_agent.c: the ssh-agent of the player signs for the admin login, see cg_auth.c

#define AGENT_MAX_KEYS		16
#define AGENT_COMMENT_SIZE	64

typedef struct agentKey_s {
	byte	pub[32];
	char	comment[AGENT_COMMENT_SIZE];
} agentKey_t;

typedef enum agentState_e {
	AGENT_IDLE,			// nothing was asked
	AGENT_BUSY,			// the answer is not there yet: ask again with the next frame
	AGENT_DONE,
	AGENT_FAILED		// the agent closed the line, or what it sent is no answer
} agentState_t;

// One question at a time. path: where the agent listens, "" for where the system says (SSH_AUTH_SOCK, on Windows
// the pipe of OpenSSH). qfalse and a reason in why: it could not be asked. Neither call waits for the answer.
qboolean		CG_AgentRequestKeys( const char *path, char *why, int whySize );
qboolean		CG_AgentRequestSign( const char *path, const byte *pub, const byte *data, int dataLen, char *why, int whySize );

// once a frame while a question is out
agentState_t	CG_AgentPoll( void );

// after AGENT_DONE: the Ed25519 keys the agent has, and the 64 bytes of the signature. qfalse: the agent said no
// (the player did not allow it, or the key is gone).
int				CG_AgentKeys( agentKey_t *out, int max );
qboolean		CG_AgentSignature( byte *signature );

void			CG_AgentClose( void );
