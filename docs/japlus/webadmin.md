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
| `g_webBind` | empty | IPv4 address to listen on. Empty: 127.0.0.1 only. Set `0.0.0.0` to reach it from the network |
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
| `POST /api/action` | `action`, `id`, `value`: say, addbot, map, kick, ban, health, armor, scale, kill, god, noclip, schizo, flatulence, disorient, poop, gravity (no `id`), playergravity, team, teleport, and the JA+ ones: amsleep, amwake, amprotect, amempower, ammerc, ammindtrick, amghost, amsilence, amunsilence, amslap, amForceAltDim, amUnForceAltDim, amdenyvote, amShowMotd, amrename, ampsay, admgrant, freeze |
| `POST /api/entity` | `op` spawn, update or delete, with `id` and `kv` (one `key=value` per line) |
| `GET /api/config` | the text of `server.cfg`, as UTF-8 |
| `POST /api/config`, `POST /api/config?exec=1` | the body is the new text of `server.cfg` (plain text, not a form). With `exec=1` the server runs `exec server.cfg` afterwards |
| `GET /api/fs/list?root=&path=` | the files of a folder. `root` is `home` (the game folder in the home path of the server) or `install` (the one of the installation, read only) |
| `GET /api/fs/get?root=&path=[&text=1]` | a file as a download, or as UTF-8 text for the editor |
| `POST /api/fs/put?path=&offset=[&last=1][&text=1]` | the body is the next piece of a file (a request is 64 KB at most). `last=1` puts the file in its place |
| `POST /api/fs/delete?path=`, `/api/fs/rename?path=&to=`, `/api/fs/mkdir?path=` | in the home path only |

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
- New: hallucinations for one player (`codemp/game/g_schizo.c`), the "Schizophrenia" button in the player panel,
  action `schizo`, also the server console command `schizo <player>`. Every few seconds something happens that
  only he gets: blaster bolts pass him, a saber is switched on behind him, steps come closer, voices, breathing
  and screams, a detonator beeps and goes off, the lock tone of a rocket launcher, thunder with a shaking view,
  chat lines other players never wrote, text on his screen, a kill message between two other players, a hit from
  nowhere. Two things are real: his saber goes off and his legs jump. It is server side only. The sounds, effects
  and the kill message are temporary event entities flagged for that one client (`SVF_SINGLECLIENT`), the bolts
  are missiles with that flag that have no damage and only hit the world, the texts are server commands. A
  spectator who follows him gets them too. Gone when he leaves or the map changes. Added without a test run.
  He also gets a stalker: a real NPC (type `ragnos`, the ghost of the single player game, `prisoner` if that one
  does not spawn) that is only sent to him. It appears behind him, stares at him and walks after him with the
  stock follow behaviour, is gone when he walks into it or after 8 to 18 seconds, and is put behind him again when
  he left it behind and does not look. For everybody else it does not exist: not sent, not solid (spawn flag 64,
  which also keeps `NPC_Begin` from killing whoever stands at the spot), no damage, no weapon, no enemies. One NPC
  per player is spawned once and then hidden and moved, not spawned again each time. Also without a test run.
- Fixed in the page: the key fields of an entity lost the focus about once a second. JKAdmin builds the key
  table again with every poll of the entities unless something was typed. Here it is only built again when the
  entity or its keys changed, and never while the cursor is in one of its fields.
- New emote `ampoop` (`codemp/game/g_poop.c`, the command with the other emotes in `g_japlus_cmds.c`). A player
  types it or binds it like `amsit`: he sits down in the air (animation `BOTH_SIT3`), moans three times (the
  pain sounds of his own model), and a small rock lands on the floor under him with a splat and a sigh. The
  rock is a plain model entity (a chunk of the rock debris), lies there for 90 seconds, at most 24 on the map.
  The rules of the emotes apply (`JP_Emote`), bit 30 of `jp_emotDisallow` turns it off, he has to stand still
  and it is not for duels. It is in the emote list the server prints. In the panel the "Poop" button sends it
  for the selected player (action `poop`). Server side only. Added without a test run.
- New: flatulence for one player (`codemp/game/g_flatulence.c`), the "Flatulence" button in the player panel,
  action `flatulence`, also the server console command `flatulence <player>`. Every 1.5 to 6 seconds he breaks
  wind: a sound everybody near him hears (the snorts of the rancor, the talk of the gonk droid, an air burst)
  and a green cloud behind him (the gas cloud of the noghri stick). One in six is a big one with three clouds
  that shoves him forward, not in a duel. Server side only, stock sound and effect events. Gone when he leaves or
  the map changes. Added without a test run.
