# Voice chat

An in-game voice chat with the microphone. It is not a JA+ feature. It needs the cgame of this tree on the
client and the game module of this tree on the server. The engine is not changed: it works with a 64 bit OpenJK.
A stock `jamp.exe` is 32 bit and can not load this cgame, the tree is built for 64 bit only.

A client without this cgame (stock jamp, the original JA+ plugin, another fork) is not touched by any of it. It
joins and plays as before, it just does not hear the voice chat. See "Clients without it".

## For players

```
bind v +voice
```

Hold the key to talk. What the others say is heard without a key. The key is also in the menu under Controls,
JA+ Control, and the settings a player needs most are in Setup under Voice Chat, in the main menu and in the
game (the menus are in `assets/japlus_client/ui/jamp`, the ui module registers the cvars so they are there
before a level was loaded).

| Cvar | Default | Meaning |
|------|---------|---------|
| `cg_voice` | 1 | 0: no voice chat, nothing is opened and the server is not asked |
| `cg_voiceMode` | 1 | 1: push to talk (`+voice`). 2: voice activation, whatever is louder than `cg_voiceThreshold` is sent |
| `cg_voiceVolume` | 1 | how loud the others are. `s_volume` does not change them |
| `cg_voiceMicGain` | 1 | how loud the own microphone is sent |
| `cg_voiceThreshold` | 0.02 | voice activation: from which level on, 0 to 1 |
| `cg_voiceMuffle` | 1 | 0: voices behind walls are not muffled, and the engine's `s_spatial` leaves them alone |
| `cg_voiceDrawTalkers` | 1 | the list of who is talking on the HUD |
| `cg_voiceCapture`, `cg_voicePlayback` | "" | the name of the microphone and of the speakers, empty: the ones the system uses. `voice_devices` lists the names |
| `cg_voiceBackend` | "" | `null`: a sound system that plays into nothing, for tests |

| Command | |
|---------|---|
| `+voice` | talk while it is held |
| `voice_devices` | the microphones and speakers there are |
| `voice_status` | is the chat connected, who talks, who is muted |
| `voice_mute <player>`, `voice_unmute <player or all>` | do not hear one player. Holds until the level changes |
| `voice_test tone`, `voice_test play <name>`, `voice_test stop` | a tone, or the file `voice/<name>.raw` again and again, in place of the microphone. For tests |
| `voice_test record <name>` | what is played also goes into `voice/<name>.raw`, until `voice_test stop`. For tests |

The files of `voice_test` are 16 bit, two channels, 24000 samples a second, low byte first, without a header:
what was recorded can be played, and `ffmpeg -i in.mp3 -ac 2 -ar 24000 -f s16le voice/name.raw` makes one from any
sound. On a pure server the engine lets a module read such a file only out of a pk3 the server has too.

With push to talk the microphone is opened at the first press of the key, which takes a moment that once, and is
closed 30 seconds after the last press. A player who never presses the key never has his microphone open. With
voice activation it is open all the time.

Who talks has the voice sprite of the game over his head and is in a list at the left of the HUD.

Use headphones. There is no echo cancellation: with speakers the others hear themselves.

## For the server

| Cvar | Default | Meaning |
|------|---------|---------|
| `g_voice` | 1 | 0: no voice chat, no port is opened (latched: from the next level on). It is in the server info |
| `g_voicePort` | 0 | the UDP port of the relay, 0: the game port plus 1 (latched) |
| `g_voiceRange` | 1024 | how far a voice carries, in units. 0: all over the level, without direction and without muffling |
| `g_voiceTeamOnly` | 0 | 1: in team games only the own team hears a player |

The voice chat is on unless a server switches it off (since 2026-10-07, before that `g_voice` was 0). So every
server of this module opens the voice port, also a game a player starts from the menu. If the port can not be
opened the server says so in one line and runs without voice. A server that has `g_voice 0` stored in its config
keeps that.

The voice port has to be reachable from outside like the game port: UDP, forwarded and open in the firewall.
`voicestatus` on the server console lists who has voice, how many datagrams of each were handed on, and how many
were refused.

A spectator is heard by spectators only, wherever they are. A spectator hears the players near the spot he
watches from. A player an admin silenced (`amsilence`) is not heard.

## How it works

The engine gives a module no microphone, no way to play samples it made itself and no channel for sound, so the
cgame does all three on its own.

**Asking.** A server with the voice chat says `g_voice` in its server info. Only there the cgame sends the client
command `jpvoice hello <protocol>`. The server answers with the server command
`jpvoice <protocol> <port> <token> <range>`. The token is 32 random bits from the random source of the system,
new for every connection. This exchange is all of the voice chat that goes through the game connection. The
cgame asks again every three seconds, ten times at most, until the answer is there: the flood protection of the
server drops a client command that comes within a second of another one.

**The sound.** The cgame opens a UDP socket of its own to that port. A datagram, low byte first:

```
to the server   "VO" version type client  token[4] sequence[2]                 frame
to a client     "VO" version type speaker token[4] sequence[2] flags distance  frame
```

A frame is 20 ms of sound, 24000 samples a second, one channel, coded with Opus at about 20 kbit a second, with
a small copy of the frame before it in each: about 3 kB a second for a speaker. The type is sound or hello. A
hello carries nothing, it tells the server where the client listens (through his router) and is answered.

