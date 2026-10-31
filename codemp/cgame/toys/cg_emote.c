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

// cg_emote.c -- interactive emoji, a toy of issue #9 that only players of this tree see: v_emote (Controls, Extras)
// sends the emoji of v_emoteImage (or the one named after the command) to the player under the crosshair. It flies
// over in an arc from the eyes of the sender to the head of the other one, follows him, and pops above his head:
// it grows, bobs, and small copies of it burst out. The pop sounds, and an emoji of a mood (laughing, angry, ...)
// lets the voice of the sender's model say it (a gloat for a laugh, a victory for a thumbs up), as far as the voice
// has such a sound. Who is sent one sees it pop on his screen in first person.
//
// The others hear of it as TOY_EMOJI (game/toys/bg_toys.h, the way a snowball goes, toys/cg_toys.c): the point it
// leaves, the client it flies to, the number of the picture in gfx/vision/sprays.txt. Every cgame flies it on its own.
// The sounds are made by tools/toys/sounds.py, the sprite shaders gfx/vision/emote/<n> by tools/toys/sprays.py.
// Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"

#define EMOTE_FLYING		16			// emoji in the air or popping at once
#define EMOTE_REACH			2048.0f		// how far a player can be to be sent one
#define EMOTE_GAP_MSEC		1000		// between two own emoji at least
#define EMOTE_SPEED			700.0f		// units a second along the line, the arc comes on top
#define EMOTE_POP_MSEC		1800
#define EMOTE_BURST_MSEC	650			// the small copies fly out so long
#define EMOTE_HEAD			36.0f		// above the origin of a player: his head
#define EMOTE_ABOVE			22.0f		// the pop above the head
#define EMOTE_SIZE			9.0f		// radius of the emoji in the air
#define EMOTE_POP_SIZE		15.0f
#define EMOTE_BITS			10
#define EMOTE_TRAIL			6			// fading copies behind it in the air
#define EMOTE_OWN_SCALE		0.55f		// the pop over the own head, so it hides less of the view

typedef enum {
	MOOD_NONE,
	MOOD_LAUGH,
	MOOD_VICTORY,
	MOOD_ANGER,
	MOOD_CONFUSE,
	MOOD_GASP,
	MOOD_TAUNT
} emoteMood_t;

// the emoji that say something, by the names of gfx/emoji
static const struct {
	const char	*name;
	emoteMood_t	mood;
} emoteMoods[] = {
	{ "joy", MOOD_LAUGH }, { "laughing", MOOD_LAUGH }, { "rofl", MOOD_LAUGH }, { "grinning", MOOD_LAUGH },
	{ "smile", MOOD_LAUGH }, { "sweatsmile", MOOD_LAUGH }, { "`kekw`", MOOD_LAUGH }, { "`lul`", MOOD_LAUGH },
	{ "`lulw`", MOOD_LAUGH }, { "`omegalul`", MOOD_LAUGH }, { "`omegalaugh`", MOOD_LAUGH },
	{ "`pepelaugh`", MOOD_LAUGH }, { "`elegiggle`", MOOD_LAUGH }, { "`lol`", MOOD_LAUGH }, { "`hahaa`", MOOD_LAUGH },
	{ "thumbsup", MOOD_VICTORY }, { "clap", MOOD_VICTORY }, { "trophy", MOOD_VICTORY }, { "muscle", MOOD_VICTORY },
	{ "100", MOOD_VICTORY }, { "fire", MOOD_VICTORY }, { "crown", MOOD_VICTORY }, { "party", MOOD_VICTORY },
	{ "star", MOOD_VICTORY }, { "sunglasses", MOOD_VICTORY }, { "`pogchamp`", MOOD_VICTORY },
	{ "`pogu`", MOOD_VICTORY }, { "`paggers`", MOOD_VICTORY }, { "`ez`", MOOD_VICTORY },
	{ "`voteyea`", MOOD_VICTORY },
	{ "angry", MOOD_ANGER }, { "triumph", MOOD_ANGER }, { "imp", MOOD_ANGER }, { "thumbsdown", MOOD_ANGER },
	{ "`swiftrage`", MOOD_ANGER }, { "`babyrage`", MOOD_ANGER }, { "`feelsrageman`", MOOD_ANGER },
	{ "`dansgame`", MOOD_ANGER }, { "`votenay`", MOOD_ANGER }, { "`middlefinger`", MOOD_ANGER },
	{ "thinking", MOOD_CONFUSE }, { "raisedeyebrow", MOOD_CONFUSE }, { "monocle", MOOD_CONFUSE },
	{ "upsidedown", MOOD_CONFUSE }, { "neutral", MOOD_CONFUSE }, { "`thonk`", MOOD_CONFUSE },
	{ "`monkahmm`", MOOD_CONFUSE }, { "`waitwhat`", MOOD_CONFUSE }, { "`rlytho`", MOOD_CONFUSE },
	{ "`thinkaboutit`", MOOD_CONFUSE },
	{ "openmouth", MOOD_GASP }, { "flushed", MOOD_GASP }, { "worried", MOOD_GASP }, { "grimacing", MOOD_GASP },
	{ "skull", MOOD_GASP }, { "ghost", MOOD_GASP }, { "boom", MOOD_GASP }, { "`wutface`", MOOD_GASP },
	{ "`monkas`", MOOD_GASP }, { "`monkaw`", MOOD_GASP }, { "`peepowow`", MOOD_GASP },
	{ "`notlikethis`", MOOD_GASP },
	{ "tongue", MOOD_TAUNT }, { "stuckouttongue", MOOD_TAUNT }, { "wink", MOOD_TAUNT }, { "clown", MOOD_TAUNT },
	{ "poop", MOOD_TAUNT }, { "`kappa`", MOOD_TAUNT }, { "`keepo`", MOOD_TAUNT }, { "`jebaited`", MOOD_TAUNT },
	{ "`trihard`", MOOD_TAUNT }, { "`smorc`", MOOD_TAUNT },
};

