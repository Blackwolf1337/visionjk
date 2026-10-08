/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

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

// wa_entities.c -- the entity editor of the web admin panel: the keys every entity was spawned with,
// the list of them, placing, changing and firing one, what the game would not survive, the export, and the
// placed entities that come back after a reload of the map.

#include "game/webadmin/wa_local.h"
#include "classdoc_txt.h"

// key/value pairs each entity was spawned with, "key\0value\0key\0value\0"
static char		*entityVars[MAX_GENTITIES];
static int		entityVarsLen[MAX_GENTITIES];
static int		lastSpawnedEntity = -1;

// entities that were spawned through the panel: WA_ReloadMap() puts these back after the map has started again
static qboolean	entityPlaced[MAX_GENTITIES];
// what WA_ReloadMap() left for this start of the map, the entities one after the other. NULL: nothing.
static char		*reloadText;

/*
==============================================================================

Entity editor

==============================================================================
*/

void WebAdmin_EntitySpawned( int entityNum ) {
	int		i, len = 0;
	char	*p;

	if ( entityNum < 0 || entityNum >= MAX_GENTITIES )
		return;

	free( entityVars[entityNum] );
	entityPlaced[entityNum] = qfalse;
	for ( i = 0; i < level.numSpawnVars; i++ )
		len += (int)strlen( level.spawnVars[i][0] ) + (int)strlen( level.spawnVars[i][1] ) + 2;

	entityVars[entityNum] = p = (char *)malloc( len + 1 );
	entityVarsLen[entityNum] = len;
	for ( i = 0; i < level.numSpawnVars; i++ ) {
		const int k = (int)strlen( level.spawnVars[i][0] ) + 1, v = (int)strlen( level.spawnVars[i][1] ) + 1;
		memcpy( p, level.spawnVars[i][0], k );
		memcpy( p + k, level.spawnVars[i][1], v );
		p += k + v;
	}
	lastSpawnedEntity = entityNum;
}

void WebAdmin_EntityFreed( int entityNum ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES )
		return;
	free( entityVars[entityNum] );
	entityVars[entityNum] = NULL;
	entityVarsLen[entityNum] = 0;
	entityPlaced[entityNum] = qfalse;
}

// entities the editor shows: no players, temp entities or saber helpers
static qboolean WA_ListedEntity( const gentity_t *ent ) {
	return (qboolean)( ent->inuse && ent->s.number >= MAX_CLIENTS && ent->s.number < ENTITYNUM_WORLD && ent->classname &&
		ent->classname[0] && ent->s.eType < ET_EVENTS && !ent->isSaberEntity && Q_stricmp( ent->classname, "tempEntity" ) &&
		Q_stricmp( ent->classname, "bodyque" ) );
}

static void WA_EntityPosition( const gentity_t *ent, vec3_t out ) {
	if ( ent->r.bmodel ) {
		VectorAdd( ent->r.absmin, ent->r.absmax, out );
		VectorScale( out, 0.5f, out );
	} else {
		VectorCopy( ent->r.currentOrigin, out );
	}
}


// entities the game can still spawn: the slots it never used and the ones that are free for long enough
int WA_FreeEntities( void ) {
	int i, count = ENTITYNUM_MAX_NORMAL - level.num_entities;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];

		// G_Spawn() leaves a slot alone for a second after it was freed
		if ( !ent->inuse && !( ent->freetime > level.startTime + 2000 && level.time - ent->freetime < 1000 ) )
			count++;
	}
	return count;
}

// how many names of a kind (models, sounds, effects) are taken. The first place of each kind is never used.
static int WA_ConfigstringsUsed( int start, int max ) {
	char	name[MAX_STRING_CHARS];
	int		i, count = 0;

	for ( i = 1; i < max; i++ ) {
		trap->GetConfigstring( start + i, name, sizeof( name ) );
		if ( name[0] )
			count++;
	}
	return count;
}

// qtrue: the name has its place among the config strings already, or there is one left for it. The compare is
// the one of G_FindConfigstringIndex(), which ends the server when it finds no place.
qboolean WA_ConfigstringRoom( int start, int max, const char *name ) {
	char	have[MAX_STRING_CHARS];
	int		i;

	for ( i = 1; i < max; i++ ) {
		trap->GetConfigstring( start + i, have, sizeof( have ) );
		if ( !have[0] || !strcmp( have, name ) )
			return qtrue;
	}
	return qfalse;
}

