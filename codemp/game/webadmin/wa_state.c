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

// wa_state.c -- /api/state of the web admin panel: the map, the players, NPCs, items, models, what
// moves and what flies and the effects, as the page asks for it ten times a second.

#include "game/webadmin/wa_local.h"

const char *WA_TeamName( team_t team ) {
	switch ( team ) {
	case TEAM_RED: return "red";
	case TEAM_BLUE: return "blue";
	case TEAM_SPECTATOR: return "spectator";
	default: return "free";
	}
}

void WA_AppendVec( waBuffer_t *b, const vec3_t v ) {
	WA_BufPrintf( b, "[%.1f,%.1f,%.1f]", v[0], v[1], v[2] );
}

// The number of a part of the answer a page keeps: a hash of its text (FNV-1a), and never 0, which is what a
// page sends that has none.
static unsigned WA_StateRev( const waBuffer_t *text ) {
	unsigned	rev = 2166136261u;
	int			k;

	for ( k = 0; k < text->len; k++ )
		rev = ( rev ^ (unsigned char)text->data[k] ) * 16777619u;
	return rev ? rev : 1;
}

// the kind of ammunition the weapon in his hands takes, AMMO_NONE for none
static int WA_StateAmmoIndex( const playerState_t *ps ) {
	int index;

	if ( ps->weapon <= WP_NONE || ps->weapon >= WP_NUM_WEAPONS )
		return AMMO_NONE;
	index = weaponData[ps->weapon].ammoIndex;
	return ( index > AMMO_NONE && index < AMMO_MAX ) ? index : AMMO_NONE;
}

// The frame the skeleton of a client or an NPC is at in the animation anim, as the server animates it
// (G_UpdateClientAnims sets the same animations a client sets: the legs on "model_root", the torso on
// "lower_lumbar"), and how fast it runs in frames per 50 ms, below zero when it runs backwards.
// -1 and 0 when the skeleton is not in that animation: the player state has a new one from his last move
// and the skeleton gets it later in the frame, or the server animates none (g_disableServerG2, a follower).
static void WA_StateBoneFrame( gentity_t *ent, const char *bone, int anim, float *frame, float *speed ) {
	int					start, end, flags;
	const animation_t	*a;

	*frame = -1.0f;
	*speed = 0.0f;
	if ( !ent->ghoul2 || !trap->G2API_HaveWeGhoul2Models( ent->ghoul2 ) || anim < 0 || anim >= MAX_TOTALANIMATIONS
		|| ent->localAnimIndex < 0 || ent->localAnimIndex >= MAX_ANIM_FILES || !bgAllAnims[ent->localAnimIndex].anims )
		return;
	a = &bgAllAnims[ent->localAnimIndex].anims[anim];
	if ( !trap->G2API_GetBoneAnim( ent->ghoul2, bone, level.time, frame, &start, &end, &flags, speed, NULL, 0 )
		|| *frame < a->firstFrame || *frame > a->firstFrame + a->numFrames ) {
		*frame = -1.0f;
		*speed = 0.0f;
	}
}

// What a page says it has of the players: known[client] becomes the number of the part "s" it has of him,
// 0 for none. p=<client>.<number>-<client>.<number>...
static void WA_StateKnown( const char *query, unsigned *known ) {
	static char	knownText[MAX_CLIENTS * 16];

	memset( known, 0, MAX_CLIENTS * sizeof( *known ) );
	if ( WA_FormValue( query, "p", knownText, sizeof( knownText ) ) ) {
		const char *at = knownText;

		while ( *at ) {
			char		*end;
			const long	id = strtol( at, &end, 10 );
			unsigned long rev;

			if ( end == at || *end != '.' )
				break;
			at = end + 1;
			rev = strtoul( at, &end, 10 );
			if ( end == at )
				break;
			if ( id >= 0 && id < MAX_CLIENTS )
				known[id] = (unsigned)rev;
			at = ( *end == '-' ) ? end + 1 : end;
			if ( *end && *end != '-' )
				break;
		}
	}
}

