# Boombox

A box in the level that plays songs of YouTube and YouTube Music, for everybody near it. It is not a JA+
feature. Only players with the client of this tree see the box and hear it, on any server: a server with the game
module of this tree keeps its boxes itself, on every other one the masterserver keeps them for its players (see
"Through the masterserver"). A client of any other kind (stock jamp, the JA+ plugin, another fork) is told nothing of it: it sees no
box, hears nothing, and its players walk through the box as everybody does (it is no entity and not solid).

Everybody whose client draws boxes may do everything with every box.

## For players

Hold the key of the pointer (`+v_cursor`, the left Alt key by default) and:

- **click** a box for its card: what it plays and how far into the song it is, what comes next, and the rows
  Play a song (Add a song while one plays), Pause or Resume, Skip this song, Stop the music, Quieter and Louder
  for everybody, Mute it for me, Move it, Take it away (click twice). The rows also go by their number keys.
- **Open the player** (the first row of the card, or `boombox window` near a box) opens the window of the box in
  the middle of the screen (2026-10-17, `cgame/boombox/cg_boombox_ui.c`): who put it down, the song and who asked
  for it, a bar to jump in the song (click or drag it, for everybody), Pause or Resume, Skip, Stop, Mute (for you),
  the volume for everybody as a bar, the line of songs with a cross to take one out, Add a song, Move it and
  Take it away (click twice), and the last line the server had for you. The wheel moves a long line. Esc, the
  right button or its cross close it. On a server or master of 0.1.7 or 0.1.8 it shows only the next song and has
  no bar to jump with.
- **drag** a box over the floor to another place. It follows the pointer and turns to you, with a red outline
  where it can not be put (no floor, further than 320 units from your eye, another box too close).

"Play a song" opens the chat box with the prompt "Boombox, link or song": paste a link with Ctrl+V, or type what
to look for, and Enter. A link of `youtube.com`, `music.youtube.com`, `youtu.be` (also `/shorts/`, `/live/`,
`/embed/`) plays that video, anything else is looked up on YouTube and the first hit plays. Live streams and songs
longer than the server allows are refused, and the card and the window say why.

The card says who put the box down and who asked for the song that plays. What a box does (a song put on or
added, skipped, stopped) is no longer written into the chat box of everybody near it (2026-10-17: "they're just
spamming"): the card and the window show it. A line the server has for you alone (what became of your song, why
it is not played) goes into the console and onto the card and the window for 8 seconds.

The console command does the same without the pointer:

```
boombox play "https://music.youtube.com/watch?v=..."    into the line of the box near you, or a new box in front of you
boombox play synthwave mix                               looked up on YouTube
boombox place                                            a new box where the crosshair points
boombox pause | resume | skip | stop | remove [box]      the box near you, or that number
boombox volume <0 to 100> [box]                          for everybody
boombox window [box]                                     the window of the box near you, or of that number
boombox list                                             the boxes and their lines, in the console
```