// the value of a key of the entity that is about to be spawned, NULL without it
static const char *WA_SpawnVar( const char *key ) {
	int i;

	for ( i = 0; i < level.numSpawnVars; i++ ) {
		if ( !Q_stricmp( key, level.spawnVars[i][0] ) )
			return level.spawnVars[i][1];
	}
	return NULL;
}

/*
=================
WA_SpawnRefused

Not in JKAdmin. The spawn code of the game ends the server when an entity is not what it has to be: an error of
the game is fatal on a dedicated server. So does running out of entities, and out of places for the names of
models, sounds and effects. What can be told from the keys is looked at here before the spawn code runs.

qtrue: it is not spawned, err says why. The keys are in level.spawnVars.
=================
*/
static qboolean WA_SpawnRefused( char *err, int errSize ) {
	// trap->Error in the spawn function of the class without the key. unless: the other key that will do
	static const struct { const char *classname, *key, *unless; } needs[] = {
		{ "target_speaker", "noise", "soundSet" },
		{ "target_play_music", "music", NULL },
		{ "target_level_change", "mapname", NULL },
		{ "trigger_lightningstrike", "lightningfx", NULL },
		{ "misc_siege_item", "model", NULL },
		{ "misc_model_breakable", "model", NULL },
		{ "trigger_shipboundary", "target", NULL },
		{ "trigger_shipboundary", "traveltime", NULL },
		{ "trigger_hyperspace", "target", NULL },
		{ "trigger_hyperspace", "target2", NULL },
	};
	// keys whose value gets a place among the config strings
	static const struct { const char *key; int start, max; const char *what; } names[] = {
		{ "model", CS_MODELS, MAX_MODELS, "models" },
		{ "model2", CS_MODELS, MAX_MODELS, "models" },
		{ "noise", CS_SOUNDS, MAX_SOUNDS, "sounds" },
		{ "fxFile", CS_EFFECTS, MAX_FX, "effects" },
		{ "lightningfx", CS_EFFECTS, MAX_FX, "effects" },
	};
	const char	*classname = WA_SpawnVar( "classname" ), *value;
	const int	room = WA_FreeEntities();
	size_t		i;

	if ( room < WA_ENTITY_RESERVE ) {
		Com_sprintf( err, errSize, "the map is full: %d entities are left, the game needs %d for itself", room, WA_ENTITY_RESERVE );
		return qtrue;
	}

	for ( i = 0; i < ARRAY_LEN( needs ); i++ ) {
		if ( Q_stricmp( classname, needs[i].classname ) )
			continue;
		value = WA_SpawnVar( needs[i].key );
		if ( !VALIDSTRING( value ) ) {
			value = needs[i].unless ? WA_SpawnVar( needs[i].unless ) : NULL;
			if ( VALIDSTRING( value ) )
				continue;
			Com_sprintf( err, errSize, "a %s needs the key %s", classname, needs[i].key );
			return qtrue;
		}
		// these two look for what they name while they spawn
		if ( !Q_stricmpn( needs[i].key, "target", 6 ) && !G_Find( NULL, FOFS( targetname ), value ) ) {
			Com_sprintf( err, errSize, "a %s needs an entity with the targetname %s, there is none", classname, value );
			return qtrue;
		}
	}

	// the classes that are made of brushes of the map take them from the model key
	if ( ( !Q_stricmpn( classname, "trigger_", 8 ) && Q_stricmp( classname, "trigger_always" ) )
		|| ( !Q_stricmpn( classname, "func_", 5 ) && Q_stricmp( classname, "func_timer" ) && Q_stricmp( classname, "func_group" ) ) ) {
		const int count = WA_MapBrushModels();

		value = WA_SpawnVar( "model" );
		if ( !value || value[0] != '*' || atoi( value + 1 ) < 1 ) {
			Com_sprintf( err, errSize, "a %s is made of brushes of the map: it needs a model key like *1", classname );
			return qtrue;
		}
		if ( count >= 0 && atoi( value + 1 ) >= count ) {
			Com_sprintf( err, errSize, "the map has no brush model %s, the last one is *%d", value, count - 1 );
			return qtrue;
		}
	}

	for ( i = 0; i < ARRAY_LEN( names ); i++ ) {
		value = WA_SpawnVar( names[i].key );
		if ( VALIDSTRING( value ) && value[0] != '*' && !WA_ConfigstringRoom( names[i].start, names[i].max, value ) ) {
			Com_sprintf( err, errSize, "the map has no room for another one of its %s (%d at most)", names[i].what, names[i].max - 1 );
			return qtrue;
		}
	}
	return qfalse;
}