// What stays the same of a player from one poll to the next, as an object in slow. Returns its number
// (WA_StateRev).
static unsigned WA_StatePlayerStays( waBuffer_t *slow, int clientNum ) {
	gentity_t		*ent = &g_entities[clientNum];
	gclient_t		*cl = ent->client;
	playerState_t	*ps = &cl->ps;
	int				k, s2, bl;

	slow->len = 0;
	WA_BufAppend( slow, "{\"name\":", 8 );
	WA_BufJsonString( slow, cl->pers.netname );
	// Not in JKAdmin: who that is for an admin. Where he connects from (without the port, nothing for a bot),
	// when he came (in the time of the map, as "time" above is) and which client plugin of JA+ he has, 0 for none.
	{
		char address[NET_ADDRSTRMAXLEN], *port;

		Q_strncpyz( address, ( ent->r.svFlags & SVF_BOT ) ? "" : cl->sess.IP, sizeof( address ) );
		port = strchr( address, ':' );
		if ( port )
			*port = '\0';
		WA_BufAppend( slow, ",\"address\":", 11 );
		WA_BufJsonString( slow, address );
		WA_BufPrintf( slow, ",\"entered\":%d,\"plugin\":%.2f", cl->pers.enterTime - level.startTime, cl->pers.jp.pluginVersion );
	}
	WA_BufPrintf( slow, ",\"team\":\"%s\",\"bot\":%s,\"connected\":%s,\"maxHealth\":%d,\"scale\":%d,\"god\":%s",
		WA_TeamName( cl->sess.sessionTeam ), ( ent->r.svFlags & SVF_BOT ) ? "true" : "false",
		cl->pers.connected == CON_CONNECTED ? "true" : "false", ps->stats[STAT_MAX_HEALTH],
		ps->iModelScale ? ps->iModelScale : 100, ( ent->flags & FL_GODMODE ) ? "true" : "false" );
	WA_BufPrintf( slow, ",\"noclip\":%s", cl->noclip ? "true" : "false" );		// not in JKAdmin
	WA_BufPrintf( slow, ",\"gravity\":%d,\"ownGravity\":%s", ps->gravity, WA_BOOL( WA_GravityIsSet( clientNum ) ) );	// not in JKAdmin
	WA_BufPrintf( slow, ",\"schizo\":%s", WA_BOOL( G_SchizoActive( clientNum ) ) );		// not in JKAdmin, g_schizo.c
	WA_BufPrintf( slow, ",\"flatulence\":%s", WA_BOOL( G_FlatulenceActive( clientNum ) ) );	// not in JKAdmin, g_flatulence.c
	WA_BufPrintf( slow, ",\"disorient\":%s", WA_BOOL( G_DisorientActive( clientNum ) ) );	// not in JKAdmin, g_disorient.c
	{
		// not in JKAdmin: what the JA+ buttons of the page show. admin is jpAdminLevel_t, altDim 2 is forced.
		const jpClient_t *jp = &cl->pers.jp;

		WA_BufPrintf( slow, ",\"jp\":{\"admin\":%d,\"sleep\":%s,\"protect\":%s,\"empower\":%s,\"merc\":%s,\"mindtrick\":%s,"
			"\"ghost\":%s,\"silence\":%s,\"altDim\":%d,\"denyVote\":%s,\"frozen\":%s}",
			(int)cl->sess.jpAdminLevel, WA_BOOL( jp->sleeping ), WA_BOOL( jp->protectedByAdmin ), WA_BOOL( jp->empowered ),
			WA_BOOL( jp->merc ), WA_BOOL( jp->mindtricked ), WA_BOOL( jp->isGhost ), WA_BOOL( jp->silenced ), jp->dimension,
			WA_BOOL( jp->denyVote ), WA_BOOL( jp->frozen ) );
	}
	if ( ps->weapon > WP_NONE && ps->weapon < WP_NUM_WEAPONS ) {
		const int ammoIndex = WA_StateAmmoIndex( ps );

		WA_BufPrintf( slow, ",\"maxAmmo\":%d,\"ammoInfinite\":%s", ammoIndex != AMMO_NONE ? ammoData[ammoIndex].max : 0,
			( !weaponData[ps->weapon].energyPerShot && !weaponData[ps->weapon].altEnergyPerShot ) ? "true" : "false" );
	}
	WA_BufPrintf( slow, ",\"saberStyle\":%d,\"forceMax\":%d,\"sabers\":[", ps->fd.saberDrawAnimLevel, ps->fd.forcePowerMax ? ps->fd.forcePowerMax : 100 );
	for ( s2 = 0, k = 0; s2 < MAX_SABERS; s2++ ) {
		const saberInfo_t *saber = &cl->saber[s2];

		if ( !saber->model[0] )
			continue;
		if ( k++ )
			WA_BufAppend( slow, ",", 1 );
		WA_BufAppend( slow, "{\"model\":", 9 );
		WA_BufJsonString( slow, saber->model );
		WA_BufAppend( slow, ",\"blades\":[", 11 );
		for ( bl = 0; bl < saber->numBlades && bl < MAX_BLADES; bl++ ) {
			const bladeInfo_t *blade = &saber->blade[bl];

			WA_BufPrintf( slow, "%s{\"length\":%.1f,\"radius\":%.1f,\"color\":%d}", bl ? "," : "", blade->lengthMax, blade->radius, blade->color );
		}
		// what of the saber file a client draws the blades by: its second flags (no blade, no light, no marks),
		// the styles of its trail, and from which blade on the second set of them holds
		WA_BufPrintf( slow, "],\"flags2\":%d,\"trailStyle\":%d,\"trailStyle2\":%d,\"bladeStyle2Start\":%d}",
			saber->saberFlags2, saber->trailStyle, saber->trailStyle2, saber->bladeStyle2Start );
	}
	WA_BufAppend( slow, "],\"model\":", 10 );
	{
		char userinfo[MAX_INFO_STRING];

		trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
		WA_BufJsonString( slow, Info_ValueForKey( userinfo, "model" ) );
		// not in JKAdmin: the colours a blade of the colour "rgb" has (r | g << 8 | b << 16), as the clients are
		// told them (c3 and c4 of ClientUserinfoChanged)
		if ( jp_allowRGBsaber.integer == 1 )
			WA_BufPrintf( slow, ",\"saberRgb\":[%d,%d]", atoi( Info_ValueForKey( userinfo, "cp_sbRGB1" ) ),
				atoi( Info_ValueForKey( userinfo, "cp_sbRGB2" ) ) );
		// The colour of his first and of his second saber as a client draws them: color1 and color2 of his
		// userinfo, which the clients are told as c1 and c2. Not the colour of a blade above: that one is of
		// the saber file as the server read it, and a client does not draw by it.
		WA_BufPrintf( slow, ",\"saberColors\":[%d,%d]", atoi( Info_ValueForKey( userinfo, "color1" ) ),
			atoi( Info_ValueForKey( userinfo, "color2" ) ) );
	}
	WA_BufAppend( slow, "}", 1 );
	return WA_StateRev( slow );
}

