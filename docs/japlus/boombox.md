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
  the middle of the screen (2026-10-17, `cgame/boombox/cg_boombox_ui.c`, its drawing helpers are in `cgame/cg_window.c` now, shared with the tool window of [sandbox.md](sandbox.md)): who put it down, the song and who asked
  for it, a bar to jump in the song (click or drag it, for everybody), Pause or Resume, Skip, Stop, Mute (for you),
  the volume for everybody as a bar, the line of songs with a cross to take one out, Add a song, Move it and
  Take it away (click twice), and the last line the server had for you. The wheel moves a long line. Esc, the
  right button or its cross close it. On a server or master of 0.1.7 or 0.1.8 it shows only the next song and has
  no bar to jump with.
- **drag** a box over the floor to another place. It follows the pointer and turns to you, with a red outline
  where it can not be put (no floor, further than 320 units from your eye, another box too close).
  While a box, the table or a speaker of a sound system hangs in the pointer (dragged, or after "Move it"), each
  notch of the mouse wheel turns it 15 degrees. Until the first notch it turns to you as before, after it keeps the
  way you turned it.

"Play a song" opens the chat box with the prompt "Boombox, link or song": paste a link with Ctrl+V, or type what
to look for, and Enter. A link of `youtube.com`, `music.youtube.com`, `youtu.be` (also `/shorts/`, `/live/`,
`/embed/`) plays that video, anything else is looked up on YouTube and the first hit plays. Live streams and songs
longer than the server allows are refused, and the card and the window say why.

A link with `list=` is a playlist: `/playlist?list=`, a video in a playlist or a mix (`watch?v=...&list=...`, it
begins with that video), an album of YouTube Music. Its first 100 videos are read from the page of the list in
one lookup (`--flat-playlist`, no video is opened), what can not be played (taken down, private, live, too long)
is left out, and the rest goes into the line as far as it has room (`vs_boomboxQueue`). What does not fit waits
behind the line, up to 100 songs a box, and moves into it as songs end, are skipped or taken out. The window and
the card count those too; Stop takes them away with the line. Needs a server or masterserver of this version: an
older one plays only the video of the link (or says that a playlist link names no video).

The card says who put the box down and who asked for the song that plays. What a box does (a song put on or
added, skipped, stopped) is no longer written into the chat box of everybody near it (2026-10-17: "they're just
spamming"): the card and the window show it. A line the server has for you alone (what became of your song, why
it is not played) goes into the console and onto the card and the window for 8 seconds.

The console command does the same without the pointer:

```
boombox play "https://music.youtube.com/watch?v=..."    into the line of the box near you, or a new box in front of you
boombox play synthwave mix                               looked up on YouTube
boombox play                                             without words: opens the chat box for a song (as "Play" of the quick wheel)
boombox place                                            a new box where the crosshair points
boombox system                                           a new sound system where the crosshair points
boombox tv                                               a new TV where the crosshair points (see "The TV")
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

On every beat, when the song suddenly gets louder than it was a moment ago, each speaker sends out a shockwave.
It is a ring that runs out from the cone and bends what is behind it, like the air of a force push, while the
middle and the edge of the ring show the level as it is. It gets weaker as it grows and is gone in less than
half a second. The ring of a speaker of a sound system grows to about 60 units, the one of a boombox to about
half of that. This needs `cg_renderToTextureFX 1`, the game's own setting for such effects (on by default). It is
drawn right with the Vulkan renderer (rd-vulkan-vision). With rd-vanilla the ring can show the level a little
shifted.

A speaker is heard by the way it faces. In front of it you hear all of it, beside it about half. Behind a
speaker of a sound system you hear only about a tenth (12 %), behind a boombox a little under half (45 %). This
holds for the sound of the level (`v_spatial`) and for the voice device alike.

## The sound system

A second kind of box: a DJ table with two turntables, whose records turn while a song plays, and two big
speakers. The table and each speaker are put down and moved by themselves: drag any part with the pointer, or
pick "Move it" on its card, which moves the part the card was opened on. The mouse wheel turns the part in the
pointer, so a speaker can be aimed at the crowd (it is heard loudest in front of it, see "How it sounds"). Every part opens the same card and the
same window.

Its song comes in stereo. The left channel is heard from the left speaker, the right channel from the right one,
and Steam Audio (`v_spatial`) places each speaker in the level. Whoever is in range of either speaker hears both
channels. It costs twice what a boombox costs: two Opus streams of 64 kbit, about 140 kbit a second to every
player in range.

```
boombox system        the table where the crosshair points, turned to you, the left speaker at your left
                      and the right one at your right (76 units from the middle of the table), moved
                      closer to the table where there is no floor
