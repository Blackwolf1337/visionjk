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

While a pak comes from the web address of the server (see below), the overlay of the download (`cl_downloadOverlay`,
the "File Download" panel) has two buttons above Abort: **Skip** (this pak is not downloaded, the next one
comes) and **Skip all** (this one and all after it). They are the Abort and Abort All of the menu
`ui/jamp/UrlRedirectDownload.menu` of the JA+ plugin, and the script of that menu (`uiscript
abordURLRedirectCurrDownload 0` or `1`) runs them too. Only a pak from the web address has them: of a pak from
the server itself, what is still on the way would be taken for the next one.

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
| `v_downloadReview_do` | | what a click in the windows does, for a key or a script: `download`, `without`, `keep 0/1`, `pak <n>`, `kind <pak> <kind>`, `use`, `skip`, `skipall` |
| `v_downloadHttp` | 1 | paks of a server that names a web address for them come from there. 0: only from the server itself. Bit 14 of `cp_pluginDisable` (AllowDownloadURL_Redirect) does the same |

## From a web address

A server can name a web address its paks lie on, with the cvar of JA+ that the JA+ client plugin reads:

```
seta jp_DlBaseURL "https://example.org/jka/"
```

The quotes are needed: without them the console takes everything after `//` as a comment. The game module turns
`sv_allowDownload` off when the cvar is set at its start, as JA+ does, so the server sends no paks itself then.

A client of this tree fetches each pak it is missing from `<jp_DlBaseURL><game folder>/<name>.pk3`, the address
the JA+ plugin asks for, with the name escaped (`%20` for a blank) and a slash put after the base when it has
none. So a server set up for JA+ players works as it is: put the paks on the web server in a folder per game
folder (`japlus/`, `base/`), under the names the server has them. The web server has to answer with the file
itself: no redirect is followed, by the plugin neither.

For the player it is the same download as from the server, only faster:

- the first window lists the paks and names the web address they come from, single paks can be unticked;
- what came is checked against the checksum the server names before it gets its name: a wrong file is deleted
  and the game goes on without it (a download from the server would end the connection there);
- the programs in it are blocked and the second window shows what is in it, as for any other download;
- a pak that does not come from the web address is asked from the server itself if that sends files
  (`sv_allowDownload` 1 and a `jp_DlBaseURL` set later), else the game goes on without it and says why;
- the size is not known before a pak is here, the download overlay says "unknown";
- a pak fetched over HTTP may have 512 MB at most (it was 2 GB), as the checksum is only tested at the end;
- the overlay has the two skip buttons named above.

A pak the web server answers 404 for is noted in `japlus/urlRedirect_cache.txt` of the home folder, where the
plugin kept the paks the web address did not have, and is not asked at that address again for six days: the
connect goes straight to the server itself, or on without the pak. A line is `<unix time> <web address> <game
folder>/<name>.pk3`. Only a 404 counts, not a timeout or a refused host. Old and broken lines are dropped when
the file is written, and it is cut at 32 KB from the oldest lines. The format is this tree's own: the plugin's
is not known (there is no decompile of that part). `Sys_HttpGet()` has an out parameter for the answer of the
web server (404 and the rest), on Windows (WinHTTP) and elsewhere (libcurl).

Bit 14 of `cp_pluginDisable` (AllowDownloadURL_Redirect) turns the web download off as `v_downloadHttp 0` does,
so a JA+ config means the same here. A `jp_DlBaseURL` that is not http or https (the `server.cfg` of JA+ names
ftp as possible) is not used, and the console says so once.

A client of this tree does not use a `jp_DlBaseURL` whose host is this machine (`localhost`, `.local`,
`.localhost`), a written-out address of a private network (10/8, 172.16/12, 192.168/16, 100.64/10), the link
(169.254/16), 0/8, 127/8 or 224 and up, a host with a user (`user@host`), an IPv6 address in brackets, or a number
written in another way (`0x...`, fewer than four parts). It says so once in the console. A name that the DNS turns
into such an address is not seen. The reason is that the fetch goes out from the machine of the player, and a
server could otherwise make it ask a router, a printer or a service on the machine itself.

