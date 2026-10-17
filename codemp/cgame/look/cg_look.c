/*
===========================================================================
Copyright (C) 2015 - 2023, TaystJK contributors
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

// cg_look.c -- hats and capes (2026-10-17), after the cosmetics of TaystJK and jaPRO, in their file layout so their
// paks work: a hat is models/cosmetics/hats/<name>.md3 on the bolt *head_top of the player, a cape
// models/cosmetics/capes/<name>.md3 on *back, and settings/cosmetics/hats|capes/<name>.cosmetic (JSON) may move
// one for a model and skin. What one wears (v_hat, v_cape) reaches the other players of this tree: on a server of
// this tree through the game module (status/g_status.c, "vs_status look" and "vs_look"), on any other through the
// masterserver (VOICE_LOOK, voice/cg_voice.c). Legacy clients see nothing: they would need the models. A player
// whose hat this machine lacks wears none here.

#include "cgame/cg_local.h"

#define LOOK_MAX_ITEMS		128
#define LOOK_NAME			32
#define LOOK_SEND_FIRST		3000	// "vs_status look" goes to the server this long after the first frame (after "vs_status all"),
#define LOOK_SEND_MSEC		4000	//	again this often while the server did not say it back,
#define LOOK_MAX_SENDS		6		//	and this many times at most
#define LOOK_MASTER_MSEC	5000	// through the masterserver: the own look is said this often,
#define LOOK_MASTER_KEEP	15000	//	and one of somebody else that was not said again for this long is over

typedef enum {
	LOOK_HAT,
	LOOK_CAPE,
	LOOK_KINDS
} lookKind_t;

static const char *lookFolder[LOOK_KINDS] = { "hats", "capes" };
static const char *lookBolt[LOOK_KINDS] = { "*head_top", "*back" };

typedef struct {
	char		name[LOOK_NAME];
	qhandle_t	model;			// registered when first worn, -1: it does not load
} lookItem_t;

typedef struct {
	char		worn[LOOK_KINDS][LOOK_NAME];	// what he says he wears, "" none
	int			masterTime;						// when the masterserver last said it, 0: from the server or nobody
	// the offsets of the .cosmetic file for what he wears on his model and skin
	char		offsetKey[LOOK_KINDS][LOOK_NAME + MAX_QPATH * 2];
	vec3_t		offset[LOOK_KINDS];
} lookClient_t;

static struct {
	lookItem_t		items[LOOK_KINDS][LOOK_MAX_ITEMS];
	int				numItems[LOOK_KINDS];
	qboolean		listed;
	lookClient_t	clients[MAX_CLIENTS];
	// the own look to the server
	char			sent[2 * LOOK_NAME + 2];	// what was last sent
	qboolean		confirmed;					// the server said it back
	int				sends;
	int				sendTime;
	int				firstFrame;
	int				masterTime;					// when it was last said to the masterserver
} look;

static void Look_List( void ) {
	char	list[8192];
	int		kind, i, count;

	for ( kind = 0; kind < LOOK_KINDS; kind++ ) {
		const char *name = list;

		look.numItems[kind] = 0;
		count = trap->FS_GetFileList( va( "models/cosmetics/%s", lookFolder[kind] ), ".md3", list, sizeof( list ) );
		for ( i = 0; i < count && look.numItems[kind] < LOOK_MAX_ITEMS; i++, name += strlen( name ) + 1 ) {
			lookItem_t	*item = &look.items[kind][look.numItems[kind]];
			char		base[MAX_QPATH];

			COM_StripExtension( name, base, sizeof( base ) );
			if ( !base[0] || strlen( base ) >= LOOK_NAME || strchr( base, '/' ) || strchr( base, ' ' ) ) {
				continue;
			}
			Q_strncpyz( item->name, base, sizeof( item->name ) );
			item->model = 0;
			look.numItems[kind]++;
		}
	}
	look.listed = qtrue;
}

static lookItem_t *Look_Find( lookKind_t kind, const char *name ) {
	int i;

	if ( !look.listed ) {
		Look_List();
	}
	if ( !name[0] || !strcmp( name, "-" ) ) {
		return NULL;
	}
	for ( i = 0; i < look.numItems[kind]; i++ ) {
		if ( !Q_stricmp( look.items[kind][i].name, name ) ) {
			return &look.items[kind][i];
		}
	}
	return NULL;
}

static qhandle_t Look_Model( lookKind_t kind, lookItem_t *item ) {
	if ( !item->model ) {
		item->model = trap->R_RegisterModel( va( "models/cosmetics/%s/%s.md3", lookFolder[kind], item->name ) );
		if ( !item->model ) {
			item->model = -1;
		}
	}
	return item->model > 0 ? item->model : 0;
}

void CG_LookInit( void ) {
	memset( &look, 0, sizeof( look ) );
}

// a name of a hat or a cape as it goes over the network: letters, digits, _ and -
static void Look_Clean( const char *in, char *out, int outSize ) {
	int len = 0;

	for ( ; *in && len < outSize - 1; in++ ) {
		if ( ( *in >= '0' && *in <= '9' ) || ( *in >= 'a' && *in <= 'z' ) || ( *in >= 'A' && *in <= 'Z' ) || *in == '_' || *in == '-' ) {
			out[len++] = *in;
		}
	}
	out[len] = '\0';
	if ( !strcmp( out, "-" ) || !Q_stricmp( out, "none" ) ) {
		out[0] = '\0';
	}
}

static void Look_Set( int client, const char *hat, const char *cape ) {
	lookClient_t *lc = &look.clients[client];

	Look_Clean( hat, lc->worn[LOOK_HAT], sizeof( lc->worn[0] ) );
	Look_Clean( cape, lc->worn[LOOK_CAPE], sizeof( lc->worn[0] ) );
}

// the own look as it is sent: "<hat> <cape>", "-" for none
static void Look_Own( char *out, int outSize ) {
	char hat[LOOK_NAME], cape[LOOK_NAME];

	Look_Clean( v_hat.string, hat, sizeof( hat ) );
	Look_Clean( v_cape.string, cape, sizeof( cape ) );
	Com_sprintf( out, outSize, "%s %s", hat[0] ? hat : "-", cape[0] ? cape : "-" );
}

/*
=================
CG_LookServerCommand

vs_look <client> <hat> <cape>
=================
*/
void CG_LookServerCommand( void ) {
	const int client = atoi( CG_Argv( 1 ) );

	if ( client < 0 || client >= MAX_CLIENTS ) {
		return;
	}
	Look_Set( client, CG_Argv( 2 ), CG_Argv( 3 ) );
	look.clients[client].masterTime = 0;
	if ( cg.snap && client == cg.snap->ps.clientNum ) {
		char own[sizeof( look.sent )];

		Look_Own( own, sizeof( own ) );
		look.confirmed = (qboolean)!Q_stricmp( own, va( "%s %s", CG_Argv( 2 ), CG_Argv( 3 ) ) );
	}
}

