# How the port was made

## What there was to work from

- `jampgamei386.so`, JA+ v2.4 Build 7, 32 bit Linux, built with the Intel compiler. Not stripped: every function and
  every global has its name.
- `cgamex86.dll` and `uix86.dll` from `clientPlugin_v1.4B4.pk3`, 32 bit Windows, stripped. No names at all.
- The documentation that ships with JA+ (cvar and command lists) and the example `server.cfg`.
- The source of the base game, which OpenJK is. JA+ was built on the Jedi Academy 1.01 SDK, so most of each binary
  is code that already exists here in a slightly newer form.

The decompiles, the helper scripts and the comparison harness described below are not part of this repository.

## Server module

1. Ghidra (headless) decompiled the whole module into one C file and one file per function.
2. A script gave the data symbols and the `jp_` cvars their names in the decompile, and replaced the common pointer
   arithmetic with readable forms (client of an entity, entity number of a pointer).
3. Structure offsets were matched to the OpenJK structs with pahole tables of a 32 bit build:
   - `playerState_t` is identical (0 to 0x610).
   - `gentity_t` is identical up to 0x5f0, JA+ has a stride of 0x600.
   - `gclient_t` has a stride of 0x2a34. JA+ has no `netname_nocolor`, extends `pers` after 0x6ac and keeps the admin
     level in `sess`. Everything after that is shifted.
4. Every function of the original was put into one of three groups: unchanged base code, base code with JA+
   changes, new JA+ code. A list of all functions without a counterpart in OpenJK, a list of all cvars, a list of
   all strings and a fingerprint comparison (JA+ against a build of the plain base game and against the port)
   were used to make sure nothing was skipped.
5. New JA+ code went into the `g_japlus_*.c` files. Changes inside base functions were made in place and marked
   with a `JA+` comment. Per client state went into one struct, see [server.md](server.md).
6. The result was checked against the original with the comparison harness, see below.

Things that had to be learned the hard way:

- Float constants in the decompile are not reliable. They were read from the disassembly and the data section.
- Every conversion from float to int in the original cuts the decimals off. The decompiler shows `ROUND`. Products
  like `damage * 0.25` are computed in 80 bit x87 precision before the cut, which gives a different integer than
  SSE in a few cases. Where JA+ code depends on it the port computes in `long double`.
- The original uses the entity index for everything it sends to a client and almost never `s.number`. For a
  spectator who follows somebody `s.number` is the number of the followed player.
- Many base functions were changed by JA+ in one line only (an extra condition for friendly fire, for ghosts, for
  the other dimension). They do not show up in a list of new functions. They were found by fingerprints and by the
  side by side games.

## Client plugin

There are no names in the two DLLs, but there are two other binaries to compare them with:

- the unmodified `cgamex86.dll` and `uix86.dll` of Jedi Academy 1.01 (in `assets3.pk3` of the game), built with the
  same compiler as the plugin,
- the Mac build of the plugin (`clientPlugin_mac.pk3`, 2007), which still has its debug information: function names
  with source file and line, globals, struct layouts. It is four years older than 1.4 Build 4 and was only used as a
  dictionary, never as a reference for the behaviour.

1. Ghidra (headless) exported for every function of all six binaries its calls, strings, constants, struct offsets,
   data references and hashes of the instructions.
2. A matcher paired the functions of the plugin with those of 1.01 and of the Mac build (identical code first, then
   unique strings and constants, the call graph, and the order of the object files, which is alphabetical in the
   DLL). Every function of the plugin got a status: same as 1.01, same instructions with other numbers, changed, new.
   cgame: 1664 functions, 427 changed and 78 new. ui: 1579 functions, 229 changed and 627 new, most of the new ones
   being libcurl and the C runtime.
3. Globals were named by voting: two functions with the same name use the same globals. That gave `cg`, `cgs`, the
   cvars and the client info array with their member names.
4. For every changed function the decompiled C of 1.01 and of the plugin were compared line by line, with the
   addresses of 1.01 translated to those of the plugin and struct members that moved put back. Both DLLs come from
   the same compiler, so unchanged code decompiles to the same text and what remains is the change JA+ made, plus
   noise where the compiler inlined or ordered differently. The noise was checked in the disassembly.
5. Every difference was read against the port, file by file. Missing things were built, differences fixed. The
   plugin adds members to `clientInfo_t` (0x174c bytes instead of 0x1710) and `centity_t` (0x77c instead of 0x774),
   the port keeps the same information in its own members.

## Comparison harness for the server module

The original server module and a 32 bit build of the port are loaded into one process. A small stand in for the
engine answers their system calls: cvars, config strings, a box world with traces, entity linking, file access to
the real game data, a simple Ghoul2 with bolts at fixed places. Nothing of the game is started.

- Pmove: random player states and commands go through `Pmove` of both modules, the resulting player states are
  compared field by field. 2.2 million steps.
- Animations and sabers: all animations and the saber definitions of both modules are compared after loading.
- Games: random games with 2 to 4 players. Each player gets random buttons, movement and commands (admin commands,
  emotes, votes, team changes, chat, duels). After every frame everything both modules sent (server commands,
  config strings, cvars, log lines) and the complete state of all players and entities is compared. After a
  difference the state of the port is set to that of the original so that one difference is counted once.
- Scripts: fixed command sequences for admin commands, emotes, the hook, saber throws and a private duel.

Every kind of difference was traced to its cause, in most cases with a debugger on both modules in the same
process. The causes are either fixed or listed in [differences.md](differences.md).

## Comparison of the client modules

The original cgame and ui are Windows DLLs for the old module interface. They and the Windows modules of the port
run in the same engine: a 32 bit Windows build of the OpenJK client (mingw) under Wine. OpenJK still loads modules
with the old interface, so the original DLLs work in it unchanged.

- Input: a demo. Played as a timedemo the engine gives the module every snapshot, config string and server command
  of the recording, one frame per snapshot. Both modules get the same game at the same times.
- Output: a renderer that loads the real renderer, passes every call on and writes it to a file: the models of a
  scene with their Ghoul2 parts, animations and the angles set on bones, sabers and other beams, lights, the
  camera, 2D pictures, text, and what gets registered.
- The two files are compared frame by frame and by category. Things that are random are compared loosely or left
  out (particles, blinking eyes, the strength of a shield flash, camera shake). The same module played twice gives
  the measure for that.
- The demos are recorded on a test server from scripted clients: a recorder, actors that run lists of console
  commands in front of it (saber moves, force powers, taunts, emotes, hook, guns, a duel, the alternate dimension)
  and admin commands sent to the server at given times.

Not covered this way: sounds, the commands and cvars a module sets, and the prediction, which does not run in a
demo. The movement code behind the prediction is shared with the server module and compared there.

Pictures of menus and scenes were taken with both module sets the same way (console scripts, engine screenshots)
and looked at side by side.
