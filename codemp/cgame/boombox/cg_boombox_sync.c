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
cg_boombox_sync.c -- Sync audioplayer: a box plays what a music player on this computer plays (Spotify, YouTube
Music, Quince ...), for everybody near it. Not from JA+, see docs/japlus/boombox.md and bg_boombox.h.

"Sync audioplayer" on the card of a box asks every player of music on the desktop bus what it plays
(ui/ui_music.c, Music_SyncScan); one that plays is taken at once, of several the player picks one. Then the box
is told (BOX_DO_SYNC with the title), and voice/cg_voice_feed.c takes the sound of that player from the sound
server, codes it and hands it here a frame at a time, which goes to the server as RELAY_FEED. The server sends it
to everybody who hears the box, but not back here: this player hears his music player himself, unless he wants it
from the box (v_boxSyncHear), which then gets the frames here, without the way to the server and back; the music
player is then moved to a silent sink of the game, so it is heard only from the box (voice/cg_voice_feed.c).

What the player plays is said again when it changes (BOX_DO_SYNCTITLE). The sync ends when this player ends it,
when somebody else does or the server ends it (the box is no longer BOX_SYNCED), when the box is gone, when the
sound of the player is not found, and when the player leaves the bus.
*/

#include "cgame/cg_local.h"
#include "cg_boombox.h"
#include "ui/ui_music.h"

#define SYNC_DECIDE_MSEC	700			// the players are asked this long before one that plays is taken by itself
#define SYNC_NONE_MSEC		3000		// and nothing plays after this long: the player is told so
#define SYNC_CHOOSE_MSEC	20000		// a choice on the card is put away after this long
#define SYNC_ANSWER_MSEC	4000		// the box says BOX_SYNCED within this long after the sync went out
#define SYNC_FIND_MSEC		8000		// the sound of the player is not found for this long: the sync ends
#define SYNC_GONE_MSEC		10000		// the player is not on the bus for this long: the sync ends
#define SYNC_TITLE_MSEC		1500		// a new title goes out at most this often (the flood protection of the server)
#define SYNC_CHOICES		MUSIC_MAX_PLAYERS

typedef struct syncPlayer_s {
	char		bus[128];
	char		key[MUSIC_KEY];
	int			pid;
	char		name[64];			// as the card shows it ("Spotify")
	char		now[128];			// what it plays
} syncPlayer_t;

static struct {
	// the sync of this player
	int				box;				// -1: none
	int				startTime;
	syncPlayer_t	player;
	char			sentTitle[BOX_TITLE];
	int				titleTime;
	int				seenTime;			// when the player was last on the bus
	qboolean		answered;			// the box said BOX_SYNCED since
	int				foundTime;			// when the sound of the player was last found (or the sync began)

	// the choice on the card
	int				chooseBox;			// -1: none
	int				chooseTime;
	qboolean		decided;			// one that played alone was looked for
	qboolean		takeFirst;			//	or the first of several (the wheel, which shows no choice)
	syncPlayer_t	choices[SYNC_CHOICES];
	int				numChoices;
} bs = { .box = -1, .chooseBox = -1 };

// The title the box shows: what the player plays, with the name of the player in front if it did not say its own
static void Sync_Title( const syncPlayer_t *p, char *out, int outSize ) {
	if ( !p->now[0] )
		Q_strncpyz( out, p->name, outSize );
	else if ( strchr( p->now, ':' ) )
		Q_strncpyz( out, p->now, outSize );
	else
		Com_sprintf( out, outSize, "%s: %s", p->name, p->now );
}

// the players of music on the bus that play now, as the last round of questions found them
static int Sync_Playing( syncPlayer_t *out, int max ) {
	const musicLive_t	*live;
	const int			count = Music_Live( &live );
	int					i, n = 0;

	for ( i = 0; i < count && n < max; i++ ) {
		if ( !live[i].playing )
			continue;
		Q_strncpyz( out[n].bus, live[i].bus, sizeof( out[n].bus ) );
		Q_strncpyz( out[n].key, live[i].key, sizeof( out[n].key ) );
		out[n].pid = ( live[i].pid > 0 ) ? live[i].pid : 0;
		Music_PlayerName( live[i].key, out[n].name, sizeof( out[n].name ) );
		Q_strncpyz( out[n].now, live[i].now, sizeof( out[n].now ) );
		n++;
	}
	return n;
}

