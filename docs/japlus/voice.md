# Voice chat

An in-game voice chat with the microphone. It is not a JA+ feature. It needs the cgame of this tree on the
client. A server with the game module of this tree relays the voices itself. On any other server they go
through the masterserver, one relay for all of those, see "On a server without it". The engine is not changed:
it works with a 64 bit OpenJK. A stock `jamp.exe` is 32 bit and can not load this cgame, the tree is built for
64 bit only.

A client without this cgame (stock jamp, the original JA+ plugin, another fork) is not touched by any of it. It
joins and plays as before, it just does not hear the voice chat. See "Clients without it".

The client that runs in a browser ([web.md](web.md)) has the voice chat too, with the same code and through the
web host of the dedicated server of this tree. See "In a browser".

## For players

```
bind v +v_voice
```

Hold the key to talk. What the others say is heard without a key. The key is also in the menu under Controls,
JA+ Control, and the settings a player needs most are in Setup under Voice Chat, in the main menu and in the
game (the menus are in `assets/japlus_client/ui/jamp`, the ui module registers the cvars so they are there
before a level was loaded). The same tab has the two switches of the chat above the heads, "Chat Bubbles"
(`v_chatBubbles`) and "Show My Music" (`v_shareMusic`): twelve rows, which is what the tab of the main menu
has room for.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_voice` | 1 | 0: no voice chat, nothing is opened and the server is not asked |
| `v_voiceMode` | 1 | 1: push to talk (`+v_voice`). 2: voice activation, whatever is louder than `v_voiceThreshold` is sent |
| `v_voiceVolume` | 1 | how loud the others are. `s_volume` does not change them |
| `v_voiceMicGain` | 1 | how loud the own microphone is sent |
| `v_voiceThreshold` | 0.02 | voice activation: from which level on, 0 to 1 |
| `v_voiceMuffle` | 1 | 0: voices behind walls are not muffled, and the engine's `v_spatial` leaves them alone |
| `v_voiceOwnReverb` | 1 | with the engine's `v_spatial`: how loud the room rings from what you say yourself. 0: not at all, 0.5: half, 2: twice. A slider in the setup menus. At 1 it is half as loud as it was until 2026-10-11. See [audio.md](audio.md) |
| `v_voiceDrawTalkers` | 1 | the list of who is talking on the HUD |
| `v_voiceCapture`, `v_voicePlayback` | "" | the name of the microphone and of the speakers, empty: the ones the system uses. `v_voice_devices` lists the names |
| `v_voiceBackend` | "" | `null`: a sound system that plays into nothing, for tests |
| `v_voiceRadioFX` | 1 | the clan radio sounds like a radio: only the band of a telephone, a little rough, with a click at both ends. 0: as clear as any voice |
| `v_voiceMaster` | 1 | 0: no voice chat on servers that have none of their own. 1: there it goes through the masterserver |
| `v_master` | master.jka.vision | the masterserver, a name or an address, with `:port` if it is not 29170. Empty: none. The one address for the voice, the intrusion detection and the store changes. It was `v_voiceMasterHost` (and `cg_voiceMasterHost`) and `v_idsMaster`, and their values are taken over. Up to version 0.1.6 the default was voice.jka.vision, and every player from then has that name stored in his config: both names have to lead to the masterserver for good |

| Command | |
|---------|---|
| `+v_voice` | talk while it is held |
| `+v_voiceclan` | talk to the clan over the radio while it is held, see "The clan radio" |
| `v_voice_devices` | the microphones and speakers there are |
| `v_voice_status` | is the chat connected, do the speakers play, who talks, who is muted, how many frames of each player were heard |
| `v_voice_mute <player>`, `v_voice_unmute <player or all>` | do not hear one player. Holds until the level changes |
| `v_voice_test tone`, `v_voice_test play <name>`, `v_voice_test stop` | a tone, or the file `voice/<name>.raw` again and again, in place of the microphone. For tests |
| `v_voice_test record <name>` | what is played also goes into `voice/<name>.raw`, until `v_voice_test stop`. For tests |

The files of `v_voice_test` are 16 bit, two channels, 48000 samples a second, low byte first, without a header:
what was recorded can be played, and `ffmpeg -i in.mp3 -ac 2 -ar 48000 -f s16le voice/name.raw` makes one from any
sound. On a pure server the engine lets a module read such a file only out of a pk3 the server has too.

With push to talk the microphone is opened at the first press of the key, which takes a moment that once, and is
closed 30 seconds after the last press. A player who never presses the key never has his microphone open. With
voice activation it is open all the time.

Who talks has the voice sprite of the game over his head and is in a list at the left of the HUD.

Use headphones. There is no echo cancellation: with speakers the others hear themselves.

### The clan radio

```
bind b +v_voiceclan
```

Since 2026-10-11. Hold the key and what you say goes to the players of your clan and to nobody else, wherever
they are on the map and whatever team they are on. Your clan are the players with the same clan password as
you (`cp_clanPwd` in the client, or `/clanpass` in the game): the ones your clan chat reaches.

- A voice from the radio has no place. It is in both ears, as loud for everybody, is not muffled by walls and
  puts no mark over the head of who talks: it says nothing about where he is. In the list of who talks it has
  the mark of a radio.
- It sounds like a radio (`v_voiceRadioFX`): the band from 300 to 3400 Hz, pushed until its peaks are rounded
  off, a little hiss, a crack when the key is pressed and a rush that is cut off when it is let go. The player
  who talks hears the two clicks of his own key.
- The radio is always by its key, also with voice activation. With both keys held it is the radio.
- The room still rings from what you say into the radio (`v_voiceOwnReverb`): you say it aloud where you
  stand. Players next to you who are not in your clan do not hear it.
- The key is in the menu under Controls, JA+ Control, the switch for the sound in Setup under Voice Chat.
- On a server without the game module of this tree the radio goes through the masterserver (since
  2026-10-11, see "On a server without it"): there your clan are the players of that server with your clan
  password, whatever the server itself knows of clans.
- Not on a server with `vs_voiceRadio 0`: the key then says so once.

## For the server

| Cvar | Default | Meaning |
|------|---------|---------|
| `vs_voice` | 1 | 0: no voice chat, no port is opened (latched: from the next level on). It is in the server info |
| `vs_voicePort` | 0 | a UDP port of its own for the relay (latched). 0: none where the engine takes the voice chat on the port of the game (`vs_voiceShared`), else the game port plus 1 |
| `vs_voiceShared` | 1 | of the engine, command line only. 1: the voice chat comes in on the UDP port of the game and leaves from it, the server needs no second port. The dedicated server of this tree has 1, a game a player starts from the menu 0. 0: the relay opens a port of its own |
| `vs_voiceSharedGame` | | read only, set by the game module: 1 while its relay takes the datagrams from the engine |
| `vs_voiceRange` | 1024 | how far a voice carries, in units. 0: all over the level, without direction and without muffling |
| `vs_voiceTeamOnly` | 0 | 1: in team games only the own team hears a player |
| `vs_voiceRadio` | 1 | 0: no clan radio. With 1 a frame a client sends for its clan goes to the players with the same clan password, whatever `vs_voiceRange` and `vs_voiceTeamOnly` say. A silenced player has no radio either |
| `vs_voiceKbps` | 6000 | what the relay may send in a second, in kbit/s with the IPv4 and UDP headers counted. 0: no cap. What one speaker says goes to everybody near him, a datagram each, so a few speakers at the limit of a speaker could fill the upload of a server at home. Datagrams not sent for it are counted in the status line of the voice chat ("not sent for vs_voiceKbps") |
| `vs_voiceRelay` | | read only, set by the game module: where the relay listens for the web host of the same server, `address:port`. Empty: no relay |

The voice chat is on unless a server switches it off (since 2026-10-07, before that `vs_voice` was 0). A server
that has `vs_voice 0` stored in its config keeps that.

**One port (since 2026-10-12).** On the dedicated server of this tree the voice chat has no port of its own: its
datagrams come in on the UDP port of the game and the answers leave from it. A client is told the port in the
answer to `vs_voiceLink hello`, as before, so one from before this change follows by itself. Nothing has to be
forwarded for the voice chat any more, and the port after the game port is free.

The relay opens a port of its own, the game port plus 1 unless `vs_voicePort` names one, in three cases: the
game module runs in another engine (the original dedicated server, another fork), a player started the game from
the menu, or the owner set `vs_voiceShared 0` or a `vs_voicePort`. That port has to be reachable from outside
like the game port: UDP, forwarded and open in the firewall. If it can not be opened the server says so in one
line and runs without voice.

How the engine tells the two apart (`server/sv_voice.cpp`, `SV_PacketEvent()`): a datagram of the voice chat
starts with `VO` and the version of its protocol. A packet of the game starts with its sequence number, and one
of them has the same four bytes: number 85846 of every client. So the server first looks whether a packet comes
from the address and port of one of its clients with his qport, and only what does not is read as voice. Only
after that it looks for a client by address and qport alone (a client whose router gave him a new port), since
bytes 4 and 5 of a voice datagram would otherwise be read as a qport. The engine hands a datagram to the game
module through an addition to the module interface that neither side touches unless the other one says so in a
cvar (`ext` in `game/g_public.h`), so the module runs in any other engine as before.

A server without this game module can say no to the voice chat as well: `sets vs_voice 0` puts the key into
its server info, and the cgame then leaves the masterserver alone there.

`vs_voicestatus` on the server console lists who has voice, how many datagrams of each were handed on, and how many
were refused.

A spectator is heard by spectators only, wherever they are. A spectator hears the players near the spot he
watches from. A player an admin silenced (`amsilence`) is not heard.

## How it works

The engine gives a module no microphone, no way to play samples it made itself and no channel for sound, so the
cgame does all three on its own.

**Asking.** A server with the voice chat says `vs_voice` in its server info. Only there the cgame sends the client
command `vs_voiceLink hello <protocol>`. The server answers with the server command
`vs_voiceLink <protocol> <port> <token> <range>`. The token is 32 random bits from the random source of the system,
new for every connection. This exchange is all of the voice chat that goes through the game connection. The
cgame asks again every three seconds, ten times at most, until the answer is there: the flood protection of the
server drops a client command that comes within a second of another one.

**The sound.** The cgame opens a UDP socket of its own to that port. A datagram, low byte first:

```
to the server   "VO" version type client  token[4] sequence[2]                 frame
to a client     "VO" version type speaker token[4] sequence[2] flags distance  frame
```

A frame is 20 ms of sound, 48000 samples a second, one channel, coded with Opus at about 32 kbit a second, with
a small copy of the frame before it in each: about 4.5 kB a second for a speaker. Until 2026-10-08 it was 24000
samples and 20 kbit, at which Opus gives up everything above 8 kHz to pay for the copy (read from its source,
`decide_fec`). The rate is no part of the protocol: a frame of Opus is 20 ms at any rate, so a client with the
old numbers and one with the new ones understand each other. The type is sound or hello. A
hello carries nothing, it tells the server where the client listens (through his router) and is answered.

**The relay** (`g_voice.c`) is polled with every server frame and with every move a client sends, so a voice does
not wait for the next server frame. It takes at most 64 datagrams a call and never waits. It does not decode the
sound. A datagram counts if it is whole, names a connected client, carries his token and comes from the address
his game connection comes from. It then goes to every client who is to hear it (`BG_VoiceHears`), with that
client's token, a flag if the server does not show him the speaker at all, and the distance in steps of 16
units. No coordinates are sent: a voice must not tell where somebody stands behind a wall. From a client it
takes only HELLO, AUDIO and RADIO datagrams. (It refused some types before and passed the rest on with their
type.)

**The listener** (`cg_voice.c`) takes datagrams only from the address and port of the server and with his own
token. The frames of a speaker wait in a queue by their sequence number. He starts to play when 60 ms of him
wait, so a datagram that comes late still finds its place. A lost frame is rebuilt from the copy in the next
one, or guessed by the codec. If his voice runs dry while he talks, more of it waits the next time.

**Where a voice comes from.** With a range a voice gets quieter with the distance and comes from the side its
speaker is on. Ten times a second one line is traced from the eye of the listener to the head of the speaker,
against the level and its doors. If something is in the way, or one of the two is under water and the other is
not, the voice is muffled: two low-passes at about 700 Hz and a little quieter, faded in over 150 ms. A speaker
the server does not show at all is muffled and comes from no direction.

With the engine of this tree and `v_spatial 1` the cgame does none of that for a speaker it can see: it hands
his voice to the engine, where it is a sound in the level, behind the walls and in the reverb of every other
sound. See [audio.md](audio.md), "The voice chat". With `v_voiceMuffle 0`, on any other engine and with
`v_spatial 0` every voice is played as described here.

**The mouth.** The game opens the mouth of a character by how loud the line is that he says, in four steps,
which the engine works out for its own sounds. A voice of the chat is none of those, so the cgame works out the
same from every frame it plays of a speaker, and for the own character from every frame that goes out
(`Voice_Lip`, asked by `CG_G2PlayerHeadAnims`): how loud the frame is against the loudest of that player lately,
since no two microphones are alike. A line of the game on his voice channel goes first. The mouth is ahead of
the voice by what waits to be played, 40 ms and more. Added 2026-10-08, built, not seen in the game.

**The sound devices** (`cg_voice_audio.c`) are opened through miniaudio (`lib/miniaudio`), which is compiled into
that one file, on Windows, Linux and macOS. The codec is Opus (`lib/opus`). Both are built into the module,
there is nothing a player has to install. The devices run in threads of miniaudio. Those threads touch nothing
but ring buffers with one writer and one reader, and three numbers per voice. Everything else runs in the thread
of the game, once a frame, and no call there waits: the socket does not block, and a server name that has to be
looked up (jamp only keeps what the player typed) is looked up in a thread of its own. The states of the codec,
the rings and the queues are static memory. `CG_Shutdown` stops the devices before the module is unloaded.

| File | Content |
|------|---------|
| `game/voice/bg_voice.h`, `bg_voice.c` | the datagram, the queue, the limit, who hears whom, the low-pass. Calls nothing of the engine |
| `game/bg/bg_net.h` | the sockets as the modules use them, shared with the web admin panel, and the random source |
| `game/voice/g_voice.c` | the relay |
| `cgame/voice/cg_voice.c` | the client: asking, socket, codec, queues, the key, where a voice comes from, HUD |
| `cgame/voice/cg_voice_audio.c`, `cg_voice.h` | microphone and speakers |

| `cgame/voice/cg_voice_web.c` | in a browser: the WebSocket to the web host in place of the UDP socket |
| `server/web/sv_web_voice.cpp` | in the dedicated server: `/voice` of the web host, between that WebSocket and the relay |
| `server/sv_voice.cpp` | in the engine: the voice chat on the UDP port of the game |
| `cgame/status/cg_status.c` | the statuses above the heads, which go through the masterserver where the server passes none on |

`-DBuildJPVoice=OFF` builds the cgame without it (and without the two libraries).

## The boombox

The relay also carries the songs of the boombox ([boombox.md](boombox.md)): the server codes them itself and
sends them as `RELAY_AUDIO` with a speaker number behind the players, `MAX_CLIENTS` + the box, only to clients
that asked for boxes (`vs_box hello`). A cgame from before drops a speaker number that is no client number. A
client whose voice chat is off (`v_voice 0`) still opens the way to the relay for the songs, without the
microphone, and drops the voices.

On a server without the game module the masterserver plays the boxes: the state to it may carry one more byte,
the boxes the client hears, and `RELAY_BOX` (type 6) carries what a player does with a box and what a box is now,
for clients and masters that said they know it (`VOICE_FLAG_BOXES`). See [boombox.md](boombox.md).

## On a server without it

A server that does not run the game module of this tree has no relay, hands out no token and tells nobody who
stands near whom. There the cgame sends the same sound to the masterserver: one relay for all such servers,
`master.jka.vision` on UDP port 29170 (`voice.jka.vision` up to version 0.1.6, the same host). Added 2026-10-10, see "Checked" for what was run of it.

When it is used:

- The server info of the game server does not have the key `vs_voice` at all, and `v_voiceMaster` is 1. The
  cgame says so in one line of the console when it starts, with the name of the master.
- A server that says `vs_voice` relays itself, or says no with 0. Either way the master is not used.
- Never in a demo, in a browser, or for a game that runs on the same machine as the client.

Nothing of it goes to the game server: no client command, no userinfo. The game server can not tell.

**The room.** The master knows no game servers. Every client names the room it is in: 16 bytes of the SHA-1 of
the address of the game server in numbers, its port and `sv_serverid`. That number is in the system info,
which a server sends only to its clients, and is a new one with every level and every `map_restart`. So the
clients of one game server name the same room, somebody who is not connected to it can not name it, and the
master sees a hash and not an address. The cgame looks at `sv_serverid` once a second. That holds for somebody
who never connected during this level. A player who was kicked or banned, or left, still knows the room key (it
is made of the server address and `sv_serverid`) until the next map or `map_restart`, and the first session that
claims a client number keeps it. There is no fix for that without a server that vouches for its players.

**The token.** Nobody hands one out over a game connection here. The cgame sends its first `RELAY_STATE`
without one, the master answers with the token that address has for that room and client number, and the cgame
says the same again with it. The master works the token out (SHA-1 of a secret it made at its start, the
address, the room and the number) and keeps nothing of an address until a datagram came back from there with
it. A sender who gives an address that is not his fills no table, and the address he gave gets 13 bytes for the
35 that were sent. A client keeps the token the master gave it while the master answers that token. An answer
with another token (which a sender who fakes the address of the master could send) is taken only after the
master did not answer the own token for 3 seconds (a master that started again), or after the room changed.

**Who hears whom.** The range is the default of `vs_voiceRange`, 1024 units, and there is no cvar for it. The
master does not know where anybody stands, so both ends say what their game shows them. Ten times a second
the cgame works out two masks with a bit for every client number, with the rule the relay of a game server
uses (`BG_VoiceHears`):

- whom it wants to hear: the players it is shown within the range and a tenth more, the spectators if it is
  one itself, nobody it muted.
- who may hear it: the players it is shown within the same distance, and every spectator, since nobody is
  shown where a spectator watches from. A spectator lets only spectators hear him.

The master hands a voice from a speaker to a listener of the same room only if the listener wants to hear him
and the speaker lets him. A client that asks for everybody still gets only the speakers who see it near them.
From there on the cgame does what it does on any server: how loud, from which side, muffled or not, the mouth,
the list on the HUD.

```
to the master   "VO" version 2 client token[4] sequence[2]   room[16] hear[4] allow[4]
to a client     "VO" version 2 client token[4] sequence[2]   flags distance
```

The cgame sends this twice a second until the master answered (after ten seconds without an answer every five
seconds), then once a second, and at once when the room or a mask changed, ten times a second at most. It is
also what keeps the session at the master: one that is silent for five seconds is over. The flags of the answer
say that somebody else speaks as this client number in this room (2) or that the master is full (4). Sound is
the datagram of the own relay, both ways. The master names 255 as the distance, the end of the range: that is
what is left of a speaker in the moment after he went out of sight.

**The clan radio and the statuses** (2026-10-11). The master says in every answer that it knows them
(`VOICE_FLAG_SOCIAL`, 16, in the flags). A client that read that sends the long state, a client from before
keeps the short one, and a master from before is never sent anything of this:

```
to the master   "VO" version 2 client token[4] sequence[2]   room[16] hear[4] allow[4] clan[8]
to the master   "VO" version 3 client token[4] sequence[2]   frame
both ways       "VO" version 4 client token[4] sequence[2] (flags distance)   flags text
```

- `clan` is the first eight bytes of the SHA-1 of "clan", the room and `cp_clanPwd`, all zero for a player
  without a clan password. Players of one server with the same password say the same bytes, the master never
  has the password, and the bytes are others with every level. A frame sent as type 3 (`RELAY_RADIO`) goes to
  the members of the room that said the same clan, wherever their game has them, as the relay of a game server
  sends it: a `RELAY_AUDIO` with the flags of the radio and a distance of 0. The two masks do not count for it.
- Type 4 (`RELAY_STATUS`) is what stands above the head of a player ([client.md](client.md), "Statuses above
  the heads"): one byte of flags (1: away from the keyboard) and up to 63 bytes of text in UTF-8, the music he
  shares. The master hands it to the other members of the room that sent the long state, one a second from a
  session at most, and none with a control character in it. It keeps nothing of it: every client says its
  status every five seconds, which is also how somebody who joins later learns it, and a status that was not
  said again for fifteen seconds is over for whoever saw it.
- Away is not found out by anybody there, so every client says it of itself: no key and no turn of the view
  for a minute. A legacy player on such a server never shows as away.

**The relay of the masterserver** is the program that runs the master (the masterserver is not part of this
repository, see [server.md](server.md), "The masterserver"). It never decodes sound and writes no address and no
room into its log, only one line a minute with numbers while something happens, and one when it is stopped. It
limits a speaker to 60 datagrams and 14000 bytes a second, as the relay of a game server does, a room to one
session for each client number (32), and what it sends out in a second, and a session is over after 5 seconds
without a datagram.

Until 2026-10-12 this relay was called the voice master and was a program of its own as well. From 2026-10-12 the
masterserver ran in its place as a dedicated server of this tree, and since 2026-10-30 as a small program of its
own again. Where the checks below say "the master by itself", that program is meant.

What is different from a server with its own relay:

- Nobody vouches for a client number. The first session that says a number in a room has it. A player of the
  same server can say the number of somebody who is not there through the master and talk as him. Muting
  (`v_voice_mute`) works on it.
- A player inside the range that the game server does not show (behind enough walls) is silent. The relay of
  a game server sends him muffled and without a direction.
- A changed client that is a spectator can ask for all players at once and gets them, since every player lets
  every spectator hear him. Between players the range holds.
- `vs_voiceRange`, `vs_voiceTeamOnly`, `vs_voiceRadio` and `amsilence` do not exist there.
- The clan of the radio is whoever knows the clan password, as on a server with the module. Somebody who
  runs the master could try passwords against the eight bytes it is told.
- The master reads the status of every player who shares one (the title of his music) on its way through.
  It is not written down. `v_shareMusic` is off by default. Browsers announce every tab that plays media as a
  player of its own, so with `v_shareMusic 1` the title of any playing tab can be shown.
- Players who reach the game server under another address than the others (its own network) are in a room of
  their own.
- A voice travels to the master and back, not to the game server: players far from the master hear it later.
- The master sees the address of every player who uses it, and which addresses are in a room together. It
  does not see which game server that is.
- Without the master there is no voice chat on those servers. The cgame says so once and asks on every five
  seconds.

## In a browser

The client for the browser is built with the voice chat by `-DBuildJPVoice=ON` (the toolchain file for
Emscripten leaves it off unless it is asked for). It is the same code: the asking, the datagram, the codec, the
queues, who is heard how. Two things a page does not have are replaced, in the places marked `__EMSCRIPTEN__`.

**The way to the relay.** A page can not send UDP. The cgame opens a WebSocket of its own to `/voice` of the web
host the page came from (`ws://<host of the page>/voice`, `wss` over https), and every datagram is one binary
message on it, nothing added. The web host (`vs_clientWebPort`, [web.md](web.md)) has a UDP socket for each such
WebSocket, tied to the address the relay names in `vs_voiceRelay` (the server machine itself, the voice port).
It hands each message to the relay from that socket and each datagram the relay sends there back as a message.
To the relay that is one more client of the voice chat, the datagram and the server command are what they were.
No thread and nothing that waits: the socket is asked for and is open some frames later, what comes in between
two frames is copied into a queue of 64 datagrams and taken out by the next frame. A socket that was open and
ended is asked for again, once a second at most. If the web host does not take it, the next try comes after 2,
4, 8, 16 and 32 seconds, and after the sixth the chat says so in one line and stays off until the level changes.

