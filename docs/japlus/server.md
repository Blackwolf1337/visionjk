# Server: the game module

## State and settings

- Everything JA+ keeps per player is in `jpClient_t` (`g_japlus.h`), a member of `clientPersistant_t` (`client->pers.jp`).
  It survives a respawn. Fields the original keeps outside of `pers` are cleared at the spawn in
  `JP_ClientSpawnReset`.
- The admin rank is `client->sess.jpAdminLevel`. It is part of the session string, so it survives a map change and
  a map restart, not a reconnect.
- The 187 `jp_` cvars are in `g_japlus_xcvar.h` with the names, defaults and flags of the original.
- `gamename` is `JA+ Mod v2.4 B7` and the server info has `V` = `2.4B7`. The client plugin and server browsers
  recognise a JA+ server by these.
- New entity fields: `jpSpawnOrigin` and `jpSpawnSet` (items that were pushed go back to their place).

## What the server tells the client

| Channel | Content |
|---------|---------|
| server info `jp_cinfo` | bit field of the settings the client needs to predict moves, see below. Built every frame in `g_japlus_misc.c` |
| server info `jp_DuelAlpha`, `jp_gripSpeedScale`, `jp_forceCombo` | read by the cgame |
| server info `vs_caps` | not from JA+. What this game module does for the clients of this tree: bit 0 chat in UTF-8 per recipient (`JP_ChatFor()`), bit 1 it passes statuses on, bit 2 the key login, bit 3 it tells who plays with a client of this tree and answers `vs_status all`. Bits 4 to 6 were Focus and the Vision Temple of 0.1.6 and 0.1.7 (removed 2026-10-17) and are not used again. Bit 7 it passes hats and capes on (`tay_look`, 2026-10-17). Read only |
| server command `jpclient <client> <version> [<client> <version> ...]` | not from JA+. With which VisionJK a player plays (`v_vision` of his userinfo, letters, digits and `. - _` only), "-" for any other client. Only sent to a client whose userinfo has `v_caps` bit 1: one player when his userinfo says something new, everybody in one command as the answer to the client command `vs_status all`, which a cgame sends once when it has started (at most every two seconds) and which is also answered with the statuses of everybody. See client.md, "Who plays with VisionJK" |
| server command `tay_look <client> <hat> <cape>` | from TaystJK's cosmetics. What a player wears, "-" for none, from his client command `tay_look <hat> <cape>` (names of letters, digits, `_` and `-`, one every three seconds at most). Only sent to a client whose userinfo has `v_caps` bit 2, also as part of the answer to `vs_status all`. See client.md, "Hats and capes" |
| server command `vs_status <client> <away> "<music>"` | not from JA+. What stands above a player's head, see "Statuses" below. Only sent to a client whose userinfo has `v_caps` bit 0 |
| `eFlags` bit 15 `EF_ALT_DIM` | player is in the alternate dimension |
| `eFlags` bit 16 `EF_GRAPPLE_SUSPEND` | player hangs on the hook |
| `eFlags` bit 17 `EF_FLAME` | flamethrower is on |
| `eFlags` bit 18 `EF_PASS_PLAYERS` | player walks through other players. Not in the original, see [differences.md](differences.md) |
| `eFlags2` bit 8 `EF2_BURNING` | player was lit by the flamethrower |
| `eFlags2` bit 9 `EF2_HOOK` | hook is out |
| `pm_flags` 0x8000 `PMF_GRAPPLE_PULL` | hook pulls the player |
| `ps.lastHitLoc`, `ps.jetpackFuel` | hook point and hook fuel |

The client tells the server its plugin version with the userinfo key `cjp_client` and its choices with
`cp_pluginDisable`, `cp_sbRGB1`, `cp_sbRGB2`, `cp_holster`, `cp_login`, `cp_clanPwd`.

Bits of `jp_cinfo` (`JPF_*` in `bg_public.h`):