// What changes of a player all the time, as the rows "f" and "b". rev is the number of what stays of him.
static void WA_StatePlayerChanges( waBuffer_t *b, int clientNum, unsigned rev, qboolean first ) {
	gentity_t		*ent = &g_entities[clientNum];
	gclient_t		*cl = ent->client;
	playerState_t	*ps = &cl->ps;
	const int		ammoIndex = WA_StateAmmoIndex( ps );
	int				s2, bl;
	qboolean		firstBlade = qtrue;
	float			legsFrame, legsSpeed, torsoFrame, torsoSpeed;

	WA_StateBoneFrame( ent, "model_root", ps->legsAnim, &legsFrame, &legsSpeed );
	WA_StateBoneFrame( ent, "lower_lumbar", ps->torsoAnim, &torsoFrame, &torsoSpeed );
	WA_BufPrintf( b, "%s{\"id\":%d,\"r\":%u,\"f\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%.1f,%.1f,%d,%d,%d,%d,",
		first ? "" : ",", clientNum, rev,
		ps->stats[STAT_HEALTH], ps->stats[STAT_ARMOR], ps->persistant[PERS_SCORE], ps->ping, ps->weapon, ps->fd.forcePower,
		( ( ps->pm_type == PM_DEAD || ps->stats[STAT_HEALTH] <= 0 ) ? 1 : 0 ) | ( ( ps->weapon == WP_SABER && !ps->saberHolstered ) ? 2 : 0 )
			| ( ps->saberInFlight ? 4 : 0 ),
		ps->saberHolstered, ps->saberEntityNum, ps->origin[0], ps->origin[1], ps->origin[2],
		ps->velocity[0], ps->velocity[1], ps->velocity[2], ps->viewangles[YAW], ps->viewangles[PITCH],
		ps->legsAnim, ps->torsoAnim, ps->viewheight, ammoIndex != AMMO_NONE ? ps->ammo[ammoIndex] : -1 );
	// the flags are the ones of his entity: what the clients are sent of him (BG_PlayerStateToEntityState)
	WA_BufPrintf( b, "%.1f,%.2f,%.1f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],\"b\":[",
		legsFrame, legsSpeed, torsoFrame, torsoSpeed, ent->s.eFlags, ent->s.eFlags2, ent->s.powerups, ps->fd.forcePowersActive,
		ps->weaponstate, ps->saberMove, ( ps->legsFlip ? 1 : 0 ) | ( ps->torsoFlip ? 2 : 0 ), ps->groundEntityNum,
		ps->brokenLimbs, ps->fd.saberAnimLevel );
	for ( s2 = 0; s2 < MAX_SABERS; s2++ ) {
		const saberInfo_t *saber = &cl->saber[s2];

		if ( !saber->model[0] )
			continue;
		for ( bl = 0; bl < saber->numBlades && bl < MAX_BLADES; bl++ ) {
			const bladeInfo_t *blade = &saber->blade[bl];

			WA_BufPrintf( b, "%s[%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f]", firstBlade ? "" : ",", blade->length,
				blade->muzzlePoint[0], blade->muzzlePoint[1], blade->muzzlePoint[2],
				blade->muzzleDir[0], blade->muzzleDir[1], blade->muzzleDir[2] );
			firstBlade = qfalse;
		}
	}
	WA_BufAppend( b, "]", 1 );
}

// Of a player most of what is told stays the same from one poll to the next: who he is, what he has, what
// was done to him. That part ("s") has a number ("r", a hash of its text), and a page that says it has the
// part of that number (p=<client>.<number>-<client>.<number>...) is not sent it again. What changes all the
// time comes as two rows of numbers:
//   "f": health, armor, score, ping, weapon, force, flags (1 dead, 2 saber on, 4 saber in flight),
//        saberHolstered, entity of the saber, origin (3), velocity (3), yaw, pitch, legs and torso animation,
//        view height, ammo of the weapon (-1: it has none),
//        from 21 on what a client draws him by: the frame of the legs and how fast it runs, the same of the
//        torso (WA_StateBoneFrame, -1 and 0 when the server has none of that animation), eFlags, eFlags2 and
//        powerups of his entity, the force powers that are on,
//        weaponstate, saberMove, the flips (1 legs, 2 torso: an animation that starts again), the entity he
//        stands on (ENTITYNUM_NONE in the air), brokenLimbs, the saber style he fights in
//   "b": for every blade of his sabers its length now, where it starts (3) and the way it points (3)
// A player was about a kilobyte ten times a second before, he is about 220 bytes now.
static void WA_StatePlayers( waBuffer_t *b, const char *query ) {
	unsigned	known[MAX_CLIENTS];
	waBuffer_t	slow = { 0 };
	int			i;
	qboolean	first = qtrue;

	WA_BufAppend( b, ",\"players\":[", 12 );
	WA_StateKnown( query, known );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t	*ent = &g_entities[i];
		unsigned	rev;

		if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED )
			continue;

		rev = WA_StatePlayerStays( &slow, i );
		WA_StatePlayerChanges( b, i, rev, first );
		first = qfalse;
		if ( known[i] != rev ) {
			WA_BufAppend( b, ",\"s\":", 5 );
			WA_BufAppend( b, slow.data, slow.len );
		}
		WA_BufAppend( b, "}", 1 );
	}
	WA_BufFree( &slow );
	WA_BufAppend( b, "]", 1 );
}

