# Verification

State of 2026-10-05, commit 37c22ac4.

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

## Seen in the game

On a local test server (64 bit Linux, dedicated) with a 64 bit client of this tree:

- the server module ran under valgrind with bots and all rcon admin commands without a memory error,
- a client joins, is detected as plugin 1.4, gets welcome text and MOTD,
- RGB saber, alternate dimension, hook and flip kick,
- sabers on the body: a saber with `idleInBack` hangs across the back, two sabers hang on the hips while the fists
  are out, also after the saber was changed with `/saber`. Looked at with engine screenshots of a second client.

## Never seen in the game

- Flamethrower, jetpack, ledge grab, the new saber moves, the two force grabs.
- Of the client: the holster model, the draw animations of sabers on the back, staffs on the back, the duel end
  camera, the fire and force effects, the fading of other duels.
- The prediction changes (reading `jp_cinfo` from the server info, `EF_PASS_PLAYERS`) were made after a report of
  prediction errors in the alternate dimension and as a ghost. They were not measured.
- The 32 bit Linux and the Windows modules were only built, never loaded into a game.
- A client with the original plugin on this server, and this client on an original JA+ server.

## Open

- The items under "Not done" in [server.md](server.md) and [client.md](client.md).
- The leg angle of a player differs from the original by half a degree for one frame when he slows down. Base
  game code, not looked into.
- The inline uses of `ent->s.number` in JA+ changes inside base files (traces and duel checks of living players)
  were left as they are. The new files use the entity index.
