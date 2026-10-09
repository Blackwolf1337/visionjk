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
#include "classkeys_txt.h"

// key/value pairs each entity was spawned with, "key\0value\0key\0value\0"
static char		*entityVars[MAX_GENTITIES];
static int		entityVarsLen[MAX_GENTITIES];
static int		lastSpawnedEntity = -1;

// entities that were spawned through the panel: WA_ReloadMap() puts these back after the map has started again
static qboolean	entityPlaced[MAX_GENTITIES];

// The entities the map file brought, each with the keys it came with. An entity that still is one of them says
// which (entityOriginal, its number plus one, 0 for none). One that was deleted or changed through the panel is
// gone: that is what a layout has to take away again at the next start of the map (see "The layout of a map").
typedef struct waOriginal_s {
	char		*vars;		// as entityVars has them
	int			len;
	qboolean	gone;
} waOriginal_t;

static waOriginal_t	originals[MAX_GENTITIES];
static int			originalCount;
static short		entityOriginal[MAX_GENTITIES];
static qboolean		panelSpawns;				// WA_SpawnFromText() is at work: what spawns is not of the map file
static qboolean		layoutSaved, layoutDirty;	// there is a file for this map, and the map is not as the file has it

static qboolean WA_LayoutFile( char *mapname, int mapSize, char *file, int fileSize );
static void WA_LayoutText( waBuffer_t *b, const char *mapname, int *adds, int *removes );
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

	// what the game spawns of itself, at the start of the map, is an entity of the map file
	entityOriginal[entityNum] = 0;
	if ( !panelSpawns && entityNum != ENTITYNUM_WORLD && originalCount < MAX_GENTITIES ) {
		waOriginal_t *o = &originals[originalCount];

		o->vars = (char *)malloc( len + 1 );
		if ( o->vars ) {
			memcpy( o->vars, entityVars[entityNum], len );
			o->len = len;
			o->gone = qfalse;
			entityOriginal[entityNum] = (short)++originalCount;
		}
	}
}

void WebAdmin_EntityFreed( int entityNum ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES )
		return;
	free( entityVars[entityNum] );
	entityVars[entityNum] = NULL;
	entityVarsLen[entityNum] = 0;
	entityPlaced[entityNum] = qfalse;
	// freed by the game (something that broke, a trigger that fired once): that is not a change of the layout
	entityOriginal[entityNum] = 0;
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

#define WA_TRIGGER_BOX	32768		// the widest box a trigger may have that is one of its own

// The key a spawn was refused for, when giving it or changing it is what helps: the page opens the entity with
// that key to fill in instead of only saying no. NULL: it was refused for something else.
static const char	*refusedKey;
#define WA_MEMORY_RESERVE	( 256 * 1024 )	// of the pool of G_Alloc the editor leaves this much to the game