| Bit | Setting | Bit | Setting |
|-----|---------|-----|---------|
| 0 | jp_allowflipkick | 10 | jp_macroScan 1 |
| 1 to 3 | jp_fixRoll above 0, 1, 2 | 11 | jp_macroScan, all binds are scanned |
| 4 | jp_improveYellowDFA | 12 | jp_jk2RedDFA |
| 5 | jp_slideOnPlayer | 13 | jp_noKATA |
| 6 | jp_allowSPattacks | 14 | jp_noAutoReplier |
| 7 | jp_allowNewDFA | 15 | jp_newGLAAnims (with 2: only when every player has the plugin) |
| 8 | jp_allowModelScale | 16 | jp_ledgeGrab |
| 9 | jp_allowDmgSpeedScale | 17 | jp_altDim |
| | | 18 | jp_allowSPForces. Not in the original |

## Features

Admin system (`g_japlus_admin.c`, `g_japlus_admin2.c`, `g_japlus_fx.c`, `g_japlus_tele.c`)

- Three ranks with passwords (`jp_councilPass`, `jp_knightPass`, `jp_instructorPass`) and a mask of allowed commands
  per rank (`jp_councilAllowedCMD` and so on). rcon is the fourth rank. `amlogin`, `amlogout`, automatic login with
  the plugin (`cp_login`). Not as in JA+: the passwords only count with `vs_adminPasswords 1`, see "Admin login
  without a password" below.
- The bit of a command in the allowed masks is its position in this list, starting at 1: amkick, amban, amShowMotd,
  amorigin, amslap, amsleep, amwake, amghost, amdenyvote, amprotect, amsilence, amunsilence, amUnForceAltDim,
  amForceAltDim, amempower, ammerc, ammindtrick, then amtele, amstatus, amwhois, ammap, ampsay, npc, amrename,
  amlockteam, amforceteam, amkillvote, amvstr, amweather, amlogout, amtelemark, ampoll, amseeGhost, admCmdPrev,
  admCmdNext, playerPrev, playerNext, admCmdEXE.
- Other admin commands: aminfo, amlogin, ammotd, amsay, amplayefx and amtest (rcon rank only).
- Console commands of the server: admGrant, admToggle, admBan, admAddIP, admRemoveIP, admListIP, freeze, size.
- Anti abuse counter with a timed lock, admin ban list by IP, the admin gun (run the chosen command on the player
  in the crosshair), logging of every admin command.
- Messages of the effects come from the `jp_msg*` cvars. A typed `\n` in them is a line break.

Players (`g_japlus_cmds.c`, `g_japlus_chat.c`, `g_japlus_info.c`)

- 27 emotes: amkneel, amdie, amdie2, amfinishinghim, ampower, amnod, amshake, amhiltThrow1, amhiltThrow2, amcomeon,
  amwon, amhello, sleep, amatease, amsurrender, amsit, amsit2, amsit3, amsit4, ambeg, amwait, amhips, amvictory,
  amneo, ambreakdance, ambreakdance2, amnoisy. Two player emotes amkiss and amhug.
- amflip, amknockmedown, amdropsaber, jetpack, toggleFlame (flamethrower in place of force lightning), amcolor,
  sabercolor, saber (change the saber while playing), amAltDim, immortal, refuseTele.
- ammove, amDMGs, duelWhois, modVersion, serverTime.
- Chat: clanpass, clansay, clanwhois, say_team_mod, ignore, word filter (`jp_wordFilter*`), silence, clan tag
  protection (a name with the clan tag needs the clan password, otherwise it is renamed after warnings), unique
  names, no black names unless allowed.

Movement and combat (changes in `bg_pmove.c`, `bg_saber.c`, `bg_panimate.c`, `w_saber.c`, `w_force.c`, `g_active.c`)

- Grappling hook on button 12 (`g_japlus_hook.c`): hook missile, pull, hanging on the rope, fuel shared with the
  jetpack, sabers and shots cut the rope.
- Alternate dimension and duel isolation (`g_japlus_dim.c`): players in different dimensions, and players in a
  private duel against everybody else (`jp_DuelAlpha` 0 or more), do not block, hit or push each other.
  `G_TempUnlink` builds the list for a player, `JP_Trace` is the trace that leaves those players out,
  `G_canInterferWith` answers for damage, force powers and missiles.
- Ledge grab, the new saber moves and kicks of the JA+ animations, staff spin, jump attacks, flip kick rules, roll
  fixes (`jp_fixRoll` 1 to 3), slide on players.