A link typed into the console needs quotes: the console ends a command at `//`. The command says so when that
happened. The chat box needs no quotes.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_boomboxes` | 1 | 0: no boxes, nothing of them heard, the server stops sending |
| `v_boomboxVolume` | 0.6 | how loud the songs are for you, 0 to 1. Also in Setup, Sound |

Both are in Setup, Sound, under Boombox. With the voice chat switched off (`v_voice 0`) the songs still come:
the way to the relay is opened for them alone, without the microphone.

### How it sounds

With this engine and `v_spatial 1` ([audio.md](audio.md)) a box is a sound of the level: walls muffle it, it
finds its way round corners, the HRTF or the panning gives it its side, and the room you stand in rings from it
(the reverb is the one of the listener, as for every other sound). The engine says so in `v_spatialBoxes`.
Elsewhere it plays on the sound device of the voice chat, by side and distance, muffled behind a wall
(`v_voiceMuffle`). Either way it gets quieter with the distance and is gone at the end of the range of the
server (`vs_boomboxRange`, 1536 units).

The two speakers of the box push out with the loudness of what you hear of it.

## For server owners

The server fetches every song itself and needs three programs for it, in the `PATH` of the server or named by
the cvars below:

- `yt-dlp`, recent: YouTube changes often, an old one fails. The package of a distribution is often too old,
  the release binary of yt-dlp updates itself (`yt-dlp -U`).
- a JavaScript runtime for yt-dlp: `deno` (what yt-dlp looks for by default), or another one named in
  `vs_boomboxYtdlpArgs` (`--js-runtimes node`).
- `ffmpeg`.

```
apt install ffmpeg            # Debian
yay -S yt-dlp deno            # Arch
```

Without yt-dlp the boxes are still there, and a player who asks for a song is told that the server can not fetch
songs. The boombox needs the voice chat (`vs_voice 1`): the songs go out over its relay. Windows servers can not
fetch songs yet.

| Cvar | Default | Meaning |
|------|---------|---------|
| `vs_boombox` | 1 | in the server info. 0: no boxes |
| `vs_boomboxMax` | 4 | boxes a level has at most (8 at most) |
| `vs_boomboxRange` | 1536 | how far a box is heard, in units |
| `vs_boomboxMaxSeconds` | 600 | the longest song, in seconds. 0: any length |
| `vs_boomboxQueue` | 10 | songs in the line of one box (16 at most) |
| `vs_boomboxYtdlp` | yt-dlp | the program, a name in the `PATH` or a path |
| `vs_boomboxFfmpeg` | ffmpeg | the same |
| `vs_boomboxYtdlpArgs` | | words for yt-dlp in addition, split at spaces: `--js-runtimes node`, `--cookies <file>`, `--proxy <url>` |

The last three can only be set while the server starts (`CVAR_INIT`: in `server.cfg` or on the command line):
whoever can set a cvar later (rcon, an admin of the panel) can not make the server start a program of his choice.

The server command `vs_boxes` lists the boxes, their lines and the last word of yt-dlp and ffmpeg for a song
that plays. `vs_boxes clear` takes every box away.

A song costs the server one Opus encoder (64 kbit, one channel) and about 70 kbit a second to every player in
range of the box. Boxes go with the level; a `map_restart` keeps them.

Playing YouTube on a server is your call: the terms of YouTube allow its videos only in its own player, and
music played to the public can need the rights for it.

## Through the masterserver

On a server without the game module of this tree the masterserver keeps the boxes of that server
(`codemp/master/ms_boombox.c`), up to four, and plays their songs. A player sees no difference, but for three:
what is put on is announced only to the players near the box, the masterserver plays a few songs at once for all
servers together (a song waits when they all play), and it does not know the level, so a box stands where the
client of the player found a floor. It needs the voice link to the masterserver (`v_voiceMaster 1`, the
default), also with the voice chat switched off.

| Cvar of the masterserver | Default | Meaning |
|------|---------|---------|
| `vs_masterBoxes` | 8 | songs it plays at once for all servers, 16 at most. 0: no boxes |

It starts the programs the owner named for its game module (`vs_boomboxYtdlp`, `vs_boomboxFfmpeg`,
`vs_boomboxYtdlpArgs`) and takes its limits (`vs_boomboxMaxSeconds`, `vs_boomboxQueue`), once a second. Its line of
numbers says once a minute how many rooms have boxes and how many songs play.

## How it works

`codemp/game/boombox` (server), `codemp/cgame/boombox` (client), the protocol in `game/boombox/bg_boombox.h`.

A box is no entity. A cgame of this tree on a server that says `vs_boombox` asks `vs_box hello` and is answered
`vs_box on`, the state of every box (`vs_box set`), every change of it, and `vs_box gone`; lines for the player
come as `vs_box note`. Since 0.1.9 `vs_box on` says `BOX_CAP_MORE` in a fifth word, `vs_box set` ends with who
put the box down and who asked for the song (a cgame of 0.1.7 or 0.1.8 reads no further), and a client that
watches a box (`vs_boxaction watch`, while its window is open) gets its line as `vs_box list` whenever it
changes. `vs_boxaction seek` and `drop` jump in the song and take a waiting one out. Nothing of it goes to a client that did not ask. What a player does is the client command
`vs_boxaction`, of which the cgame sends one a second at most (the flood protection of the server drops a command
that comes within a second of another one); five clicks on Louder go out as one.

A song: `yt-dlp` looks it up first (`--print` of its id, length, whether it is live, and title: nothing is
fetched), and when its turn comes `yt-dlp -o -` fetches the audio and hands it to `ffmpeg`, which makes one
channel of 16 bit at 48 kHz of it (`game/boombox/bg_boombox_fetch.c`). The programs run in a process group of
their own, started by a go-between that ends at once, never through a shell, and with pipes that do not block:
nothing of the server waits for them. A stopped song is one kill of that group. Of what a player typed only the
address of a YouTube video, made anew from its eleven characters, or `ytsearch1:` with the words reaches yt-dlp,
behind `--`: any other address is refused, so the server never fetches an address inside its own network.

The server codes 20 ms of the song at a time with Opus as they are due by the clock and sends each frame to every
player in range, over the relay of the voice chat, as the voice of speaker `MAX_CLIENTS` + the number of the box
(`G_VoiceSendBox`, a `VOICE_AUDIO` datagram as [voice.md](voice.md) has it). Everybody hears the same moment. A
song that is fetched too slowly waits for everybody, a fetch that breaks off is begun again where it was (three
times a song). The cgame keeps 120 ms of a song before it plays it (the voices: 60).

The game module and the masterserver play the songs with the same code (`game/boombox/bg_boombox_core.c`): the
line of a box, the lookups, the pacing and the encoder. Each is its host (`boxHost_t`) and says how its clients
are told and who hears a frame.

Through the masterserver everything goes over the way of the voice chat to it ([voice.md](voice.md), "The
boombox"): a master that plays boxes says so in every answer (`VOICE_FLAG_BOXES`), and only a client that read
it sends the state with one more byte (the boxes it hears, which the client works out from where the boxes
stand and the range, 1536) and `VOICE_BOX` datagrams with what its player does and his name. The master answers
with the state of a box, its end, or a line for the player, at once and every box again every two seconds.
Since 0.1.9 a master says `VOICE_FLAG_BOXLIST` too and sends `BOX_SAY_INFO` (who put it down, who asked for the
song) after every `BOX_SAY_SET`, which itself never grows (a client of 0.1.7 or 0.1.8 checks its length to the
byte and drops what it does not know), and the line of a watched box as `BOX_SAY_LIST`, a song a datagram, again
every two seconds. `BOX_DO_SEEK`, `BOX_DO_DROP` and `BOX_DO_WATCH` come from the client. The bytes are in
`game/boombox/bg_boombox.h`, packed and checked in `bg_boombox.c`.

A jump (`Box_Seek()`) begins the programs of the song again at that second: `ffmpeg` decodes what is before and
drops it, so the further in, the longer the song waits for its start (the wait it is given before it is skipped
grows with the jump). In the hidden run of 2026-10-17 a jump to 3:23 of a song of 6:46 played again within a few
seconds.

## Not done, not checked

- 2026-10-17, hidden and silent runs on Linux (`uitest/boxui.sh` of the test tools, a server of this tree with
  yt-dlp, one client): three songs looked up and put into the line, the card with its new lines, the window, a
  jump into the middle of the song, pause. Nobody has heard a song (the runs have no sound), the masterserver
  path of the window (watch, list, seek, drop) was never run, nor two clients at one box, nor a cross in the
  line clicked.
- Before 2026-10-17 nothing of it was run. The model was looked at in a picture the files were drawn into by a
  small script, never in the game.
- Windows: the server can not fetch songs (no process code for it yet), the client is not built for it.
- The masterserver part was never run either. A client there can put a box anywhere in the room of its own
  server (the master has no level to check it against).
- The browser client: the code is the same, the songs would come through the web host of the server as the
  voices do; not built (the browser client is off).
- One channel: a box is one point in the level.