extern stringID_table_t WPTable[];

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
		{ "info_siege_radaricon", "icon", NULL },
		// no error, but the class frees itself without it, and "the game did not keep it" does not say why
		{ "fx_runner", "fxFile", NULL },
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
	// every spawn takes memory for the strings of its keys that the game gives back only with the map
	if ( G_MemoryLeft( NULL ) < WA_MEMORY_RESERVE ) {
		Q_strncpyz( err, "the memory the game has for this map is used up by all that was spawned: start the map again (Reload map keeps what was placed)", errSize );
		return qtrue;
	}
	// It loads another .bsp file into the map and spawns every entity of that one. A file that is not there ends
	// the server (also no bspmodel key at all: it asks for maps/.bsp), and so does one that brings more than
	// the map has room for. And a client loads such a file only while it loads the map (CG_RegisterGraphics):
	// one that is connected already gets an entity with a model its collision map does not have, and the
	// engine drops it for that (CM_InlineModel: bad number).
	if ( !Q_stricmp( classname, "misc_bsp" ) ) {
		Q_strncpyz( err, "a misc_bsp puts a whole other .bsp file into the map with all of its entities, the panel does not place it: it only works when it is there as the map starts. A missing file ends the server, and players who are on it already are dropped", errSize );
		return qtrue;
	}
	// it registers the item of its weapon and ends the server when there is none (a name that is no weapon, or
	// a weapon nobody can pick up)
	value = WA_SpawnVar( "weapon" );
	if ( !Q_stricmp( classname, "misc_weapon_shooter" ) && VALIDSTRING( value ) ) {
		const int		weapon = GetIDForString( WPTable, value );
		const gitem_t	*item;

		for ( item = bg_itemlist + 1; item->classname; item++ ) {
			if ( item->giType == IT_WEAPON && item->giTag == weapon )
				break;
		}
		if ( !item->classname ) {
			Com_sprintf( err, errSize, "a misc_weapon_shooter can not fire %s: its weapon has to be one that lies around as an item, like WP_BLASTER or WP_ROCKET_LAUNCHER", value );
			refusedKey = "weapon";
			return qtrue;
		}
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
			refusedKey = needs[i].key;
			return qtrue;
		}
		// these two look for what they name while they spawn
		if ( !Q_stricmpn( needs[i].key, "target", 6 ) && !G_Find( NULL, FOFS( targetname ), value ) ) {
			Com_sprintf( err, errSize, "a %s needs an entity with the targetname %s, there is none", classname, value );
			refusedKey = needs[i].key;
			return qtrue;
		}
	}

	// the classes that are made of brushes of the map take them from the model key
	if ( ( !Q_stricmpn( classname, "trigger_", 8 ) && Q_stricmp( classname, "trigger_always" ) )
		|| ( !Q_stricmpn( classname, "func_", 5 ) && Q_stricmp( classname, "func_timer" ) && Q_stricmp( classname, "func_group" ) ) ) {
		const int		count = WA_MapBrushModels();
		const char		*lo = WA_SpawnVar( "mins" ), *hi = WA_SpawnVar( "maxs" );
		// a trigger may be a box of its own instead (InitTrigger in g_trigger.c), all but the one that does not ask it
		const qboolean	boxed = (qboolean)( !Q_stricmpn( classname, "trigger_", 8 ) && Q_stricmp( classname, "trigger_asteroid_field" ) );
		vec3_t			mins, maxs;

		value = WA_SpawnVar( "model" );
		if ( ( !value || value[0] != '*' ) && boxed && ( VALIDSTRING( lo ) || VALIDSTRING( hi ) ) ) {
			// mins below maxs on every axis, or InitTrigger asks for the brushes after all
			if ( !lo || !hi || sscanf( lo, "%f %f %f", &mins[0], &mins[1], &mins[2] ) != 3 || sscanf( hi, "%f %f %f", &maxs[0], &maxs[1], &maxs[2] ) != 3
				|| maxs[0] <= mins[0] || maxs[1] <= mins[1] || maxs[2] <= mins[2] ) {
				Com_sprintf( err, errSize, "the box of a %s needs mins and maxs with three numbers each, mins below maxs", classname );
				refusedKey = "maxs";
				return qtrue;
			}
			if ( maxs[0] - mins[0] > WA_TRIGGER_BOX || maxs[1] - mins[1] > WA_TRIGGER_BOX || maxs[2] - mins[2] > WA_TRIGGER_BOX ) {
				Com_sprintf( err, errSize, "the box of a trigger may be %d units wide at most", WA_TRIGGER_BOX );
				return qtrue;
			}
		} else if ( !value || value[0] != '*' || atoi( value + 1 ) < 1 ) {
			Com_sprintf( err, errSize, boxed
				? "a %s needs a shape: a model key like *1 for brushes of the map, or mins and maxs for a box of its own"
				: "a %s is made of brushes of the map: it needs a model key like *1", classname );
			refusedKey = "model";
			return qtrue;
		} else if ( count >= 0 && atoi( value + 1 ) >= count ) {
			Com_sprintf( err, errSize, "the map has no brush model %s, the last one is *%d", value, count - 1 );
			refusedKey = "model";
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
	int			i, memoryLeft, memorySize;

	// how much of what the engine has a limit for is in use, and how many brush models the map has
	memoryLeft = G_MemoryLeft( &memorySize );
	WA_BufPrintf( &b, "{\"budget\":{\"entities\":[%d,%d],\"models\":[%d,%d],\"sounds\":[%d,%d],\"effects\":[%d,%d],\"memory\":[%d,%d]},\"brushModels\":%d,",
		ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_FreeEntities(), ENTITYNUM_MAX_NORMAL - MAX_CLIENTS - WA_ENTITY_RESERVE,
		WA_ConfigstringsUsed( CS_MODELS, MAX_MODELS ), MAX_MODELS - 1, WA_ConfigstringsUsed( CS_SOUNDS, MAX_SOUNDS ), MAX_SOUNDS - 1,
		WA_ConfigstringsUsed( CS_EFFECTS, MAX_FX ), MAX_FX - 1, memorySize - memoryLeft, memorySize - WA_MEMORY_RESERVE, WA_MapBrushModels() );
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

	// the layout of the map: is one saved, is the map as it has it, and what it would hold now
	{
		char	mapname[MAX_QPATH], file[MAX_QPATH];
		int		adds = 0, removes = 0;

		if ( WA_LayoutFile( mapname, sizeof( mapname ), file, sizeof( file ) ) )
			WA_LayoutText( NULL, mapname, &adds, &removes );
		WA_BufPrintf( &b, "],\"layout\":{\"saved\":%s,\"dirty\":%s,\"add\":%d,\"remove\":%d},\"used\":[", WA_BOOL( layoutSaved ),
			WA_BOOL( layoutDirty && ( layoutSaved || adds + removes > 0 ) ), adds, removes );
	}

	// what fired what in the last seconds: the number of the event, how long ago, who, whom
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
		// index: its place in bg_itemlist, which is what an item of /api/state names it by
		WA_BufPrintf( &b, "{\"index\":%d,\"classname\":", i );
		WA_BufJsonString( &b, item->classname );
		WA_BufAppend( &b, ",\"model\":", 9 );
		WA_BufJsonString( &b, item->world_model[0] ? item->world_model[0] : "" );
		WA_BufAppend( &b, ",\"model2\":", 10 );
		WA_BufJsonString( &b, item->world_model[1] ? item->world_model[1] : "" );
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
// An entity that came through the panel with the very keys an entity of the map file had that is gone: it is
// that one again. That is an undo of a delete, or keys that were put back to what they were.
static void WA_OriginalBack( int entityNum ) {
	int k;

	for ( k = 0; k < originalCount; k++ ) {
		if ( originals[k].gone && originals[k].len == entityVarsLen[entityNum]
			&& !memcmp( originals[k].vars, entityVars[entityNum], originals[k].len ) ) {
			originals[k].gone = qfalse;
			entityOriginal[entityNum] = (short)( k + 1 );
			return;
		}
	}
}

/*
=================
WA_SpawnVarsFromText

The keys of an entity, one "key=value" to a line, into level.spawnVars, and looked at by the spawn guard.
qfalse: they are not to be spawned, err says why. Nothing is spawned here: the page also asks this way what an
entity it has only as a draft would be refused for (op check).
=================
*/
static qboolean WA_SpawnVarsFromText( const char *text, char *err, int errSize ) {
	char		line[MAX_SPAWN_VARS_CHARS];
	const char	*p = text;
	qboolean	hasClass = qfalse;

	refusedKey = NULL;
	level.numSpawnVars = 0;
	level.numSpawnVarChars = 0;

	while ( *p ) {
		const char	*end = strchr( p, '\n' );
		const int	len = end ? (int)( end - p ) : (int)strlen( p );
		char		*key, *value, *eq;

		if ( len >= (int)sizeof( line ) ) {
			Q_strncpyz( err, "a line is too long", errSize );
			return qfalse;
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
			return qfalse;
		}
		if ( !Q_stricmp( key, "classname" ) ) {
			if ( !value[0] || !Q_stricmp( value, "worldspawn" ) ) {
				Q_strncpyz( err, "this classname can not be spawned", errSize );
				return qfalse;
			}
			hasClass = qtrue;
		}

		level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken( key );
		level.spawnVars[level.numSpawnVars][1] = G_AddSpawnVarToken( value );
		level.numSpawnVars++;
	}

	if ( !hasClass ) {
		Q_strncpyz( err, "no classname", errSize );
		return qfalse;
	}
	return (qboolean)!WA_SpawnRefused( err, errSize );
}

static int WA_SpawnFromText( const char *text, char *err, int errSize ) {
	qboolean	wasSpawning;

	if ( !WA_SpawnVarsFromText( text, err, errSize ) )
		return -1;

	wasSpawning = level.spawning;
	level.spawning = qtrue;
	lastSpawnedEntity = -1;
	panelSpawns = qtrue;
	G_SpawnGEntityFromSpawnVars( qfalse );
	panelSpawns = qfalse;
	level.spawning = wasSpawning;

	// new item types have to reach the clients
	SaveRegisteredItems();

	if ( lastSpawnedEntity < 0 || !g_entities[lastSpawnedEntity].inuse ) {
		Q_strncpyz( err, "the game did not keep it: no such class, a class that is not for this game type, or one the game frees at once (many are only for the map compiler)", errSize );
		return -1;
	}
	WA_OriginalBack( lastSpawnedEntity );
	layoutDirty = qtrue;
	// a push that is aimed at it throws to where it is now, and not to where its target was when the map began
	G_PushAimAgain( &g_entities[lastSpawnedEntity] );
	return lastSpawnedEntity;
}

static void WA_EntityResult( waConnection_t *c, qboolean ok, const char *msg, int id ) {
	waBuffer_t b = { 0 };

	WA_BufPrintf( &b, "{\"ok\":%s,\"id\":%d,\"message\":", ok ? "true" : "false", id );
	WA_BufJsonString( &b, msg );
	// what a refused spawn was refused for, once
	if ( !ok && refusedKey ) {
		WA_BufAppend( &b, ",\"key\":", 7 );
		WA_BufJsonString( &b, refusedKey );
	}
	refusedKey = NULL;
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
	// an entity of the map file that the panel takes away is gone for the layout
	if ( entityOriginal[ent->s.number] )
		originals[entityOriginal[ent->s.number] - 1].gone = qtrue;
	layoutDirty = qtrue;
	G_FreeEntity( ent );
}

void WA_ApiEntity( waConnection_t *c, const char *body ) {
	char		op[16], idStr[16], ofMap[4], err[256], expected[MAX_QPATH];
	static char	kv[WA_MAX_REQUEST];
	int			id = -1, newId, original;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_FormValue( body, "kv", kv, sizeof( kv ) );
	if ( WA_FormValue( body, "id", idStr, sizeof( idStr ) ) )
		id = atoi( idStr );

	// What a spawn of these keys would be refused for, with nothing spawned: for an entity the page holds as a
	// draft. It changes nothing, so it is not a line in the log either (the page asks at every change of a key).
	if ( !strcmp( op, "check" ) ) {
		const qboolean ok = WA_SpawnVarsFromText( kv, err, sizeof( err ) );

		WA_EntityResult( c, ok, ok ? "it can be placed" : err, -1 );
		return;
	}
	WA_LogUse( c, va( "entity %s id %i keys %.200s", op, id, kv ) );

	if ( !strcmp( op, "spawn" ) ) {
		newId = WA_SpawnFromText( kv, err, sizeof( err ) );
		// map=1: an entity of the map that was deleted and is taken back (undo), not a placed one. One that came
		// with the very keys the map file had it with is that entity again, whatever the page says.
		if ( newId >= 0 )
			entityPlaced[newId] = (qboolean)( !entityOriginal[newId]
				&& !( WA_FormValue( body, "map", ofMap, sizeof( ofMap ) ) && ofMap[0] == '1' ) );
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
	// class=<classname>: what the page takes the entity for. The game uses a number again: one it freed since
	// the page last looked (an item that started inside a wall, something that broke) may be another entity by
	// now, and a delete or an edit that was meant for the old one must not reach it.
	if ( WA_FormValue( body, "class", expected, sizeof( expected ) ) && expected[0] && Q_stricmp( g_entities[id].classname, expected ) ) {
		WA_EntityResult( c, qfalse, "that entity is gone, another one has its number now", id );
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
		// Spawn the new one first, so a bad edit keeps the old entity. For the layout the old one is gone before
		// that already: keys that are put back to what the map file has make the new one that entity again.
		original = entityOriginal[id];
		if ( original )
			originals[original - 1].gone = qtrue;
		newId = WA_SpawnFromText( kv, err, sizeof( err ) );
		if ( newId < 0 ) {
			if ( original )
				originals[original - 1].gone = qfalse;
			WA_EntityResult( c, qfalse, err, id );
			return;
		}
		entityOriginal[id] = 0;
		entityPlaced[newId] = (qboolean)( !entityOriginal[newId] && entityPlaced[id] );
		WA_TakeTeamPlace( &g_entities[id], &g_entities[newId] );
		WA_FreeEntity( &g_entities[id] );
		WA_EntityResult( c, qtrue, "updated", newId );
	} else {
		WA_EntityResult( c, qfalse, "unknown op", id );
	}
}

/*
==============================================================================

The layout of a map, and its reload

Not in JKAdmin. A start of the map removes what was done to it through the panel, and the module is loaded
anew with nothing it remembered. A layout is what was done, as text: the entities that are there and that the
map file does not bring (placed ones, and ones of the map that were changed), each with the keys it was
spawned with and the place it is at now, and the entities of the map file that are not to be there (deleted,
or changed), each with the keys the map file has it with.

  map=<name>
  -{            an entity of the map file that is taken away
  key=value
  }
  {             an entity that is spawned
  key=value
  }

Saved, it is the file layouts/<map>.txt in the game folder, and is done to the map again at every start of it,
with the panel on or off. An entity to take away that the map does not have (another game type, another
version of the map) is passed over.

"Reload map" writes the same text for the one start that follows: the engine looks through its folders for pk3
files whenever a map starts (FS_Restart in SV_SpawnServer) and at no other time, so starting the running map
again is what makes it find a pk3 that was uploaded. The players stay connected. That file wins over the saved
layout for that one start, it is the map as it stood.

==============================================================================
*/

#define WA_RELOAD_FILE		"webadmin_reload.dat"
#define WA_LAYOUT_FOLDER	"layouts"
#define WA_RELOAD_DELAY		1500		// msec after the start of the map

// the keys of an entity as a block of a layout. at: the entity, for where it is now, NULL for the keys as they are
static void WA_LayoutBlock( waBuffer_t *b, const char *open, const char *vars, int len, const gentity_t *at ) {
	const char *key, *end;

	WA_BufAppend( b, open, (int)strlen( open ) );
	for ( key = vars, end = vars + len; key < end; ) {
		const char *value = key + strlen( key ) + 1;

		vec3_t put;

		// Where it is now, not where it was put, when the two are not the same place: a ball rolls. One that
		// stands where its key says keeps the key as it is written.
		if ( at && !at->r.bmodel && !Q_stricmp( key, "origin" )
			&& ( sscanf( value, "%f %f %f", &put[0], &put[1], &put[2] ) != 3 || Distance( put, at->r.currentOrigin ) > 0.5f ) ) {
			WA_BufPrintf( b, "origin=%.1f %.1f %.1f\n", at->r.currentOrigin[0], at->r.currentOrigin[1], at->r.currentOrigin[2] );
		} else {
			WA_BufPrintf( b, "%s=%s\n", key, value );
		}
		key = value + strlen( value ) + 1;
	}
	WA_BufAppend( b, "}\n", 2 );
}

// The layout of the map as it stands: what of the map file is gone, and what is there that the map file does
// not bring. b may be NULL, to count only. adds and removes: how many of each.
static void WA_LayoutText( waBuffer_t *b, const char *mapname, int *adds, int *removes ) {
	int i;

	*adds = *removes = 0;
	if ( b )
		WA_BufPrintf( b, "map=%s\n", mapname );
	for ( i = 0; i < originalCount; i++ ) {
		if ( !originals[i].gone )
			continue;
		if ( b )
			WA_LayoutBlock( b, "-{\n", originals[i].vars, originals[i].len, NULL );
		( *removes )++;
	}
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];

		// one that still is an entity of the map file is brought by the map
		if ( !entityVars[i] || entityOriginal[i] || !WA_ListedEntity( ent ) )
			continue;
		if ( b )
			WA_LayoutBlock( b, "{\n", entityVars[i], entityVarsLen[i], ent );
		( *adds )++;
	}
}

// the name of the map that runs, and of the file its layout is kept in. qfalse: no map, or a name that is none
static qboolean WA_LayoutFile( char *mapname, int mapSize, char *file, int fileSize ) {
	trap->Cvar_VariableStringBuffer( "mapname", mapname, mapSize );
	Q_strstrip( mapname, "\n\r\";", NULL );
	if ( !mapname[0] || strstr( mapname, ".." ) || mapname[0] == '/' )
		return qfalse;
	Com_sprintf( file, fileSize, WA_LAYOUT_FOLDER "/%s.txt", mapname );
	return qtrue;
}

void WA_ReloadMap( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH], file[MAX_QPATH];
	int			adds, removes;

	if ( !WA_LayoutFile( mapname, sizeof( mapname ), file, sizeof( file ) ) ) {
		WA_ActionResult( c, qfalse, "no map is running" );
		return;
	}

	// the map as it stands, for the one start that follows
	WA_LayoutText( &b, mapname, &adds, &removes );
	if ( !WA_WriteGameFile( WA_RELOAD_FILE, b.data, b.len ) ) {
		WA_BufFree( &b );
		WA_ActionResult( c, qfalse, "could not write " WA_RELOAD_FILE ", the map is not started again" );
		return;
	}
	WA_BufFree( &b );

	// devmap keeps the cheats on if they are
	trap->SendConsoleCommand( EXEC_APPEND, va( "%s \"%s\"\n", trap->Cvar_VariableIntegerValue( "sv_cheats" ) ? "devmap" : "map", mapname ) );
	WA_ActionResult( c, qtrue, va( "starting %s again, %d placed objects come back", mapname, adds ) );
}

