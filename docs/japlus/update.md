# The updater of the client

The client asks the website whether there is a newer version of the game, tells the player, and on his word
fetches the files that changed and puts them in place. It is not a JA+ feature. It is in the engine
(`client/cl_update.cpp`) and in the ui module (`ui/ui_update.c`), so only the client of this tree has it. The
dedicated server and the client that runs in a browser do not: a page is the newest version every time it is
loaded.

Added 2026-10-10. Run on Linux, under Wine and on a Windows 11 in a virtual machine, each against a server
on the local network: see "Checked".

## For players

At its start the game asks the website once. That happens beside the game, which does not wait for the
answer. If there is a newer version, the console says so and the main menu has a line above its EXIT button:

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
  The folder of the program is the one the system names (`Sys_ProgramFolder`), not the one the program was
  called with: on Windows that is empty for a program started by its bare name from a batch file. Both are
  compared as whole paths, so the same folder spelled another way is the same folder.
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
in code and is no item of a menu file, so it is there with the main menu of the client assets in `japlus`,
where the game starts, and with the one of the original game in `base`.

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
- The console takes two slashes as the start of a comment, which cut such an address off behind `https:`.
  `main()` now puts an argument with `//` in it into quotes before it hands the command line on, so the
  line above works as it is written, also from a shortcut on Windows.

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

2026-10-10, Linux, no sound and no window in sight. The tools are in `tools/updatetest` beside the tree.

- Unit tests (`run.sh`, with the address and undefined behaviour sanitizers): 182 checks of versions, of what
  a path and an address may look like, of the reader of file lists and of SHA-256. Among them 300000 lists
  with something changed in them or made of noise, each in a buffer of exactly its size: nothing was read
  past one, and no list that was taken named a path the game does not touch. SHA-256 against the values of
  the standard, whole and in pieces of every size around a block.
- The reader and SHA-256 against real files: the lists `tools/build-game.sh` writes for the two zips of
  0.1.0 (27 and 28 files) were read, and every one of the 55 checksums was the one `sha256sum` gives.
- End to end (`e2e.sh`): a game folder of a made up version 0.0.1 with the original game linked into it, a
  web server on the loopback address with version 0.0.2 (its lists written by `fill_update` of the website's
  build script), and the client on a hidden workspace. The two programs are the engine of the build with
  `cl_update.cpp` built again for the test address and each version. 72 checks in nine runs:
  - 0.0.1 finds 0.0.2, the console and the line of the menu say so, `update` fetches the four files that
    differ and none of the others, the game starts again twice and is 0.0.2: the new program, still one that
    can be run, the changed file, the `.cfg` the player had untouched, the one he did not have and a file in
    a new folder in place, no `.vjknew`, `.vjkold` or journal left, and nothing newer offered.
  - A file of the server that is not the one of the list: the console names it, nothing was changed, what
    came in right before it is kept, and the next try fetches only what is missing and goes through.
  - A swap that was cut short after one file of four: the next start finishes it and is 0.0.2.
  - A file that can not be put in place (its folder read only): the two that had been moved are moved back,
    the journal is gone, the game starts as 0.0.1, says which file it was and offers 0.0.2 again.
  - `cl_updateCheck 0`: the server gets no request.
  - No server: the start says nothing, `update_check` says why, the game runs.
  - The address: without https on the command line it is refused and the address of the build is asked. A
    config file with `set` and `seta` can not change it, neither can the console. With https on the command
    line it is taken, and a server that speaks no TLS there gives no update ("SSL connect error").
  - A build between two releases and a game with another `fs_basepath`: each says why it does not update,
    and the server gets no request. The folder of the program under another spelling is not another folder.
- The line on the menu was looked at in two screenshots, once offered and once failed, on the main menu of
  the client assets and on the one of the original game.
- `check_update` of the website's `tools/deploy.sh`, by itself on a copy of that server: a true list
  passes, and a changed file, a byte flipped at the same size, a file too many, a file missing, another
  version on the page and no release on the page are each named.
- The Windows build under Wine 11.19 (`WIN=1 e2e.sh`, a Wine prefix of its own): the same nine runs, 73
  checks. The list and the files came in through WinHTTP, the game started itself again twice
  (`CreateProcess`), put the files in place and was 0.0.2, with the paths of Windows. Its release had one
  file more, `SDL2.dll` with other bytes: a library the running program has loaded from its start was
  replaced. What a program writes to its output goes on with the one it starts, so a log has all three.
- Builds: client, ui module and dedicated server for Linux 64 and with mingw for Windows 64, and the client
  for the browser. The Windows client imports `WINHTTP.dll` in addition to what it imported before.

- Windows 11 Pro 24H2 (German, fresh install, Defender as it comes) in a virtual machine without a graphics
  card, the test folder `tools/updatetest/wintest.sh` packs, started by its batch file, driven by keys from
  outside and read from pictures of its screen:
  - The game runs there with the OpenGL Windows has by itself. At the first start Windows asks whether the
    program may be reached from the network (its firewall, as for any game), and nothing else ever asked or
    warned.
  - The line stands on the main menu and lights up under the cursor of the game.
  - `update` in a folder of the user (`C:\vjk-test`): five files came in through WinHTTP, among them the
    program and `SDL2.dll`, which the running program has loaded. One second later the game had started
    itself again twice and was 0.0.2. No `.vjknew`, `.vjkold` or journal was left, also not for one start:
    the old program could be deleted at once. The `.cfg` the player had was his own still. The log of the
    batch file has the lines of all three starts.
  - The same in `C:\Program Files`, as a user without the right to write there: "The folder of the game can
    not be written to (base/new.cfg)", the line of the menu says that it failed, no file was fetched and
    nothing in the folder changed.
- Builds: client, ui module and dedicated server for Linux 64 and with mingw for Windows 64, and the client
  for the browser. The Windows client imports `WINHTTP.dll` in addition to what it imported before.

Found by these runs and fixed: `+set cl_updateURL https://...` was cut off by the console (see "Who is
trusted"), the line of the menu lay over the top of the logo, and, only on the real Windows: a game started
by its bare name from a batch file took the folder it was called with, which is empty there, for its own,
and refused to update as a game that reads its files from somewhere else. Wine names the whole path and did
not show it.

Not checked:

- A click on the line with a real mouse. The cursor of the game was moved onto it once (it lit up), a click
  through the two pointer devices of the virtual machine could not be sent cleanly. The runs typed `update`.
- Windows 10, a Windows with a graphics driver, an antivirus other than Defender, and a program that was
  fetched with a browser (which marks it as from the internet; this one came through curl).
- A real https server with a real certificate, and a certificate that is wrong. The test server spoke plain
  http, which only the address of a build may be.
- A download that is stopped (`update_cancel`) or a line that drops in the middle of a file.
- A release made by `tools/build-game.sh` from start to end, and the site behind its proxy.
