# JA+ v2.4 Build 7 in VisionJK

This tree, VisionJK, is OpenJK (multiplayer) with the features of the JA+ mod for Jedi Academy built in. The source of JA+
was never released. Everything here was rebuilt from the released binaries:

- `jampgamei386.so` of JA+ v2.4 Build 7, the Linux server module. It still has its symbol names.
- `clientPlugin_v1.4B4.pk3`, the client plugin: `cgamex86.dll` and `uix86.dll` (stripped) plus menus, effects and
  graphics.

The port starts from OpenJK commit 260c59c2 (2026-09-29) and is the commits on top of that history.

## Documents

| File | Content |
|------|---------|
| [porting-method.md](porting-method.md) | How the binaries were read and how the code was carried over |
| [server.md](server.md) | The game module: files, state, features, commands, what it tells the client |
| [client.md](client.md) | cgame and ui: the plugin features, cvars, prediction |
| [differences.md](differences.md) | Where the port differs from the original JA+ and where it changes OpenJK |
| [verification.md](verification.md) | What was compared against the original, what was seen in the game, what is open |
| [webadmin.md](webadmin.md) | The web admin panel taken from JKAdmin (not a JA+ feature): cvars, requests, JA+ glue, limits |
| [security.md](security.md) | Security pass over `codemp/`: findings, fixes, build hardening, what stays, server settings |
| [threading.md](threading.md) | The frame loop from `main`, why server and client share one thread, timing numbers, what was not built |
| [voice.md](voice.md) | Voice chat with the microphone (not a JA+ feature): in the cgame, relayed by the game module, engine unchanged, clients without it unaffected |
| [audio.md](audio.md) | Sound that knows the level (not a JA+ feature): `s_spatial`, with Steam Audio in the engine. Walls muffle, rooms ring, sounds come round corners. Measured, not listened to yet |
| [renderer.md](renderer.md) | rd-vulkan-vision, a Vulkan renderer next to rd-vanilla (not a JA+ feature): how it is built, the state of each part, what runs on the GPU, differences, what is left |

## Where the code is

New files, all JA+ code that did not have to live inside a base function:

