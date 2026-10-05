# JA+ v2.4 Build 7 in OpenJK

This tree is OpenJK (multiplayer) with the features of the JA+ mod for Jedi Academy built in. The source of JA+
was never released. Everything here was rebuilt from the released binaries:

- `jampgamei386.so` of JA+ v2.4 Build 7, the Linux server module. It still has its symbol names.
- `clientPlugin_v1.4B4.pk3`, the client plugin: `cgamex86.dll` and `uix86.dll` (stripped) plus menus, effects and
  graphics.

The port starts from OpenJK commit 260c59c2 (2026-09-29). All of it is in one commit on top of that history.

## Documents

| File | Content |
|------|---------|
| [porting-method.md](porting-method.md) | How the binaries were read and how the code was carried over |
| [server.md](server.md) | The game module: files, state, features, commands, what it tells the client |
| [client.md](client.md) | cgame and ui: the plugin features, cvars, prediction |
| [differences.md](differences.md) | Where the port differs from the original JA+ and where it changes OpenJK |
| [verification.md](verification.md) | What was compared against the original, what was seen in the game, what is open |

## Where the code is

New files, all JA+ code that did not have to live inside a base function:

| File | Content |
|------|---------|
| `codemp/game/g_japlus.h` | `jpClient_t` (the state of a player), prototypes, version strings |
| `codemp/game/g_japlus_xcvar.h` | the 187 `jp_` cvars |
| `codemp/game/g_japlus_admin.c` | login, ranks, allowed commands, anti abuse, admin gun, console commands |
| `codemp/game/g_japlus_admin2.c` | ammap, amvstr, amrename, amlockteam, amforceteam, amkillvote, ampoll, amweather |
| `codemp/game/g_japlus_fx.c` | sleep, ghost, empower, merc, mindtrick, protect, silence, slap, forced dimension |
| `codemp/game/g_japlus_tele.c` | amtele, amtelemark, amorigin, refuseTele, the teleport effect |
| `codemp/game/g_japlus_cmds.c` | emotes, kiss and hug, flip, jetpack, amcolor, sabercolor, /saber, flamethrower |
| `codemp/game/g_japlus_chat.c` | word filter, silence, clan chat, ignore, name rules, clan tag |
| `codemp/game/g_japlus_info.c` | ammove, amDMGs, duelWhois |
| `codemp/game/g_japlus_dim.c` | alternate dimension and duel isolation: who may touch whom, the traces |
| `codemp/game/g_japlus_hook.c` | grappling hook |
| `codemp/game/g_japlus_highlander.c` | highlander mode |
| `codemp/game/g_japlus_blockwall.c` | invisible blocking walls and their editor |
| `codemp/game/g_japlus_scale.c` | model scale |
| `codemp/game/g_japlus_misc.c` | timers, welcome and MOTD, inactivity protection, `jp_cinfo` |
| `codemp/cgame/cg_japlus.c` | auto replier, cameras, macro scan |

Changes inside base files carry a comment that starts with `JA+`. The largest ones are in `w_saber.c` (damage
rules, thrown saber, kicks), `bg_pmove.c` and `bg_saber.c` (new moves, ledge grab, hook pull, freeze), `w_force.c`
(grabs, flamethrower, absorb, team rules), `g_cmds.c` (duels, votes, say, team), `g_client.c` (connect, spawn,
names) and `cg_players.c` (sabers on the body, effects, fading of other players). `git show --stat 37c22ac4` lists
all 70 files.

## Building

The three modules are `jampgame`, `cgame` and `ui`. The mod directory is `japlus` (`+set fs_game japlus`).

64 bit Linux, with engine and dedicated server:

```
cmake -S . -B build64 -DCMAKE_BUILD_TYPE=Release -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF -DBuildMPRend2=OFF
cmake --build build64 -j$(nproc)
```

32 bit Linux modules:

```
cmake -S . -B build32 -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/linux-i686.cmake -DCMAKE_BUILD_TYPE=Release -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF -DBuildMPRend2=OFF -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF
cmake --build build32 -j$(nproc)
```

32 bit Windows DLLs, cross compiled with mingw-w64 (`jampgamex86.dll`, `cgamex86.dll`, `uix86.dll`):

```
cmake -S . -B buildwin32 -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/i686-w64-mingw32.cmake -DCMAKE_BUILD_TYPE=Release -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF -DBuildMPRend2=OFF -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF
cmake --build buildwin32 -j$(nproc)
```

All three configurations build without errors. The code keeps to the rules of the surrounding files: C, no
assumptions about pointer size in the game code, bounded string functions.

## Files needed at run time

They come from the original JA+ packages and are not part of this repository.

| File | Where | Needed for |
|------|-------|-----------|
| `japlus_gla_anims.pk3` | `base/` on the server and on every client | the new animations. Without it `jp_newGLAAnims` is switched off at map start |
| `japlus_sabers.pk3`, `efx_MOD.pk3` | `japlus/` | the JA+ sabers and effects |
| `modelscale.cfg` | `japlus/` | `jp_allowModelScale` |
| `<map>_holes.cfg` | `japlus/` | blocking walls of a map |
| `autoreplier.cfg` | `japlus/` on the client | auto replier texts |
| contents of `clientPlugin_v1.4B4.pk3` without the two DLLs | `japlus/` on the client, as a pk3 | menus, shaders, effects and graphics of the plugin |

## State

The server module was compared against the original function by function and in about 700 random games that both
modules played side by side, see [verification.md](verification.md). The client side has no such comparison. Parts
of it were looked at in the game on a local test server, most of it was not. Treat the client features as untested
unless verification.md says otherwise.
