# Differences

The aim was the behaviour of JA+ v2.4 Build 7. This file lists where the port is knowingly different, and where it
changes the behaviour of OpenJK outside of the JA+ features.

## OpenJK behaviour that was kept

OpenJK fixed things in the base game that the original JA+ (built on Jedi Academy 1.01) does not have. They are
not JA+ features, so the fixes stay.

Movement:

- Attack and "use item" pressed in the same command: OpenJK drops both buttons at the start of the move.
- "Use item" clears the rocket lock.
- No firing flags while a player is in noclip.
- A spawned player has "no ground" for his first move. In the original the ground is entity 0, which slows a
  player who walks backwards in the frame of his spawn by 4 units per second for one step.

Game:

- Session data has its own format. Chat lines carry the number of the speaker as one more argument.
- "vote passed" and "vote failed" name the vote. A vote with an odd number of voters needs a real majority of no
  votes to fail, and a vote nobody voted on fails at once (not polls).
- A dead player or a spectator is told when a command needs him alive.
- Unknown console commands are left to the engine. The original sends them to all players as chat.
- `listip` lists the addresses. `where` prints the position of the player. `callteamvote` outside of a team game
  says why nothing happens. `gc` needs both arguments.
- A spectator who stops following somebody gets a clean player state. A following spectator gets the team overlay
  of the followed player. `follow` and `followcycle` of a playing client respect the pause after a team change.
- Team votes: the new leader has to be on the team.
- In team games a player is tinted in the color of his team when his model has no team skin.
- `SnapVectorTowards` rounds down or up. The original cuts the decimals off, which is one unit off for negative
  numbers, and snaps a hook on a player twice. The anchor of a hook can be 1 to 2 units away from where the original
  puts it.
- The word filter also checks the last word of `jp_wordFilterBan`. The original only checks words that are
  followed by a space.
- The list of admin banned addresses has 1024 entries, the original 64.
- Holocron, Jedi Master and CTY can be played. The original prints "This gametype is not supported." and plays
  FFA.
- During the intermission commands are answered one by one. The original turns everything that is not in its
  list into a chat line.
- `fallSound1` to `fallSound3` in saber files are skipped without the "unknown keyword" warning. They are single
  player keywords the JA+ saber pack sets.
- Texts with a typed `\n` (MOTD, welcome, admin messages) are converted in place. The original takes memory from
  the level pool for every such message.

## OpenJK behaviour that was changed

Set back to the original because the client predicts with it or because it changes the game:

- `g_fixRunWalkAnims`, `g_fixWeaponAttackAnim`, `g_fixSaberMoveData` and `g_fixSaberDisarmBonus` are 0 by default.
- Force regeneration gives one point when the time is over and then waits `g_forceRegenTime` from that frame.
  OpenJK regenerates independent of the server frame rate, which is faster at 20 frames per second.
- The sabers of a player are set at his first spawn, not at the connect.
- The spawn point is picked with the `random()` of the base game.
- The 5 second pause after the `team` command starts with every `team` command, also when the team did not
  change. With highlander a waiting player stays a spectator after `team f` and could join again and again
  otherwise.

A bug of OpenJK that was fixed:

- `VectorCompare2` (`shared/qcommon/q_math.c`) had a plus where a minus belongs and never found two vectors equal.
  With single player saber damage a resting blade was traced at five points instead of from base to tip, so it
  did not hit walls and clashed differently. This file is shared with the engine.

## Additions that are not in the original

- `jp_cinfo` bit 18 (`JPF_SPFORCES`): tells the client that `jp_allowSPForces` is on, so it can predict the single
  player force animations.
- `eFlags` bit 18 (`EF_PASS_PLAYERS`): set while a player walks through other players (ghost, 3 seconds after a
  duel). The client of this tree predicts the move with it. The original plugin has no rule for these cases and is
  corrected by the server. An original plugin on this server ignores the bit.
- A model without the tags `*back`, `*hip_l` and `*hip_r` is loaded and simply has no saber on the body. The plugin
  refuses such a model.

## Where the original itself depends on the platform

The Linux build of the original computes with the x87 FPU in 80 bit precision. A build with SSE rounds differently
in a few places (the Windows build of the original probably too, that was not checked). The port does not copy
these:

- Products that are cut to an integer: the charged pistol (10 * charge * 1.7) does 50 and 84 damage on the Linux
  server of the original, 51 and 85 here.
- Comparisons on their threshold: the standing animation when the speed is exactly 5, the choice between two
  targets or two spawn points at exactly the same distance, the side of a saber block when the hit comes exactly
  from the front.
- Directions of vectors that are zero up to a rounding error: force lightning and the flamethrower report the
  centre of the target as the impact point, so the hit location is decided by rounding. On the Linux server of
  the original a tick of 1 or 2 damage can lose a point that way. Here such a hit counts as a body hit.
- Random numbers: the original uses the `rand()` of the C library for `random()` and `crandom()`, OpenJK its own
  generator in many of these places. The sequence of random results is therefore not the same.