**Who is let in.** The relay takes a datagram by its token and by the address it comes from. From the web host
they all come from the server machine, so the web host makes that check where the address of the page is known:

- `/voice` becomes a WebSocket only for an address a client of the game plays from through the web host (and
  only with the Origin of the page, as `/net`). The cgame asks when it has its token, which is when it is in the
  game. Anybody else gets 403, a server without a relay answers 404.
- A message is handed to the relay only if it is a whole datagram of the voice chat and the client it names
  plays through the web host from the address of that WebSocket. So a page speaks for the clients of its own
  address and for nobody else, which is what the relay asks of a client over UDP. A client that plays over UDP
  can not be named through the web host at all.
- What the relay sends to the socket of a WebSocket is handed on only if it has the token that peer itself last
  spoke with.
- The relay takes a datagram from the address of its own machine with the token alone, if the server runs the
  web host (`vs_clientWebPort` is set). On a server without the web host nothing changed.

Limits of `/voice`: a message of 251 bytes at most (a datagram, a longer one ends the socket), 60 datagrams at
once and 60 a second from one WebSocket (what the relay takes from a speaker, the relay counts again), 4 such
sockets for one address, and each takes two of the 60 connections of the web host because of its UDP socket.
A socket whose address has no client in the game any more is closed after 10 seconds. One that is silent is
asked after 30 seconds and closed after 90, as every WebSocket of the web host.

