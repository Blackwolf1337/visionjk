# Web admin panel

The game module can serve an admin page over HTTP: the map in 3D with all players as animated models, player
actions and a live entity editor. It is not part of JA+. The code comes from JKAdmin
(commit 7b712a55, "Add web admin panel to the MP game module" by Lixsl),
which is OpenJK 260c59c2 plus that one commit, the same base this tree starts from.

| File | Content |
|------|---------|
| `codemp/game/webadmin/g_webadmin.c` | the HTTP server: connections, which request goes where, what the game calls |
| `codemp/game/webadmin/wa_http.c` | buffers, answers, headers, the login and its throttle, form values |
| `codemp/game/webadmin/wa_state.c` | `/api/state` |
| `codemp/game/webadmin/wa_actions.c` | player and server actions, the bans |
| `codemp/game/webadmin/wa_entities.c` | the entity editor, the reload of placed entities |
| `codemp/game/webadmin/wa_assets.c` | the map, the files of the game, the lists to pick from |
| `codemp/game/webadmin/wa_fs.c` | the file manager and the server config |
| `codemp/game/webadmin/wa_perf.c` | `/api/perf` |
| `codemp/game/webadmin/wa_log.c` | the chat lines and the log of the game |
| `codemp/game/webadmin/wa_routes.c` | the route of the bots |
| `codemp/game/webadmin/wa_local.h` | what those files share |
| `codemp/game/webadmin/page/` | the page: a template, a style sheet and its script files |
| `codemp/game/webadmin/index.html` | the page of JKAdmin, with a chat box added |
| `cmake/Modules/EmbedFile.cmake` | turns the page into `webadmin_html.h` in the build directory |
| `cmake/Modules/ExtractQuaked.cmake` | collects the entity definitions of the game code for the editor |
| `cmake/Modules/ExtractSpawnKeys.cmake` | collects the keys the spawn function of every class reads, for the completion of the editor |

The page is compiled into `jampgame`, the server needs no extra files and no extra library (`ws2_32` on Windows).

## Turning it on

The panel only starts when a password is set. In the server config:

```
seta g_webPassword "something long"
seta g_webPort 8080
seta g_webBind "0.0.0.0"
```

| Cvar | Default | Meaning |
|------|---------|---------|
| `g_webPassword` | empty | password of the panel. Empty: the panel is off |
| `g_webPort` | 80 | TCP port. Below 1024 needs root on Linux |
| `g_webBind` | 0.0.0.0 | IPv4 address to listen on. `0.0.0.0` or empty: every interface, the panel is reached from the network. `127.0.0.1`: this machine only. A value that is no address: 127.0.0.1, with a line in the log |
| `g_webRoot` | empty | folder with the files of the page (`codemp/game/webadmin/page` of the source), served instead of the built in page, for working on the page: the server puts `index.html` and the files it includes together at every load |

The first three are latched, they take effect at the next map start. Open `http://server:8080/` and log in with
any user name and the password (HTTP basic auth).

## What the page does

- Two modes, switched in the top bar and kept by the browser. Administration: the tabs Players, Config and
  Files, and the map drawn the way the game draws it (see below). Editor: the tabs World and Models, the plain
  view of the map (an image times the light) and its debug views: a marker at every entity, the triangles of the
  map as lines, the light alone or the texture alone.
- Players: list, health, armor, size, team, god mode, kill, kick, ban, follow, first person view with the HUD.
  Right click on the map teleports the selected player.
- World: all entities with their spawn keys. Edit keys and apply, drag or right click to move, rotate, duplicate,
  delete, place new ones with a model preview, export the layout as `.ent`.
- The tools of the editor on top of that: what a class is, with its keys and its spawnflags by name, read from
  the game code. The box of every entity and the lines to what it fires. Position and angles as numbers, a grid,
  moving with keys, putting on the floor, undo and redo. How much of what the engine limits is in use. The
  server refuses to place what would end it.
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

The page is kept as a template, a style sheet and one script file for each part of it
(`codemp/game/webadmin/page`, its `README.md` says what is where). The build puts them together into the one
file a browser gets (`cmake/Modules/BundlePage.cmake`) and compiles that into the game module, so nothing
changed for the browser when the page was split: one request, one script.

The code of the panel in the game module is ten files with one header (`wa_local.h` lists them). It was one
file of 4,000 lines: the functions moved as they were, the ones another file calls lost their `static`, and
every variable stayed with the one file that owns it. Where a file needed a variable of another it asks
through a function now (`WA_Running`, `WA_GravityIsSet`, the `Frame` and `Shutdown` of the entities and the
assets).

The three longest functions were cut up as well, each into the parts it already had: `WA_ApiState` (320
lines) into one function for each list of the answer and for the two parts of a player, `WA_ApiAction` (265
lines) into what is done to the server and what is done to a player, and `frame()` of the page (245 lines)
into one function for each thing a frame draws.

What was checked for the split, on Linux: the page put together by the build and the one the server puts
together for `g_webRoot` are the page as it was before the split, byte for byte (322,968 bytes). Of the 3,900
lines of the one C file all but four are in the new files, once each (the four: a title and a comment that
were no longer true), and the files compile without a warning. After each step the checks of the panel ran
against a private server: 25 scripts with about 230 checks, every route once by hand, the file manager with a
file written, read, renamed and deleted, and the 20 actions of `/api/action`. The answer of `/api/state` was
compared before and after by its keys, their order and the kinds of its values, not byte for byte, since it
changes with every frame. Not built: Windows.

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
| `GET /api/state` | map, scores, players, NPCs, items and `models` as JSON. The page polls this. `itemsRev` is a number for the list of the items as it is, and with `items=<that number>` the list is left out as long as it is the same. An item is `[entity, its place in the list of the items of the game (the `index` of `/api/classes`), eFlags, x, y, z, pitch, yaw, roll]`: every item the clients are sent, also the ghost of a weapon that was taken. An NPC has, besides its name, model, health and place, its `class`, `weapon`, `angles`, `vel` and `f` (the frames and speeds as of a player, eFlags, eFlags2, powerups, the flips, the entity it stands on); for a vehicle the model is the one of its vehicle file and `skin` the skin of that file. A player is `id`, `r` (the number of what stays the same of him), `s` (that part: name, address, team, flags, sabers, model; left out for a page that names the number with `p=<id>.<r>-<id>.<r>`), `f` (35 numbers: health, armor, score, ping, weapon, force, flags 1 dead 2 saber on 4 saber in flight, saberHolstered, saber entity, origin, velocity, yaw, pitch, legs and torso animation, view height, ammo, and from the 22nd on what a client draws him by: the frame the server has his legs at and how fast it runs in frames per 50 msec, the same of his torso (-1 and 0 while the skeleton is not in the animation the row names), `eFlags`, `eFlags2` and `powerups` of his entity, the force powers that are on, `weaponstate`, `saberMove`, the flips 1 legs 2 torso, the entity he stands on, `brokenLimbs`, the saber style he fights in) and `b` (for every blade its length now, where it starts and the way it points). `models`: every other entity the clients draw as a model (props, models of the map that are entities), with its model, scale, origin and angles as the clients have them. `movers`: every brush model the clients draw as an entity (doors, lifts, what turns or can be switched off), as the number of the brush model, or as `[number, x, y, z, pitch, yaw, roll]` when it is not where the map file has it, with two more for one that has them: the frame of its shaders (`EF_SHADER_ANIM`, else -1) and its `model2`. `shots`: what is in the air, each as `[entity, weapon, 1 for the other way to fire it, x, y, z, its speed along the three axes, pitch, yaw, roll, eFlags]` The effects: with `fx=<number>&fxs=<number>` the page says what it has, and `fx` (`rev` and a `list` of the entities that are effects: entity, index of the effect, state, delay and random part of its loop in msec, place, direction) and `fxSeq` with `fxPlay` (what the game played once in the last two seconds: number, msec ago, kind 0 for an `effectTypes_t` or 1 for an index, which, place, direction) are sent only when they differ |
| `GET /api/map` | the `.bsp` of the current map |
| `GET /api/file?name=` | a file below `textures/`, `models/` or `gfx/` (images, md3, glm, gla, cfg, skin), a shader script `shaders/<name>.shader`, or a `.wav` or `.mp3` below `sound/` or `music/` Also an effect file `effects/<name>.efx`, as text |
| `GET /api/shaders` | the names of the shader scripts of the game, in the order the engine reads them in |
| `GET /api/anims` | animation names in `animNumber_t` order |
| `GET /api/classes` | everything that can be spawned, and the items with model and icon |
| `GET /api/entities` | the entities with their keys. One that is made of brushes has `moved` and `turned`: how far its brushes are from where the map file has them. `budget`: entities, models, sounds and effects in use and the most there can be. `brushModels`: how many brush models the map has. `used`: what fired what in the last ten seconds, each as `[number of the event, milliseconds ago, entity that fired, entity that was fired]`, the first entity is the world (1022) for what the panel fired |
| `GET /api/list?kind=` | names the fields of the page offer: `maps`, `effects` and `music` (the files of the game), `npcs` (the types of the `.npc` files), `bots`. `kind=dir&dir=&ext=`: the folders and files of one folder below `sound`, `models`, `music`, `effects` or `maps`, with the endings of `ext` (`.wav,.mp3`) `kind=fxindex`: the effects the map has in use, in the order of the numbers the state feed names them by |
| `GET /api/bans` | the addresses that are banned, as `addip` and `removeip` take them (a `*` for a part that matches anything) |
| `GET /api/waypoints` | the route of the bots: every waypoint with its place, its flags and the numbers of the ones it leads to. `editing`: it was changed and not saved, the bots stand still |
| `POST /api/waypoint` | `op` add (`x`, `y`, `z`, `flags`, `after`: the waypoint it comes behind, none for the end), remove (`index`), flags (`index`, `flags`) or save |
| `GET /api/log?from=` | the log of the game from a place on, `next` is where to ask from the next time. Without `from`, or with a place the panel does not have: its last part |
| `GET /api/perf?since=` | the load of the server, a sample a second, the last 300 kept: `samples` are `[frames, average and longest frame of the game in microseconds, longest wait for a frame in ms, entities, players, average and longest part of this panel in a frame in microseconds]`, `seq` is how many there were, `from` the number of the first one sent. `since`: only the ones from that number on. With them `svFps`, `entityMax`, `maxClients` |
| `GET /api/classdoc` | the entity definitions of the game code: every `QUAKED` comment of its source files as text |
| `GET /api/classkeys` | the keys the game code reads, as text: a line for a class with its name and, each after a tab, `key\|kind\|default`. The first line (`*`) is the keys every entity takes, `*item` the ones of an item |
| `GET /api/export` | the entities in `.ent` format. `placed=1`: only what was placed through the panel, without the worldspawn |
| `GET /api/chat?since=&epoch=` | chat lines after number `since`. Everything if `epoch` is not the current one |
| `POST /api/action` | `action`, `id`, `value`: say, addbot, map, kick, ban, health, armor, scale, kill, god, noclip, schizo, flatulence, disorient, poop, gravity (no `id`), reload (no `id`), playergravity, team, teleport, and the JA+ ones: amsleep, amwake, amprotect, amempower, ammerc, ammindtrick, amghost, amsilence, amunsilence, amslap, amForceAltDim, amUnForceAltDim, amdenyvote, amShowMotd, amrename, ampsay, admgrant, freeze |
| `POST /api/entity` | `op` spawn, update or delete, with `id` and `kv` (one `key=value` per line). `op` use with `id` fires the entity. `op` clear with `kv` `classname=<class>` deletes every entity of that class that was placed through the panel. `map=1` with spawn: it is an entity of the map that comes back (undo), not a placed one. `op` check with `kv` spawns nothing and answers what a spawn of these keys would be refused for (`ok`, `message`, and `key` when a key is what is missing): the page asks it for its drafts. A refused spawn answers with `key` the same way |
| `POST /api/layout` | `op` save keeps the map as it stands for every start of it (`layouts/<map>.txt` in the game folder), `op` forget removes that file. `/api/entities` has `layout`: is one saved, is the map as it has it, and how many entities it would add and take away now |
| `GET /api/config` | the text of `server.cfg`, as UTF-8 |
| `POST /api/config`, `POST /api/config?exec=1` | the body is the new text of `server.cfg` (plain text, not a form). With `exec=1` the server runs `exec server.cfg` afterwards |
| `GET /api/models` | every `.md3` and `.glm` under `models/`. The server reads the folders a few at a frame: `done` says whether the list is complete, the page asks again until it is |
| `GET /api/fs/list?root=&path=` | the files of a folder. `root` is `home` (the game folder in the home path of the server) or `install` (the one of the installation, read only) |
| `GET /api/fs/get?root=&path=[&text=1]` | a file as a download, or as UTF-8 text for the editor |
| `POST /api/fs/put?path=&offset=[&last=1][&text=1]` | the body is the next piece of a file (a request is 64 KB at most). `last=1` puts the file in its place |
| `POST /api/fs/delete?path=`, `/api/fs/rename?path=&to=`, `/api/fs/mkdir?path=` | in the home path only |

## Where it differs from JKAdmin

In `wa_actions.c` the player actions that would bypass JA+ state go through the JA+ code instead, each place
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
  script section "chat", and the say form moved there from the top bar. The chat section in `wa_log.c` and the
  script section are marked "not in JKAdmin". The rest of the page is JKAdmin's.
- A message typed in the panel is turned from UTF-8 into the one byte per character the game uses (Latin-1), so
  umlauts arrive. Characters the game does not have become `?`.
