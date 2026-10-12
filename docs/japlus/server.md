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
| server info `g_visionCaps` | not from JA+. What this game module does for the clients of this tree: bit 0 chat in UTF-8 per recipient (`JP_ChatFor()`), bit 1 it passes statuses on, bit 2 the key login, bit 3 it tells who plays with a client of this tree and answers `jpstatus all`, bit 4 it protects the body of a player who is out of it (Focus), bit 5 it is the Vision Temple (`g_visionTemple`), bit 6 it has a temple of its own for its players (`sv_templeInstance` of its engine). Read only |
| server command `jpclient <client> <version> [<client> <version> ...]` | not from JA+. With which VisionJK a player plays (`cjp_vision` of his userinfo, letters, digits and `. - _` only), "-" for any other client. Only sent to a client whose userinfo has `cjp_caps` bit 1: one player when his userinfo says something new, everybody in one command as the answer to the client command `jpstatus all`, which a cgame sends once when it has started (at most every two seconds) and which is also answered with the statuses of everybody. See client.md, "Who plays with VisionJK" |
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
  the plugin (`cp_login`). Not as in JA+: the passwords only count with `jp_adminPasswords 1`, see "Admin login
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

### Focus: the body of a player who is out of it (not from JA+)

`game/status/g_focus.c`, the client part is in [client.md](client.md), "Focus". Run with bots and with two real
clients on 2026-10-12, see there.

| Cvar | Default | Meaning |
|------|---------|---------|
| `g_visionFocus` | 1 | the body of a player in Focus can not be hurt by players: 0 never, 1 in a free for all (as `jp_inactivityProtectTimer`), 2 in every kind of game |

Focus itself happens in the client and needs nothing from a server: the world stands still on its screen while
the body stands in the level. This game module protects that body. A client asks with the client command
`jpfocus 1` where it reads bit 4 of `g_visionCaps`, and says `jpfocus 0` when its player is back. The answer is
the server command `jpfocus <held> <why>`: 1 the body is protected, or 0 with the reason (`focusWhy_t` in
`bg_public.h`: back, not here, a fight a moment ago, the last time a moment ago, not now).

- It is granted to somebody who stands on the ground alive, in no duel, in no vehicle and with no flag, who
  was not hit by a player and hit none for five seconds (`G_FocusFight()` in `G_Damage()`), ten seconds after
  his last time at the earliest. So it is no way out of a fight.
- While it holds the body has the invulnerability a fresh spawn has (`EF_INVULNERABLE`, renewed with every
  command of the client), its saber is away and it meditates (`BOTH_MEDITATE`, held as the emotes of JA+ are,
  set again where something ended it; the sitting animation the inactivity protection plays now and then is
  left out for it). Force powers, a punch and a kick in the face of other players do not work on it, as on a
  player an admin protects. What the level does to it, a fall or lava, still hurts. When its player is back it
  stands up (`BOTH_MEDITATE_END`).
- It ends with `jpfocus 0`, with the first move, attack or use of the client (a client in Focus sends none, so
  that is also the end when its command was dropped by the flood protection), with a duel and with death.
- The command is open to every client, a legacy one too: typed into the console it protects somebody who
  stands still under the same rules, which is what the inactivity protection does after its time.
- A legacy client sees a player who meditates inside the shell of a protected one.
- For the rules on idle players (`g_inactivity`, `g_afkTime`) the body is idle.

### The Vision Temple as a server (not from JA+)

`game/status/g_temple.c`, `tools/temple/build.py`, the client part is in [client.md](client.md), "The Vision
Temple as a server, and the visit". Run hidden on Linux on 2026-10-12 with one and two clients and with bots,
on one machine. Played by nobody, never run on a public address.

| Cvar | Default | Meaning |
|------|---------|---------|
| `g_visionTemple` | 0 | 1: this server is the temple. Read at the start of a level. The clients see it as bit 5 of `g_visionCaps` in the server info |

A temple server is a server of this tree with `+set g_visionTemple 1 +map vision_temple` (free for all). What
the mode does:

- Everybody who connects plays at once: no force setup, no join menu, no welcome in the middle of the screen.
- Nobody has a weapon (the fists, and they do not strike), a force power but the jump, or an item. Nobody
  takes damage, nobody can be challenged to a duel, Focus is off.
- Players do not stand in each other's way. That and the attack buttons are in the shared movement code
  (`PmoveSingle()`, `bg_visionTemple`), so the client predicts both.
- Somebody who gets under the floors starts again.

