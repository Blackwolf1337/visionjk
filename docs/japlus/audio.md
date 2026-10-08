# Sound that knows the level

The sound of the game is a mixer from 2003: every sound has a volume for the left and one for the right ear,
nothing else. It does not know that there is a wall between a sound and the listener, how large the room is or
that a sound has to come round a corner. `s_spatial 1` changes that for the sounds in a level, with Steam Audio
of Valve doing the acoustics. It is not a JA+ feature.

It is in the engine, so only a player with this engine has it. Nothing of it is in the protocol: a server and
the other players notice nothing. The one thing a game module does for it is in the cgame, which hands the
voices of the [voice chat](voice.md) to it when the engine says it takes them.

Nobody has listened to it yet. Everything under "Checked" is measured from recordings, how it sounds is not
judged.

## For players

Put the library of Steam Audio next to the program (see "The library"), then in the console:

```
s_spatial 1
snd_restart
```

| Cvar | Default | Meaning |
|------|---------|---------|
| `s_spatial` | 0 | 1: on (latched: from the next `snd_restart` on). Needs `s_khz 44` and stereo |
| `s_spatialHrtf` | 1 | 1: direction for headphones (HRTF). 0: panned for speakers |
| `s_spatialReflections` | 1 | 1: the room around the listener rings. 0: no reverb |
| `s_spatialReverb` | 1 | how loud the reverb is |
| `s_spatialPaths` | 1 | 1: a sound comes round corners |
| `s_spatialPathLoss` | 1 | 0: a sound round a corner only gets duller, without what bending costs |
| `s_spatialRays` | 2048 | rays of a pass of the reflections, 256 to 8192 |
| `s_spatialBounces` | 16 | how often a ray is followed off a wall. Fewer cut the tail of a large room |
| `s_spatialAhead` | 0.1 | seconds of sound rendered ahead. Less follows a turn of the head sooner, more survives a longer hitch |

### The menu

The sound page of the setup menu has three entries for it. "3D Sound" is a preset:

| Preset | `s_spatial` | Reflections | Rays | Bounces | Paths |
|--------|-------------|-------------|------|---------|-------|
| Off | 0 | | | | |
| Low | 1 | 0 | | | 0 |
| Medium | 1 | 1 | 1024 | 8 | 0 |
| High | 1 | 1 | 2048 | 16 | 1 |
| Very High | 1 | 1 | 8192 | 32 | 1 |

High is the defaults. Low is direction and muffling behind walls without a ray pass. The numbers of Medium and
Very High are a choice inside what the engine clamps to, their cost was not measured. The entry shows "Custom"
when the cvars are none of the rows. Choosing a preset restarts the sound only when it switches `s_spatial`
itself, the rest is read while the game runs. "Output" is `s_spatialHrtf` (Headphones or Speakers), "Reverb" a
slider on `s_spatialReverb`. The menu does not know whether the library is there: without it the console says
so and the sound stays as it is.

`s_spatialInfo` says whether it runs, what the level is made of, every sound with how much of it is in sight,
what the threads take and the state of the paths.

What changes: a sound behind a wall is quieter and duller, by what the wall is made of. A sound has a direction
that is more than left and right (with headphones). The room around the listener rings, a hall longer than a
chamber. A sound in the next room arrives through the doorway. The voices of the voice chat are sounds in the
level too, see "The voice chat". The sounds of the own player, the menus, the announcer and the music are not
touched.

What does not: how loud a sound is at its distance is still what the game makes of it. Doors and lifts are not
in the way of anything, see "Limits".

## The library

The engine is not linked against Steam Audio. It loads `libphonon.so` (Linux), `phonon.dll` (Windows) or
`libphonon.dylib` (macOS) from the folder the program is in when the sound system starts. Without the file, or
with one of another version, it says so in one line and the sound is as it always was.

The file comes from Valve's release of the version the tree is built for, 4.8.1:
`steamaudio_4.8.1.zip` from github.com/ValveSoftware/steam-audio/releases, in it
`lib/linux-x64`, `lib/windows-x86`, `lib/windows-x64`, `lib/osx`. It is 24 to 50 MB.

Steam Audio is under the Apache License 2.0, this engine under the GPL version 2. They stay two files, the way
the engine has always loaded the EAX library. Before giving out builds with the library in them, look at that
again.

## Building

Valve's repository is a submodule, `lib/steamaudio`, at the tag `v4.8.1`:

```
git submodule update --init
```

The engine only takes the header `phonon.h` from it. cmake copies that and the version header Valve's own build
generates into `<build>/steamaudio`, nothing else of the submodule is on the include path. Without the
submodule checked out, cmake says so and builds the client without `s_spatial`. `-DBuildJPSpatialAudio=OFF`
does the same on purpose.