- New: the modes Administration and Editor (`setAppMode` in the page). In the administration the map is drawn
  with the shader scripts of the game, by a renderer of the page (script section "the shader scripts of the
  game"): the page asks `/api/shaders` for the script files, reads them through `/api/file` and keeps the text of
  every shader until a map uses it. A surface is drawn with the stages of its script, in the sort order of the
  game: images, `animMap`, `clampMap`, `$lightmap`, `blendFunc`, `alphaFunc`, `depthFunc`, `depthWrite`,
  `rgbGen` and `alphaGen` (const, wave, vertex, `lightingDiffuse`, `lightingSpecular`), `tcGen` (lightmap, environment), `tcMod` (scroll, scale, rotate,
  stretch, transform, turb), `polygonOffset`, `sort`. A shader without a script is the lightmap times its image,
  a surface without a lightmap is lit by its vertices, both as `tr_shader.cpp` makes them. The sky is the box of
  `skyParms` around the eye and the stages of the sky shader as layers of clouds, as in `tr_sky.cpp`. The
  lightmaps are taken as they are in the map, which is what the game shows with `r_overBrightBits 0`,
  `r_mapOverBrightBits 0` and `r_gamma 1`, the defaults. See-through surfaces are drawn after the players and
  the models. A surface is drawn from the side the game draws it from (`cull`, `setCull`): the front, unless
  its script says the back or both. Seen from outside a room is open that way, as in the game when one leaves
  the map: its walls and its ceiling face inwards. So the view from above shows who is in a building. A click
  on the map goes through what is not drawn (`pickGround` with `oneSided`), a right-click from above lands
  on the floor of the room and not on its roof. `deformVertexes` is done in the vertex shader (`setDeform`,
  `buildSprites`): `wave` and `bulge` move a vertex along its normal, `move` shifts all of them, and the quads
  of `autosprite` and `autosprite2` turn to the viewer as in `tr_shade_calc.cpp`, which is what makes the
  flares of a map round lights and its chains chains from every side. Not done: the deforms `normal`,
  `projectionShadow` and the text ones, the fog of a brush on a surface that is deformed, glow,
  `surfaceSprites`, portals.
  Images outside `textures/`, `models/` and `gfx/` are not handed out by the server, a stage that names one is
  left out. With a game module that has no `/api/shaders` the administration shows the plain view. The rules
  were checked by reading all 3365 shaders of the game through the parser. The picture was looked at in headless
  Chromium on mp/ffa3, where every image a stage names loaded. It was not put next to a picture of the game.
- New: the tools of the editor (script section "the tools of the editor"), each made of what the game code has.
  - The entity definitions. A step of the build (`cmake/Modules/ExtractQuaked.cmake`) collects every `QUAKED`
    comment of `codemp/game` into the module, `/api/classdoc` hands them out. The panel of an entity shows the
    text of its class, a box for every spawnflag that has a name, and offers the keys the text names (and since
    the completion of the keys many more, see below). A class
    is drawn in the colour of its definition, and the class picker shows the first lines as a tip.
  - Views, switched in the bar at the top left of the map: Volumes draws every entity as lines (a trigger or a
    door with the real shape of its brushes, worked out from the planes of the map file, moved and turned as
    the server says the entity is now, another one as the box its class is defined with), Links a line from an entity to every one
    it fires (`target` to `targetname`), Clip the brushes of the map that stop players, NPCs, bots or shots
    and are not seen in the game. What belongs to the selected entity is drawn through the walls, the panel
    lists it with a click to go there.
  - Moving: position and angles as numbers, a grid (1 to 64) for what the mouse places, Shift with the arrow
    keys and Page Up or Down for a step of the grid, "To floor", "Focus". Page Up and Down alone move the camera
    up and down.
  - Undo and redo (Ctrl+Z, Ctrl+Y, 200 steps) for placing, changing and deleting. The server spawns an entity
    anew with every change, so the history follows its number. An entity of the map that is deleted and taken
    back is one of the map again (`map=1`). Deleting a whole class, or an entity without keys, empties the
    history.
  - The budget line: entities, models, sounds and effects in use against the limits of the engine, red from
    90 percent on.
  - The plain view shows a shader whose own name is no image with the image its script names for editors
    (`qer_editorimage`), or the first one of its stages.
- New: the page draws the models of `models` in both modes, on their way from where they were at the last poll:
  the props of the Models tab, cubes, balls and what else is a model entity.
- New: in the administration every model is drawn with the stages of its shaders too (`drawModelScripted`):
  players and NPCs with the shaders of their skin, items, the weapons they hold, props. A surface whose shader
  has no script is its image times the light of the place, as the engine makes it. The light of the place comes
  from the light grid of the map file as in `tr_light.cpp`: the eight samples around the model weighed by how
  near they are, without the ones inside walls, the ambient light times `r_ambientScale` (0.6) with the little
  the engine adds for everything, the directed light and where it comes from. A map without a grid gives the
  same light everywhere. Light styles that flicker and dynamic lights (a saber, a shot) are not in it. A
  surface of a model that is see-through (a sort after the banners) waits until everything solid is drawn
  (`lateModels`) and is then drawn in the order of its sort together with the see-through surfaces of the map,
  the model first where both have the same sort, as the game sorts all of them together. Before that a model
  drawn later behind such a surface wiped it out. Within one sort nothing is ordered by distance, the game
  does not do that either. The colour of the team that the plain view lays over a player is left out here, the
  game has none. The editor draws models as before.
- New: values to pick from (`/api/list`). The Map field of the top bar offers the maps of the server and the
  bot field its bots. In the key table of an entity a value field offers what fits its key: the class names for
  `classname`, the targetnames there are for `target` and the other keys that fire, the effects for a key with
  `fx` in its name, the NPC types for `NPC_type`, the maps for `mapname`, the music files for `music`. A value
  that is a file is completed folder by folder: `noise` below `sound/`, `model` and `model2` below `models/`. The
  field offers what is in the folder typed so far, a folder that is picked shows its own files next.
- New: the tab Routes of the editor, the waypoints the bots walk along (`ai_wpnav.c`). The trail is drawn from
  each waypoint to the next and to the others it leads to, faintly through the walls so that it is seen from
  above. A click selects a waypoint. With Add on, a right-click on the map puts a new one there, behind the
  selected one or at the end, with what a bot does there (walk, jump, duck, snipe or camp, goal, a flag).
  Remove takes the selected one away, Save writes `botroutes/<map>.wnt` into the server folder. The server uses
  the functions of the waypoint commands of the game (`bot_wp_add`, `bot_wp_rem`, `bot_wp_save`), and as with
  those the bots stand still from the first change until the route is saved: which waypoint leads to which is
  only worked out again by the save.
- New: the fly camera, in both modes (the button at the bottom right of the map, or C). The camera of JKAdmin
  looks at a point of the map from above. Flying, it is in the map: W A S D go along and across the line of
  sight, Q and E down and up, the mouse turns the head, the wheel sets the speed, Shift is faster. Leaving it
  puts the point the other camera looks at in front of where one was. Picking, placing and "Focus" work in both.
- New: copy and paste in the World tab. Ctrl+C takes the keys of the selected entity, Ctrl+V places one with them
  where the mouse is on the map (on the grid, as high above the floor as the copied one was), Ctrl+D is the
  Duplicate of the panel. Both go into the history of undo.
- New: doors, lifts and everything else that is made of brushes and is an entity are drawn where the game has
  them, in both modes (`movers` of `/api/state`, `applyMovers`, `moverPlaces`). The map file has such a
  brush model at one place, the game moves it: before this a lift that rests at the bottom was drawn at the
  top (on mp/ffa3 all six of them, 270 to 296 units too high), a door never opened and what was broken or
  switched off stayed. The server lists what the clients draw as a mover (`CG_Mover`: linked, `ET_MOVER`,
  not `EF_NODRAW`, not `SVF_NOCLIENT`) with place and angles from its trajectory, the page puts the surfaces
  of the brush model there and moves them between two polls as it does with the players. A brush model no
  entity shows is not drawn and a click goes through it, one that moved or turned is hit where it is
  (`pickGround` takes the ray to where the map file has the model), so a right-click puts a player on a lift
  that is up. In the editor the surfaces now follow the box of a brush entity that is moved or turned, and a
  deleted one is gone at once. Limits: the fog of a brush is not drawn on one that moved, and a turn of more
  than half a circle between two polls (a tenth of a second) is drawn the short way round.
- New: the switch "Open" in the bar of the debug views of the editor. The plain view of the editor draws every
  surface from both sides, so a map with ceilings is a closed box from above. With "Open" it draws the side
  the game draws, a click goes through the rest, and entities can be placed into a room from above.
- Changed: the bar at the top holds together in a narrow window. Below about 1000 px "Add bot" broke into two
  lines and the fields at the right ran out of the window. Now what only tells goes first (the game type and
  the time below 1240 px, the name of the server and of the panel below 1080 px), the fields get shorter, and
  nothing wraps. Tried at 1440, 1280, 1100, 1000, 900 and 800 px (`steps_topbar.js`).
- Changed: the bar of the debug views and the legend of the keys share the top of the map view (`#viewTop`).
  The legend goes below the bar when both do not fit next to each other, they lay over each other before.
- New: "Fire" in the panel of an entity (`op` use). It does to the entity what a trigger does that has it as
  its target: a door opens, a lift goes up, a trigger fires its targets, something that breaks breaks. The
  world stands in for the trigger, as with a `trigger_always`, so what only works on the player who fired it
  (a teleporter, a give) does nothing. With several entities selected all of them are fired. It is not a
  change of the map and no step of undo. The server refuses an entity without a use function, one that is
  inactive, and an NPC (a vehicle takes whoever uses it on board without asking whether that is a player).
  The use functions of the game were read for this: every one that reaches into the client of what fired it
  asks first whether there is one.
- New: a trigger can be a box of its own (`InitTrigger` in `g_trigger.c`, the one change to a file of the game
  for this). A trigger of the game is made of brushes of the map, so the editor could only place one in a
  shape the map already had: a second teleporter the size of the first, where the first is. A trigger that
  names no brush model and has the keys `mins` and `maxs` is now a box of that size at its origin, and it
  works as any other: `trigger_multiple`, `trigger_once`, `trigger_push`, `trigger_teleport`, `trigger_hurt`
  and the rest that go through `InitTrigger` (not `trigger_asteroid_field`, which asks for its brushes
  itself). No map file is read differently: a trigger without a model ended the server before. Such a box
  does not turn (a trigger takes its angles as the way it pushes) and only the server works it: a client
  predicts its own way only through triggers that are brush models, so the push of a box comes with the next
  snapshot instead of at once. In the editor "Place" gives a trigger class a box of 128 x 128 x 96 that stands
  on the floor, the Volumes view draws it, and the panel of the entity has a "Box" row with its size (the
  middle of the box stays where it is; for another middle the two keys are edited). A `trigger_push` and a
  `trigger_teleport` are placed with a `target_position` of their own, as one step: 256 above for the push,
  192 towards the viewer for the teleport. The game frees a `trigger_push` without a target in its first
  frame, before a key could be added. The server refuses a box whose `mins` are not below its `maxs`, half a
  box, and one wider than 32768. A layout keeps such a trigger like every other placed entity.
- New: the renderer of the page is measured against the game, and ported from the engine function by function
  where it differed (the user: "The renderer in the webui, still is not correct ... Do a 1:1 copy of the
  render", and that mountains and rocks were missing on hoth). Until this the page was written from reading
  the engine and judged by eye. Now `tools/refshot.sh` (outside of this tree) runs the real client with
  rd-vanilla and every cvar at its default as a listen server that also serves the page, puts that client as
  a spectator at spots of a map, reads back where its eye is (`/api/state?view=`), and has the game and the
  page take a picture from the same spot, both on the graphics card. The page has a view for it
  (`?ref=<width>x<height>`: the map alone, the projection of the game, no smoothing of edges) and a mask of
  what animates on its own (`refMask`), which two pictures never catch at the same moment. The numbers are
  the mean difference of a pixel (of 255) and the share of pixels that are off by more than 16. One
  departure from the defaults: `r_ext_compress_textures 0`, because what the driver makes of a compressed
  texture can not be made in a browser.
  The first table, six views on each of six maps: the plain map was close (mean 1 to 2), and whole things
  were missing. What was done about it, each from the function of the engine or the client game that does it:
  - The projection (`gameProjection` in `js/camera.js`): 80 degrees across whatever the shape of the picture,
    the angle up and down from that shape (`CG_CalcFov`), near plane 4, and the far plane of `SetFarClip`
    (the farthest corner, at least 2048, at most `distancecull` of the worldspawn times 1.732). The page had
    55 degrees up and down everywhere. The view from above keeps a far plane behind all of the map.
  - The sky portal (`js/world-ents.js`, `CG_DrawSkyBoxPortal`): the room of a `misc_skyportal` is drawn first,
    from the origin of the portal, then the depth is cleared and the map is drawn over it without its sky.
    Which part of the map is that room comes from the visibility data of the map file. Those are the
    mountains of mp/siege_hoth.
  - The models of `misc_model_static` (`SP_misc_model_static`, `CG_DrawMiscStaticModels`): the client game
    spawns them itself from the entities of the map file, the server never has them. 63 on mp/siege_hoth, 189
    on mp/siege_korriban. Placed and culled as the client does it, lit by the light grid.
  - Textures (`js/gl.js`, `js/map.js`; `Upload32`, `R_MipMap`): mip-nearest filtering with anisotropy 16, the
    mip levels built as the engine builds them (four pixels made one, never rounded up) by a small program on
    the graphics card, lightmaps without mipmaps.
  - Curved surfaces (`js/bsp.js`; `R_SubdividePatchToGrid`, `MakeMeshNormals`, the stitching, and the level
    of detail of `RB_SurfaceGrid` per frame). The engine's own code, built into a small program, gives the
    same grids float for float on all 23 maps of the game.
  - Which surface is drawn is decided as the engine decides it and no longer by the name of its shader.
  - Flares (`js/flares.js`; `RB_SurfaceFlare`, `RB_TestZFlare`): a quad that faces the viewer, sized by
    `alphaGen portal` of its shader, shown when nothing is in front of it, which the page asks the graphics
    card with a query whose answer is one or two frames old.
  - Fog (`js/scripts-world.js`, `js/scripts-programs.js`; `R_LoadFogs`, `RB_FogPass`, `RB_CalcFogTexCoords`,
    and the fog of OpenGL for the fog of a whole map): per pixel over the depth, every stage fading to the
    colour the engine's table gives it, the fog of a brush as one more pass with the fog image, also on
    models, movers and effects in a fog of the whole map. The frame begins as the colour of that fog.
  - Lightmap and image of a wall are one pass with two images (`CollapseMultitexture`) and no longer two
    passes. That is what makes most pixels the very value of the game, and it removed a striped line on a
    floor of mp/ctf4 where two triangles of the map file overlap.
  - The order of drawing: the sort values of the engine, and within one value the order in which the engine
    registers the shaders of a map.
  - The parser of shader scripts reads every word the scripts of the game have but one
    (`deformVertexes projectionShadow`); `tools/panelshot/shader_keywords.js` lists what it drops.
  Not done in this step, and seen in the pictures: weather (the snow of mp/ctf2), items and pickups as the
  game draws them, players, the HUD. No visibility culling: from outside the hull of a map the game shows
  only what is seen from there and the page shows rooms through their missing outer walls.
- New: the state says what a client draws players, items, movers and shots by (`wa_state.c`). The page drew
  them from less than a client has: an animation without the frame it is at, an item as a class name and a
  place. Now:
  - A player and an NPC have the frame the server has their skeleton at, for the legs and for the torso, and
    how fast it runs (`WA_StateBoneFrame`: the server sets the same animations on its own copy of the skeleton
    as a client does, `G_UpdateClientAnims`). The frame is left out while the skeleton is not in the animation
    the row names: the player state gets a new one with a move, the skeleton later in the frame. On a private
    server with bots that was 1 or 2 of 40 looks.
  - A player has the flags of his entity (`eFlags`, `eFlags2`), his powerups and the force powers that are
    on, the state of his weapon, his saber move, the flips, what he stands on, his broken limbs and the saber
    style he fights in. What stays of him has the two RGB colours of his sabers (`saberRgb`, as `c3` and `c4`
    of the client info, when `jp_allowRGBsaber` is 1).
  - An NPC has its class, weapon, angles and speed, and a vehicle the model and the skin of its vehicle file:
    the server tells the clients only the name of a vehicle with a `$` in front, and they look it up
    (`CG_G2AnimEntModelLoad`). The walker of mp/siege_hoth came as `$atst_vehicle`, which is no model.
  - An item is a row: its entity, its place in the list of the items of the game, the flags of its entity,
    its place and its angles. `/api/classes` says that place for every class (`index`) and the second model
    some items have (`model2`). A weapon or a powerup that was taken stays in the list with
    `EF_ITEMPLACEHOLDER`: the game draws a ghost of it there. The page drew it as if it lay there, and still
    does until the items are drawn as `CG_Item` draws them.
  - A mover says the frame of its shaders and its `model2` when it has them, a shot its angles and the
    flags of its entity.
  A player is about 220 bytes ten times a second now, he was about 170. `tools/panelshot/steps_feed.js` (19
  checks): the frames against the animation files the page reads, a weapon placed through the panel in the
  list by its place, a `func_static` with `model2` and a switched shader, a rocket.
- New: the editor draws the map as the game does, unless its plain view is chosen (the user: "when in editor mode
  the map is really bright than in normal administration mode", and "add the as in the game and make it
  default"). The editor had only its plain view: an image times the light for every surface, with twice the
  light of the map so that dark corners can be worked in, and a grey haze with the distance. That is a third
  brighter than the game on mp/ffa3, and it has no shader scripts, no sky and no fog. The choice in the bar of
  the views now begins with "As in the game", which is the renderer of the administration (`gameLook()` in
  `js/pick.js`: the world, the models and which side of a surface is drawn). The three plain views are still
  there: "Texture and light", "Light only", "Texture only". "Open" works in both: without it the editor draws
  both sides of every surface, so that a room is closed from outside, with it only the side the game draws
  (for a day it was greyed out in the game look, which then always had the sides of the game; the user asked
  why). The wireframe lies over both, in the game look drawn forward by a hair of the depth. Until the shader scripts are read the game look is the plain view with
  the light as it is and without the haze.
- New: the effects of the game are drawn (`js/fx-parse.js`, `fx-run.js`, `fx-draw.js`, `fx-weapons.js`; the
  user: "Effects/FXes don't show up in the panel"). Nothing of them was in the page: the fire of an
  `fx_runner`, an explosion, what a blaster bolt leaves on a wall were not there.
  - What the server tells (`WA_StateEffects` in `wa_state.c`): the entities that are effects (`ET_FX`:
    `fx_runner`, `misc_blackhole`) with the effect, the state, the delay and where they are, and what the
    game played once through `G_PlayEffect` and `G_PlayEffectID` in the last two seconds. For the second a
    line in each of those two functions of `g_utils.c` tells the panel (`WebAdmin_Effect`, a ring of 64):
    the game sends such an effect to clients only, as an event. Both parts are left out of a poll when the
    page has them, so a poll in which nothing happened is no bigger than before (663 bytes with four bots).
  - The files: the page reads an effect file (`.efx`) through `/api/file` the first time it is needed and
    parses it with the rules of the engine (`FxTemplate.cpp`, `GenericParser2`): counts, delays, ranges, the
    blocks for size, colour and alpha with their flags.
  - Playing (`fx-run.js`): as `CFxScheduler` does it. Where a primitive is made and how it moves (through the
    axis of the effect, on a sphere or a cylinder, with gravity), how its size, colour and alpha change over
    its life, emitters that play an effect as they move, effects at the death of a primitive, effects that
    play other effects. An entity loops as `CG_FX` loops it, and a state that means once plays once for every
    change. No more than 3000 primitives live at a time, counts thin out from 2000 on, and nothing is played
    further than 8192 units from the camera.
  - Drawing (`fx-draw.js`): particles, oriented particles, lines, tails, cylinders and electricity as quads
    with the shader scripts of the game, through the same stage renderer as the map (`materialFor`,
    `drawStage`): the engine colours a particle by the vertex colour of a normal shader. One buffer that is
    filled every frame, a batch for every shader. After the see-through surfaces of the map, with the depth
    test and without writing the depth.
  - Weapons (`fx-weapons.js`): the server names no effect for a shot, a client makes them itself. The page
    has a table by weapon for the muzzle, the shot in the air and what it leaves on a wall or on a player,
    and finds the wall with a ray against the map (four rays a poll at most). The glow quads from before
    stay for when Effects is off in the editor, the shader scripts are not read yet, or a file is missing.
  - The view toggle "Effects" of the editor switches them off there. The administration always has them.
  - Not drawn: sounds, lights, camera shakes, decals and screen flashes of an effect, the models of emitters,
    wind, forks of electricity; particles do not collide with the map. And what a client plays on its own
    that is neither an effect entity, nor played by the server, nor a missile: saber clashes, the laser of a
    trip mine, force powers, jetpacks, the beams of the disruptor and of the concussion rifle's second fire.
    A hitscan weapon has no shot the feed could tell of.
- New: everything new in the World tab is a draft first (`js/editor-drafts.js`; the user: "The draft is not a
  bad idea, make this for all entities"). A class or a template placed with a right-click, the copy that
  Duplicate or a paste makes, and a `trigger_push` or `trigger_teleport` with the `target_position` it is aimed
  at exist only in the page: the server gets no spawn. A draft is in the entity list (first, numbered "new")
  and on the map as a diamond in the colour of its class made half white, turning over a ring. It is selected,
  moved, turned, resized, duplicated and copied like an entity, alone or together with placed ones: in a
  mixed selection the drafts change in the page and the placed ones on the server, and only the placed ones
  are in the step of the history. A draft has no Apply: what is typed into its keys is taken when the
  selection changes, when it is moved or placed, and before any request to the server. After a draft is made
  and after its keys change the page asks the server what a spawn would be refused for (`op` check, which
  spawns nothing): a class that needs a key gets that key as an empty row, a single new draft with the cursor
  in it, and its panel and its name on the map say what is missing.
  "Place" in the panel of a draft sends it, with several drafts selected it reads "Place N", and "Place all"
  above the list sends every draft. Drafts that a sent draft names as its targets go with it, and what is
  aimed at is sent before what aims at it, because the game frees a `trigger_push` whose target is not there
  in its first frame. All that one Place puts on the server is one step of undo. A draft the server refuses
  stays a draft, selected, with the reason in its panel; one that aims at a draft that was not placed is not
  sent. "Discard" throws drafts away, "Discard all" asks once when there is more than one. Drafts belong to
  the map they were made on and are lost with a reload of the page.
  What goes to the server at once as before: undo and redo, the import of a `.ent` file, the layout, and
  Place of the Models tab. Their keys are complete, there is nothing to fill in first. What is not in a draft:
  an export or a saved layout, which are made on the server.
- New: the names of the entities float above their markers (`js/editor-labels.js`, view toggle "Names"): the
  class, the targetname in quotes, and `[draft]` or `[draft: needs fxFile]` for a draft. Every draft and what
  is selected always have a name (64 at most), besides those the 40 entities nearest to the camera that pass
  the filter of the list, are in front of the camera and within reach (1500 units, more when the camera is
  far out). A name that would lie over one already placed is left out. Which entities have one is worked out
  every 150 ms without the screen, a frame only projects those and moves their lines, which are made once and
  used again. A name has no test against the walls: it shows where its marker is hidden. Not in the
  administration.
- New, and since the entry above the way of every new entity: a class that can not be placed bare is opened as a
  draft (first as one draft at a time, with ``openDraft`, `draftOp`, `placeDraft` in
  `js/editor-entities.js`). Place gives a class its name and where it is, and the server refuses a class
  without a key it can not be without: an effect without its file, a speaker without its sound, a class of
  brushes without its model. That left such a class with no way into the map but a template or an import
  (the log of the user's server had a `fx_runner` tried four times in a row). The answer of a refused spawn
  now names the key it was refused for (`key` in the answer of `/api/entity`), and the page opens the entity
  as a draft with that key in its table and the cursor in its value. A draft is in the list (as "new") and on
  the map like an entity, its keys are edited and completed like those of one, it is moved, turned and
  resized like one, and "Place" in its panel sends it; "Discard" throws it away. The server knows nothing of
  it until then, so nothing of it is in the history either: the entity it becomes is one step. Place that is
  refused again leaves the draft as it was typed, with the next key that is missing. One draft at a time, and
  it belongs to the map it was begun on. What is refused for something no key helps with (a `misc_bsp`, a
  full map) is only refused, as before.
- New: the `model` key of a class that is made of brushes offers the brush models of the map (`*1`, `*2`,
  ...), each with the entities that have it now. A disabled button looks disabled.
- Fixed: a `misc_bsp` placed in the editor ended the server. It happened to the user on 2026-10-08: the class
  loads another `.bsp` file into the map, placed without keys it asks for `maps/.bsp`, and a file the engine
  can not load is an error that a dedicated server does not survive. That error comes from the engine and not
  from the spawn function, so reading the spawn functions for `trap->Error` had not found it. The panel does
  not place a `misc_bsp` at all now: a file that is there can still bring more entities than the map has room
  for. And it can not work on a running map for the players either: a client loads such a file only while
  it loads the map (`CG_RegisterGraphics`), so one that is connected already gets an entity with a model its
  collision map does not have, and the engine drops it (`CM_InlineModel: bad number`). The refusal says so.
  A `misc_bsp` could only come with the start of a map, before any client loads it. Found by going through every class again, this time with what it calls (two deep) and with every call
  that can end the server: an `info_siege_radaricon` without `icon` is refused, and a `misc_weapon_shooter`
  whose `weapon` is no weapon or one without an item (it registers the item and ends the server when there is
  none). A `fx_runner` without `fxFile` frees itself; it is refused with that reason instead of "the game did
  not keep it", which is now worded for what it means.
- New: the memory of the game is a budget of the editor. Every spawn takes memory for the text of its keys
  from a pool the game gives back only when the map starts (`G_Alloc`, 4 MB here), every move of an entity in
  the editor is a spawn, and the allocation that does not fit ends the server. The line with the budgets
  shows how much is used ("Memory 1%", `memory` in the `budget` of `/api/entities`, from `G_MemoryLeft` in
  `g_mem.c`), and the server refuses a spawn when less than 256 KB are left and says that a start of the map
  frees it. A map takes about 20 KB when it starts and a spawn 30 to 100 bytes, so this is far away, but it
  is not out of reach for a script.
- Fixed: a model whose name begins with a `/` is found by the page as the game finds it (two classes of the
  game name their model that way and the page got a 400 for it).
- New: the keys are completed while they are typed (`js/editor-complete.js`, asked for by the user: "add auto
  completion for keys"). The key field of a row had the list of the browser with the keys a definition writes
  in quotes, which for many classes is none (`trigger_push` writes `wait - how long ...`). Now a list opens
  under the field with every key the entity can have and does not have yet, each with what is known about
  it, from four places, the nearest first: what the definition of the class says about a key (both ways of
  writing are read, a spawnflag is not taken for a key), what the spawn function of the class reads, what
  other entities of the class have on this map (with a value one of them has), and the keys every entity
  takes. Typing narrows the list to what begins with the typed text, then what has it in its middle. The
  arrows walk it, Enter or a click takes a key and goes on to the field of its value, Tab takes the nearest
  one, Escape closes it. The field of a value says in its tip what its key is for and shows the default of the
  key while it is empty.
  The keys of the code come from the build: `cmake/Modules/ExtractSpawnKeys.cmake` looks for the calls
  `G_SpawnString`, `G_SpawnInt`, `G_SpawnFloat`, `G_SpawnVector` and `G_SpawnBoolean` in the function the table
  of the classes names for a class and in the functions that one calls (`InitTrigger`, `InitMover`, three
  deep), with the kind of the value and the default the call has: 133 classes. The keys every entity takes
  are the table of the fields in `g_spawn.c`, handed out at run time (`G_SpawnFieldInfo`, 84 keys).
  `/api/classkeys` is both. What this does not know: a key that is read some other way (a field that a class
  uses for something of its own is offered as what it is for every entity), and what a value may be.
- New: the throw of a push is drawn (`pushArc`, `drawArcs` in `js/editor-lines.js`). With the Links view on,
  every `trigger_push` and `target_push` that throws an arc has that arc as a line: from its middle on the way
  whose top is its target, worked out as the game does it (`AimAtTarget`: the time to the top from the height
  and the gravity, the speed across from that time). For the selected push, and for the pushes that are aimed
  at the selected entity, the way is followed on down until it meets the map, with a cross there; under a roof
  it ends at the roof, which is where a player would hit his head. The panel of a push says the same in
  numbers: how far up and across, the time to the top, where it meets the map. While a target is dragged the
  arc follows it. A push that goes straight (`LINEAR`, `RELATIVE`, `CONSTANT`) has no arc. A push whose target
  is not above it has no throw: a red line, a warning in its panel and a line in "Entities to look at".
- Fixed: a push throws to where its target is now (`G_PushAimAgain` in `g_trigger.c`, called for every entity
  the panel spawns). The game works the throw out once, in the first frame of the push, so a target moved in
  the editor changed nothing until the map started again, and a layout that moved the target of a jump pad of
  the map left that pad throwing to the old place. What is aimed at a placed or changed entity is aimed again.
  Not when the new place has no throw (a target that is not above the push): the game frees such a push, and
  moving a target should not take the push away, so it keeps the throw it had and the report names it.
- Fixed in the game: a push whose target is below it (`AimAtTarget`). The time to the top is a root, and of a
  negative height that is no number: the push stayed and gave everybody who touched it a speed that is no
  number. Only a target at exactly the height of the push ended it. Now one below does too.
- New: the faces of a trigger box are dragged with the mouse (`boxHandles`, `boxDragMove` in
  `js/editor-move.js`). A selected trigger that is a box has a yellow handle in the middle of each of its six
  faces, drawn through the walls and as big on the screen wherever it is; the one under the mouse is white. A
  handle dragged with the left button moves its face along its axis, to the point of that line the ray of the
  mouse comes closest to, on the grid of the editor. The other five faces and the origin stay, so a box can be
  fitted into a doorway side by side. While it is dragged the box is drawn where it will be and the "Box" row
  says its size; nothing is sent before the button is let go, and then it is one change and one step of undo.
  A face stops one unit before the one across. The box of such a trigger is drawn while it is selected, also
  with the Volumes view off. Several selected entities have no handles (they are moved, not shaped), and with
  the fly camera a drag that starts on a handle moves the face and not the head.
- New: the layout of a map (`WA_ApiLayout`, `WA_LayoutText`, `WA_ReloadSpawn` in `wa_entities.c`). What the
  editor did to a map was lost with every change of the map and every start of the server, only "Reload map"
  carried the placed entities over. "Save layout" in the World tab keeps the map as it stands in
  `layouts/<map>.txt`, and the server does that to the map again whenever it starts, with the panel on or off:
  the entities that are there and that the map file does not bring are spawned (the placed ones, and the ones
  of the map that were changed), and the entities of the map file that were deleted or changed are taken
  away. For that the server keeps the keys every entity of the map file came with, and knows of each entity
  whether it still is one of them. Keys that are put back to what the map file has, or a delete that is
  undone, make an entity that one again. An entity that the game frees by itself (something that broke) is
  not a change. The line above the buttons says how it stands: none, not saved, saved, changed since.
  "Forget" removes the file, the map is its own again with the next start. The file is text and can be
  changed in the Files tab, what is in it goes through the same checks as an entity that is placed by hand.
  An entity keeps the origin its keys have unless it is somewhere else by now, as a prop that was pushed.
  "Reload map" writes the same text for the one start that follows, so it carries what was deleted or
  changed of the map as well now. Limits: an entity of the map file is known by its keys, so a layout made
  for one version of a map takes nothing away in another whose entities have other keys (it says how many it
  did not find), and two entities with the very same keys are one as good as the other.
- New: an arrow at every entity whose keys say which way it looks (`drawFacing`, with the markers): where a
  player looks at a start point, where a shooter fires, the way a door opens. `angle` and `angles` are read
  as the game reads them, -1 and -2 are up and down. The arrow of the selected entity is longer and in its
  colour, and follows the turn buttons.
- New: "Entities to look at" in the report of the tab Map (`entityProblems`). Red, it does not work as it is:
  a target key (`target` to `target6`, `opentarget`, `closetarget`, `paintarget`, `deathtarget`) that names
  no entity, an entity outside of the map, no `info_player_deathmatch`, capture the flag without a flag.
  Yellow, it may be meant or a script may do what is missing: a `func_door`, a `func_usable` or a `target_`
  entity with a name that nothing fires (such a door never opens), a brush class without a brush model in
  its keys, a game type without its own start points. A click on a line selects the entity in the World tab
  and brings the camera to it. The twenty maps of the game for free for all, capture the flag and duel give
  no line at all, the three siege maps a few yellow ones for relays that the rules of the siege fire by name.
- New: what a surface is made of, in the tab Map of the editor (`inspectSurface`, `renderSurface`). A click on
  the map shows the surface under it: the name of its shader and the script of it as the game reads it, or
  that it has none and is its image times the light; whether it is a face of a brush, a patch or the triangles
  of a model of the map; the brush model it belongs to and the entity that has that one; its lightmap and its
  fog; what it is for the game (the material that says how steps and hits sound, and flags like slick or no
  marks) and what the brush behind it holds (solid, water, the clips); and how the page draws it (stages, sort,
  side, deforms). Its triangles are outlined. The page had all of this already, it only had to remember which
  surface a triangle of the map is from. What is never drawn (the sky, nodraw, the clip brushes themselves)
  has no triangles and can not be clicked.
- New: what fires what lights up in the editor (`WebAdmin_Used`, `used` of `/api/entities`, `takeFired`). The
  game tells the panel every time an entity fires one of its targets (one line in `G_UseTargets2`), the last
  64 of them are kept. With "Links" on, the line from the one to the other turns yellow for three seconds,
  with "Volumes" the box of the one that was fired. So a trigger that a player walks into, a button, a chain
  of relays or a click on "Fire" can be followed while it happens. Only what goes through a target is told:
  a door that its own trigger opens when somebody walks up to it is not.
- New: several entities selected at once in the World tab. Ctrl and a click on a marker adds an entity to the
  selected one or takes it out again, a click without Ctrl or Escape ends it. Moving (the drag, the right-click,
  the fields of the position, "To floor", Shift and the keys) takes the others along by the same way, Delete,
  Duplicate, Ctrl+C and Ctrl+V take all of them, and a paste keeps where they stand to each other. Each of
  these is one step of undo. The two turn buttons turn all of them together, by 15 degrees around the middle
  of where they stand, each as far as the others (what is made of brushes stays: its angle is the way a door
  opens). The keys of the panel stay those of the one that was clicked first, and "Apply" changes only that
  one. The server knows nothing of it: the page sends one change for each
  entity, so one of them can be refused while the others are made, and the message says how many.
  Shift and a drag draws a rectangle and selects every entity whose marker is in it, also the ones behind a
  wall. With Ctrl they are added to what is selected. The one nearest to where the drag began is the one
  the panel shows.
- New: stored views. Shift and a digit from 1 to 9 stores where the camera is, around a point or flying, the
  digit alone goes back there. The browser keeps them for each map.
- New: the button Picture at the bottom right saves what the map view shows as a PNG.
- New: the tab Map of the editor, a report of the map that runs. What its file holds (size, extent, surfaces,
  triangles, vertices, shaders, lightmaps, brushes, brush models, the shader of its sky). What is missing: a
  shader a surface names that is neither a script nor an image, and every image a stage of a shader names that
  the server does not have. The entities by class with their number, a click shows a class in the World tab.
- New: the tab Log of the administration, the log of the game as it grows (who came and went, kills, chat, the
  commands of admins), with a filter for lines. The engine writes the file of the log in blocks unless
  `g_logSync` is set, so the end of the file is behind: the game hands every line to the panel where it writes
  it (`WebAdmin_Log`, one line in `G_LogPrintf`), which keeps the last 64 KB. The module is loaded anew with
  every map, so the view of a new map starts with the end of the file as the map before left it. The whole
  file is `games.log` in the Files tab.
- New: fog in the administration, where the camera is in the map (flying, or through the eyes of a player; from
  above it would hide the map). The page reads the fogs of the map file and which one a surface is in, and takes
  colour and depth from `fogParms` of the fog shader. The fog of the whole map is what the engine does with the
  fog of OpenGL: every stage fades to the colour of the fog by `exp( -( d * 2.354 / depth )^2 )`, a stage that
  adds fades to black and one that multiplies to white. The fog of a brush (a pit) is a pass of its own over the
  surfaces in it, as `RB_FogPass`: the colour of the fog, the thicker the further the line of sight runs through
  the fog, cut at the side of the brush the fog ends at. Only shaders with a sort up to see-through are fogged,
  and `noglfog` is read, as in the engine. Models are in the fog of the whole map, the fog of a brush is not
  drawn on them, and the sky is not fogged.
- A shader script or an image the server did not answer for is asked for again (the server is away for a moment
  when the map changes), a few times, before it counts as missing.
- New: in the World tab. "Export placed" downloads only what was placed through the panel, "Import .ent" places
  every entity of such a file, one after the other, and says how many the server refused and why. The
  worldspawn and what is made of brushes of the map it came from are left out. An import empties the history of
  undo. "Template" in the panel of an entity keeps its keys under a name in the browser: the name is offered in
  the class search as `@name` and is placed like a class, Shift and a click there takes it away. At the bottom
  of the map the place under the mouse is shown, on the grid, and how far it is from the selected entity.
- The panel of a selected player is a column at the right edge of the map. JKAdmin has it across the bottom,
  where with the buttons of JA+ in it it took half of the height of the map view and lay over what one wanted
  to click on.
- New: what is in the air is drawn, in both modes (`shots` of `/api/state`, `drawShots`): the shots, rockets
  and grenades the clients draw as missiles. The page has none of the effects of the game, so a bolt is a
  streak of light in about the colour of its effect, as long and as thick as the weapon makes it, and what the
  game draws as a model is that model: a rocket, a thermal detonator, a det pack. They move between two polls
  by where they were, a new one comes from where its speed says it was. A saber that was thrown was drawn
  before, but only as its hilt, upright and without a blade: the server has it among the models then. Now the
  page knows whose saber an entity is (`saberEnt` of a player), lays it flat as the client does, lets it turn
  its 800 degrees a second between the polls and gives it the blade of its owner. Where a shot ends there is
  a flash for a moment (`flashes`): the page has no explosion of the game to show, so what was in the air at
  one poll and is gone at the next lights up about where it must have hit, a spark for a bolt, a ball of 35
  to 90 units for half a second for a rocket, a grenade, a det pack and the shots that burst. Not drawn: the
  trail of anything, the real effects, and what only an effect shows (the beam of a disruptor).
- Changed: "Fire" also fires a `misc_weapon_shooter`. The game lends that class a client for the weapon code,
  and the first guard took everything with a client for an NPC. It asks for the NPC itself now.
- New: the tab Load of the administration (`/api/perf`, `WA_PerfFrameBegin`, `WebAdmin_FrameEnd`). Six
  values, one a second, each with a graph of the last five minutes: how long a frame of the game takes on
  average and at most, how much of that is this panel (`WA_PerfPanelDone`, from the start of a frame to
  the end of `WebAdmin_Frame`), how many frames the game got, the longest wait from one frame to the next, the
  entities in use and the players. The game module measures itself from the start of `G_RunFrame` to its
  end with a clock of the system (`clock_gettime`, `QueryPerformanceCounter` on Windows), since a frame is
  done in a fraction of a millisecond and the precision timer of the engine only counts on Windows. The
  engine around the game is not in the frame time, it shows in the wait between two frames. The work of the
  panel is in it: a map or a model handed out to a browser is sent from inside a frame, and the graph shows
  that as peaks when a page loads. A value turns yellow when the game got less than nine tenths of its
  frames, waited twice as long as `sv_fps` says, or a frame took longer than the time there is for one. The
  numbers start anew with every map. The graph of the frame time lets the few highest peaks run over its
  top, or one long frame at the start of a map would flatten the rest.
- New: the tab Bans of the administration. It lists the addresses that may not connect (`G_BanListEntry`, the
  list the game keeps in `banIPs.txt`), lifts a ban and takes a new one by its address, with a `*` for any
  number. The actions `banip` and `unban` run `addip` and `removeip` of the game, only digits, dots and `*`
  go into the line.
- New: "Play" next to a key of an entity that names something to hear or see. A sound (`noise`, `music`) is
  played in the browser, from the file of the game. An effect (a key with `fx` in its name) is played once in
  the game where the entity is (action `effect`, and `sound` does the same for a sound), so it is seen by who is
  in the game, not in the panel. The server refuses it when the name would need a place among the config
  strings and none is left.
- New: "Set" in the Routes tab gives the selected waypoint what a bot does there (`op` flags).
- New: the panel of a player says who that is for an admin: how long he has been in the game, the address he
  connects from (without the port, none for a bot) and the client plugin of JA+ he has or that he has none
  (`address`, `minutes`, `plugin` of a player in `/api/state`).
- New: the server refuses to place what would end it (`WA_SpawnRefused`): a class made of brushes (`trigger_`,
  `func_`) without a `model` key that names a brush model of the map (a trigger may have a box instead, see
  above), the classes whose spawn function raises
  an error without a key (`target_speaker`, `target_play_music`, `target_level_change`,
  `trigger_lightningstrike`, `misc_siege_item`, `misc_model_breakable`, `trigger_shipboundary`,
  `trigger_hyperspace`, later `info_siege_radaricon` and `fx_runner`), a `misc_bsp`, a `misc_weapon_shooter` with a
  weapon that has no item, an entity when fewer than 64 are left for the game or the memory of the game is
  nearly used up, and a model, sound or effect when its kind has no place left. The answer says which rule
  it was.
- Fixed: a `func_plat` or a `func_door` that the panel deletes or changes takes its own trigger along
  (`WA_FreeEntity`). Such a mover makes a trigger when it spawns and the game never frees a mover, so the
  trigger stayed: every move of a lift left one more behind, and it named an entity number that the next
  entity gets.
- Fixed: a team of movers (doors with the same `team` key, which open together) stays whole when the panel
  deletes or changes one of its parts (`WA_LeaveTeam`, `WA_TakeTeamPlace`). The game builds the chain of a
  team once at the start of the map and never frees a mover. Deleted through the panel, the first part of a
  team left the others without the one that moves them, and any part stayed in the chain, so the team went
  on moving and linking whatever entity got its number. An edited part (an edit spawns a new entity) was no
  longer one of the team. Now a deleted part is taken out of the chain, the next one leads when it was the
  first (it takes over the targetname, the trigger and what the team was about to do), and an edited part
  takes the place of the old one unless the edit gave it another team.
- Changed: the state feed is smaller and asked for less often. The list of the items is more than a third of
  an answer and the same from one poll to the next unless something was picked up or came back: it has a
  number now (`itemsRev`, a hash of its text), and a page that has the list of that number is not sent it
  again. The same for a player, who was about a kilobyte ten times a second: who he is and what he has stays
  the same nearly always and is sent with a number and then only when it changed, what changes all the time
  comes as two rows of numbers, about 150 bytes (`expandPlayers` makes the one object of it that the rest of
  the page reads). With three bots an answer went from about 7,500 to about 1,000 bytes. And the page asks once a second and not ten times while the map is not looked at: the tab of the
  browser is in the back, or the config, the files or the log lie over the map.
- Fixed: the map file is read as it is sent as well (`/api/map`), and the panel no longer keeps a copy of
  it. It read the whole `.bsp` in one go at the first question an editor has after a map started, 7 to 20 MB,
  and kept it, only to hand it out and to know how many brush models there are. That number comes from the
  head of the file now. The longest frame of the game after a map start with an editor open went from 8.6 to
  1.5 ms on mp/ffa3 and from 12.4 to 2.3 ms on mp/siege_hoth.
- Fixed: a file of the game is read as it is sent (`/api/file`, `WA_FileMore`): at most 256 KB for a
  connection and 512 KB for all of them in one frame, and another connection is the first each frame. It was
  read in one go before, and the tab Load showed what that costs: the animations of the players, 11.8 MB out
  of a pk3, made a frame of 30 ms every time a page was opened. Now the longest frame while six such files
  go out at once is under 2 ms, and the file takes half a second.
- Fixed: a file of the game that the page asked for while the server changed the map is asked for again
  (`fetchGameFile`, every three seconds for half a minute). The server does not answer during a change, and
  what was on its way just then was lost for good: a player whose model was being read stayed the plain
  figure until the page was loaded again. An answer that says the file is not there is still final.
- New: the JA+ admin commands that work on a player (`WA_JpAction`). The panel does not copy them, it sends the
  same line an rcon admin would type on the server console (`amsleep 3`, `amrename 3 "name"`), so the rules,
  texts and logs are those of JA+. The command runs in the next frame, the answer of the panel only says it was
  sent, what happened shows in the state (`jp` object of a player: admin rank and the JA+ flags). `freeze` has no
  console command in JA+ and toggles the flag directly. Left out: `amplayefx` and `amtest`, which JA+ does not
  offer on the console. Kick, ban, team and teleport were there already. Added without a test run.
- New: hallucinations for one player (`codemp/game/fun/g_schizo.c`), the "Schizophrenia" button in the player panel,
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
- New emote `ampoop` (`codemp/game/fun/g_poop.c`, the command with the other emotes in `g_japlus_cmds.c`). A player
  types it or binds it like `amsit`: he sits down in the air (animation `BOTH_SIT3`), moans three times (the
  pain sounds of his own model), and a small rock lands on the floor under him with a splat and a sigh. The
  rock is a plain model entity (a chunk of the rock debris), lies there for 90 seconds, at most 24 on the map.
  The rules of the emotes apply (`JP_Emote`), bit 30 of `jp_emotDisallow` turns it off, he has to stand still
  and it is not for duels. It is in the emote list the server prints. In the panel the "Poop" button sends it
  for the selected player (action `poop`). Server side only. Added without a test run.
- New: flatulence for one player (`codemp/game/fun/g_flatulence.c`), the "Flatulence" button in the player panel,
  action `flatulence`, also the server console command `flatulence <player>`. Every 1.5 to 6 seconds he breaks
  wind: a sound everybody near him hears (the snorts of the rancor, the talk of the gonk droid, an air burst)
  and a green cloud behind him (the gas cloud of the noghri stick). One in six is a big one with three clouds
  that shoves him forward, not in a duel. Server side only, stock sound and effect events. Gone when he leaves or
  the map changes. Added without a test run.
- New: disorientation for one player (`codemp/game/fun/g_disorient.c`), the "Disorient" button in the player panel,
  action `disorient`, also the server console command `disorient <player>`. The view sways all the time (pitch,
  yaw and roll, two sines each with periods that do not fit together). Every 3.5 to 9 seconds it is thrown around
  by 70 to 200 degrees with a screen shake only this client gets, and the player stumbles a step to the side (not
  in a duel, not in the air). One in four of those is a whirl instead: the view turns once or twice around. All
  of it goes through `ps.delta_angles`, the offset the server adds to the view angles of a client, so the client
  predicts it and the movement and the aim follow the view. The roll shows in the first person view only (the
  third person camera has none). Not while dead, a spectator or in a vehicle. Bots are not bothered, they turn
  to where they want to look. Server side only. Gone when the player leaves or the map changes. Added without a
  test run.
- New: the tab "Config" edits `server.cfg` (`WA_ApiConfigGet`, `WA_ApiConfigSet` in `wa_fs.c`). It is that
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
  `wa_fs.c`). It shows two folders: the game folder in the home path of the server
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
- New: action `reload`, the running map again (`WA_ReloadMap` in `wa_entities.c`). The engine looks through its
  folders for pk3 files when a map starts and at no other time (`FS_Restart` in `SV_SpawnServer`), so this is
  what makes the server find a pk3 that was uploaded. A restart of the server process is not needed for that,
  and the panel could not start the process again. The players stay connected. With cheats on it is `devmap`,
  so they stay on. In the Files tab the page asks for it after an upload with a `.pk3` in it, and there is the
  button "Reload map".
  A start of the map removes what was spawned through the panel and loads the module anew. So the entities
  that were spawned through the panel (`entityPlaced`, the list of `/api/entities` has `placed`) are written
  to `webadmin_reload.dat` in the home path first, with their keys and the place they are at now, and the new
  map spawns them 1.5 seconds after its start and removes the file. The file names its map and is removed at
  the next start of any map, so it can not come back later. An entity of the map that was edited in the panel
  is not kept, the map has it as it was. Tried once on the running server: nine placed objects before and
  after, the file gone.
- New: the physgun, as in Garry's Mod (`g_props.c`, the section "The physgun"). An admin of JA+ types
  `/amphysgun` and has it: it is the stun baton, which he gets with the command and keeps over a death. The
  command again takes it away. The client of the port takes the baton out by itself, on another client it is
  reached with the next and previous weapon keys (it has no number).
  - Attack takes the prop he looks at into the beam, from up to 2048 units away (what a shot would hit: the box
    of the prop), and holds it while the button is down: at the distance it was taken at, where he looks. It
    is held by its middle, stiff (it goes to its place 12 times a second whatever its mass, 1500 units a second
    at most), it does not swing or turn by itself and still bumps into the world and into other props. Let go,
    it keeps its speed. The beam does not let go when the prop is caught behind something.
  - What is in the beam turns with him: when he turns, the prop (or the player) goes around him and turns
    about the vertical by the same angle, as if it were fixed to him (`Prop_GunTurn`). Looking up and down
    lifts it without tilting it.
  - Use held next to attack: his mouse turns the prop and not his view. Left and right turn it about the
    vertical, up and down about the line through his shoulders. His view stands still for that:
    `PmoveSingle` keeps it (`PM_SetPMViewAngle`) while `CARRY_GUNHOLD` is in `stats[STAT_CARRY]`, the weapon is
    the baton and both buttons are down, on the server and in the prediction of the client, and the server
    takes what the mouse did from the angles of the commands (`G_PropGunCmd`). He can still walk.
  - Alt attack next to attack freezes the prop where it is, in the air too. A frozen prop is not moved by
    anything (shots, shoves, force powers, the black hole, the hand of `+button13`, other props that hit it),
    it is in the way of players and props like a piece of the map. Attack on it with the physgun takes it
    again and that unfreezes it. A use of the entity (targetname) brings it home and unfreezes it too.
  - The distance: with a prop in the beam every press of `+button14` moves it away along the beam and every
    press of `+button15` brings it closer, by a tenth of the distance (16 units at least, never closer than 48,
    no further than 2048). The cgame of the port presses those for one command when the previous weapon
    (away) or the next weapon (closer) is asked for with a prop in the beam, so the mouse wheel does it and
    the weapon stays. On another client the keys can be bound: `bind MWHEELUP +button14`. Buttons and not a
    client command, which the flood protection of the server would let through once a second.
  - Players: attack on a player takes him into the beam like a prop. He is moved the way a carried player is
    (`Prop_CarryPlayer`: his speed is set every frame, 1000 units a second at most), without the hug: his hands
    are free, he has no moves of his own while he hangs there. Use next to attack and the mouse turns him about
    the vertical. He is not frozen by alt attack. The rules of carrying hold: no duels, no vehicles, nobody who
    is carried takes one, and the two walk through each other.
  - Use alone, with nothing in the beam (the R key of the default binds): every frozen prop of the map is free
    again.
  - The beam: an entity of the type `ET_INVISIBLE` with `generic1` `PROP_BEAM` names the player and what is in
    his beam (`otherEntityNum`, `otherEntityNum2`), everybody gets it. The cgame of the port draws a blue line
    (`gfx/effects/blueLine`) from his weapon to the middle of the prop or the player, every frame from where
    both are drawn (`CG_PhysgunBeam`). Another client draws nothing for it. While something is in a beam the
    entity is in the list of the World tab as `physgun_beam`.
  - As the physgun the baton does not fire: `PM_Weapon` drops its attack buttons while `CARRY_PHYSGUN` is set.
  Also in this change: a ball that was in a hand kept the tiny box of a body with no corners afterwards
  (`Body_Net` set it for balls too), it has the box of its radius again.
  Added without a test run, built for Linux only.
- New: a flip off a prop kicks it. A trace of a move that ends at a prop names the world with the contents of
  a body (`BG_PropHit`), so the code of the moves can tell a prop from a wall: in the air with a prop right in
  front the player flips back off it as off a player (`bg_pmove.c`, next to the JA+ flip kick), the wall flips
  from the ground do the same, and `forceKickFlip` gets `PROP_KICK_FLIP`. The server then kicks the body that
  is closest on the side he pushed off (`G_PropFlipKick` in `g_props.c`): back along the way he flies now, so
  to his side after a side flip and in front of him after a back flip. It gets 900 over its mass that way and
  a bit up, at the spot next to him, so a kick near the top of a crate knocks it over. (The first version
  looked for the prop in front of him and kicked along his view, which was wrong for the flips to the side.) What it hits afterwards
  counts for him. Added without a test run, and built for Linux only.
- New: `misc_blackhole` pulls the props too (`misc_prop`, `misc_cube`, `misc_ball`): by the middle of their
  bodies, with the strength it has for players, whatever their mass (`G_PropPull` in `g_props.c`, called from
  `blackhole_think`). The core does nothing to them: they do not break, and damage would knock them out of the
  hole. A prop that lies still is only woken where the pull is half the gravity or more (with the defaults
  within about 350 of the 600 units of reach), a weaker pull would not move it and everything in reach would
  stay awake. Added without a test run.
- New: props have the shape of their model, players stand on them, and a prop that is thrown at somebody hurts
  (`codemp/game/props/bg_props.c`, shared by the game and the cgame, and `g_props.c`).
  - The shape. `misc_prop` has a key `shape`: `model` (the default now), `box` or `ball`. `model` is the convex
    hull of the model: its furthest vertex along each of 26 directions, then the vertex that sticks out of the
    hull of those furthest, one at a time, until nothing sticks out by more than half a percent of the size of
    the model or 32 corners are used (60 faces at most). The first step alone was not enough: a crate with
    slanted shoulders lost its flat top, none of its corners is the furthest along one of the 26. A hull has no
    holes: what is hollow in a model is filled, a ring is a disc, a table a block with the outline of its top
    and its feet. A model that is flat or thinner than 4 units somewhere gets the box of its bounds. `misc_cube`
    stays a box, taken from the vertices of its model now.
  - The physics is the one of the cube with a hull in the place of the box: the spots that are asked about the
    world are the corners of the hull, then the middles of its faces and edges (64 at most, 26 for a box), two
    bodies are pushed apart along the direction of the face of either that they overlap least along, and every
    spot of one in the other is a contact. The middle of a body is the centre of mass of the hull. Mass and
    inertia still come from the box around it.
  - Players. A body is in the way of the moves of players: `SV_PMTrace` cuts every trace of a move against
    the bodies it comes near (`G_PropClipMove`). The engine could not do that, an entity is a box that is not
    turned for it. `BG_PropBrush` makes planes in the world of a shape where it lies, as a brush of a map has
    them (its faces, the six sides of the box around it, and six planes for each of its edges, where an edge
    of the box of a player slides past an edge of the shape), and `BG_PropClipBox` is the trace of a brush. So
    a player stands on top of a prop, walks on its slopes, falls off beside a turned crate where the box around
    it would still carry him. What is hit counts as world. Props count as bodies for the mask: ghosts,
    spectators and corpses pass. A player who is in a prop (it fell on him) is not stopped by it and walks
    out, and it is pushed out of him. The one who holds a prop is not stopped by it. A ball is in nobody's way.
    A player shoves a body by walking against it: he is stopped at its face, so it is the speed he asks for
    with his keys that counts for the shove, not the speed he has. A body somebody stands on is left asleep.
    He is not weight on it and is not carried along when it moves.
  - Clients. The cgame of the port makes the same shape from the model file (`generic1` of the entity says
    how, `otherEntityNum` who holds it) and cuts its predicted moves against it with the same code
    (`CG_ClipMoveToProps`), so standing on a prop that lies still is predicted right. While a prop moves the
    client has it where the last snapshot had it, as with every mover. A client that does not know any of this
    gets a box in `s.solid`, which is all such a client can predict with: not turned, as wide as it is long,
    around the origin of the model. For such a client a crate on its face is about right and everything else
    jerks at its edges.
  - Damage. A prop that comes at a player or an NPC faster than 320 units a second hurts him: 1 + (speed - 320)
    * 0.03 * mass, 150 at most, once in half a second, as `MOD_CRUSH`. What counts is how fast the middle of
    the prop comes at him, also for one that will only reach him when the frame is over (a fast one is in him
    before it was seen next to him). Who held, shoved, pushed or shot it in the five seconds before is the
    attacker, and is not hurt by it himself in the first 0.4 seconds. A crate of mass 6 at 600 units a second
    does about 50.
  - New server command `props`: lists the props with their corners, faces, mass, place, lowest and highest
    corner and whether they lie still. `props <number>`: what that body touches.
  - Checked before the user asked me to stop testing and build it: `bg_props.c` alone in a small program (300
    clouds of points give sound hulls, a nearest spot is off by 1.3 units at most where a face has no width,
    4000 moves of a box against turned shapes stop where stepping along the way says they should, 0.3 units
    early at most). On a private server with the first version of the hull, before the corners that stick out
    were added: 78 models dropped onto a floor all came to rest with their lowest corner on it, 4 of them did
    not fall asleep (two of those are boxes, as before). With the hull as it is now: a frozen bot dropped onto
    a cube stands on its top and the cube stays asleep, beside a cube turned by 45 degrees but inside the box
    around it he falls to the floor, he stands on the flat top of the crate model and on its slanted shoulder,
    a cube he is put into is pushed out of him and he is not stuck, a cube that falls 290 units onto him takes
    37 off him and one let go just above his head nothing.
    Not checked: the prediction of the cgame (not at all, it shares the code of the trace), shoving by walking,
    a prop thrown with +button13 at a player, stacks and piles after the last change of the hull, a client
    without the new cgame. Built for Linux and Windows.
- New: `+button13` takes players too, for admins only (`Prop_CarryPlayer` in `codemp/game/props/g_props.c`): who is
  logged in as an admin of JA+, with any rank. Everybody else takes props with it as before. The ranks are not
  compared and `jp_admUsableOn` is not looked at, any admin takes any player. He takes the player he looks at
  within 96 units (his legs, his chest or his head within 32 units of the line of sight, not through a wall)
  and carries him in the hug of `amhug` while the button is held:
  - Both play the animations of the emote (`BOTH_HUGGER1` the admin, `BOTH_HUGGEE1` the other one). The admin
    has it on his upper body only, his legs go on walking. The one who is carried has it on all of his body.
    The animations do not loop, so keeping their timers from running out holds the last frame, the embrace.
    When it is over the normal animations come back.
  - The admin has his as a plain animation of the torso that the server keeps alive and starts again if
    something else takes its place. He can still attack. The legs of the other one are kept the same way.
  - The upper body of the one who is carried has it as a hand extend (`HANDEXTEND_TAUNT` with `forceDodgeAnim`, the way a taunt
    is played, renewed every frame). That is what the game uses to say that the hands are busy, and every
    client predicts it: he can not attack with any weapon, not use the Force powers that need a hand and not
    switch his saber on. His saber is switched off when he is taken, in the middle of a swing too, and again
    should it come back on (a thrown one that returns). The sabers of both are put away at the start as for
    the emote. If something else has his hands when he is taken (he lies on the ground, he is choked) that
    runs out first.
  - The one who is carried is right in front of the admin, turned to him (his yaw is set every frame, his
    pitch is his own), 4 units off the ground, `g_grabHugDist` units away (default 10, 0 to 64). The emote
    itself can not have two players closer than 30 units, their boxes are 30 wide, and at that distance the
    arms do not reach. So the two of a hug stand in each other, and neither is there for the moves of the
    other: the server takes the other one out of every trace of the move (`SV_PMTrace`, `G_PropMate`). That
    is for these two only, everybody else and every wall, the invisible ones too, blocks them as always, and
    shots and sabers still hit both. A wall jump off the other one, which is what a flip kick is, finds nobody
    either. After the hug the two stay a pair until their boxes are apart, or they would be stuck in each
    other, and nobody else takes one of them before that.
    (The first version gave both the flag of the ghosts, `EF_PASS_PLAYERS`. That let them through every
    player and through player clips, the invisible walls, and the wall jump asks with a mask of its own, so
    the admin could still flip kick the one he carried. And a bot never got it, the mask of a bot is chosen
    first.)
  - The clients of the two are told about each other in `stats[STAT_CARRY]` of their player states (the
    number of the other one plus one, `CARRY_HOLDS` or `CARRY_HELD`, `bg_public.h`). Nobody else gets it, and
    a client that does not know it loses nothing but smoothness. The client of the port does three things
    with it:
    - `CG_BuildSolidList` leaves the other one out, so the prediction of the move does not run into him.
    - The one who carries: `CG_CarriedLerp` (`cg_ents.c`). He is predicted to where he is now, the player in
      his arms comes out of the snapshots like every other player and would be drawn where he was 100 to 150
      ms ago (at `sv_fps 20`: a server frame, the interpolation between two snapshots and the ping), 25 to 35
      units behind at a run. So that player is drawn at the place he had in those snapshots as seen from
      where the server had the carrier then, moved and turned to where the carrier is now. He does not trail
      any more and turns with the mouse at once.
    - The one who is carried: `CG_PredictPlayerState` does not predict him but takes him out of the snapshots
      too (`CG_InterpolatePlayerState`, what a spectator who follows somebody gets), so he sees himself
      where the one who carries him has him.
    For everybody who watches nothing changes, both come out of the same snapshots for them.
  - The one who is carried has no moves of his own: the server drops his walking and jumping the way it does
    for a player the admin froze (`stiffenedUp` in `PmoveSingle`, server side only). So he can not jump off
    the one who holds him, which is what a flip kick is. His client does not know that and may show the start
    of a jump for a frame before the next snapshot puts it right.
  - A player is no body of the physics there, so the server sets his speed once a frame, to what has him at
    his place when the next snapshot is made (600 units a second at most), and his own movement code takes him
    there and stops him at walls. A bot makes its move later in the same frame, so the way to its place is
    all. A human makes his with the commands that come in until the next frame, for him the way the place
    went since the last frame is added. He has no grip on the ground (the knockback time of a hit, renewed
    every frame). Let go, he keeps the speed he had, so he can be thrown, and for five seconds a fall to death
    counts for the one who threw him, as with force push.
  He is let go when the button is let go, when one of the two dies, leaves, starts a duel, enters a vehicle or
  he gets more than 128 units from his place, and when the admin logs out. Not taken: spectators, players in a
  duel, in a vehicle or in noclip, and nobody who is carried takes a player himself (two who hold each other
  would lift each other off the ground). Bots can be taken. Of a prop and a player in front of him he takes
  what is closer to his line of sight. The default of 10 units is what the user chose in the game, `g_grabHugDist`
  sets it while the server runs. A stock client knows nothing of `STAT_CARRY`: for somebody who plays with
  one, carrying and being carried jerks, his prediction runs into the other one. Built for Linux and
  Windows, not tried in the game by me.
- New: the button "Delete all props" under the list of the tab "Models" (second click within three seconds, as
  for the other deletes): every `misc_prop` that was placed through the panel goes, with one request (`op`
  clear). Props of the map stay, and so do the balls and cubes of the World tab. The buttons that need a second
  click share one helper on the page now (`confirmed`). Added without a test run.
- New: the tab "Models" and the entity `misc_prop` (`codemp/game/props/g_props.c`): any model of the game as a thing
  with the physics of the cube. The tab lists every `.md3` and `.glm` of the game (748 with the base game and
  JA+), with a search and the turning preview the class list has. Pick one, set scale, mass and shape if they
  are not to be the defaults, and right-click on the map: it is spawned as a `misc_prop` half its height above
  the floor and drops. It is in the list of the World tab afterwards and comes back after a map reload of the
  panel like everything that was placed.
  `misc_prop` is the general prop, `misc_ball` and `misc_cube` stay as the ball and the crate with numbers that
  suit them. Keys: `model`, `scale` (percent, default 100), `shape` (`box`, or `ball` for something round),
  `mass`, `bounce`, `friction`, `kick`, `angles`.
  - The box of the physics is the box around the model, edge by edge: the bounds of the first frame of an md3,
    and for a Ghoul2 model, which has none in its file, the box around the vertices of its first level of
    detail without the surfaces that are switched off. So a bench is long and low and tips over its long edge.
    It is the rigid body of the cube: it lands on corners and edges, tumbles, slides, collides with other
    props, balls and cubes by mass, is shoved by players, thrown by force push and pull and taken with
    `+button13`. Players are not blocked by it, as with the cube.
  - The longest edge is kept between 8 and 384 units, the scale is changed for that.
  - Mass without a `mass` key: 4 for a box of 32 units like the cube, and it grows with the surface of the box,
    not with its volume (`4 * (volume / 32768) ^ (2/3)`): models are shells and not solid blocks, and a big one
    stays something a player can move. The cargo crate at full size (40 units) has 6.2, the model of a player
    3.6, a small crystal 1.1, a box of 100 units 39. The limit is 100.
  - What shots, sabers and explosions hit is the box that the turned body just fits in, for cubes too now (it
    was a fixed cube before). An entity can only have a box that does not turn, so it is generous at the ends
    of a long thing that lies across the axes.
  - A Ghoul2 model stands as it is in its file, without an animation: a player model in the pose it was built
    in. The client makes the model itself and looks for its default skin (stock code for a general entity
    with `modelGhoul2`).
  - The body touches the world at 27 spots (corners, middles of edges and faces) and with a box in its middle.
    On a long thin model those are far apart, something thinner than the gaps can pass between them.
  The list of models is built by the server from the file system of the engine, a few folders a frame for 3
  ms at most, so the game does not stop for it: about 7 seconds once after every start of a map.
  Checked on the running server: the list is exactly the md3 and glm files of the pk3 files (748 of 748), and
  props dropped onto a floor come to rest with their middle half their height above it (a crate, a crystal, a
  player model, a weapon model at 300 percent, a ball), two crates stack. The user placed props through the
  page. How the models look in the game was not seen by me.
- New entity `misc_cube` (`codemp/game/props/g_props.c`, next to the ball), placed in the World tab like the ball. A
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
- Changed 2026-10-07, the cost of many props (`g_props.c`). Measured on a dedicated server without clients,
  `sv_fps 20`, 300 `misc_cube` over the spawn points of `mp/ffa3`. Where the time goes while they move: about
  two thirds in the lines to the 26 spots (`trap->Trace`, 426000 a second, most of that the walk through the
  map, a fifth the list of entities of the sector), the rest in the contacts with other props and the solver.
  None of it is arithmetic that wider registers would speed up.
  - Every loop that looked for players or props went over all entities of the map. `G_PropFrame` makes two
    lists once a frame (`propList`, `moverList`), and the places and sizes of the props sit in arrays of their
    own (`propSeenAt`, `propSeenRadius`), so the search for the props next to one reads 16 bytes per prop and
    not an entity. The vector functions of `q_math.c` are inline for these two files (`bg_props_math.h`).
    Together 15 to 17 % less time in the physics, the simulation is the same to the last digit.
  - A pile never came to rest: of 300 crates in low piles about 400 fell asleep and about 400 were woken every
    second, for as long as the server ran, at 300 to 340 ms of CPU a second. A body that woke woke everything
    that lay still next to it, down the whole pile, and what woke them were bodies that had come to rest deep
    in each other: that depth woke one (more than 2 units at the look at the ground every 500 ms) or the other
    (more than 4 units in `Prop_Fixed`) again and again. Now a body wakes its neighbours when it leaves the
    place it lay at (`Body_Left`: 0.25 units or 0.01 radians), not because it woke, so a pile still falls when
    its base goes. The depth a body came to rest with is kept (`restSep`) and only 1 unit more than that wakes.
    And a body that leans between a floor and a wall, with no three spots under it, thought for good: it lies
    still once it has not left its place for a second, and stays so while as many spots touch as did then.
    The same piles: 224 ms a second in the first 5 seconds, 64 in the next 5, 28 after 40 seconds (an idle
    server: 17). Checked on that server: a crate with two others on it pushed away, both woke within 0.4
    seconds and fell. Not tried in the game with players.
  - Looked at and left: one box test in place of the 26 lines for a body in the air (the box test of the
    engine does not see a surface on `mp/ffa3` that the lines hit), keeping the answers of the lines within a
    frame (11 % of them could be kept, the simulation changes), and a finer tree of sectors in the server
    (`AREA_DEPTH` 6 in `sv_world.cpp`: 22 % less time in the lines with the same simulation, but it changes the
    order of the entities every area query in the game gets. The user decided against it on 2026-10-07, it
    stays at 4).
- Changed 2026-10-07, bodies against players (`g_props.c`). A body went through a player when it was in a hand
  or in the beam, when it was fast, and when it lay on his head. The cause was one: players were no part of
  the solver. `Cube_Players` looked at them once a frame, gave the body a part of the speed between them (with
  the defaults 31 % of it, that is the shove of a heavy thing) and put it outside of him. So the pull of a
  hand, which the solver works on six times in each of the four steps of a frame, dragged a body through him;
  a body at 900 units a second moves 45 units a frame and was never seen at him; and one on his head kept
  falling into him, 4.7 units deep at 127 units a second for a crate of mass 4, deeper the heavier it is, until
  his middle was in it and it was shoved out sideways. Now `Body_Movers` makes every player and NPC near a
  body a box in its step, like the world but one that moves (`Body_Touch`, the code that finds where two
  bodies touch, with his box as the second one). The body stops at him in every step, bounces off him, and
  lies on his head, where friction takes it along when he walks. A body that can reach him within the step is
  held back before it is in him (one contact that lets it close the gap and no more), which is what stops the
  fast ones, and that is also where a fast one hurts him now. Nothing there moves the player. His speed towards
  a body beside him does not count, he is stopped by it and shoves it as before. A body that lies on him goes
  up with him when he jumps: his box is half a unit lower for it (`PROP_PLAYER_TOP`), so it lies in his box by
  that and does not stop his moves. At his sides the box is a quarter unit wider (`PROP_PLAYER_SIDE`), so a
  body comes to rest outside of it and stays in his way. The one who holds a body is still not in its way, a
  body somebody stands on is still left alone, and what lies or leans on a player does not go to sleep.
  `Cube_Players` keeps the shove and wakes a body a player is in, the step puts it outside of him. Checked on
  a private server with a frozen bot: a crate put over his head lies on it at speed 0 (before: it hung in his
  head at 127 units a second). Throws at him gave no clean numbers there. Not tried in the game.
- Changed 2026-10-07, the same for balls. A ball that is free has a move of its own (`Ball_Move`, one sweep of
  its box through the frame against the world) and looked at players once a frame as well (`Ball_Players`, the
  kick). Now that sweep is also cut at the boxes of the players and NPCs near its way (`Ball_ClipMovers`), with
  the code their moves are cut at bodies with (`BG_PropBrush` of his box, `BG_PropClipBox`). The sweep covers
  the whole frame, so a fast ball does not get past him, it bounces off him like off a wall and hurts him
  there, and one that falls on his head lies on it, whatever its mass. His head is no ground for it: there is
  no rolling resistance on it and it does not go to sleep there, so it rolls off when he walks. A ball that
  starts in his box is not stopped by him, which is how a ball he walks into (players walk through balls, that
  is the kick) gets away. A ball in a hand or in the beam is moved by the solver of the bodies and gets one
  contact there at its spot next to his box (`Ball_Touch` in `Body_Movers`), so it is not pulled through
  other players any more. `Ball_Players` keeps the kick, and puts a ball outside of a player only when it is
  more than a unit in him. Not tried, neither on a test server nor in the game.
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
- New entity `misc_ball` (`codemp/game/props/g_props.c`), placed in the World tab like the black hole. A ball with
  physics of its own: gravity, bounces that keep part of the speed, rolling with resistance, rolling down slopes,
  coming to rest. Players and NPCs kick it by running into it (it bounces off them like off something much
  heavier that moves), force push and pull throw it (a call in `ForceThrow` of `w_force.c`), shots, sabers and
  explosions knock it away from the point they hit. It is server side only: a plain model entity (`ET_GENERAL`,
  the ball of the training remote scaled up) on a linear trajectory the server renews every frame, with a second
  one for the turning. Its contents are those of a corpse, so shots hit it and players are not blocked. Keys:
  `radius` 18, `modelscale` 600, `bounce` 0.7, `friction` 90, `mass` 1, `kick` 1, `model`. A use, falling out of
  the world or getting stuck brings it back to where it was placed. Lifts do not carry it, and the black hole
  does not pull it. Added without a test run.
- New entity `misc_blackhole` (`codemp/game/props/g_blackhole.c`), made to be placed in the World tab: it is in the
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

- The entity editor can still end the server in ways the guard does not know. If the spawn code of a class
  raises an error, the dedicated server exits, because OpenJK makes every game error fatal there. The cases that
  can be told from the keys are refused since `WA_SpawnRefused` (see above). The list was first made by reading
  the spawn functions for `trap->Error`, which missed `misc_bsp` (the error comes from the engine). Since then
  every class the editor offers is placed bare by a check (`steps_guard.js`) and the server has to be there
  after it. What that does not cover: keys with values that are wrong, and other game types.
- The lights of a map can not be shown: the compiler takes them out of the map file.
- Plain HTTP with basic auth, and it listens on every interface unless `g_webBind` says otherwise: the password
  crosses the network as it is with every request. Where that network is not your own, set `g_webBind 127.0.0.1`
  and use an SSH tunnel, or put a reverse proxy with HTTPS in front. The security pass had made localhost the
  default; since 2026-10-08 it is every interface again, by decision of the owner of this fork, who opens the
  panel from other machines. A config that still has `g_webBind ""` from that time listens on every interface
  now.
- `g_webPassword` is an archived cvar. The server writes it in clear text into `visionjk_server.cfg` in its home
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

2026-10-08, the modes and the tools of the editor, on a private server (mp/ffa3 with bots) in headless Chromium
with software GL, driven through its debugging port: both modes and every view came up without an error on the
console. The guard refused the five bad entities that were tried and the server kept running. Placing a cube,
moving it a grid step, two undos and two redos gave the states they should, a deleted spawn point of the map came
back with its keys and as an entity of the map, a spawnflag box wrote its bit into the key table.

The same day the panel was used with the mouse and the keys as events of the browser
(`japlus_port/tools/panelshot/steps_mouse.js`, `steps_keys.js`), 18 checks: a click selects a player, an entity, a
waypoint. A drag moves the view, turns it with the middle button, moves an entity by its marker, turns the head
when flying. The wheel zooms and sets the flying speed. A right-click teleports, places the class that is armed,
adds a waypoint. Undo, the Z field, Remove and Save of a route. Copy, paste, the stored views and the keys of
the fly camera went through the keyboard. What that is not: a person at a real browser on a real GPU.

Triggers as boxes (`steps_boxtrigger.js`, 21 checks, 2026-10-08, mp/ffa3 with four bots): a trigger without a
shape, a flat box, half a box, one wider than 32768, a `trigger_asteroid_field` with a box and a `func_door`
with one were refused, each with its reason. A box over the whole map was tried with four classes: as a
`trigger_multiple` it fired its `target_relay` seven times in three seconds (read from the trace of what fires
what), as a `trigger_teleport` it had all four bots at its target, as a `trigger_push` with a target 4000 units
up it gave them 2530 units/s upwards and was still there after its first frame, as a `trigger_hurt` it left
seven deaths by `MOD_TRIGGER_HURT` in the log. Placed through the page a `trigger_hurt` stood on the floor
with the keys of the box, its panel showed 128 x 128 x 96, a new size gave the keys for it around the same
middle, a start point had no such row, a push and a teleport came with their `target_position` and one Undo
took both away, and after "Save layout" and a start of the map the box was there again. Not tried: a real
player in such a box (how the late push feels on a client with its own prediction), an NPC, a vehicle, the
classes for ships (`trigger_space`, `trigger_shipboundary`, `trigger_hyperspace`), and the Windows build.

The page against the game (`tools/refshot.sh`, 2026-10-09, six views from start points on each of six maps,
game and page on the same graphics card; mean difference of 255 / percent of pixels off by more than 16):

| Map | Before | After |
|---|---|---|
| mp/siege_hoth | 6.28/7.5, 2.54/3.5, 0.25/0.1, 0.43/0.3, 0.88/0.2, 0.36/0.1 | 0.39/0.2, 0.02/0.0, 0.02/0.0, 0.02/0.0, 0.02/0.0, 0.01/0.0 |
| mp/ffa3 | 0.79/0.1, 0.96/0.3, 1.59/0.6, 3.60/3.4, 1.96/1.3, 1.20/0.2 | 0.02/0.0, 0.10/0.1, 0.11/0.3, 0.43/0.5, 0.95/1.0, 0.02/0.0 |
| mp/ffa2 | 2.53/0.5, 2.03/0.7, 2.34/0.4, 1.35/0.2, 1.04/0.2, 1.35/0.2 | 0.20/0.2, 0.46/0.5, 0.02/0.0, 0.04/0.1, 0.06/0.1, 0.01/0.0 |
| mp/ctf4 | 0.97/0.1, 6.01/17.0, 7.24/6.8, 6.42/10.2, 6.50/15.7, 0.71/0.1 | 0.01/0.0, 0.01/0.0, 4.67/5.0, 0.12/0.1, 0.02/0.0, 0.01/0.0 |
| mp/duel7 | 1.22/0.3, 1.31/0.2, 1.26/0.4, 0.84/0.3, 0.84/0.3, 1.10/0.5 | 0.11/0.3, 0.02/0.0, 0.10/0.3, 0.05/0.1, 0.05/0.1, 0.04/0.1 |
| mp/ctf2 | 6.86/5.1, 13.10/27.7, 27.06/49.2, 20.40/53.8, 60.41/71.0, 7.76/5.2 | 0.58/0.0, 0.42/0.0, 0.12/0.1, 0.31/0.3, 4.84/0.3, 0.11/0.0 |

How to read it. The before column is too bad for the two fog maps: the view of the comparison itself drew no
fog then (fog was only on for the fly camera and the eyes of a player), which was found and put right in this
step. The after column leaves out more than the before one: where the clouds of a sky scroll the whole sky is
masked, 8 and 12 percent of the picture in the first two views of hoth and 62 percent of the fourth, and with
it the mountains of the sky portal behind it. Those were measured apart, from spots outdoors, before the
mask knew the sky: 22.2 and 27.1 before, 1.9 and 2.5 after. What is left: view 3 of mp/ctf4 is a weapon
pickup, view 5 of mp/ctf2 the falling snow, view 5 of mp/ffa3 fine lines of texture filtering. Other maps,
measured by the sessions that did the parts: mp/duel5 (19 flares) 1.1 to 2.8 before and 0.10 to 0.13 after,
mp/duel6 (sky portal, 51 trees) 5.8 to 30.3 and 2.1 to 4.7, mp/siege_korriban at its static models 7.5 to
64.9 and 0.1 to 2.0. One view of mp/ffa5 from outside the hull of the map stays at 35.5 (no visibility
culling). A frame on the graphics card costs what it did or less: mp/ctf2 2.4 to 2.7 ms before and 1.9 to
2.0 ms after (573 draw calls of the map became 412), the portal pass of hoth is not measurable.
Written by three sessions of the assistant side by side, each with its own client of the game to measure
against, and put together and measured again by the one that reports. Not done: a comparison at the game's
default of compressed textures, another graphics card, the Windows build.

The look of the editor (`steps_look.js`, 8 checks, 2026-10-08, mp/ffa3): the editor begins with "As in the
game"; with its markers off its picture is as bright as the one of the administration from the same camera
(63.2 against 63.2 of 255 on average, with "Open" set as the administration has it); "Texture and light" is
brighter (83.9); "Open" switches the sides in the game look too (seen from below the map the picture changes); the wireframe is in the picture of both and gone without it; a ray into
the middle of the picture finds the same spot of the map in both. The checks of the editor, which had all
run in the plain view, ran again in the game look: `steps_drafts.js` 68, `steps_labels.js` 23,
`steps_boxdrag.js` 16, `steps_mouse.js` 19, `steps_group.js` 24, `steps_boxtrigger.js` 24, `steps_pusharc.js`
13, `steps_fx.js` 38, `steps_cull.js` 5, `steps_surface.js` 7, `steps_movers.js` 15, `steps_facing.js` 3,
`steps_trace.js` 4. Not tried: how fast the game look is in the editor on a real GPU with many entities.

The effects (`steps_fx.js`, 38 checks, and `steps_fxsweep.js`, 9 checks, 2026-10-08, mp/ffa3): an effect file
is handed out and a name with `..` is not; a poll of a page that has it all has neither `fx` nor `fxPlay`
(663 bytes, 720 when the page says nothing of the effects); an `fx_runner` with `env/fire` is one runner and
within two seconds has primitives and quads, the pixels at its place are brighter with Effects on than off,
and the administration draws it too; one that plays once plays once for every time it is fired; the Play
button of the editor arrives once and is not played again by the next poll; a bolt that ends at a wall or at
a player leaves the effect of its weapon there; while the bots fight effects are played that the script did
not ask for; with Effects off the glow quads are back; a change of the map leaves nothing behind. The sweep
reads all 378 effect files the server has (the 376 of the game, seven of them replaced and two added by the
JA+ pk3) through the page: none makes the parser fail, each is played and stepped for three seconds with no
exception and no number in a vertex that is none, and never more than 580 of the 3000 are alive. The files
hold 692 particles, 151 lines, 102 tails, 72 oriented particles, 46 emitters, 47 cylinders, 29 electricity
and 20 runners; seven files hold nothing the page draws. With 3000 primitives of fire close to the camera a
frame took 29 to 57 ms in software GL, of that 0.7 to 1.8 ms to write the vertices. Looked at as pictures:
fire, a rocket explosion, a shockwave ring, electricity, sparks, water spray, the 28 torches of mp/ffa2.
Written and first run by a second session of the assistant that worked next to the one on the drafts, with a
private server of its own; the counts here are from a run of both parts together, where `steps_shots.js`
(9), `steps_saber.js` (4), `steps_state.js` (14), `steps_movers.js` (15), `steps_deform.js` (12) and
`steps_cull.js` (5) ran as before. Not done: a comparison with the picture of a real client, a real GPU, the
Windows build (the C code of it is three small functions with ints, floats and vectors).

Drafts for everything (`steps_drafts.js`, 68 checks, 2026-10-08, it takes the place of `steps_draft.js`): a
placed class is a draft with a number from 100000 and the server has none of it; three survive a poll; typed
keys survive a change of the selection; a class that needs a key shows it without a spawn; a template,
Duplicate, a paste of one and of a group and a `trigger_push` give drafts; Place on the push alone places its
target first and the push is still there two seconds later; the rectangle and Ctrl with a click take drafts;
a draft and a placed entity moved together are one step of the history; "Place all" with one `fx_runner`
that lacks its file places the others, keeps that one with the cursor in `fxFile`, and one Undo takes away
all that were placed; "Discard all"; a draft of another map is gone; a prop of the Models tab is on the
server at once. The names (`steps_labels.js`, 23 checks): a draft reads `fx_runner [draft: needs fxFile]`, a
targetname shows, no more than 40 besides the ones that always have one, the pool of lines does not grow in
200 frames, none with Names off and none in the administration, and a frame spends 0.1 to 0.2 ms on them with
74 entities and 0.3 to 0.5 ms with 924 (software GL). The checks that placed, duplicated or pasted were
changed to place their drafts and run as before: `steps_boxtrigger.js` 24, `steps_group.js` 24,
`steps_pusharc.js` 13, `steps_mouse.js` 19, `steps_boxdrag.js` 16, `steps_complete.js` 22, `steps_guard.js`
12, `steps_layout.js` 13, `steps_problems.js` 7. Written and first run by a second session of the assistant
that worked next to the one on the effects, each with a private server of its own; the counts here are from
a run after it. Not tried: a real browser, the editor through the eyes of a player, many drafts (hundreds).

The first drafts (`steps_draft.js`, 18 checks, the same day, the script is gone since): a `fx_runner` placed with Place came up as a draft with
the row `fxFile`, the cursor in its value and the list of the effects on it, "Place" and "Discard" as its
buttons, Fire and Duplicate off, listed as "new", and the server had no `fx_runner`; it was still there after
the next poll. With an effect typed in, Place made it entity 107 of the server and one Undo took that away.
A `target_speaker` draft was moved by the Z field with a key typed into a new row and not applied: it stood
where the number said and the typed key was still in the table, and nothing had gone to the server. Place
without the noise was refused and left the draft, with the noise it was placed where it had been moved to.
Discard threw a draft away at once. A `func_wall` asked for its model with the brush models of the map to
pick from (`*1 func_plat #77`, ...). A `misc_bsp` gave no draft, a `trigger_hurt` was placed as before, and
a draft of another map was gone at the next poll. Not tried: a real browser, a draft of a class of brushes
placed (the checks do not put a second entity on the brushes of the lift).

Every class placed bare (`steps_guard.js`, 12 checks, the same day, mp/ffa3): a `misc_bsp` with and without a
file, an `info_siege_radaricon` without an icon, a `fx_runner` without an effect and a shooter with `WP_NONSENSE`
and with `WP_NONE` were refused with their reasons, a shooter with `WP_ROCKET_LAUNCHER` and one without the
key were placed. The budget had 21088 of 3932160 bytes of memory in use and an entity with three text keys
took 96 more. Then each of the 243 classes the editor offers was spawned with the keys a click on Place
gives it (classname and origin, a box for a trigger), given a moment, and deleted: 184 were placed and
deleted, 4 were placed and gone by themselves (the three flags outside of CTF, a `trigger_push` without a
target), 55 were refused (31 the game did not keep, 13 are made of brushes, 11 need a key or are a
`misc_bsp`), and the server was still there. That sweep is what would have found the `misc_bsp` before the
user did. Not tried: every class with keys that are wrong instead of missing, other game types, other maps.

The completion of the keys (`steps_complete.js`, 22 checks, the same day, keyboard and mouse of the browser):
`/api/classkeys` had 84 keys for every entity and 133 classes, `func_door` with `lip|Float|8` and, from
`InitMover`, `color|Vector|1 1 1`. The definition of `trigger_push` gave `wait` and `speed` and none of its
flags. For a `trigger_push` the list had 86 keys, each once, `wait`, `speed`, `mins`, `maxs` before
`targetname`. In the panel: "+ Key" opened the list without the keys the entity has; `wa` left `wait` and
`awakescript` with `wait` marked; Enter took it, the focus was in the value field and its tip had the text
of the definition; Apply gave the entity `wait=2`. The arrows walked the list and Enter took the marked key,
Tab took `speed` for `spe`, `name` offered `targetname`, Escape closed the list and left the entity selected,
the arrow down opened it again, a click took `targetname`. On a door a key it did not have showed its default
in the empty value field, and `lip`, which it has, was not offered. A headless window sends no focus events
by itself, the check turns them on. Not tried: a real browser, and how it reads on a narrow screen (the list
is 440 px wide).

The throw of a push (`steps_pusharc.js`, 12 checks, the same day, mp/ffa3 with four bots). In the game: a
push over the whole map with its target 4000 above gave the bots 2530 units/s upwards, which is the root of
2 x 800 x 4000; with the target alone moved to 500 above they got 894, the number for that; with it moved to
below the middle of the push the push was still there and threw 894 as before, the report named it and its
panel said "No throw". A push placed with its target below it from the start was gone after its first frame
and every place and speed of the players was a number. In the editor: a placed push had an arc of 256 up
and none across with its top at the target and 0.80 s to it, it came down on the floor under it, the panel
said so; with the target moved to the side the point of the arc at the top was the target to a hundredth of
a unit; not selected the arc has no rays, and with `LINEAR` there is none. Following the way down cost 30 ms
for 19 rays on this machine. At the first place tried the way ended at a roof 215 above the floor, which is
right and is why the check now looks for open sky. Not tried: a real player on such a pad (the arc is the
way from the middle of the push, a player starts where he touches it), `target_push`, a push of a map file
whose target a layout moves.

The handles of a box (`steps_boxdrag.js`, 16 checks, the same day, with the mouse of the browser): six
handles, none under the panel of the entity in the view that was set up. The face towards +X dragged 64 units
out, the top 48 up, the one towards -Y 32 out and the one towards -X 32 in gave exactly those keys on a grid
of 16, with the origin where it was. In the middle of a drag the box was drawn 240 wide, the row said 240, the
keys were the old ones and the entity the same; let go, the keys followed. A face dragged 600 units through
the one across stopped one unit before it. Six undos gave the box that was placed. A click on a handle
changed nothing, flying the yaw stayed, and a start point, two selected entities and the administration had
no handles. Not tried: a hand on a real mouse, a box seen along one of its axes (the code leaves the face
where it is then), a touch screen.

The layout (`steps_layout.js`, 13 checks, the same day, mp/ffa3): two entities placed, an item of the map
deleted and a start point of the map moved gave a layout of three to spawn and two to take away. Saved, the
file was in the game folder with those five blocks, and after the map was started again with the map command
all of it was there: the two placed ones, the item gone, the start point at its new place and not at the old.
Moved back to the keys of the map file the start point was the one of the map again (two and one). After
"Forget" the file was gone and the next start was the map as its file has it. A "Reload map" with an entity
placed and an item deleted came back with both, and a plain start after it was the map again. On a second
server without a password, so with the panel off, a layout written by hand was done at the start of its map:
one entity spawned, a door without a brush model refused, an entity to take away that the map does not have
passed over. Not tried: a layout in another game type than it was made in, one with many props, a layout
that a pk3 brings.

The arrows (`steps_facing.js`, 3 checks, the same day, mp/ffa3): 23 of the entities of the map have an angle.
Seen from straight above, the pixels that the arrow of a start point adds to the picture reach furthest in
the direction 359, its angle key says 360. Not tried: an arrow that points up or down.

Entities to look at (`steps_problems.js`, 7 checks, the same day, mp/ffa3): the map as it is gave nothing. Of
five entities placed for it, the one with a target that names nothing, the relay with a name nothing fires
and the one 900 units outside of the map were listed, the relay that is fired and what fires it were not. A
click on a line opened the World tab with that entity selected, and with the five deleted the list was empty
again. The rules were also run over the entity lists of all 23 map files of the game by a script.

What a surface is made of (`steps_surface.js`, 7 checks, the same day, mp/ffa3): the camera was put in front of
a surface with a script, one without, a patch and one of a lift, each the first of its kind that can be seen
from there, and a click in the middle of the view showed that surface with its shader and with what its
kind must say, the lift with the `func_door` it belongs to. The outline had six line ends for every triangle,
and a click at nothing emptied the box.

A saber that was thrown (`steps_saber.js`, the same day, mp/ffa3): no bot throws on demand, so the page was given
a saber in the air by hand. As a thrown one it got one blade, flat and as long as the saber of its owner, and
the picture differed from the one with the hilt alone. Then the bots were watched: one threw, his saber was
among the models with its own hilt from the second state on, moved about 50 units from poll to poll and
turned 80 degrees in each, the 800 a second the page turns it by. Not seen: the picture of a real throw (it
is over in a second), a saber with two blades, a thrown saber of a player with two sabers.

What is in the air (`steps_shots.js`, 7 checks, the same day, mp/ffa3): a `misc_weapon_shooter` with a rocket
launcher, placed and fired through the panel, put a rocket into the list that flew 360 units in 0.4 s along
the way the shooter looks (its speed is 900) and had the page ask for the model of a rocket. One with a
blaster that repeats had three bolts in the air at once, and the picture with them differed from the one
without. The rocket ended in one flash, about where it met the wall 2,700 units on, and the flash was gone
half a second later (9 checks now, three runs). Seen while testing, in the code of the game and not of the
panel: the client the game lends a `misc_weapon_shooter` starts as zeros, which `WP_FireRocket` takes for a
finished lock on player 0, so the first rocket a shooter fires after a map started turns to that player
when he is alive. Not tried: a thermal detonator and a det pack (no bot threw one), and how the colours
compare with the effects of the game.

What fires what (`steps_trace.js`, 4 checks, the same day, mp/ffa3): a trigger_multiple fired through the
panel gave five events, the world firing the trigger and the trigger firing each of its four lifts, nothing
before the click and nothing after, and the light was out after three seconds.

The state feed (`steps_state.js`, 14 checks, the same day, mp/ffa3): without a number the 40 items came with
one, with that number they were left out and the answer was 87% smaller (456 of 3,418 bytes, nobody on the
server), with another number they came again. The page kept its list, got a new one when an item was placed
and the old number back when it was taken away. It asked 29 times in three seconds with the map in view, 4
times with the log over it, and 29 times again after it. Three players came with what stays of them the
first time (2,988 bytes), without it when their numbers were named (1,012 bytes, about 152 a player), and
with it again for a number that was not theirs. The page read a player as before, with his name, his team,
his place and the blades of his saber, and the checks that click, follow and teleport players passed.

The tab Load (`steps_load.js`, 11 checks, the same day, Linux, a private server with four bots and nobody on
it): 40 frames in a second at `sv_fps 40`, a frame of 0.06 ms on average and 0.2 ms at most, 0.3 and 1.1 ms
while the page read its files, a wait of 25 ms, the entities the editor counts too. A value came every
second, a number the server does not have got all it keeps, and nothing was asked for with the tab closed.
In one run a second had a frame of 30 ms: the page was reading the animations of the players just then,
which the server read in one go. That led to the files being read as they are sent. Tried with a script
that takes six files through the route and compares them with what the pk3 files of the game hold: the same
to the byte, 11.8 MB in 0.56 s alone and three of them next to three models in 1.8 s, with frames of the game
of 1.7 to 1.8 ms at most in those seconds and all 40 frames in each. The same for the map: `/api/map` of
mp/ffa3 (7.6 MB, 0.38 s) and mp/siege_hoth (11.1 MB, 0.55 s) is what the pk3 holds to the byte, and the
number of brush models (9 and 95) is right.
The part of the panel: on the empty server 0.05 of 0.06 ms (a page asking for the state ten times a second
is most of what the game module does then), with four bots 0.05 of 0.34 ms. Not tried: a server under real
load, where the yellow marks would show, and the clock on Windows.

Fire, the rectangle and teams of doors (`steps_fire.js`, 11 checks, the same day): on mp/ffa3 Fire sent a lift
its 280 units, an info_player_deathmatch answered that it does nothing when fired, and a fired
trigger_multiple sent the four lifts it has as targets. A drag with Shift around three placed entities
selected the three. On mp/ctf2, with its team of four doors: fired at the second part all four moved, after
an edit of the third part all four still moved, and with the first part deleted the other three moved
together when the last was fired. On mp/duel6 a fired `func_breakable` broke: its entity was gone and its
brush model was not drawn any more (30 drawn before, 29 after). Not tried: a team whose first part is
deleted while it moves, and firing every class of the game (the use functions were read, not run).

Doors and lifts (`steps_movers.js`, the same day, mp/ffa3): the server named 6 of the 8 brush
models as drawn, the two triggers not, and each where the entity list of the editor has it. A lift with a
targetname of its own was fired by a `trigger_always` placed through the panel: it was drawn up to 280 units
from where it rests, at seven or eight places on the way, the picture changed, it came back and the picture
was the one from before to the pixel. Deleted in the editor its brush model was not drawn and a ray at it
went through, undo brought both back. Aimed at where the lift rests the ray hit it, aimed at where the map
file has it the ray went on. A `func_rotating` placed with the brush model of the lift made the server name
that model twice, the second one turned 90 degrees in a second as its speed says, and a ray at one of its
triangles where it was at that moment hit it (15 checks now). Not tried: a door that a player opens, a
`func_usable` that is switched off.

The sides, the deforms and the order of see-through model surfaces (`steps_cull.js`, `steps_deform.js`, the
same day, on mp/ffa3, mp/ctf4 and mp/duel7): inside the map, at 8 spawn points looking four ways, the picture
with one side drawn and the one with both differ in 0.04% of their pixels on average and 0.25% at most, and a
click in the middle hits the same place either way in all 32 views, so the front of a triangle is taken as the
game takes it. From above a third of the picture is another one: the rooms are open. A player close up is not
inside out. The three flares of mp/ctf4 and the eight chain quads of mp/duel7 are found and the picture differs
from the one with the quads as the map has them. No map of the game for more than one player has a wave, a
bulge or a move, so those were put on every shader of mp/ffa3 by hand: the picture changes and is the old one
after it, and a move by nothing changes nothing. After the two changes of the map every player had his model
(one file was cut off on its way and was asked for again). Not tried: a model with a see-through surface in
front of another model (no such pair stood on the maps), a comparison with a picture of the game.

Several entities at once (`steps_group.js`, 22 checks, the same day): Ctrl and a click selected three and took
one out again, Shift and Page Up lifted the three by one grid step as one step of undo, Ctrl+Z and Ctrl+Y put
them back and up again, the Z field moved the others by as much, a paste of the three stood as the copied ones,
Delete took them away and undo brought them back, Duplicate made three more, and at the end the map had the
entities it had before, class by class. A brush entity in the group went the same way and came back, and with
one of the three deleted behind the back of the page the other two were moved and the message named the one
refused. Six turns of 15 degrees put the three exactly where a quarter turn puts them, each with the angle
90, and six undos put them back. Tried with info_notnull, which stays where it is put: a prop falls, and then where it is and what its
origin key says are two things. This run is what found the trigger a moved lift left behind: after the fix a
func_plat moved and put back leaves the same entities as before (the door of this map has no trigger of its
own, so that half is not tried).

The page was looked at once in headless Chromium with software GL: the 3D map, the player list and the chat box
with all kinds of lines show up. Not tested: using the page by hand (HUD view, dragging entities), the Windows DLL
at run time (it builds and links with mingw-w64), macOS.