// what the masterserver hands on from somebody (voice/cg_voice.c)
void CG_LookFromMaster( int client, const char *text ) {
	char hat[LOOK_NAME], cape[LOOK_NAME];

	if ( client < 0 || client >= MAX_CLIENTS || sscanf( text, "%31s %31s", hat, cape ) != 2 ) {
		return;
	}
	Look_Set( client, hat, cape );
	look.clients[client].masterTime = cg.time ? cg.time : 1;
}

// a player left, or this slot is somebody else now (CG_NewClientInfo)
void CG_LookClear( int client ) {
	if ( client >= 0 && client < MAX_CLIENTS ) {
		memset( &look.clients[client], 0, sizeof( look.clients[0] ) );
	}
}

/*
=================
CG_LookFrame

Says the own look to the server (vs_caps bit 7) or to the masterserver, and forgets what the master said of
somebody long ago
=================
*/
void CG_LookFrame( void ) {
	const int	now = trap->Milliseconds();
	char		own[sizeof( look.sent )];
	int			i;

	if ( !cg.snap || cg.demoPlayback ) {
		return;
	}
	if ( !look.firstFrame ) {
		look.firstFrame = now ? now : 1;
	}
	Look_Own( own, sizeof( own ) );
	if ( Q_stricmp( own, look.sent ) ) {
		Q_strncpyz( look.sent, own, sizeof( look.sent ) );
		look.confirmed = qfalse;
		look.sends = 0;
		look.masterTime = 0;
	}

	if ( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vs_caps" ) ) & 128 ) {
		// the server says it back when it has it; the flood protection of a server drops a command that comes
		// too soon after another one
		if ( !look.confirmed && look.sends < LOOK_MAX_SENDS && now - look.firstFrame >= LOOK_SEND_FIRST
			&& ( !look.sends || now - look.sendTime >= LOOK_SEND_MSEC ) ) {
			trap->SendClientCommand( va( "vs_status look %s", own ) );
			look.sendTime = now;
			look.sends++;
		}
	} else if ( CG_VoiceLooks() && ( !look.masterTime || now - look.masterTime >= LOOK_MASTER_MSEC ) ) {
		CG_VoiceSendLook( own );
		look.masterTime = now;
	}

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		lookClient_t *lc = &look.clients[i];

		if ( lc->masterTime && ( cg.time - lc->masterTime > LOOK_MASTER_KEEP || cg.time < lc->masterTime ) ) {
			lc->worn[LOOK_HAT][0] = lc->worn[LOOK_CAPE][0] = '\0';
			lc->masterTime = 0;
		}
	}
}

