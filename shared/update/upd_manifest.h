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
upd_manifest.h -- the updater of the client, the part that is plain text work: the file list of a release as
the website has it, version numbers, and which paths and addresses the client is willing to touch. Not from
JA+, see docs/japlus/update.md. Nothing in here knows the engine, so it is built and tested by itself.

The file list of a release, one for every system, text with a line for every file:

	visionjk-update 1
	version 0.1.1
	file <bytes> <sha256, 64 hex digits> <path>
	keep <bytes> <sha256> <path>

The first line is the format. A client does not read a list of another format than its own. A line that
starts with a word it does not know is skipped, so a later release can say more without a new format.
"keep" is a file a player or a server owner changes (the .cfg files): it is put there if it is missing and
never replaced.

A version is a release, "0.2.8", or a development build before it, "0.2.8-dev.2". The development builds of a
release come before it, the release before the next development build: 0.2.8-dev.1 < 0.2.8-dev.2 < 0.2.8 <
0.2.9-dev.1. As numbers that is a fourth part, the number of the development build or UPD_STABLE.
*/

#pragma once

#include <stddef.h>

#include "qcommon/q_platform.h"
#include "web/web_http.h"		// WEB_SHA256_SIZE

#if defined(__cplusplus)
extern "C" {
#endif

#define UPD_FORMAT			1
#define UPD_VERSION_PARTS	4
#define UPD_STABLE			10000		// the fourth part of a release, above every development build (9999 at most)
#define UPD_VERSION_TEXT	32
#define UPD_MAX_FILES		256
#define UPD_MAX_PATH		96
#define UPD_MAX_ADDRESS		200
#define UPD_MAX_LIST		( 128 * 1024 )				// bytes of a file list. The one of a release has about 3000
#define UPD_MAX_FILE_BYTES	( 1024LL * 1024 * 1024 )	// one file of a release. The biggest so far has 85 MB

// What the updater puts beside a file while it works: the new one before it is in place, the old one until the
// next start. And its note of a swap that is under way, in the folder of the game.
#define UPD_NEW_ENDING		".vjknew"
#define UPD_OLD_ENDING		".vjkold"
#define UPD_JOURNAL			"visionjk-update.txt"

typedef struct updFile_s {
	char			path[UPD_MAX_PATH];		// from the folder of the game, with forward slashes
	long long		bytes;
	unsigned char	sha256[WEB_SHA256_SIZE];
	qboolean		keep;					// only put there if it is missing
} updFile_t;

typedef struct updList_s {
	int			version[UPD_VERSION_PARTS];
	int			numFiles;
	updFile_t	files[UPD_MAX_FILES];
} updList_t;

qboolean	UPD_ParseVersion( const char *text, int version[UPD_VERSION_PARTS] );
qboolean	UPD_ParseRelease( const char *tag, int version[UPD_VERSION_PARTS] );
qboolean	UPD_ParseBuild( const char *tag, int version[UPD_VERSION_PARTS] );
int			UPD_CompareVersion( const int a[UPD_VERSION_PARTS], const int b[UPD_VERSION_PARTS] );
qboolean	UPD_IsDev( const int version[UPD_VERSION_PARTS] );
void		UPD_VersionText( const int version[UPD_VERSION_PARTS], char *out, size_t outSize );
qboolean	UPD_PathAllowed( const char *path );
qboolean	UPD_AddressAllowed( const char *address, qboolean plainToo );
const char	*UPD_ParseList( const char *text, size_t length, updList_t *list );

#if defined(__cplusplus)
} // extern "C"
#endif