// the text of a layout if it is one for this map: what follows its first line, which names the map. NULL: not.
// text is freed or handed back.
static char *WA_LayoutBody( char *text, const char *mapname ) {
	char *body = text ? strchr( text, '\n' ) : NULL;

	if ( !body || strncmp( text, "map=", 4 ) || (int)( body - text ) - 4 != (int)strlen( mapname )
		|| strncmp( text + 4, mapname, strlen( mapname ) ) ) {
		free( text );
		return NULL;
	}
	memmove( text, body + 1, strlen( body + 1 ) + 1 );
	return text;
}

// At the start of a map: takes what is to be done to it. That is what WA_ReloadMap() left for this one start
// (the file is removed whatever is in it), or else the layout that was saved for the map.
void WA_ReloadRead( void ) {
	char		path[MAX_OSPATH], mapname[MAX_QPATH], file[MAX_QPATH];
	qboolean	exists, isDir;
	double		size;
	FILE		*f;
	char		*text = NULL, *saved, *whole;
	int			len;

	free( reloadText );
	reloadText = NULL;
	layoutSaved = layoutDirty = qfalse;
	if ( !WA_LayoutFile( mapname, sizeof( mapname ), file, sizeof( file ) ) )
		return;

	if ( WA_FsResolve( qfalse, WA_RELOAD_FILE, qfalse, path, sizeof( path ), &exists )
		&& WA_FsStat( path, &isDir, &size, NULL ) && !isDir ) {
		len = ( size < 4 * 1024 * 1024 ) ? (int)size : 0;
		f = fopen( path, "rb" );
		text = f ? (char *)malloc( len + 1 ) : NULL;
		if ( text )
			len = (int)fread( text, 1, len, f );
		if ( f )
			fclose( f );
		remove( path );
		if ( text )
			text[len] = '\0';
		reloadText = WA_LayoutBody( text, mapname );
	}

	// the layout that was saved for this map
	saved = WA_ReadGameFile( file, &len );
	whole = saved ? (char *)malloc( len + 1 ) : NULL;
	if ( whole ) {
		memcpy( whole, saved, len );
		whole[len] = '\0';
		whole = WA_LayoutBody( whole, mapname );
	}
	free( saved );
	layoutSaved = (qboolean)( whole != NULL );
	if ( reloadText ) {
		// what a reload carried over is the map as it stood then: not what was saved, unless the two are the same
		layoutDirty = (qboolean)( whole ? strcmp( whole, reloadText ) != 0 : strstr( reloadText, "{\n" ) != NULL );
		free( whole );
	} else {
		reloadText = whole;
	}
}