```

It counts as one box against `vs_boomboxMax` (and against the 4 boxes of a room of the masterserver).

Only clients of version 0.2.4 or later are told of a sound system. An older client of this tree is not told of it
at all and does not hear it. It works on a server with the game module of this tree and through the
masterserver, but both need version 0.2.4 or later: an older server or master offers no sound system, and the
client says that the server is older.

The engine plays up to 16 box streams in `v_spatial` now (`v_spatialBoxes` 16, it was 8). With an older engine
the right speaker is played on the voice device, by side and distance.

## The quick wheel

Hold the key of `+v_radial`: a dark ring of nine slices stands in the middle of the screen. Move the mouse
toward a slice and let the key go, or click it. The picked slice is lit, the box it is about is named in the
middle, and what the slice does stands under the ring. The slice stays picked when the mouse comes back toward
the middle. A short tap of the key keeps the ring open until a click, or until the key is pressed again. Escape
or the right mouse button closes it.

Every choice has its slice for good. Clockwise from the top: Play (a song, or add one while one plays), Pause or
Resume, Skip, Louder, Player (opens the player window), Quieter. The two slices at the left depend on what the
crosshair (or the pointer, while the mouse is free) was on when the ring opened. On a boombox or any part of a
sound system they are "Move it" (that part) and "Remove" (takes it away, needs a second click, and the ring
stays open for it). Anywhere else they are "Boombox" and "Sound System", which put one down where the crosshair
points (Sound System only where the server or the master has them). The ninth slice, "TV", is after them. Anywhere else it puts a
TV down (see "The TV", only where the server or the master has them), on a box it is dark and does nothing.

The other choices are about the box the crosshair was on, or else the box near you. Louder and Quieter change
the volume for everybody, 10 a step.

No key is bound by default. Controls, Extras has the row "Quick Wheel (Boombox)".

## The TV

A third kind of box: an Imperial monitor of the game (`models/map_objects/imperial/monitor.md3`, drawn 1.25 times
its size). A TV plays songs as a boombox does, and shows the video of the song on its middle screen, in step with
the song. The side screens keep their Imperial displays. The colour of the screen lights the room around it.

### For players

Put a TV down with the quick wheel (the slice "Put a TV here") or with the console:

```
boombox tv            a TV where the crosshair points
```

Everything else is as with a boombox. Ask for a song with "Play a song", the card or the window, and the TV
plays it and shows its picture. Pause, Skip, a jump in the song, the volume, Move it and Take it away work the
same, and the picture follows them. It counts as one box against `vs_boomboxMax` (and against the 4 boxes of a
room of the masterserver).

The 4 TVs nearest to you that play get a picture. The others show a dark screen. A picture is only drawn up to
2048 units from your eye. If the picture is lost, it is fetched again after 10 seconds.

Only clients of version 0.2.9-dev.29 or later see a TV. An older client of this tree does not see it at all. It
works on any server: a server with the game module of this tree keeps the TV itself, on every other one the
masterserver keeps it. The game module and the masterserver must be dev.29 or later too. The browser
page (/watch/) does not show TV pictures.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_tvService` | `https://jka.vision/tv/` | where the pictures come from. Protected: only a config of the home folder or the console sets it, a server can not. It takes https, or http to 127.0.0.1 for tests |

### For masterserver owners

The picture is made by the TV service of the masterserver (`codemp/master/ms_tv.c`). It is an HTTP server on
127.0.0.1, and the web server of the site hands `/tv/` to it over HTTPS (nginx: 30 requests a minute and 4
connections per address). It needs `yt-dlp` and `ffmpeg` like the songs do.

| Option | Default | Meaning |
|--------|---------|---------|
| `--tv-port <port>` | 0 (no TV service) | the port on 127.0.0.1 |
| `--tv-sessions <n>` | 4 (16 at most) | videos made at once |

