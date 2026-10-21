# Sound that knows the level

The sound of the game is a mixer from 2003: every sound has a volume for the left and one for the right ear,
nothing else. It does not know that there is a wall between a sound and the listener, how large the room is or
that a sound has to come round a corner. `v_spatial 1` changes that for the sounds in a level, with Steam Audio
of Valve doing the acoustics. It is not a JA+ feature.

It is in the engine, so only a player with this engine has it. Nothing of it is in the protocol: a server and
the other players notice nothing. What a game module does for it is in the cgame, which hands the voices of the
[voice chat](voice.md) and the places of doors and other brush models to it when the engine says it takes them,
and which draws what `v_spatialDebug` shows.

The client in a browser can have it too. It is built and run differently there, see "In a browser".

The owner listened to it once, on 2026-10-17, and what that day and 2026-10-21 changed has not been heard (see
"Limits"). Everything under "Checked" is measured from recordings, how it sounds is not judged.

## For players

Put the library of Steam Audio next to the program (see "The library"), then in the console:

```
v_spatial 1
snd_restart
```

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_spatial` | 0 | 1: on (latched: from the next `snd_restart` on). Needs `s_khz 44` and stereo |
| `v_spatialHrtf` | 1 | 1: direction for headphones (HRTF). 0: panned for speakers |
| `v_spatialReflections` | 1 | 1: the room around the listener rings. 0: no reverb |
| `v_spatialReverb` | 1 | how loud the reverb is |
| `v_spatialPaths` | 1 | 1: a sound comes round corners |
| `v_spatialPathLoss` | 1 | 0: a sound round a corner only gets duller, without what bending costs |
| `v_spatialShock` | 1 | how strong the thump of an explosion is: its lows, in both ears. 0: none, 2: twice as much |
| `v_spatialRays` | 2048 | rays of a pass of the reflections, 256 to 8192. Taken at the next `snd_restart` |
| `v_spatialSources` | 8 | how many of the loudest sounds have reflections of their own, 0 to 16. 0: all ring with the room of the listener, as before 2026-10-17 |
| `v_spatialThreads` | 0 | threads for a pass and for a bake. 0: a pass gets a quarter of the cores (1 to 4), a bake half of them. Above 0: that many for both, at most the cores of the machine. Taken at the next `snd_restart`. Not in a browser page |
| `v_spatialPhysical` | 0 | 1: a sound fades with the distance as a real one does (1/r) and not as the game fades it, loses its highs in the air, and arrives at the speed of sound |
| `v_spatialBounces` | 16 | how often a ray is followed off a wall. Fewer cut the tail of a large room |
| `s_doppler` | 1 | the doppler. With `v_spatial` it is the one of this file, see "Doppler" |
| `v_spatialAhead` | 0.1 | seconds of sound rendered ahead. Less follows a turn of the head sooner, more survives a longer hitch |
| `v_spatialBudget` | 0.75 | only in a browser: milliseconds of a frame the passes of the reflections take on average, 0.1 to 8 |
| `v_spatialEcho` | 0 | 0: the echo is worked out as you go, with echoes of their own for the loudest sounds. 1: it is worked out once per level and kept in a file, see below. Not in a browser |
| `v_spatialDebug` | 0 | shows in the level what becomes of the sounds, see below. 1: all, or the sum of 2 the sounds, 4 the ways round corners, 8 rays off the walls, 16 the numbers and the wall under the crosshair. Not stored |

In a browser three defaults are others: `v_spatial` 1, `v_spatialRays` 1024, `v_spatialBounces` 8, and no sound
has reflections of its own there (`v_spatialSources` does nothing).

With `v_spatialEcho 1` the echo is not worked out while you play. It is worked out once per level, in the
background after the paths, and kept in `steamaudio/<level>_<checksum>_v2.echo` in the profile. It is looked up
at the probes where you stand. So it does not flutter while you move, all sounds ring with the room you are in,
and no sound has echoes of its own (`v_spatialSources` does nothing). The bake takes Valve's numbers, those of
the bake of their Unity plugin: 16384 rays and 1024 diffuse samples, whatever `v_spatialRays` is. The bounces are
`v_spatialBounces` as it is when the bake starts, and until it is done the echo is worked out as you go. On
mp/ffa3 it was measured on 2026-10-21: 3960 probes in 21.3 s, a file of 28.7 MB, a pass about 1 ms. Nobody has
listened to it.

Until 2026-10-21 the bake took the live `v_spatialRays` and 32 diffuse samples, and the file was `_v1`. On
mp/ffa3 that was 3960 probes in 3.2 s at 2048 rays and 16 bounces, a file of the same size. The files of `_v1`
stay in the profile and are not read, so every level is baked again.

### The menu

The sound page of the setup menu has these entries for it (the new look; the classic menus only have the first
three). "3D Sound" is a preset:

| Preset | `v_spatial` | Reflections | Rays | Bounces | Paths | Sources |
|--------|-------------|-------------|------|---------|-------|---------|
| Off | 0 | | | | | |
| Low | 1 | 0 | | | 0 | |
| Medium | 1 | 1 | 1024 | 8 | 0 | 4 |
| High | 1 | 1 | 2048 | 16 | 1 | 8 |
| Very High | 1 | 1 | 8192 | 32 | 1 | 16 |

High is the defaults. Low is direction and muffling behind walls without a ray pass. The numbers of Medium and
Very High are a choice inside what the engine clamps to, their cost was not measured. The entry shows "Custom"
when the cvars are none of the rows. Choosing a preset restarts the sound when it switches `v_spatial` or the
rays, the rest is read while the game runs. "Output" is `v_spatialHrtf` (Headphones or Speakers), "Reverb" a
slider on `v_spatialReverb`, "Echo" is `v_spatialEcho` (Real Time or Precalculated, not in a browser, and not
part of a preset). "Room Reflections" (`v_spatialReflections`), "Echoes of Their Own"
(`v_spatialSources`: Off, 4, 8, 12, 16) and "Sound Around Corners" (`v_spatialPaths`) are parts of the preset:
changing one of them says at once which preset the cvars are now, or "Custom" (their action runs
`uiScript getsoundsetup`). "Real Sound Travel" (`v_spatialPhysical`) and "Doppler Effect" (`s_doppler`) are
not in any preset: they are a matter of taste, not of what a frame costs. The menu does not know whether the
library is there: without it the console says so and the sound stays as it is.

Operated by mouse on 2026-10-17 in a hidden client (`v_uiMouse`, clienttests `ui_sound_rows.cfg` and
`ui_sound_preset.cfg` of the port): the left arrow of "Echoes of Their Own" from 8 to 4, the right one to 12,
OFF and ON of "Real Sound Travel", the preset row then "Custom", and High to Very High set 16 echoes, 8192 rays
and 32 bounces. The entries were seen in a picture at 1280x720 in English only.

The entry is a cvar of the ui (`v_spatialQuality`) that is worked out from the cvars of the engine by
`uiScript getsoundsetup`, and it is not stored itself. The menus of the game run that script when the tab of the
sound is clicked. The Setup screens of the new look (`ui/vision/setup.menu`, `ingame_setup.menu`) have no such
tab action, and until 2026-10-12 nothing ran it there: the entry said "Off" after every start of the game,
whatever the engine had stored, and the owner read that as "they don't get saved" (his config had Very High all
along). A click on the entry then went on from Off and wrote that preset over what was stored. Now
`tools/menus/options.py` puts the script into what the screen does when it opens. Checked with a hidden client
whose config has Very High (`tools/uitest/soundsetup.sh` of the test tools): the cvar did not exist with the
screen open before, it is 4 now and the row says Very High.

`v_spatialInfo` says whether it runs, what the level is made of, which tracer the rays use (Embree or Steam
Audio's own), every sound with how much of it is in sight, what the threads take and the state of the paths. One
of its lines is what the sound of the level took of the
thread of the game since the command was last given: the frames, the sounds in them, the microseconds of a frame
by what they went into, and the frame that took most.

`v_spatialCapture <seconds>` writes what the sounds of the level give the mixer to `steamaudio/capture.wav` in
the home path (16 bit, two channels), as it was heard. It is for measuring without ears, see "In a browser".
What the mixer plays itself is not in it: the sounds of the own player, the menus, the music.

`v_spatialDebug` shows in the level what becomes of the sounds. It is the sum of 2 the sounds (a label over each),
4 the ways round corners, 8 rays off the walls and 16 the numbers and the wall under the crosshair with its
material; 1 is all of them. It is not stored. The cgame draws it (`cgame/cg_spatialdebug.c`, through
`ext.S_SpatialDebug`, with the read-only cvar `v_cgameSpatialDebug`). Steam Audio does not show the rays it
traces. The rays drawn are traced by the engine through a copy of the mesh and bounce off the walls as off a
mirror, where Steam Audio scatters some of them. They show which walls a sound can reach and what they are made
of, not what Steam Audio did with them. Sounds no voice was free for are shown too.

`v_spatialSaveScene` writes the walls Steam Audio has, with the brush models where they are now, to
`steamaudio/scene.obj` in the home path.

What changes: a sound behind a wall or a shut door is quieter and duller, by what it is made of. A sound has a
direction that is more than left and right (with headphones). The room around the listener rings, a hall longer
than a chamber. A sound in the next room arrives through the doorway, and the loudest sounds ring off the walls
around them, in their own room as well as in the listener's. A sound that comes closer or goes away has a
doppler. The voices of the voice chat are sounds in the level too, see "The voice chat". The sounds of the own
player stay as they are, in both ears, and the room rings from them like from any other sound. An explosion has
a thump. The menus, the announcer and the music are not touched.

What does not, unless `v_spatialPhysical` is 1: how loud a sound is at its distance is still what the game makes
of it, and it is heard at once.

## The library

The engine is not linked against Steam Audio. It loads `libphonon.so` (Linux), `phonon.dll` (Windows) or
`libphonon.dylib` (macOS) from the folder the program is in when the sound system starts. Without the file, or
with one of another version, it says so in one line and the sound is as it always was. The same when the library
refuses the context or has no HRTF for the sample rate: everything the engine was given is let go of in one
place. (Until 2026-10-21 the case of no HRTF did not let go of the Embree device.)

The file comes from Valve's release of the version the tree is built for, 4.8.1:
`steamaudio_4.8.1.zip` from github.com/ValveSoftware/steam-audio/releases, in it
`lib/linux-x64`, `lib/windows-x64`, `lib/osx`. It is 24 to 50 MB. The zip also has libraries for 32 bit, which
nothing here uses: the tree is built for 64 bit only, and the build takes the 64 bit library of its system.

The build puts it there. cmake takes the library of the system it builds for from the folder `SteamAudioSDK`
(`lib/steamaudio-sdk` in the tree, which is not in git: the content of that zip, or a link to where it is
unpacked), copies it next to the client when the client is built and installs it with the client. If the folder
does not have the library, cmake fetches the zip once (181 MB), holds it against the checksum in
`codemp/CMakeLists.txt` and takes that one file out of it. `-DFetchSteamAudio=OFF` leaves the fetching out,
`-DSteamAudioURL=file:///path/steamaudio_4.8.1.zip` takes a zip that is on the disk already. Without a library
the client is built all the same, and says when it runs that the file is not there.