**The sound devices.** miniaudio plays and records through Web Audio, with a ScriptProcessorNode, since this
build has no threads and no audio worklets. The page hands the sound over in the thread the game runs in, in
pieces of 2048 samples (43 ms), so that much more of every voice is kept decoded ahead of the device: a voice is
later than on the desktop by that and by what the browser adds, which was not measured. A frame of the game
that takes much longer than a piece is a gap in the voices.
The devices are the ones the browser uses, `v_voiceCapture` and `v_voicePlayback` choose nothing, and
`v_voiceBackend null` is not there. A browser lets a page play sound after the first click or key in it.

**The microphone.** A browser gives a page a microphone only if the page came over https or from the machine
itself (`http://localhost`, `http://127.0.0.1`), and only if the player says yes. The web host speaks plain
http, so a player on another machine hears the others and can not talk. The page is asked at the first press of
`+v_voice` (with voice activation: at once), the game does not wait for the answer. Without a microphone the
press says why in one line, once a press, voice activation falls back to push to talk, and `v_voice_status` says
it in words:

```
This page has no microphone: it was not opened over https or on the machine itself. The others are heard all the same.
This page has no microphone: the browser did not give it one (it was refused, or there is none). The others are heard all the same.
```

A no is asked about again with the next press. The microphone is given back to the browser when it is closed,
30 seconds after the last press. What a browser does to a microphone by itself is left as the browser has it.