- JA+ saber damage: the rules of `jp_alterDMG` and `d_saberSPStyleDamage`, the damage cvars of the special moves,
  `jp_SaberThrowScaleDMG`, `jp_reduceSaberBlock`, `jp_allowSaberTouchDMG`.
- Force: `jp_gripSpeedScale`, `jp_forceCombo` (push and pull grabs), `jp_fixAbsorb`, `jp_pushPullKnockDown`,
  `jp_gripOpenSaber`, `jp_allowSPForces`, `jp_improveYellowDFA`, `jp_jk2RedDFA`, `jp_cloakFuel`, `jp_pushAll`. Push
  and pull reach team mates when `g_friendlyFire` is on.
- Flamethrower: damage in a cone, a lit player burns on for 1.6 seconds and loses a point every 265 ms.
- A `/kill` gives the frag to the player who hit or pushed last.

Game modes and rules

- Duels: the JA+ rules of `engageDuel`, full force duels (`engage_FullForceDuel`), sabers stay off at the start, the
  winner message with health and armor left, the winner is healed, 3 seconds of walking through players after
  the end.
- Highlander (`g_japlus_highlander.c`): lives, the powers of the victim go to the killer, final duel with a timer.
- Jedi against mercenaries: the spawn extras of levels 2 and 3, team choice by class.
- Votes: sleep and silence votes, polls, `jp_votesDisable`, vote spam protection, `amdenyvote`.
- Blocking walls (`g_japlus_blockwall.c`): boxes of a map that stop players and let shots through. Not as in
  JA+ in three ways.
  Clients predict them. In JA+ a wall was one entity with SVF_NOCLIENT that no client knew of, so every client,
  the plugin too, walked into it in its prediction and was set back by the server. Here the wall is an entity
  that only says where it is and how big, and parts that do the blocking and are sent to the clients as solid
  boxes (ET_INVISIBLE, `s.solid` packed by the module, contents still CONTENTS_PLAYERCLIP). What a client can be
  told of a box is square in X and Y, 510 wide and 446 high at most, so a wall is cut into parts as wide as its
  short side, 64 at most: a long thin wall gets thicker to stay under that, and each side is rounded up to an
  even number. No engine and no cgame change is needed for it, so the unchanged game, the JA+ plugin and this
  client predict the same boxes. Still not predicted: a spectator and a dead body (their moves ignore bodies on
  the client), and a client shows no crosshair name through a wall.
  The commands are gone (the user, 2026-10-09: the web panel drags them, the commands are not needed):
  BlockWallCreate, BlockWallDel, BlockWallNext, BlockWallPrev, BlockWallDraw, BlockWallWrite and
  BlockWallGrantEditor, with the editor who flew through everything as a spectator. The walls are placed, dragged
  and shaped in the tab Clips of the web admin panel (`docs/japlus/webadmin.md`).
  The file is `wallblocks/<map>.cfg` in the server folder, written by Save in that tab: a wall a line as JA+
  wrote them (two corners, six numbers), and a line `clipsoff` with the clip brushes of the map the server does
  not mind. A map without that file takes `<map>_holes.cfg` of JA+, so the files of the package still work.
- Clip brushes of the map turned off (the engine, not JA+): `clipbrush off|on <number> ...`, see the tab Clips
  in `webadmin.md`. For another client that wants to predict them: the server sends the system info key
  `vs_clipRanges`, the numbers of the brushes in the brush lump of the map file that are off, with a blank between
  them and a run written as `first-last` (`4178-4182 4190`), empty when none is. A client sets the contents of
  those brushes to 0 in its collision map and gives every other clip brush the contents of its shader again,
  whenever the key changes and after it has loaded the map. Only for a brush whose shader has PLAYERCLIP,
  MONSTERCLIP, BOTCLIP or SHOTCLIP and not SOLID. Here that is `CL_ClipOffChanged` (`cl_cgameapi.cpp`) and
  `CM_ApplyClipOff` (`cm_load.cpp`), about forty lines.
- Model scale from `modelscale.cfg` (`g_japlus_scale.c`): model, eye height and saber blades, with
  `jp_allowDmgSpeedScale` also saber damage and speed.