// NPCs, drawn with their models too. The model is the one a client loads for it (CG_G2AnimEntModelLoad): a
// vehicle tells the clients its name with a $ in front, and they take the model and the skin from what the
// vehicle files say of that name. "skin" is there for a vehicle only, "" for the default one. "f" is what a
// client draws it by, as from 21 on in the row of a player: the frame of the legs and how fast it runs, the
// same of the torso, eFlags, eFlags2, powerups, the flips, the entity it stands on.
static void WA_StateNpcs( waBuffer_t *b ) {
	int			i;
	qboolean	first = qtrue;

	WA_BufAppend( b, ",\"npcs\":[", 9 );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t		*ent = &g_entities[i];
		playerState_t	*ps;
		char			model[MAX_QPATH];
		float			legsFrame, legsSpeed, torsoFrame, torsoSpeed;
		const qboolean	vehicle = (qboolean)( ent->inuse && ent->s.NPC_class == CLASS_VEHICLE && ent->m_pVehicle
							&& ent->m_pVehicle->m_pVehicleInfo && VALIDSTRING( ent->m_pVehicle->m_pVehicleInfo->model ) );

		if ( !ent->inuse || ent->s.eType != ET_NPC || !ent->client )
			continue;
		ps = &ent->client->ps;

		if ( !first )
			WA_BufAppend( b, ",", 1 );
		first = qfalse;

		if ( vehicle )
			Com_sprintf( model, sizeof( model ), "models/players/%s/model.glm", ent->m_pVehicle->m_pVehicleInfo->model );
		else
			trap->GetConfigstring( CS_MODELS + ent->s.modelindex, model, sizeof( model ) );
		WA_BufPrintf( b, "{\"id\":%d,\"name\":", i );
		WA_BufJsonString( b, ent->NPC_type ? ent->NPC_type : "NPC" );
		WA_BufAppend( b, ",\"model\":", 9 );
		WA_BufJsonString( b, model );
		if ( vehicle ) {
			WA_BufAppend( b, ",\"skin\":", 8 );
			WA_BufJsonString( b, VALIDSTRING( ent->m_pVehicle->m_pVehicleInfo->skin ) ? ent->m_pVehicle->m_pVehicleInfo->skin : "" );
		}
		WA_StateBoneFrame( ent, "model_root", ps->legsAnim, &legsFrame, &legsSpeed );
		WA_StateBoneFrame( ent, "lower_lumbar", ps->torsoAnim, &torsoFrame, &torsoSpeed );
		WA_BufPrintf( b, ",\"health\":%d,\"dead\":%s,\"scale\":%d,\"yaw\":%.1f,\"legsAnim\":%d,\"torsoAnim\":%d,\"class\":%d,\"weapon\":%d,"
			"\"angles\":[%.1f,%.1f,%.1f],\"vel\":[%.0f,%.0f,%.0f],\"f\":[%.1f,%.2f,%.1f,%.2f,%d,%d,%d,%d,%d],\"pos\":",
			ent->health, ( ent->health <= 0 ) ? "true" : "false", ps->iModelScale ? ps->iModelScale : 100,
			ps->viewangles[YAW], ps->legsAnim, ps->torsoAnim, ent->s.NPC_class, ps->weapon,
			ps->viewangles[PITCH], ps->viewangles[YAW], ps->viewangles[ROLL], ps->velocity[0], ps->velocity[1], ps->velocity[2],
			legsFrame, legsSpeed, torsoFrame, torsoSpeed, ent->s.eFlags, ent->s.eFlags2, ent->s.powerups,
			( ps->legsFlip ? 1 : 0 ) | ( ps->torsoFlip ? 2 : 0 ), ps->groundEntityNum );
		WA_AppendVec( b, ps->origin );
		WA_BufAppend( b, "}", 1 );
	}
	WA_BufAppend( b, "]", 1 );
}