| File | Content |
|------|---------|
| `codemp/game/japlus/g_japlus.h` | `jpClient_t` (the state of a player), prototypes, version strings |
| `codemp/game/japlus/g_japlus_xcvar.h` | the 187 `jp_` cvars |
| `codemp/game/japlus/g_japlus_admin.c` | login, ranks, allowed commands, anti abuse, admin gun, console commands |
| `codemp/game/japlus/g_japlus_admin2.c` | ammap, amvstr, amrename, amlockteam, amforceteam, amkillvote, ampoll, amweather |
| `codemp/game/japlus/g_japlus_fx.c` | sleep, ghost, empower, merc, mindtrick, protect, silence, slap, forced dimension |
| `codemp/game/japlus/g_japlus_tele.c` | amtele, amtelemark, amorigin, refuseTele, the teleport effect |
| `codemp/game/japlus/g_japlus_cmds.c` | emotes, kiss and hug, flip, jetpack, amcolor, sabercolor, /saber, flamethrower |
| `codemp/game/japlus/g_japlus_chat.c` | word filter, silence, clan chat, ignore, name rules, clan tag |
| `codemp/game/japlus/g_japlus_info.c` | ammove, amDMGs, duelWhois |
| `codemp/game/japlus/g_japlus_dim.c` | alternate dimension and duel isolation: who may touch whom, the traces |
| `codemp/game/japlus/g_japlus_hook.c` | grappling hook |
| `codemp/game/japlus/g_japlus_highlander.c` | highlander mode |
| `codemp/game/japlus/g_japlus_blockwall.c` | invisible blocking walls and their editor |
| `codemp/game/japlus/g_japlus_scale.c` | model scale |
| `codemp/game/japlus/g_japlus_misc.c` | timers, welcome and MOTD, inactivity protection, `jp_cinfo` |
| `codemp/cgame/cg_japlus.c` | auto replier, cameras, macro scan |
| `codemp/game/webadmin/g_webadmin.c`, `codemp/game/webadmin/index.html` | web admin panel from JKAdmin, see [webadmin.md](webadmin.md) |
| `codemp/game/g_blackhole.c` | the entity `misc_blackhole`, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/g_schizo.c` | hallucinations only one player gets, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/g_props.c` | the entities `misc_ball`, `misc_cube` and `misc_prop` (any model of the game) with physics, taking them with `+button13` (admins take players with it too), not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/g_poop.c` | the emote `ampoop`, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/g_flatulence.c` | a player who breaks wind all the time, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/g_disorient.c` | a player whose view sways and is thrown around, not from JA+, see [webadmin.md](webadmin.md) |

Changes inside base files carry a comment that starts with `JA+`. The largest ones are in `w_saber.c` (damage
rules, thrown saber, kicks), `bg_pmove.c` and `bg_saber.c` (new moves, ledge grab, hook pull, freeze), `w_force.c`
(grabs, flamethrower, absorb, team rules), `g_cmds.c` (duels, votes, say, team), `g_client.c` (connect, spawn,
names) and `cg_players.c` (sabers on the body, effects, fading of other players). `git show --stat 37c22ac4` lists
all 70 files.

## Building

The three modules are `jampgame`, `cgame` and `ui`. The mod directory is `japlus`. The engine of this
tree starts in it by itself, another OpenJK needs `+set fs_game japlus`.

64 bit Linux, with engine and dedicated server:

```
cmake -S . -B build64 -DCMAKE_BUILD_TYPE=Release
cmake --build build64 -j$(nproc)
```

64 bit Windows, cross compiled with mingw-w64: the client, the renderers and the three modules
(`jampgamex86_64.dll`, `cgamex86_64.dll`, `uix86_64.dll`):

```
cmake -S . -B buildwin64 -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/x86_64-w64-mingw32.cmake -DCMAKE_BUILD_TYPE=Release -DBuildMPDed=OFF
cmake --build buildwin64 -j$(nproc)
```

Single player is not built: `BuildSPEngine`, `BuildSPGame` and `BuildSPRdVanilla` are off by default, as the
three for Jedi Outcast always were. `-DBuildSPEngine=ON -DBuildSPGame=ON -DBuildSPRdVanilla=ON` switches it on.

The tree is built for 64 bit only: cmake stops on a 32 bit compiler. The toolchain files, the SDL2 libraries and
the code for 32 bit x86 were removed after commit `5ab290ba`. A stock `jamp.exe` is 32 bit and can not load these
modules, it plays with its own cgame and ui.

Both configurations build without errors. The code keeps to the rules of the surrounding files: C, no
assumptions about pointer size in the game code, bounded string functions.

The client can use Steam Audio for its sound (`s_spatial`, [audio.md](audio.md)). For that Valve's repository
is a submodule, `lib/steamaudio`: `git submodule update --init` once after a clone. Without it the client is
built without the feature.

### A folder to release

The build directory is not meant to be looked into. One command collects what a build made:

```
cmake --install build64 --prefix release
```

`release/JediAcademy` is then a complete game folder, and its content goes into `GameData` as it is:

```
JediAcademy/
  visionjk.x86_64  visionjkded.x86_64  rd-vanilla_x86_64.so  rd-vulkan-vision_x86_64.so
  japlus/
    jampgamex86_64.so  cgamex86_64.so  uix86_64.so
    zz_japlus_client_assets.pk3
    server.cfg and the other configs of assets/configs
  base/
    jampgamex86_64.so  cgamex86_64.so  uix86_64.so
