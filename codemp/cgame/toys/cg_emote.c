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
// over from the eyes of the sender to the head of the other one, follows him, and pops above his head with a burst
// and a sound, and the voice of the sender's model may say a line that fits (a gloat for a laugh, a victory for a
// thumbs up), as far as the voice has such a sound. Who is sent one sees it pop on his screen in first person.
//
// An emoji of the game flies in an arc, rocks, pops by growing past its size, and small copies of it burst out with
// the sound of a pop: it goes to the others as TOY_EMOJI (game/toys/bg_toys.h, the way a snowball goes,
// toys/cg_toys.c) with the number of its picture in gfx/vision/sprays.txt. The interactive emojis players made on the
// page of the store of the site (deploy/models/modelstore.py and site/js/emotes.js of the website, which draws the
// same in its preview) say how they fly (an arc, a spiral, hops, straight), turn (rock, spin, wobble), pop (grow,
// shake, float up, twirl), what bursts out (copies, sparkles, confetti, hearts, stars, smoke, rings) in which two
// colours, their size, their sound (one of the game or an own one) and the line of the voice. Every chat emoji of
// the store is one of them too, as an emoji of the game pops. The engine fetches their list into emotes/index.txt
// (client/cl_models.cpp, v_emoteStoreChanged) and their own sounds into sound/emotes; one goes to the others as
// TOY_EMOTE_STORE with its number in that list. Every cgame flies it on its own. The sounds are made by
// tools/toys/sounds.py, the pieces of the bursts by tools/toys/emotes.py, the sprites of the emojis of the game
// gfx/vision/emote/<n> by tools/toys/sprays.py. Not from JA+, see docs/japlus/toys.md.

#include "cgame/cg_local.h"
#include "cgame/toys/cg_toys.h"

#define EMOTE_FLYING		16			// emoji in the air or popping at once
#define EMOTE_REACH			2048.0f		// how far a player can be to be sent one
#define EMOTE_GAP_MSEC		1000		// between two own emoji at least
#define EMOTE_SPEED			700.0f		// units a second along the line, the arc comes on top
#define EMOTE_POP_MSEC		1800
#define EMOTE_HEAD			36.0f		// above the origin of a player: his head
#define EMOTE_ABOVE			22.0f		// the pop above the head
#define EMOTE_SIZE			9.0f		// radius of the emoji in the air
#define EMOTE_POP_SIZE		15.0f
#define EMOTE_BITS			24			// pieces of a burst at most
#define EMOTE_TRAIL			6			// fading copies behind it in the air
#define EMOTE_OWN_SCALE		0.55f		// the pop over the own head, so it hides less of the view
#define EMOTE_STORE_MOST	900			// interactive emojis of the store (EMOTE_MOST and EMOJI_MOST of modelstore.py)
#define EMOTE_NAME			32
#define EMOTE_PICTURE		128			// pixels of the sprite of an emoji of the store each way

// the choices of an interactive emoji, as modelstore.py names them
typedef enum { BURST_COPIES, BURST_SPARKLES, BURST_CONFETTI, BURST_HEARTS, BURST_STARS, BURST_SMOKE, BURST_RINGS, BURST_NONE } emoteBurst_t;
typedef enum { PATH_ARC, PATH_SPIRAL, PATH_BOUNCE, PATH_STRAIGHT } emotePath_t;
typedef enum { SPIN_ROCK, SPIN_SPIN, SPIN_WOBBLE } emoteSpin_t;
typedef enum { POP_GROW, POP_SHAKE, POP_FLOAT, POP_TWIRL } emotePop_t;
typedef enum { SOUND_POP, SOUND_CHIME, SOUND_BOING, SOUND_SPARKLE, SOUND_RIMSHOT, SOUND_FANFARE, SOUND_NONE, SOUND_OWN } emoteSound_t;

static const char *const emoteBursts[] = { "copies", "sparkles", "confetti", "hearts", "stars", "smoke", "rings", "none" };
static const char *const emotePaths[] = { "arc", "spiral", "bounce", "straight" };
static const char *const emoteSpins[] = { "none", "spin", "wobble" };
static const char *const emotePops[] = { "pop", "shake", "float", "spin" };
static const char *const emoteSounds[] = { "pop", "chime", "boing", "sparkle", "rimshot", "fanfare", "none", "own" };
static const char *const emoteVoiceNames[] = { "none", "laugh", "victory", "anger", "confuse", "gasp", "taunt" };

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

#define MOOD_AUTO	( -1 )		// the line of the voice that fits the emoji

// an emoji that can be sent: one of the game or of the list of the store
typedef struct {
	int				number;			// in the list of the store, 0: a picture of the game
	int				picture;		//	of the game: its line of gfx/vision/sprays.txt
	char			name[EMOTE_NAME];
	char			emoji[EMOTE_NAME];
	emoteBurst_t	burst;
	emotePath_t		path;
	emoteSpin_t		spin;
	emotePop_t		pop;
	emoteSound_t	sound;
	int				voice;			// emoteMood_t or MOOD_AUTO
	vec3_t			colors[2];
	float			size;			// 1 the size of an emoji of the game
} emoteKind_t;