// Items, flags and other pickups, so the map looks alive. The list is the same from one poll to the next
// unless something was picked up or came back, and it is more than a third of the answer. So it has a
// number (WA_StateRev), and a page that says it has the list of that number (items=) is not sent it again.
// Each is a row of what CG_Item draws it by: its entity, its place in the list of the items (bg_itemlist, the
// "index" of /api/classes), the eFlags of its entity, where it is and its angles. All of them the clients are
// sent, also one that is not drawn (EF_NODRAW) or is the ghost of one that was taken (EF_ITEMPLACEHOLDER):
// what of it shows is for the page to say, as it is for a client.
static void WA_StateItems( waBuffer_t *b, const char *query ) {
	waBuffer_t	items = { 0 };
	char		value[16];
	unsigned	rev;
	int			i;
	qboolean	first = qtrue;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];

		if ( !ent->inuse || ent->s.eType != ET_ITEM || !ent->item || ent->s.modelindex <= 0 || ent->s.modelindex >= bg_numItems
			|| ( ent->r.svFlags & SVF_NOCLIENT ) )
			continue;

		WA_BufPrintf( &items, "%s[%d,%d,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", first ? "" : ",", i, ent->s.modelindex, ent->s.eFlags,
			ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2],
			ent->s.angles[0], ent->s.angles[1], ent->s.angles[2] );
		first = qfalse;
	}
	rev = WA_StateRev( &items );

	WA_BufPrintf( b, ",\"itemsRev\":%u", rev );
	if ( !WA_FormValue( query, "items", value, sizeof( value ) ) || (unsigned)strtoul( value, NULL, 10 ) != rev ) {
		WA_BufAppend( b, ",\"items\":[", 10 );
		if ( items.len )
			WA_BufAppend( b, items.data, items.len );
		WA_BufAppend( b, "]", 1 );
	}
	WA_BufFree( &items );
}

// Not in JKAdmin: what else the clients draw as a model, where they draw it: the props, the models of the
// map that are entities. Not what is made of brushes, and not what only one client is sent.
static void WA_StateModels( waBuffer_t *b ) {
	int			i;
	qboolean	first = qtrue;

	WA_BufAppend( b, ",\"models\":[", 11 );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		char		model[MAX_QPATH];
		vec3_t		pos, angles;

		if ( !ent->inuse || ent->s.eType != ET_GENERAL || !ent->s.modelindex || ent->r.bmodel || ( ent->s.eFlags & EF_NODRAW )
			|| ( ent->r.svFlags & ( SVF_NOCLIENT | SVF_SINGLECLIENT ) ) )
			continue;
		trap->GetConfigstring( CS_MODELS + ent->s.modelindex, model, sizeof( model ) );
		if ( !model[0] || model[0] == '*' )
			continue;

		if ( !first )
			WA_BufAppend( b, ",", 1 );
		first = qfalse;

		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		BG_EvaluateTrajectory( &ent->s.apos, level.time, angles );
		WA_BufPrintf( b, "{\"id\":%d,\"scale\":%d,\"model\":", i, ent->s.iModelScale ? ent->s.iModelScale : 100 );
		WA_BufJsonString( b, model );
		WA_BufAppend( b, ",\"pos\":", 7 );
		WA_AppendVec( b, pos );
		WA_BufAppend( b, ",\"angles\":", 10 );
		WA_AppendVec( b, angles );
		// A mine or a det pack is one of them once it sticks (CG_General draws the beam of a trip mine by these),
		// and so is what shines by itself (constantLight, CG_EntityEffects): only for those.
		if ( ent->s.weapon || ent->s.constantLight )
			WA_BufPrintf( b, ",\"weapon\":%d,\"eFlags\":%d,\"time\":%d,\"delta\":[%.1f,%.1f,%.1f],\"light\":%d", ent->s.weapon, ent->s.eFlags,
				ent->s.time, ent->s.pos.trDelta[0], ent->s.pos.trDelta[1], ent->s.pos.trDelta[2], ent->s.constantLight );
		WA_BufAppend( b, "}", 1 );
	}
	WA_BufAppend( b, "]", 1 );
}

// Not in JKAdmin: what is made of brushes and is an entity (doors, lifts, what turns, what can be switched
// off or broken), as the clients draw it (CG_Mover). Each is the number of its brush model, or that number
// with its place and its angles when it is not where the map file has it. One that is not in the list is
// not drawn by a client either. Two more follow for one that has them: the frame its shaders are at (the
// frame of an animMap, EF_SHADER_ANIM, else -1) and the model that is drawn with it (model2, else "").
static void WA_StateMovers( waBuffer_t *b ) {
	int			i;
	qboolean	first = qtrue;

	WA_BufAppend( b, ",\"movers\":[", 11 );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		vec3_t		pos, angles;
		char		model2[MAX_QPATH];
		qboolean	more;

		if ( !ent->inuse || !ent->r.linked || !ent->r.bmodel || ent->s.eType != ET_MOVER || ent->s.modelindex <= 0
			|| ( ent->s.eFlags & EF_NODRAW ) || ( ent->s.eFlags2 & EF2_HYPERSPACE )
			|| ( ent->r.svFlags & ( SVF_NOCLIENT | SVF_SINGLECLIENT ) ) )
			continue;

		if ( !first )
			WA_BufAppend( b, ",", 1 );
		first = qfalse;

		model2[0] = '\0';
		if ( ent->s.modelindex2 > 0 && ent->s.modelindex2 < MAX_MODELS )
			trap->GetConfigstring( CS_MODELS + ent->s.modelindex2, model2, sizeof( model2 ) );
		more = (qboolean)( model2[0] || ( ent->s.eFlags & EF_SHADER_ANIM ) );

		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		BG_EvaluateTrajectory( &ent->s.apos, level.time, angles );
		if ( !more && VectorCompare( pos, vec3_origin ) && VectorCompare( angles, vec3_origin ) ) {
			WA_BufPrintf( b, "%d", ent->s.modelindex );
			continue;
		}
		WA_BufPrintf( b, "[%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f", ent->s.modelindex, pos[0], pos[1], pos[2],
			angles[0], angles[1], angles[2] );
		if ( more ) {
			WA_BufPrintf( b, ",%d,", ( ent->s.eFlags & EF_SHADER_ANIM ) ? ent->s.frame : -1 );
			WA_BufJsonString( b, model2 );
		}
		WA_BufAppend( b, "]", 1 );
	}
	WA_BufAppend( b, "]", 1 );
}