Steam Audio is under the Apache License 2.0, this engine under the GPL version 2. They stay two files, the way
the engine has always loaded the EAX library. Before giving out builds with the library in them, look at that
again: since 2026-10-08 a folder the build installed has the library in it.

## Building

Valve's repository is a submodule, `lib/steamaudio`, at the tag `v4.8.1`:

```
git submodule update --init
```

The engine only takes the header `phonon.h` from it. cmake copies that and the version header Valve's own build
generates into `<build>/steamaudio`, nothing else of the submodule is on the include path. Without the
submodule checked out, cmake says so and builds the client without `v_spatial`. `-DBuildJPSpatialAudio=OFF`
does the same on purpose. The client for a browser is the one build that compiles the library from the
submodule, see "In a browser".

## How it works

**The level** (`qcommon/cm_export.cpp`). Steam Audio traces rays in threads of its own, and the collision code
of the engine can not be called from a second thread. So it gets the level once, as triangles: every side of a
solid brush of the world, every facet of a solid curve. Each triangle has a material of the game (the ones of
the footsteps, `MATERIALS` in `game/surfaceflags.h`), mapped onto Valve's numbers for a few standard materials
(how much a material swallows, scatters and lets through). The sky swallows everything. mp/ffa3 is 50749
triangles and takes 0.1 s, during the loading screen.

Where the material of a triangle comes from: a level file can name it in the surface flags of a shader, and
few do (of the sides of the base levels, 4 in 100). So there is a table, `steamaudio/materials.txt` in
`zz_japlus_client_assets.pk3`: the name of a shader and a material, a line each. A line of the table goes
before what the level says. What neither names counts as "generic". A side that is not drawn (caulk, the back
of a wall) is taken as made of what the drawn side of its brush is made of, so that a wall is one thing for a
sound that goes through it. `v_spatialInfo` says how many triangles of the level got their material from the
table.

The table is read when the sound starts and kept: at a pure server that does not have the pk3 the file can
not be read. It is written by `japlus_port/tools/samat` (`collect.py`, `sheets.py`, `merge.py`): the material
a shader script names (`q3map_material`), and for the shaders nobody named one for, what the texture looks
like. Those are guesses from a picture, kept with their source in `materials.json` there. To put one right,
change its line in that file to `"source": "user"` with the right word and run `merge.py write`.

Doors, lifts and the other brush models of the level (`func_static`, panes of glass) are not in that mesh. Since
2026-10-21 (5200ad81) each is a mesh of its own in the scene the simulator traces in (an instanced mesh of Steam
Audio), placed once a frame where the cgame says its entity is. The cgame does that in
`cgame/cg_spatialmovers.c`, through `ext.S_SpatialMovers`; the engine says it takes them with the read-only cvar
`v_cgameSpatialMovers`. A brush model the cgame never named stays where the map has it, which for a door is
shut. One that is gone from the snapshot (broken, switched off) is taken out of the scene. A change goes into the
scene only while no pass of the reflections runs. So a shut door muffles a sound like a wall of what it is made
of, and an open one lets it through. The ways round corners are worked out from the world without the brush
models (see "Paths"), so a way through a shut door can still be found. Until 2026-10-21 doors and lifts were not
in the way of anything. What was checked of it and what was not is under "Limits".

The cgame puts the sounds of a brush model (the start, stop and loop sounds of a door or a lift) in the middle
of it, which is inside its own walls (`CG_SetEntitySoundPosition`, and the loop sound in `CG_EntityEffects`).
With the brush models in the scene those walls hid the sound. The lift of mp/ffa3 that carried the listener
was 0 % in sight and let through 0.04 of its lows and none of its highs, so moving doors and lifts were not
heard. The owner reported it. Since 2026-10-21 a place inside the bounds of a brush model of the scene goes out
of them along the line to the listener: to where that line leaves them and 0.5 m on (the radius a source has),
but no further than halfway to him (`S_SpatialOutOfBrushes` in `snd_spatial_level.cpp`). The bounds come from
`CM_ModelBounds` and are turned and moved as the brush is. Steam Audio takes that place for what is in the way,
the ways round corners and the reflections. The direction from the listener and the game's fade by distance stay
as they were. A place is not moved when the listener is inside the bounds too. `v_spatialInfo` says "from the
side of its brush model" for such a sound, with the place.

**The sounds** (`snd_dma.cpp`, `S_SpatialGather`). Once a frame every sound with a place in the level is
named: the channels of the mixer, and every loop sound on its own (the old mixer adds loop sounds of one
sample up into one channel, which has no place). Each keeps a voice for as long as it plays. There are 128
voices (`SPATIAL_VOICES`), shared with the voice chat and the boombox. A sound no voice is free for goes to the
old mixer, which knows no walls.

