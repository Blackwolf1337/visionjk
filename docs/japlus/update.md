# The updater of the client

The client asks the website whether there is a newer version of the game, tells the player, and on his word
fetches the files that changed and puts them in place. It is not a JA+ feature. It is in the engine
(`client/cl_update.cpp`) and in the ui module (`ui/ui_update.c`), so only the client of this tree has it. The
dedicated server and the client that runs in a browser do not: a page is the newest version every time it is
loaded.

Added 2026-10-10. Built, never run: see "Checked".

## For players

At its start the game asks the website once. That happens beside the game, which does not wait for the
answer. If there is a newer version, the console says so and the main menu has a line at its top:

```
Version 0.1.1 is out. Click here to update (12.3 MB)
```

A click fetches the files that changed, the line shows how far that is, then the game ends and starts again by
itself, twice in a row: once to put the files in place, once as the new version. Settings, binds and `.cfg`
files are not touched.

| Cvar | Default | Meaning |
|------|---------|---------|
| `cl_updateCheck` | 1 | 0: the website is not asked at the start |
| `cl_updateURL` | https://jka.vision/downloads/update | where it asks. Only the command line can name another one, see "Who is trusted" |
| `cl_updateState`, `cl_updateText` | | read only: what the updater is doing, and the line of the menu |

| Command | |
|---------|---|
| `update_check` | asks the website now, also with `cl_updateCheck 0` |
| `update` | fetches the newer version, puts it in place and starts the game again. Only in the main menu, not on a server |
| `update_cancel` | stops a download. What came in is kept for the next try |

On Linux the game asks through the `libcurl` of the system (`libcurl.so.4`). Without it there is no update
check, one line says so when `update_check` is typed, and the game runs.

## What is never replaced

- A build that is not a release. The version of a build is the tag it was made from, and only a tag like
  `v0.1.1` counts. A build between two releases (`v0.1.1-3-gabc1234`) is somebody's own and is left alone.
- A `.cfg` that is there (`japlus/server.cfg` and the others). A release can bring a new one, it never
  replaces one.
- Anything outside the folder the program is in, and anything a file list names that is not a plain path
  below that folder.
- A game that is started with another `fs_basepath` than the folder of its program. That is set up by hand.
- A game that is connected to a server: `update` says to leave first.

## How it works

**The file list.** The website has one for every system, plain text:

```
https://jka.vision/downloads/update/windows-x86_64.txt
https://jka.vision/downloads/update/linux-x86_64.txt

visionjk-update 1
version 0.1.1
file 2051598 <sha256, 64 hex digits> visionjk.x86_64.exe
keep 13071 <sha256> japlus/server.cfg
```

The first line is the format: a client reads no list of another format than its own, and skips a line that
starts with a word it does not know. `keep` is a file that is only put there if it is missing. The files are at
`<address>/<version>/<system>/<path>`. `tools/build-game.sh` of the website writes both.

**Asking.** A thread fetches the list (`Sys_HttpGet`). If its version is newer than the build, every file of
the list is compared with the one that is here: by size, and if that is the same by SHA-256. What differs or
is missing has to be fetched. The game learns the result some frames later and says it, in the console and in
`cl_updateState` and `cl_updateText`, which the ui module draws on the menu named `main`. That line is drawn
in code and is no item of a menu file, because the first main menu a player sees is the one of the original
game in `base`.

**Fetching.** Each file goes to `<path>.vjknew` beside the one it replaces and is hashed as it comes in. It
counts only with exactly the size and the SHA-256 the list names, anything else is deleted. One that is there
from an earlier try and is right is not fetched again. Nothing of the game is touched in this step.

**The swap.** A program file that runs can not be written to on Windows, only renamed, and a file the game has
open (a pk3) not even that. So the game itself moves nothing. It writes a journal into its folder
(`visionjk-update.txt`: which files wait), ends, and starts again. The first thing `main()` does, before the
engine is started and before any file is open, is to look for that journal (`CL_UpdateResume`):

