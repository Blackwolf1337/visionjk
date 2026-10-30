# JA+ v2.4 Build 7 in VisionJK

This tree, VisionJK, is OpenJK (multiplayer) with the features of the JA+ mod for Jedi Academy built in. The source of JA+
was never released. Everything here was rebuilt from the released binaries:

- `jampgamei386.so` of JA+ v2.4 Build 7, the Linux server module. It still has its symbol names.
- `clientPlugin_v1.4B4.pk3`, the client plugin: `cgamex86.dll` and `uix86.dll` (stripped) plus menus, effects and
  graphics.

The port starts from OpenJK commit 260c59c2 (2026-09-29) and is the commits on top of that history.

## Names

Since 2026-10-17 every cvar and command this tree added begins with `v_` (the client: cgame, ui, engine client,
sound, renderer, and the console commands of the client) or `vs_` (the server: game module, dedicated server,
masterserver, its console commands, and the commands client and server send each other). Stock OpenJK and JA+
names keep theirs. `codemp/qcommon/cvar_renames.h` lists every old name with its new one. A value stored or set
under an old name, and a bind with an old command, is taken over by the engine with a line on the console, so
a config with the old names keeps working and says what to change. The commands between client and server are
renamed too: a client or server of 0.1.7 or before does not understand them, so voice chat, statuses, boombox
and the key login only work between versions from after the change.

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
| [loading.md](loading.md) | What the load of a level and a long frame are spent on: the two reports of the engine, the test, the numbers, the packed textures that are kept |
| [ui.md](ui.md) | The new look of menus and HUD (not a JA+ feature): what a player sees, what a mod's files get, the new words of the menu files, how the menus are made |
| [windows.md](windows.md) | The Windows client run as a whole on Windows 11: what ran, what it found, what is still unchecked |
| [voice.md](voice.md) | Voice chat with the microphone (not a JA+ feature): in the cgame, relayed by the game module, engine unchanged, clients without it unaffected |
| [boombox.md](boombox.md) | Boxes that play songs of YouTube (not a JA+ feature): fetched by the server, sent over the relay of the voice chat, a sound of the level with `v_spatial`, only VisionJK players see and hear them. Never run |
| [audio.md](audio.md) | Sound that knows the level (not a JA+ feature): `v_spatial`, with Steam Audio in the engine. Walls muffle, rooms ring, sounds come round corners. Measured, not listened to yet |
| [renderer.md](renderer.md) | rd-vulkan-vision, a Vulkan renderer next to rd-vanilla (not a JA+ feature): how it is built, the state of each part, what runs on the GPU, differences, what is left |
| [downloads.md](downloads.md) | Downloads from a server (not a JA+ feature): a window to choose the paks, kept or for one session, what is in them before they are used, programs blocked |
| [web.md](web.md) | The client in a browser (not a JA+ feature): built as WebAssembly, handed out by the dedicated server itself (`vs_clientWebPort`), packets through a WebSocket. What runs and what never ran |
| [testing.md](testing.md) | The test scripts of `tools/test/`: smoke test of the zips, sanitizers and valgrind, unit tests, static checks, other games, the environment variables, the release flow |

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
| `codemp/game/webadmin/` (`g_webadmin.c`, `wa_*.c`, `page/`) | web admin panel from JKAdmin, see [webadmin.md](webadmin.md) |
| `codemp/game/props/g_blackhole.c` | the entity `misc_blackhole`, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/fun/g_schizo.c` | hallucinations only one player gets, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/props/g_props.c` | the entities `misc_ball`, `misc_cube` and `misc_prop` (any model of the game) with physics, taking them with `+button13` (admins take players with it too), not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/fun/g_poop.c` | the emote `ampoop`, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/fun/g_flatulence.c` | a player who breaks wind all the time, not from JA+, see [webadmin.md](webadmin.md) |
| `codemp/game/fun/g_disorient.c` | a player whose view sways and is thrown around, not from JA+, see [webadmin.md](webadmin.md) |

Changes inside base files carry a comment that starts with `JA+`. The largest ones are in `w_saber.c` (damage
rules, thrown saber, kicks), `bg_pmove.c` and `bg_saber.c` (new moves, ledge grab, hook pull, freeze), `w_force.c`
(grabs, flamethrower, absorb, team rules), `g_cmds.c` (duels, votes, say, team), `g_client.c` (connect, spawn,
names) and `cg_players.c` (sabers on the body, effects, fading of other players). `git show --stat 37c22ac4` lists
all 70 files.

## Building

How to build the tree, the Windows cross build, the release folder (`cmake --install`) and the assets pk3 are in
[BUILDING.md](../../BUILDING.md). The three modules are `jampgame`, `cgame` and `ui`.

## Files needed at run time

All of them are part of this repository.

| File | Where | Needed for |
|------|-------|-----------|
| `modelscale.cfg` | `japlus/`, from `assets/configs` | `jp_allowModelScale` |
| `<map>_holes.cfg` | `japlus/`, from `assets/configs` | blocking walls of a map as JA+ kept them, read when the map has no `wallblocks/<map>.cfg` |
| `wallblocks/<map>.cfg` | the server folder, written by the tab Clips of the web panel | the blocking walls of a map and the clip brushes of it that are off |
| `autoreplier.cfg` | `japlus/` on the client, from `assets/configs` | auto replier texts |
| `server.cfg`, `admin.cfg`, `jk2.cfg`, `vstr.cfg`, `chat.cfg`, `saber.cfg` | `japlus/`, from `assets/configs` | the example server config of JA+ and what it loads. Nothing reads them unless a server is started with `+exec server.cfg` |
| `zz_japlus_client_assets.pk3` | `japlus/` on the server and on the client | menus, shaders, effects and graphics of the plugin, with the menus of the port, the JA+ sabers and effects, and the new animations (`models/players/_humanoid`). Without them `jp_newGLAAnims` is switched off at map start |

The last file is built with the tree from `assets/japlus_client` (see [BUILDING.md](../../BUILDING.md)), which holds the contents of
`clientPlugin_v1.4B4.pk3`, `japlus_sabers.pk3`, `efx_MOD.pk3` and `japlus_gla_anims.pk3` of JA+ 2.4 Build 7, without the two DLLs of the
plugin and without the `Thumbs.db` files. It replaces those four: a folder that has it does not need them. It is packed with the folder entries of the original pk3 and in
their order: the engine lists a folder of a pk3 only when it has an entry of its own, and the head list of the
player menu, and with it the model a new player starts with, follows that order.

## State

The server module was compared against the original function by function and in about 700 random games that both
modules played side by side. For the client every function of the plugin DLLs was compared with the unmodified
1.01 modules to find what the plugin changed, and what the cgame and ui draw was compared with the original plugin
on recorded test scenes. Both comparisons, what they found and what they do not cover are in
[verification.md](verification.md). Little of the client was used by a person in a real game so far.
