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

// cg_spray.c -- sprays, a toy of issue #9 that only players of this tree see, as in the games of Valve: v_spray
// (Controls, Extras) puts the picture of v_sprayImage on the wall the player looks at, upright. A player has one
// spray at a time, a new one takes the place of the old one, and it stays for the level.
//
// v_sprayImage names an emoji of the game (heart is gfx/emoji/heart.png, the ones of gfx/vision/sprays.txt, whose
// line numbers go to the others as TOY_SPRAY), or a picture of the player's own: a file in sprays/ of the home folder
// (png, jpg, tga or an animated gif, with or without its extension) or a chat emoji of the store of the site. Such a
// picture goes to the store of sprays of the site (v_sprays upload, client/cl_models.cpp), and its number goes to the
// others as TOY_SPRAY_STORE; their engine fetches it (v_sprays get) and says when it came (v_sprayArrived).
//
// With an engine that has ext.R_PaintPicture (client/cl_sprays.cpp) every picture is a sheet in a slot of the
// renderer, a slot for each player: an animated one plays its frames. rd-vulkan-vision lays it on as spray paint
// (ext.R_AddPaint, shaders/paint.frag): it comes on in passes of the can, is wet and runs down a wall at first and
// dries, the wall's relief shows through. Other renderers draw a mark of the frame, as bright as the light there. An
// older engine shows only the emoji of the game, as marks of the shaders tools/toys/sprays.py writes.
//
// The others hear of it as TOY_SPRAY or TOY_SPRAY_STORE (game/toys/bg_toys.h, the way a snowball goes, toys/cg_toys.c):
// the point, the angles of the wall and the picture. Each finds the wall there itself. The own spray is said again
// every SPRAY_AGAIN_MSEC, so a player who came later sees it too. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"

#define SPRAY_REACH			128.0f		// how far from the eyes a wall can be sprayed
#define SPRAY_RADIUS		24.0f
#define SPRAY_GAP_MSEC		2000		// between two own sprays at least
#define SPRAY_AGAIN_MSEC	45000		// the own spray is said again this often, for those who came later
#define SPRAY_REVEAL_MSEC	700			// the passes of the can until all of it is on
#define SPRAY_DRY_MSEC		25000		// the paint is wet and runs this long
#define SPRAY_FILE			MAX_QPATH

typedef struct {
	qboolean		active;
	int				picture;		// TOY_SPRAY: the line of gfx/vision/sprays.txt; TOY_SPRAY_STORE: the number in the store
	int				kind;			// TOY_SPRAY, TOY_SPRAY_STORE
	int				shown;			// the picture in the slot: picture + 1 for TOY_SPRAY, -picture for the store, 0 none
	qhandle_t		shader;			// for marks with the slot
	paintPicture_t	frames;
	int				length;			// msec of all frames
	int				time;			// when it was sprayed
	float			seed;
	float			orientation;	// of the mark, so the picture is upright
	vec3_t			origin, normal, up;
} spray_t;

static struct {
	spray_t		sprays[MAX_CLIENTS];
	qhandle_t	shaders[TOY_SPRAY_MOST];	// an older engine: the shaders of tools/toys/sprays.py
	int			lastSpray;
	int			againTime;
	int			paint;						// 1: the engine makes sheets (v_cgamePaint), -1 not, 0 not asked
	int			decals;						// 1: the renderer lays them on as paint, -1 not, 0 not asked
	sfxHandle_t	hiss;
	char		ownFile[SPRAY_FILE];		// the own picture of the player that went to the store
	int			ownNumber;					//	and its number there, 0 none yet
} spray;

void CG_SprayInit( void ) {
	memset( &spray, 0, sizeof( spray ) );
}

static qboolean Spray_Paint( void ) {
	if ( !spray.paint ) {
		char value[8];

		trap->Cvar_VariableStringBuffer( "v_cgamePaint", value, sizeof( value ) );
		spray.paint = ( atoi( value ) && trap->ext.R_PaintPicture ) ? 1 : -1;
		spray.decals = ( spray.paint > 0 && trap->ext.R_AddPaint && trap->ext.R_AddPaint( NULL, 0 ) ) ? 1 : -1;
	}
	return (qboolean)( spray.paint > 0 );
}

/*
=================
Spray_Up

Where the top of the picture is along the surface: up on a wall; on a floor or a ceiling away from the player who
sprayed it (along yaw). And the turn of the mark (CG_ImpactMark) that has it so: the rows of the picture run along
the second axis of the mark, so that axis goes down.
=================
*/
static float Spray_Up( const vec3_t normal, float yaw, vec3_t up ) {
	vec3_t	axis1, down, cross;

	if ( fabsf( normal[2] ) > 0.9f ) {
		vec3_t angles;

		VectorSet( angles, 0.0f, yaw, 0.0f );
		AngleVectors( angles, up, NULL, NULL );
	} else {
		VectorSet( up, 0.0f, 0.0f, 1.0f );
	}
	VectorMA( up, -DotProduct( up, normal ), normal, up );
	VectorNormalize( up );
	VectorScale( up, -1.0f, down );
	PerpendicularVector( axis1, normal );
	CrossProduct( axis1, down, cross );
	return RAD2DEG( atan2f( DotProduct( cross, normal ), DotProduct( axis1, down ) ) );
}