// An entity of the map file that a layout takes away: the one that still has the keys of this block, which are
// lines of key=value. qfalse: there is none (another game type, or the map file is not the one it was).
static qboolean WA_LayoutRemove( const char *lines ) {
	static char	vars[MAX_SPAWN_VARS_CHARS];
	int			len = 0, i;

	while ( *lines ) {
		const char	*end = strchr( lines, '\n' ), *eq;
		const int	n = end ? (int)( end - lines ) : (int)strlen( lines );

		eq = (const char *)memchr( lines, '=', n );
		if ( eq ) {
			if ( len + n + 2 > (int)sizeof( vars ) )
				return qfalse;
			memcpy( vars + len, lines, eq - lines );
			len += (int)( eq - lines );
			vars[len++] = '\0';
			memcpy( vars + len, eq + 1, n - (int)( eq - lines ) - 1 );
			len += n - (int)( eq - lines ) - 1;
			vars[len++] = '\0';
		}
		lines = end ? end + 1 : lines + n;
	}

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		const waOriginal_t *o = entityOriginal[i] ? &originals[entityOriginal[i] - 1] : NULL;

		if ( o && g_entities[i].inuse && o->len == len && !memcmp( o->vars, vars, len ) ) {
			WA_FreeEntity( &g_entities[i] );
			return qtrue;
		}
	}
	return qfalse;
}

