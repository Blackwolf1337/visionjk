# Loading a level, and long frames

What the load of a level takes, why the game stands still for a moment when something is loaded in the middle
of it, how that is measured, and what was done about it. Everything here is about the client of this tree.

## Two reports of the engine

Both are off by default (`qcommon/com_load.cpp`).

- `com_loadReport 1`: after the load of a level the console has a table: the kinds of things that were
  loaded, how long each kind took together, and how many there were. It runs from the start of the server of
  the level (if this program is that server) to the end of the first frame of the game.
- `com_hitchLog <ms>`: a frame that took longer than that says what was loaded in it, the longest first, and
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

**The packed textures are kept** (`rd-vulkan-vision/vk_texcache.cpp`, cvar `r_vkTexCache`, on by default).
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

## What the numbers did not confirm

Read from the code, these looked like causes, and they are not, on this machine and these levels:

- Pipelines made in the middle of a frame: a level makes seven, 0.4 ms together, and none showed up in a long
  frame. No pipeline cache was added.
- The buffers of the world and of models, built at the first draw: 1.2 ms for the world of mp/ffa3, the
  models of a player too little to measure.
- Weapons that are loaded when first used: no long frame.

## Not done yet

- What a join still costs: about 1.3 ms a frame waiting for an upload (`VK_FlushUploads` waits for the
  device), the Ghoul2 model and the sounds of the player.
- The first frame of the game after a load takes about 16 ms in the cgame, in code no marker is around.
- What every load does again: the ui is started anew (112 ms), the file system is restarted (79 ms).
- The first load with an empty cache is as slow as before. Decoding and packing on the worker threads the
  packer already has would shorten it.
- Sounds are read and decoded when they are registered (123 ms of a first load).
- `rd-vanilla` and the renderer for the browser have no cache: OpenGL packs in the driver, and the device for
  the browser takes no textures in blocks.
- Measured on one level and one machine. Not played by a person with it.