// what the voice says for a mood: the first group the voice has a sound of, one of it at random
static const char *const emoteVoices[][3][3] = {
	{ { NULL } },
	{ { "gloat1", "gloat2", "gloat3" }, { "victory1", "victory2", "victory3" }, { "taunt1", "taunt2", "taunt" } },
	{ { "victory1", "victory2", "victory3" }, { "taunt1", "taunt2", "taunt" } },
	{ { "anger1", "anger2", "anger3" }, { "taunt1", "taunt2", "taunt" } },
	{ { "confuse1", "confuse2", "confuse3" }, { "gasp" } },
	{ { "gasp" }, { "confuse1", "confuse2", "confuse3" } },
	{ { "taunt1", "taunt2", "taunt3" }, { "taunt" } },
};

typedef struct {
	qboolean	active;
	int			from, to;			// client numbers
	int			picture;			// line of gfx/vision/sprays.txt
	emoteMood_t	mood;
	int			start;				// cg.time it left
	int			flight;				// msec it flies
	int			popTime;			// cg.time it arrived, 0: in the air
	vec3_t		origin;				// where it left
	vec3_t		head;				// of the one it flies to, kept while he is out of sight
	float		arc;				// height of the arc
	float		sway;				// a random phase of the rocking
	vec3_t		bits[EMOTE_BITS];	// the ways of the small copies out of the pop
} emote_t;

static struct {
	emote_t		emotes[EMOTE_FLYING];
	qhandle_t	shaders[TOY_SPRAY_MOST];
	sfxHandle_t	sendSound, popSound;
	int			lastSend;
} emote;

void CG_EmoteInit( void ) {
	memset( &emote, 0, sizeof( emote ) );
}

static qboolean Emote_FirstPerson( void ) {
	return (qboolean)( !cg.renderingThirdPerson && cg.snap && cg.snap->ps.clientNum == cg.clientNum );
}

// the head of a player, qfalse when he is not in the snapshot
static qboolean Emote_Head( int client, vec3_t out ) {
	if ( client == cg.clientNum ) {
		VectorCopy( cg.predictedPlayerState.origin, out );
	} else {
		const centity_t *cent = &cg_entities[client];

		if ( !cent->currentValid || ( cent->currentState.eFlags & EF_DEAD ) )
			return qfalse;
		VectorCopy( cent->lerpOrigin, out );
	}
	out[2] += EMOTE_HEAD;
	return qtrue;
}

static emoteMood_t Emote_Mood( int picture ) {
	char	name[TOY_PICTURE_NAME];
	int		i;

	COM_StripExtension( CG_ToyPictureName( picture ), name, sizeof( name ) );
	for ( i = 0; i < (int)ARRAY_LEN( emoteMoods ); i++ ) {
		if ( !Q_stricmp( emoteMoods[i].name, name ) )
			return emoteMoods[i].mood;
	}
	return MOOD_NONE;
}