// does to the map what WA_ReloadRead() took, when the map has run for a moment
static void WA_ReloadSpawn( void ) {
	char		*block = reloadText, err[128];
	int			added = 0, lost = 0, removed = 0, missing = 0;
	const qboolean	wasDirty = layoutDirty;

	while ( ( block = strstr( block, "{\n" ) ) != NULL ) {
		const qboolean	remove = (qboolean)( block > reloadText && block[-1] == '-' );
		char			*end = strstr( block + 1, "\n}\n" );

		if ( !end )
			break;
		end[1] = '\0';
		if ( remove ) {
			if ( WA_LayoutRemove( block + 2 ) )
				removed++;
			else
				missing++;
		} else {
			const int id = WA_SpawnFromText( block + 2, err, sizeof( err ) );

			if ( id >= 0 ) {
				entityPlaced[id] = (qboolean)!entityOriginal[id];
				added++;
			} else {
				lost++;
			}
		}
		block = end + 3;
	}
	free( reloadText );
	reloadText = NULL;
	// the map is now as the file has it: doing that was no change
	layoutDirty = wasDirty;
	trap->Print( "Web admin: the layout of the map: %d entities placed, %d of the map taken away%s%s\n", added, removed,
		lost ? va( ", %d did not spawn", lost ) : "", missing ? va( ", %d to take away were not there", missing ) : "" );
}