- New: disorientation for one player (`codemp/game/g_disorient.c`), the "Disorient" button in the player panel,
  action `disorient`, also the server console command `disorient <player>`. The view sways all the time (pitch,
  yaw and roll, two sines each with periods that do not fit together). Every 3.5 to 9 seconds it is thrown around
  by 70 to 200 degrees with a screen shake only this client gets, and the player stumbles a step to the side (not
  in a duel, not in the air). One in four of those is a whirl instead: the view turns once or twice around. All
  of it goes through `ps.delta_angles`, the offset the server adds to the view angles of a client, so the client
  predicts it and the movement and the aim follow the view. The roll shows in the first person view only (the
  third person camera has none). Not while dead, a spectator or in a vehicle. Bots are not bothered, they turn
  to where they want to look. Server side only. Gone when the player leaves or the map changes. Added without a
  test run.
- New: the tab "Config" edits `server.cfg` (`WA_ApiConfigGet`, `WA_ApiConfigSet` in `g_webadmin.c`). It is that
  one file, the request names none. The text field lies over the map view, the buttons are "Save", "Save and
  execute" (`exec server.cfg` on the server console) and "Reload". The engine reads a file from the home path
  first and writes only there, so the first save makes `fs_homepath/<game>/server.cfg` and the file of the
  installation stays as it is. What was there before a save is kept as `server.cfg.bak`. An empty text is not
  saved. The file is Latin-1 on disk and UTF-8 on the page, a character the game does not have becomes `?`.
  A request may be 64 KB now (16 KB before), the page refuses a text over 60000 bytes. The passwords in the
  file are shown as they are, and whoever can log in can change them: the panel is only as safe as
  `g_webPassword`. The security log gets "config saved" with the size, never the text. Executing sets cvars at
  once, latched ones wait for the next map, and a line that changes the map changes the map. Added without a
  test run (the read was called once after the deploy, nothing was saved).
  The text has colors: commands of the console, cvar names, quoted text, numbers and comments, and inside
  quotes the color codes of the game color what follows them as the game would (a `sv_hostname` reads as on the
  server list). It is a second layer under the text field, whose own letters are not drawn (`cfgLine`,
  `cfgString`, `cfgPaint` in the page), no library. Checked with node that every line of the config reads
  the same colored and plain, not seen in a browser.
- New: the tab "Files", a file manager for the game folder and nothing above it (`WA_ApiFs*` in
  `g_webadmin.c`). It shows two folders: the game folder in the home path of the server
  (`fs_homepath/<fs_game>`: logs, the stored config, what the panel saved), which can be changed, and the game
  folder of the installation (`fs_basepath/<fs_game>`), which can only be read. A click opens a folder or a
  text file (in a text field as in the Config tab, with the colors for a `.cfg`), any other file is
  downloaded. In the server folder: upload (in pieces of 48 KB, several files, 512 MB at most), new folder,
  rename (also into another folder), delete (a folder only when it is empty). A text file of the installation
  is saved into the server folder under the same name, where the engine looks first.
  How it is kept inside the folder:
  - a path is checked letter by letter: names of letters, digits, blank and `_-.()+,` with `/` between them,
    none that begins with a dot. That leaves no `..`, no absolute path, no backslash, no drive letter. A file
    with another kind of name is listed and can not be touched.
  - the system then resolves the path (`realpath`), and the result has to lie inside the folder. A symbolic
    link is shown as one and neither followed nor touched (on Windows a junction is not looked for).
  - nothing is written, renamed or deleted that could be run: `.so`, `.dll`, `.exe`, scripts and the like,
    wherever in the name the extension is. A game module with another content would be loaded with the next
    map, so the modules can only be downloaded.
  - a file comes in as `<name>.part` and takes its place with the last piece, so a file that is there is
    replaced in one go or not at all. A rename never goes onto something that is there.
  - an address that does not fit the buffer of the request line is refused (414), not cut.
  Every change and every file that is read goes to the security log with its path. Checked on the running
  server with a script: 27 paths that try to leave the folder for list and get, four more for put, mkdir,
  rename and delete, a link to `/etc` inside the folder, the game module, and the plain operations on scratch
  files with text in Latin-1 and an upload in two pieces. Everything was refused that has to be and nothing
  was written outside. The page itself was not seen in a browser. The Windows module builds, it was not run.
- New entity `misc_cube` (`codemp/game/g_props.c`, next to the ball), placed in the World tab like the ball. A
  cube with corners and edges that is a rigid body: it falls, lands on a corner or an edge, tips over, tumbles
  and slides to a stop on one of its faces. It is heavy. Players and NPCs shove it instead of kicking it away:
  only part of their speed gets into it, off its middle the shove turns it, and the player is slowed down while
  he shoves (`G_PropPlayerSpeed` in `ClientThink_real` of `g_active.c` scales `ps.speed`, which the client
  predicts). Force push and pull take it above its middle, so it tumbles. A shot pushes along its line at the
  spot it hit, so a hit off the middle spins the cube. The engine only traces boxes that are not turned, so the
  cube looks at 26 spots of itself in every step (corners, middles of edges and faces) with a line from its
  middle, and takes the speed into the surfaces away with impulses, friction included. A server frame is cut
  into steps of 12.5 ms for that. A small box in its middle is traced as well, so nothing thin gets through it.
  Once it hardly moves on three spots that carry it, it lies still and costs nothing. For the client it is a
  model entity whose place and angles change every frame (`TR_INTERPOLATE`, like players). The model is
  `models/map_objects/cairn/cargo_sm.md3`, a small cargo crate. The base game has no plain cube, this one has
  slanted upper edges, the physics use the full box around it. That box is read from the bounds in the md3 when
  the cube spawns, so the physics fit whatever `model` is set, cubes or not. Keys: `size` 32 (its longest edge,
  the model is scaled to it), `bounce` 0.25, `friction` 0.7 (grip on the ground, not the rolling resistance of
  the ball), `mass` 4, `kick` 1, `model`, `angles`. The page places it 48 units above the floor. Nobody can
  stand on one, lifts do not carry it. Added without a test run.