static qboolean Spray_Exists( const char *name ) {
	fileHandle_t	f;
	const int		len = trap->FS_Open( name, &f, FS_READ );

	if ( f )
		trap->FS_Close( f );
	return (qboolean)( len > 0 );
}

// the picture of a spray into the slot of its player, if it is not there yet; the shader of an older engine
static void Spray_Show( int client ) {
	spray_t		*s = &spray.sprays[client];
	const int	want = s->kind == TOY_SPRAY_STORE ? -s->picture : s->picture + 1;
	char		name[SPRAY_FILE];

	if ( s->shown == want ) {
		return;
	}
	if ( !Spray_Paint() ) {
		if ( s->kind == TOY_SPRAY ) {
			if ( !spray.shaders[s->picture] )
				spray.shaders[s->picture] = trap->R_RegisterShader( va( "gfx/vision/spray/%i", s->picture ) );
			s->shader = spray.shaders[s->picture];
			s->shown = want;
			s->frames.frames = 0;
		}
		return;
	}
	if ( s->kind == TOY_SPRAY_STORE ) {
		// the own picture is in the slot already; a picture of the store that is not here yet is fetched, and
		// v_sprayArrived shows it
		if ( client == cg.clientNum )
			return;
		Com_sprintf( name, sizeof( name ), "sprays/store/%i.png", s->picture );
		if ( !Spray_Exists( name ) ) {
			trap->SendConsoleCommand( va( "v_sprays get %i\n", s->picture ) );
			return;
		}
	} else {
		Com_sprintf( name, sizeof( name ), "gfx/emoji/%s", CG_ToyPictureName( s->picture ) );
	}
	s->shader = trap->ext.R_PaintPicture( client, name, &s->frames );
	s->shown = want;
	s->length = 0;
	for ( int i = 0; s->shader && i < s->frames.frames; i++ )
		s->length += s->frames.delays[i];
}

static void Spray_Put( int client, int kind, int picture, const vec3_t origin, const vec3_t normal, float yaw ) {
	spray_t *s = &spray.sprays[client];

	s->active = qtrue;
	s->kind = kind;
	s->picture = picture;
	VectorCopy( origin, s->origin );
	VectorCopy( normal, s->normal );
	s->orientation = Spray_Up( normal, yaw, s->up );
	s->time = cg.time ? cg.time : 1;
	s->seed = Q_flrand( 0.0f, 1.0f );
	if ( !spray.hiss )
		spray.hiss = trap->S_RegisterSound( "sound/vision/toys/spray_can.wav" );
	Spray_Show( client );
}

static void Spray_Send( void ) {
	const spray_t	*s = &spray.sprays[cg.clientNum];
	vec3_t			angles;
	toyThrow_t		toy;
	int				i, pitch, yaw;

	if ( !s->active || ( s->kind == TOY_SPRAY_STORE && !spray.ownNumber ) )
		return;
	vectoangles( s->normal, angles );
	pitch = (int)Com_Clamp( -90.0f, 90.0f, AngleNormalize180( angles[PITCH] ) );
	// (the yaw of a floor or a ceiling says nothing: there it is the one of the player, for the top of the picture)
	yaw = (int)AngleNormalize180( fabsf( s->normal[2] ) > 0.9f ? RAD2DEG( atan2f( s->up[1], s->up[0] ) ) : angles[YAW] );
	toy.kind = s->kind;
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)floorf( s->origin[i] + 0.5f );
	if ( s->kind == TOY_SPRAY_STORE ) {
		toy.velocity[0] = ( ( pitch + 90 + TOY_SPRAY_STEP / 2 ) / TOY_SPRAY_STEP ) * TOY_SPRAY_YAWS
			+ ( yaw + 180 + TOY_SPRAY_STEP / 2 ) / TOY_SPRAY_STEP;
		toy.velocity[1] = spray.ownNumber / TOY_BOARD_SPLIT;
		toy.velocity[2] = spray.ownNumber % TOY_BOARD_SPLIT;
	} else {
		toy.velocity[0] = pitch;
		toy.velocity[1] = yaw;
		toy.velocity[2] = s->picture;
	}
	if ( BG_ToyValid( &toy ) )
		CG_ToySend( &toy );
	spray.againTime = cg.time ? cg.time : 1;
}