The map `maps/vision_temple.bsp` is in the asset pack. `tools/temple/build.py` writes it without a map
compiler. What one stands on are brushes: the terrace, the five steps of the podium, the way, and fences that
keep players from the clouds. Since 2026-10-12 what one sees is in the map too: the gold as surfaces (the
terrace, the steps, the way and its kerbs, the columns), the clouds and the light in the air as two fog
brushes, and a box of sky around all of it. Their shader scripts (`textures/vision/temple_gold`,
`temple_clouds`, `temple_light`, `temple_sky` in `shaders/vision.shader`) say `q3map_vision`, see
[renderer.md](renderer.md), "What only this renderer draws of a level": rd-vulkan-vision draws the place with
them, and its shaders have the same shapes as the map (the shadows on the gold are worked out from them), so a
size is changed in both. A client without that renderer (a legacy client, `rd-vanilla`, the browser) gets what
else the scripts say: the gold in the colours the map has in its points (a sun and a sky baked into them by
`build.py`, so a column has a bright and a dark side), a fog and a sky of one colour. Seen so with `rd-vanilla`
in a hidden run on 2026-10-12.

`assets/configs/temple.cfg` has the settings of a temple server: `+exec temple.cfg +map vision_temple`.

Clients find the server through the voice master, which names it in its answers to clients that enter the
temple (`VOICE_TEMPLE_SERVER`, `game/voice/bg_voice.h`). The masterserver (below) names itself. The voice master
as a program by itself names the server it was started with (`-temple <port>` for one on the same host, or
`-temple <address>:<port>`) and none without that option: its clients then stay in the temple that is drawn
into their own game.

### The temple of a server (not from JA+)

`server/sv_instance.cpp`, `qcommon/net_link.h`, `qcommon/net_ip.cpp`, the client part is in
[client.md](client.md), "The Vision Temple as a server, and the visit". Built 2026-10-12 for Linux, run hidden
on one machine with one and two clients. Played by nobody, never over a real network. A server on Windows or
macOS starts no temple yet (`Sys_StartSelf()` is written for Linux).

A dedicated server of this tree has a Vision Temple of its own: the temple key of one of its players leads
there, and to the masterserver (below) only from a server that has none. The players of one server are among
themselves in it.

| Cvar | Default | Meaning |
|------|---------|---------|
| `sv_templeInstance` | 1 | of the engine. 0: no temple of its own, the players go to the masterserver. 1: started when the first player asks for it. 2: started with the server |
| `sv_templeIdle` | 5 | minutes without a player after which a temple that was asked for ends, until somebody asks again. 0: never |

`templeinstance` on the console starts it by hand, `templeinstance stop` ends it.

**What it is.** A level and a server are one thing in this engine, so the temple is the same program a second
time, started by the server as a temple server on the map `vision_temple` (`+exec temple.cfg`). It reads what
its server reads, in the same order: the game module, the level, `temple.cfg`, the settings the server stored
and the lists of its admins and bans are the same for both. It writes into the folder `temple` inside the
folder its server writes to, stores no settings of its own, and says what it does in `temple/temple.log`
there. It ends when its server tells it to, when it does not hear of its server for half a minute, or when
nobody was in it for `sv_templeIdle` minutes.

**One port.** The temple has no port a player could reach, and the server needs none for it:

- The link: both programs have a UDP socket on 127.0.0.1. The server names its port and a key of 8 random
  bytes in the environment of the program it starts. Every datagram of the link carries the key and the
  address of the player it is from or for, so the temple sees every player with the address he has. Each end
  takes nothing without the key and nothing from another port than the one that said hello with it.
- The tunnel: a player in the temple sends to the port of the game of the server, with five bytes in front of
  every packet (the four of the number -2, which no packet of the game starts with, and a 1). The server takes
  such a packet only from an address one of its players plays from, and passes it on over the link. What the
  temple sends comes back over the link and leaves from the port of the game with the same five bytes.
- The voice chat of the temple goes the same way without the five bytes: a datagram of it that is none of a
  player in the level is passed to the temple, and the answers of the temple leave as they are. The socket of
  the voice chat in the client knows no tunnel.

So who is not let onto a server is not let into its temple, and nobody reaches the temple from outside.

Checked on 2026-10-12, hidden on Linux, everything on one machine over 127.0.0.1 (the scripts are in
`tools/templetest` and `tools/uitest` of the test tools):

- Without a client (`instance.sh`): the temple comes up when asked and says hello over the link. The server
  holds its game port and one socket on 127.0.0.1, the temple one socket on 127.0.0.1 and nothing else. A
  packet of the tunnel from an address nobody plays from gets no answer. `templeinstance stop` ends it, it is
  started again when asked, and it ends with its server.
- One client (`INSTANCE=1 uitest/visit.sh live`): the temple key in a level, the server starts its temple, the
  client is in it about a second later, walks, and is back in its body with the temple key.
- Two clients of one server (`pair.sh`): both are in the temple with the addresses they have, the test tone
  of one arrives at the other without a gap (the voice chat of the temple on the port of the game), both are
  back. With `PAIR_MAP=1` the server changes its level meanwhile and both come back into the new one.