// Not in JKAdmin: what is in the air, the shots, rockets and grenades the clients draw (CG_Missile). Each is
// its entity, the weapon, 1 when it was fired the other way, where it is and how fast it flies, its angles
// and the eFlags of its entity (a mine that sticks, a trip mine whose beam is on), then what CG_Missile turns
// it by: the kind of its way (trType), the speed it was fired with (trDelta), how long ago in msec, the time
// of its entity (a resting one is turned by it), and who fired it. Not a saber that was thrown: that is drawn
// with its player.
static void WA_StateShots( waBuffer_t *b ) {
	int			i;
	qboolean	first = qtrue;

	WA_BufAppend( b, ",\"shots\":[", 10 );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		vec3_t		pos, speed, angles;

		if ( !ent->inuse || !ent->r.linked || ent->s.eType != ET_MISSILE || ent->s.weapon == WP_SABER
			|| ( ent->s.eFlags & EF_NODRAW ) || ( ent->r.svFlags & ( SVF_NOCLIENT | SVF_SINGLECLIENT ) ) )
			continue;

		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, speed );
		BG_EvaluateTrajectory( &ent->s.apos, level.time, angles );
		WA_BufPrintf( b, "%s[%d,%d,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%d,%d,%.0f,%.0f,%.0f,%d,%d,%d]", first ? "" : ",", i, ent->s.weapon,
			( ent->s.eFlags & EF_ALT_FIRING ) ? 1 : 0, pos[0], pos[1], pos[2], speed[0], speed[1], speed[2],
			angles[0], angles[1], angles[2], ent->s.eFlags, ent->s.pos.trType, ent->s.pos.trDelta[0], ent->s.pos.trDelta[1],
			ent->s.pos.trDelta[2], level.time - ent->s.pos.trTime, ent->s.time, ent->r.ownerNum );
		first = qfalse;
	}
	WA_BufAppend( b, "]", 1 );
}

/*
==============================================================================

Effects

Not in JKAdmin. A client plays the effects of the game itself, the server only tells it which and where: an
entity that is an effect (ET_FX: fx_runner, misc_blackhole) is played in a loop by CG_FX, and what the game
plays once reaches the client as an event (EV_PLAY_EFFECT, EV_PLAY_EFFECT_ID). The page is told the same two
things and plays the effect files as a client does.

==============================================================================
*/

#define WA_FX_KEEP		64
#define WA_FX_MS		2000		// older than that is not worth playing any more

typedef struct waEffect_s {
	int		time;		// level.time
	int		kind, id;	// kind 0: id is an effectTypes_t, 1: an index of CS_EFFECTS
	vec3_t	org, dir;
} waEffect_t;

static waEffect_t	fxRing[WA_FX_KEEP];
static int			fxSeq;		// the number of the one that was played last, they count from 1 since the module was loaded

// G_PlayEffect (kind 0) and G_PlayEffectID (kind 1) say what they play. dir becomes the way the client plays it:
// for EV_PLAY_EFFECT it takes what the game calls angles as the direction itself, for EV_PLAY_EFFECT_ID the
// way those angles look (CG_EntityEvent).
void WebAdmin_Effect( int kind, int id, const vec3_t org, const vec3_t ang ) {
	waEffect_t *e;

	if ( !WA_Running() )
		return;
	e = &fxRing[fxSeq++ % WA_FX_KEEP];
	e->time = level.time;
	e->kind = kind;
	e->id = id;
	VectorCopy( org, e->org );
	if ( kind )
		AngleVectors( ang, e->dir, NULL, NULL );
	else
		VectorCopy( ang, e->dir );
	if ( !e->dir[0] && !e->dir[1] && !e->dir[2] )
		e->dir[1] = 1;
}

