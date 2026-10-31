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
web_models.h -- the player models of the model store of the site (jka.vision/models/, issue #6): its list
index.txt, and which files a pak of one model may have. The store (deploy/models/modelstore.py of the website)
takes an upload only by the same rules, the client looks again before it loads one (client/cl_models.cpp). Plain
text work, the files themselves are fetched and read by the caller.

index.txt has a line for each model, "<name> <bytes> <sha256, 64 hex digits>", lines with # are none.
*/

#pragma once

#include <stddef.h>

#include "qcommon/q_platform.h"
#include "web_http.h"		// WEB_SHA256_SIZE

#if defined(__cplusplus)
extern "C" {
#endif

#define WEB_MODEL_NAME		33						// 2 to 32 of a-z, 0-9 and _, and the end
#define WEB_MODEL_MOST		( 100 * 1024 * 1024 )	// bytes of a pak of the store at most

// name is one the store can have: 2 to 32 of a-z, 0-9 and _ (the folder in models/players, small letters)
qboolean	WEB_ModelName( const char *name );
// The model of the "model" key of a userinfo ("<model>/<skin>", or only "<model>"), in small letters, into out
// (WEB_MODEL_NAME). qfalse: no name the store can have.
qboolean	WEB_ModelFromInfo( const char *value, char *out, size_t outSize );
// A file of a pak may be in the pak of model: models/players/<model>/ (the model, its skins and textures,
// sounds.cfg, animevents.cfg, animation.cfg), sound/chars/<model>/ (its sounds), shaders/<model>.shader, or a
// readme (.txt) at the top. Without regard to case, as the engine looks names up. A name that is no plain path
// (.., //, a backslash, a drive, quotes, ; or control characters) is never one.
qboolean	WEB_ModelEntryAllowed( const char *entry, const char *model );
// The line of model in the text of index.txt: its size and SHA-256. qfalse: not listed, or a line that is broken
// or larger than WEB_MODEL_MOST.
qboolean	WEB_ModelIndexFind( const char *text, const char *model, long long *bytes, unsigned char sha256[WEB_SHA256_SIZE] );

#if defined(__cplusplus)
} // extern "C"
#endif
