# Downloads from a server

Not a JA+ feature. `codemp/client/cl_download.cpp` (the windows), `codemp/qcommon/files.cpp` (the folders and
the blocked files), 2026-10-17.

## For players

When a server offers paks you do not have and you allow downloads (`cl_allowDownload`), a window comes before
anything is downloaded (`v_downloadReview 1`, the default):

- every pak the server offers, ticked: untick what you do not want;
- **Keep them (downloads folder)**: the paks go into `<game>/downloads` of your home folder and stay there;
- **This session only**: they go into `<game>/downloads/session`, which is emptied when the game starts and
  when it ends (`v_downloadKeep 0` makes that the default);
- **Download**, **Join without** (nothing is downloaded), **Disconnect**. A pure server may not let you play
  without its files.

A pak is sent whole, so what is in it is known only when it is here. When the paks came, a second window shows
what is in each, by kind: maps, models, textures and effects, sounds and music, shaders and scripts, menus and
HUD, configs (they can run commands), other files, and programs, with the program files by name. Untick what you
do not want: nothing of it is used. Programs are unticked from the start: a library or an executable in a pak
could run on your computer (on Windows the engine unpacks a game module it finds in a pak and loads it). **Use
them** goes on into the game, **Disconnect** leaves.

Programs are blocked in every pak that arrives, with the window or without it (`v_downloadReview 0`, or a
download the window did not list). Both switches are in Setup, Network.

The paks of both folders are loaded only while a server references them, as the `dl_` paks of the engine were:
what one server sends does not change the menus or the models on another server, nor when you play alone. Paks
that were downloaded into the game folder itself before 0.1.9 stay where they are and load as before.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `v_downloadReview` | 1 | the two windows. 0: everything is downloaded, programs in it stay blocked |
| `v_downloadKeep` | 1 | where the paks go by default: `downloads` (1) or `downloads/session` (0) |
| `v_downloadReview_do` | | what a click in the windows does, for a key or a script: `download`, `without`, `keep 0/1`, `pak <n>`, `kind <pak> <kind>`, `use` |

## How it works

- `FS_ComparePaks()` names the place of a download as `<game>/downloads/<name>.pk3` (with the checksum in the
  name when one of that name is there already). `CL_DownloadLocalName()` moves it into `downloads/session` for a
  pak of this session.
- `CL_InitDownloads()` opens the first window (`CL_DownloadReviewBegin()`) in place of starting the first
  download; the question per file of `cl_downloadPrompt` is not asked while a window of this file is open.
- When a pak came and has the checksum the server named, `CL_DownloadArrived()` reads its central directory
  (minizip), counts its files by kind and writes `<pak>.blocked` next to it: one path a line, the programs from
  the start. After the last download `CL_DownloadReviewEnd()` opens the second window, and **Use them** writes the
  list again with what was unticked and lets `CL_DownloadsComplete()` restart the file system.
- `FS_LoadZipFile()` reads `<pak>.blocked`: those files still count for the checksum (a pure server sees the
  pak it sent), but they are in no hash table and in no list, so nothing can open or list them, `Sys_UnpackDLL()`
  included.
- `FS_AddGameDirectory()` adds the paks of `downloads` and `downloads/session` of the home folder after those of
  the game folder, with the rule of the `dl_` paks. The session folder is emptied once a game folder a run, before
  its first paks are loaded, and at quit (`FS_Shutdown( qtrue )`), only `.pk3`, `.tmp` and `.blocked` files.
- Program files: `.dll .so .dylib .exe .bat .cmd .com .scr .msi .ps1 .sh .vbs .jar .app .bin .pif .qvm` and
  anything with `.so.` in its name.
- The windows are drawn by the engine (`CL_DrawEngineMenus()`), in the colours of the new look and with its
  letters (`vision_sans`) where the pack has them; their texts are in `strings/*/vision.str` under the keys
  `tools/menus/texts.py` makes.

## Checked, and what not

2026-10-17, hidden and silent on Linux (`uitest/dltest.sh` of the test tools: a server of this tree with a test
pak holding a map file, a texture, a config and a fake `cgamex86_64.dll`, a fresh client profile): both windows
seen in German, kept and session mode, the fake program blocked and not found by `which`, an unticked kind
blocked, the map file and the config loaded from `downloads/` and `downloads/session/`, the session folder empty
after quit. Not run: Windows (where `Sys_UnpackDLL()` would have been the danger), a pure server, a pak with
thousands of files in the second window (it scrolls with the wheel, never seen), a real mouse.
