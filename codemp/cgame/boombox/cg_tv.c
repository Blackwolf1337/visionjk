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

// cg_tv.c -- the TV of the boombox (issue #9): a box of the kind BOX_KIND_TV is an Imperial monitor of the game
// (models/map_objects/imperial/monitor.md3) whose middle screen shows the video of the song it plays, in step with
// the song, and whose light falls on the room around it in the colours of the picture. The sound is the song of
// the box as cg_boombox.c plays any song. The picture is fetched by the engine from the TV of the masterserver
// (client/cl_video.cpp) into one of its first TV_STREAMS streams: the nearest TVs that play get one,
// a TV further away shows its screen dark. Not from JA+, see docs/japlus/boombox.md, "The TV".
//
// A picture starts TV_LEAD_SEC ahead of the song, so the first second of it is there before the song gets to it.
// When the song jumps (a seek), the picture falls behind (the line is slow), or nothing comes, the stream is opened
// again at where the song is then.

#include "cgame/cg_local.h"
#include "cgame/boombox/cg_boombox.h"

#define TV_STREAMS			4			// the streams of the engine 0 to 3 are the TVs' (the drawing boards have the others)
#define TV_MODEL			"models/map_objects/imperial/monitor.md3"
#define TV_SCALE			1.25f		// the monitor of the game is drawn this much larger
#define TV_LIFT				27.0f		// the lowest point of the model is this far below its origin (unscaled)
#define TV_SCREEN_X			7.8f		// the middle of the middle screen, from the origin of the model (unscaled)
#define TV_SCREEN_Z			2.7f
#define TV_HALF_DEPTH		21.0f		// its size for the pointer, from the middle of its foot (unscaled)
#define TV_HALF_WIDTH		56.0f
#define TV_HEIGHT			53.0f
#define TV_LEAD_SEC			2
#define TV_LATE_MSEC		3000		// a picture this far behind the song is opened again further in
#define TV_STALL_MSEC		15000		// a stream that shows nothing after this is opened again
#define TV_RETRY_MSEC		10000		// after a stream failed
#define TV_SEE				2048.0f		// a TV further from the eye gets no stream
#define TV_LIGHT_AHEAD		40.0f		// where its light is, in front of the screen

typedef struct {
	int			key;				// the box
	vec3_t		origin;
	float		yaw;
	char		video[12];
	int			msec;				// how far its song is
	qboolean	playing;
} tvShown_t;

typedef struct {
	int			key;				// the box it shows, -1: free
	char		video[12];
	int			second;				// it was opened at
	int			openedAt;			// cg.time
	int			failedAt;
} tvStream_t;

static struct {
	qboolean	registered;
	qboolean	has;				// the engine has the videos (v_cgameVideo)
	qhandle_t	model;
	qhandle_t	skins[TV_STREAMS], skinOff;
	tvShown_t	shown[BOX_MAX + 1];
	int			numShown;
	tvStream_t	streams[TV_STREAMS];
} tv;

void CG_TvInit( void ) {
	int i;

	memset( &tv, 0, sizeof( tv ) );
	for ( i = 0; i < TV_STREAMS; i++ )
		tv.streams[i].key = -1;
}

static void Tv_Register( void ) {
	char	value[8];
	int		i;

	if ( tv.registered )
		return;
	tv.registered = qtrue;
	trap->Cvar_VariableStringBuffer( "v_cgameVideo", value, sizeof( value ) );
	tv.has = (qboolean)( atoi( value ) == 1 );
	tv.model = trap->R_RegisterModel( TV_MODEL );
	for ( i = 0; i < TV_STREAMS; i++ )
		tv.skins[i] = trap->R_RegisterSkin( va( "models/vision/tv/screen_%i.skin", i ) );
	tv.skinOff = trap->R_RegisterSkin( "models/vision/tv/screen_off.skin" );
}

