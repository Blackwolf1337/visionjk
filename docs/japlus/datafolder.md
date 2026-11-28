# The folder of VisionJK in the home folder

Not a JA+ feature. `codemp/qcommon/files.cpp` (the block "THE FOLDER OF VISIONJK"), `codemp/qcommon/qcommon.h`
(`FS_DATAFOLDER`, `FS_DATAGAME`, `FS_DOWNLOADS`), 2026-11-28.

What VisionJK makes for the player that belongs to no mod lives in one folder, `visionjk-data`, in the home folder
(`~/.local/share/visionjk` on Linux, the cvar `fs_homepath`). The read only cvar `fs_datapath` has the whole path of it.
Before 2026-11-28 these files were spread over `base/`, the mod folder of the game and the home folder itself.

## What is where

The folder has two parts. Only `files/` is in the search path of the file system. The rest is not.

| Path in `visionjk-data` | Content |
|-------------------------|---------|
| `admin_ed25519`, `admin_ed25519.pub`, `admin_hosts.txt` | the key of the admin login and the host keys of servers (see [security.md](security.md)) |
| `ids_session` | the address of the server a client is on, for the crash check (see [security.md](security.md)) |
| `cvarseen.txt` | the day each cvar was last used (see [client.md](client.md), "Cleaning up cvars") |
| `cache/` | what the client makes again when it is gone: textures, materials, sounds, pipelines, sound paths of levels (`v_cacheMaxMB`, `v_cacheList`, `v_cacheClear`). The keys are in [loading.md](loading.md) |
| `downloads/<game>/` | paks downloaded from a server and kept (see [downloads.md](downloads.md)) |
| `downloads/<game>/session/` | paks of this session only, emptied when the game starts and ends |
| `files/` | the head of the search path, below |

What is in `files/`:

| Path in `files/` | Content |
|------------------|---------|
| `modelstore/` | the player models of the store, one pak a model, and `index.txt` and `icons/` (see [models.md](models.md)) |
| `gfx/emoji/`, `emojistore.txt` | the chat emojis of the store and the list of those that came from it |
| `sound/emotes/`, `emotesounds.txt`, `emotes/index.txt` | the interactive emojis of the store: their own sounds, the list of sounds, the list of emojis |
| `sprays/store/` | the sheets of the sprays of other players |
| `drawings/` | the drawing board: presets, pictures of other boards, pictures to import, the clipboard picture (see [toys.md](toys.md)) |
| `chatlogs/` | the chat logs of `tay_chatLog` |
| `shaders/zz_mat_*.oshader`, `materials/status.txt` | what the Material Lab writes |
| `vision/fxlights.txt`, `debugmenus.ini` | the notes of the debug menus |
| `remaps_off.txt` | the texture swaps switched off in `v_remaps_window` |
| `visionjk/changelog-fetched.md` | the changelog the updater fetched (see [update.md](update.md)) |
| `steamaudio/capture.wav`, `steamaudio/scene.obj` | what `v_spatialCapture` and `v_spatialSaveScene` write (see [audio.md](audio.md)) |

The paks in `modelstore/` count as paks of `base`, which is what a server is told about them. The paks in `downloads/`
count as paks of their game folder and are loaded only while a server references them.

## Why only files/ is searched

The key of an admin login must not be readable through the file system of the engine, which is how a module, a console
command or a script reads files: everything in the search path can be read that way. So the key, the session and the
caches are in the part that is in no search path. `fs_datapath` is read only, and the cgame opens the key from that path
directly. This is no protection against a program on the computer: the modules are native code and run with the rights
of the player.

The same reason keeps a server out of it: the name `visionjk-data` is refused as the `fs_game` a server sends
(`CL_BadGameDir()`) and as a game folder from any other source (`FS_AddGameDirectory()`), so it can not become a search
path.

