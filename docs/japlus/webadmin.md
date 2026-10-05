# Web admin panel

The game module can serve an admin page over HTTP: the map in 3D with all players as animated models, player
actions and a live entity editor. It is not part of JA+. The code comes from JKAdmin
(commit 7b712a55, "Add web admin panel to the MP game module" by Lixsl),
which is OpenJK 260c59c2 plus that one commit, the same base this tree starts from.

| File | Content |
|------|---------|
| `codemp/game/g_webadmin.c` | HTTP server, JSON state, player actions, entity editor, chat lines |
| `codemp/game/webadmin/index.html` | the page of JKAdmin, with a chat box added |
| `cmake/Modules/EmbedFile.cmake` | turns the page into `webadmin_html.h` in the build directory |

The page is compiled into `jampgame`, the server needs no extra files and no extra library (`ws2_32` on Windows).

## Turning it on

The panel only starts when a password is set. In the server config:

```
seta g_webPassword "something long"
seta g_webPort 8080
seta g_webBind ""
```

| Cvar | Default | Meaning |
|------|---------|---------|
| `g_webPassword` | empty | password of the panel. Empty: the panel is off |
| `g_webPort` | 80 | TCP port. Below 1024 needs root on Linux |
| `g_webBind` | empty | IPv4 address to listen on. Empty: all addresses |
| `g_webRoot` | empty | folder with an `index.html` that is served instead of the built in page, for working on the page |

The first three are latched, they take effect at the next map start. Open `http://server:8080/` and log in with
any user name and the password (HTTP basic auth).

## What the page does

- Players: list, health, armor, size, team, god mode, kill, kick, ban, follow, first person view with the HUD.
  Right click on the map teleports the selected player.
- World: all entities with their spawn keys. Edit keys and apply, drag or right click to move, rotate, duplicate,
  delete, place new ones with a model preview, export the layout as `.ent`.
- JA+ admin commands for the selected player: sleep and wake, protect, empower, merc, mindtrick, ghost, silence
  and unsilence, force and unforce the alternate dimension, deny vote, freeze, slap, show the MOTD, rename, a
  message on his screen (ampsay) and his admin rank (admGrant). A button is lit while its state is on.
- Chat, at the bottom of the side bar: what the players say, with the time, the team of the speaker and a tag for
  team chat, tells (with the receiver), clan chat and admin chat. Below it the field for a message to all players.
  A click on the title folds it away, new lines are counted there meanwhile.
- Gravity: a field in the top bar for everybody (`g_gravity`, 800 is normal, the field shows the current value),
  and a field in the player panel for a gravity only that player has. Empty there means the gravity of the server.
- Top bar: add bots, change map.

## How it works

`WebAdmin_Frame` is called at the start of `G_RunFrame`. All sockets are non-blocking, a request is read, answered
and closed without ever waiting, so a slow browser can not hold up the game. At most 32 connections at once, an
idle one is closed after 15 seconds, a request may be 16 KB.

`G_SpawnGEntityFromSpawnVars` hands the spawn keys of every map entity to `WebAdmin_EntitySpawned`, `G_FreeEntity`
calls `WebAdmin_EntityFreed`. That is where the editor has the keys from. An edit spawns a new entity from the
changed keys through the normal spawn code and then frees the old one.

Chat lines come from `WebAdmin_Chat`, called where a line really goes out: `G_Say` after the word filter (say,
say_team, tell), `clansay`, `amsay`, the `say` of the server console and the say action of the panel. The newest
128 lines are kept in a ring. The engine loads the game module again for every map and every `map_restart`, so the
lines of the map before are gone on the server. The page keeps what it already shows and draws a line. `epoch` in
the answer tells the page that the numbers started over.

| Request | Answer |
|---------|--------|
| `GET /` | the page |
| `GET /api/state` | map, scores, players, NPCs and items as JSON. The page polls this |
| `GET /api/map` | the `.bsp` of the current map |
| `GET /api/file?name=` | a file below `textures/`, `models/` or `gfx/` (images, md3, glm, gla, cfg, skin) |
| `GET /api/anims` | animation names in `animNumber_t` order |
| `GET /api/classes` | everything that can be spawned, and the items with model and icon |
| `GET /api/entities` | the entities with their keys |
| `GET /api/export` | the entities in `.ent` format |
| `GET /api/chat?since=&epoch=` | chat lines after number `since`. Everything if `epoch` is not the current one |
| `POST /api/action` | `action`, `id`, `value`: say, addbot, map, kick, ban, health, armor, scale, kill, god, noclip, gravity (no `id`), playergravity, team, teleport, and the JA+ ones: amsleep, amwake, amprotect, amempower, ammerc, ammindtrick, amghost, amsilence, amunsilence, amslap, amForceAltDim, amUnForceAltDim, amdenyvote, amShowMotd, amrename, ampsay, admgrant, freeze |
| `POST /api/entity` | `op` spawn, update or delete, with `id` and `kv` (one `key=value` per line) |

## Where it differs from JKAdmin

In `g_webadmin.c` the player actions that would bypass JA+ state go through the JA+ code instead, each place
carries a `JA+` comment:

| Action | JKAdmin | Here | Reason |
|--------|---------|------|--------|
| scale | writes `ps.iModelScale` | `JP_SetModelScale` | JA+ keeps the scale in `pers.jp.modelScale` and applies it at every spawn. The direct write is gone after the next death |
| god | toggles `FL_GODMODE` | also sets `pers.jp.cheatGod` | with `jp_allowGodTalk` the flag is cleared in every frame without a chat bubble |
| team | `SetTeam` | `JP_ForceTeam`, the answer tells whether the team changed | passes `jp_teamLock` and the join check of the team balance like `amforceteam`. With `g_teamForceBalance` 2 or 3 the game can still move the player back when he spawns in the bigger team, the answer says so |
| teleport | `TeleportPlayer` | `JP_ManualTeleport` | frees the hook, honours `jp_starTrekTeleport`, no push forward. Refused for a player in the alternate dimension, in a grapple move or with `refuseTele`, like rcon `amtele`. Works in every gametype |
| kill | `G_Kill` | refused while sleeping, knocked down or ghost | the states the `kill` command refuses |

Also changed:

- The chat view is new: `WebAdmin_Chat`, `/api/chat` and the one line hooks in `g_cmds.c`, `g_japlus_chat.c`,
  `g_japlus_admin.c` and `g_svcmds.c`. In the page: the `#chat` block in the side bar with its style rules, the
  script section "chat", and the say form moved there from the top bar. The chat section in `g_webadmin.c` and the
  script section are marked "not in JKAdmin". The rest of the page is JKAdmin's.
- A message typed in the panel is turned from UTF-8 into the one byte per character the game uses (Latin-1), so
  umlauts arrive. Characters the game does not have become `?`.
- New: the JA+ admin commands that work on a player (`WA_JpAction`). The panel does not copy them, it sends the
  same line an rcon admin would type on the server console (`amsleep 3`, `amrename 3 "name"`), so the rules,
  texts and logs are those of JA+. The command runs in the next frame, the answer of the panel only says it was
  sent, what happened shows in the state (`jp` object of a player: admin rank and the JA+ flags). `freeze` has no
  console command in JA+ and toggles the flag directly. Left out: `amplayefx` and `amtest`, which JA+ does not
  offer on the console. Kick, ban, team and teleport were there already. Added without a test run.
- New: gravity. `gravity` sets `g_gravity` for everybody, between -2000 and 5000. The map sets it again at its
  next start (worldspawn key `gravity`). `playergravity` gives one player a gravity of his own: `ClientThink_real`
  in `g_active.c` asks `WebAdmin_ClientGravity` where it used `g_gravity` (not in space, on a vehicle or as an NPC
  with its own gravity). It survives a respawn and is gone when the player leaves or the map changes. The state
  has `gravity` for the server and `gravity` and `ownGravity` for each player.
- New player action noclip: a button next to god mode toggles `client->noclip` like the `noclip` cheat, for a
  living player. The state has a `noclip` field, the list shows a tag. Added without a test run.
- "Add bot" with an empty name adds `Kyle Katarn`. JKAdmin asks for `Kyle`, a bot the game does not have, so
  nothing happened there.
- The two entity hooks pass `ent-g_entities` instead of `ent->s.number`.
- The JKAdmin text in the top level `README.md` was not taken over, it is this file.

To take a later JKAdmin commit: apply its diff and keep the `JA+` blocks in `WA_ApiAction` and the chat parts.

## Limits

- Not fixed: the entity editor can end the server. If the spawn code of a class raises an error, the dedicated
  server exits, because OpenJK makes every game error fatal there. Seen with `trigger_lightningstrike` placed
  without a `lightningfx` key. Brush classes (`trigger_`, `func_`) without a valid `model` key are expected to do
  the same. A restart script or care in the World tab is needed until placing is guarded.
- Plain HTTP with basic auth. Put it behind a reverse proxy with HTTPS before it is reachable from outside, or
  bind it to 127.0.0.1 and use an SSH tunnel.
- `POST` requests are not checked for their origin. A hostile web page opened in the browser that is logged in to
  the panel could send actions.
- The password compare is not constant time.
- `g_webPassword` is an archived cvar. The server writes it in clear text into `openjk_server.cfg` in its home
  path, and it shows in the process list when it is given on the command line.
- Whoever can connect can occupy the 32 connections, then the panel does not answer until they time out. The game
  is not affected.
- With the panel off the server prints one line at every map start ("Web admin is off, set g_webPassword to
  start it").
- Entities the game code made itself (JA+ blocking walls, hooks, effects) have no keys. The editor can only delete
  them, and the JA+ code is not told about it.
- The chat view shows team chat, tells, clan chat and admin chat too, like `games.log` does. The server keeps 128
  lines and only those of the current map.

## What was tested

On Linux with a dedicated server, two to four bots, FFA on mp/ffa3 and team FFA on mp/ffa4 after a map change
through the panel: login, every request of the table above, every action, the JA+ cases of the table (god mode
with `jp_allowGodTalk` 1, scale across a respawn, team past `amlockteam`, teleport with and without
`jp_starTrekTeleport`, the refusals), spawn, update and delete of an entity, malformed and oversized requests,
40 connections at once. 64 bit and, under valgrind without a finding, 32 bit.

Chat: every kind of line once from a scripted client (`japlus_port/tools/clienttests/chat.cfg`: say, say_team,
tell, amsay, clansay, umlauts), the console `say`, the panel, the ring with more than 128 lines, `since` and
`epoch` over a `map_restart` and a map change, also under valgrind.

The page was looked at once in headless Chromium with software GL: the 3D map, the player list and the chat box
with all kinds of lines show up. Not tested: using the page by hand (HUD view, dragging entities), the Windows DLL
at run time (it builds and links with mingw-w64), macOS.