// A TV of a box in this frame. Called before CG_TvScene, by the boombox for every TV it draws.
void CG_TvAdd( int key, const vec3_t origin, float yaw, const char *video, int msec, qboolean playing ) {
	tvShown_t *s;

	if ( tv.numShown >= (int)ARRAY_LEN( tv.shown ) )
		return;
	s = &tv.shown[tv.numShown++];
	s->key = key;
	VectorCopy( origin, s->origin );
	s->yaw = yaw;
	Q_strncpyz( s->video, video ? video : "", sizeof( s->video ) );
	s->msec = msec;
	s->playing = (qboolean)( playing && s->video[0] );
}

static int Tv_StreamOf( int key ) {
	int i;

	for ( i = 0; i < TV_STREAMS; i++ ) {
		if ( tv.streams[i].key == key )
			return i;
	}
	return -1;
}

static void Tv_Open( int stream, const tvShown_t *s ) {
	tvStream_t *t = &tv.streams[stream];

	t->key = s->key;
	Q_strncpyz( t->video, s->video, sizeof( t->video ) );
	t->second = Q_max( 0, s->msec / 1000 + TV_LEAD_SEC );
	t->openedAt = cg.time;
	t->failedAt = 0;
	if ( !trap->ext.Video_Open( stream, t->video, t->second ) )
		t->failedAt = cg.time;		// (no thread free now: again after TV_RETRY_MSEC)
}

static void Tv_Free( int stream ) {
	trap->ext.Video_Reset( stream );
	memset( &tv.streams[stream], 0, sizeof( tv.streams[stream] ) );
	tv.streams[stream].key = -1;
}

// the stream of a TV plays on to where its song is, opened again where it has to be. qtrue: it shows a frame.
static qboolean Tv_Play( int stream, const tvShown_t *s ) {
	tvStream_t	*t = &tv.streams[stream];
	const int	local = s->msec - t->second * 1000;
	int			state, frame;

	if ( strcmp( t->video, s->video ) || local < -( TV_LEAD_SEC + 1 ) * 1000 ) {
		Tv_Open( stream, s );
		return qfalse;
	}
	state = trap->ext.Video_State( stream );
	if ( state == VIDEO_FAILED || ( t->failedAt && state == VIDEO_NONE ) ) {
		if ( !t->failedAt )
			t->failedAt = cg.time;
		if ( cg.time - t->failedAt > TV_RETRY_MSEC )
			Tv_Open( stream, s );
		return qfalse;
	}
	if ( state == VIDEO_FETCHING && cg.time - t->openedAt > TV_STALL_MSEC ) {
		Tv_Open( stream, s );
		return qfalse;
	}
	frame = trap->ext.Video_Advance( stream, local );
	if ( state == VIDEO_PLAYING && frame >= 0 && local - frame > TV_LATE_MSEC ) {
		Tv_Open( stream, s );
		return qfalse;
	}
	return (qboolean)( frame >= 0 && local >= 0 );
}

// The model of a TV that stands at origin (on the floor) turned to yaw, as the boombox draws and picks it.
// qfalse: there is no model.
qboolean CG_TvBody( const vec3_t origin, float yaw, refEntity_t *ent ) {
	vec3_t angles;

	Tv_Register();
	if ( !tv.model )
		return qfalse;
	memset( ent, 0, sizeof( *ent ) );
	ent->reType = RT_MODEL;
	ent->hModel = tv.model;
	VectorSet( angles, 0.0f, yaw, 0.0f );
	AnglesToAxis( angles, ent->axis );
	VectorScale( ent->axis[0], TV_SCALE, ent->axis[0] );
	VectorScale( ent->axis[1], TV_SCALE, ent->axis[1] );
	VectorScale( ent->axis[2], TV_SCALE, ent->axis[2] );
	ent->nonNormalizedAxes = qtrue;
	VectorCopy( origin, ent->origin );
	ent->origin[2] += TV_LIFT * TV_SCALE;
	VectorCopy( ent->origin, ent->oldorigin );
	VectorCopy( ent->origin, ent->lightingOrigin );
	ent->renderfx = RF_LIGHTING_ORIGIN;
	ent->shaderRGBA[0] = ent->shaderRGBA[1] = ent->shaderRGBA[2] = ent->shaderRGBA[3] = 255;
	return qtrue;
}