- A temple that does not answer (`fallback.sh`): the client gives the visit up after 8 seconds, is back in its
  body, and the next temple key leads to the masterserver. With `FALLBACK_OFF=1` (`sv_templeInstance 0`) the
  first key does.
- The end by itself (`idle.sh`): a temple that was asked for ends after `sv_templeIdle` minutes without a
  player, one that was started with the server stays.

Not checked: a real network, more than two players, a server under load, a level that takes long to load,
Windows and macOS (no temple is started there), a client in a browser.

What an owner should know: while the server loads a level its players in the temple stand still, since their
packets pass through it. A second server uses memory while it runs. A server with `sv_voiceShared 0` or a
`g_voicePort` has no voice chat in its temple. The game module in another engine, or a game a player started
from the menu, has no temple of its own: its players go to the masterserver.

### The masterserver (not from JA+)

`server/sv_voice.cpp`, `voicemaster/vm_core.c`. Built 2026-10-12, run hidden on Linux on one machine (the checks
are in [voice.md](voice.md), "Checked"). Never run on a public address so far.

One program on one UDP port that is everything central of VisionJK: the Vision Temple as a game server for the
players of servers without this game module, and the voice master with the voice chat, the clan radio and the
statuses of those servers. It is the dedicated server of this tree built with `-DBuildMasterServer=ON` (target
`visionjk-master`, off unless asked for, with no install rule: it is for the hosts of VisionJK itself and no
part of a release).

```
visionjk-master.x86_64 +set dedicated 1 +set net_port 29170 +exec temple.cfg +map vision_temple
```

What comes in on its port:

- a packet of the game: a player in the temple, as on any server
- a datagram of the voice chat of one of those players: the relay of its game module, as on any dedicated
  server of this tree
- any other datagram of the voice chat: the voice master. `sv_masterKbps` (4000, command line only) is what it
  may send in a second, 0: anything

A client that asks the voice master for the temple is told the port of the game of this very program, so the
client goes on to it with a visit. 29170 is the port the clients of 0.1.5 know the voice master on.

Not there: rooms for those who use a word of their own, a list of who is in the temple, more than the 32
players one server holds.

### Admin login without a password (not from JA+)

`game/auth/g_auth.c` and `game/auth/bg_auth.c`, the client part is in [client.md](client.md). Since 2026-10-11.

| Cvar | Default | Meaning |
|------|---------|---------|
| `jp_adminPasswords` | 0 | 1: the three rank passwords count again (`amlogin <password>`, `cp_login` of a plugin) |
| `jp_authHosts` | empty | the addresses the server is reached by, `"1.2.3.4:29070 192.168.0.5:29070"`. A key login signed for another address is refused |

A rank password is the same for everybody of the rank, it is typed into a console that logs, the JA+ plugin
sends it to every server it joins in the userinfo, and whoever has seen it once has it for good. So the
passwords are off unless the owner switches them on, and an admin logs in one of two ways:

- With a key. The file `admin_keys.txt` in the game folder has a line for each admin:

  ```
  # rank       what "amkey show" of the client prints, or the line of a .pub file of ssh-keygen
  council      ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... emir
  rcon         ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... owner
  ```

  The rank is `council`, `knight`, `instructor` or `rcon` (every right). The client says which key it has
  (`jpauth hello`), gets 32 random bytes that are good for one answer and 30 seconds, and sends a signature
  over them (`jpauth sign`). The signed text starts with a fixed line, so the signature is worth nothing
  anywhere else, and it has the address of the server as the client sees it and the number of the client in
  it. A wrong signature counts as a wrong try: three of them and the player waits, as with wrong passwords.
  Who has the rank of his key already (it is kept over a change of the map) is not asked or announced again.
  `amkeys` on the console of the server reads the file again and lists it, it is also read with every map.
- With a one-time code, for admins whose client can not sign (jamp.exe, the JA+ plugin): `amcode <rank>` on
  the console of the server, or the web panel, makes a code of eight letters and digits. It is typed as
  `amlogin <code>` within a minute and works once.

`g_visionCaps` has bit 2 for the key login, a client only offers it where it reads the bit. Set `jp_authHosts`
on a public server: without it the server can not tell a signature that was made for another server and
passed on.

Tested: `japlus_port/tools/authtest/run.sh` (66 checks: the three test vectors of RFC 8032 through the
Monocypher of the tree, the signed message, the key lines, key files in both directions against `ssh-keygen`,
and the agent protocol against an `ssh-agent` started for the test). The logins themselves ran against a server
on 2026-10-11, see client.md.

## Not done on the server

- The wall flip part of `jp_jk2RedDFA` (the red DFA rule itself is in).
- `jp_debugClientCMD` has no code in the original either.