// the sync of this player ends here; tell: the box is told too
static void Sync_End( qboolean tell ) {
	if ( bs.box < 0 )
		return;
	CG_FeedStop();
	if ( tell )
		CG_BoomboxSyncSend( BOX_DO_UNSYNC, bs.box, NULL );
	bs.box = -1;
}

// a line for this player, in the chat box as the lines of the server for the boxes are
static void Sync_Say( const char *text ) {
	const char *line = va( "^5Boombox^7: %s", text );

	trap->Print( "%s\n", line );
	CG_NotifyAdd( NOTIFY_EVENT, line );
}

// a frame of the music player, coded: to the server, and to the box here if he wants to hear it from there
static void Sync_Out( int channel, int seq, const byte *frame, int len ) {
	CG_VoiceFeedSend( bs.box, channel, seq, frame, len );
	if ( v_boxSyncHear.integer )
		CG_VoiceFeedLocal( bs.box, channel, seq, frame, len );
}

/*
=================
Sync_Start

The box plays that player from now on. qfalse: its sound can not be taken, he is told why.
=================
*/
static qboolean Sync_Start( int box, const syncPlayer_t *p ) {
	boomboxInfo_t	info;
	char			title[BOX_TITLE];

	if ( !CG_BoomboxInfo( box, &info ) )
		return qfalse;
	// (the one before ends first: the server ends it by itself, but the frames would go on)
	Sync_End( qfalse );
	if ( !CG_FeedStart( p->pid, p->key, ( info.kind == BOX_KIND_SYSTEM ) ? 2 : 1 ) ) {
		Sync_Say( CG_VisionText( "The sound of your music player can not be taken (no PulseAudio or PipeWire)" ) );
		return qfalse;
	}
	bs.box = box;
	bs.player = *p;
	bs.startTime = bs.titleTime = bs.seenTime = bs.foundTime = trap->Milliseconds();
	bs.answered = qfalse;
	Sync_Title( p, title, sizeof( title ) );
	Q_strncpyz( bs.sentTitle, title, sizeof( bs.sentTitle ) );
	CG_BoomboxSyncSend( BOX_DO_SYNC, box, title );
	return qtrue;
}

/*
===============================================================================

For the card (select/cg_select.c) and the wheel (radial/cg_radial.c)

===============================================================================
*/

// can this box be synced here: the server or the master takes it, this computer can take the sound, it is no TV
qboolean CG_BoxSyncOffered( int box ) {
	boomboxInfo_t info;

	return ( CG_BoomboxSyncs() && CG_BoomboxInfo( box, &info ) && info.kind != BOX_KIND_TV && CG_FeedSupported() ) ? qtrue : qfalse;
}

// every player of music is asked (ui/ui_music.c): while a choice is open or a sync runs
qboolean CG_BoxSyncWantsMusic( void ) {
	return ( bs.box >= 0 || bs.chooseBox >= 0 ) ? qtrue : qfalse;
}

// "Sync audioplayer": the players are asked, one that plays is taken by itself, of several he chooses (the card).
// first: the first of several is taken (the wheel).
void CG_BoxSyncChoose( int box, qboolean first ) {
	if ( !CG_BoxSyncOffered( box ) )
		return;
	bs.takeFirst = first;
	bs.chooseBox = box;
	bs.chooseTime = trap->Milliseconds();
	bs.decided = qfalse;
	bs.numChoices = 0;
}

// the players the card offers for box, -1: no choice of it is open, 0: none found yet
int CG_BoxSyncChoices( int box ) {
	return ( bs.chooseBox == box && bs.chooseBox >= 0 ) ? bs.numChoices : -1;
}

const char *CG_BoxSyncChoiceName( int index ) {
	static char text[192];

	if ( index < 0 || index >= bs.numChoices )
		return "";
	if ( bs.choices[index].now[0] )
		Com_sprintf( text, sizeof( text ), "%s: %s", bs.choices[index].name, bs.choices[index].now );
	else
		Q_strncpyz( text, bs.choices[index].name, sizeof( text ) );
	return text;
}

// the choice of the card: that player, -1 none (the choice is put away)
void CG_BoxSyncPick( int index ) {
	const int box = bs.chooseBox;

	bs.chooseBox = -1;
	if ( box >= 0 && index >= 0 && index < bs.numChoices )
		Sync_Start( box, &bs.choices[index] );
}

int CG_BoxSyncBox( void ) {
	return bs.box;
}

const char *CG_BoxSyncPlayerName( void ) {
	return ( bs.box >= 0 ) ? bs.player.name : "";
}