**The engine's sound in the level.** The engine for the browser is built without `v_spatial`, so every voice is
played on the device of the cgame, as on any engine that does not take voices. If the engine offers the call
later (`v_spatialVoice` 1), the voices go there as on the desktop: that part has no code of its own here.

## Clients without it

Nothing of the voice chat is in the game protocol.

- No change to snapshots, entity state, entity flags, events, configstrings or the pure check, no userinfo key
  that is required, no file a client has to have.
- The server command `vs_voiceLink` only goes to a client that sent the client command of that name. A client
  without this cgame never does, so it never gets a command it does not know.
- The sound goes over datagrams of its own, on the port of the game or on a second one. A client that never
  sent one with a valid token gets none.
- The one thing every client can see is the key `vs_voice` in the server info. Clients ignore keys they do not
  know.
- The mark over a speaker is drawn by the cgame of the listener. No flag is set on the player.
- With `vs_voice 0`, or if the port can not be opened, the server prints one line and runs as before.
- The other way round, this cgame on a server without the voice chat sends that server nothing it did not send
  before. What it sends goes to the masterserver, see "On a server without it".
- The way of a browser through the web host is beside all of that. A client over UDP sends and is checked as
  before, also on a server that has players in a browser. The game module only gained the cvar `vs_voiceRelay`,
  which is not in the server info.