/*
===============================================================================

The offsets of settings/cosmetics/<kind>/<name>.cosmetic, TaystJK's format:

  { "<model>": { "xOffset": 0, "yOffset": 0, "zOffset": -2, "modelFallback": true,
                 "<skin>": { "xOffset": ..., "yOffset": ..., "zOffset": ... }, "<prefix>*": { ... } }, ... }

A skin by its name, or the longest prefix with a *. Without one, the model's offsets when modelFallback is true,
else none. x forward, y right, z up, along the bolt (TaystJK added them along the axes of the world).

===============================================================================
*/

typedef struct {
	const char	*p;
	qboolean	error;
} lookJson_t;

static void Json_Space( lookJson_t *j ) {
	while ( *j->p && (unsigned char)*j->p <= ' ' ) {
		j->p++;
	}
}

static qboolean Json_String( lookJson_t *j, char *out, int outSize ) {
	int len = 0;

	Json_Space( j );
	if ( *j->p != '"' ) {
		j->error = qtrue;
		return qfalse;
	}
	for ( j->p++; *j->p && *j->p != '"'; j->p++ ) {
		if ( *j->p == '\\' && j->p[1] ) {
			j->p++;
		}
		if ( len < outSize - 1 ) {
			out[len++] = *j->p;
		}
	}
	out[len] = '\0';
	if ( *j->p != '"' ) {
		j->error = qtrue;
		return qfalse;
	}
	j->p++;
	return qtrue;
}

// skips a value of any kind: a string, an object or an array with all in them, or a number, true, false, null
static void Json_Skip( lookJson_t *j ) {
	char	dummy[4];
	int		depth = 0;

	Json_Space( j );
	if ( *j->p == '"' ) {
		Json_String( j, dummy, sizeof( dummy ) );
		return;
	}
	if ( *j->p != '{' && *j->p != '[' ) {
		while ( *j->p && *j->p != ',' && *j->p != '}' && *j->p != ']' && (unsigned char)*j->p > ' ' ) {
			j->p++;
		}
		return;
	}
	while ( *j->p ) {
		if ( *j->p == '"' ) {
			if ( !Json_String( j, dummy, sizeof( dummy ) ) ) {
				return;
			}
			continue;
		}
		if ( *j->p == '{' || *j->p == '[' ) {
			depth++;
		} else if ( ( *j->p == '}' || *j->p == ']' ) && --depth == 0 ) {
			j->p++;
			return;
		}
		j->p++;
	}
	j->error = qtrue;
}

typedef struct {
	vec3_t		offset;
	int			have;			// bits 0..2: x, y, z were given
	qboolean	fallback;
} lookOffsets_t;