// What fired what, the last of it: the editor lights up the line from one entity to the other. G_UseTargets2
// says it for every target an entity fires, the panel for what is fired through it (from the world then).
#define WA_USED_KEEP	64
#define WA_USED_MS		10000		// older than that is not worth telling any more

typedef struct waUsed_s {
	int		time;		// level.time
	int		by, ent;
} waUsed_t;

static waUsed_t	usedRing[WA_USED_KEEP];
static int		usedSeq;

void WebAdmin_Used( const gentity_t *by, const gentity_t *ent ) {
	waUsed_t *u;

	if ( !WA_Running() || !by || !ent )
		return;
	u = &usedRing[usedSeq++ % WA_USED_KEEP];
	u->time = level.time;
	u->by = by->s.number;
	u->ent = ent->s.number;
}

void WA_ApiEntities( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	qboolean	first = qtrue;
	int			i;

	// how much of what the engine has a limit for is in use, and how many brush models the map has
	WA_BufPrintf( &b, "{\"budget\":{\"entities\":[%d,%d],\"models\":[%d,%d],\"sounds\":[%d,%d],\"effects\":[%d,%d]},\"brushModels\":%d,",
		ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_FreeEntities(), ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_ENTITY_RESERVE,
		WA_ConfigstringsUsed( CS_MODELS, MAX_MODELS ), MAX_MODELS - 1, WA_ConfigstringsUsed( CS_SOUNDS, MAX_SOUNDS ), MAX_SOUNDS - 1,
		WA_ConfigstringsUsed( CS_EFFECTS, MAX_FX ), MAX_FX - 1, WA_MapBrushModels() );
	WA_BufAppend( &b, "\"entities\":[", 12 );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		vec3_t		pos;

		if ( !WA_ListedEntity( ent ) )
			continue;

		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;

		WA_EntityPosition( ent, pos );
		WA_BufPrintf( &b, "{\"id\":%d,\"classname\":", i );
		WA_BufJsonString( &b, ent->classname );
		WA_BufPrintf( &b, ",\"type\":%d,\"bmodel\":%s,\"linked\":%s,\"placed\":%s,\"pos\":", ent->s.eType, ent->r.bmodel ? "true" : "false",
			ent->r.linked ? "true" : "false", WA_BOOL( entityPlaced[i] ) );
		WA_AppendVec( &b, pos );
		// one that is made of brushes: how far they were moved and turned from where the map file has them
		if ( ent->r.bmodel ) {
			WA_BufAppend( &b, ",\"moved\":", 9 );
			WA_AppendVec( &b, ent->r.currentOrigin );
			WA_BufAppend( &b, ",\"turned\":", 10 );
			WA_AppendVec( &b, ent->r.currentAngles );
		}

		if ( entityVars[i] ) {
			const char *p = entityVars[i], *end = entityVars[i] + entityVarsLen[i];
			qboolean firstKey = qtrue;

			WA_BufAppend( &b, ",\"keys\":[", 9 );
			while ( p < end ) {
				const char *value = p + strlen( p ) + 1;
				if ( !firstKey )
					WA_BufAppend( &b, ",", 1 );
				firstKey = qfalse;
				WA_BufAppend( &b, "[", 1 );
				WA_BufJsonString( &b, p );
				WA_BufAppend( &b, ",", 1 );
				WA_BufJsonString( &b, value );
				WA_BufAppend( &b, "]", 1 );
				p = value + strlen( value ) + 1;
			}
			WA_BufAppend( &b, "]", 1 );
		}
		WA_BufAppend( &b, "}", 1 );
	}

	// what fired what in the last seconds: the number of the event, how long ago, who, whom
	WA_BufAppend( &b, "],\"used\":[", 10 );
	first = qtrue;
	for ( i = usedSeq > WA_USED_KEEP ? usedSeq - WA_USED_KEEP : 0; i < usedSeq; i++ ) {
		const waUsed_t *u = &usedRing[i % WA_USED_KEEP];

		if ( level.time - u->time > WA_USED_MS )
			continue;
		WA_BufPrintf( &b, "%s[%d,%d,%d,%d]", first ? "" : ",", i, level.time - u->time, u->by, u->ent );
		first = qfalse;
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// everything that can be spawned: the spawn table and all items
void WA_ApiClasses( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	int			i, n = G_NumSpawnClassnames();
	qboolean	first;

	WA_BufAppend( &b, "{\"classes\":[", 12 );
	for ( i = 0; i < n; i++ ) {
		if ( i )
			WA_BufAppend( &b, ",", 1 );
		WA_BufJsonString( &b, G_SpawnClassname( i ) );
	}
	for ( i = 1; i < bg_numItems; i++ ) {
		if ( !bg_itemlist[i].classname || !bg_itemlist[i].classname[0] )
			continue;
		WA_BufAppend( &b, ",", 1 );
		WA_BufJsonString( &b, bg_itemlist[i].classname );
	}

	// models and icons of the items, for the previews
	WA_BufAppend( &b, "],\"items\":[", 11 );
	for ( i = 1, first = qtrue; i < bg_numItems; i++ ) {
		const gitem_t *item = &bg_itemlist[i];

		if ( !item->classname || !item->classname[0] )
			continue;
		if ( !first )
			WA_BufAppend( &b, ",", 1 );
		first = qfalse;
		WA_BufAppend( &b, "{\"classname\":", 13 );
		WA_BufJsonString( &b, item->classname );
		WA_BufAppend( &b, ",\"model\":", 9 );
		WA_BufJsonString( &b, item->world_model[0] ? item->world_model[0] : "" );
		WA_BufAppend( &b, ",\"view\":", 8 );
		WA_BufJsonString( &b, item->view_model ? item->view_model : "" );
		WA_BufAppend( &b, ",\"icon\":", 8 );
		WA_BufJsonString( &b, item->icon ? item->icon : "" );
		WA_BufAppend( &b, ",\"description\":", 15 );
		WA_BufJsonString( &b, item->description ? item->description : "" );
		WA_BufPrintf( &b, ",\"type\":%d,\"tag\":%d,\"quantity\":%d}", item->giType, item->giTag, item->quantity );
	}
	WA_BufAppend( &b, "]}", 2 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// The current entities in .ent format, for keeping an edited layout.
// placed=1: only what was placed through the panel, without the worldspawn. That is what an import takes on
// another map or another server.
void WA_ApiExport( waConnection_t *c, const char *query ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH], headers[256], flag[4];
	qboolean	placedOnly;
	int			i;

	WA_FormValue( query, "placed", flag, sizeof( flag ) );
	placedOnly = (qboolean)( flag[0] == '1' );

	for ( i = MAX_CLIENTS; i <= ENTITYNUM_WORLD; i++ ) {
		const int num = i == MAX_CLIENTS ? ENTITYNUM_WORLD : ( i == ENTITYNUM_WORLD ? MAX_CLIENTS : i );	// worldspawn first
		const char *p, *end;

		if ( !entityVars[num] || ( num != ENTITYNUM_WORLD && !g_entities[num].inuse ) )
			continue;
		if ( placedOnly && ( num == ENTITYNUM_WORLD || !entityPlaced[num] ) )
			continue;

		WA_BufAppend( &b, "{\n", 2 );
		for ( p = entityVars[num], end = p + entityVarsLen[num]; p < end; ) {
			const char *value = p + strlen( p ) + 1;
			WA_BufPrintf( &b, "\"%s\" \"%s\"\n", p, value );
			p = value + strlen( value ) + 1;
		}
		WA_BufAppend( &b, "}\n", 2 );
	}

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	Com_sprintf( headers, sizeof( headers ), "Content-Disposition: attachment; filename=\"%s%s.ent\"\r\nCache-Control: no-store\r\n", COM_SkipPath( mapname ),
		placedOnly ? "_placed" : "" );
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", headers, b.data ? b.data : "", b.len );
	WA_BufFree( &b );
}

// Spawns an entity from "key=value" lines through the normal spawn code.
// Returns the entity number, or -1 with a reason in err.
static int WA_SpawnFromText( const char *text, char *err, int errSize ) {
	char		line[MAX_SPAWN_VARS_CHARS];
	const char	*p = text;
	qboolean	hasClass = qfalse, wasSpawning;

	level.numSpawnVars = 0;
	level.numSpawnVarChars = 0;

	while ( *p ) {
		const char	*end = strchr( p, '\n' );
		const int	len = end ? (int)( end - p ) : (int)strlen( p );
		char		*key, *value, *eq;

		if ( len >= (int)sizeof( line ) ) {
			Q_strncpyz( err, "a line is too long", errSize );
			return -1;
		}
		memcpy( line, p, len );
		line[len] = '\0';
		p = end ? end + 1 : p + len;

		Q_strstrip( line, "\r\"", NULL );
		eq = strchr( line, '=' );
		if ( !eq )
			continue;
		*eq = '\0';
		key = line;
		value = eq + 1;
		while ( *key == ' ' || *key == '\t' )
			key++;
		while ( eq > key && ( eq[-1] == ' ' || eq[-1] == '\t' ) )
			*--eq = '\0';
		if ( !key[0] )
			continue;

		if ( level.numSpawnVars >= MAX_SPAWN_VARS ||
			 level.numSpawnVarChars + (int)strlen( key ) + (int)strlen( value ) + 2 >= MAX_SPAWN_VARS_CHARS ) {
			Q_strncpyz( err, "too many keys", errSize );
			return -1;
		}
		if ( !Q_stricmp( key, "classname" ) ) {
			if ( !value[0] || !Q_stricmp( value, "worldspawn" ) ) {
				Q_strncpyz( err, "this classname can not be spawned", errSize );
				return -1;
			}
			hasClass = qtrue;
		}

		level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken( key );
		level.spawnVars[level.numSpawnVars][1] = G_AddSpawnVarToken( value );
		level.numSpawnVars++;
	}

	if ( !hasClass ) {
		Q_strncpyz( err, "no classname", errSize );
		return -1;
	}
	if ( WA_SpawnRefused( err, errSize ) )
		return -1;

	wasSpawning = level.spawning;
	level.spawning = qtrue;
	lastSpawnedEntity = -1;
	G_SpawnGEntityFromSpawnVars( qfalse );
	level.spawning = wasSpawning;

	// new item types have to reach the clients
	SaveRegisteredItems();

	if ( lastSpawnedEntity < 0 || !g_entities[lastSpawnedEntity].inuse ) {
		Q_strncpyz( err, "the game did not spawn it (unknown classname, or not for this gametype)", errSize );
		return -1;
	}
	return lastSpawnedEntity;
}

static void WA_EntityResult( waConnection_t *c, qboolean ok, const char *msg, int id ) {
	waBuffer_t b = { 0 };

	WA_BufPrintf( &b, "{\"ok\":%s,\"id\":%d,\"message\":", ok ? "true" : "false", id );
	WA_BufJsonString( &b, msg );
	WA_BufAppend( &b, "}", 1 );
	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

// Movers with the same team key move together (G_FindTeams): they are a chain, the first one leads, the game
// runs only that one and it moves the others, who name it as their master. The game never frees a mover, so
// nothing takes one out of its team either. Left in, the team goes on moving and linking whatever entity gets
// its number, and without its first part the others never move again.
// Returns the part that leads the team from now on when ent led it, NULL otherwise.
static gentity_t *WA_LeaveTeam( gentity_t *ent ) {
	gentity_t	*master = ent->teammaster, *next = NULL, *part;

	if ( !master )
		return NULL;

	if ( master != ent ) {
		for ( part = master; part; part = part->teamchain ) {
			if ( part->teamchain == ent ) {
				part->teamchain = ent->teamchain;
				break;
			}
		}
	} else if ( ( next = ent->teamchain ) != NULL ) {
		next->flags &= ~FL_TEAMSLAVE;
		for ( part = next; part; part = part->teamchain )
			part->teammaster = next;
		// what fires the team fires its first part: G_FindTeams gave that one the name
		if ( !next->targetname )
			next->targetname = ent->targetname;
		// what the team was about to do (come back, start to move) it still does
		if ( !next->think ) {
			next->think = ent->think;
			next->nextthink = ent->nextthink;
		}
	}
	ent->teammaster = ent->teamchain = NULL;
	return next;
}

// An edit spawns a new entity and frees the old one. The new one takes the place of the old one in its team,
// unless the edit gave it another team key.
static void WA_TakeTeamPlace( gentity_t *old, gentity_t *ent ) {
	gentity_t	*master = old->teammaster, *part;

	if ( !master || !old->team || !ent->team || strcmp( old->team, ent->team ) )
		return;

	if ( master == old ) {
		ent->teamchain = old->teamchain;
		ent->flags &= ~FL_TEAMSLAVE;
		for ( part = ent; part; part = part->teamchain )
			part->teammaster = ent;
		if ( !ent->targetname )
			ent->targetname = old->targetname;
	} else {
		for ( part = master; part && part->teamchain != old; part = part->teamchain )
			;
		if ( !part )
			return;
		part->teamchain = ent;
		ent->teamchain = old->teamchain;
		ent->teammaster = master;
		ent->flags |= FL_TEAMSLAVE;
		// as G_FindTeams: targets only point at the first part
		if ( ent->targetname ) {
			master->targetname = ent->targetname;
			ent->targetname = NULL;
		}
	}
	old->teammaster = old->teamchain = NULL;
}

// A func_plat and a func_door make a trigger of their own when they spawn (SpawnPlatTrigger,
// Think_SpawnNewDoorTrigger) and are its parent. The game never frees a mover, so nothing frees that trigger
// either: left behind it would use whatever entity gets the number of the mover next. The trigger of a team
// goes to the part that leads the team now.
static void WA_FreeEntity( gentity_t *ent ) {
	gentity_t	*heir = WA_LeaveTeam( ent );
	int			i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *other = &g_entities[i];

		if ( !other->inuse || other == ent || other->parent != ent || entityVars[i] || other->r.contents != CONTENTS_TRIGGER )
			continue;
		if ( heir )
			other->parent = heir;
		else
			G_FreeEntity( other );
	}
	G_FreeEntity( ent );
}

void WA_ApiEntity( waConnection_t *c, const char *body ) {
	char		op[16], idStr[16], ofMap[4], err[128];
	static char	kv[WA_MAX_REQUEST];
	int			id = -1, newId;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_FormValue( body, "kv", kv, sizeof( kv ) );
	if ( WA_FormValue( body, "id", idStr, sizeof( idStr ) ) )
		id = atoi( idStr );
	WA_LogUse( c, va( "entity %s id %i keys %.200s", op, id, kv ) );

	if ( !strcmp( op, "spawn" ) ) {
		newId = WA_SpawnFromText( kv, err, sizeof( err ) );
		// map=1: an entity of the map that was deleted and is taken back (undo). It is not kept over a start of
		// the map as a placed one is, the map brings it itself.
		if ( newId >= 0 )
			entityPlaced[newId] = (qboolean)!( WA_FormValue( body, "map", ofMap, sizeof( ofMap ) ) && ofMap[0] == '1' );
		WA_EntityResult( c, newId >= 0, newId >= 0 ? "spawned" : err, newId );
		return;
	}

	if ( !strcmp( op, "clear" ) ) {
		// the keys are "classname=<class>": every entity of that class that was placed through the panel goes,
		// the ones of the map stay
		const char	*classname = Q_stricmpn( kv, "classname=", 10 ) ? "" : kv + 10;
		int			i, count = 0;

		for ( i = MAX_CLIENTS; classname[0] && i < level.num_entities; i++ ) {
			gentity_t *ent = &g_entities[i];

			if ( entityPlaced[i] && WA_ListedEntity( ent ) && !Q_stricmp( ent->classname, classname ) ) {
				WA_FreeEntity( ent );
				count++;
			}
		}
		WA_EntityResult( c, qtrue, va( "%i deleted", count ), -1 );
		return;
	}

	if ( id < 0 || id >= MAX_GENTITIES || !WA_ListedEntity( &g_entities[id] ) ) {
		WA_EntityResult( c, qfalse, "no such entity", id );
		return;
	}

	if ( !strcmp( op, "delete" ) ) {
		WA_FreeEntity( &g_entities[id] );
		WA_EntityResult( c, qtrue, "deleted", id );
	} else if ( !strcmp( op, "use" ) ) {
		// Fires the entity as a trigger_always would that has it as its target, with the world in the place of
		// the trigger: a door opens, a trigger fires its targets, a target does what it is there for. What only
		// works on the player who fired it (a teleporter, a give) does nothing then, the use functions of the game
		// all ask for a client before they reach into one. An NPC is left alone: a vehicle takes whoever uses it
		// on board without asking. A misc_weapon_shooter has a client too, one the game lends it for the weapon
		// code, and is fired like everything else.
		gentity_t *ent = &g_entities[id];

		if ( ent->NPC || ent->s.eType == ET_NPC ) {
			WA_EntityResult( c, qfalse, "an NPC is not fired from here", id );
		} else if ( !ent->use ) {
			WA_EntityResult( c, qfalse, "this entity does nothing when it is fired", id );
		} else if ( ent->flags & FL_INACTIVE ) {
			WA_EntityResult( c, qfalse, "this entity is inactive, a target_activate has to wake it first", id );
		} else {
			WebAdmin_Used( &g_entities[ENTITYNUM_WORLD], ent );
			GlobalUse( ent, &g_entities[ENTITYNUM_WORLD], &g_entities[ENTITYNUM_WORLD] );
			// it may be gone now (something that breaks, a trigger that fires once)
			WA_EntityResult( c, qtrue, "fired", ent->inuse ? id : -1 );
		}
	} else if ( !strcmp( op, "update" ) ) {
		if ( !entityVars[id] ) {
			WA_EntityResult( c, qfalse, "this entity was made by the game code, it has no keys to edit", id );
			return;
		}
		// spawn the new one first, so a bad edit keeps the old entity
		newId = WA_SpawnFromText( kv, err, sizeof( err ) );
		if ( newId < 0 ) {
			WA_EntityResult( c, qfalse, err, id );
			return;
		}
		// an entity of the map that was edited is still one of the map: the next start of the map has it as it was
		entityPlaced[newId] = entityPlaced[id];
		WA_TakeTeamPlace( &g_entities[id], &g_entities[newId] );
		WA_FreeEntity( &g_entities[id] );
		WA_EntityResult( c, qtrue, "updated", newId );
	} else {
		WA_EntityResult( c, qfalse, "unknown op", id );
	}
}

/*
==============================================================================

Map reload

Not in JKAdmin. The engine looks through its folders for pk3 files whenever a map starts (FS_Restart in
SV_SpawnServer) and at no other time, so starting the running map again is what makes it find a pk3 that was
uploaded. The players stay connected.

A start of the map also removes what was spawned through the panel, and the module is loaded anew with
nothing it remembered. So the placed entities are written to a file first, with the keys they were spawned
with and the place they are at now, and the new map spawns them again and removes the file.

==============================================================================
*/

#define WA_RELOAD_FILE		"webadmin_reload.dat"
#define WA_RELOAD_DELAY		1500		// msec after the start of the map

void WA_ReloadMap( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH];
	int			i, count = 0;

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	Q_strstrip( mapname, "\n\r\";", NULL );
	if ( !mapname[0] ) {
		WA_ActionResult( c, qfalse, "no map is running" );
		return;
	}

	// the first line names the map, then a block of key=value lines for each entity
	WA_BufPrintf( &b, "map=%s\n", mapname );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		const gentity_t	*ent = &g_entities[i];
		const char		*key, *end;

		if ( !entityPlaced[i] || !entityVars[i] || !WA_ListedEntity( ent ) )
			continue;
		WA_BufAppend( &b, "{\n", 2 );
		for ( key = entityVars[i], end = key + entityVarsLen[i]; key < end; ) {
			const char *value = key + strlen( key ) + 1;

			if ( !Q_stricmp( key, "origin" ) && !ent->r.bmodel ) {
				// where it is now, not where it was put: a ball rolls
				WA_BufPrintf( &b, "origin=%.1f %.1f %.1f\n", ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2] );
			} else {
				WA_BufPrintf( &b, "%s=%s\n", key, value );
			}
			key = value + strlen( value ) + 1;
		}
		WA_BufAppend( &b, "}\n", 2 );
		count++;
	}
	if ( !WA_WriteGameFile( WA_RELOAD_FILE, b.data, b.len ) ) {
		WA_BufFree( &b );
		WA_ActionResult( c, qfalse, "could not write " WA_RELOAD_FILE ", the map is not started again" );
		return;
	}
	WA_BufFree( &b );

	// devmap keeps the cheats on if they are
	trap->SendConsoleCommand( EXEC_APPEND, va( "%s \"%s\"\n", trap->Cvar_VariableIntegerValue( "sv_cheats" ) ? "devmap" : "map", mapname ) );
	WA_ActionResult( c, qtrue, va( "starting %s again, %d placed objects come back", mapname, count ) );
}

