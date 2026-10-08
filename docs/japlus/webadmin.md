# Web admin panel

The game module can serve an admin page over HTTP: the map in 3D with all players as animated models, player
actions and a live entity editor. It is not part of JA+. The code comes from JKAdmin
(commit 7b712a55, "Add web admin panel to the MP game module" by Lixsl),
which is OpenJK 260c59c2 plus that one commit, the same base this tree starts from.

| File | Content |
|------|---------|
| `codemp/game/webadmin/g_webadmin.c` | HTTP server, JSON state, player actions, entity editor, chat lines |
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
| `GET /api/file?name=` | a file below `textures/`, `models/` or `gfx/` (images, md3, glm, gla, cfg, skin), or a shader script `shaders/<name>.shader` |
| `GET /api/shaders` | the names of the shader scripts of the game, in the order the engine reads them in |
| `GET /api/anims` | animation names in `animNumber_t` order |
| `GET /api/classes` | everything that can be spawned, and the items with model and icon |
| `GET /api/entities` | the entities with their keys. `budget`: entities, models, sounds and effects in use and the most there can be. `brushModels`: how many brush models the map has |
| `GET /api/classdoc` | the entity definitions of the game code: every `QUAKED` comment of its source files as text |
| `GET /api/export` | the entities in `.ent` format |
| `GET /api/chat?since=&epoch=` | chat lines after number `since`. Everything if `epoch` is not the current one |
| `POST /api/action` | `action`, `id`, `value`: say, addbot, map, kick, ban, health, armor, scale, kill, god, noclip, schizo, flatulence, disorient, poop, gravity (no `id`), reload (no `id`), playergravity, team, teleport, and the JA+ ones: amsleep, amwake, amprotect, amempower, ammerc, ammindtrick, amghost, amsilence, amunsilence, amslap, amForceAltDim, amUnForceAltDim, amdenyvote, amShowMotd, amrename, ampsay, admgrant, freeze |
| `POST /api/entity` | `op` spawn, update or delete, with `id` and `kv` (one `key=value` per line). `op` clear with `kv` `classname=<class>` deletes every entity of that class that was placed through the panel. `map=1` with spawn: it is an entity of the map that comes back (undo), not a placed one |
| `GET /api/config` | the text of `server.cfg`, as UTF-8 |
| `POST /api/config`, `POST /api/config?exec=1` | the body is the new text of `server.cfg` (plain text, not a form). With `exec=1` the server runs `exec server.cfg` afterwards |
| `GET /api/models` | every `.md3` and `.glm` under `models/`. The server reads the folders a few at a frame: `done` says whether the list is complete, the page asks again until it is |
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
- New: the modes Administration and Editor (`setAppMode` in the page). In the administration the map is drawn
  with the shader scripts of the game, by a renderer of the page (script section "the shader scripts of the
  game"): the page asks `/api/shaders` for the script files, reads them through `/api/file` and keeps the text of
  every shader until a map uses it. A surface is drawn with the stages of its script, in the sort order of the
  game: images, `animMap`, `clampMap`, `$lightmap`, `blendFunc`, `alphaFunc`, `depthFunc`, `depthWrite`,
  `rgbGen` and `alphaGen` (const, wave, vertex), `tcGen` (lightmap, environment), `tcMod` (scroll, scale, rotate,
  stretch, transform, turb), `polygonOffset`, `sort`. A shader without a script is the lightmap times its image,
  a surface without a lightmap is lit by its vertices, both as `tr_shader.cpp` makes them. The sky is the box of
  `skyParms` around the eye and the stages of the sky shader as layers of clouds, as in `tr_sky.cpp`. The
  lightmaps are taken as they are in the map, which is what the game shows with `r_overBrightBits 0`,
  `r_mapOverBrightBits 0` and `r_gamma 1`, the defaults. See-through surfaces are drawn after the players and
  the models. Not done: fog, `deformVertexes`, glow, `surfaceSprites`, portals, culling by side (every surface
  is drawn from both sides), and the scripts on the models of players and items, which are drawn as before.
  Images outside `textures/`, `models/` and `gfx/` are not handed out by the server, a stage that names one is
  left out. With a game module that has no `/api/shaders` the administration shows the plain view. The rules
  were checked by reading all 3365 shaders of the game through the parser. The picture was looked at in headless
  Chromium on mp/ffa3, where every image a stage names loaded. It was not put next to a picture of the game.
- New: the tools of the editor (script section "the tools of the editor"), each made of what the game code has.
  - The entity definitions. A step of the build (`cmake/Modules/ExtractQuaked.cmake`) collects every `QUAKED`
    comment of `codemp/game` into the module, `/api/classdoc` hands them out. The panel of an entity shows the
    text of its class, a box for every spawnflag that has a name, and offers the keys the text names. A class
    is drawn in the colour of its definition, and the class picker shows the first lines as a tip.
  - Views, switched in the bar at the top left of the map: Volumes draws the box of every entity (the size of
    its brushes for a trigger or a door, from the models of the map file, else the box its class is defined
    with), Links a line from an entity to every one it fires (`target` to `targetname`). What belongs to the
    selected entity is drawn through the walls, the panel lists it with a click to go there.
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
- New: the server refuses to place what would end it (`WA_SpawnRefused`): a class made of brushes (`trigger_`,
  `func_`) without a `model` key that names a brush model of the map, the classes whose spawn function raises
  an error without a key (`target_speaker`, `target_play_music`, `target_level_change`,
  `trigger_lightningstrike`, `misc_siege_item`, `misc_model_breakable`, `trigger_shipboundary`,
  `trigger_hyperspace`), an entity when fewer than 64 are left for the game, and a model, sound or effect when
  its kind has no place left. The answer says which rule it was.
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
- New: action `reload`, the running map again (`WA_ReloadMap` in `g_webadmin.c`). The engine looks through its
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
  can be told from the keys are refused since `WA_SpawnRefused` (see above): the list was made by reading the
  spawn functions for `trap->Error`, a class that fails somewhere else is not in it.
- Volumes shows a brush entity as the box around its brushes, not their shape. The lights of a map can not be
  shown: the compiler takes them out of the map file.
- Plain HTTP with basic auth. Put it behind a reverse proxy with HTTPS before it is reachable from outside, or
  leave `g_webBind` empty (127.0.0.1) and use an SSH tunnel. Since the security pass the default bind is localhost,
  not every interface.
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
back with its keys and as an entity of the map, a spawnflag box wrote its bit into the key table. Not done by
hand with a mouse: dragging, the number fields, the keys.

The page was looked at once in headless Chromium with software GL: the 3D map, the player list and the chat box
with all kinds of lines show up. Not tested: using the page by hand (HUD view, dragging entities), the Windows DLL
at run time (it builds and links with mingw-w64), macOS.