## How it works

**The level** (`qcommon/cm_export.cpp`). Steam Audio traces rays in threads of its own, and the collision code
of the engine can not be called from a second thread. So it gets the level once, as triangles: every side of a
solid brush of the world, every facet of a solid curve. Each triangle has a material, from the surface flags
the game uses for footsteps, mapped onto Valve's numbers for a few standard materials (how much a material
swallows, scatters and lets through). The sky swallows everything. Most surfaces of the base levels carry no
material and count as "generic". mp/ffa3 is 50749 triangles and takes 0.1 s, during the loading screen.

**The sounds** (`snd_dma.cpp`, `S_SpatialGather`). Once a frame every sound with a place in the level is
named: the channels of the mixer, and every loop sound on its own (the old mixer adds loop sounds of one
sample up into one channel, which has no place). Each keeps a voice for as long as it plays.

**What is in the way.** For each voice, every frame: 16 rays from the listener to points around the source say
how much of it is in sight, and the walls in between say what gets through. A wall is a brush with two sides,
so a side gets the square root of Valve's number and the wall as a whole the number.

**Rendering** (`snd_spatial.cpp`). The old mixer paints the next fifth of a second again in every frame, which
is how a sound that starts is heard after 50 ms. The effects of Steam Audio keep state from block to block and
can only run once and in order. So each voice is rendered ahead on its own, in blocks of 1024 samples, into a
ring of finished sound by the time of the mixer, and the mixer adds what the ring has for the samples it
paints. A sound that starts adds itself to what is there and is heard as soon as on the old path. What changes
for a voice (where it is, what hides it) reaches the blocks that are not rendered yet: `s_spatialAhead`.

**Reflections.** One source stands where the listener stands. Rays from there are followed through the level
and what comes back becomes the impulse response of the place. Every voice is sent through that one reverb, as
it arrives after the walls in its way. A pass takes 10 to 60 ms, so it runs in a thread. The game asks for a
pass at most every 50 ms and takes what the last one found.

**Paths.** For a sound the listener does not see, Steam Audio finds the ways round the corners from points it
has spread over the floors of the level (probes) and from which of them see each other. That is worked out
once per level, in another thread, the first time the level is loaded, and kept in
`steamaudio/<level>_<checksum of its walls>.paths` in the profile. Until it is done the level has no paths.
How loud a sound is at the end of a way is what the game makes of a sound at that distance.

**The voice chat.** The cgame plays the voices of its voice chat on a sound device of its own, which the
engine knows nothing of. With `s_spatial` on, the engine sets the cvar `s_spatialVoice` to 1 and the cgame
hands over what it decoded of a player, 20 ms at a time, with where his mouth is and how loud he is at his
distance (`ext.S_SpatialVoice` in `cgame/cg_public.h`, a call only this engine has: the cgame looks at the
cvar before it touches it). The engine brings it to the rate of the mixer and it is a voice like the others:
the walls, the reverb, the ways round corners. How loud a voice is at its distance stays what the voice chat
makes of it (`g_voiceRange`, `cg_voiceVolume`), and `s_volume` does not change it. A voice is decided when its
speaker starts to talk and stays on its way until he stops.

Not handed over, and played by the cgame as before: a chat without a range (`g_voiceRange 0`), a speaker the
server does not show, a spectator, and every voice for a player with `cg_voiceMuffle 0`, who does not want
voices muffled. The same goes for any other engine, and for this one with `s_spatial 0` or without the library.

The other sounds are there ahead of time, a voice is not. It is rendered as far as what arrived of it reaches,
a block behind where the others are rendered to, so it is heard about `s_spatialAhead` (0.1 s) later than the
cgame would play it.

**Threads.** The engine had none. Now: one for the passes of reflections and paths, one while the paths of a
level are worked out, and what Steam Audio starts inside them. The game never waits for one, except when the
sound system is shut down. A level that was loaded is handed to the simulator in the first frame no pass
runs, until then the old mixer plays.

| File | Content |
|------|---------|
| `client/snd_spatial.cpp`, `snd_spatial.h` | everything of Steam Audio: loading, the level, voices, rendering, reverb, paths |
| `qcommon/cm_export.cpp` | the world as triangles |
| `client/snd_dma.cpp` | `S_SpatialGather`, and the fade by distance as functions both paths use (`S_AttenuateDistance`) |
| `client/snd_mix.cpp` | `S_SampleRun`: samples of a sound as floats. The mixer leaves out what the new path plays |
| `cgame/voice/cg_voice.c` | `Voice_InLevel`: which voices of the voice chat go to the engine |
| `lib/steamaudio` | Valve's repository, a submodule |

## Checked