// one object of offsets; with skin set, its keys that are objects are skins, the best match goes to *skinOut
static void Json_Offsets( lookJson_t *j, lookOffsets_t *out, const char *skin, lookOffsets_t *skinOut, int *skinMatch ) {
	char key[MAX_QPATH];

	Json_Space( j );
	if ( *j->p != '{' ) {
		j->error = qtrue;
		return;
	}
	j->p++;
	for ( ;; ) {
		Json_Space( j );
		if ( *j->p == '}' ) {
			j->p++;
			return;
		}
		if ( !Json_String( j, key, sizeof( key ) ) ) {
			return;
		}
		Json_Space( j );
		if ( *j->p != ':' ) {
			j->error = qtrue;
			return;
		}
		j->p++;
		Json_Space( j );
		if ( *j->p == '{' && skin ) {
			const size_t	len = strlen( key );
			int				match = 0;

			if ( !Q_stricmp( key, skin ) ) {
				match = 1000;		// the name itself beats any prefix
			} else if ( len && key[len - 1] == '*' && !Q_stricmpn( key, skin, (int)len - 1 ) ) {
				match = (int)len;
			}
			if ( match > *skinMatch ) {
				lookOffsets_t found;

				memset( &found, 0, sizeof( found ) );
				Json_Offsets( j, &found, NULL, NULL, NULL );
				if ( found.have == 7 ) {
					*skinOut = found;
					*skinMatch = match;
				}
			} else {
				Json_Skip( j );
			}
		} else if ( !Q_stricmp( key, "xOffset" ) || !Q_stricmp( key, "yOffset" ) || !Q_stricmp( key, "zOffset" ) ) {
			const int axis = tolower( key[0] ) - 'x';

			out->offset[axis] = (float)atoi( j->p );		// whole numbers, as TaystJK
			out->have |= 1 << axis;
			Json_Skip( j );
		} else if ( !Q_stricmp( key, "modelFallback" ) ) {
			out->fallback = (qboolean)!Q_stricmpn( j->p, "true", 4 );
			Json_Skip( j );
		} else {
			Json_Skip( j );
		}
		if ( j->error ) {
			return;
		}
		Json_Space( j );
		if ( *j->p == ',' ) {
			j->p++;
		}
	}
}

static void Look_LoadOffsets( lookKind_t kind, const char *name, const char *model, const char *skin, vec3_t out ) {
	char			text[16384], key[MAX_QPATH];
	fileHandle_t	f;
	int				len;
	lookJson_t		j;

	VectorClear( out );
	len = trap->FS_Open( va( "settings/cosmetics/%s/%s.cosmetic", lookFolder[kind], name ), &f, FS_READ );
	if ( len <= 0 || !f ) {
		return;
	}
	if ( len >= (int)sizeof( text ) ) {
		trap->FS_Close( f );
		trap->Print( S_COLOR_YELLOW "The offsets of %s %s are too long to be read\n", lookFolder[kind], name );
		return;
	}
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';

	j.p = text;
	j.error = qfalse;
	Json_Space( &j );
	if ( *j.p != '{' ) {
		return;
	}
	j.p++;
	while ( !j.error ) {
		Json_Space( &j );
		if ( *j.p == '}' || !*j.p || !Json_String( &j, key, sizeof( key ) ) ) {
			break;
		}
		Json_Space( &j );
		if ( *j.p != ':' ) {
			break;
		}
		j.p++;
		if ( !Q_stricmp( key, model ) ) {
			lookOffsets_t	forModel, forSkin;
			int				skinMatch = 0;

			memset( &forModel, 0, sizeof( forModel ) );
			memset( &forSkin, 0, sizeof( forSkin ) );
			Json_Offsets( &j, &forModel, skin, &forSkin, &skinMatch );
			if ( j.error ) {
				break;
			}
			if ( skinMatch ) {
				VectorCopy( forSkin.offset, out );
			} else if ( forModel.fallback && forModel.have == 7 ) {
				VectorCopy( forModel.offset, out );
			}
			return;
		}
		Json_Skip( &j );
		Json_Space( &j );
		if ( *j.p == ',' ) {
			j.p++;
		}
	}
	if ( j.error ) {
		trap->Print( S_COLOR_YELLOW "The offsets of %s %s are no JSON that can be read\n", lookFolder[kind], name );
	}
}