- Server housekeeping: welcome text and MOTD, inactivity protection in FFA (a player without input is protected
  from his spawn on until his first input, and again after `jp_inactivityProtectTimer` seconds), anti fake name list,
  userinfo flood protection, auto quit at a time of day (`jp_autoQUIT`), fake clients, download redirect URL,
  logging of plugin versions and IPs.

### Statuses (not from JA+)

`game/status/g_status.c`, the client part is in [client.md](client.md).

| Cvar | Default | Meaning |
|------|---------|---------|
| `vs_afkTime` | 60 | seconds without a key or a turn after which a player counts as away, 0 never |

A player whose commands did not change for `vs_afkTime` seconds is away until they do. The server finds that
out itself, so it holds for legacy clients too. A client can say what its player listens to with the client
command `vs_status music <text>` and end it with `vs_status none`: at most one every three seconds, 64 bytes,
without colours, quotes and control characters. Both are passed on to every client whose cgame asked for
statuses, and to nobody else, so a legacy client never gets a command it does not know. `vs_status` is not
written to the client command log. Not run.

### The masterserver (not from JA+)

`server/sv_voice.cpp`, `master/ms_relay.c`, `master/ms_boombox.c`. Built 2026-10-12, run hidden on Linux on one
machine (the checks are in [voice.md](voice.md), "Checked"), and on the host of VisionJK since 2026-10-12 21:43,
where it took the place of the voice master, as its relay was called while it was a program of its own.

One program on one UDP port for the players of servers without this game module: the relay of their voice
chat, their clan radio, their statuses and the boxes of their boombox. It is the dedicated server of this tree
built with `-DBuildMasterServer=ON` (target `visionjk-master`, off unless asked for, with no install rule: it is
for the hosts of VisionJK itself and no part of a release). Its relay only runs while a level runs, so it loads
one that nobody plays on (`assets/configs/master.cfg`, one slot):

```
visionjk-master.x86_64 +set dedicated 1 +set net_port 29170 +exec master.cfg +map mp/duel1
```

What comes in on its port:

- a datagram of the voice chat of a player in its own level: the relay of its game module, as on any dedicated
  server of this tree
- any other datagram of the voice chat: its relay for the other servers. `vs_masterKbps` (4000, command line
  only) is what that may send in a second, 0: anything

Limits of the relay:

- Token answers to a state without the right token are at most 10 per address and second and 4000 in all, and
  they no longer count into `vs_masterKbps`. They used to fill the budget of every second, so a flood of made up
  states silenced the relay for everybody.
- The session token is made of the address without the port. A state without the token for a client number whose
  session is alive on another port of the same address is answered "taken", so somebody else behind the same
  router can not take that session over. The client that has the token moves to a new port with it.
- There are 4096 sessions (was 1024), still 16 per address, and one address may be in 4 rooms at most.
- Once half of the budget of a second (`vs_masterKbps`) is used, no room gets more than an equal share of it among
  the rooms that were sent anything in that second.
- A client may do something with a box other than ask for a song (place, move, volume, seek, skip, pause, stop,
  remove) once a second at most, and change the box it watches four times a second. The line of a box is sent
  only when the watched box changes.
- The engine of the masterserver registers `vs_boomboxYtdlp`, `vs_boomboxFfmpeg` and `vs_boomboxYtdlpArgs` as
  `CVAR_INIT` itself, so it does not depend on the game module for that.

29170 is the port the clients of 0.1.5 know for the voice chat on servers without the module
(`v_voiceMasterHost`: `voice.jka.vision` up to 0.1.6, `master.jka.vision` after it, the same host). Until
2026-10-17 it was also the Vision Temple as a game server (`temple.cfg`, map `vision_temple`), which is
removed. Not run with master.cfg yet.

### Lag compensation (from TaystJK's jaPRO)

