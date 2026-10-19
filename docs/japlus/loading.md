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
`tools/loadstats.py <log>` prints the loads and the long frames of a log in a few lines.

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

A dedicated server saves the same at every change of the level, a client of another server at every gamestate
(`FS_ConditionalRestart`). The pure checksums are made by the same code from the same entries: a client of this
build joined a server of it with `sv_pure 1`, and the download test (`uitest/dltest.sh`) blocked the same files.

## What the numbers did not confirm

Read from the code, these looked like causes, and they are not, on this machine and these levels:

- Pipelines made in the middle of a frame: a level makes seven, 0.4 ms together, and none showed up in a long
  frame. No pipeline cache was added.
- The buffers of the world and of models, built at the first draw: 1.2 ms for the world of mp/ffa3, the
  models of a player too little to measure.
- Weapons that are loaded when first used: no long frame.

## Not done yet

- What a join still costs: the Ghoul2 model and the sounds of the player (the wait for the upload is gone).
- The first frame of the game after a load takes about 16 ms in the cgame, in code no marker is around.
- What every load does again: the ui is started anew (112 ms), the file system is restarted (46 ms since the
  buffered list, it was 79 when this was first measured and 130 on 2026-10-19).
- The first load with an empty cache is as slow as before. Decoding and packing on the worker threads the
  packer already has would shorten it.
- Sounds are read and decoded when they are registered (123 ms of a first load).
- `rd-vanilla` and the renderer for the browser have no cache: OpenGL packs in the driver, and the device for
  the browser takes no textures in blocks.
- Measured on one level and one machine. Not played by a person with it.