static sfxHandle_t Emote_Voice( int client, emoteMood_t mood ) {
	int g, i;

	for ( g = 0; g < 3 && mood != MOOD_NONE; g++ ) {
		sfxHandle_t	found[3];
		int			count = 0;

		for ( i = 0; i < 3 && emoteVoices[mood][g][i]; i++ ) {
			const sfxHandle_t sfx = CG_CharacterSound( client, emoteVoices[mood][g][i] );

			if ( sfx )
				found[count++] = sfx;
		}
		if ( count )
			return found[Q_irand( 0, count - 1 )];
	}
	return 0;
}

static void Emote_Spawn( int from, int to, int picture, const vec3_t origin ) {
	emote_t	*e = NULL;
	vec3_t	head;
	float	dist;
	int		i, oldest = 0;

	if ( !Emote_Head( to, head ) )
		return;
	for ( i = 0; i < EMOTE_FLYING && !e; i++ ) {
		if ( !emote.emotes[i].active )
			e = &emote.emotes[i];
		else if ( emote.emotes[i].start < emote.emotes[oldest].start )
			oldest = i;
	}
	if ( !e )
		e = &emote.emotes[oldest];
	memset( e, 0, sizeof( *e ) );
	e->active = qtrue;
	e->from = from;
	e->to = to;
	e->picture = picture;
	e->mood = Emote_Mood( picture );
	e->start = cg.time;
	VectorCopy( origin, e->origin );
	VectorCopy( head, e->head );
	dist = Distance( origin, head );
	e->flight = (int)( 1000.0f * Com_Clamp( 0.35f, 2.2f, 0.25f + dist / EMOTE_SPEED ) );
	e->arc = Com_Clamp( 16.0f, 120.0f, dist * 0.3f );
	e->sway = Q_flrand( 0.0f, 2.0f * M_PI );
	for ( i = 0; i < EMOTE_BITS; i++ ) {
		VectorSet( e->bits[i], Q_flrand( -1.0f, 1.0f ), Q_flrand( -1.0f, 1.0f ), Q_flrand( -0.2f, 1.0f ) );
		VectorNormalize( e->bits[i] );
	}
	if ( !emote.shaders[picture] )
		emote.shaders[picture] = trap->R_RegisterShader( va( "gfx/vision/emote/%i", picture ) );
	if ( !emote.sendSound ) {
		emote.sendSound = trap->S_RegisterSound( "sound/vision/toys/emote_send.wav" );
		emote.popSound = trap->S_RegisterSound( "sound/vision/toys/emote_pop.wav" );
	}
	trap->S_StartSound( e->origin, from, CHAN_AUTO, emote.sendSound );
}

// the player nearest to the crosshair that can be seen, -1: none
static int Emote_Target( const vec3_t eyes, const vec3_t forward ) {
	float	best = 1.0f;
	int		i, target = -1;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		vec3_t	head, dir;
		float	dist, along, off;
		trace_t	tr;

		if ( i == cg.clientNum || !cgs.clientinfo[i].infoValid || !Emote_Head( i, head ) )
			continue;
		head[2] -= 12.0f;		// the middle of the upper body
		VectorSubtract( head, eyes, dir );
		dist = VectorNormalize( dir );
		along = DotProduct( dir, forward );
		if ( dist > EMOTE_REACH || along <= 0.0f )
			continue;
		// how far off the line of sight, against how far off it may be at that distance
		off = dist * sqrtf( Q_max( 0.0f, 1.0f - along * along ) ) / Q_max( 28.0f, dist * 0.12f );
		if ( off >= best )
			continue;
		CG_Trace( &tr, eyes, NULL, NULL, head, cg.clientNum, MASK_SOLID );
		if ( tr.fraction < 1.0f && tr.entityNum != i )
			continue;
		best = off;
		target = i;
	}
	return target;
}

