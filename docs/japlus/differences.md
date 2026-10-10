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

Client (seen when the cgame and ui are compared with the original plugin on the same recorded game, see
[verification.md](verification.md)):

- `cg_drawCrosshair 1` is the first crosshair picture. 1.01 and the plugin count from 0 and show the second one.
- The name of the player under the crosshair stays for a second and fades out. 1.01 drops it as soon as the
  crosshair is on something else.
- A center print is placed a little higher when its text is long: OpenJK also counts a line for every 50 characters
  when it centers the block.
- The number of shots in the HUD has the color of the menu item (orange) and changes it while the weapon fires or
  is empty. 1.01 draws it in white.
- The scoreboard writes `BOT` in the ping column of a bot.
- `cg_thirdPersonRange` and `cg_thirdPersonVertOffset` are no cheat cvars.
- The server browser filters by the installed mod folders, so `japlus` is one of the entries. The plugin has a
  fixed third entry "JA+ Mod".
- The ui keeps up to 1024 heads and 256 saber hilts (plugin: 768 and 192).
- In team games the HUD is not tinted in the color of the team. OpenJK took that out, 1.01 and the plugin draw
  the frames and ticks in light red or light blue.
- Static models of a map (`misc_model_static`, for example the flag stands of `mp/ctf3`) are only added to the
  scene when they are in the view, and cast no stencil shadow.
- The own player is not drawn while he is a spectator. 1.01 and the plugin show him for the first frames after a
  join as spectator, until the second snapshot arrives.
- In the menus: the mouse cursor is 40 pixels wide (1.01: 48), the scrollbar of the head list sits 2 pixels further
  left, the demo list shows the file names with their ending, and a list item whose cvar has a value outside of its
  list reads "Custom" (1.01: empty).

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
- A giant shakes the ground (`g_stomp`, on by default, `codemp/game/fun/g_stomp.c`): a player the server made
  more than three times his size (model scale above 300) is felt by who is near him. With every step, where he
  pushes off for a jump and where he lands, the view shakes, a heavy step is heard (the rancor's) and a ring of
  dust comes up. It grows with his size up to four times, a landing is stronger than a step and a fall stronger
  than a landing, and it fades with the distance. It is done by the server with events every client of the game
  knows (the screen shake, sent to each player near enough, a sound and an effect at the spot), so a client
  without the cgame of this tree gets it. What such a client can be made to do is limited: the screen shake of
  the game only moves the place of the view, by 16 units at most, and does not turn it (the client changes the
  angles after it made the axis of the view from them). That is plain for somebody who stands next to the giant
  and little for the giant himself, whose camera is far out. So the event also says by how many degrees the view
  is to be turned about, which the cgame of this tree does (up to 3 degrees, half of it for the giant's own
  view). The server does not know when a foot touches the ground, only a client plays the animation: a step is
  where the movement code's cycle of steps turns over, which keeps the pace of the legs and not the very moment.
  `cg_stomp 0` leaves the shake out for a player with this cgame (sound and dust stay), `g_stomp 2` and
  `cg_stomp 2` say each one on the console, of the server and of the client. On a live server the events of a
  bot of scale 500 were seen to arrive at a client in a browser, with units and degrees. What it looks like was
  not seen from here.


## What of the original was taken out

- The commands of the blocking walls: BlockWallCreate, BlockWallDel, BlockWallNext, BlockWallPrev (players with
  editor rights), BlockWallDraw, BlockWallWrite and BlockWallGrantEditor (rcon). The user on 2026-10-09: the web
  panel drags the walls, the commands are not needed. The walls themselves are there and are predicted by the
  clients now, see `server.md`. The client still knows none of these names for its tab completion.

## Where the client is knowingly not the plugin

- No download over an URL, see [client.md](client.md).
- The plugin shows the highlander rules on the loading screen of every server, the port only when the server has
  the key `jp_highlander`.
- The plugin applies its rule for the pistol hook also to the bryar shots of servers without JA+. The port only on
  JA+ servers.
- Prediction of walking through players follows the rules of the server. The plugin predicts wrongly with
  `jp_DuelAlpha -1` and `jp_altDim` on, and has no rule for ghosts.
- The player menu of the plugin stops the game with an error when the menu has no model item named `character`.
  The port does nothing then, so the menus of the base game keep working.
- The macro scan of the plugin follows `exec` and `vstr` without a limit, a file that executes itself ends in a
  memory error. The port follows 4 levels and reads 64 KB of a file.
- The plugin keeps a copy of the saber hilts of every player in his client info to draw them on the body. The port
  keeps them per player number.

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