typedef enum { BIT_COPY, BIT_SPARKLE, BIT_CONFETTI, BIT_HEART, BIT_STAR, BIT_SMOKE, BIT_RING } emoteBitKind_t;

// a piece of the burst of a pop: it flies out from the pop with its speed (units a second), slowed by its drag
// and pulled by its gravity, turns and fades over its life
typedef struct {
	emoteBitKind_t	kind;
	vec3_t			velocity;
	float			size, gravity, drag, spin, spinSpeed, twinkle, flip;
	int				life, delay;
	int				color;			// 0, 1: of the two of the emoji
	float			from, to;		// a ring: its radius
} emoteBit_t;

typedef struct {
	qboolean	active;
	int			from, to;			// client numbers
	emoteKind_t	kind;
	qhandle_t	shader;				// the sprite of its emoji
	emoteMood_t	mood;
	int			start;				// cg.time it left
	int			flight;				// msec it flies
	int			popTime;			// cg.time it arrived, 0: in the air
	vec3_t		origin;				// where it left
	vec3_t		head;				// of the one it flies to, kept while he is out of sight
	float		arc;				// height of the arc
	float		sway;				// a random phase
	emoteBit_t	bits[EMOTE_BITS];
	int			numBits;
} emote_t;

static struct {
	emote_t		emotes[EMOTE_FLYING];
	qhandle_t	shaders[TOY_SPRAY_MOST];
	qhandle_t	pieces[BIT_RING + 1];		// the pieces of the bursts (tools/toys/emotes.py), BIT_COPY none
	sfxHandle_t	sendSound, sounds[SOUND_NONE];
	int			lastSend;
	emoteKind_t	store[EMOTE_STORE_MOST];	// the list of the store (emotes/index.txt)
	qhandle_t	storeShaders[EMOTE_STORE_MOST];	//	the sprites of their emojis, 0 not made yet, -1 none
	int			numStore;
	int			storeChanged;				// v_emoteStoreChanged when the list was read
	qboolean	storeRead;
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

// the mood of an emoji by its name (gfx/emoji)
static emoteMood_t Emote_Mood( const char *emoji ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( emoteMoods ); i++ ) {
		if ( !Q_stricmp( emoteMoods[i].name, emoji ) )
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

// the index of a word in a list of choices, 0 (the first) when it is none of them
static int Emote_Choice( const char *word, const char *const *list, int count ) {
	int i;

	for ( i = 0; i < count; i++ ) {
		if ( !Q_stricmp( word, list[i] ) )
			return i;
	}
	return 0;
}

static void Emote_Color( const char *hex, vec3_t out ) {
	const unsigned value = (unsigned)strtoul( hex, NULL, 16 );

	VectorSet( out, ( ( value >> 16 ) & 255 ) / 255.0f, ( ( value >> 8 ) & 255 ) / 255.0f, ( value & 255 ) / 255.0f );
}

// an emoji of the game (gfx/vision/sprays.txt): it flies and pops as it always did
static void Emote_GameKind( int picture, emoteKind_t *out ) {
	memset( out, 0, sizeof( *out ) );
	out->picture = picture;
	COM_StripExtension( CG_ToyPictureName( picture ), out->emoji, sizeof( out->emoji ) );
	Q_strncpyz( out->name, out->emoji, sizeof( out->name ) );
	out->voice = MOOD_AUTO;
	out->size = 1.0f;
	VectorSet( out->colors[0], 1.0f, 1.0f, 1.0f );
	VectorSet( out->colors[1], 1.0f, 1.0f, 1.0f );
}

/*
=================
Emote_ReadStore

The list of the interactive emojis of the store, emotes/index.txt, when the engine fetched a new one: a line each,
<number> <name> <emoji> <burst> <colour> <colour> <path> <spin> <size> <pop> <sound> <voice> <bytes> <sha256>
=================
*/
static void Emote_ReadStore( void ) {
	static char		text[96 * 1024];
	char			value[16], *line, *next;
	fileHandle_t	f;
	int				len, changed;

	trap->Cvar_VariableStringBuffer( "v_emoteStoreChanged", value, sizeof( value ) );
	changed = atoi( value );
	if ( emote.storeRead && changed == emote.storeChanged )
		return;
	emote.storeRead = qtrue;
	emote.storeChanged = changed;
	emote.numStore = 0;
	memset( emote.storeShaders, 0, sizeof( emote.storeShaders ) );
	len = trap->FS_Open( "emotes/index.txt", &f, FS_READ );
	if ( !f )
		return;
	len = Q_min( len, (int)sizeof( text ) - 1 );
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[Q_max( len, 0 )] = '\0';
	for ( line = text; line && *line && emote.numStore < EMOTE_STORE_MOST; line = next ) {
		emoteKind_t	*k = &emote.store[emote.numStore];
		char		name[EMOTE_NAME], emoji[EMOTE_NAME], burst[16], colorA[8], colorB[8], path[16], spin[16], pop[16], sound[16], voice[16];
		int			number, size;

		next = strchr( line, '\n' );
		if ( next )
			*next++ = '\0';
		if ( line[0] == '#' || sscanf( line, "%d %31s %31s %15s %7s %7s %15s %15s %d %15s %15s %15s", &number, name, emoji, burst, colorA, colorB,
				path, spin, &size, pop, sound, voice ) != 12 || number <= 0 || number >= TOY_BOARD_SPLIT * TOY_BOARD_SPLIT ) {
			continue;
		}
		memset( k, 0, sizeof( *k ) );
		k->number = number;
		Q_strncpyz( k->name, name, sizeof( k->name ) );
		Q_strncpyz( k->emoji, emoji, sizeof( k->emoji ) );
		k->picture = CG_ToyPicture( emoji );
		k->burst = (emoteBurst_t)Emote_Choice( burst, emoteBursts, ARRAY_LEN( emoteBursts ) );
		k->path = (emotePath_t)Emote_Choice( path, emotePaths, ARRAY_LEN( emotePaths ) );
		k->spin = (emoteSpin_t)Emote_Choice( spin, emoteSpins, ARRAY_LEN( emoteSpins ) );
		k->pop = (emotePop_t)Emote_Choice( pop, emotePops, ARRAY_LEN( emotePops ) );
		k->sound = (emoteSound_t)Emote_Choice( sound, emoteSounds, ARRAY_LEN( emoteSounds ) );
		k->voice = !Q_stricmp( voice, "auto" ) ? MOOD_AUTO : Emote_Choice( voice, emoteVoiceNames, ARRAY_LEN( emoteVoiceNames ) );
		k->size = Com_Clamp( 0.6f, 1.6f, size / 100.0f );
		Emote_Color( colorA, k->colors[0] );
		Emote_Color( colorB, k->colors[1] );
		emote.numStore++;
	}
}

static const emoteKind_t *Emote_StoreByName( const char *name ) {
	int i;

	Emote_ReadStore();
	for ( i = 0; i < emote.numStore; i++ ) {
		if ( !Q_stricmp( emote.store[i].name, name ) )
			return &emote.store[i];
	}
	return NULL;
}

static int Emote_StoreByNumber( int number ) {
	int i;

	Emote_ReadStore();
	for ( i = 0; i < emote.numStore; i++ ) {
		if ( emote.store[i].number == number )
			return i;
	}
	return -1;
}

// The sprite of the emoji of a kind: an emoji of the game has its shader of tools/toys/sprays.py; an emoji of the
// store is made from its picture with an engine that can (v_cgameImage, v_cgameScene). 0: there is none.
static qhandle_t Emote_Shader( const emoteKind_t *k ) {
	if ( k->number ) {
		const int i = Emote_StoreByNumber( k->number );

		if ( i >= 0 && i < emote.numStore && !emote.storeShaders[i] ) {
			static byte	rgba[EMOTE_PICTURE * EMOTE_PICTURE * 4];
			char		value[8];

			emote.storeShaders[i] = -1;
			trap->Cvar_VariableStringBuffer( "v_cgameImage", value, sizeof( value ) );
			if ( atoi( value ) && trap->ext.R_LoadImageFit && trap->ext.R_RegisterShaderRGBA
				&& trap->ext.R_LoadImageFit( va( "gfx/emoji/%s.png", k->emoji ), rgba, EMOTE_PICTURE, EMOTE_PICTURE ) ) {
				const qhandle_t shader = trap->ext.R_RegisterShaderRGBA( va( "emote_%s", k->emoji ), rgba, EMOTE_PICTURE, EMOTE_PICTURE, 0 );

				emote.storeShaders[i] = shader ? shader : -1;
			}
			// (an older engine: only an emoji of the game can be shown)
			if ( emote.storeShaders[i] < 0 && k->picture >= 0 )
				emote.storeShaders[i] = trap->R_RegisterShader( va( "gfx/vision/emote/%i", k->picture ) );
		}
		return ( i >= 0 && i < emote.numStore && emote.storeShaders[i] > 0 ) ? emote.storeShaders[i] : 0;
	}
	if ( k->picture < 0 || k->picture >= TOY_SPRAY_MOST )
		return 0;
	if ( !emote.shaders[k->picture] )
		emote.shaders[k->picture] = trap->R_RegisterShader( va( "gfx/vision/emote/%i", k->picture ) );
	return emote.shaders[k->picture];
}

static void Emote_Register( void ) {
	static const char *const pieces[] = { NULL, "sparkle", "confetti", "heart", "star", "smoke", "ring" };
	int i;

	if ( emote.sendSound )
		return;
	emote.sendSound = trap->S_RegisterSound( "sound/vision/toys/emote_send.wav" );
	for ( i = 0; i < SOUND_NONE; i++ )
		emote.sounds[i] = trap->S_RegisterSound( va( "sound/vision/toys/emote_%s.wav", emoteSounds[i] ) );
	for ( i = BIT_SPARKLE; i <= BIT_RING; i++ )
		emote.pieces[i] = trap->R_RegisterShader( va( "gfx/vision/emote/%s", pieces[i] ) );
}

/*
=================
Emote_Burst

The pieces that burst out of a pop, as site/js/emotes.js makes them: around the pop, more up than down
=================
*/
static void Emote_Bit( emote_t *e, emoteBitKind_t kind, float speed, int life, float size, int color ) {
	emoteBit_t	*b;
	float		a, up;

	if ( e->numBits >= EMOTE_BITS )
		return;
	b = &e->bits[e->numBits];
	memset( b, 0, sizeof( *b ) );
	a = (float)e->numBits / EMOTE_BITS * 2.0f * M_PI + Q_flrand( -0.3f, 0.3f ) + e->sway;
	up = Q_flrand( -0.2f, 1.0f );
	VectorSet( b->velocity, cosf( a ) * speed, sinf( a ) * speed, up * speed * 0.8f );
	b->kind = kind;
	b->life = life;
	b->size = size;
	b->color = color;
	e->numBits++;
}

static void Emote_Burst( emote_t *e ) {
	int i, n;

	e->numBits = 0;
	switch ( e->kind.burst ) {
	case BURST_COPIES:
		for ( i = 0; i < 10; i++ ) {
			Emote_Bit( e, BIT_COPY, 92.0f, 650, 3.5f, 0 );
			e->bits[i].gravity = 140.0f;
			e->bits[i].spin = i * 37.0f;
		}
		break;
	case BURST_SPARKLES:
		for ( i = 0; i < 16; i++ ) {
			Emote_Bit( e, BIT_SPARKLE, Q_flrand( 110.0f, 170.0f ), 850, Q_flrand( 3.5f, 6.0f ), i & 1 );
			e->bits[i].drag = 2.2f;
			e->bits[i].twinkle = Q_flrand( 0.0f, 6.0f );
		}
		break;
	case BURST_CONFETTI:
		for ( i = 0; i < 24; i++ ) {
			Emote_Bit( e, BIT_CONFETTI, Q_flrand( 60.0f, 120.0f ), 1200, Q_flrand( 1.6f, 2.4f ), i & 1 );
			e->bits[i].velocity[2] = Q_flrand( 60.0f, 150.0f );
			e->bits[i].gravity = 160.0f;
			e->bits[i].drag = 1.5f;
			e->bits[i].spin = Q_flrand( 0.0f, 360.0f );
			e->bits[i].spinSpeed = Q_flrand( -720.0f, 720.0f );
			e->bits[i].flip = Q_flrand( 4.0f, 12.0f );
		}
		break;
	case BURST_HEARTS:
		for ( i = 0; i < 8; i++ ) {
			Emote_Bit( e, BIT_HEART, Q_flrand( 40.0f, 70.0f ), 1100, Q_flrand( 3.0f, 4.5f ), i % 3 == 2 );
			e->bits[i].gravity = -40.0f;
			e->bits[i].drag = 1.2f;
			e->bits[i].twinkle = Q_flrand( 0.0f, 6.0f );
		}
		break;
	case BURST_STARS:
		for ( i = 0; i < 10; i++ ) {
			Emote_Bit( e, BIT_STAR, Q_flrand( 100.0f, 150.0f ), 850, Q_flrand( 3.0f, 4.5f ), i & 1 );
			e->bits[i].gravity = 60.0f;
			e->bits[i].drag = 1.8f;
			e->bits[i].spin = Q_flrand( 0.0f, 360.0f );
			e->bits[i].spinSpeed = Q_flrand( -540.0f, 540.0f );
		}
		break;
	case BURST_SMOKE:
		for ( i = 0; i < 6; i++ ) {
			Emote_Bit( e, BIT_SMOKE, Q_flrand( 15.0f, 30.0f ), 900, Q_flrand( 6.0f, 9.0f ), 0 );
			e->bits[i].gravity = -10.0f;
			e->bits[i].drag = 1.5f;
			e->bits[i].spin = Q_flrand( 0.0f, 360.0f );
			e->bits[i].spinSpeed = Q_flrand( -60.0f, 60.0f );
		}
		break;
	case BURST_RINGS:
		for ( i = 0; i < 2; i++ ) {
			Emote_Bit( e, BIT_RING, 0.0f, 550, 0.0f, i );
			e->bits[i].delay = i * 130;
			e->bits[i].from = 6.0f;
			e->bits[i].to = i ? 32.0f : 42.0f;
		}
		break;
	default:
		break;
	}
	for ( n = 0; n < e->numBits; n++ )
		e->bits[n].size *= e->kind.size;
}

static void Emote_Spawn( int from, int to, const emoteKind_t *kind, const vec3_t origin ) {
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
	e->kind = *kind;
	e->shader = Emote_Shader( kind );
	if ( !e->shader )
		return;
	e->active = qtrue;
	e->from = from;
	e->to = to;
	e->mood = kind->voice == MOOD_AUTO ? Emote_Mood( kind->emoji ) : (emoteMood_t)kind->voice;
	e->start = cg.time;
	VectorCopy( origin, e->origin );
	VectorCopy( head, e->head );
	dist = Distance( origin, head );
	e->flight = (int)( 1000.0f * Com_Clamp( 0.35f, 2.2f, 0.25f + dist / EMOTE_SPEED ) );
	e->arc = Com_Clamp( 16.0f, 120.0f, dist * 0.3f );
	e->sway = Q_flrand( 0.0f, 2.0f * M_PI );
	Emote_Register();
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

// v_emote [emoji]: the emoji (or the one of v_emoteImage) to the player under the crosshair: an emoji of the game, or
// an interactive emoji of the store by its name
void CG_Emote_f( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const char			*name = trap->Cmd_Argc() > 1 ? CG_Argv( 1 ) : v_emoteImage.string;
	const emoteKind_t	*stored = NULL;
	emoteKind_t			kind;
	vec3_t				eyes, forward;
	toyThrow_t			toy;
	int					picture, target, i;

	if ( !CG_ToyCanUse() || ( emote.lastSend && cg.time - emote.lastSend < EMOTE_GAP_MSEC && cg.time >= emote.lastSend ) )
		return;
	picture = CG_ToyPicture( name );
	if ( picture < 0 && !( stored = Emote_StoreByName( name ) ) ) {
		Com_Printf( "v_emoteImage names no known emoji (a chat emoji without colons, as laughing, or an interactive emoji of jka.vision)\n" );
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
	for ( i = 0; i < 3; i++ )
		toy.origin[i] = (int)floorf( eyes[i] + 0.5f );
	toy.velocity[0] = target;
	if ( stored ) {
		toy.kind = TOY_EMOTE_STORE;
		toy.velocity[1] = stored->number / TOY_BOARD_SPLIT;
		toy.velocity[2] = stored->number % TOY_BOARD_SPLIT;
		kind = *stored;
	} else {
		toy.kind = TOY_EMOJI;
		toy.velocity[1] = picture;
		toy.velocity[2] = 0;
		Emote_GameKind( picture, &kind );
	}
	if ( !BG_ToyValid( &toy ) )
		return;
	CG_ToySend( &toy );
	// in first person it leaves a little in front of the eyes, not out of the camera
	if ( Emote_FirstPerson() ) {
		VectorMA( eyes, 40.0f, forward, eyes );
		eyes[2] -= 10.0f;
	}
	Emote_Spawn( cg.clientNum, target, &kind, eyes );
}

// an emoji of somebody else, from the game module or the masterserver. One of the store that is not in the own
// list yet has the engine fetch the list again.
void CG_EmoteHeard( int client, int kind, const vec3_t origin, const vec3_t velocity ) {
	const int	to = (int)velocity[0];
	emoteKind_t	k;

	if ( to == client || to < 0 || to >= MAX_CLIENTS || !cgs.clientinfo[to].infoValid )
		return;
	if ( kind == TOY_EMOTE_STORE ) {
		const int i = Emote_StoreByNumber( (int)velocity[1] * TOY_BOARD_SPLIT + (int)velocity[2] );

		if ( i < 0 ) {
			trap->SendConsoleCommand( "v_emotes refresh\n" );
			return;
		}
		Emote_Spawn( client, to, &emote.store[i], origin );
		return;
	}
	if ( (int)velocity[1] < 0 || (int)velocity[1] >= CG_ToyPictures() )
		return;
	Emote_GameKind( (int)velocity[1], &k );
	Emote_Spawn( client, to, &k, origin );
}

static void Emote_Sprite( qhandle_t shader, const vec3_t origin, float radius, float rotation, const vec3_t color, float alpha, qboolean added ) {
	refEntity_t	ent;
	const float	a = Com_Clamp( 0.0f, 1.0f, alpha );
	int			i;

	if ( !shader || a <= 0.0f )
		return;
	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	VectorCopy( origin, ent.origin );
	ent.radius = radius;
	ent.rotation = rotation;
	ent.customShader = shader;
	// (what is added has no alpha: its colour fades)
	for ( i = 0; i < 3; i++ )
		ent.shaderRGBA[i] = (byte)( Com_Clamp( 0.0f, 1.0f, color[i] * ( added ? a : 1.0f ) ) * 255.0f );
	ent.shaderRGBA[3] = (byte)( a * 255.0f );
	trap->R_AddRefEntityToScene( &ent );
}

// a scrap of confetti: a quad that turns about its middle and tumbles, so it shows its edge now and then
static void Emote_Scrap( const vec3_t origin, float radius, float spin, float tumble, const vec3_t color, float alpha ) {
	polyVert_t	verts[4];
	vec3_t		right, up, axis1, axis2;
	const float	s = sinf( DEG2RAD( spin ) ), c = cosf( DEG2RAD( spin ) );
	const float	squash = fabsf( cosf( tumble ) ) * 0.85f + 0.15f;
	int			i;

	VectorCopy( cg.refdef.viewaxis[1], right );
	VectorCopy( cg.refdef.viewaxis[2], up );
	for ( i = 0; i < 3; i++ ) {
		axis1[i] = ( right[i] * c + up[i] * s ) * radius;
		axis2[i] = ( up[i] * c - right[i] * s ) * radius * squash;
	}
	for ( i = 0; i < 4; i++ ) {
		const float	u = ( i == 1 || i == 2 ) ? 1.0f : -1.0f, v = ( i >= 2 ) ? 1.0f : -1.0f;
		int			k;

		for ( k = 0; k < 3; k++ )
			verts[i].xyz[k] = origin[k] + axis1[k] * u + axis2[k] * v;
		verts[i].st[0] = u * 0.5f + 0.5f;
		verts[i].st[1] = 0.5f - v * 0.5f;
		for ( k = 0; k < 3; k++ )
			verts[i].modulate[k] = (byte)( Com_Clamp( 0.0f, 1.0f, color[k] ) * 255.0f );
		verts[i].modulate[3] = (byte)( Com_Clamp( 0.0f, 1.0f, alpha ) * 255.0f );
	}
	trap->R_AddPolysToScene( emote.pieces[BIT_CONFETTI], 4, verts, 1 );
}

/*
=================
Emote_Along

Where it is in the air at a fraction of its flight, the paths of site/js/emotes.js: an arc over the line, a spiral
round it, three hops, or straight
=================
*/
static void Emote_Along( const emote_t *e, float f, vec3_t out ) {
	vec3_t	way, side;
	float	e2, up = 0.0f, across = 0.0f;

	f = Com_Clamp( 0.0f, 1.0f, f );
	e2 = f * f * ( 3.0f - 2.0f * f );
	VectorSubtract( e->head, e->origin, way );
	VectorMA( e->origin, e2, way, out );
	switch ( e->kind.path ) {
	case PATH_SPIRAL:
		up = e->arc * 4.0f * e2 * ( 1.0f - e2 ) * 0.6f + 18.0f * sinf( M_PI * f ) * sinf( f * M_PI * 6.0f );
		across = 18.0f * sinf( M_PI * f ) * cosf( f * M_PI * 6.0f );
		break;
	case PATH_BOUNCE:
		up = e->arc * 0.6f * fabsf( sinf( M_PI * 3.0f * e2 ) ) * ( 1.0f - 0.5f * e2 );
		break;
	case PATH_STRAIGHT:
		break;
	default:
		up = e->arc * 4.0f * e2 * ( 1.0f - e2 );
	}
	out[2] += up;
	if ( across != 0.0f ) {
		VectorSet( side, -way[1], way[0], 0.0f );
		if ( VectorNormalize( side ) > 0.0f )
			VectorMA( out, across, side, out );
	}
}

// the turn of the emoji in the air, degrees, msec after it left
static float Emote_Turn( const emote_t *e, int t ) {
	switch ( e->kind.spin ) {
	case SPIN_SPIN:		return t * 0.54f;
	case SPIN_WOBBLE:	return 30.0f * sinf( t * 0.018f );
	default:			return 14.0f * sinf( t * 0.012f + e->sway );
	}
}

// the pop at an age: its scale, its turn, how far it is moved across and up (units), and how much of it is seen
static void Emote_Pop( const emote_t *e, int age, float *scale, float *turn, float *x, float *y, float *alpha ) {
	const int fadeFrom = EMOTE_POP_MSEC - 450;

	*alpha = age > fadeFrom ? 1.0f - (float)( age - fadeFrom ) / 450.0f : 1.0f;
	*scale = 1.0f;
	*turn = 6.0f * sinf( age * 0.006f + e->sway );
	*x = 0.0f;
	*y = 8.0f * age / EMOTE_POP_MSEC + 1.5f * sinf( age * 0.008f );
	switch ( e->kind.pop ) {
	case POP_SHAKE:
		*scale = Q_min( 1.0f, age / 100.0f );
		*x = 6.0f * sinf( age * 0.06f ) * expf( -age / 500.0f );
		break;
	case POP_FLOAT:
		*scale = Q_min( 1.0f, age / 150.0f );
		*y = 40.0f * age / EMOTE_POP_MSEC;
		break;
	case POP_TWIRL: {
		const float f = 1.0f - Q_min( 1.0f, age / 600.0f );

		*scale = age < 200 ? 1.2f * age / 200.0f : 1.2f - 0.2f * Q_min( 1.0f, ( age - 200 ) / 200.0f );
		*turn = 720.0f * ( 1.0f - f * f * f );
		break;
	}
	default:
		if ( age < 120 )
			*scale = 1.35f * sinf( (float)age / 120.0f * ( M_PI * 0.5f ) );
		else if ( age < 300 )
			*scale = 1.35f - 0.35f * (float)( age - 120 ) / 180.0f;
		else if ( age > fadeFrom )
			*scale = 1.0f + 0.15f * (float)( age - fadeFrom ) / 450.0f;
	}
}

/*
=================
Emote_Bits

The pieces of the burst of a pop at point, scale the size of all of it: the smoke and the rings behind the emoji,
the rest in front of it (front), as the preview of the page has them
=================
*/
static void Emote_Bits( const emote_t *e, const vec3_t point, float scale, int age, qboolean front ) {
	int i;

	for ( i = 0; i < e->numBits; i++ ) {
		const emoteBit_t	*b = &e->bits[i];
		const int			a = age - b->delay;
		const float			*color = e->kind.colors[b->color];
		float				life, s, fade, drag, r, rot, alpha;
		vec3_t				at;

		if ( a < 0 || a >= b->life || ( b->kind == BIT_SMOKE || b->kind == BIT_RING ) == front )
			continue;
		life = (float)a / b->life;
		s = a / 1000.0f;
		fade = 1.0f - life;
		if ( b->kind == BIT_RING ) {
			r = b->from + ( b->to - b->from ) * ( 1.0f - ( 1.0f - life ) * ( 1.0f - life ) );
			Emote_Sprite( emote.pieces[BIT_RING], point, r * e->kind.size * scale, 0.0f, color, fade, qtrue );
			continue;
		}
		drag = b->drag > 0.0f ? ( 1.0f - expf( -b->drag * s ) ) / b->drag : s;
		VectorMA( point, drag * scale, b->velocity, at );
		at[2] -= 0.5f * b->gravity * s * s * scale;
		r = b->size * scale;
		rot = b->spin + b->spinSpeed * s;
		alpha = fade;
		switch ( b->kind ) {
		case BIT_COPY:
			Emote_Sprite( e->shader, at, r * ( 1.0f - 0.5f * life ), rot, colorWhite, alpha, qfalse );
			break;
		case BIT_SPARKLE:
			Emote_Sprite( emote.pieces[BIT_SPARKLE], at, r * ( 0.6f + 0.6f * fabsf( sinf( s * 14.0f + b->twinkle ) ) ), 0.0f, color, alpha, qtrue );
			break;
		case BIT_CONFETTI:
			Emote_Scrap( at, r, rot, s * b->flip, color, Q_min( 1.0f, fade * 2.5f ) );
			break;
		case BIT_HEART:
			at[0] += 4.0f * sinf( s * 5.0f + b->twinkle ) * scale;
			Emote_Sprite( emote.pieces[BIT_HEART], at, r * Q_min( 1.0f, life * 6.0f ), 0.0f, color, Q_min( 1.0f, fade * 2.0f ), qfalse );
			break;
		case BIT_STAR:
			Emote_Sprite( emote.pieces[BIT_STAR], at, r, rot, color, Q_min( 1.0f, fade * 2.0f ), qfalse );
			break;
		case BIT_SMOKE:
			Emote_Sprite( emote.pieces[BIT_SMOKE], at, r * ( 1.0f + 1.2f * ( 1.0f - ( 1.0f - life ) * ( 1.0f - life ) ) ), rot, color, fade * 0.7f, qfalse );
			break;
		default:
			break;
		}
	}
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
					Emote_Sprite( e->shader, point, EMOTE_SIZE * e->kind.size * grow * ( 1.0f - k * 0.1f ), Emote_Turn( e, age - k * 30 ),
						colorWhite, alpha, qfalse );
				}
				continue;
			}
			e->popTime = cg.time ? cg.time : 1;
			Emote_Burst( e );
			VectorCopy( e->head, point );
			point[2] += EMOTE_ABOVE;
			if ( e->kind.sound == SOUND_OWN ) {
				const sfxHandle_t own = trap->S_RegisterSound( va( "sound/emotes/%s.wav", e->kind.name ) );

				if ( own )
					trap->S_StartSound( point, e->to, CHAN_AUTO, own );
			} else if ( e->kind.sound < SOUND_NONE ) {
				trap->S_StartSound( point, e->to, CHAN_AUTO, emote.sounds[e->kind.sound] );
			}
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
			const float	scale = e->to == cg.clientNum ? EMOTE_OWN_SCALE : 1.0f;
			float		size, turn, x, y, alpha;

			Emote_Pop( e, pop, &size, &turn, &x, &y, &alpha );
			VectorCopy( e->head, point );
			point[2] += EMOTE_ABOVE + y;
			VectorMA( point, x, cg.refdef.viewaxis[1], point );
			Emote_Bits( e, point, scale, pop, qfalse );
			Emote_Sprite( e->shader, point, EMOTE_POP_SIZE * e->kind.size * scale * size, turn, colorWhite, alpha, qfalse );
			Emote_Bits( e, point, scale, pop, qtrue );
		}
	}
}

