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
| server info `g_visionCaps` | not from JA+. What this game module does for the clients of this tree: bit 0 chat in UTF-8 per recipient (`JP_ChatFor()`), bit 1 it passes statuses on. Read only |
| server command `jpstatus <client> <away> "<music>"` | not from JA+. What stands above a player's head, see "Statuses" below. Only sent to a client whose userinfo has `cjp_caps` bit 0 |
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
  the plugin (`cp_login`).
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
  `cm_clipOff`, the numbers of the brushes in the brush lump of the map file that are off, with a blank between
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
| `g_afkTime` | 60 | seconds without a key or a turn after which a player counts as away, 0 never |

A player whose commands did not change for `g_afkTime` seconds is away until they do. The server finds that
out itself, so it holds for legacy clients too. A client can say what its player listens to with the client
command `jpstatus music <text>` and end it with `jpstatus none`: at most one every three seconds, 64 bytes,
without colours, quotes and control characters. Both are passed on to every client whose cgame asked for
statuses, and to nobody else, so a legacy client never gets a command it does not know. `jpstatus` is not
written to the client command log. Not run.

## Not done on the server

- The wall flip part of `jp_jk2RedDFA` (the red DFA rule itself is in).
- `jp_debugClientCMD` has no code in the original either.