/*
===============================================================================

Drawing

===============================================================================
*/

// what this player wears here: his own, or with v_showCosmetics 2 the own one; a seasonal hat for one without
static const char *Look_Worn( int client, lookKind_t kind ) {
	const int	source = ( v_showCosmetics.integer == 2 || ( cg.snap && client == cg.snap->ps.clientNum ) ) ? -1 : client;
	static char	own[LOOK_NAME];
	const char	*name;

	if ( source < 0 ) {
		Look_Clean( kind == LOOK_HAT ? v_hat.string : v_cape.string, own, sizeof( own ) );
		name = own;
	} else {
		name = look.clients[source].worn[kind];
	}
	if ( !name[0] && kind == LOOK_HAT && v_seasonalCosmetics.integer ) {
		qtime_t now;

		trap->RealTime( &now );
		if ( ( now.tm_mon == 10 && now.tm_mday > 21 ) || now.tm_mon == 11 || ( now.tm_mon == 0 && now.tm_mday < 8 ) ) {
			name = "santahat";
		} else if ( now.tm_mon == 9 && now.tm_mday == 31 ) {
			name = "pumpkin";
		}
	}
	return name;
}

/*
=================
CG_LookDraw

From CG_Player, with the refEntity of the body
=================
*/
void CG_LookDraw( centity_t *cent, clientInfo_t *ci, const refEntity_t *body ) {
	const int	client = cent->currentState.number;
	int			kind;

	if ( !v_showCosmetics.integer || client < 0 || client >= MAX_CLIENTS || !cent->ghoul2 || !cg.snap ) {
		return;
	}
	if ( cent->currentState.eFlags & EF_DEAD ) {
		return;
	}
	// not on oneself in first person, nor on whom one follows that way
	if ( !cg.renderingThirdPerson && client == cg.snap->ps.clientNum ) {
		return;
	}
	if ( CG_IsMindTricked( cent->currentState.trickedentindex, cent->currentState.trickedentindex2,
		cent->currentState.trickedentindex3, cent->currentState.trickedentindex4, cg.snap->ps.clientNum ) ) {
		return;
	}
	if ( cent->currentState.powerups & ( 1 << PW_CLOAKED ) ) {
		return;
	}

	for ( kind = 0; kind < LOOK_KINDS; kind++ ) {
		lookItem_t		*item = Look_Find( (lookKind_t)kind, Look_Worn( client, (lookKind_t)kind ) );
		lookClient_t	*lc = &look.clients[client];
		char			key[sizeof( lc->offsetKey[0] )];
		mdxaBone_t		matrix;
		vec3_t			angles, origin;
		refEntity_t		re;
		int				bolt;

		if ( !item || !Look_Model( (lookKind_t)kind, item ) ) {
			continue;
		}
		Com_sprintf( key, sizeof( key ), "%s %s %s", item->name, ci->modelName, ci->skinName );
		if ( strcmp( key, lc->offsetKey[kind] ) ) {
			Q_strncpyz( lc->offsetKey[kind], key, sizeof( lc->offsetKey[kind] ) );
			Look_LoadOffsets( (lookKind_t)kind, item->name, ci->modelName, ci->skinName, lc->offset[kind] );
		}

		bolt = trap->G2API_AddBolt( cent->ghoul2, 0, lookBolt[kind] );
		if ( bolt == -1 ) {
			continue;
		}
		memset( &re, 0, sizeof( re ) );
		// the angles of the body as CG_Player turns it, without its pitch (TaystJK)
		VectorCopy( cent->lerpAngles, angles );
		angles[PITCH] = 0;
		angles[YAW] = cent->turAngles[YAW];
		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &matrix, angles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale );
		BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, origin );
		BG_GiveMeVectorFromMatrix( &matrix, POSITIVE_X, re.axis[0] );
		BG_GiveMeVectorFromMatrix( &matrix, POSITIVE_Y, re.axis[1] );
		BG_GiveMeVectorFromMatrix( &matrix, POSITIVE_Z, re.axis[2] );

		VectorMA( origin, -2.0f + lc->offset[kind][2], re.axis[2], origin );
		VectorMA( origin, lc->offset[kind][0], re.axis[0], origin );
		VectorMA( origin, lc->offset[kind][1], re.axis[1], origin );

		re.hModel = Look_Model( (lookKind_t)kind, item );
		VectorCopy( origin, re.origin );
		VectorCopy( origin, re.lightingOrigin );
		re.renderfx = body->renderfx;
		re.customShader = body->customShader;
		re.shaderRGBA[0] = body->shaderRGBA[0];
		re.shaderRGBA[1] = body->shaderRGBA[1];
		re.shaderRGBA[2] = body->shaderRGBA[2];
		re.shaderRGBA[3] = body->shaderRGBA[3];
		trap->R_AddRefEntityToScene( &re );
	}
}