void CG_TvSize( float *halfDepth, float *halfWidth, float *height ) {
	*halfDepth = TV_HALF_DEPTH * TV_SCALE;
	*halfWidth = TV_HALF_WIDTH * TV_SCALE;
	*height = TV_HEIGHT * TV_SCALE;
}

static void Tv_Draw( const tvShown_t *s, int stream, qboolean picture ) {
	refEntity_t ent;

	if ( !CG_TvBody( s->origin, s->yaw, &ent ) )
		return;
	ent.customSkin = ( picture && stream >= 0 ) ? tv.skins[stream] : tv.skinOff;
	trap->R_AddRefEntityToScene( &ent );

	if ( picture && stream >= 0 ) {
		vec3_t	color, at;
		float	bright;

		trap->ext.Video_Color( stream, color );
		bright = Q_max( color[0], Q_max( color[1], color[2] ) );
		VectorMA( ent.origin, TV_SCREEN_X + TV_LIGHT_AHEAD / TV_SCALE, ent.axis[0], at );
		at[2] += TV_SCREEN_Z * TV_SCALE;
		// (the hue of the picture at full strength, and as much light as the picture is bright)
		if ( bright > 0.02f )
			VectorScale( color, 1.0f / bright, color );
		trap->R_AddLightToScene( at, 90.0f + 230.0f * bright, color[0], color[1], color[2] );
	}
}

/*
=================
CG_TvScene

Once a frame while the scene is built, after the boombox said which TVs there are: the nearest that play get a
stream, every TV is drawn, the screens that show a picture light the room.
=================
*/
void CG_TvScene( void ) {
	qboolean	wanted[TV_STREAMS] = { qfalse };
	int			order[BOX_MAX + 1], count = 0, i, k;

	Tv_Register();
	if ( !tv.has ) {
		for ( i = 0; i < tv.numShown; i++ )
			Tv_Draw( &tv.shown[i], -1, qfalse );
		tv.numShown = 0;
		return;
	}
	// the TVs that play, nearest first
	for ( i = 0; i < tv.numShown; i++ ) {
		const float d = Distance( tv.shown[i].origin, cg.refdef.vieworg );

		if ( !tv.shown[i].playing || d > TV_SEE )
			continue;
		for ( k = count; k > 0 && Distance( tv.shown[order[k - 1]].origin, cg.refdef.vieworg ) > d; k-- )
			order[k] = order[k - 1];
		order[k] = i;
		count++;
	}
	count = Q_min( count, TV_STREAMS );
	for ( i = 0; i < count; i++ ) {
		const int stream = Tv_StreamOf( tv.shown[order[i]].key );

		if ( stream >= 0 )
			wanted[stream] = qtrue;
	}
	for ( i = 0; i < TV_STREAMS; i++ ) {
		if ( !wanted[i] && tv.streams[i].key >= 0 )
			Tv_Free( i );
	}
	for ( i = 0; i < count; i++ ) {
		const tvShown_t	*s = &tv.shown[order[i]];
		int				stream = Tv_StreamOf( s->key );

		if ( stream < 0 ) {
			stream = Tv_StreamOf( -1 );
			if ( stream < 0 )
				continue;
			Tv_Open( stream, s );
		}
		wanted[stream] = Tv_Play( stream, s );
		Tv_Draw( s, stream, wanted[stream] );
		tv.shown[order[i]].key = -2;		// (drawn)
	}
	for ( i = 0; i < tv.numShown; i++ ) {
		if ( tv.shown[i].key != -2 )
			Tv_Draw( &tv.shown[i], -1, qfalse );
	}
	tv.numShown = 0;
}