// v_emote [emoji]: the emoji (or the one of v_emoteImage) to the player under the crosshair
void CG_Emote_f( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	vec3_t				eyes, forward;
	toyThrow_t			toy;
	int					picture, target, i;

	if ( !CG_ToyCanUse() || ( emote.lastSend && cg.time - emote.lastSend < EMOTE_GAP_MSEC && cg.time >= emote.lastSend ) )
		return;
	picture = CG_ToyPicture( trap->Cmd_Argc() > 1 ? CG_Argv( 1 ) : v_emoteImage.string );
	if ( picture < 0 ) {
		Com_Printf( "v_emoteImage names no emoji of the chat (the name without colons, as laughing)\n" );
		return;
	}
	VectorCopy( ps->origin, eyes );
	eyes[2] += ps->viewheight;
	AngleVectors( cg.refdef.viewangles, forward, NULL, NULL );
	target = Emote_Target( eyes, forward );
	if ( target < 0 ) {
		Com_Printf( "No player under the crosshair to send an emoji to\n" );
		return;
	}
	emote.lastSend = cg.time ? cg.time : 1;
	toy.kind = TOY_EMOJI;
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)floorf( eyes[i] + 0.5f );
	toy.velocity[0] = target;
	toy.velocity[1] = picture;
	toy.velocity[2] = 0;
	if ( !BG_ToyValid( &toy ) )
		return;
	CG_ToySend( &toy );
	// in first person it leaves a little in front of the eyes, not out of the camera
	if ( Emote_FirstPerson() ) {
		VectorMA( eyes, 40.0f, forward, eyes );
		eyes[2] -= 10.0f;
	}
	Emote_Spawn( cg.clientNum, target, picture, eyes );
}

// an emoji of somebody else, from the game module or the masterserver
void CG_EmoteHeard( int client, const vec3_t origin, const vec3_t velocity ) {
	const int to = (int)velocity[0], picture = (int)velocity[1];

	if ( to == client || to < 0 || to >= MAX_CLIENTS || !cgs.clientinfo[to].infoValid || picture >= CG_ToyPictures() )
		return;
	Emote_Spawn( client, to, picture, origin );
}

static void Emote_Sprite( qhandle_t shader, const vec3_t origin, float radius, float rotation, float alpha ) {
	refEntity_t ent;

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	VectorCopy( origin, ent.origin );
	ent.radius = radius;
	ent.rotation = rotation;
	ent.customShader = shader;
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = 255;
	ent.shaderRGBA[3] = (byte)( Com_Clamp( 0.0f, 1.0f, alpha ) * 255.0f );
	trap->R_AddRefEntityToScene( &ent );
}

// where it is in the air at a fraction of its flight
static void Emote_Along( const emote_t *e, float f, vec3_t out ) {
	vec3_t way;

	f = Com_Clamp( 0.0f, 1.0f, f );
	f = f * f * ( 3.0f - 2.0f * f );
	VectorSubtract( e->head, e->origin, way );
	VectorMA( e->origin, f, way, out );
	out[2] += e->arc * 4.0f * f * ( 1.0f - f );
}

// how big the pop is at an age: it grows past its size, settles, and swells a little while it fades
static float Emote_PopScale( int age ) {
	if ( age < 120 )
		return 1.35f * sinf( (float)age / 120.0f * ( M_PI * 0.5f ) );
	if ( age < 300 )
		return 1.35f - 0.35f * (float)( age - 120 ) / 180.0f;
	if ( age > EMOTE_POP_MSEC - 450 )
		return 1.0f + 0.15f * (float)( age - ( EMOTE_POP_MSEC - 450 ) ) / 450.0f;
	return 1.0f;
}

static float Emote_PopAlpha( int age ) {
	return age > EMOTE_POP_MSEC - 450 ? 1.0f - (float)( age - ( EMOTE_POP_MSEC - 450 ) ) / 450.0f : 1.0f;
}