/*
=================
CG_Cosmetics_f

v_cosmetics: the hats and capes there are, and what is worn
v_cosmetics hats <name> / v_cosmetics capes <name>: wear one, "none" takes it off
v_cosmetics clear: neither
=================
*/
void CG_Cosmetics_f( void ) {
	const char	*what = CG_Argv( 1 );
	int			kind, i;

	if ( !look.listed ) {
		Look_List();
	}
	if ( !Q_stricmp( what, "clear" ) ) {
		trap->Cvar_Set( "v_hat", "" );
		trap->Cvar_Set( "v_cape", "" );
		return;
	}
	for ( kind = 0; kind < LOOK_KINDS; kind++ ) {
		if ( what[0] && !Q_stricmpn( what, lookFolder[kind], 3 ) ) {
			const char *name = CG_Argv( 2 );

			if ( name[0] && Q_stricmp( name, "none" ) && !Look_Find( (lookKind_t)kind, name ) ) {
				trap->Print( "There is no %s called %s here (v_cosmetics lists them)\n", kind == LOOK_HAT ? "hat" : "cape", name );
				return;
			}
			if ( name[0] ) {
				trap->Cvar_Set( kind == LOOK_HAT ? "v_hat" : "v_cape", Q_stricmp( name, "none" ) ? name : "" );
				return;
			}
		}
	}
	for ( kind = 0; kind < LOOK_KINDS; kind++ ) {
		if ( what[0] && Q_stricmpn( what, lookFolder[kind], 3 ) ) {
			continue;
		}
		trap->Print( "%s (%i):", kind == LOOK_HAT ? "Hats" : "Capes", look.numItems[kind] );
		for ( i = 0; i < look.numItems[kind]; i++ ) {
			trap->Print( " %s", look.items[kind][i].name );
		}
		trap->Print( "\n" );
	}
	trap->Print( "You wear: hat %s, cape %s. v_cosmetics hats <name>, v_cosmetics capes <name>, v_cosmetics clear\n",
		v_hat.string[0] ? v_hat.string : "none", v_cape.string[0] ? v_cape.string : "none" );

	// what the others say they wear, and whether this machine has it
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const lookClient_t *lc = &look.clients[i];

		if ( ( !cg.snap || i != cg.snap->ps.clientNum ) && ( lc->worn[LOOK_HAT][0] || lc->worn[LOOK_CAPE][0] ) ) {
			trap->Print( "%s" S_COLOR_WHITE ": hat %s%s, cape %s%s, %s\n", cgs.clientinfo[i].name,
				lc->worn[LOOK_HAT][0] ? lc->worn[LOOK_HAT] : "none",
				lc->worn[LOOK_HAT][0] && !Look_Find( LOOK_HAT, lc->worn[LOOK_HAT] ) ? " (not here)" : "",
				lc->worn[LOOK_CAPE][0] ? lc->worn[LOOK_CAPE] : "none",
				lc->worn[LOOK_CAPE][0] && !Look_Find( LOOK_CAPE, lc->worn[LOOK_CAPE] ) ? " (not here)" : "",
				lc->masterTime ? "through the masterserver" : "through the server" );
		}
	}
}