1. Every file that waits: the old one becomes `<path>.vjkold`, the new one takes its name.
2. If one can not be moved, the ones that were are moved back, the journal is dropped and the game starts as
   the old version and says which file it was. `update` then tries again with the files that are still there.
3. Otherwise the journal says `done` and the program starts once more, now as the new one.
4. That start deletes the `.vjkold` files and the journal, and the console says that the game was updated.

A swap that was cut short (the machine went off) is finished by the next start in the same way. On Linux a
file keeps whether it can be run, a new one gets that by its name (`visionjk.x86_64`).

**The web.** The engine brings no code for https. On Windows it asks WinHTTP, which is part of the system
(`sys_http_win32.cpp`). Everywhere else it loads the `libcurl` of the system when it first needs it and is
built without its headers (`sys_http_curl.cpp`). Both check the certificate of the server against what the
system trusts, follow no redirect, and take nothing but an answer of 200.

| File | Content |
|------|---------|
| `client/cl_update.cpp` | the journal, the swap at the start, the thread, cvars and commands |
| `shared/update/upd_manifest.c`, `.h` | reading a file list, versions, which paths and addresses are allowed. Calls nothing of the engine |
| `shared/web/web_sha256.c` | SHA-256 in pieces |
| `shared/sys/sys_http.h`, `sys_http_win32.cpp`, `sys_http_curl.cpp` | one question to a web server |
| `shared/sys/sys_main.cpp` | `main()` calls `CL_UpdateResume`, and the program starts itself again when it has ended (`Sys_RestartOnExit`) |
| `ui/ui_update.c` | the line on the main menu |

## Who is trusted

The server at the address the game asks, by its certificate. A release is not signed (decided 2026-10-10).
What a client fetches is checked against the file list, and the file list comes from the same server.

So whoever can change the files of that server, or holds the name `jka.vision` later, decides what the
clients out there install. Keep the domain renewed.

Because of that the address is hard to change:

- It is compiled in (`-DUpdateURL=...`, by default `https://jka.vision/downloads/update`).
- A player can name another one on the command line and nowhere else:
  `visionjk.x86_64 +set cl_updateURL https://example.org/update`. `Com_Init` takes it right after the command
  line was read, before any config file is run, and the updater keeps a copy of its own and never reads the
  cvar again. No config, no module, no game server and nobody at the console can send a client elsewhere.
- An address a player gives has to be `https://`. If it is not, the game says so and asks the one it was
  built with.

That one way in is what moving to another domain takes: a new release has the new address compiled in, and
a player with an older one starts it once with `+set cl_updateURL`.

If the website can not be reached, nothing happens: no answer, no word (with `developer 1` one line), and the
next start asks again. A download that breaks off changes no file.

## For whoever makes a release

- `tools/build-game.sh` of the website repository writes the lists and the files into
  `site/downloads/update`, `tools/deploy.sh --check` looks whether they are true. From the moment a list is
  published, every release build that is started offers that version.
- A version with a suffix (`0.1.0-rc1`) gets no list and never updates itself.
- `-DUpdateVersion=v0.0.1` makes a build take itself for that release, for a try against a server of one's
  own together with `-DUpdateURL=`. The address of a build may be `http://`, the one a player gives may not.

## Checked

2026-10-10.

- Builds: client, ui module and dedicated server for Linux 64 and with mingw for Windows 64, and the client
  for the browser. The Windows client imports `WINHTTP.dll` in addition to what it imported before.
- The reader of file lists and SHA-256, once, against real files: the lists `tools/build-game.sh` writes for
  the two zips of 0.1.0 (27 and 28 files) were read by `UPD_ParseList`, and `Web_SHA256` gave the checksum
  `sha256sum` gives for every one of the 55 files, the biggest 85 MB, with the sanitizers on.

Not checked: everything at run time. The game never asked a server, no file was ever fetched or swapped by
it, the line on the menu was not seen, the game never started itself again. On Windows in particular: that
a program and its loaded libraries can be renamed while they run is how Windows is documented, not something
this was tried on, and neither is what an antivirus says to it. A folder the player may not write to (Program
Files) should end in one line of the console, untried.