What the game writes is the other half. `files/` is put at the head of the search path, before all other folders, and
files of certain names go there when a module or the engine writes them (`FS_DataFolderOf()`: `debugmenus.ini`,
`vision/fxlights.txt`, `materials/status.txt`, `shaders/zz_mat_`, `drawings/`, `chatlogs/`, `remaps_off.txt`,
`steamaudio/capture`). So what the game writes there is what it reads next. A cgame that is asked for
`drawings/preset1.vjd` finds it in `files/` whatever the mod folder is.

## The move

Once at the start of the program, before the search path is made (`FS_MoveToDataFolder()`), the old places are moved
into the new ones. It is a rename, never a copy, and never over a file that is there: that file stays where it was and
is counted. The console says `VisionJK moved its files into visionjk-data: N, M stayed where they were` when anything
was moved or kept. A folder that was moved is gone from the old place when it is empty (`rmdir`).

What is taken from where:

- from the home folder: `cache`, `cvarseen.txt`, `ids_session`, `admin_ed25519`, `admin_ed25519.pub`, `admin_hosts.txt`;
- from `base`: `modelstore`, `gfx/emoji`, `sound/emotes`, `emotesounds.txt`, `emojistore.txt`, `emotes`, `sprays/store`,
  `visionjk/changelog-fetched.md`;
- from the mod folder of the game (`japlus` first, so where two have the same file its one wins) and from `base`:
  `debugmenus.ini`, `vision/fxlights.txt`, `materials/status.txt`, `remaps_off.txt`, `drawings`, `chatlogs`,
  `steamaudio/capture.wav`, the files `shaders/zz_mat_*.oshader`, and `downloads`, which becomes `downloads/<game>`;
- the same from the folders of other mods, only when the home folder is not the install folder. Where it is the install
  folder, other clients write into those folders too, and a `drawings/` or `chatlogs/` there may be theirs.

The permissions of the key stay as they were (it is renamed, not rewritten). An older client on the same profile does
not find the moved files: it makes its own in the old places. The next start of a new client moves them again where it
can, and overwrites nothing. `steamaudio/scene.obj` is written to the new place only, there was nothing to move.

## What stays where it was

The configs of each mod, screenshots, videos and demos (other programs and players know where to find them), the files
of a server (`games.log`, `security.log`, `ids.log`, the anti cheat files, the web panel, bans), the navigation files of
bots, `servercache.dat`, and the own `sprays/` folder of the mod, where players put their own pictures for `v_sprayImage`.
`japlus/urlRedirect_cache.txt` and `commandmenu.txt` of the japlus folder are not moved either.

The WASM page (see [web.md](web.md), switched off) still writes the pak of a model to `/home/base/modelstore`. It was not
changed for the folder of VisionJK.

## Other code that names the folder

`CL_DownloadLocalName()` (`cl_download.cpp`) puts a download of this session into the `session` folder below the game
folder. `Cache_Init()` (`cl_cache.cpp`) uses `visionjk-data/cache`. `cl_models.cpp` writes the stores with
`FS_DATAGAME`, and `cl_update.cpp` the changelog. `cl_memwatch.cpp` writes `ids_session` and `cvar_cleanup.cpp`
`cvarseen.txt` into `visionjk-data`. `cg_auth.c` (`Auth_Folder()`) and `cg_board.c` take `fs_datapath` for the key and the
folder of the board pictures, and the home folder and the mod folder with an engine that has no such cvar. The script
`tools/materials/export.sh` reads `visionjk-data/files` by default.

## Checked, and what not

2026-11-28, hidden and silent on Linux. The move on a made up profile with a placeholder key: the permissions 0600 kept,
the key and the cache not found through the file system, a second start after an old client had made the old places
again overwrote nothing, and a chat log was written to the new place. Real downloads from a server on 127.0.0.1 with
the paks forced by `ref_forcelist.txt`, the two windows clicked with real pointer input: keep, on a server that is not
pure (the pak byte for byte the same, the map file and the config loaded, `cgamex86_64.dll` blocked), this session only,
on a pure server (joined, not dropped in about 10 seconds, `session/` empty after quit), and an old kept pak found after
the move and not asked for again.

Not run: Windows, macOS, a download from a web address, a server that sends hostile pak names, a real store download.
