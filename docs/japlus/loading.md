# Loading a level, and long frames

What the load of a level takes, why the game stands still for a moment when something is loaded in the middle
of it, how that is measured, and what was done about it. Everything here is about the client of this tree.

## Two reports of the engine

Both are off by default (`qcommon/com_load.cpp`).

- `v_loadReport 1`: after the load of a level the console has a table: the kinds of things that were
  loaded, how long each kind took together, and how many there were. It runs from the start of the server of
  the level (if this program is that server) to the end of the first frame of the game.
- `v_hitchLog <ms>`: a frame that took longer than that says what was loaded in it, the longest first, and
  which part of the frame the rest went into (`frame: server`, `game`, `client`, `cgame`, `sound`, `drawn`).
  With `r_swapInterval 1` a frame waits for the screen, so a threshold below the time of a picture reports
  every frame.

Code that loads something says so around it: `Com_LoadPush( kind, name )` before and `Com_LoadPop()` after
(the renderer: `ri.LoadPush`, `ri.LoadPop`). They nest, a model loads its images: the time of what is inside
is taken off what is around it, so the kinds add up to the whole and "not named" is what no marker is around.
Only the main thread may call them.

## The test

`japlus_port/tools/loadtest.sh` starts a client on a level it serves itself, with both reports on, runs a
script of `tools/clienttests` and writes a log into the profile `runtime_load`. It is run through
`tools/hidden.sh`: the window is on the hidden workspace and the sound goes to a driver that plays nothing,
which the script insists on (the sound system has to run, its loading is part of what is measured).
The log has the load tables and the long frames as the engine prints them; `loadstats.py`, which printed them
in a few lines, is not in the tree any more.

- `load_twice.cfg`: a level loaded, played five seconds, loaded again.
- `load_play.cfg`: bots with different models join a second apart (a listen server has room for seven), then
  every weapon is fired once.
- `load_shot.cfg`: two pictures from fixed spots, to see that a change did not alter what is drawn.

`LOAD_FRESH=1` deletes what the renderer keeps in the profile, for the numbers of a first start.

## What was measured

mp/ffa3, `rd-vulkan-vision`, a listen server, on the machine this was written on. A frame of this test takes
1 to 3 ms when nothing is loaded.

The first load of the level since the program started, before any change, 1507 ms:

| kind | ms | how many |
|---|---|---|
| textures packed into blocks | 397 | 4122 levels |
| image files read and decoded | 389 | 643 |
| sounds | 123 | 1123 |
| the start of the ui | 112 | 1 |
| the restart of the file system | 79 | 1 |
| the world | 77 | 1 |
| effects | 47 | 142 |
| the rest | 283 | |

The second load of the same level: 383 ms. Images, models and sounds are still there, what is left is what
every load does again: the ui (116), the file system (79), the game (32).

A player who joins: one frame of 21 to 27 ms. About 12 of it are the image files of his skin, 7 packing them,
3 to 4 the skin itself, 1.3 the wait for the upload, 2 to 4 his sounds. Seven bots, seven such frames.
Taking out and firing each weapon gave no long frame.

## What was done

**The packed textures are kept** (`rd-vulkan-vision/vk_texcache.cpp`, cvar `v_vkTexCache`, on by default).
What is made from an image file, its mip levels packed into BC1 or BC3 blocks, only depends on the file and
on a few settings. It is written to `vulkan/tex/` of the profile, a file per texture, and the next time the
blocks are read and handed to the device: the image file is neither read nor decoded nor packed.

A file of the cache is named after what it was made from: the path of the image file, its length, the
checksum of the pak it is in and the settings (`r_picmip`, `r_simpleMipMaps`, `r_texturebits`,
`r_intensity`, the overbright bits, the largest texture of the device). Another pak or another setting is
another name: nothing is compared and nothing goes stale, an old file is merely left behind. An image file of
a directory, as a mapper has them while he works, is never kept, because it can change under the same name.
The checksum is the one of the pak file itself (`FS_FileSource()` of the engine), not the one a pure server
compares, which is another number with every level. The files end in `.dat`, one of the few kinds the engine
lets be read from a directory on a pure server.

| | before | kept textures |
|---|---|---|
| first load of mp/ffa3 since the program started | 1541 ms | 661 ms |
| the same with an empty cache, which it fills | | 1541 ms, 27 of them writing |
| a bot joins | a frame of 22 to 28 ms | a frame of 4 to 6.5 ms |

Two other levels, each loaded for the first time since the program started, with an empty cache and with
a filled one: mp/siege_hoth 1624 and 756 ms, t2_trip 1629 and 687 ms. A second load of either is 440 ms.