**The relay** (`g_voice.c`) is polled with every server frame and with every move a client sends, so a voice does
not wait for the next server frame. It takes at most 64 datagrams a call and never waits. It does not decode the
sound. A datagram counts if it is whole, names a connected client, carries his token and comes from the address
his game connection comes from. It then goes to every client who is to hear it (`BG_VoiceHears`), with that
client's token, a flag if the server does not show him the speaker at all, and the distance in steps of 16
units. No coordinates are sent: a voice must not tell where somebody stands behind a wall.

**The listener** (`cg_voice.c`) takes datagrams only from the address and port of the server and with his own
token. The frames of a speaker wait in a queue by their sequence number. He starts to play when 60 ms of him
wait, so a datagram that comes late still finds its place. A lost frame is rebuilt from the copy in the next
one, or guessed by the codec. If his voice runs dry while he talks, more of it waits the next time.

**Where a voice comes from.** With a range a voice gets quieter with the distance and comes from the side its
speaker is on. Ten times a second one line is traced from the eye of the listener to the head of the speaker,
against the level and its doors. If something is in the way, or one of the two is under water and the other is
not, the voice is muffled: two low-passes at about 700 Hz and a little quieter, faded in over 150 ms. A speaker
the server does not show at all is muffled and comes from no direction.

With the engine of this tree and `s_spatial 1` the cgame does none of that for a speaker it can see: it hands
his voice to the engine, where it is a sound in the level, behind the walls and in the reverb of every other
sound. See [audio.md](audio.md), "The voice chat". With `cg_voiceMuffle 0`, on any other engine and with
`s_spatial 0` every voice is played as described here.

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
| `game/bg_net.h` | the sockets as the modules use them, shared with the web admin panel, and the random source |
| `game/voice/g_voice.c` | the relay |
| `cgame/voice/cg_voice.c` | the client: asking, socket, codec, queues, the key, where a voice comes from, HUD |
| `cgame/voice/cg_voice_audio.c`, `cg_voice.h` | microphone and speakers |

`-DBuildJPVoice=OFF` builds the cgame without it (and without the two libraries).

## Clients without it

Nothing of the voice chat is in the game protocol.

- No change to snapshots, entity state, entity flags, events, configstrings or the pure check, no userinfo key
  that is required, no file a client has to have.
- The server command `jpvoice` only goes to a client that sent the client command of that name. A client
  without this cgame never does, so it never gets a command it does not know.
- The sound goes over the second port. A client that never sent there with a valid token gets nothing from it.
- The one thing every client can see is the key `g_voice` in the server info. Clients ignore keys they do not
  know.
- The mark over a speaker is drawn by the cgame of the listener. No flag is set on the player.
- With `g_voice 0`, or if the port can not be opened, the server prints one line and runs as before.
- The other way round, this cgame on a server without the voice chat sends nothing it did not send before.

## Security

- The relay opens a UDP port. It is off by default (`g_voice 0`).
- Every length of a datagram is checked in one place (`BG_VoiceUnpack`), what passes fits the buffers of both
  sides. The server never decodes sound.
- A datagram is only taken with the token of the client it names and from the address of his game connection,
  so nobody can talk as somebody else without being on the line between him and the server.
- One datagram in is at most one out per listener, and only to addresses that proved themselves with a token:
  the port can not be used to flood a third party.
- From one speaker 60 datagrams and 14000 bytes a second are taken, the rest is dropped and counted. One call
  of the relay takes at most 64 datagrams, the system drops what does not fit its buffer.
- The client decodes sound only from the server it is connected to. Opus is built with its checks for release
  builds on.
- Nothing is encrypted, like the game itself. Somebody on the line can listen.
- A player whose sound takes another way out of his network than his game (two lines, some carrier NATs) is
  refused by the address check and has no voice.

## Checked

2026-10-07, Linux, without sound hardware.

- Unit tests of `bg_voice.c` (`tools/voicetest/run.sh`): 108 checks with the address and undefined behaviour
  sanitizers. Among them 800000 random datagrams in buffers of exactly their size, the queue with loss,
  doubles and reordering over 200000 rounds, the low-pass (150 Hz at 0.57, 700 Hz at 0.30, 3000 Hz at 0.03 of
  what went in).
- End to end (`tools/voicetest/e2e.sh`): a dedicated server and two clients with `cg_voiceBackend null` on the
  test room of the renderer tests. One sends a tone of 440 and 3000 Hz, the other writes what it would play to
  a file. Next to the speaker: both pitches at the same level, louder on the side he stands on, no gap in four
  seconds. From the other room: 3000 Hz at 1 % and 440 Hz at 15 % of that. With `cg_voiceMuffle 0` both at the
  same level again.
- A client without the voice code (`tools/voicetest/legacy.sh`): a third client with a cgame from before the
  voice chat joins the same server while the two talk, once with `g_voice 1` and once with 0. Its log is the
  same but for the order in which the others joined. It sent no `jpvoice`, the server gave it no token and
  listed two clients with voice.
- 12000 datagrams that are no voice (random bytes, the right shape with tokens nobody has, client numbers out
  of range, too long, cut off) at the relay of a server under valgrind (`tools/voicetest/flood.py`): all
  refused, no error.
- Builds: Linux 64, and with mingw for Windows 64. The Windows cgame imports `ws2_32` in addition to
  what it imported before.

Not checked: a real microphone and real speakers, how it sounds, Windows at run time, the
original JA+ plugin as the client without it, macOS (not even built), more than two speakers, a connection with
loss, a server over the internet.