`cl_allowDownload 0` still means no download at all, `v_downloadHttp 0` only from the server itself.

## How it works

Of the web address:

- `CL_InitDownloads()` no longer gives up on a server with `sv_allowDownload 0` when its systeminfo names a web
  address (`CL_DownloadWebOffered()`). `CL_BeginDownloadConfirm()`, which asked the server with `download`, asks
  `CL_DownloadWebStart()` first and the server only when that does not take it (`CL_DownloadUdpStart()`).
- The fetch runs in a thread of its own with `Sys_HttpGet()` (shared/sys/sys_http.h: WinHTTP on Windows, the
  libcurl of the system elsewhere, as the updater), which writes the `.tmp` of the download and nothing else.
  `CL_DownloadWebFrame()` looks at it once a frame and never waits: it shows how much came, and when the thread
  is through it checks the checksum, renames the file and hands it to `CL_NextDownload()`.
- A disconnect or a new level lets the fetch go (`CL_DownloadWebStop()`): it is told to stop and its thread
  frees its slot when it ends (four slots).
- Only one question to a web server runs at a time: a fetch waits while the updater asks, and the updater does
  not start while a fetch runs, also one that was let go and has not ended yet.
- Data of a download the server sends while a pak comes from the web address is not written (`stopdl`).

Of the windows:

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

The web address, 2026-10-19, hidden and silent on Linux (`uitest/webdltest.sh <case>` of the test tools: a server
of this tree on 127.0.0.1 with two test paks, one with a fake `cgamex86_64.dll`, a web server of python on
127.0.0.1, a fresh client profile): both paks from the web address with the address in the first window and the
program blocked (`ok`), an unticked pak not asked for (`untick`), a wrong file under the name of a pak refused by
its checksum while the other came (`wrong`), nobody at the address and the game going on without the paks
(`dead`), the same with a server that sends files itself and both paks coming from it (`fallback`), a disconnect
while a slow web server still sent, the web server seeing the client go away, and both paks after connecting
again (`stop`). Not run: HTTPS (the same `Sys_HttpGet()` as the updater, which uses it), Windows, a real JA+
server and the JA+ plugin side by side, large paks.

The skip buttons and the cache file, 2026-10-27, hidden and silent on Linux (a script in the scratchpad, not in
the tree): a server of the build with two test paks forced by `ref_forcelist.txt`, `jp_DlBaseURL` at a python web
server on 127.0.0.1, reached as `127.0.0.1.nip.io` (a written out private address is refused by
`DL_WebPublicHost()`), the web server having only one of the paks. The 404 was noted, and the second connect
after a level change of the server did not ask the web server again. Skip during a slow transfer (4 KB a second)
left the pak out and the web server saw the client go away. Skip all with three paks left out the one that came
and the two after it. Bit 14 made no request. An ftp address gave its console line once. A screenshot of the
overlay with the buttons was looked at. Not run: a click on the buttons (no mouse in the headless runs, the
commands `v_downloadReview_do skip` and `skipall` were used), HTTPS, Windows, a real JA+ server. The parser of
the cache file has unit tests in `tests/vjk/missing.cpp` (listed, expired, other address, a time after now,
CRLF, names with blanks, a full file, refused names).

The windows:

2026-10-17, hidden and silent on Linux (`uitest/dltest.sh` of the test tools: a server of this tree with a test
pak holding a map file, a texture, a config and a fake `cgamex86_64.dll`, a fresh client profile): both windows
seen in German, kept and session mode, the fake program blocked and not found by `which`, an unticked kind
blocked, the map file and the config loaded from `downloads/` and `downloads/session/`, the session folder empty
after quit. Not run: Windows (where `Sys_UnpackDLL()` would have been the danger), a pure server, a pak with
thousands of files in the second window (it scrolls with the wheel, never seen), a real mouse.
