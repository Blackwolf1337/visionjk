# Tests

The scripts in `tools/test/` check a release before it is published: they start the zips, run the game with
sanitizers and valgrind, look at the code with cppcheck and shellcheck, and join it with stock OpenJK. All of
them are run from the root of this repository, and what they make goes into `build-test/` (not in git).

## Where the scripts find things

`tools/test/test-env.sh` is sourced by every script. Each path can be given in the environment, the defaults
are the folders next to this repository on the machine of the project.

| Variable | Default | Meaning |
|----------|---------|---------|
| `VJK_TEST_WORK` | `build-test` in this repository | what the tests make: builds, unpacked games, logs |
| `VJK_BASE_ASSETS` | `../GameData/base` | `assets0.pk3` to `assets3.pk3` of the original game |
| `VJK_DOWNLOADS` | `../visionjk-website/site/downloads` | the release zips (`tools/build-game.sh` of the website writes them) |
| `VJK_AGENT` | `../../agent/visionjk` | the working folder of the agent, with the Steam Audio SDK and stock OpenJK |
| `VJK_STEAMAUDIO_SDK` | `$VJK_AGENT/japlus_port/steamaudio/steamaudio` | the unpacked Steam Audio SDK |
| `VJK_STOCK_SRC` | `$VJK_AGENT/openjk_stock/src` | the source of stock OpenJK for `compat-test.sh` |
| `VJK_WEBSITE` | `../visionjk-website` | the website repository, whose scripts `lint.sh` checks too when it is there |
| `VJK_JOBS` | all cores | build jobs |
| `VJK_NICE` | 10 | niceness of the builds |

## Releasing

The zips are built in the website repository. After `tools/build-game.sh <version>` there, run the tests from
this repository on the zips in `site/downloads` of the website:

```
tools/test/smoke-test.sh 0.2.6
tools/test/sanitize-test.sh
tools/test/lint.sh
tools/test/compat-test.sh 0.2.6
```

The smoke test, the sanitizers and the lint have to pass before every release. The compat test is needed when
the protocol, the connect or the JA+ parts changed since the last release. The updater test
(`tools/test/updater/e2e.sh`, and `WIN=1` for Windows) is needed when the updater or the files of a release
changed. Then ask whoever releases whether they want to test in the game themselves. Only after that run
`tools/deploy.sh` in the website repository.

## Testing the zips before publishing

```
tools/test/smoke-test.sh 0.2.6
```

unpacks the zips of `VJK_DOWNLOADS` into `build-test/smoke`, links the base assets (`VJK_BASE_ASSETS`) in, and
starts the Linux client (Vulkan, one client per system), the Linux dedicated server with a bot (Desann) and the
Windows client under Wine (both renderers) once each. A client loads `mp/ffa1`, takes a screenshot and quits. The
clients run in gamescope without an output (`--backend headless`), so no window shows up on the desktop, and
without sound. It needs gamescope and wine. Windows runs in a Wine prefix of its own, `build-test/smoke-wine`.

A run passes when the game exits with 0, no error line is in its log, and the version line, the cgame and the
level are there (and the bot joined, for the server). Logs are in `build-test/smoke/<run>.log`, screenshots in
`build-test/smoke/home-<run>/japlus/screenshots`. A full run takes about a minute. `--only linux|windows` tests
one system, `--keep` keeps the unpacked games, `--dir <folder>` tests the Linux game in a folder instead of the
zip. To test the zips of another folder set `VJK_DOWNLOADS`. gamescope 3.16 crashes when it ends after the game,
so the script takes the exit code of the game itself.

## Unit tests

The unit tests are in `tests/vjk/` (Boost). They are built with `-DBuildTests=ON` and run by
`tools/test/sanitize-test.sh`, see below. They look at what the game reads from outside: the file lists and paths
of the updater, UTF-8 and Windows-1252, info strings, the status and look packets of the voice chat, and what a
player types for a boombox. The tests also check SHA-256 (`shared/web/web_sha256.c`, which the updater checks every
file of a release with) against the values of FIPS 180-4, also fed in pieces of every size around a block. The file
lists of the updater get their edge cases (the limits of a file, of the number of files and of the length of a
list, every way a line can be wrong) and 300000 changed and random lists, each in a buffer of exactly its size, so
the sanitizers see a read past it. Any list that is taken may only name paths the game may touch. These came on
2026-10-24 from `tools/updatetest/updatetest.c` of the agent folder, which they replace. There are 45 test cases,
they take about half a second.