/*
=================
Spray_OwnFile

The file of a picture of the player's own that v_sprayImage names: in sprays/ as it is or with the extension of a
picture, or a chat emoji of the store. Into the slot of the player. qfalse: there is none.
=================
*/
static qboolean Spray_OwnFile( const char *name, char *file, int fileSize ) {
	static const char	*extensions[] = { "", ".png", ".gif", ".jpg", ".jpeg", ".tga" };
	spray_t				*s = &spray.sprays[cg.clientNum];
	int					i;

	if ( !name[0] || strchr( name, '/' ) || strchr( name, '\\' ) || strstr( name, ".." ) )
		return qfalse;
	for ( i = 0; i <= (int)ARRAY_LEN( extensions ); i++ ) {
		if ( i < (int)ARRAY_LEN( extensions ) )
			Com_sprintf( file, fileSize, "sprays/%s%s", name, extensions[i] );
		else
			Com_sprintf( file, fileSize, "gfx/emoji/%s.png", name );
		if ( ( i == 0 && !strchr( name, '.' ) ) || !Spray_Exists( file ) )
			continue;
		s->shader = trap->ext.R_PaintPicture( cg.clientNum, file, &s->frames );
		if ( !s->shader )
			continue;
		s->length = 0;
		for ( int k = 0; k < s->frames.frames; k++ )
			s->length += s->frames.delays[k];
		return qtrue;
	}
	return qfalse;
}

// v_spray: the picture of v_sprayImage on the wall the player looks at
void CG_Spray_f( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	spray_t				*s = &spray.sprays[cg.clientNum];
	vec3_t				start, end, forward;
	trace_t				tr;
	char				file[SPRAY_FILE];
	int					picture, kind = TOY_SPRAY;

	if ( !CG_ToyCanUse() || ( spray.lastSpray && cg.time - spray.lastSpray < SPRAY_GAP_MSEC && cg.time >= spray.lastSpray ) )
		return;
	VectorCopy( ps->origin, start );
	start[2] += ps->viewheight;
	AngleVectors( cg.refdef.viewangles, forward, NULL, NULL );
	VectorMA( start, SPRAY_REACH, forward, end );
	CG_Trace( &tr, start, NULL, NULL, end, cg.clientNum, MASK_SOLID );
	if ( tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) )
		return;

	picture = CG_ToyPicture( v_sprayImage.string );
	if ( picture < 0 ) {
		if ( !Spray_Paint() || !Spray_OwnFile( v_sprayImage.string, file, sizeof( file ) ) ) {
			Com_Printf( "v_sprayImage names no emoji and no picture in sprays/ (png, jpg, tga or gif)\n" );
			return;
		}
		kind = TOY_SPRAY_STORE;
		// a new picture goes to the store; until its number comes the others do not see it
		if ( Q_stricmp( file, spray.ownFile ) ) {
			Q_strncpyz( spray.ownFile, file, sizeof( spray.ownFile ) );
			spray.ownNumber = 0;
			trap->SendConsoleCommand( va( "v_sprays upload \"%s\"\n", file ) );
		}
		picture = spray.ownNumber;
		s->shown = -picture;
	}
	spray.lastSpray = cg.time ? cg.time : 1;
	Spray_Put( cg.clientNum, kind, picture, tr.endpos, tr.plane.normal, cg.refdef.viewangles[YAW] );
	trap->S_StartSound( tr.endpos, cg.clientNum, CHAN_AUTO, spray.hiss );
	Spray_Send();
}

// v_sprayPicture <number>: the engine says the number the store gave the own picture, and the others hear of it
void CG_SprayPicture_f( void ) {
	spray_t		*s;
	const int	number = atoi( CG_Argv( 1 ) );

	if ( number <= 0 || number >= TOY_BOARD_SPLIT * TOY_BOARD_SPLIT || cg.clientNum < 0 || cg.clientNum >= MAX_CLIENTS )
		return;
	spray.ownNumber = number;
	s = &spray.sprays[cg.clientNum];
	if ( s->active && s->kind == TOY_SPRAY_STORE ) {
		s->picture = number;
		s->shown = -number;
		Spray_Send();
	}
}

// v_sprayArrived <number>: a picture of the store came, the sprays that wait for it show it
void CG_SprayArrived_f( void ) {
	const int number = atoi( CG_Argv( 1 ) );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		const spray_t *s = &spray.sprays[i];

		if ( s->active && s->kind == TOY_SPRAY_STORE && s->picture == number && i != cg.clientNum )
			Spray_Show( i );
	}
}