// a piece of a burst on the screen: x, y the middle, size the side, in the 2D of 640 x 480
static void Emote_Pic( qhandle_t shader, float x, float y, float size, float rotation, const vec3_t color, float alpha, qboolean added ) {
	vec4_t c;

	if ( !shader || alpha <= 0.0f )
		return;
	VectorScale( color, added ? alpha : 1.0f, c );
	c[3] = Com_Clamp( 0.0f, 1.0f, alpha );
	trap->R_SetColor( c );
	if ( rotation != 0.0f )
		CG_DrawRotatePic2( x, y, size, size, rotation, shader );
	else
		CG_DrawPic( x - size * 0.5f, y - size * 0.5f, size, size, shader );
}

/*
=================
CG_EmoteDraw

In the 2D of the HUD: an emoji sent to the own player pops on the screen in first person, its burst around it. A
unit of the pop in the level is 4.3 units of the screen here.
=================
*/
void CG_EmoteDraw( void ) {
	const float	unit = 64.0f / EMOTE_POP_SIZE;
	int			i, k;

	if ( !Emote_FirstPerson() )
		return;
	for ( i = 0; i < EMOTE_FLYING; i++ ) {
		const emote_t	*e = &emote.emotes[i];
		const int		pop = cg.time - e->popTime;
		float			size, turn, x0, y0, alpha, cx, cy;

		if ( !e->active || e->to != cg.clientNum || !e->popTime || pop < 0 || pop >= EMOTE_POP_MSEC )
			continue;
		Emote_Pop( e, pop, &size, &turn, &x0, &y0, &alpha );
		cx = SCREEN_WIDTH * 0.5f + x0 * unit;
		cy = SCREEN_HEIGHT * 0.22f - y0 * unit * 0.6f;
		for ( k = 0; k < e->numBits; k++ ) {
			const emoteBit_t	*b = &e->bits[k];
			const int			a = pop - b->delay;
			const float			*color = e->kind.colors[b->color];
			float				life, s, fade, drag, x, y, r;

			if ( a < 0 || a >= b->life )
				continue;
			life = (float)a / b->life;
			s = a / 1000.0f;
			fade = 1.0f - life;
			if ( b->kind == BIT_RING ) {
				r = ( b->from + ( b->to - b->from ) * ( 1.0f - ( 1.0f - life ) * ( 1.0f - life ) ) ) * e->kind.size;
				Emote_Pic( emote.pieces[BIT_RING], cx, cy, r * 2.0f * unit, 0.0f, color, fade, qtrue );
				continue;
			}
			drag = b->drag > 0.0f ? ( 1.0f - expf( -b->drag * s ) ) / b->drag : s;
			// the burst spreads across the screen: its way left and right and up
			x = cx + b->velocity[0] * drag * unit * 0.8f;
			y = cy - ( b->velocity[2] * drag - 0.5f * b->gravity * s * s ) * unit * 0.8f;
			r = b->size * 2.0f * unit;
			switch ( b->kind ) {
			case BIT_COPY:
				Emote_Pic( e->shader, x, y, r * ( 1.0f - 0.5f * life ), 0.0f, colorWhite, fade, qfalse );
				break;
			case BIT_SPARKLE:
				Emote_Pic( emote.pieces[BIT_SPARKLE], x, y, r * ( 0.6f + 0.6f * fabsf( sinf( s * 14.0f + b->twinkle ) ) ), 0.0f, color, fade, qtrue );
				break;
			case BIT_CONFETTI:
				Emote_Pic( emote.pieces[BIT_CONFETTI], x, y, r, b->spin + b->spinSpeed * s, color, Q_min( 1.0f, fade * 2.5f ), qfalse );
				break;
			case BIT_HEART:
				Emote_Pic( emote.pieces[BIT_HEART], x, y, r * Q_min( 1.0f, life * 6.0f ), 0.0f, color, Q_min( 1.0f, fade * 2.0f ), qfalse );
				break;
			case BIT_STAR:
				Emote_Pic( emote.pieces[BIT_STAR], x, y, r, b->spin + b->spinSpeed * s, color, Q_min( 1.0f, fade * 2.0f ), qfalse );
				break;
			case BIT_SMOKE:
				Emote_Pic( emote.pieces[BIT_SMOKE], x, y, r * ( 1.0f + 1.2f * ( 1.0f - fade * fade ) ), 0.0f, color, fade * 0.7f, qfalse );
				break;
			default:
				break;
			}
		}
		Emote_Pic( e->shader, cx, cy, 64.0f * e->kind.size * size, turn, colorWhite, alpha, qfalse );
	}
	trap->R_SetColor( NULL );
}