Since 2026-10-21 (98ba02b9) the sounds of the level are heard from the head of the player and not from the
camera (`S_SpatialHearing` in `snd_dma.cpp`; `CG_HearingPoint` in `cg_view.c` must stay alike). The place is
above the origin of the player, at the height of the camera but held between 12 and 36 units above the origin.
When the camera is more than 160 units away (a spectator, a dead player) it is the camera. Left and right stay
those of the camera. In third person the camera circles the player when it turns, which moved what the walls
hide and bent every sound by its doppler.

**What is in the way.** For each voice, every frame: 16 rays from the listener to points around the source say
how much of it is in sight, and the walls in between say what gets through. A wall is a brush with two sides.
Steam Audio takes the square root of what all the sides it crossed let through when there are more than one
(`DirectSimulator::transmission()` of 4.8.1), so Valve's numbers go into the mesh as they are and are the number
of a wall. Until 2026-10-17 the mesh had their square root as well: a wall let through the square root of its
number (concrete in the highs 0.03 and not 0.001), and two walls what one should.

What the walls take from a sound fades in and out over about 50 ms (`SPATIAL_WALL_MSEC`), so a step behind a
wall is a quick fade and not a cut.

**Rendering** (`snd_spatial_render.cpp`). The old mixer paints the next fifth of a second again in every frame,
which is how a sound that starts is heard after 50 ms. The effects of Steam Audio keep state from block to block
and can only run once and in order. So each voice is rendered ahead on its own, in blocks of 1024 samples, into a
ring of finished sound by the time of the mixer, and the mixer adds what the ring has for the samples it
paints. A sound that starts adds itself to what is there and is heard as soon as on the old path. What changes
for a voice (where it is, what hides it) reaches the blocks that are not rendered yet: `v_spatialAhead`.