/*
=================
WA_ApiLayout

POST /api/layout, op=save: the map as it stands is kept for every start of it. op=forget: it is not any more.
=================
*/
void WA_ApiLayout( waConnection_t *c, const char *body ) {
	waBuffer_t	b = { 0 };
	char		op[16], mapname[MAX_QPATH], file[MAX_QPATH], path[MAX_OSPATH];
	qboolean	exists;
	int			adds, removes;

	WA_FormValue( body, "op", op, sizeof( op ) );
	WA_LogUse( c, va( "layout %s", op ) );
	if ( !WA_LayoutFile( mapname, sizeof( mapname ), file, sizeof( file ) ) ) {
		WA_ActionResult( c, qfalse, "no map is running" );
		return;
	}

	if ( !strcmp( op, "save" ) ) {
		WA_LayoutText( &b, mapname, &adds, &removes );
		if ( !WA_WriteGameFile( file, b.data, b.len ) ) {
			WA_BufFree( &b );
			WA_ActionResult( c, qfalse, va( "could not write %s", file ) );
			return;
		}
		WA_BufFree( &b );
		layoutSaved = qtrue;
		layoutDirty = qfalse;
		WA_ActionResult( c, qtrue, va( "Saved as %s: %d entities placed or changed, %d of the map taken away. It comes back with the map.",
			file, adds, removes ) );
	} else if ( !strcmp( op, "forget" ) ) {
		if ( !WA_FsResolve( qfalse, file, qfalse, path, sizeof( path ), &exists ) || !exists || remove( path ) != 0 ) {
			WA_ActionResult( c, qfalse, layoutSaved ? va( "could not remove %s", file ) : "no layout is saved for this map" );
			return;
		}
		layoutSaved = qfalse;
		WA_LayoutText( NULL, mapname, &adds, &removes );
		layoutDirty = (qboolean)( adds + removes > 0 );
		WA_ActionResult( c, qtrue, "Forgotten. The next start of the map is the map as its file has it." );
	} else {
		WA_ActionResult( c, qfalse, "unknown op" );
	}
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
	for ( i = 0; i < originalCount; i++ )
		free( originals[i].vars );
	originalCount = 0;
	free( reloadText );
	reloadText = NULL;
}