// The effects for a page that says what it has of them (fx=<number of the list>&fxs=<number of the last one
// played>). Both parts are left out when the page has them: the list of the entities changes when one is
// switched or moves, and most polls nothing was played.
//   "fx": the entities that are effects, each its entity, the index of its effect in CS_EFFECTS, its state
//        as CG_FX reads it (0 off, below 10 once for every change of the number, else in a loop: an fx_runner
//        has 20 then), the delay and the random part of the loop in msec, where it is and the way it plays
//   "fxPlay": what was played once and is not older than two seconds, each its number, how long ago in msec,
//        the kind (0 an effectTypes_t, 1 an index of CS_EFFECTS), which one, where and the way it plays
static void WA_StateEffects( waBuffer_t *b, const char *query ) {
	waBuffer_t	list = { 0 };
	char		value[16];
	unsigned	rev;
	int			i, have;
	qboolean	first = qtrue;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t	*ent = &g_entities[i];
		vec3_t		pos, dir;

		if ( !ent->inuse || ent->s.eType != ET_FX )
			continue;

		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		// as CG_FX turns the angles of the entity into the way the effect plays
		AngleVectors( ent->s.angles, dir, NULL, NULL );
		if ( !dir[0] && !dir[1] && !dir[2] )
			dir[1] = 1;
		WA_BufPrintf( &list, "%s[%d,%d,%d,%.0f,%d,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f]", first ? "" : ",", i, ent->s.modelindex,
			ent->s.modelindex2, ent->s.speed, ent->s.time, pos[0], pos[1], pos[2], dir[0], dir[1], dir[2] );
		first = qfalse;
	}
	rev = WA_StateRev( &list );
	if ( !WA_FormValue( query, "fx", value, sizeof( value ) ) || (unsigned)strtoul( value, NULL, 10 ) != rev ) {
		WA_BufPrintf( b, ",\"fx\":{\"rev\":%u,\"list\":[", rev );
		if ( list.len )
			WA_BufAppend( b, list.data, list.len );
		WA_BufAppend( b, "]}", 2 );
	}
	WA_BufFree( &list );

	// A page without a number is sent what there is. One that is ahead of the server has its number from before
	// the module was loaded anew, it starts again.
	have = WA_FormValue( query, "fxs", value, sizeof( value ) ) ? atoi( value ) : -1;
	if ( have == fxSeq )
		return;
	if ( have > fxSeq )
		have = 0;
	WA_BufPrintf( b, ",\"fxSeq\":%d,\"fxPlay\":[", fxSeq );
	first = qtrue;
	for ( i = Q_max( Q_max( have, 0 ), fxSeq - WA_FX_KEEP ); i < fxSeq; i++ ) {
		const waEffect_t	*e = &fxRing[i % WA_FX_KEEP];
		const int			ago = level.time - e->time;

		if ( ago < 0 || ago > WA_FX_MS )
			continue;
		WA_BufPrintf( b, "%s[%d,%d,%d,%d,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f]", first ? "" : ",", i + 1, ago, e->kind, e->id,
			e->org[0], e->org[1], e->org[2], e->dir[0], e->dir[1], e->dir[2] );
		first = qfalse;
	}
	WA_BufAppend( b, "]", 1 );
}

// &view=<client>: where that client looks from. Not for the page as it is used: for the comparison of its
// picture with the one a client of the game draws from the same spot (tools/panelshot/steps_ref.js), which
// reads the eye back instead of working it out, because a spectator that is put somewhere drifts on.
//   "view": origin, pitch yaw roll, viewheight, pm_type, level.time, velocity
static void WA_StateView( waBuffer_t *b, const char *query ) {
	char			value[8];
	int				n;
	const gclient_t	*cl;

	if ( !WA_FormValue( query, "view", value, sizeof( value ) ) )
		return;
	n = atoi( value );
	if ( n < 0 || n >= level.maxclients || level.clients[n].pers.connected != CON_CONNECTED )
		return;
	cl = &level.clients[n];
	WA_BufPrintf( b, ",\"view\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%.2f,%.2f,%.2f]",
		cl->ps.origin[0], cl->ps.origin[1], cl->ps.origin[2], cl->ps.viewangles[PITCH], cl->ps.viewangles[YAW], cl->ps.viewangles[ROLL],
		cl->ps.viewheight, cl->ps.pm_type, level.time, cl->ps.velocity[0], cl->ps.velocity[1], cl->ps.velocity[2] );
}

// one array of whole numbers of a player state, as a part of an object
static void WA_StateInts( waBuffer_t *b, const char *key, const int *v, int n ) {
	int i;

	WA_BufPrintf( b, ",\"%s\":[", key );
	for ( i = 0; i < n; i++ )
		WA_BufPrintf( b, "%s%d", i ? "," : "", v[i] );
	WA_BufAppend( b, "]", 1 );
}

