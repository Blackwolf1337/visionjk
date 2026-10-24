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
the protocol, the connect or the JA+ parts changed since the last release. Then ask whoever releases whether
they want to test in the game themselves. Only after that run `tools/deploy.sh` in the website repository.

## Testing the zips before publishing

```
tools/test/smoke-test.sh 0.2.6
```

unpacks the zips of `VJK_DOWNLOADS` into `build-test/smoke`, links the base assets (`VJK_BASE_ASSETS`) in, and
starts the Linux client (Vulkan and vanilla renderer), the Linux dedicated server with a bot (Desann) and the
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
player types for a boombox.

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
dedicated server, modules and rd-vanilla; most other clients and servers are made from it). Then it makes two
runs on 127.0.0.1: the stock client (named Legacy) joins the dedicated server of the release (`fs_game japlus`),
and the client of the release (named Vision) joins a stock dedicated server. A run passes when the server logs
the ClientBegin of the client, neither log has an error and the client ends by itself (it stays about 1500
frames after the connect). A full run takes about 20 seconds. `--dir <folder>` tests a game folder instead of
the zip, `--keep` keeps `build-test/compat`. Logs are in `build-test/compat/<run>-server.log` and
`build-test/compat/<run>-client.log`. The original JA+ server module is only 32 bit and there is no original
`jamp.exe` on this machine, so neither is tested. `tools/test/game-run.sh` holds what `smoke-test.sh` and
`compat-test.sh` share (a game in gamescope headless, its log, the checks).