**Reflections.** Rays from where the listener stands are followed through the level, and what comes back
becomes impulse responses: for a source where the listener stands, the room he is in, and for each of the
loudest `v_spatialSources` sounds one of its own, of what reaches the listener from where it is, off the walls
round it and through the openings between (Valve's real-time reflections, "Reflections" of their guide). A sound
of those goes into its own convolution as the game plays it at its distance, without the walls in its way: its
impulse response has them. Valve's response is that of a source as loud at 1 m as it is, falling with 1/r. The
source in the simulator has Valve's default distance model, so the response carries the distance from the walls
to the listener. Until 67d3d8b9 of 2026-10-17 the source was given the distance the game plays the sound at full
volume (`S_FullVolumeDistance`, `IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE`) and the sound went in as loud as
the game plays it times its distance. That is gone. The convolutions add into one mixer of Valve's
(`IPLReflectionMixer`), which is decoded with the room of the listener. Every other sound goes through the room
of the listener, as it arrives after the walls in its way, as all of them did until 2026-10-17.

Which sounds have their own is decided every frame by how loud the game plays them. Since 2026-10-21 (19f8b2a7) a
sound that has them keeps them while it plays and gives them up only to one 4 times as loud
(`SPATIAL_REFLECT_KEEP`), and not in its first second (`SPATIAL_REFLECT_HOLD`, 1000 ms). Steam Audio adds the
passes for a sound up as long as neither it nor the listener moves, and begins anew when a slot gets another
sound. So the echo of a boombox changed while nobody moved, when far loops took the slots back and forth. Until
then one that had them counted twice. A sound that gets them goes on through the room of the listener until a
pass with it in it is done. When a sound loses them or ends, they ring out on their own. A slot is about 7 MB
(its response, as Valve partitions it, and its convolution), so there are 16 at most.

A pass takes 20 to 70 ms (less since 2026-10-21, see "Checked"), so it runs in a thread. The game asks for a pass
at most every 50 ms and takes what the last one found. What a pass starts from (where the listener and the
sounds are, the rays) is handed to the thread when it is asked, and the thread sets it in Steam Audio itself
before the pass: Valve sets the inputs of the reflections in the thread that runs them (`iplSourceSetInputs` in
`phonon.h`). Until 2026-10-17 the game set them while a pass could be running.

The simulator is made for the rays a pass traces, on the desktop and in a page: Steam Audio spreads the rays it
was made for round the listener in the order of their direction, and a pass of fewer takes the first of them.
Until 2026-10-17 the desktop made it for 8192, and the 2048 of the default went into a quarter of the circle,
always the same one, so the walls on the other three sides did not ring. So `v_spatialRays` is taken when the
sound starts.

**Doppler.** The doppler of the game (`S_AddLoopingSound`) is only for loops the cgame gives a speed (missiles,
vehicles), and its factor is the square of the distance a second later over a hundred times the square of the
distance now, with no speed of sound in it: above 1 only when the sound would be ten times as far away a second
later, and then it only raises the pitch, by skipping samples, up to fifty times, also for a sound that goes
away. The listener's motion counts for nothing. With `v_spatial` that is not used. Each voice measures how fast
its distance to the listener gets shorter, from frames at least 1024 samples of the mixer apart, smoothed, and
reads its sound 1 + that speed / 343 m/s as fast (0.5 to 2), with the samples between interpolated
(`S_SampleRun`). That is every sound with a place, shots and steps too, from its motion and the listener's. A
jump faster than sound (a teleporter, the view going to another player) is no speed. A voice of the chat, a
song of the boombox and the own player have none. `s_doppler 0` switches it off. With `v_spatial 0` the doppler is
the game's.

A channel of the mixer is freed when the mixer's time is past the end of its sound. A voice that reads slower,
or later (below), reads on after that: the channel stays for it (`spatialTail` of `channel_t`), which an MP3 needs,
as it is decoded through its channel.

**Sound travel** (`v_spatialPhysical 1`). The game fades a sound from where it is at full volume (6.5 m for most,
19.5 m for a voice, 52 m for `CHAN_LESS_ATTEN`) in a straight line down to nothing 32 m further. With
`v_spatialPhysical` it is at full volume as far, then falls with 1/r, 6 dB for each doubling, and is not heard
below -50 dB (`S_SpatialFade`, which also gives the length of a way round a corner its fade). So it is as loud as
before near by, and louder further away: a sound at 38 m is -15 dB and not gone. A far sound loses its highs in the
air (Valve's default model for that). A sound the game starts is heard when it got here, at 343 m/s: 87 ms from
30 m. Its own reflections get it without that delay, as their response has the time from it to the walls and
from them in it. That is read a second time from the sound and is not possible for an MP3, whose reflections are
late by the way here. Loops are not late, they have no start.

**The own player.** What comes from the entity the game listens from (steps, shots, saber swings, the voice of
the character) is played by the old mixer, at full volume in both ears, as it always was. With reflections on
it also gets a voice that renders nothing but what goes into the reverb (`S_SpatialAddSend`): as loud as the
game plays it, no rays, no direction. The reverb is the one of where the listener stands, which for these
sounds is the right place. Left dry: what is started on the channels of the menus, the HUD and the announcer.

**Explosions.** The data that gives a sound its direction for headphones has none below about 200 Hz: both ears
get the lows at half volume wherever the sound is, where the old mixer gives the near ear up to all of it. An
explosion is mostly lows, so with headphones it lost more than other sounds. Its lows (a low pass at 120 Hz)
are added a second time, to both ears alike, past the direction and past the panning for speakers too, by
`v_spatialShock`. A wall takes from them what it takes from the lows of any sound. An explosion is a sound
with "explo" in the name of its file, or the hit of a rocket, a proton torpedo, a concussion missile or a
mine of a vehicle (`spatialShockPaths`). `v_spatialInfo` says "thump" behind such a sound.

**Paths.** For a sound the listener does not see, Steam Audio finds the ways round the corners from points it
has spread over the floors of the level (probes) and from which of them see each other. That is worked out
once per level, in another thread, the first time the level is loaded, and kept in
`steamaudio/<level>_<checksum of its walls>.paths` in the profile. Until it is done the level has no paths.
How loud a sound is at the end of a way is what the game makes of a sound at that distance.

A level where no floor is found for a probe has no paths, and the console says so. Its file is written all the
same (32 bytes), so the next load does not look again. The simulator is never given a set of fewer than two
probes (`SPATIAL_MIN_PROBES`). Until 2026-10-12 it was, and the game crashed a second after such a level was
loaded with `v_spatial` on, in the thread of the reflections: a pass asks every set it has for the probes near
the listener (`SimulationManager::lookupBakedReflections()` of Steam Audio 4.8.1), and one without a probe has
no tree to ask, which is a read at address 0. The map of the Vision Temple is such a level, t2_wedge too (its
file in the owner's profile has 32 bytes). Seen as five core dumps of the owner's game that day, reproduced
with a hidden and silent client (`s_initsound 1`, the sound device of SDL that plays nothing), and run again
with the fix, once without a file and once with the 32 bytes of the run before: both to their end.

A level that is left while its paths are still worked out, a restart of the sound and the end of the game tell
the bake to stop (`S_SpatialCancelBake()`). What a stopped bake has is thrown away unread. Until 2026-10-12 the
thread wrote it out for the file all the same, and Steam Audio 4.8.1 leaves the paths half made in the probes
when it is stopped (`PathBaker::bake()` adds what the stopped `BakedPathData` has): a crash in the thread of the
bake, in `iplProbeBatchSave`. Found with a bake of mp/ffa3 that took 3.8 s in an experiment and a restart of
the picture two seconds into it, and run again with the fix: the first bake is stopped, the second one ends.
With the 0.1 s the paths of mp/ffa3 take as the code is, nobody leaves in time; a large level takes longer.

The probes stand all over a level since 2026-10-17. Steam Audio spreads them over the cube from -0.5 to 0.5 as
the matrix places it (`generateUniformFloorProbes()`), and until then `S_SpatialBaker()` gave it the matrix of a
cube from 0 to 1: the box of the probes had its middle at the lowest corner of the level, only the quarter of a
level next to that corner had probes, and sounds found their way round corners only there. Behind a wall
anywhere else only what came through the wall was heard. (That is also why the Vision Temple had none.) Now the
matrix has the middle of the level. The probes are 3 m apart, and in a level where that is more than 4000
(`SPATIAL_MAX_PROBES`) the spacing grows by half until it is not: the work and the file grow with the square of
them. mp/ffa3 has 3960 at 3 m, 3.0 s in the background (Ryzen 9 5900X, 2026-10-17), the file 16.5 MB. The name of
the file got `_v2` in it then, so every level was worked out once more. The files of before stay in the profile and
are not read (`steamaudio/*.paths` without `_v2`, 454 MB in the owner's profile): they can be deleted.

Since 2026-10-21 the paths take Valve's numbers, those of the bake of their Unity plugin (`Baker.cs`). A probe
tries 4 points (was 1), 1 m from its middle (was 0.5 m). Probes up to 1000 m apart are asked whether they see
each other (was 30 m), and a way may be up to 1000 m long (was 100 m). The check of a way while the game runs
uses the same numbers. The file has `_v3` in it, so every level is worked out again, and the `_v2` files stay in
the profile and are not read. On mp/ffa3, in a silent hidden client (22 threads, Embree), the same 3960 probes
took 4.2 s (1.6 s with the old numbers) and the file is 21.7 MB (16.5 MB before). Nobody has listened to it.

**The voice chat.** The cgame plays the voices of its voice chat on a sound device of its own, which the
engine knows nothing of. With `v_spatial` on, the engine sets the cvar `v_spatialVoice` to 1 and the cgame
hands over what it decoded of a player, 20 ms at a time, with where his mouth is and how loud he is at his
distance (`ext.S_SpatialVoice` in `cgame/cg_public.h`, a call only this engine has: the cgame looks at the
cvar before it touches it). The engine brings it to the rate of the mixer and it is a voice like the others:
the walls, the reverb, the ways round corners. How loud a voice is at its distance stays what the voice chat
makes of it (`vs_voiceRange`, `v_voiceVolume`), and `s_volume` does not change it. A voice is decided when its
speaker starts to talk and stays on its way until he stops.

**The boombox.** Its songs come to the cgame as the voices of speakers behind the players ([boombox.md](boombox.md)),
and the same call takes them: clientNum `MAX_CLIENTS` and up are sixteen more streams, behind the one of the own
voice. The engine says how many in the cvar `v_spatialBoxes` (16 while `v_spatial` runs, else 0); an engine that
does not say it gets no box, and the cgame plays the songs on its own device.

Not handed over, and played by the cgame as before: a chat without a range (`vs_voiceRange 0`), a speaker the
server does not show, a spectator, and every voice for a player with `v_voiceMuffle 0`, who does not want
voices muffled. The same goes for any other engine, and for this one with `v_spatial 0` or without the library.

What the own player says goes the same way as his other sounds: into the reverb and nowhere else. The cgame
hands every frame of the microphone that goes out to the engine too (`ext.S_SpatialOwnVoice`, a second call,
which an engine has that sets the cvar `v_spatialOwnVoice` to 1). He does not hear his voice, he hears the room
he stands in ring from it. That room is in his ears a moment after the word, while he talks on, so it gets
little of him: at `v_voiceOwnReverb 1` 0.15 of what the voice of a player next to him would give it
(`VOICE_OWN_REVERB_GAIN`, 0.3 until 2026-10-11, which was found too loud in the game). And it has a cap: a frame of the microphone that is louder than a tenth of full
scale (`VOICE_OWN_REVERB_CAP`, as the voice activation measures it) goes in that loud and no louder, turned
down within the frame, and comes back over the next frames, so a raised voice does not raise the room.
`v_voiceOwnReverb` scales what the room gets, 0 turns it off, and the voice part of the setup menus has a
slider for it ("Own Voice In The Room", 0 to 2). Not handed over: a spectator's voice, and a chat
without a range.

The other sounds are there ahead of time, a voice is not. It is rendered as far as what arrived of it reaches,
a block behind where the others are rendered to, so it is heard about `v_spatialAhead` (0.1 s) later than the
cgame would play it.

**Threads.** The engine had none. Now: one for the passes of reflections and paths, one in the background while
the paths of a level (and with `v_spatialEcho 1` its echo, after them) are worked out, and what Steam Audio
starts inside them. The game never waits for one, except when the sound system is shut down. A level that was
loaded is handed to the simulator in the first frame no pass runs, until then the old mixer plays.

Since 2026-10-21 `v_spatialThreads` sets the threads of Steam Audio. With 0 (the default) a pass gets a quarter of
the logical cores, at least 1 and at most 4 (`SPATIAL_PASS_CORES`, `SPATIAL_PASS_THREADS`), and a bake gets half of
them (`SPATIAL_BAKE_CORES`). A number above 0 is the threads for both, at most the cores of the machine. It is read
when the sound system starts, so a change needs a `snd_restart`. A browser page has one thread. Valve's plugins
for Unity and Unreal give real-time work 5 % of the cores and a bake 50 %. Before, a pass and a bake were shared
between all cores but two (22 threads on the owner's machine). A friend reported a high CPU use of the client. A
new pass also started 50 ms (`SPATIAL_REFLECT_MSEC`) after the last one was asked for, so after a pass of 50 ms
or more the next one came at once and on a slower CPU the threads never rested. Now a pass also waits until twice
what the last pass took has gone by since it was asked (`SPATIAL_REFLECT_DUTY`), so the threads rest at least as
long as they work. That is not so in a browser page, which has `v_spatialBudget`. `v_spatialInfo` prints the
threads for a pass and for a bake. The rays are traced by Intel's Embree where the library has it
(`IPL_SCENETYPE_EMBREE`, loaded optionally). A library without it falls back to Steam Audio's own tracer, and
`v_spatialInfo` says which one is in use. What that took off a pass is under "Checked".

Steam Audio 4.8.1 keeps one copy of what a pass found of the ways round the corners.
`SimulationManager::simulatePathing()` writes it with a plain memcpy where the pass runs, and
`iplSourceGetOutputs()` hands out a pointer to it. Only the impulse responses of the reflections go through a
triple buffer. The voices read that pointer in the thread of the game while the next pass could be writing it
(Valve's own FMOD plugin does the same). Found by reading Valve's code. Since 2026-10-21 each voice gets a copy
once a pass is done and while the thread is idle (`S_SpatialTakePaths`), and renders from the copy.

The code of the sound system was one file, `client/snd_spatial.cpp`, until 2026-10-21.

| File | Content |
|------|---------|
| `client/spatial/snd_spatial.cpp` | the library, the cvars, start and end of the sound system |
| `client/spatial/snd_spatial_level.cpp` | the walls of the level and of its brush models, what they are made of, a level that waits to be taken over |
| `client/spatial/snd_spatial_bake.cpp` | what is worked out once per level and kept in a file (paths, echo) |
| `client/spatial/snd_spatial_sim.cpp` | what is in the way of each sound, the thread of the reflections and the paths |
| `client/spatial/snd_spatial_voice.cpp` | which sound of the mixer is which voice, the voice chat and the boombox |
| `client/spatial/snd_spatial_render.cpp` | the voices and the reverb rendered ahead into the ring the mixer adds from |
| `client/spatial/snd_spatial_debug.cpp` | `v_spatialInfo`, `v_spatialCapture`, `v_spatialDebug` |
| `client/spatial/snd_spatial_page.cpp` | the client in a browser (`__EMSCRIPTEN__`) |
| `client/spatial/snd_spatial_local.h` | what these files share |
| `client/spatial/snd_spatial.h` | what the rest of the engine calls |
| `qcommon/cm_export.cpp` | the world as triangles |
| `qcommon/spatial_debug.h` | what the engine and the cgame share for `v_spatialDebug` |
| `client/snd_dma.cpp` | `S_SpatialGather`: which sound is a voice in the level and which only goes into the reverb. `S_SpatialHearing`: where the sounds are heard from. The fade by distance as functions both paths use (`S_AttenuateDistance`) |
| `client/snd_mix.cpp` | `S_SampleRun`: samples of a sound as floats. The mixer leaves out what the new path plays |
| `cgame/voice/cg_voice.c` | `Voice_InLevel`: which voices of the voice chat go to the engine |
| `cgame/cg_spatialmovers.c` | where the brush models are, handed to the engine through `ext.S_SpatialMovers` |
| `cgame/cg_spatialdebug.c` | draws what `v_spatialDebug` shows, through `ext.S_SpatialDebug` |
| `cgame/cg_view.c` | `CG_HearingPoint`: must give the place `S_SpatialHearing` gives |
| `lib/steamaudio` | Valve's repository, a submodule |
| `cmake/Modules/SteamAudioWasm.cmake` | for a browser: builds the library, `phonon.wasm`, from the submodule |
| `lib/steamaudio-wasm` | for a browser: what is built in place of a file of Valve or laid over one, see "In a browser" |

## In a browser

The client in a browser ([web.md](web.md)) has `v_spatial` when it is built with `-DBuildJPSpatialAudio=ON`. The
toolchain file for the browser leaves that off. Two things are different there, the rest is the same code:
what is in the way of a sound, the direction for headphones or speakers, the reverb, the ways round corners,
the thump, the sounds of the own player, the voices of the voice chat.

The library is a file of its own there too, `phonon.wasm`, and the engine is not linked against it. Valve gives
out no library for WebAssembly, so this one is built from the submodule, as a module next to cgame, ui and the
renderer in the folder the server hands out. The page takes it along when the server has it. The engine loads
it when the sound system starts and looks its functions up by name, the same code as on the desktop with another
file name, and lets go of it when the sound system stops. Without the file the page says so in one line, the
engine in one more, and the sound is as it always was: a server that is not to have `v_spatial` leaves
`phonon.wasm` out of its web folder.

What is different is that a page has one thread. What the two threads of the desktop do, the game does itself:

| | Desktop | Page |
|---|---------|------|
| The level as a mesh | during the loading screen, handed to the simulator in the first frame | during the loading screen, handed over right there |
| A pass of the reflections | in a thread, about seven a second | the game traces the rays, a slice in a frame. `v_spatialBudget` is the time of a frame the passes take on average. A slice is at least 32 rays with their bounces, and what makes the impulse response at the end of a pass is 2 ms in one piece, so some frames go without a slice. A pass of the defaults is spread over about half a second |
| The ways round corners | looked up in each pass | looked up by the game every 50 ms: 20 microseconds for two sounds behind a wall |
| The paths of a level | worked out in a thread the first time it is loaded | worked out while the level loads, the first time, and kept in the browser like the config. Only for a level of up to 2000 probes |

A pass traces fewer rays by default than on the desktop (1024 with 8 bounces, the "Medium" of the menu). As on
the desktop the simulator is made for exactly the rays a pass traces, see "Reflections". A page has no
reflections of a sound of its own: a pass there is spread over many frames already. The precalculated echo
(`v_spatialEcho`) is not there either, and nor is Embree. What a page does itself is in
`client/spatial/snd_spatial_page.cpp`. Nothing of the changes of 2026-10-17 and 2026-10-21 was built or run for
a page (the client in a browser is switched off).

`v_spatial` is 1 by default there, so whether a page has it is a matter of the file: it is on where the server
has `phonon.wasm` and off where it has not. With the defaults the sound of the level took 1.1 ms of a frame
where it was measured, with a few sounds on a fast processor. That is a choice by the numbers below and by
nothing else: nobody has listened to it.

What a player in a browser has less of:

- The reverb follows him about twice a second, not seven times: in another room it is the old room's for up to
  half a second longer. More `v_spatialBudget` makes that shorter, more rays or bounces longer.
- A pass has half the rays and half the bounces of the desktop's default, so the reverb of a large hall is
  rougher and its tail ends sooner. The cvars are the same ones: twice the rays or bounces is a pass twice as
  long.
- A level of more than 2000 probes has no sound round corners. The work and the memory the paths take grow with
  the square of the probes, and nothing can work them out later next to the game. mp/ffa3 has 740. The console
  says so when such a level loads, and `v_spatialPaths` says it when it is switched on in one.
- A level loads longer the first time: by the 0.4 s of its paths on mp/ffa3, by about 3 s at 2000 probes if the
  time grows as said. Both are from before 2026-10-21: the paths now take Valve's numbers (see "Paths"), which
  is longer on the desktop, so a page waits longer too. By how much was never measured.
- `phonon.wasm` is 6.2 MB more to fetch, and with it the engine needs 60 to 80 MB more memory, see "Measured".

### Building it

cmake does all of it (`cmake/Modules/SteamAudioWasm.cmake`). It needs the program `patch`, and once the network:

| Option | Default | Meaning |
|--------|---------|---------|
| `BuildJPSpatialAudio` | OFF with the toolchain file for the browser | ON builds the client with `v_spatial`, and `phonon.wasm` into the web folder of the build |
| `FetchSteamAudio` | ON | fetch the sources of FlatBuffers and PFFFT when `SteamAudioDeps` does not have them (1.3 MB, once) |
| `SteamAudioDeps` | `<build>/steamaudio/deps` | the folder with `flatbuffers` and `pffft` in it |
| `SteamAudioFlatBuffersURL`, `SteamAudioPFFFTURL` | empty | another address for an archive, `file://` for one on the disk |

FlatBuffers and PFFFT are not in the submodule. They are fetched as the archives GitHub makes of the commits
Valve's `core/build/dependencies.json` names, and held against a checksum in the cmake file. Without them, or
with another checksum, cmake says so and builds the client without `phonon.wasm`: it then asks for the file
when it runs, like a desktop client without its library. flatc, the compiler of FlatBuffers, is built with
Emscripten and run with the node of the SDK, so the build machine needs no compiler of its own.

Valve's own cmake files are not used: they want their dependencies where their Python script puts them and
copy the result into the folders of their plugins, inside the submodule. What is built is what they build for
this platform: the sources their CMakeLists names for the core, with the vector code for NEON, which a header
of Emscripten maps to the SIMD of WebAssembly. Left out: Embree, Radeon Rays, TrueAudio Next, IPP, MKL, FFTS,
which this platform does not have, and MySOFA with its zlib, which reads HRTF data from SOFA files the engine
never uses (`IPL_DISABLE_SOFA`, and an empty `mysofa.h` for the header Valve's code still asks for).

The submodule stays as it is. `lib/steamaudio-wasm` has what is different:

| File | What it is |
|------|------------|
| `thread_pool.cpp` | built in place of Valve's file of that name. Their thread pool starts threads, which a page can not, and does no job without them. This one lets the caller do the jobs, and can stop when a time is up |
| `simulation_manager.cpp.patch` | laid over a copy of Valve's file in the build folder: a pass of the reflections that goes on where the last call stopped |
| `hrtf_database.cpp.patch` | the same way: an error of Valve's code for NEON, see below |
| `phonon_slices.h` | the two functions that give a pass its time. `phonon.wasm` has them next to Valve's, and the engine looks them up like those |
| `mysofa.h` | empty |

A patch that does not fit line for line stops the build. That is what happens when the submodule is moved to a
version whose files differ there.

The error: with a direction between two measured ones, Steam Audio mixes the neighbours' filters (the engine asks
for that, `IPL_HRTFINTERPOLATION_BILINEAR`) and brings each phase back into one turn of the circle. Its function
for rounding in `neon_float4.h` adds a half and rounds nothing, so every phase came out as pi or -pi: a filter
as loud as it should be, with no timing left in it. The patch has that step done one value at a time, by code
of Valve that rounds. By the source every build of Steam Audio 4.8.1 for NEON has this, Valve's own for phones
too, which was not tried. The library for the desktop does not have it: that was measured, see "Checked".

FlatBuffers at Valve's commit does not compile with the clang of Emscripten 6 as it is (a function in a
template that nothing calls). The build gives the compiler `-fdelayed-template-parsing` for it and leaves the
header alone.

`phonon.wasm` is 6,184,239 bytes (5.2 MB as gzip): 5.37 MB are the HRTF data Valve builds into the library, for
every sampling rate, 0.67 MB are code. From outside it has the functions of `phonon.h` and the two of
`phonon_slices.h`, nothing else of it is visible. The engine's `.wasm` is 3,887,181 bytes with `v_spatial` and
3,853,061 without `client/spatial` (one file, `snd_spatial.cpp`, when this was measured), and nothing of Steam
Audio.

Only `phonon.wasm` holds SIMD instructions (Chrome 91, Firefox 89, Safari 16.4). A browser that does not know
them can not compile that file, which the page takes like a file that is not there. That was not tried: no such
browser was at hand.

The module is linked with `-Wl,-Bsymbolic`, as the renderer is: the engine's loader makes a new instance for
every load, and what a module has visible of its own it would otherwise look up in the first one. And every
function of `phonon.h` stays a function of its own in the file, where the optimizer of Emscripten would make
one of all that have the same code (145 of the 216 do, all that release a handle for one). The loader of the
engine gives a slot of the function table back once for each name it was asked for: with one function behind
several names the slot went back several times, was handed out again as often, and after the first
`snd_restart` a call of one function ran another. The switch for the optimizer is in `SteamAudioWasm.cmake`
and can go when the loader (`shared/sys/web/sys_web.js`) notes a slot once.

Steam Audio is under the Apache License 2.0 and FlatBuffers too, PFFFT under a BSD license, this engine under
the GPL version 2. They stay two files here as on the desktop. One thing to look at before a build is given
out: `thread_pool.cpp` and the two patches of `lib/steamaudio-wasm` are built into `phonon.wasm` and carry the
licence header of this tree.

### Measured

2026-10-09 and 2026-10-10, AMD Ryzen 9 5900X, Chromium 153 without a window on the graphics card, 60 frames a
second, while other work ran on the machine. The numbers are of the build that had Steam Audio linked into the
engine, which the first version was, and were taken again with `phonon.wasm` where a row says so: a call into
the library goes through a function pointer either way, and what a frame takes did not move. The demo `webdemo`: mp/ffa3, a spectator at a fixed place, one bot fighting
behind a wall and others far off. That is 3.1 sounds at a time and 5 at most, not a fight. Time is what the
sound of the level took of the page's one thread, by the counters of `v_spatialInfo`. A browser counts time in
steps of 0.1 ms: the averages are over 900 frames each, the one frame that took most is that coarse.

| What | Result |
|------|--------|
| The mesh of the level, 50749 triangles | 112 to 138 ms, in the loading screen. The desktop: 113 ms |
| The paths of mp/ffa3, 740 probes | 0.3 to 0.4 s, in the loading screen of the first visit. Later from the browser's storage |
| A frame with the defaults | 1.09 ms: 0.05 what is in the way, 0.12 the sounds, 0.18 the reverb, 0.75 the passes. With `phonon.wasm`: 1.10 ms (0.04, 0.13, 0.18, 0.75) |
| The frame that took most | 2.8 to 4.0 ms. With `phonon.wasm`: 2.9 ms |
| A pass, 1024 rays and 8 bounces | 21 to 28 ms in 27 to 32 slices over 0.44 to 0.57 s, the longest slice 1.7 to 2.7 ms. With `phonon.wasm`: 20 to 24 ms in 26 to 30 slices, the longest 1.9 ms |
| The same pass in 4 slices (`v_spatialBudget 8`) | 20 ms. In many slices it is longer: the game has the processor's caches in between |
| A pass in one piece with other numbers | 2048 rays and 16 bounces 58 to 64 ms, 1024 and 8: 16.6 ms, 512 and 8: 10.6 ms, 256 and 4: 3.9 ms. About 2 ms of each are the impulse response. Measured before the simulator was made for the rays it traces, so with the rays of a part of the circle |
| `v_spatialBudget` 0.5, 1.5 | a frame 0.86 and 1.81 ms, a pass over 0.70 and 0.22 s |
| `v_spatialReflections 0` | a frame 0.19 ms, the same with `phonon.wasm` |
| and `v_spatialPaths 0` | 0.16 ms. With `v_spatialHrtf 0`: 0.09 ms |
| One more sound | about 0.05 ms a frame with HRTF, 0.03 without: 0.01 what is in the way, 0.02 its samples, 0.02 direction for headphones. Measured by doing the effects of each sound up to 21 times |
| The desktop, the same demo | a frame 0.12 to 0.14 ms without reverb (page: 0.16 to 0.19) and 0.41 ms with it, a pass of 2048 rays and 16 bounces 36 ms in its thread |
| Memory of the engine in use | at the menu 101 MB without and 161 MB with `v_spatial`, in mp/ffa3 159 and 240 MB. With `phonon.wasm` 5.4 MB of it are the library's own data, taken when it is loaded and given back when it is unloaded |

By these numbers the mixer's 32 channels and 32 loop sounds, all in the level at once, would be about 3.5 ms a
frame on this machine. A cap on the sounds that get HRTF would take a third off that and was left out: what a
frame costs most are the passes, which have their own limit.

### Checked

The same day, without ears. Nobody has listened to it in a browser either.

Each effect of Steam Audio fed with the same noise, in Valve's library for the desktop and in the one built
here, run with node:

| Effect | The two builds apart |
|--------|----------------------|
| What a wall lets through, panning, HRTF of a measured direction | nothing, sample for sample |
| HRTF between measured directions, before the patch | the same loudness to 0.15 dB, and up to 3 dB apart in a band: another filter. Against the measured direction next to it Valve's fits with 0.60 to 1.00 (ten of twelve above 0.9), the one built here with 0.28 to 0.57 |
| The same after the patch | a ten thousandth of the peak |
| Ambisonics to two ears, turned or not, and the effect of the paths | a hundred thousandth of the peak |
| The reverb between two walls, 2048 rays | the one built here has 0.4 dB more energy (three runs against five). Two runs of Valve's are up to 0.3 dB apart, the rays bounce by chance. Where the rest comes from was not looked for |

18 seconds of the demo in the page and on the desktop (a hidden client with the sound driver "dummy"), each
written by `v_spatialCapture` from the first frame of the level on, compared by `tools/satest/compare.py` of the
port: the level of each ear as a whole and in four bands from 100 Hz to 16 kHz.

| What | Page and desktop apart |
|------|------------------------|
| The desktop twice | 0.2 dB at most |
| Direct sound with HRTF | 0.1 dB at most, left against right the same. With `phonon.wasm`: the same |
| Direct sound panned | 0.2 dB |
| With the ways round corners | 0.2 dB, and 0.5 and 0.8 dB from 400 to 1600 Hz |
| With the reverb | 0.5 dB at most. Each works its reverb out from other rays. With `phonon.wasm`: 0.5 dB at most, and 1.1 dB in the right ear from 400 to 1600 Hz |
| Gaps | none in the 5 ms levels, but the first 0.05 to 0.26 s of a level: the first frames of a page are long |

| What | Result |
|------|--------|
| The triangles of mp/ffa3 | the same checksum as on the desktop, so the file of the paths has the same name |
| The paths the page worked out | the size of the desktop's file, 387 of 662548 bytes are others: some pairs of probes see each other in one and not in the other |
| The desktop's file put into the page | read, 740 probes |
| The sizes of what crosses to Steam Audio | 64 structs and 26 enums of `phonon.h` have the same size, alignment and offsets with the flags of the engine and with those of the library |
| A level with more probes than the limit (the limit set to 500 for it) | the line in the console, no paths, no file, the rest runs |
| `snd_restart` in the level, a new level while a pass is in its slices | runs |
| `phonon.wasm` unloaded and loaded again: `snd_restart` six times at the menu and six times in the level, then three more levels | runs. The memory in use at the menu is the same after each of the six. What stays of a load are 623 entries of the function table, which the loader gives back for no module, and what the instance noted for the end of the program, which a page never reaches |
| The page without `phonon.wasm` | one line of the page, one of the engine ("no phonon.wasm next to the program"), the demo plays with the old mixer, `v_spatialVoice` is 0 |
| `v_spatial 0` | nothing of it runs |

So a server could hand the paths of its levels to a page: the name and the format are the same. Nothing does
that yet.

### Not run

- Firefox, Safari, a phone, a slower machine. The numbers are of one fast desktop processor: a slice is at
  least 32 rays and the end of a pass 2 ms there, both take longer on a slower one, and the limit is 8 ms.
- A fight, a game on a server (the demo has no server), any level but mp/ffa3.
- The voice chat through it. The engine's side is in the build and sets its two cvars, and the cgame for the
  browser has the voice chat since the same day. Nobody ran the two together.
- A browser without the SIMD of WebAssembly, which can not have `phonon.wasm`.
- A level between 740 and 2000 probes: how long its paths take is reckoned, not measured.
- A build with a version of Emscripten other than 6.0.12.

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
| `v_spatial 0` | the same levels to 0.1 dB as the engine from before this work |
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
| `v_voiceMuffle 0`, `v_spatial 0` | the voice comes from the own device, nothing from the engine |

The "disk" sound driver of SDL takes 0.6 % more sound a second than there is. A listener on it runs out of
voice every 3 s and the codec makes up a frame, which is a hole of 40 ms in a tone. The numbers above are from a
driver buffer of 882 samples, with which it runs 0.4 % slow (`tools/satest/clock.py`). A sound card that is that
far off would do the same on either way of the voice.

The thump, 2026-10-08: not from the game, from the samples of the base assets and the low pass on its own.

| Sample | Energy under 120 Hz | Highest sample after the low pass (the whole: 1.0) |
|--------|---------------------|-----------------------------------------------------|
| `weapons/rocket/hit_wall` | 57 % | 0.57 |
| `weapons/thermal/explode` (also the trip mine and the mines of the flechette) | 47 % | 0.70 |
| `weapons/detpack/explode` | 44 % | 0.81 |
| `weapons/concussion/explosion` | 61 % | 0.84 |
| `weapons/explosions/explosion_huge3` | 63 % | 0.91 |
| `weapons/repeater/alt_explode` | 11 % | 0.36 |
| `weapons/blaster/hit_wall`, for comparison, no thump | 5 % | 0.30 |

The low pass in single precision: -0.3 dB at 60 Hz, -3 dB at 120 Hz, -12 dB at 240 Hz.

Cost on mp/ffa3 with bots fighting, 2026-10-07 and 2026-10-08 (AMD Ryzen 9 5900X 12-Core Processor, two threads
for a pass): a pass of reflections and paths 21 ms, in its own thread, about seven a second. In the thread of
the game the sound costs mostly under 0.5 ms a frame and up to 2 ms, most of it the reverb. The paths of
mp/ffa3: 740 probes, under a second, a file of 660 kB (before the probes stood all over the level, see "Paths").

The same level on 2026-10-21, in silent hidden runs, with Embree and a pass spread over all cores but two (see
"Threads"): a pass took 28 ms without Embree and 4 ms with it, and the bake of the paths 3.1 s and 2.0 s. With
22 threads a pass of 8192 rays and 32 bounces took 8 ms.
That was measured with the old thread count. The new count (4 threads for a pass on that machine) was not measured,
so a pass there will take longer than the 8 ms.

The code in `client/spatial` (the same day) was run twice in a silent hidden client on mp/ffa3, the sound driver
"dummy": once with `v_spatialEcho 1`, the paths and the echo baked, `v_spatialDebug 1`, a `snd_restart` that read
both files back, and a change of level to mp/duel1 with its own bakes; once with `v_spatialEcho 0` and five bots,
with a sound with reflections of its own and up to 13 sounds in a frame. Both to their end, no crash. Nothing of
it was recorded or heard.

The bakes with Valve's numbers (2026-10-21, branch feature/spatial-bake), in a silent hidden client on mp/ffa3
(22 threads, Embree): the paths of 3960 probes 3 m apart took 4.2 s (1.6 s before) and the file is 21.7 MB (16.5
MB before). The echo took 21.3 s (3.0 to 3.2 s before) and the file is 28.7 MB as before: its size goes with the probes
and the length of the echo, not with the rays. A pass while the game runs took 1 ms with the stored echo, as
before, and 12 ms while the bake of the echo ran, since the bake shares the cores. No crash. Nothing of it was
recorded or heard.

The sounds of a lift (2026-10-21, branch feature/sound-system, Linux 64), in a silent hidden client on mp/ffa3
that rode the lift of the listener (put into its trigger). Before the sounds were moved out of the brush model,
the lift was 0 % in sight. After, its piston sounds 100 % in sight, heard from the top of the lift. Recorded
with `v_spatialCapture`: the loudest 50 ms went from -29.8 to -24.2 dBFS and the middle level from -37.7 to
-31.0 dBFS, nothing clipped. A person did not hear it.

Builds: Linux 64, and the client and the modules for 64 bit Windows with mingw (run on Windows 11 on 2026-10-11
with `v_spatial 1`: library loaded, level handed over, paths worked out and stored, see [windows.md](windows.md).
Nobody listened).

The library in the build, 2026-10-08: after a build it is next to the client, after an install too, on Linux 64
and with mingw for Windows 64. The fetching was tried with the zip from the disk (`file://`): the library came
out of it and was the right one, a zip with another checksum was refused, an address with nothing behind it
gave a warning and the configure went on. The address on GitHub answers with a file of the size of that zip, the
download itself was not run. The part for macOS and its application bundle is written and was never run.

## Compared with Valve's plugins

On 2026-10-21 this was read against Valve's plugins for Unity, Unreal and FMOD (`lib/steamaudio`, the core,
`phonon.h`, Valve's guide to the C API) and against the open source integrations for Godot and for Rust. No Quake
or idTech engine with Steam Audio was found. It is a reading of code.

These are done the way Valve does them:

- The direct simulation (what is in the way) runs in the thread of the game every frame. The reflections and the
  paths run in one thread of their own, with at most one pass at a time. In Unity that is `SteamAudioManager`
  (LateUpdate) and its simulation thread, in Unreal the Tick and a pool of one thread.
- A commit to the simulator or the scene is made only while no pass runs. Here that holds for the probes and the
  doors too, where Valve's Unity plugin adds probe batches and moves instanced meshes without waiting.
- The inputs of a kind of simulation are set in the thread that runs it (`phonon.h`, `iplSourceSetInputs`).
- The reverb of the listener is a source where he stands, with convolution, one reflection mixer and one decode.
- The HRTF is the library's, with no normalization and volume 1, as Valve's plugins default to.
- The bakes take the numbers of the bake of Valve's Unity plugin: the echo with 16384 rays and 1024 diffuse
  samples, the paths with 4 points of a probe 1 m from its middle, probes up to 1000 m apart asked and ways up
  to 1000 m long.

These are done otherwise on purpose:

- The fade by distance is the game's and not Steam Audio's. Valve's plugins work their own out too and do not
  use the simulator's.
- The walls and the ways round corners are smoothed from frame to frame here. Valve smooths only inside a block.
- A fixed set of 128 sources is made once. Valve makes a source per sound.
- A sound with reflections of its own gets the sound with the game's fade.
- The live pass follows 16 bounces where Valve has 4 (the default of their real-time reflections).

On 2026-10-21 the bakes took Valve's numbers. On mp/ffa3 the paths take 4.2 s (1.6 s before) and the echo 21.3 s
(3.0 to 3.2 s before), and every level is baked again.

One thing was found wrong here by this reading and fixed: the ways round corners were read while the next pass
wrote them (see "Threads").

## Limits

- The owner listened on 2026-10-17, before the changes of that day: good, but far too muffled behind walls and
  no sound off other walls. Those changes (probes over the whole level, rays all round, reflections of a sound
  of its own, walls as Valve has them, the doppler, `v_spatialPhysical`) were run without ears only: a hidden
  client with the sound driver "dummy" on mp/ffa3 with bots, a restart of the sound and a change of level, once
  with `v_spatialPhysical 0` and once with 1, to their end and without a crash. Nobody has heard them.
- The changes of 2026-10-21 (the code in `client/spatial`, doors and other brush models in the scene, the head
  as the place the sounds are heard from, the copy of the paths per voice, Embree and the cores, reflections that
  are kept, the precalculated echo, `v_spatialDebug`) were built on Linux and nobody has heard them. So were the
  bakes with Valve's numbers (later that day, on the branch feature/spatial-bake): the echo with 16384 rays and
  1024 diffuse samples, the paths with 4 points of a probe and ways up to 1000 m. Whether that sounds better
  is for ears to say. What was run of them, in silent hidden clients, is under "Checked".
- Until 2026-10-17 a pass on the desktop did not look all round the listener (the simulator was made for 8192
  rays and traced 2048, a quarter of the circle). Found in Valve's code (`Sampling::generateSphereSamples`) and
  measured then with their library, a listener between two walls: what came back came from one side (its
  direction went with the whole by 0.87, by -0.11 when made for 2048), and a pass of 1024 had twice the energy.
  The reverb was tuned with that. With the rays all round it may be quieter: `v_spatialReverb`.
- What the reflections of a sound of its own cost, measured in that run (Ryzen 9 5900X): a frame took 1.3 to
  1.7 ms of the thread of the game on average with 4 to 7 of them and up to 13 sounds, 5.4 ms at most, most of it
  the convolutions; before, with the room of the listener only, 0.4 ms. A pass 38 to 70 ms in its thread (21 to
  36 before). Each one more is about 0.15 ms a frame. `v_spatialSources` lower if frames are long.
- A sound that gets reflections of its own rings with the room of the listener until the next pass is done
  (50 to 100 ms). One that loses them lets them ring out.
- With `v_spatialPhysical 1` the reflections of an MP3 (the voices of characters) are late by the way here, the
  others are not. Sounds the server does not send (an entity it does not show the client) are not heard however
  far 1/r would carry them.
- The doppler is from the distance of frames, smoothed over a few: a sound that turns round quickly gets there
  a few frames late. It is not in the paths and the reflections of their own, which go with the sound as read.
- With `v_spatial 0` the doppler is the game's, which does nothing that can be heard as one (see "Doppler").
- The reverb of the own player's sounds and the thump of explosions were built and never run: not heard and
  not measured in the game. How much of an own sound the room gets is one number, `SPATIAL_OWN_SEND`.
- The thump is added on top. With `s_volume` near 1 an explosion next to the listener goes past what the
  sound card takes and is cut off there, sooner than without it. `v_spatialShock` below 1 if it distorts.
- A sound the cgame starts as a local sound on the channel of the body sounds rings too: in this cgame that is
  the zoom of the disruptor.
- The voice of the own character is an MP3 that is decoded while it plays, through its channel, and now by two
  readers: the mixer and the voice for the reverb. By the code they stay inside what the decoder keeps. If a
  line of the own character ever has a hole in its reverb, that is where to look.
- The reverb of the own voice is late. By the code, not measured: the microphone and its frames of 20 ms, a
  block of 1024 samples that has to be full, then `v_spatialAhead` (0.1 s), then the sound card. About 0.2 s
  from the word to the first of its reverb, which in a small room is heard as an echo more than as a room.
  Built and never run, like the two above. With speakers the microphone hears that reverb again.
- A moving door was never seen in a run. A lift was ridden in one (2026-10-21, see "Checked"): its sounds were
  measured, the motion of its walls in the scene was not looked at separately. The transform of the meshes was
  checked with a small standalone program against Steam Audio's library. The ways round corners are worked out
  from the world without the brush models, so a way through a shut door can still be found.
- A sound inside the bounds of a door with a gap (two wings) is moved too, out to the edge of the bounds, since
  the bounds do not know the gap.
- Only the loudest `v_spatialSources` sounds have reflections of their own, the others ring like the room of the
  listener.
- What a voice is given reaches the ear after `v_spatialAhead` (0.1 s): a turn of the head is heard that much
  later. If the game stands still for longer than that, the sounds of the level have a gap while the others
  play on.
- A sound that starts gets its reverb up to 0.1 s late.
- A voice of the voice chat behind a wall without a way round it was 13 dB quieter than the voice chat makes it
  on its own (see "Checked"), with walls that let through the square root of their number. Since 2026-10-17 a
  wall lets through its number, so it is quieter than that, and the way round the corner is found all over a
  level: the wall is the wall of every other sound. Whether that is too quiet to talk
  through is for ears to say. `v_voiceMuffle 0` takes the voices out of it.
- The voice chat looks at doors when it muffles a voice itself. Until 2026-10-21 a door was not in the way here;
  now it is, as for every sound.
- A voice is 0.1 s later than on the voice chat's own device. That number is from the code, not measured.
- Not tried: more than one speaker at a time, a speaker who moves, a change of level while somebody talks.
- Not run: macOS. Not built: macOS. On Windows it started and worked out the paths of a level (2026-10-11,
  [windows.md](windows.md)), nobody has heard it there, and it is off by default there.
- 22 kHz (`s_khz 22`) is refused.