## Sanitizers and valgrind

```
tools/test/sanitize-test.sh
```

builds this repository as it is (HEAD and local changes) for Linux on this machine with AddressSanitizer and
UndefinedBehaviorSanitizer (`build-sanitize`, installed into `build-test/stage-sanitize`) and runs
`tools/test/smoke-test.sh --dir` on it. Then it builds only the dedicated server and the server game module
without sanitizers (`build-valgrind`) and runs the server once under valgrind (level, bot, quit). A valgrind
error fails it, leaks are only counted. Leaks are not checked under ASan (graphics drivers). `--only
sanitizers|valgrind` runs one part. It takes about two minutes with a warm ccache (the sanitizers part alone
about 45 seconds).

Before the smoke test the script builds the unit tests in `build-sanitize` and runs them with the sanitizers
and leak detection on. Their log is `build-test/logs/sanitize-unit.log`.

`tools/test/sanitize-suppressions.txt` holds known cases of undefined behaviour that UndefinedBehaviorSanitizer
lets pass, one line each (`<check>:<file>`), so that a run fails only on a new one. A line comes out when its case
is fixed. When the tests came on 2026-10-24 they found the zone allocator and the pool of the game module handing
out misaligned blocks (the bot structs among them), three integer shifts (`MSG_WriteBits`, the checksum feed of the
server, the mp3 decoder) and a wrong downcast in `FxTemplate.cpp`. All of these are fixed and the file is empty.
`smoke-test.sh` fails a run on any sanitizer report.

## Static checks

```
tools/test/lint.sh
```

runs cppcheck over `codemp` and `shared` (from the `compile_commands.json`, Linux x86_64, `lib/` is left out)
and shellcheck over the scripts of this repository and of the website repository (`VJK_WEBSITE`). cppcheck
found 520 distinct findings in the old code. They are in `tools/test/cppcheck-baseline.txt` (file, check,
severity, message, no line numbers), and only a finding that is not in there fails. `tools/test/lint.sh
--baseline` rewrites the baseline after a fix, or after a new finding was looked at and is fine. The first run
takes about a minute and a half, later runs a few seconds (cache in `build-test/cppcheck`). The scripts have to
be shellcheck clean.

## Getting along with other games

```
tools/test/compat-test.sh 0.2.6
```

builds stock OpenJK once from the source in `VJK_STOCK_SRC` into `build-test/openjk-stock` (Release, client,
dedicated server, modules and its renderer; most other clients and servers are made from it). Then it makes two
runs on 127.0.0.1: the stock client (named Legacy) joins the dedicated server of the release (`fs_game japlus`),
and the client of the release (named Vision) joins a stock dedicated server. A run passes when the server logs
the ClientBegin of the client, neither log has an error and the client ends by itself (it stays about 1500
frames after the connect). A full run takes about 20 seconds. `--dir <folder>` tests a game folder instead of
the zip, `--keep` keeps `build-test/compat`. Logs are in `build-test/compat/<run>-server.log` and
`build-test/compat/<run>-client.log`. The original JA+ server module is only 32 bit and there is no original
`jamp.exe` on this machine, so neither is tested. `tools/test/game-run.sh` holds what `smoke-test.sh` and
`compat-test.sh` share (a game in gamescope headless, its log, the checks). A server that `stop_server` ended
no longer fails a run for its line "Received signal 15, quitting" (the dedicated server quits on SIGTERM since
2026-10-27, the compat test had failed on it).

## Joining a server

```
tools/test/join-test.sh <game folder> [--bots n] [--pure] [--strace] [--reuse] [--keep]
```

starts a dedicated server of the game folder on 127.0.0.1 with bots (`addbot` through the console fifo, up to
31; one of them is often dropped again, so 30 stay), and a headless client that joins three times. Then the
server changes the level once. The script prints the `v_joinReport` and `v_loadReport` lines, see
[loading.md](loading.md) for the numbers. `--bots n` is the number of bots, `--pure` sets `sv_pure 1`,
`--reuse` keeps the home of the client of the run before (filled caches: the first join is that of a game
started again), `--strace` counts and times the failed file opens, `--keep` keeps what the run made. It
replaces `jointest.sh`, `joinlocal.sh` and `loadstats.py` of the agent folder.

## The intrusion detection of the client

```
tools/test/ids-test.sh [--build <folder>] [--keep] [scenario ...]
```

