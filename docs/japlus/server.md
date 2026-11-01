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

The masterserver is not part of this repository. It is a program of its own that the VisionJK hosts run, and it
is the one place for the players of servers without this game module: the relay of their voice chat, their clan
radio, their statuses and the boxes of their boombox, and the reputation of game servers. The client finds it at
`master.jka.vision`, UDP port 29170 (`v_voiceMasterHost`, `v_idsMaster`). A game server owner does nothing for it
and the server is told nothing: the masterserver answers no server query and no connect, only the voice datagrams
([voice.md](voice.md)) and the packets of the reputation ([security.md](security.md), "Added later: reports to the
masterserver"). The voice chat, the clan radio, the statuses and the boombox there work the same for the player as
with the game module, with the differences that [voice.md](voice.md) ("On a server without it") and
[boombox.md](boombox.md) ("Through the masterserver") list.

A game server that clients of this tree reported for an attack, or crashed on, is flagged by the masterserver. The
clients mark it in the server browser and refuse the first connect. A flag is a warning, nobody is blocked by it.
A game server that is not flagged sees nothing of it.

### The TV of the server (not from JA+)

`server/sv_tv.cpp`, `server/web/sv_web_tv.cpp`, `client/cl_tv.cpp`. Issue #7. Built 2026-10-30.

The TV is a spectator that stands in no slot of the server and has no connection, as a bot that records a server
side demo has none. It follows a player who plays and goes on to the next one after `sv_tvSwitch` seconds (20), or
at once when its player stops playing. While nobody plays it stands as a spectator of its own, where the last
player it followed was, else where a spectator is, else in the middle of the level, so a viewer still gets a
picture. No player sees it, the game module knows nothing of it, and its client number in the demo is a free slot
from the top (`MAX_CLIENTS - 1` down).

`tvrecord <name>` writes `demos/<name>.dm_26`, a normal demo that any client plays with `demo`, until `tvstop` or
the end of the level. A new `tvrecord` ends the old file. `sv_tvSwitch` is archived.

The same stream goes live to the viewers of the web host (`vs_clientWebPort`, which is 0, off, until set; the port
is the one the browser client uses for `/net` and the voice chat for `/voice`, see [web.md](web.md)). The TV is
watched in the browser, with the page of the browser client opened with `?tv`
(`http://<host>:<vs_clientWebPort>/?tv`, over `wss` when the page came over https), and in demos written with
`tvrecord`. The desktop client has no `tv` command (`client/cl_tv.cpp` is compiled for the browser client only).
What the page loads in this mode is in [web.md](web.md), "The TV in the page".

`GET /tvinfo` answers `{"map":"<map name>"}`, the map that runs. A name with characters other than letters, digits
and `_-./` is sent as `""`. The page asks it which pack of files to load.

A server that only wants to offer the TV sets `vs_clientWebTVOnly 1`. The web host then answers `GET /tv` and
`GET /tvinfo` and sends 404 for everything else (the page, `/manifest.json`, `/pk3/...`, `/net`, `/voice`), so it
hands out no game files and takes no players.

- `vs_tv` (default 1, archived): 0 turns the viewers off. `GET /tv` is then answered with 404, as it is when no
  level runs.
- 32 viewers at once and 4 of them from one address. Past that the answer is 503 with `Retry-After: 5`.
- No login, and any `Origin` is accepted: what the TV shows is what any spectator of the server sees.
- The viewer only listens, what it sends is ignored.

A server with a website behind https can hand the TV out on its own address. The TV host stays on the machine
(`vs_clientWebBind 127.0.0.1`, `vs_clientWebTVOnly 1`), the web server passes `/tv` and `/tvinfo` on to it, and
`vs_clientWebProxy` names the address of that web server (IPv4 addresses, split by blanks or commas, empty by
default). For a viewer that comes from one of them, the address in its `X-Real-IP` header counts for the limit of 4
per address, and the log line says "(through a proxy)". Without it every viewer has the address of the web server
and 4 of them fill the limit. The browser client and its packs can be served by the same web server, under
`/watch/` on jka.vision (see [web.md](web.md), "The TV in the page"). For nginx:

```
location ~ ^/(tv|tvinfo)$ {
	proxy_pass http://127.0.0.1:29071;
	proxy_http_version 1.1;
	proxy_set_header Upgrade $http_upgrade;
	proxy_set_header Connection "upgrade";
	proxy_set_header X-Real-IP $remote_addr;
	proxy_set_header Host $host;
	proxy_read_timeout 1h;
}
```
- Nothing is built while nobody watches or records, so a server without viewers pays nothing for it.

For somebody who writes a viewer: `GET /tv` becomes a WebSocket and every binary message on it is one record of a
demo, `[int32 number][int32 length][message]`, both integers little endian, the message ending with `svc_EOF`.
The first record is the gamestate, then comes one snapshot per frame of the server, each against the one before.
When the level changes the gamestate comes again, and the message numbers go on (a client refuses numbers that
go back). A viewer whose send buffer was full gets nothing until the next whole snapshot, which the TV then makes
for it. The file has the same records, so one reader does for both.

On a stream the end of a demo (a length of -1) never comes, the socket closes instead.

### Cleaning up cvars

`qcommon/cvar/cvar_cleanup.cpp`, `qcommon/cvar/cvar_seen.c`. The engine of the server notes the day each cvar was last used,
the same as the client does (client.md, "Cleaning up cvars"). The file is `cvarseen.txt` in `fs_homepath`. The
console command `cvar_cleanup` lists the user made cvars no code used for 30 days, and `cvar_cleanup remove <name
...|all>` unsets them and writes `visionjk_server.cfg` again. `server.cfg` and other files written by hand are not
edited. The web panel does that in the view Cleanup of the tab Settings (webadmin.md, "The tab Settings").

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
  council      ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... admin
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

`vs_caps` has bit 2 for the key login, a client only offers it where it reads the bit. Its default is 399 now
(was 143): bit 8 (value 256, `AUTH_CAPS_HOST`) is the key login with the host key.

The server makes a host key of 32 random bytes once and keeps it as base64 in `auth_host.key` in its game
folder, the same place as `admin_keys.txt`, through the file system of the game. It is made only when
`admin_keys.txt` has keys. If the file can not be written, the key is new at every start of the server. The
key goes out with the nonce (`vs_auth nonce <nonce> <host key>`), clients of before read only the nonce. The
signed text of version 2 is the line `VisionJK admin login v2`, the nonce, the host key, the address and the
client number. A client keeps the host key of each address and does not sign for another one, see
[client.md](client.md) and [security.md](security.md). The host key is no secret, the protection is what the
client kept.

A signature of version 1 (older clients, no host key) is taken only when `vs_authHosts` is set. Without it the
older client gets `vs_auth fail version` and is not logged in. So players with an older client have to update,
or the owner sets `vs_authHosts`. Set it on a public server anyway: it is what stops a signature that was made
for another server and passed on, for those clients.

Tested: `japlus_port/tools/authtest/run.sh` (66 checks: the three test vectors of RFC 8032 through the
Monocypher of the tree, the signed message, the key lines, key files in both directions against `ssh-keygen`,
and the agent protocol against an `ssh-agent` started for the test). The logins themselves ran against a server
on 2026-10-11, see client.md.

## Not done on the server

- The wall flip part of `jp_jk2RedDFA` (the red DFA rule itself is in).
- `jp_debugClientCMD` has no code in the original either.
