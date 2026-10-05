# Verification

State of 2026-10-05.

## Compared against the original server module

The method is in [porting-method.md](porting-method.md). Results:

| Comparison | Size | Result |
|------------|------|--------|
| Pmove | 2.2 million random steps | 1 step differs, by float rounding |
| Animations | 1446 with frames | equal |
| Saber definitions | 45 | equal |
| Scripts: admin commands, emotes, hook (2), saber throw, private duel | 6 | equal |
| Random games, seeds 0 to 600, mixed FFA, team and CTF | 601 games of 400 frames | about 480 without any difference, the rest only the platform effects of [differences.md](differences.md), the hook anchor and the vote rule of OpenJK |
| The same with spectators, with duel isolation, dimensions, hook and friendly fire switched on, team games with friendly fire | several hundred more games | as above |

Not covered by these comparisons:

- the duel gametypes after the start (OpenJK has its own tournament code), Holocron, Jedi Master, siege,
- everything that needs map entities: items, doors, movers. The test world is a box,
- the two force grabs, pushed items, removal of explosives (no scripts yet),
- the second half of `CheckSaberDamage` (block, parry, knock away) and the functions of `w_force.c` without JA+
  changes were not read line by line. The fingerprints and the games show no difference there.

Every new batch of random games found something until the last ones. More seeds, longer games and more
combinations of settings are worth running.

## Found by the comparison and fixed

A selection, to show what kind of errors the method finds:

- answers of commands went to the followed player when a following spectator typed them,
- ghosts and players of a finished duel did not walk through players,
- a lit player did not burn on, and the burning flag was never cleared,
- force push and pull skipped team mates with friendly fire on,
- `/kill` did not give the frag to the last attacker,
- the rope of the hook started at the torso instead of the right hand,
- a player could take the name of another player with a higher number,
- the team command did not always start its pause, `ampoll` showed its text only to players without the plugin,
  `amForceAltDim` did not skip players in a duel or a grab,
- saber damage of several special moves, three products that are cut off and not rounded,
- the `VectorCompare2` bug of OpenJK.

## Compared against the original client plugin

The method is in [porting-method.md](porting-method.md): the same demo is played with the original cgame and ui
and with the modules of this tree, and what both hand to the renderer is compared frame by frame.

| Scene (recorded on a test server from scripted clients) | Frames | Result |
|---------|--------|--------|
| two players show saber moves, force powers, taunts, emotes, hook, guns, jetpack and cloak, admin commands on them | 1900 | equal |
| the recording player himself does the same, dies and comes back | 2600 | equal |
| four bots fight, the recording player dies | 1350 | equal |
| a private duel next to a third player, ended by a death | 860 | equal |
| a spectator follows a player | 1530 | equal |
| players change between the dimensions | 960 | equal |

"Equal" is after the fixes below and without the differences between OpenJK and the 1.01 game code that are
listed in [differences.md](differences.md). Compared are the camera, every model with its Ghoul2 parts, animations
and bone angles, saber blades and other beams, lights, HUD pictures and texts.

Found this way and fixed:

- while the local player duels, the other players were faded to `jp_DuelAlpha` instead of away,
- the holster model of another player was shown at once (the plugin loads it with the deferred player models) and
  sat at another place among the models of the player, where the base game removes it at a weapon change,
- the camera turned one step too far at the end of the rotation after a won duel,
- a player hanging on a ledge bent his body with the view, which the original (server and plugin) does not. This is
  shared code, it also moved his saber blades on the server,
- the list of heads in the player menu had another order than with the plugin pk3, so a new player got another
  default model (the pk3 of the client assets needs the folder entries of the original),
- `amorigin` on the server console crashed the server.

Not covered by this comparison:

- sounds, and the commands and cvars the modules set,
- the prediction of the own movement (a demo is not predicted),
- the menus beyond the three that were looked at as pictures (player, saber, moves), key handling in menus,
- team games, CTF (flag shell), siege, vehicles, the duel gametypes,
- other maps than `mp/ffa3`, and everything a scripted client does not do: aimed attacks, saber locks, grabs and
  kisses with a partner, the flamethrower hitting somebody.

## Seen in the game

On a local test server (64 bit Linux, dedicated) with a 64 bit client of this tree:

- the server module ran under valgrind with bots and all rcon admin commands without a memory error,
- a client joins, is detected as plugin 1.4, gets welcome text and MOTD,
- RGB saber, alternate dimension, hook and flip kick,
- sabers on the body: a saber with `idleInBack` hangs across the back, two sabers hang on the hips while the fists
  are out, also after the saber was changed with `/saber`. Looked at with engine screenshots of a second client,
- walking through players of the other dimension and through players as a ghost is predicted without errors
  (reading `jp_cinfo` from the server info, `EF_PASS_PLAYERS`).

With the 32 bit Windows modules (cgame and ui) in a Windows build of the engine under Wine, next to the original
plugin in the same engine: the player menu with head and holster list, the saber menu with an RGB blade, the moves
menu, the HUD and the scoreboard look the same in engine screenshots.

## Never seen in the game

- Flamethrower hits, ledge grab by a human player, the two force grabs.
- Walking through players in the 3 seconds after a duel (the other use of `EF_PASS_PLAYERS`).
- The 32 bit Linux modules and the Windows server module were only built, never loaded into a game.
- A client with the original plugin on this server in a real game (only in the scripted test scenes), and this
  client on an original JA+ server.
- The menus of the ui were never used with mouse and keys after the changes of the client work: the holster list,
  the color sliders, the head list with its preview and the seventh title of the moves menu were only drawn, not
  clicked.

## Open

- The items under "Not done" in [server.md](server.md) and [client.md](client.md).
- The leg angle of a player differs from the original by half a degree for one frame when he slows down. Base
  game code, not looked into.
- The inline uses of `ent->s.number` in JA+ changes inside base files (traces and duel checks of living players)
  were left as they are. The new files use the entity index.
