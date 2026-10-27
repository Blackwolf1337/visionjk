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

// sv_idstest.cpp -- a dedicated server that does on purpose what no server of the game does, so that the intrusion
// detection of the client (client/cl_ids.cpp) can be run against it. Not from OpenJK.
//
// Only in visionjkded-idstest (BuildDevIdsTest), never in a release: the file is not part of any other program.
// tools/test/ids-test.sh runs it. vs_idsTest is a sum of what the server does wrong:
//   1   systeminfo: cvars a server may not set (cl_allowDownload, rconPassword)
//   2   systeminfo: a protected cvar (fs_basepath). The client refuses it as one it may not set: the drop for a
//       protected cvar in Cvar_Server_Set is only reached by one a server may set otherwise, and there is none
//   4   systeminfo: a path in the place of the game folder (fs_game ../idstest)
//   8   a download: block zero says half of the size of the file
//   16  a download block in every snapshot of a client that asked for none
// The cvar is in the systeminfo itself, so a change of it sends the systeminfo again.

#include "server.h"

#ifdef VISIONJK_IDS_TEST

#define IDSTEST_CVARS		1
#define IDSTEST_PROTECTED	2
#define IDSTEST_GAMEDIR		4
#define IDSTEST_SIZE		8
#define IDSTEST_UNASKED		16

static cvar_t	*vs_idsTest;

void SV_IDSTest_Init( void ) {
	vs_idsTest = Cvar_Get( "vs_idsTest", "0", CVAR_SYSTEMINFO, "Test server only: what it does wrong toward its clients, a sum. 1 cvars it may not set, 2 a protected cvar, 4 a path as game folder, 8 a download says half its size, 16 download data nobody asked for" );
	Com_Printf( S_COLOR_YELLOW "visionjkded-idstest: this server misbehaves on purpose (vs_idsTest), never run it for players\n" );
}

// the systeminfo as it goes to the clients
const char *SV_IDSTest_SystemInfo( const char *info ) {
	static char	out[BIG_INFO_STRING];

	Q_strncpyz( out, info, sizeof( out ) );
	if ( vs_idsTest->integer & IDSTEST_CVARS ) {
		Info_SetValueForKey_Big( out, "cl_allowDownload", "0" );
		Info_SetValueForKey_Big( out, "rconPassword", "idstest" );
	}
	if ( vs_idsTest->integer & IDSTEST_PROTECTED ) {
		Info_SetValueForKey_Big( out, "fs_basepath", "/idstest" );
	}
	if ( vs_idsTest->integer & IDSTEST_GAMEDIR ) {
		Info_SetValueForKey_Big( out, "fs_game", "../idstest" );
	}
	return out;
}

// the size of a download as block zero tells it
int SV_IDSTest_DownloadSize( int size ) {
	return ( vs_idsTest->integer & IDSTEST_SIZE ) ? size / 2 : size;
}

// a download message for a client in the game that asked for none. Only the command: a client that asked for
// nothing reads no further (CL_ParseDownload), so a block behind it would be taken for the next commands.
void SV_IDSTest_Unasked( client_t *cl, msg_t *msg ) {
	if ( !( vs_idsTest->integer & IDSTEST_UNASKED ) || *cl->downloadName || cl->state != CS_ACTIVE ) {
		return;
	}
	MSG_WriteByte( msg, svc_download );
}

#endif // VISIONJK_IDS_TEST