The cache of mp/ffa3 and of seven player models is 905 files, 62 MB. Every texture of the game together
would be several hundred. Deleting the folder is safe at any time.

Two faults on the way, both found by the test and not by reading. The first key had the checksum a pure
server compares, so no texture was ever found again and every load wrote its files anew. And a kept texture
skipped the end of `Upload32()`, where the filters are set: it was drawn without its mip levels, which a
picture from one spot did not show and a picture from another did (10 % of its pixels were off). Now
`Upload32()` itself takes the kept texture and goes on to its filters. With that, pictures with and without
the cache differ by 0.07 to 0.23 % of the pixels, as much as two runs of one setting do.

**Uploads are not waited for** (`rd-vulkan-vision/vk_texture.cpp`, 2026-10-19). Every batch of copies to the
device waited for the whole queue (`vkQueueWaitIdle`), the frames that were still drawn included. Now there are
two upload slots, each with its command buffer, staging buffer and fence: a batch is submitted and the CPU goes
on, and it only waits when it wants to fill a slot whose last batch the device has not read yet.
`VK_FinishUploads()` still waits, where a caller needs the copies done (a staging buffer of its own is freed, a
frame is read back, the renderer ends). Two barriers in every batch keep the order the wait gave: its copies run
after everything submitted before it, and what they wrote is seen by everything after it.

| mp/ffa3, four runs of each, alternating | waiting for uploads | load |
|---|---|---|
| first load (filled texture cache), before / after | 16 to 24 ms / 3 to 5 ms | median 816 / 802 ms |
| second load, before / after | | median 497 / 500 ms |
| twelve bots join, frames over 3 ms, before / after | 6 to 11 ms in 13 to 14 frames / none in 10 to 11 frames | |

The load as a whole is not shorter: the loading screen is drawn between the steps of a load, and what the CPU
no longer waits for at an upload it waits for at the fence of the next frame. What it gains is in the game: a
frame that uploads something (a player joins with a skin, a video plays) no longer waits for the frames before
it. Under the validation layer with its synchronization checks (`VK_LAYER_VALIDATE_SYNC=1`) the two loads and
three bots gave no message; a build without the two barriers gave read after write hazards on the index buffers
of the models at once, which also showed that the checks ran. Pictures from two fixed spots differ from the
build before as much as two runs of that build differ from each other.

**The list of a pak is read through a buffer** (`qcommon/files.cpp`, `FS_UnzOpen()`, 2026-10-19). Every level
restarts the file system (the server draws a new checksum feed), which reads the central directory of every
pak again: 35000 entries in the paks of the game. minizip reads them a few bytes at a time and seeks before every
entry, which empties the buffer of stdio, so every entry was a read of the disk: 95 % of the restart under
callgrind, three quarters of it in `fread`. The list is now read through 64 KB of the pak in memory; the files
of the pak are read through a handle without it, as before (a first try that read them through the buffer too
made the start of the ui half as long again, every small file cost a fill of 64 KB).

| mp/ffa3, three runs of each, alternating | the file system | load |
|---|---|---|
| first load, before / after | 132 to 141 ms / 46 to 48 ms | 786 to 815 / 685 to 693 ms |
| second load, before / after | | 478 to 496 / 395 to 407 ms |

The directory was also walked twice, the first time only to add up the lengths of the names; minizip reads every
header twice in a walk. Now it is walked once into temporary arrays: the restart 47 to 51 ms to 27 to 28 ms, the
second load (with the ui of below) 301 to 322 / 288 to 292 ms. The server named the same `sv_paks` with the
build before and after, and `path` listed the same paks with the same number of files.

A dedicated server saves the same at every change of the level, a client of another server at every gamestate
(`FS_ConditionalRestart`). From 2026-10-27 most of these restarts do not happen, see below. The pure checksums are made by the same code from the same entries: a client of this
build joined a server of it with `sv_pure 1`, and the download test (`uitest/dltest.sh`) blocked the same files.

**The ui makes its list of player models when a menu asks for it** (`ui/ui_main.c`, `UI_Q3HeadsNeeded()`,
2026-10-19). The ui starts again with every level, and 87 of the 140 ms of that start were the list of the
player models and skins for the menus (`UI_BuildQ3Model_List()`): a list of the files of every model folder,
each a walk over all files of all paks, and up to three icons opened per skin. The two places that read it make
it the first time (and `ui_cache 1`), so a player who opens the menu with the models pays that once per level.

| mp/ffa3, two runs of each | the start of the ui | second load |
|---|---|---|
| before / after | 138 to 172 ms / 53 to 58 ms | 379 / 302 to 326 ms |

The list has the same 165 models, and the player menu of the game looks the same in a picture.