// pause, play or the next song, to the music player of the sync (MPRIS)
void CG_BoxSyncControl( const char *method ) {
	if ( bs.box >= 0 )
		Music_Control( bs.player.bus, method );
}

// "Stop sync": of this player's box here, of anybody else's at the server
void CG_BoxSyncStop( int box ) {
	if ( box == bs.box )
		Sync_End( qtrue );
	else
		CG_BoomboxSyncSend( BOX_DO_UNSYNC, box, NULL );
}

/*
===============================================================================

Once a frame

===============================================================================
*/

// the choice on the card: what plays, and one that plays alone is taken
static void Sync_ChoiceFrame( int now ) {
	if ( bs.chooseBox < 0 )
		return;
	if ( now - bs.chooseTime > SYNC_CHOOSE_MSEC || !CG_BoxSyncOffered( bs.chooseBox ) ) {
		bs.chooseBox = -1;
		return;
	}
	bs.numChoices = Sync_Playing( bs.choices, SYNC_CHOICES );
	if ( now - bs.chooseTime < SYNC_DECIDE_MSEC )
		return;
	// (once something was found: the first round of questions may not be through yet)
	if ( !bs.decided && bs.numChoices > 0 ) {
		bs.decided = qtrue;
		if ( bs.numChoices == 1 || ( bs.numChoices > 1 && bs.takeFirst ) ) {
			CG_BoxSyncPick( 0 );
			return;
		}
	}
	if ( !bs.numChoices && now - bs.chooseTime >= SYNC_NONE_MSEC ) {
		bs.chooseBox = -1;
		Sync_Say( CG_VisionText( "No music player plays anything right now. Start a song and try again." ) );
	}
}

void CG_BoxSyncFrame( void ) {
	const int			now = trap->Milliseconds();
	boomboxInfo_t		info;
	const musicLive_t	*live;
	int					count, i;

	Sync_ChoiceFrame( now );
	if ( bs.box < 0 )
		return;
	// the box is gone, the server no longer takes it, or somebody ended the sync (the server says why)
	if ( !CG_BoomboxInfo( bs.box, &info ) || !CG_BoomboxSyncs() ) {
		Sync_End( qfalse );
		return;
	}
	if ( info.synced ) {
		bs.answered = qtrue;
	} else if ( bs.answered || now - bs.startTime > SYNC_ANSWER_MSEC ) {
		Sync_End( qfalse );
		return;
	}
	// (not found for a while: at the start, or after its stream went; a move between two outputs is a moment)
	if ( CG_FeedFound() )
		bs.foundTime = now;
	else if ( now - bs.foundTime > SYNC_FIND_MSEC ) {
		Sync_Say( CG_VisionTextWith( "The sound of %s was not found, the sync ends", bs.player.name ) );
		Sync_End( qtrue );
		return;
	}
	// "Hear it from the box": the player plays into a sink of the game, heard only from the box (cg_voice_feed.c)
	CG_FeedRoute( (qboolean)( v_boxSyncHear.integer != 0 ) );
	CG_FeedFrame( now, Sync_Out );

	// what it plays now, and whether it is still there
	count = Music_Live( &live );
	for ( i = 0; i < count && strcmp( live[i].bus, bs.player.bus ); i++ )
		;
	if ( i < count ) {
		char title[BOX_TITLE];

		bs.seenTime = now;
		// (started again: same name on the bus, another process)
		if ( live[i].pid > 0 && live[i].pid != bs.player.pid ) {
			bs.player.pid = live[i].pid;
			CG_FeedPid( live[i].pid );
		}
		if ( live[i].now[0] )
			Q_strncpyz( bs.player.now, live[i].now, sizeof( bs.player.now ) );
		Sync_Title( &bs.player, title, sizeof( title ) );
		if ( strcmp( title, bs.sentTitle ) && now - bs.titleTime >= SYNC_TITLE_MSEC ) {
			Q_strncpyz( bs.sentTitle, title, sizeof( bs.sentTitle ) );
			bs.titleTime = now;
			CG_BoomboxSyncSend( BOX_DO_SYNCTITLE, bs.box, title );
		}
	} else if ( now - bs.seenTime > SYNC_GONE_MSEC ) {
		Sync_Say( CG_VisionTextWith( "%s is gone, the sync ends", bs.player.name ) );
		Sync_End( qtrue );
	}
}

// the module goes: the thread of the sound server runs code of it, so it ends first
void CG_BoxSyncShutdown( void ) {
	bs.box = bs.chooseBox = -1;
	CG_FeedShutdown();
}