// At the start of a map: takes what WA_ReloadMap() left. The file is for the start that follows it and for no
// other, so it is removed whatever is in it.
void WA_ReloadRead( void ) {
	char		path[MAX_OSPATH], mapname[MAX_QPATH];
	qboolean	exists, isDir;
	double		size;
	FILE		*f;
	char		*text, *body;
	int			len;

	free( reloadText );
	reloadText = NULL;
	if ( !WA_FsResolve( qfalse, WA_RELOAD_FILE, qfalse, path, sizeof( path ), &exists )
		|| !WA_FsStat( path, &isDir, &size, NULL ) || isDir )
		return;
	len = ( size < 4 * 1024 * 1024 ) ? (int)size : 0;
	f = fopen( path, "rb" );
	text = f ? (char *)malloc( len + 1 ) : NULL;
	if ( text )
		len = (int)fread( text, 1, len, f );
	if ( f )
		fclose( f );
	remove( path );
	if ( !text )
		return;
	text[len] = '\0';

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	body = strchr( text, '\n' );
	if ( !body || strncmp( text, "map=", 4 ) || (int)( body - text ) - 4 != (int)strlen( mapname )
		|| strncmp( text + 4, mapname, strlen( mapname ) ) ) {
		free( text );		// it was left for another map
		return;
	}
	memmove( text, body + 1, strlen( body + 1 ) + 1 );
	reloadText = text;
}