### How it works

`codemp/cgame/boombox/cg_tv.c` (the box), `codemp/client/cl_video.cpp` (fetch and decode),
`codemp/master/ms_tv.c` (the service), the protocol in `game/boombox/bg_boombox.h`.

The request is `GET /tv/<video id>?t=<second>`. The answer is an MPEG-1 stream (video only) of 426x240 at 25 fps
and about 700 kbit/s, which `yt-dlp` and `ffmpeg` make as it is read (`Box_VideoStart()` in
`game/boombox/bg_boombox_fetch.c`). A bad request is answered 400, an unknown path 404. The sound is not in it: it
is the song of the box, which plays as it plays for any boombox.

A session is one video from one second on. Viewers of the same video and second share one session. One address
may have 4 connections, a session nobody watches ends after 15 seconds, and the stream is read no more than 12
seconds ahead of the clock, so a session holds a few seconds and not the whole video. At most `--tv-sessions`
sessions run at once. Nothing there waits: the sockets do not block and are in the `select()` of the program.

The client fetches the stream in a background thread (`v_tvService`) and decodes it with pl_mpeg (`lib/pl_mpeg`,
MIT). It refuses a picture larger than 640x360 and more than 4 MB waiting. The picture is drawn into the middle
screen of the model, one of 4 streams (one per TV that gets a picture). A picture starts 2 seconds ahead of the
song and is shown when the song reaches it. After a jump in the song, a stall, or when the picture falls 3 seconds
behind, it is opened again further in. After a failure it tries again after 10 seconds. The light is a dynamic
light in front of the screen, in the colour of the picture and as strong as the picture is bright.

On a server with the game module the TV is a box of the kind 2 (`BOX_KIND_TV`): `vs_boxaction tv x y z yaw`
puts it down, and `vs_box set` ends with the kind 2, eight zeros and the video id. Through the masterserver
the client sends `BOX_DO_TV`, the master tells the video id with `BOX_SAY_VIDEO` after `BOX_SAY_SET` and says that
a box is a TV in the state byte (`BOX_STATE_TVS`), and it says `BOX_CAP_TV` in its hello.

### Tested, not tested

2026-10-31, hidden and silent, on Linux: a local server with the game module, one client, a TV put down with
`v_boombox tv`, a song asked for with fake `yt-dlp` and `ffmpeg` programs, and the local master serving `/tv/`.
The middle screen showed the moving test picture in step with the song. The `/tv/` server was run with a script:
errors for bad requests (400, 404), and two viewers of the same video and second shared one session. The new
packets have unit tests (`tv_packets` in `tests/vjk/boombox.cpp`).

Not tested: the masterserver path in a real game (a server without the game module), real YouTube videos (this
machine has no yt-dlp), Windows, the light in a real game, and nginx on the host before the deploy.

## For server owners

The server fetches every song itself and needs three programs for it, in the `PATH` of the server or named by
the cvars below:

- `yt-dlp`, recent: YouTube changes often, an old one fails. The package of a distribution is often too old,
  the release binary of yt-dlp updates itself (`yt-dlp -U`).
- a JavaScript runtime for yt-dlp: `deno` (what yt-dlp looks for by default), or another one named in
  `vs_boomboxYtdlpArgs` (`--js-runtimes node`).
- `ffmpeg`, with https support (the packages of Debian, Ubuntu and Arch have it): it reads a song from its address
  itself.

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
| `vs_boomboxMaxSeconds` | 600 | the longest song, in seconds. 0: any length, but a song of more than ten hours is not played whatever this says (the samples of a song are counted in an int) |
| `vs_boomboxQueue` | 10 | songs in the line of one box (16 at most) |
| `vs_boomboxYtdlp` | yt-dlp | the program, a name in the `PATH` or a path |
| `vs_boomboxFfmpeg` | ffmpeg | the same |
| `vs_boomboxYtdlpArgs` | | words for yt-dlp in addition, split at spaces: `--js-runtimes node`, `--cookies <file>`, `--proxy <url>` (a song then always goes through the pipe of yt-dlp). With `--cookies` mind that a playlist id must have 12 characters at least and may not begin with `RDMM`: the lists of an account (`WL`, `LL`, `LM`, `RDMM`) would be reachable for every player |

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