```

The renderers have to lie next to the engine and the modules in the mod directory. The same three modules are
in `base/` as well: the engine looks there when the client plays as base, which is every server that is not
`japlus` and a start with `+set fs_game base`. Without them that ends with `VM_CreateLegacy on ui failed`.

A start without arguments runs in `japlus`: it is the default of `fs_game` in this engine (`JAPLUSGAME` in
`codemp/qcommon/q_shared.h`), for the client and for the dedicated server. So the menus, sabers and effects of
the port are there from the first picture on. The same command on the Windows build directory gives the `.exe`, `SDL2.dll` and the `.dll` files in
the same layout. A config that is already in the folder is kept, so an edited `server.cfg` survives the next
install. Not in the folder, because the build does not make them: `libphonon.so` of Steam Audio for `s_spatial`
([audio.md](audio.md)), `japlus_gla_anims.pk3` of the original JA+ package, and on Windows `libwinpthread-1.dll`
of mingw. On macOS the modules are installed into the app as before.

Every build also packs the assets of the port, `assets/japlus_client` (the files of the JA+ client plugin
1.4B4 with the menus of the port, and the JA+ sabers and effects), into
`<build>/assets/zz_japlus_client_assets.pk3`, and `cmake --install` puts
it into `JediAcademy/japlus/` (`-DJPAssetsDir=` names another mod directory). A file that was changed, added or
removed there is packed again by the next build. The pk3 is the same for every platform: the files are kept
byte for byte (`assets/.gitattributes`) and `assets/make_pk3.py` writes them in the order of
`assets/japlus_client.order`, which the player menu and the pure check depend on. Packing needs Python 3,
`-DBuildJPAssets=OFF` builds without it. A client needs this pk3 in `japlus/` next to the modules, and so does
the server: it reads the sabers from it.

The configs of the JA+ package are in `assets/configs` and are not packed: `cmake --install` puts them into the
same directory as loose files, and only where there is no file of that name yet, so a config an admin edited
stays. `server.cfg` is the example of the package with its passwords (rcon and the three admin ranks) emptied:
the ones it came with can be looked up by everybody. Set your own before a server runs with it.

## Files needed at run time

Only the first one is not part of this repository: it comes from the original JA+ package.

| File | Where | Needed for |
|------|-------|-----------|
| `japlus_gla_anims.pk3` | `base/` on the server and on every client | the new animations. Without it `jp_newGLAAnims` is switched off at map start |
| `modelscale.cfg` | `japlus/`, from `assets/configs` | `jp_allowModelScale` |
| `<map>_holes.cfg` | `japlus/`, from `assets/configs` | blocking walls of a map |
| `autoreplier.cfg` | `japlus/` on the client, from `assets/configs` | auto replier texts |
| `server.cfg`, `admin.cfg`, `jk2.cfg`, `vstr.cfg`, `chat.cfg`, `saber.cfg` | `japlus/`, from `assets/configs` | the example server config of JA+ and what it loads. Nothing reads them unless a server is started with `+exec server.cfg` |
| `zz_japlus_client_assets.pk3` | `japlus/` on the server and on the client | menus, shaders, effects and graphics of the plugin, with the menus of the port, and the JA+ sabers and effects |

The last file is built with the tree from `assets/japlus_client` (see "Building"), which holds the contents of
`clientPlugin_v1.4B4.pk3`, `japlus_sabers.pk3` and `efx_MOD.pk3` of JA+ 2.4 Build 7, without the two DLLs of the
plugin and without the `Thumbs.db` files. It replaces those three: a folder that has it does not need them. It is packed with the folder entries of the original pk3 and in
their order: the engine lists a folder of a pk3 only when it has an entry of its own, and the head list of the
player menu, and with it the model a new player starts with, follows that order.

## State

The server module was compared against the original function by function and in about 700 random games that both
modules played side by side. For the client every function of the plugin DLLs was compared with the unmodified
1.01 modules to find what the plugin changed, and what the cgame and ui draw was compared with the original plugin
on recorded test scenes. Both comparisons, what they found and what they do not cover are in
[verification.md](verification.md). Little of the client was used by a person in a real game so far.