// spawns what WA_ReloadRead() took, when the map has run for a moment
static void WA_ReloadSpawn( void ) {
	char	*block = reloadText, err[128];
	int		count = 0, lost = 0;

	while ( ( block = strstr( block, "{\n" ) ) != NULL ) {
		char *end = strstr( block, "\n}\n" );

		if ( !end )
			break;
		end[1] = '\0';
		{
			const int id = WA_SpawnFromText( block + 2, err, sizeof( err ) );

			if ( id >= 0 ) {
				entityPlaced[id] = qtrue;
				count++;
			} else {
				lost++;
			}
		}
		block = end + 3;
	}
	free( reloadText );
	reloadText = NULL;
	trap->Print( "Web admin: %d placed objects are back after the map reload%s\n", count, lost ? va( ", %d did not spawn", lost ) : "" );
}

// every frame: what a reload left for this start of the map is spawned once the map has settled
void WA_EntitiesFrame( void ) {
	if ( reloadText && level.time - level.startTime >= WA_RELOAD_DELAY )
		WA_ReloadSpawn();
}

// the end of the map: the keys that were kept of every entity, and a reload that never got its turn
void WA_EntitiesShutdown( void ) {
	int i;

	for ( i = 0; i < MAX_GENTITIES; i++ )
		WebAdmin_EntityFreed( i );
	free( reloadText );
	reloadText = NULL;
}

// GET /api/classdoc: the entity definitions of the game code, every QUAKED comment of its source files as it
// stands there (cmake/Modules/ExtractQuaked.cmake). The editor of the page reads the classes from it: what one
// does, its keys, the names of its spawnflags, its box and its colour.
void WA_ApiClassDoc( waConnection_t *c ) {
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: max-age=3600\r\n", (const char *)classdoc_txt, (int)classdoc_txt_len );
}