## Security

- The relay opens a UDP port. It is on by default since 2026-10-07 (`vs_voice 1`).
- Every length of a datagram is checked in one place (`BG_VoiceUnpack`), what passes fits the buffers of both
  sides. The server never decodes sound.
- A datagram is only taken with the token of the client it names and from the address of his game connection,
  so nobody can talk as somebody else without being on the line between him and the server.
- On a server with the web host (`vs_clientWebPort`) a datagram from the server machine itself is taken with the token
  alone: that is how the voice of a browser arrives, and the web host has checked the address by then, see "In
  a browser". A packet with the address of the machine does not come in from a network, the system drops it.
  What is left is a program on the server machine: it can try tokens (32 random bits) for any client without
  being at his address. More than 20 wrong tokens in a second from the machine make the relay take nothing
  from it for 10 seconds, with one line in the security log ("Voice chat: more than 20 wrong tokens in a
  second from this machine, nothing from it is taken for 10 s"). That stops the voice of browser clients for
  that time too. With that limit 2^32 tokens take decades. Tested: 10 wrong ones in a second do nothing, 30 at
  once give the line.
- Whoever reaches the port of the web host and is no player gets no WebSocket for the voice chat (403). A player
  in a browser can send for the clients of his own address only, hears only what the relay sends for the token
  he spoke with, and nothing he sends changes where the web host sends: its UDP sockets are tied to the relay.
- One datagram in is at most one out per listener, and only to addresses that proved themselves with a token:
  the port can not be used to flood a third party.
- From one speaker 60 datagrams and 14000 bytes a second are taken, the rest is dropped and counted. One call
  of the relay takes at most 64 datagrams, the system drops what does not fit its buffer.
- The client decodes sound only from the server it is connected to. Opus is built with its checks for release
  builds on.
- Nothing is encrypted, like the game itself. Somebody on the line can listen.
- The relay of the masterserver is a port open to everybody. It answers a datagram without a token with a
  shorter one and keeps nothing for it. It sends sound only to addresses that came back with their token, one
  datagram out for one in per member of a room, so it can not be used to flood somebody else. What leaves it
  in a second is capped.
- Through the master nobody checks who is who: see the list in "On a server without it".
- A status that came through the master is text somebody else wrote. The master refuses control characters,
  the cgame refuses them again, strips colour codes, and draws at most 63 bytes of it.
- A player whose sound takes another way out of his network than his game (two lines, some carrier NATs) is
  refused by the address check and has no voice.

## The clan radio in the protocol

A client sends a frame for its clan as `RELAY_RADIO` (type 3) in place of `RELAY_AUDIO`, only to a relay that
said it knows the radio: the answer to `vs_voiceLink hello` has a fifth number for that (`VOICE_CAP_RADIO`), which a
cgame from before does not read. The relay (`G_VoiceRadio`) hands the frame to the clients whose clan password
is the one of the speaker, as a `RELAY_AUDIO` with `VOICE_FLAG_RADIO`, `VOICE_FLAG_UNSEEN` and a distance of 0.
The masterserver does the same for the members of a room that named the clan of the speaker, so a
client plays the radio the same from either. A cgame that knows the first flag plays it without a place through `BG_VoiceRadio()`. One from before the radio
(release 0.1.1) knows the second: for it the frame is a voice close by that it is not shown, which it plays
muffled and at full loudness. So nobody of the clan is left out, some hear it duller. The limits of a speaker
(60 datagrams and 14000 bytes a second) count for both kinds of frame together.

## Checked

2026-10-12 21:43, on the host of VisionJK (the first time over a real network): the game server and the
masterserver were deployed, and the two programs it replaces were stopped. From another machine of that network,
by the address of the network and through the public names:

- Both answer `getstatus` on their one UDP port (29070, 29170). The host has no other UDP port open to the
  outside, the game server three sockets on 127.0.0.1 (its temple, the link to it, the relay for the web host).
- The relay of the masterserver: the two steps of a client (a state without a token gets one, with it the
  client is in, flags 16).
- A hidden, silent client on the public game server: the temple key started the temple of that server (its log
  has him one second later), he stood in it and was back in his body (`tools/templetest/live.sh own`).
- The same client on a server of this machine without a temple of its own: the temple key led to the
  masterserver, which its relay named, and back (`live.sh master`).

Nobody talked there, and no second player was in either temple.

2026-10-12 night, after the relay got its name (it was the voice master before, see "On a server without it"):
the 62 checks against the relay by itself and against the masterserver, a visit to a temple server the relay
by itself names, and the way from a server whose temple does not answer on to the masterserver
(`master.sh`, `masterserver.sh`, `uitest/visit.sh live`, `templetest/fallback.sh`). All as before.

2026-10-12, the voice chat on the port of the game and the masterserver, Linux, hidden and without sound
hardware, everything on one machine over 127.0.0.1:

- The relay both ways (`tools/voicetest/e2e.sh`, and with `SV_ARGS="+set vs_voiceShared 0"`): the listener got
  the tone open, through the wall and with muffling off as before, no gap, on the port the server named: the
  port of the game, or the one after it.
- A client from before the change (`OLD_LISTENER=1 SV_ARGS="+set sv_floodProtect 1" tools/voicetest/e2e.sh`, the
  listener with the cgame that was installed, v0.1.6-dev.2): it followed the port the server told it and heard
  the same.
- Telling voice from game (`tools/voicetest/gate.sh`, flood protection on): one client sent until it was past
  its packet 85846 and stayed connected with its voice chat. Five datagrams with its qport in bytes 4 and 5
  from another port of its address and 4000 datagrams of junk (`flood.py`) were refused: the relay counted
  2405, which are those five and the 2400 of the junk that have the shape of a voice datagram, so that packet
  was not among them. The server never took the port of the client for another one.
- The sockets (`tools/voicetest/sockets.sh`): a dedicated server holds one UDP socket, the one of the game. With
  the web host on it has a second one on 127.0.0.1 for that host and names it in `vs_voiceRelay`. With
  `vs_voiceShared 0` it has the port after the game port as before.
- The masterserver (`tools/voicetest/masterserver.sh`): the 62 checks of its relay, on its game port. The same
  62 pass against the master by itself (`master.sh`, sanitizers). One of them was changed for the masterserver:
  a game server answers a packet it knows no client for with "disconnect", which is no answer of the relay.
- The way to the temple through it (`MASTER=1 tools/uitest/visit.sh live`): the temple key in a level, the
  master names its own port, the client visits it, walks, and is back in its body.

Not checked: voice of a client in a browser through the web host of a server with one port (the socket for it
is there, nobody talked through it), Windows, how any of it sounds. (The masterserver on its real machine: see
the lines of 2026-10-12 21:43 below.) The dedicated server now catches SIGTERM and SIGINT (Linux, macOS) and
quits as on "quit", so a service that is stopped tells its players. Tested with `kill -TERM` on a dedicated
server with a bot: "Received signal 15, quitting", ShutdownGame, exit code 0.

2026-10-11, the clan radio and the statuses through the relay for servers without the module (the "voice master"
of then, a program of its own), Linux, without sound hardware:

- Unit tests (`tools/voicetest/run.sh`, 209 checks with the ones below): the long state and the short one,
  every length between them, a clan of zeros, a status there and back both ways, a text cut at the end of a
  character, control characters.
- The master alone (`tools/voicetest/master.sh`, 62 checks, sanitizers): the radio reaches the same clan in the
  same room and nobody else (another clan, a client that sent the short state, one without a clan, the same
  clan in another room), whatever the masks say; a status reaches the others of the room that sent the long
  state, one a second, none with a control character; a client from before next to new ones hears and is heard
  as before.
- Three scripted clients on a server that says nothing of voice (`SOCIAL=1 tools/voicetest/e2e_master.sh`):
  one of the clan in the other room got the radio in both ears at the same level with both pitches of the tone,
  the plain voice came dull through the wall; somebody next to the speaker without the clan password got none
  of the radio and did get the plain voice; both read "Listening to ..." above the speaker, the line went when
  his music ended, and after a minute without a key the two who stayed were away for each other. The game
  server got no command of it.

Not checked: the master on its real machine (it has to be deployed anew), the internet path, a real player of
music on this path, how any of it sounds. On Windows the relay, the master and the statuses ran the same day,
see [windows.md](windows.md).

2026-10-07, Linux, without sound hardware. The clan radio of 2026-10-11 has unit tests only (the datagram, what
its sound lets through: 1000 Hz whole, 100 Hz and 8000 Hz at less than a tenth, the clicks), 145 checks with the
ones below. Run on 2026-10-11 between three scripted clients (`tools/sectest/e2e.sh`): one of the clan in the
other room got the radio in both ears at the same level with both pitches of the test tone in it, somebody
next to the speaker who is not of the clan got none of it and did get his plain voice. The level of the radio
was turned down after that run (it came out more than twice as loud as a voice next to the listener), and a
client whose `cp_clanPwd` is still "none" is now told so instead of sending to nobody. Nobody has heard it.

- Unit tests of `bg_voice.c` (`tools/voicetest/run.sh`): 108 checks with the address and undefined behaviour
  sanitizers. Among them 800000 random datagrams in buffers of exactly their size, the queue with loss,
  doubles and reordering over 200000 rounds, the low-pass (150 Hz at 0.57, 700 Hz at 0.30, 3000 Hz at 0.03 of
  what went in).
- End to end (`tools/voicetest/e2e.sh`): a dedicated server and two clients with `v_voiceBackend null` on the
  test room of the renderer tests. One sends a tone of 440 and 3000 Hz, the other writes what it would play to
  a file. Next to the speaker: both pitches at the same level, louder on the side he stands on, no gap in four
  seconds. From the other room: 3000 Hz at 1 % and 440 Hz at 15 % of that. With `v_voiceMuffle 0` both at the
  same level again.
- A client without the voice code (`tools/voicetest/legacy.sh`): a third client with a cgame from before the
  voice chat joins the same server while the two talk, once with `vs_voice 1` and once with 0. Its log is the
  same but for the order in which the others joined. It sent no `vs_voiceLink`, the server gave it no token and
  listed two clients with voice.
- 12000 datagrams that are no voice (random bytes, the right shape with tokens nobody has, client numbers out
  of range, too long, cut off) at the relay of a server under valgrind (`tools/voicetest/flood.py`): all
  refused, no error.
- Builds: Linux 64, and with mingw for Windows 64. The Windows cgame imports `ws2_32` in addition to
  what it imported before.

Not checked: a real microphone and real speakers, how it sounds, the
original JA+ plugin as the client without it, macOS (not even built), more than two speakers, a connection with
loss, a server over the internet.

2026-10-09, the browser. No game server was run for it: the two halves were checked apart.

- The web host with the sanitizers, on sockets that are none (the test program of the web host, with the UDP
  sockets to the relay added): who gets `/voice` (404, 403, 500, 503, 101), a datagram each way byte for byte,
  14 kinds of message that are not handed on, the token on the way back, 1000 datagrams at once (60 are handed
  on, 60 more a second later), 4 sockets for an address and the fifth, the last place of the table, a peer that
  does not read, the closing 11 seconds after its client left, and 100000 random wake-ups with peers that ask
  for `/voice` and send what they like. 13 changes that take one of the checks out again were each seen by a
  test.
- The cgame in headless Chromium 153 (`tools/panelshot/steps_voice_mock.js`), in a demo, with the answer of the
  server given by hand and a WebSocket in the page that answers like the relay and sends each frame back as the
  voice of another player. The device of the page plays (48000 Hz, pieces of 2048), `v_voice_test tone` comes back
  through the codec at 440 and 3000 Hz at 0.183 and 0.179 of full scale (0.183 went in), without a gap in 1.5
  seconds. With the microphone of the browser that is none: asked, opened, 50 frames a second sent, closed 30
  seconds after the key. Without one, and as a page over plain http: one line a press, `v_voice_status` as above,
  the tone still heard. On a host without `/voice`: six tries in 62 seconds, one line, nothing after it.
  `vid_restart` while it talks: devices and socket gone, the new cgame starts again.
- Builds: the browser with and without the voice chat (without it the object code of the voice files is the
  same as before), Linux 64, mingw for Windows 64 (server, game module, cgame).

2026-10-10, the relay for servers without the module, then a program of its own called the voice master, Linux,
without sound hardware.

- Unit tests of `bg_voice.c` (`tools/voicetest/run.sh`): 126 checks with the sanitizers, 18 of them for
  `RELAY_STATE` (both directions, every wrong length, the masks byte for byte). The 800000 random datagrams
  pass with the new type among them.
- The master by itself (`tools/voicetest/master.sh`): built with the address and undefined behaviour
  sanitizers and talked to over the loopback addresses of the machine, each client from an address of its own.
  41 checks: the token is named for 35 bytes with 13 and nothing is kept, sound with it before the second
  `RELAY_STATE` is no session, a voice passes only where the listener asks and the speaker allows, rooms do
  not mix, a second address that says a number that is taken is refused and reaches nobody, the token of
  somebody else from another address or under another number is nobody, a new port of the same address is
  the same session, 3000 datagrams that are no voice get no answer, of 300 at once 60 a second are handed on,
  16 sessions from one address and the 17th is refused, a silent session is over after 5 seconds and its
  number free, another room means another token, `-maxkbps 8` lets 4 of 50 datagrams out. It ends with 0 on
  SIGTERM and the sanitizers found nothing.
- End to end (`tools/voicetest/e2e_master.sh`): the dedicated server with a stand-in for a foreign game module
  (this tree's module with `vs_voice` out of the server info and at 0), the master, and two clients with
  `v_voiceBackend null` on the test room. The server info had no `vs_voice` and the game server got no
  `vs_voiceLink` command. Next to the speaker: 440 and 3000 Hz at the same level (0.132 and 0.130), louder on his
  side, no gap in four seconds. From the other room: 440 Hz at 14 % and 3000 Hz at 1 % of that. In the hall
  2400 units away, where the level still shows him: nothing. Back next to him: as before. Muted: nothing. As
  a spectator who follows him: both pitches at the same level, the same on both sides.
- The same run with `v_voiceMaster 0` on both clients: the master got no datagram at all, `v_voice_status`
  says why. Without a master: one line after ten seconds, the scripts of both clients run to their end.
- The relay of a game server after the changes to `cg_voice.c` (`tools/voicetest/e2e.sh`): the same levels as
  through the master, to the fourth digit.
- Builds: the master, the cgame and the game module for Linux 64 and with mingw for Windows 64, and the cgame
  for the browser (which has no master mode). The file of that program and `cg_voice.c` give no warning with `-Wall -Wextra`.

Not checked: a real microphone, the master over the internet and behind a router, two clients behind one
router, the answer "taken" as a player sees it (the master's side of it is in the checks), the menu entry,
Windows at run time, a foreign game module that is not of this tree, the master as a service on its host.

Not checked in a browser: a page against the relay of a real server (`tools/panelshot/steps_voice_e2e.js` is
for that), the UDP sockets of the web host on a real system, a real microphone, how it sounds, a browser that
asks the player, Firefox and Safari, a page over https behind a proxy, the web host on Windows.