/*
=================
CG_EmoteScene

While the scene of the frame is built: the emoji fly on, pop when they arrive, and are drawn
=================
*/
void CG_EmoteScene( void ) {
	int i, k;

	for ( i = 0; i < EMOTE_FLYING; i++ ) {
		emote_t		*e = &emote.emotes[i];
		const int	age = cg.time - e->start;
		vec3_t		point;

		if ( !e->active )
			continue;
		if ( age < 0 || ( e->popTime && cg.time - e->popTime >= EMOTE_POP_MSEC ) || !cgs.clientinfo[e->to].infoValid ) {
			e->active = qfalse;
			continue;
		}
		Emote_Head( e->to, e->head );
		if ( !e->popTime ) {
			const float f = (float)age / e->flight;

			if ( f < 1.0f ) {
				const float	grow = 0.35f + 0.65f * Q_min( 1.0f, f * 4.0f );
				const float	rock = 14.0f * sinf( cg.time * 0.012f + e->sway );
				// one that flies at the own eyes fades before it fills the screen
				const qboolean	atEyes = (qboolean)( e->to == cg.clientNum && Emote_FirstPerson() );

				for ( k = EMOTE_TRAIL; k >= 0; k-- ) {
					const float	back = f - k * 0.03f;
					float		alpha = k ? 0.4f * ( 1.0f - (float)k / ( EMOTE_TRAIL + 1 ) ) : 1.0f;

					if ( back < 0.0f )
						continue;
					Emote_Along( e, back, point );
					if ( atEyes )
						alpha *= Com_Clamp( 0.0f, 1.0f, ( Distance( point, cg.refdef.vieworg ) - 32.0f ) / 64.0f );
					Emote_Sprite( emote.shaders[e->picture], point, EMOTE_SIZE * grow * ( 1.0f - k * 0.1f ), rock, alpha );
				}
				continue;
			}
			e->popTime = cg.time ? cg.time : 1;
			VectorCopy( e->head, point );
			point[2] += EMOTE_ABOVE;
			trap->S_StartSound( point, e->to, CHAN_AUTO, emote.popSound );
			{
				const sfxHandle_t voice = Emote_Voice( e->from, e->mood );

				if ( voice )
					trap->S_StartSound( point, e->to, CHAN_AUTO, voice );
			}
		}
		// the own pop is drawn on the screen in first person (CG_EmoteDraw)
		if ( e->to == cg.clientNum && Emote_FirstPerson() )
			continue;
		{
			const int	pop = cg.time - e->popTime;
			const float	burst = (float)pop / EMOTE_BURST_MSEC;
			const float	scale = e->to == cg.clientNum ? EMOTE_OWN_SCALE : 1.0f;

			VectorCopy( e->head, point );
			point[2] += EMOTE_ABOVE + 8.0f * pop / EMOTE_POP_MSEC + 1.5f * sinf( pop * 0.008f );
			Emote_Sprite( emote.shaders[e->picture], point, EMOTE_POP_SIZE * scale * Emote_PopScale( pop ),
				6.0f * sinf( pop * 0.006f + e->sway ), Emote_PopAlpha( pop ) );
			for ( k = 0; k < EMOTE_BITS && burst < 1.0f; k++ ) {
				vec3_t bit;

				VectorMA( point, 60.0f * scale * ( 1.0f - ( 1.0f - burst ) * ( 1.0f - burst ) ), e->bits[k], bit );
				bit[2] -= 30.0f * scale * burst * burst;
				Emote_Sprite( emote.shaders[e->picture], bit, 3.5f * scale * ( 1.0f - 0.5f * burst ), k * 37.0f, 1.0f - burst );
			}
		}
	}
}

/*
=================
CG_EmoteDraw

In the 2D of the HUD: an emoji sent to the own player pops on the screen in first person
=================
*/
void CG_EmoteDraw( void ) {
	int i, k;

	if ( !Emote_FirstPerson() )
		return;
	for ( i = 0; i < EMOTE_FLYING; i++ ) {
		const emote_t	*e = &emote.emotes[i];
		const int		pop = cg.time - e->popTime;
		const float		burst = (float)pop / EMOTE_BURST_MSEC;
		vec4_t			color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float			size, x, y;

		if ( !e->active || e->to != cg.clientNum || !e->popTime || pop < 0 || pop >= EMOTE_POP_MSEC )
			continue;
		x = SCREEN_WIDTH * 0.5f;
		y = SCREEN_HEIGHT * 0.22f - 10.0f * pop / EMOTE_POP_MSEC + 3.0f * sinf( pop * 0.008f );
		for ( k = 0; k < EMOTE_BITS && burst < 1.0f; k++ ) {
			const float	angle = k * ( 2.0f * M_PI / EMOTE_BITS ) + e->sway;
			const float	reach = 90.0f * ( 1.0f - ( 1.0f - burst ) * ( 1.0f - burst ) );

			color[3] = 1.0f - burst;
			trap->R_SetColor( color );
			size = 16.0f * ( 1.0f - 0.5f * burst );
			CG_DrawPic( x + cosf( angle ) * reach - size * 0.5f, y + sinf( angle ) * reach + 40.0f * burst * burst - size * 0.5f,
				size, size, emote.shaders[e->picture] );
		}
		color[3] = Emote_PopAlpha( pop );
		trap->R_SetColor( color );
		size = 64.0f * Emote_PopScale( pop );
		CG_DrawRotatePic2( x, y, size, size, 6.0f * sinf( pop * 0.006f + e->sway ), emote.shaders[e->picture] );
	}
	trap->R_SetColor( NULL );
}
