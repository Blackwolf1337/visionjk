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
- Blocking walls (`g_japlus_blockwall.c`): boxes per map from `<map>_holes.cfg`, editor commands BlockWallCreate,
  BlockWallDel, BlockWallNext, BlockWallPrev, BlockWallDraw, BlockWallWrite, BlockWallGrantEditor.
- Model scale from `modelscale.cfg` (`g_japlus_scale.c`): model, eye height and saber blades, with
  `jp_allowDmgSpeedScale` also saber damage and speed.
- Server housekeeping: welcome text and MOTD, inactivity protection in FFA (a player without input is protected
  from his spawn on until his first input, and again after `jp_inactivityProtectTimer` seconds), anti fake name list,
  userinfo flood protection, auto quit at a time of day (`jp_autoQUIT`), fake clients, download redirect URL,
  logging of plugin versions and IPs.

## Not done on the server

- The wall flip part of `jp_jk2RedDFA` (the red DFA rule itself is in).
- Macro scan of binds that run `exec` or `vstr`. Only binds with several commands are checked.
- `jp_debugClientCMD` has no code in the original either.