needs a build with the CMake option `BuildDevIdsTest=ON` (developer builds only, never a release). That builds
one more program, `visionjkded-idstest`: the dedicated server with `server/sv_idstest.cpp`, which with the cvar
`vs_idsTest` does on purpose what no server of the game does toward its clients. It is never installed. The
release dedicated server has none of this code. `vs_idsTest` is a sum: 1 cvars a server may not set in the
systeminfo (`cl_allowDownload`, `rconPassword`), 2 a protected cvar (`fs_basepath`), 4 a path as the game folder
(`fs_game ../idstest`), 8 a download whose first block says half of its size, 16 a download message in every
snapshot of a client that asked for none, with 16 bytes of data in its block. The script installs the build folder (default `build64`) into
`build-test/ids/install`, runs the test server on 127.0.0.1 port 29097 for each scenario and a headless client
that connects three times (about ten seconds each), so a server the client left is refused once and joined
again. The scenarios: `none` (`vs_idsTest 0`: the client joins three times and `ids.log` stays empty), `cvars`
(written down, the client stays and joins three times), `protected`, `gamedir` and `unasked` (the
client leaves twice and is refused once in between; a client that does not read the whole download message
fails `unasked`), `size` (the same, on the level `mp/idstest`, a copy of
`mp/duel4` in a pak only the server has, so the client downloads it; it also checks that no `.tmp` of the download is left in the home folder of the
client, and run with the deletion taken out it failed with the `.tmp` still there). It checks the client log and the `ids.log`
of the client. The logs are `build-test/ids/<scenario>-server.log`, `-client.log` and `-ids.log`; `--keep` also
keeps the install and the home folders. A full run takes about three minutes.

Since 2026-10-28 the client also runs `v_idsstatus` and `v_idsSeqShown` at the end of each scenario, and the
script checks them: `none` must print `nothing this run`, `cvars` must list the event in `v_idsstatus` and have
`v_idsSeqShown` 3 (the cgame showed the notice for each event), and `protected` must list the server left. Right
after the first connect the client takes a screenshot (`screenshot` after 500 frames, while the notice is up).
The pictures are copied to `build-test/ids/<scenario>-shots/`. The script does not judge them: they are there for a
person to look at the notice.

## The updater

```
tools/test/updater/e2e.sh
```

is the end to end test of the updater of the client (it was moved here from the agent folder on 2026-10-24). It
makes a game folder of a made up version 0.0.1 under `build-test/updater`, serves a made up 0.0.2 on the loopback
address (port 8099) and lets the client find it, fetch it, put it in place and start again. There are twelve
scenarios: `update damaged cutshort undo off down address devbuild elsewhere repair shadow same`. Names as
arguments run only those:

```
tools/test/updater/e2e.sh update damaged
```

`WIN=1` does the same with the Windows build (`buildwin64`) under Wine, in a prefix of its own
(`build-test/updater/wine`):

```
WIN=1 tools/test/updater/e2e.sh
```

The test programs are `build64` (or `buildwin64`) with `client/cl_update.cpp` built again, with the address of the
test server and a version of its own. The file lists of the made up release are written by `fill_update` of
`tools/build-game.sh` of the website repository (`VJK_WEBSITE`), as it is. The modules are stripped as in a
release: above 16 MB the look at every start takes a file by its size alone (`UPDATE_QUICK_BYTES`).

Linux has 116 checks, Windows 117. A run takes about five minutes each. Run it when the updater or the files of a
release change. The scripts of the agent folder for a real Windows VM (`wintest.sh`, `winfs.sh`) source its
`tools/test/updater/lib.sh`.

## No window, no sound

Every client of the tests runs through `tools/test/headless.sh`: in gamescope without an output (`--backend
headless`), with `SDL_AUDIODRIVER=dummy`. No window shows up and nothing is played.

```
HEADLESS_TIMEOUT=60 tools/test/headless.sh <command> [arguments]
```

`HEADLESS_TIMEOUT` is the number of seconds until the command is killed (default 300, exit code 124 then),
`HEADLESS_SIZE` the size of the display (default `1280 720`), `HEADLESS_LOG` the file for the output of gamescope
and of the command (default nowhere).

gamescope 3.16 crashes on its way out, and the script kills what is left. The exit code is the one of the command.
A Windows client killed that way (137) counts as ended when its log has the last line of the shutdown of the
engine ("Automatically freeing ... blocks"). The old `tools/hidden.sh` of the agent folder calls this script.