// GET /api/classdoc: the entity definitions of the game code, every QUAKED comment of its source files as it
// stands there (cmake/Modules/ExtractQuaked.cmake). The editor of the page reads the classes from it: what one
// does, its keys, the names of its spawnflags, its box and its colour.
void WA_ApiClassDoc( waConnection_t *c ) {
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: max-age=3600\r\n", (const char *)classdoc_txt, (int)classdoc_txt_len );
}

// GET /api/classkeys: the keys the game code reads, for the completion of the editor. A line for a class: its
// name, then for every key a tab and key|kind|default (cmake/Modules/ExtractSpawnKeys.cmake has the form and
// how they are found). The first line, with the name *, is the keys every entity takes: the table of the
// fields in g_spawn.c, which has no defaults.
void WA_ApiClassKeys( waConnection_t *c ) {
	waBuffer_t	b = { 0 };
	const char	*name, *kind;
	int			i;

	WA_BufAppend( &b, "*", 1 );
	for ( i = 0; G_SpawnFieldInfo( i, &name, &kind ); i++ )
		WA_BufPrintf( &b, "\t%s|%s|", name, kind );
	WA_BufAppend( &b, "\n", 1 );
	WA_BufAppend( &b, (const char *)classkeys_txt, (int)classkeys_txt_len );
	WA_Respond( c, 200, "OK", "text/plain; charset=utf-8", "Cache-Control: max-age=3600\r\n", b.data, b.len );
	WA_BufFree( &b );
}