/*
=================
CG_SprayHeard

A spray of somebody else, from the game module or the masterserver. The wall is looked for at the point, so the
picture lies on it even where the angles came in steps.
=================
*/
void CG_SprayHeard( int client, int kind, const vec3_t origin, const vec3_t velocity ) {
	vec3_t		angles, normal, start, end;
	trace_t		tr;
	int			picture;
	float		yaw;
	qboolean	moved;

	if ( kind == TOY_SPRAY_STORE ) {
		const int a = (int)velocity[0];

		VectorSet( angles, (float)( ( a / TOY_SPRAY_YAWS ) * TOY_SPRAY_STEP - 90 ), (float)( ( a % TOY_SPRAY_YAWS ) * TOY_SPRAY_STEP - 180 ), 0.0f );
		picture = (int)velocity[1] * TOY_BOARD_SPLIT + (int)velocity[2];
	} else {
		VectorSet( angles, velocity[0], velocity[1], 0.0f );
		picture = (int)velocity[2];
		if ( picture < 0 || picture >= CG_ToyPictures() )
			return;
	}
	yaw = angles[YAW];
	AngleVectors( angles, normal, NULL, NULL );
	VectorMA( origin, 8.0f, normal, start );
	VectorMA( origin, -16.0f, normal, end );
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction < 1.0f && !tr.startsolid && tr.entityNum == ENTITYNUM_WORLD && DotProduct( tr.plane.normal, normal ) > 0.8f )
		VectorCopy( tr.plane.normal, normal );
	moved = (qboolean)( !spray.sprays[client].active || !VectorCompare( spray.sprays[client].origin, origin ) );
	if ( !moved && spray.sprays[client].kind == kind && spray.sprays[client].picture == picture )
		return;
	Spray_Put( client, kind, picture, origin, normal, yaw );
	if ( moved )
		trap->S_StartSound( (float *)origin, client, CHAN_AUTO, spray.hiss );
}

// once a frame: the own spray is said again now and then, a spray of a player who left goes
void CG_SprayFrame( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( spray.sprays[i].active && i != cg.clientNum && !cgs.clientinfo[i].infoValid ) {
			spray.sprays[i].active = qfalse;
			spray.sprays[i].shown = 0;
		}
	}
	if ( cg.clientNum >= 0 && cg.clientNum < MAX_CLIENTS && spray.sprays[cg.clientNum].active
		&& ( cg.time - spray.againTime >= SPRAY_AGAIN_MSEC || cg.time < spray.againTime ) ) {
		Spray_Send();
	}
}

// the part of the sheet of a spray shown now: its frames play on from when it was sprayed
static void Spray_Frame( const spray_t *s, float *part ) {
	const paintPicture_t	*f = &s->frames;
	int						n = 0;

	if ( f->frames <= 0 || f->sheet <= 0 ) {
		VectorSet4( part, 0.0f, 0.0f, 1.0f, 1.0f );
		return;
	}
	if ( f->frames > 1 && s->length > 0 ) {
		int at = ( cg.time - s->time ) % s->length;

		while ( at < 0 )
			at += s->length;
		for ( n = 0; n < f->frames - 1 && at >= f->delays[n]; n++ )
			at -= f->delays[n];
	}
	part[0] = (float)( ( n % f->columns ) * f->cell ) / (float)f->sheet;
	part[1] = (float)( ( n / f->columns ) * f->cell ) / (float)f->sheet;
	part[2] = part[3] = (float)f->cell / (float)f->sheet;
}

// While the scene is built: every spray, as paint, or a mark drawn anew each frame as bright as the light where it is
void CG_SprayScene( void ) {
	paintDecal_t	decals[MAX_CLIENTS];
	int				i, count = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		spray_t		*s = &spray.sprays[i];
		vec3_t		light;
		float		part[4];

		if ( !s->active || ( i != cg.clientNum && !v_toys.integer ) )
			continue;
		if ( s->shown != ( s->kind == TOY_SPRAY_STORE ? -s->picture : s->picture + 1 ) )
			Spray_Show( i );
		if ( !s->shader )
			continue;
		Spray_Frame( s, part );
		if ( spray.decals > 0 ) {
			paintDecal_t *d = &decals[count++];

			VectorCopy( s->origin, d->origin );
			VectorCopy( s->normal, d->normal );
			VectorCopy( s->up, d->up );
			d->radius = SPRAY_RADIUS;
			d->reveal = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - s->time ) / SPRAY_REVEAL_MSEC );
			d->fresh = 1.0f - Com_Clamp( 0.0f, 1.0f, (float)( cg.time - s->time ) / SPRAY_DRY_MSEC );
			d->seed = s->seed;
			d->slot = i;
			VectorCopy4( part, d->frame );
			continue;
		}
		CG_ToyLight( s->origin, light );
		CG_ImpactMarkFrame( s->shader, s->origin, s->normal, s->orientation, light, SPRAY_RADIUS, part );
	}
	if ( count )
		trap->ext.R_AddPaint( decals, count );
}
