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

There are no names in the two DLLs, so the plugin code was located by what it touches:

- strings (cvar names, command names, effect and shader paths, bone and tag names),
- the handles it registers at start (effects, shaders, sounds) and where they are used,
- the bits of `cp_pluginDisable`, each of which guards one feature,
- the server info keys it parses (`jp_cinfo`, `jp_DuelAlpha`, `V`).

Base functions were recognised by their strings and their structure and given the names they have in OpenJK. The
fields the plugin adds to `clientInfo_t` and `centity_t` were mapped by their offsets. The features were then
written into the OpenJK cgame by behaviour. Plugin code that has no string, handle or switch of its own could not be
told apart from base code and was not compared.

## Comparison harness

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