**The file system is kept on a new level** (`qcommon/files.cpp`, `FS_NewChecksumFeed()`, `FS_StartupKey()`,
2026-10-27; called from `FS_ConditionalRestart()` and from `SV_SpawnServer()` in `server/sv_init.cpp`). Every
level of a server has a new random checksum feed, which made the client, and the server on every level change,
restart the whole file system. The feed only goes into the pure checksum of each pak. Now every pak keeps the
CRCs of its header (`pack_t` `headerLongs`), and when nothing else changed since the last `FS_Startup()` only
the pure checksums are made anew, the references are cleared, the reference lists (`ref_forcelist.txt` and the
others) are read again and the pure order is redone. "Nothing else" is a key of `fs_game`, `fs_basegame`,
`fs_forcegame`, the paks the server references and the paks it lists as pure (`FS_StartupKey()`). Anything
else is the full restart as before. A pak put into a folder while the game runs comes in with the next full
restart (`fs_restart`, a connect to another server; `vid_restart` is not needed).

The full restart took 21 to 41 ms on the client at a level change, the kept one 0 ms. It happens before the load
report starts (in the gamestate), so the load table never showed it. Checked: `tools/test/join-test.sh --pure`
(`sv_pure 1`, a level change, no unpure client), the compat test with stock OpenJK, and a server with a
`ref_forcelist.txt` whose forced paks were still asked for after a level change.

**The loose folders are listed once** (`qcommon/files.cpp`, `FS_DirMayHave()`, cvar `v_fsIndex`, on by default and
archived, 2026-10-27). Every file the game looks for was first tried in the loose folders of the search path (home
and game folder, japlus and base) before the paks, and each miss was a call of the system. Now every loose folder
of the search path is listed once (`Sys_ListFiles`), a sub folder the first time a file below it is looked for, and
a file that is not in the listing of its folder, or is below a folder that is not there, is not tried. The files
the game writes go into the listings at once (every write passes `FS_CreatePath`, and the two renames do). Anything
else that changes the folders (another program, the web panel of the server writing a file with plain `fopen`) is
seen after the next level change, when all listings are made anew, or after the next restart of the file system. A
.cfg is always tried, so a config put into the folder by hand can be exec'd at once. A folder with more files than
`Sys_ListFiles` lists (4095) cannot say what is missing, below it everything is tried. `v_fsIndex 0` tries every
file in every folder as before.

Measured with `tools/test/join-test.sh --strace` (3 joins and a level change, filled caches), the failed opens of
the whole run of the client:

| | before | after |
|---|---|---|
| all failed opens | 39815 | 3107 |
| sound/*.wav and sound/*.mp3 in the loose folders | 2328 and 2404 | 0 |

On Linux a failed open costs about 1 microsecond (measured), so that is about 40 ms a run there. What is left is
mostly the folder scans of `FS_GetFileList`. Checked: the kept textures (722) and sounds (272) of the caches are
all found again on the next start, a demo recorded in a level plays in the same run, a demo copied into `demos/`
before the start plays, a missing one still says it can not be opened, the compat test and `join-test.sh --pure`.

## Measured again, 2026-10-27

With `tools/test/join-test.sh` (Radeon 610M, 30 bots, mp/ffa1, loopback, no lag proxy):

| | ms |
|---|---|
| rejoin in the same session (caches in memory), join | 614 to 670 |
| the same, load report | 280 to 334 |
| first join of a game started again, filled caches | 852 to 1365 |
| level change, load | 218 to 229 |

Of every load: the ui starts in 48 ms, the cgame in 36 to 39 ms, effects take 14 ms. The frame line of the
client is about 70 ms: those are frames while the client waits for the server, not load work. The first join
of a started again game varies with the disk: "sound file" took 37 to 76 ms for 598 files. A level change had
the file system restart before its load, which is gone.

## What the numbers did not confirm

Read from the code, these looked like causes, and they are not, on this machine and these levels:

- Pipelines made in the middle of a frame: a level makes seven, 0.4 ms together, and none showed up in a long
  frame. No pipeline cache was added.
- The buffers of the world and of models, built at the first draw: 1.2 ms for the world of mp/ffa3, the
  models of a player too little to measure.
- Weapons that are loaded when first used: no long frame.

## Joining a server

`v_joinReport 1` prints one line when the game starts after a `connect`: how long each step of the join took
(challenge, connect, the gamestate with its packets and bytes, downloads, the load, the first snapshot, the
total). `v_joinReport 2` also prints every packet of the join, by the time since connectResponse.
`tools/test/join-test.sh <game folder>` (see [testing.md](testing.md)) starts a dedicated server of the folder
on 127.0.0.1 with bots and a hidden, silent client with both reports on, which joins three times, and then the
server changes the level once. It prints the `v_joinReport` and `v_loadReport` lines. It replaces
`japlus_port/tools/jointest.sh`, `tools/joinlocal.sh` and `loadstats.py`, which are not in the tree any more.
The numbers of 2026-10-19 above were made with them (the server of the build behind `lagproxy.py` with 13 ms);
the numbers of 2026-10-27 below with `join-test.sh`, which has no lag proxy.

A public JA+ server on JAmp 1.01 (29 players, mp/ffa1, 13 ms away), filled texture cache, 2026-10-19:

| step | ms |
|---|---|
| challenge and connect | 45 to 50 |
| gamestate, 16 to 20 packets, 20 KB | 1455 to 1552 |
| the load | 540 to 670 (sounds of the players 205 of it) |
| the first snapshot | 96 to 139 |
| the whole join | 2190 to 2350 |

With an empty texture cache the load was 2487 ms. The trace showed where the gamestate goes: three packets at
once, then a second of nothing, then one packet per frame of the server. The second is a rule of the server for
clients that connect ("don't pile up empty snapshots while connecting", `SV_SendMessageToClient()`): after every
message the next send is put a second away, and the rest of a gamestate that needs several packets waited for
it. A client cannot change that on a server of the old engine; `rate` 25000 or 100000 gave the same times there
(after the pause a packet goes every frame anyway), so the default of `rate` stays.

The server of this tree now sends the rest of a message that did not fit into one packet at the rate, as ioq3
does, and keeps the second for what comes after it. Against a server of the build, 31 bots, sv_lanForceRate 0:

| | gamestate | join |
|---|---|---|
| before | 1300 to 1349 ms (9 to 11 packets, 10 KB) | 1982 to 2107 ms |
| after | 350 to 399 ms | 1006 to 1017 ms |

What is left of the gamestate is the rate of the server, `sv_clientRate` 50000 with `sv_ratePolicy` 1 (the
`rate` of the client does not count there either).

The connect itself: a lost getchallenge or connect was sent again after 3 s, now after 1 s, and a server that
does not answer a step for 30 s ends the connect with an error that names its last word (a server that answers
"Reconnect rejected : too soon" may stay silent after it, and the connect screen waited for ever).

The loading screen is drawn after every item and every player of the level, 89 times on that server: 16 ms
together, nothing to gain.

**The unpacked sounds are kept** (`client/snd_cache.cpp`, cvar `v_soundCache`, on by default). An MP3 outside
`sound/chars` is unpacked to samples when it is registered: weapons, effects, the sounds of the players. On the
server above that was 182 of the 256 ms of sounds, 288 files. The samples are written to `soundcache/` of the
profile, a file per sound, named as the textures of `vk_texcache.cpp` are (the path of the MP3, its length, the
checksum of its pak; never a file of a directory), before they are brought to the rate of the mixer.

| | unpacking | load |
|---|---|---|
| 31 bots on mp/ffa1, three joins each, before / after | 153 to 162 / 15 ms | 438 to 474 / 305 to 318 ms |
| the server above, filled cache | 182 / 19 ms | 540 to 670 / 396 to 426 ms |

What is left of the unpacking is the resampling to the rate of the mixer. The first load that fills the cache
writes 16 ms more. The cache of mp/ffa1 with its players is 24 MB in 280 files; deleting it is safe at any time.
The 280 kept sounds were compared once with a fresh unpack of their MP3: all the same.

## Not done yet

- What a join still costs: the Ghoul2 model and the sounds of the player (the wait for the upload is gone).
- The first frame of the game after a load takes about 16 ms in the cgame, in code no marker is around.
- The first load with an empty cache is as slow as before. Decoding and packing on the worker threads the
  packer already has would shorten it.
- Every folder scan of `FS_GetFileList` (models/players, strings, ext_data) still tries the loose folders
  and does not go through the listings. On Windows the cost of a failed open was not measured (no Windows here).
- The resampling of kept sounds: at most 15 ms, on the first join of a session only. The copy and the
  allocation would stay. Not done.
- The ui is started anew on every load (48 ms). The screen draws nothing at all while the ui is not started
  (`SCR_DrawScreenField()`), so starting it after the load needs that changed, and the 48 ms then come when the
  menu is first opened in the level. Open.
- The sounds of the players are read only on the first join of a session, on a rejoin they are in memory.
- `rd-vanilla` and the renderer for the browser have no cache: OpenGL packs in the driver, and the device for
  the browser takes no textures in blocks.
- Measured on one level and one machine. Not played by a person with it.