The names on the card of a box there (who placed it, who asked for a song) are what the client sends: the master
has no other source.

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

A song: `yt-dlp` looks it up first (`--print` of its id, length, whether it is live, title, the address of the
sound of the video and the User-Agent it wants: nothing is fetched). The address is kept for 2 hours (32 songs at
most). When the turn of the song comes, `ffmpeg` reads it from that address itself, with `-ss` in front of `-i`
and `-reconnect`, and makes one channel of 16 bit at 48 kHz of it (`game/boombox/bg_boombox_fetch.c`). So the
start of a song after its lookup usually needs no yt-dlp at all. Without a kept address (a song of a playlist,
more than 2 hours after the lookup) yt-dlp looks up only the address (`--print url`) first. An error of yt-dlp
there (video gone, private) ends the song with that reason.

An address only goes to ffmpeg when it is https on a host of `googlevideo.com` and made of plain characters, and
ffmpeg gets `-protocol_whitelist https,tls,tcp`, so it can open nothing else (no files, no other protocol).
yt-dlp prints whatever it is told, and ffmpeg would open files or any address.

The old way stays as the fallback: `yt-dlp -o -` fetches the audio and hands it to `ffmpeg`. It is used when there
is no usable address, when ffmpeg got nothing from the address (it expired, or this ffmpeg has no https), and
always when `vs_boomboxYtdlpArgs` has `--proxy` (an address is only good for the network that asked for it, and
ffmpeg would not use the proxy). The programs run in a process group of
their own, started by a go-between that ends at once, never through a shell, and with pipes that do not block:
nothing of the server waits for them. A stopped song gets `SIGTERM` to that group first and `SIGKILL` 2 seconds later
to what is still there (the one-file build of yt-dlp removes the folder it unpacks itself into in `/tmp` only when
it may end; with `SIGKILL` alone one folder was left per song). yt-dlp is started with `--no-plugin-dirs` as well
(no plugins from the home folder or next to the program), which needs a yt-dlp of 2025 or newer. A skip or a seek
restarts the programs of a song, so a box takes one of those a second at most, whoever asks (game server and
masterserver). A line of yt-dlp that names a path, cookies, a proxy or an address is not shown to players, they get
the general reason; `vs_boxes` still shows the line to the owner. Of what a player typed only the
address of a YouTube video, made anew from its eleven characters, the address of a playlist made anew from its
id (with the video it begins with), or `ytsearch1:` with the words reaches yt-dlp, behind `--`: any other address
is refused, so the server never fetches an address inside its own network.

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

A jump (`Box_Seek()`) begins the programs of the song again at that second: `ffmpeg` asks the address only for
that part of the file (an HTTP range), so a jump far into a song waits no longer than one to its start, and so
does the restart of a fetch that broke off. Through the pipe (the fallback) `ffmpeg` still decodes what is
before and drops it, and the wait it is given before the song is skipped grows with the jump.

## Not done, not checked

- 2026-10-17, hidden and silent runs on Linux (`uitest/boxui.sh` of the test tools, a server of this tree with
  yt-dlp, one client): three songs looked up and put into the line, the card with its new lines, the window, a
  jump into the middle of the song, pause. Nobody has heard a song (the runs have no sound), the masterserver
  path of the window (watch, list, seek, drop) was never run, nor two clients at one box, nor a cross in the
  line clicked.
- 2026-10-30, the fetch from the address of the sound (#8): unit tests with scripts in place of yt-dlp and ffmpeg
  (tests/vjk/boombox.cpp), and the real ffmpeg 5.1 with the same arguments against a local https server, which
  it asked for the middle of the file for a start at 60 s. Never run against YouTube itself (no yt-dlp here), and
  no song was heard that way.
- Before 2026-10-17 nothing of it was run. The model was looked at in a picture the files were drawn into by a
  small script, never in the game.
- Windows: the server can not fetch songs (no process code for it yet), the client is not built for it.
- The masterserver part was never run either. A client there can put a box anywhere in the room of its own
  server (the master has no level to check it against).
- The browser client: the code is the same, the songs would come through the web host of the server as the
  voices do; not built (the browser client is off).
- One channel: a box is one point in the level.