// &hud=<client>: what a client draws his HUD and his own view from (CG_Draw2D, CG_CalcViewValues,
// CG_AddViewWeapon), the part of his player state the rows of the players do not have. Only for the one a page
// looks through the eyes of, and only when it asks. The times are the ones of the game as the player state has
// them, and "now" is the time of the game they are measured against.
//   "stats", "pers", "powerups", "ammo": the arrays of the player state (STAT_, PERS_, PW_ and AMMO_ of
//       bg_public.h; a powerup is the time it runs out)
//   "forceLevel": the level of every force power, "force": the powers he knows (mask), the one he has
//       selected, his side, his rank, when his rage recovery ends
//   "zoom": zoomMode, zoomFov, zoomTime, zoomLocked    "weapon": weaponTime, weaponChargeTime
//   "lock": rocketLockIndex, rocketLockTime, rocketLastValidTime, rocketTargetTime
//   "fuel": jetpackFuel, cloakFuel    "duel": duelInProgress, duelIndex, duelTime
//   "pm": pm_type, pm_flags, pm_time, bobCycle    "damage": damageEvent, damageYaw, damagePitch, damageCount, damageType
//   "angles": his view angles with the roll, "psClient": the client of the player state (the one he follows)
static void WA_StateHud( waBuffer_t *b, const char *query ) {
	char				value[8];
	int					n;
	const playerState_t	*ps;

	if ( !WA_FormValue( query, "hud", value, sizeof( value ) ) )
		return;
	n = atoi( value );
	if ( n < 0 || n >= level.maxclients || level.clients[n].pers.connected != CON_CONNECTED )
		return;
	ps = &level.clients[n].ps;

	WA_BufPrintf( b, ",\"hud\":{\"client\":%d,\"psClient\":%d,\"now\":%d", n, ps->clientNum, level.time );
	WA_StateInts( b, "stats", ps->stats, MAX_STATS );
	WA_StateInts( b, "pers", ps->persistant, MAX_PERSISTANT );
	WA_StateInts( b, "powerups", ps->powerups, MAX_POWERUPS );
	WA_StateInts( b, "ammo", ps->ammo, MAX_AMMO );
	WA_StateInts( b, "forceLevel", ps->fd.forcePowerLevel, NUM_FORCE_POWERS );
	WA_BufPrintf( b, ",\"force\":[%d,%d,%d,%d,%d],\"zoom\":[%d,%.1f,%d,%d],\"weapon\":[%d,%d],\"lock\":[%d,%.0f,%.0f,%.0f],\"fuel\":[%d,%d],"
		"\"duel\":[%d,%d,%d],\"pm\":[%d,%d,%d,%d],\"damage\":[%d,%d,%d,%d,%d],\"angles\":[%.2f,%.2f,%.2f]",
		ps->fd.forcePowersKnown, ps->fd.forcePowerSelected, ps->fd.forceSide, ps->fd.forceRank, ps->fd.forceRageRecoveryTime,
		ps->zoomMode, ps->zoomFov, ps->zoomTime, ps->zoomLocked ? 1 : 0, ps->weaponTime, ps->weaponChargeTime,
		ps->rocketLockIndex, ps->rocketLockTime, ps->rocketLastValidTime, ps->rocketTargetTime, ps->jetpackFuel, ps->cloakFuel,
		ps->duelInProgress ? 1 : 0, ps->duelIndex, ps->duelTime, ps->pm_type, ps->pm_flags, ps->pm_time, ps->bobCycle,
		ps->damageEvent, ps->damageYaw, ps->damagePitch, ps->damageCount, ps->damageType,
		ps->viewangles[PITCH], ps->viewangles[YAW], ps->viewangles[ROLL] );
	WA_BufPrintf( b, ",\"fallingToDeath\":%d,\"holocronBits\":%d,\"hackingTime\":%d,\"hackingBaseTime\":%d,\"generic1\":%d,\"emplacedIndex\":%d,"
		"\"vehicle\":%d,\"forceHandExtend\":%d,\"saberLockTime\":%d,\"lookTarget\":%d,\"detPack\":%d,\"electrifyTime\":%d,\"commandTime\":%d,"
		"\"jediMaster\":%d}",
		ps->fallingToDeath, ps->holocronBits, ps->hackingTime, ps->hackingBaseTime, ps->generic1, ps->emplacedIndex,
		ps->m_iVehicleNum, ps->forceHandExtend, ps->saberLockTime, ps->hasLookTarget ? ps->lookTarget : -1, ps->hasDetPackPlanted ? 1 : 0,
		ps->electrifyTime, ps->commandTime, ps->isJediMaster ? 1 : 0 );
}

// GET /api/state[?items=<number>][&fx=<number>&fxs=<number>][&p=<client>.<number>-...][&view=<client>][&hud=<client>]:
// everything the page draws, one part after the other
void WA_ApiState( waConnection_t *c, const char *query ) {
	waBuffer_t	b = { 0 };
	char		mapname[MAX_QPATH], hostname[MAX_NETNAME * 2];

	trap->Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	trap->Cvar_VariableStringBuffer( "sv_hostname", hostname, sizeof( hostname ) );

	WA_BufAppend( &b, "{\"map\":", 7 );
	WA_BufJsonString( &b, mapname );
	WA_BufAppend( &b, ",\"host\":", 8 );
	WA_BufJsonString( &b, hostname );
	WA_BufPrintf( &b, ",\"gravity\":%d", (int)g_gravity.value );		// not in JKAdmin
	WA_BufPrintf( &b, ",\"gametype\":%d,\"gametypeName\":\"%s\",\"time\":%d,\"timelimit\":%d,\"fraglimit\":%d,\"capturelimit\":%d,"
		"\"scores\":{\"red\":%d,\"blue\":%d},\"maxClients\":%d",
		level.gametype, BG_GetGametypeString( level.gametype ), level.time - level.startTime, timelimit.integer,
		fraglimit.integer, capturelimit.integer, level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], level.maxclients );

	WA_StatePlayers( &b, query );
	WA_StateNpcs( &b );
	WA_StateItems( &b, query );
	WA_StateModels( &b );
	WA_StateMovers( &b );
	WA_StateShots( &b );
	WA_StateEffects( &b, query );
	WA_StateView( &b, query );
	WA_StateHud( &b, query );
	WA_BufAppend( &b, "}", 1 );

	WA_RespondJson( c, &b );
	WA_BufFree( &b );
}