`game/unlagged/g_unlagged.c`, built 2026-10-19. jaPRO has it as `g_unlagged`, which goes back to the Unlagged mod
of Neil Toronto for Quake 3. A client draws the others where its snapshots put them, at the time its commands
carry (`usercmd_t.serverTime`); its shot reaches the server later by its ping. The server keeps where every client
was at the end of each of its last 32 frames (box, origin, the yaw the ghoul2 trace turns the model by), moves the
others back to the shooter's time for the shot and puts them back afterwards. Nothing goes over the network, so
stock and old clients get it too. Bots are not shifted for, sabers neither (as in jaPRO). The pose of a ghoul2
model is the one of the present: the server poses it at `sv.time` for every trace.

| Cvar | Default | |
|------|---------|-|
| `tay_unlagged` | `7` | bits: 1 hitscan (disruptor, concussion alt fire, stun baton, melee), 2 projectiles (a missile starts as far along as the shooter's lag, as in jaPRO, its first frame traces that stretch), 4 force push and pull |
| `tay_unlaggedMax` | `200` | at most this many ms back |

With `developer 1` every shot that shifted the others prints a line: how many ms back, how many players, how far
the farthest moved, whether the shooter's hit count rose.

Checked 2026-10-19 with `agent/japlus_port/tools/lagtest.sh`: a client 100 ms behind a UDP relay on the same
machine (`lagproxy.py`) shoots the disruptor twenty times and pushes and pulls while a bot runs about on mp/ffa3.
Every shot shifted the bot back by 115 to 141 ms (the relay and the client's interpolation) and by about 30 units,
what it runs in that time; with `tay_unlagged 0` nothing was shifted. Not checked: that a shot aimed at what the
client sees hits (the script can not aim), the projectiles (their start is not printed), real ping over the
internet.

### Admin login without a password (not from JA+)

`game/auth/g_auth.c` and `game/auth/bg_auth.c`, the client part is in [client.md](client.md). Since 2026-10-11.

| Cvar | Default | Meaning |
|------|---------|---------|
| `vs_adminPasswords` | 0 | 1: the three rank passwords count again (`amlogin <password>`, `cp_login` of a plugin) |
| `vs_authHosts` | empty | the addresses the server is reached by, `"1.2.3.4:29070 192.168.0.5:29070"`. A key login signed for another address is refused |

A rank password is the same for everybody of the rank, it is typed into a console that logs, the JA+ plugin
sends it to every server it joins in the userinfo, and whoever has seen it once has it for good. So the
passwords are off unless the owner switches them on, and an admin logs in one of two ways:

- With a key. The file `admin_keys.txt` in the game folder has a line for each admin:

  ```
  # rank       what "v_amkey show" of the client prints, or the line of a .pub file of ssh-keygen
  council      ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... emir
  rcon         ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... owner
  ```

  The rank is `council`, `knight`, `instructor` or `rcon` (every right). The client says which key it has
  (`vs_auth hello`), gets 32 random bytes that are good for one answer and 30 seconds, and sends a signature
  over them (`vs_auth sign`). The signed text starts with a fixed line, so the signature is worth nothing
  anywhere else, and it has the address of the server as the client sees it and the number of the client in
  it. A wrong signature counts as a wrong try: three of them and the player waits, as with wrong passwords.
  Who has the rank of his key already (it is kept over a change of the map) is not asked or announced again.
  `amkeys` on the console of the server reads the file again and lists it, it is also read with every map.
- With a one-time code, for admins whose client can not sign (jamp.exe, the JA+ plugin): `amcode <rank>` on
  the console of the server, or the web panel, makes a code of eight letters and digits. It is typed as
  `amlogin <code>` within a minute and works once.

`vs_caps` has bit 2 for the key login, a client only offers it where it reads the bit. Set `vs_authHosts`
on a public server: without it the server can not tell a signature that was made for another server and
passed on.

Tested: `japlus_port/tools/authtest/run.sh` (66 checks: the three test vectors of RFC 8032 through the
Monocypher of the tree, the signed message, the key lines, key files in both directions against `ssh-keygen`,
and the agent protocol against an `ssh-agent` started for the test). The logins themselves ran against a server
on 2026-10-11, see client.md.

### Training bot (not from JA+)

`game/trainer/g_trainer.c`.

| Cvar | Default | Meaning |
|------|---------|---------|
| `vs_allowTrainer` | 0 | how many training bots there may be at once, 0 turns `vs_trainer` off. Needs `bot_enable 1` and free for all (`g_gametype 0`) |

The player command `vs_trainer [1-5|off]` calls a training bot that fights only the player who called it. Without a
number it is level 3. With a number while the bot exists, that changes its level. `off` sends it away.

The bot fights like flip kick players, and moves like the movement log of one (15 kicks in 83 seconds). From 300 to
1200 units it pulls itself in with the JA+ grappling hook (needs `jp_allowHook 1`) and lets go at 200 units. Up close
it uses the short pull of the log: it jumps, fires the hook in the air 105 to 170 units off the player and lets go
after about 60 ms on the rope or at 105 units. It lets go with use, as a bot counts as a client with the JA+ plugin
and would hang on the rope otherwise. The hook is not used in the alternate dimension of JA+, it does not work
there.

It grabs with melee (needs `g_debugMelee`), flip kicks (needs `jp_allowflipkick`), or swings from above in the red
style. The forward kick starts on the ground within 100 units: the jump is held 100 ms, let go for 50 ms and pressed
again, and that press is the kick. If the player is still out of reach it lets go and presses again while it still
rises fast enough. During the flip back the hook pulls it in again for the next kick, from up to 230 units, as long
as the kicks so far are at most half its level: one kick at level 1, two in a row at levels 2 and 3, three at levels
4 and 5. After a kick that knocks the player down it follows with the red swing.

The side kick: both on the ground at about the same height (within 16 units) and within 140 units, it turns so the
player is at its side, strafes into him and jumps at 40 units. The wall flip off a player in PM_CheckJump kicks him.

It reads the player: while he attacks, or has the attack button down, it does not hook, grab or kick, it backs off
and blocks. It counters his hook (it swings, which can cut the rope, and moves sideways), his flip kick (it swings
in the red style, which keeps it from being knocked down, or crouches) and his grab (it backs off).

| Level | 1 | 5 |
|-------|---|---|
| reaction time | 450 ms | 80 ms |
| sees an attack | 45 percent | 97 percent |
| counters | 20 percent | 95 percent |
| pause between its attacks | 2.6 s | 0.5 s |

The view turn speed grows with the level too.

The bot is the stock profile "Jedi Trainer" with a single saber, force jump 3 and saber offense and defense 3, under
the name "Trainer". Its commands go through the normal player movement code, so it can do nothing a player can not.
It has no path finding: it runs straight at the player. Its command is made once a game frame and sent again
unchanged in between (the bot frame runs more often than the game frame, and a bot moves once a game frame with its
last command).

It leaves when its player leaves, or when `vs_allowTrainer` goes to 0 or the gametype is not free for all. It stays a
trainer over a map change: owner and level are kept in the userinfo key `vs_trainer` of the bot.

#### Movement log (not from JA+)

`game/trainer/g_movelog.c`. The server console and rcon command `vs_movelog <player>` (name without colours, or the
slot number) records the movements of one player, to see how players hook and flip kick. The command toggles it. In
the web panel it is the "Record moves" button of the player panel (action `movelog`), and the player list shows the
tag "rec" while it runs.

Every usercmd of that player becomes one CSV line in `movelog/<date>_<time>_<slot>.csv` in the home folder of the
server (`fs_homepath/<fs_game>`, `/var/lib/visionjk/japlus/movelog/` on the official host). The first line is
`# <name>`, the second the column names. A line has:

| Part | Columns |
|------|---------|
| inputs | buttons, forward, right and up move, view angles |
| player, after its pmove | origin, velocity, on ground, legs and torso animation with their timers, `pm_flags`, `eFlags`, `eFlags2`, flip kick, hook out and hooked, weapon, `weaponTime`, saber move, saber style, holstered, force points, active force powers, levitation level, health |
| nearest other player | slot, distance, offset, velocity, on ground, animations, saber move, `eFlags2`, health |

Everybody on the server is told in the chat when it starts ("The movements of <name> are recorded now (for the
training bot).") and when it stops, and every two minutes while it runs. It stops by itself after 15 minutes, when
the player leaves and with the map.

## Not done on the server

- The wall flip part of `jp_jk2RedDFA` (the red DFA rule itself is in).
- `jp_debugClientCMD` has no code in the original either.