- New: balls and cubes collide with each other, so cubes can be pushed with cubes and stacked. The traces of
  the engine do not see them (shots and players have to get through), so this is worked out from their shapes.
  Cube against cube: of the six directions their faces look into, the one along which they overlap least is
  the direction they are pushed apart along, and every spot of one that is in the other is a contact in the
  same solver that handles the world. Both get the impulse, each by its mass, friction included, so a cube that
  is pushed takes the one on top of it along. Cube against ball: one contact at the spot of the cube next to
  the ball. Ball against ball and ball against cube are also handled in the think of the ball: they bounce
  off each other by their masses and are put apart, and a ball on top of a cube rolls out like on the ground. A
  prop that lies still counts as part of the world for the others, and so does one that rests on something
  for what lies on top of it. That is what lets a pile come to rest, from the bottom up. A hit faster than 60
  units per second (weighed by the masses for a hit from the side) or a push more than 4 units into it wakes
  it, and a prop that wakes up wakes the ones that lie still next to it, so a pile falls when its base is
  taken away. Limits: edges that cross without a spot of one cube in the other are not seen, a ball can not
  come to rest on a cube without thinking every frame (it does), and a pile is only as calm as the solver.
  Added without a test run.
- New: taking a ball or a cube into the hand with `+button13` (for example `bind g +button13`). The button
  bits of the base game end at 2048, JA+ has 4096 (`+button12`) for the hook, `+button13` to `+button15` (8192,
  16384, 32768) are free and all 16 bits reach the server. No client change: the stock client and the JA+
  plugin already have the command. The player takes the prop he looks at, within 96 units: a cube at the corner
  next to his line of sight, a ball at the spot next to his eye. While he holds the button that spot is pulled
  to a point 48 units in front of his eyes and the rest hangs from it and swings, a pendulum on one point. It
  follows when he turns and walks, heavy props lag more. It still bumps into the world. He lets go by letting go
  of the button, it keeps the speed it had, so it can be flung. A force push throws it out of the hand. It is
  also let go when he dies, leaves, or it gets more than 144 units away from the hand (caught behind a wall,
  teleport). The one who holds it does not kick it. Added without a test run.
- New entity `misc_ball` (`codemp/game/g_props.c`), placed in the World tab like the black hole. A ball with
  physics of its own: gravity, bounces that keep part of the speed, rolling with resistance, rolling down slopes,
  coming to rest. Players and NPCs kick it by running into it (it bounces off them like off something much
  heavier that moves), force push and pull throw it (a call in `ForceThrow` of `w_force.c`), shots, sabers and
  explosions knock it away from the point they hit. It is server side only: a plain model entity (`ET_GENERAL`,
  the ball of the training remote scaled up) on a linear trajectory the server renews every frame, with a second
  one for the turning. Its contents are those of a corpse, so shots hit it and players are not blocked. Keys:
  `radius` 18, `modelscale` 600, `bounce` 0.7, `friction` 90, `mass` 1, `kick` 1, `model`. A use, falling out of
  the world or getting stuck brings it back to where it was placed. Lifts do not carry it, and the black hole
  does not pull it. Added without a test run.
- New entity `misc_blackhole` (`codemp/game/g_blackhole.c`), made to be placed in the World tab: it is in the
  spawn table, so it shows up in the class list. It pulls players and NPCs towards itself, stronger the closer
  they are, and crushes what reaches its core. It is server side only: for the client it is a stock looping effect
  entity (`ET_FX`, like `fx_runner`) with a looping sound and a light. Keys: `radius` 600, `strength` 2400 (the
  gravity is 800), `coreradius` 48, `dmg` 1000, `fxFile`, `delay`, `noise`, `light`, spawnflag 1 for off at the
  start. The page places it 72 units above the floor and gives it the first four keys, so they are there to edit.
  Players in noclip and spectators are left alone. Added without a test run.
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
  leave `g_webBind` empty (127.0.0.1) and use an SSH tunnel. Since the security pass the default bind is localhost,
  not every interface.
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

The panel went through the security pass in [security.md](security.md). Fixed there: the `Content-Length`
overflow, requests from other sites (an `Origin` check on every POST), the password compare (now constant time)
and a wait after wrong passwords, guarded allocations, the base64 decoder, and the localhost default bind. The IP
of a ban is checked before it goes into a console line. Open items above (plain HTTP, the editor ending the
server) stand.

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