2026-10-07, Linux 64, without ears. A hidden client stands at a spot of a test level
(`tools/rdroom.py` of the port, with a speaker) and what it would send to the sound card goes into a file
(the "disk" sound driver of SDL). `tools/satest` of the port measures the file.

| What | Result |
|------|--------|
| Noise from a speaker in the open, 45 degrees to the right | louder ear -19.7 dB with HRTF, -19.4 dB panned, the old path -18.3 dB |
| Turned round | the other ear is the louder one |
| Behind a pillar | 22 dB quieter than the old path, highs 7.7 dB below the lows |
| In the next room, through two walls | 28 dB quieter, highs 8.4 dB down. The old path only knows the distance |
| A burst of 10 ms, no reflections | silence after it, as on the old path |
| The same in a room of 13 x 13 x 6.5 m | 40 dB down for good after 400 ms |
| In a hall of 39 x 26 x 16 m with walls of metal | after 670 ms |
| In a chamber of 6.5 x 6.5 x 3.3 m with carpet | after 140 ms |
| A speaker round a corner, a wall of metal in between | -57 dB through the wall alone, -45.6 dB with the path. The old path: -23.4 dB, as if there were no wall |
| Sounds the game starts one by one | arrive whole and ring like looped ones |
| Turning on the spot | each ear goes from -39 to -18 dB without a jump |
| Every recording | no gap, no click in the levels of 5 ms |
| `s_spatial 0` | the same levels to 0.1 dB as the engine from before this work |
| The triangles against the collision model | 15000 rays on five levels: 97 to 99.6 % stop within a unit. Where they differ it is a curve hit from behind, which the collision model lets a point through |
| The sound system started again, a change of level, bots fighting on mp/ffa3 | runs, the threads end when the sound system does |

The voice chat, the same day: a dedicated server, a client that sends in place of a microphone and a listener
365 units in front of him, whose own voice device writes into a second file (`tools/satest/voice.sh`).

| What | Result |
|------|--------|
| Noise with the spectrum of speech, on the voice chat's own device | -28.9 dB in each ear |
| The same through the engine | -28.9 dB with HRTF, -29.1 dB panned, and nothing on the own device |
| A tone of 440 and 3000 Hz, 10 s | no hole in the levels of 5 ms |
| The tone in sight, 365 units away | 440 Hz at -22 dB. On the voice chat's own device: -21.5 dB |
| The listener in the next room, 625 units away, a wall in between | 440 Hz at -47.9 dB. On the own device, with its muffling: -34.7 dB |
| `cg_voiceMuffle 0`, `s_spatial 0` | the voice comes from the own device, nothing from the engine |

The "disk" sound driver of SDL takes 0.6 % more sound a second than there is. A listener on it runs out of
voice every 3 s and the codec makes up a frame, which is a hole of 40 ms in a tone. The numbers above are from a
driver buffer of 882 samples, with which it runs 0.4 % slow (`tools/satest/clock.py`). A sound card that is that
far off would do the same on either way of the voice.

Cost on mp/ffa3 with bots fighting (AMD Ryzen 9 5900X 12-Core Processor, two threads for a pass): a pass of
reflections and paths 21 ms, in its own thread, about seven a second. In the thread of the game the sound costs mostly under 0.5 ms a frame
and up to 2 ms, most of it the reverb. The paths of mp/ffa3: 740 probes, under a second, a file of 660 kB.

Builds: Linux 64, and the client and the modules for 64 bit Windows with mingw (built, never run).

## Limits

- Nobody has heard it. Volumes, how strong the reverb is and how much a wall lets through are from
  measurements and from Valve's numbers, not from listening.
- Doors and lifts do not block sound. They are brush models that move, the mesh is the world that does not.
- One reverb, of where the listener stands. A sound in another room rings like the listener's room, and has no
  reflections of its own.
- What a voice is given reaches the ear after `s_spatialAhead` (0.1 s): a turn of the head is heard that much
  later. If the game stands still for longer than that, the sounds of the level have a gap while the others
  play on.
- A sound that starts gets its reverb up to 0.1 s late.
- A voice of the voice chat behind a wall without a way round it is 13 dB quieter than the voice chat makes it
  on its own (see "Checked"): the wall is the wall of every other sound. Whether that is too quiet to talk
  through is for ears to say. `cg_voiceMuffle 0` takes the voices out of it.
- The voice chat looks at doors when it muffles a voice itself. Here a door is not in the way, as for every
  sound.
- A voice is 0.1 s later than on the voice chat's own device. That number is from the code, not measured.
- Not tried: more than one speaker at a time, a speaker who moves, a change of level while somebody talks.
- Not run: Windows and macOS. Not built: macOS, 64 bit Windows.
- 22 kHz (`s_khz 22`) is refused.
