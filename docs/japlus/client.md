# Client: cgame and ui

The original JA+ client plugin is a replacement `cgamex86.dll` and `uix86.dll` plus assets. Its features are built into
the cgame and ui of this tree. The client reports itself to the server as plugin 1.4 Build 4 (userinfo `cjp_client`),
which is what switches the plugin dependent behaviour of the server on.

The reference for the behaviour is the Windows plugin 1.4 Build 4. Every function of its two DLLs was compared with
the unmodified 1.01 modules, see [porting-method.md](porting-method.md), and what the cgame and ui draw was compared
with the original on recorded games, see [verification.md](verification.md).

## Cvars

| Cvar | Meaning |
|------|---------|
| `cp_pluginDisable` | bit field, each bit turns one feature off, see below |
| `cp_sbRGB1`, `cp_sbRGB2` | RGB color of the first and second saber (red, green << 8, blue << 16) |
| `cp_holster` | holster model of the player, 0 for none |
| `cp_login`, `cp_clanPwd` | admin password and clan password sent at the join |
| `cp_hookModel` | model of the hook |
| `cp_altDimAlpha` | how visible players of the other dimension are |
| `cp_autoReplierFile` | file with the auto replier texts |
| `ui_sbRed1`, `ui_sbGreen1`, `ui_sbBlue1`, `ui_sbRed2`, `ui_sbGreen2`, `ui_sbBlue2` | the color sliders of the saber menu |
| `ui_selectedHolsterIndex` | the entry selected in the holster list of the player menu |

Bits of `cp_pluginDisable` (the console command `pluginDisable <number>` toggles one and lists them):

| Bit | Name in the plugin | Bit | Name in the plugin |
|-----|--------------------|-----|--------------------|
| 0 | NewDrainEFX | 8 | NoAltDim_effect |
| 1 | IgnoreAllPlayers_WhenInDuel | 9 | holsteredSaberBolted |
| 2 | EndDuel_Rotation | 10 | ledgeGrab |
| 3 | seeBlackSabers | 11 | NewDFA_PrimAttack |
| 4 | AutoReplier | 12 | NewDFA_AltAttack |
| 5 | NewForceEffect | 13 | No_SP_Cartwheel |
| 6 | No_NewDeathMessage | 14 | AllowDownloadURL_Redirect |
| 7 | NewForceSight_effect | | |

## Console commands

`pluginDisable`, `reloadReplier` (reads the auto replier file again), `showPlayerID` (list of players with their
numbers) and the admin gun: `admCmdNext` and `admCmdPrev` choose one of 17 admin commands, `playerNext` and
`playerPrev` a player, `admCmdEXE` runs the command on him, and the `gun...` commands run an admin command on the
player under the crosshair.

## Features

cgame:

- Sabers: RGB sabers (colors 6 to 10) and the black saber (11) with trails and light, `sabercolor` and `amcolor`.
- Sabers carried on the body (`cg_players.c`, `CG_JPHolster`): a saber that is not in the hand hangs on the right
  hip, the second one on the left hip. Staffs and sabers with the saber key `idleInBack` hang across the back and
  come to the hand in the middle of the draw animations. The bolts `*back`, `*hip_l` and `*hip_r` are made on the
  model of the clientinfo in `CG_RegisterClientModelname`, so every copy of the model has them at the same index.
  The holster model of the player is shown for sabers with `idleInBack 1`. Like the plugin the own holster is
  loaded with the first client info after the first snapshot, the holster of another player together with the
  deferred player models (scoreboard, own death).
- Alternate dimension: HUD text, screen overlay, players of the other dimension are faded (`cp_altDimAlpha`).
- Private duels on a server with `jp_DuelAlpha`: while the local player duels, every other player fades away
  completely together with his sprites, shadow, effects, missiles, explosions and thrown saber (to an alpha of 40
  with `cp_pluginDisable` bit 1). A player in a duel of others fades to `jp_DuelAlpha`. The crosshair ignores them.
- Hook: model, shot effect, rope from the right hand, the two kinds of hook missiles.
- Effects: flamethrower jet, fire on burning players, drain hand, kothos beam, stab effect, rage and heal at the
  bolts the plugin uses, wide drain effect, force sight overlay, the shell around a flag carrier.
- Cameras (`cg_japlus.c`): rotation around the player after a won duel, view compensation in two player
  animations, more distance in the kick combo and the back attacks.
- Auto replier (`cg_japlus.c`): answers at duel start, duel end, own join and the join of another player from the
  texts in `cp_autoReplierFile`.
- HUD: `[Alt-Dim]`, vote texts for sleep, silence and polls, names under the crosshair in their colors in FFA,
  score and deaths on the wider scoreboard of JA+ servers from 2.1 on, highlander rules on the loading screen.
- Macro scan when the server asks for it, model scale of players, yaw speed limit with dmflags 128.

ui:

- Saber menu: the blade styles `rgb`, `rgb2` to `rgb5` and `black` with a preview, three color sliders per saber
  that are written to `cp_sbRGB1` and `cp_sbRGB2`.
- Player menu: the holster list (pictures `models/weapons2/holster/model<N>.jpg`, sets `cp_holster`). The head list
  shows every skin of the models a player can use, also the ones without an icon (with
  `gfx/menus/icon_hidden.jpg`), and the selected one on a model in the menu.
- Moves menu: the JA+ moves and the title "Melee & other attacks".
- Macro scan (`ui_macroscan`, `ui_macroenable`): key bindings that are movement or attack macros are disabled,
  also when they hide in a file or a cvar that the binding executes.
- The script commands `voteSleep`, `voteSilence` and `sendModSayMsg` the JA+ menus use.

The menus, shaders and pictures come from the plugin pk3, see [README.md](README.md).

### The setup menu

`ui/jamp/setup.menu` (main menu) and `ui/jamp/ingame_setup.menu` (in a game) are the plugin's copies of the base
game's menu, changed for this engine. Both have the same pages.

| Page | Entry | What it sets |
|------|-------|--------------|
| Video | Video Mode | a list the ui fills: "Desktop", the size of the screen (`r_mode -2`), and the sizes of `r_availableModes`. A size is `r_mode -1` with `r_customwidth` and `r_customheight` |
| Video | Display Mode | Window, Fullscreen, Borderless (`r_fullscreen`, `r_noborder`). Borderless with "Desktop" covers the screen |
| Video | Renderer | Vulkan or OpenGL (`cl_renderer`) |
| Video | Anti-Aliasing | Off, 2x, 4x, 8x (`r_ext_multisample`) |
| Video | Compressed Textures | `r_ext_compress_textures` |
| Video | Video Quality | presets for the detail only (`r_lodbias`, `r_picmip`, `r_textureMode`, `r_subdivisions`, `r_fastSky`, `r_inGameVideo`), not for the size or the bit depths any more |
| Sound | 3D Sound, Output, Reverb | the sound of [audio.md](audio.md), see "The menu" there |
| Options | Allow Downloads | Off, On (`cl_allowDownload`) |
| Bot Mode | Bot Mode | the client plays by itself (`cg_botMode`, not saved: off at every start) |
| Bot Mode | Skill | Initiate to Jedi Master (`cg_botModeSkill` 1 to 5): turn speed, aim, time to the first attack |
| Bot Mode | Fight | Never, In Duels, Always (`cg_botModeFight` 0 to 2) |
| Bot Mode | Walk The Route | No: it stays where it is and only turns and fights (`cg_botModeRoam`) |
| Bot Mode | Keys Take Over | a move or attack key ends the bot mode (`cg_botModeKeys`) |

Everything on the video page is applied by its "Apply" with a `vid_restart`. Taken out, because they did
nothing or did harm: Color Depth and Texture Quality (`rd-vulkan-vision` ignores `r_colorbits` and
`r_texturebits`), Sound Quality (11 or 22 kHz, the engine runs at 44), EAX, and the button for the URL redirect
cache. The menus of the base game, which a client shows while it plays as base, still have the EAX and Sound
Quality entries: the ui hides them (`Item_ApplyHacks`).

None of the pages was looked at in the game after these changes: the rows were placed by their numbers.

### Wide screens

The game places all 2D (menus, HUD, console) on a grid of 640 x 480. Until 2026-10-11 both renderers stretched
that grid over the window, so on 16:9 everything was a third too wide. Now the grid keeps its height of 480 and
gets as wide as the window: at 16:9 it is 853 wide, the old 640 sit in the middle and x runs from `-bias` to
`640 + bias` (`R_2DBias()` in `rd-vanilla/tr_init.cpp`, used by `RB_SetGL2D()` of both renderers).

| Cvar | Default | Meaning |
|------|---------|---------|
| `r_2dAspect` | 1 | 1 the 2D keeps its shape on a wide window, 0 the old stretch. Latched, `vid_restart` |
| `r_2dBias` | set by the renderer | read only: the width beside the 640, in units of the grid. 0 on a 4:3 window, with `r_2dAspect 0` and on an engine that does not have this |
| `r_mode` | -2 (was 4) | a new install starts in the size of the desktop, not in 800 x 600 |
| `r_noborder` | 1 (was 0) | and as a window without a border. A stored config keeps what it has |

What goes where on a wide window:

- Menus stay in the middle. A window of a menu that covers the whole 640 x 480 (a background) is drawn over the
  whole window, the cursor reaches the margins.
- HUD: health and armour, vote lines, voice talkers, chat box, holocron and power icons and the flag status are
  at the left edge. Force and ammo, the upper right block (team overlay, fps, timer, radar, duel icons,
  powerups), fuel bars, pickup icon, lagometer and timer bar are at the right edge (`CG_Place2D()` in
  `cgame/cg_drawtools.c`). Crosshair, weapon, force and item selection, centre prints and the scoreboard stay
  in the middle.
- Fades, tints and the loading picture cover the whole window. The scopes (binoculars, disruptor) keep their
  shape, with black beside them. A video keeps its 4:3 with black beside it.
- The console is as wide as the window, as before.

The modules read `r_2dBias`. In a stock `jamp.exe`, or with a renderer of another tree, the cvar is missing or
0 and they draw as they always did.

Not looked at in the game: it was built for Linux and nothing more. Open: the siege timer and the automap are
placed by the grid but not moved to an edge, and the text of the stock fonts is as soft as before (they are
bitmaps made for 640 x 480).

### UTF-8

Text inside the client is UTF-8 (2026-10-11). The original game knows one byte a character, Windows-1252 in the
western languages, and so do all legacy clients and servers. What follows from that:

| Cvar | Default | Meaning |
|------|---------|---------|
| `cl_utf8` | 1 | text is UTF-8 inside the client. 0: one byte a character as in the original game |
| `cl_utf8Text` | set by the engine | read only, in the userinfo: 1 while text really is UTF-8. It is 0 with `cl_utf8 0` and with a language that has a code page of its own (`se_language` russian, polish, korean, taiwanese, japanese, chinese, thai), where everything stays as it was |
| `g_visionCaps` | 1, game module | read only, in the server info. Bit 0: the server passes UTF-8 chat on |

- Typing: a character arrives as the bytes of UTF-8, one char event each, so the modules get what they always
  got (`K_CHAR_FLAG`). The edit fields of the engine and of the menus move and delete by character.
- Reading: one decoder (`Q_UTF8_Decode()` in `shared/qcommon/q_string.c`) takes a byte that starts no valid
  UTF-8 sequence as Windows-1252. So names and chat of legacy players, the strings of the game and old configs
  show right without being converted anywhere.
- Drawing (`rd-common/tr_font.cpp`, both renderers): a character of Windows-1252 that the picture of a font has
  comes from that picture as before. Any other character is made from Noto Sans
  (`fonts/visionjk/NotoSans-Regular.ttf` in the client assets, OFL) with stb_truetype (`rd-common/tr_font_ttf.cpp`),
  in the point size of the font it stands in. That covers Latin, Greek and Cyrillic. A character Noto Sans does
  not have (Chinese, Japanese, Korean) is a `?`. `fontlist` says how many glyphs were made.
- The font file: the engine reads it once at its start and keeps it (`client/cl_font.cpp`), because a renderer
  that starts on a pure server can not read the pk3 of this tree. A file with another SHA-256 than the one the
  build shipped is refused: a server can send a pk3 with a file of that name. So another font can not be
  dropped in without building the client anew.
- Sending: chat goes out as UTF-8 only to a server whose server info has `g_visionCaps` bit 0. To any other
  server every command is converted to Windows-1252 on the way out, with `?` for what that lacks. The userinfo
  (the name) goes out as Windows-1252 to every server, since a name is handed to all clients alike.
- On a server of this tree the game module gives each recipient its form (`JP_ChatFor()` in
  `game/japlus/g_japlus_chat.c`): what a UTF-8 client wrote reaches a legacy client as Windows-1252. Umlauts
  and accents arrive there as they should, Cyrillic or Greek as `?`. Nothing else is converted.

Limits. The chat line of the engine (what a stock cgame or `cg_chatInput 0` types in) is drawn with the charset
picture of the game, which has Windows-1252 only: a Cyrillic letter is a `?` there. The console and the chat box
of this tree show it, see below. Names can not hold characters outside Windows-1252. None of this was run: it is
built for Linux, the decoder and the conversions passed a short native test, and the size and place of the
TrueType glyphs next to the letters of the game were only checked as numbers.

### The console

`client/cl_console.cpp`, reworked on 2026-10-11.

- A cell holds a code point, so the console shows every character the fonts have. `condump` writes UTF-8.
- With `con_font 1` (the default) the console and its input line are drawn from DejaVu Sans Mono
  (`fonts/visionjk/DejaVuSansMono.ttf`, read and checked by the engine like the other font) in the size of a
  cell: sharp at any `con_scale`. `con_font 0`, a language with a code page of its own and a renderer without
  the font keep the letters of the game.
- The mouse belongs to the console while it is down. In a window the system draws its cursor, in fullscreen
  the console draws its own.

| Input | What it does |
|-------|--------------|
| hold the left button and move | marks text, from where the button went down to where the cursor is. Past the upper or lower edge the text scrolls |
| double click, third click | marks the word, then the line (with the rows the console wrapped it into) |
| Shift and a click | moves the end of what is marked |
| Ctrl+A | marks everything |
| Ctrl+C, right button | puts what is marked on the clipboard: without colours, a wrapped line as one line. The right button pastes into the input line when nothing is marked |
| Shift with left, right, Home, End | marks in the input line. Typing replaces it, Ctrl+C copies, Ctrl+X cuts, Ctrl+V pastes |
| Ctrl with left or right | jumps by word in the input line |

Ctrl+C with nothing marked anywhere clears the input line, as it always did. Text of an Asian language can not
be marked: its rows are drawn as strings of another height. Not run: nobody has seen the console of this build.

### The chat box

`cgame/chat/cg_chat.c` replaces the chat box of the base game (2026-10-11).

| Cvar | Default | Meaning |
|------|---------|---------|
| `cg_chatBox` | 10000 | as before: milliseconds a line stands at the lower left while nobody types, 0 none |
| `cg_chatBoxHeight` | 350 | as before: the lower edge of the lines |
| `cg_chatBoxWidth` | 380 | the width lines are wrapped to, 200 to 600 |
| `cg_chatInput` | 1 | the message mode keys open the box of the cgame. 0: the line of the engine, as in the base game |

The box keeps the last 256 lines with their kind and who said them (the client number a server built on OpenJK
appends, else the name in the line). While nobody types the newest six rows stand there and fade out. The
message mode keys (`messagemode` to `messagemode4`) open it: a frame with ten rows of history, three tabs
(All, Team with the clan and admin chat of JA+, Private), a bar that shows the place in the history, and the
line to type in with `Say:`, `Team:` or `To <name>:` in front.

| Input | What it does |
|-------|--------------|
| wheel, Page Up, Page Down | scroll the history |
| Tab, a click on a tab | another tab |
| hold the left button and move | marks text |
| Ctrl+C, right button | copy what is marked, a line of the history a line, without colours |
| a click on an underlined address | asks "Open ... ?" with the whole address. Enter opens it in the browser of the system, any other key does not |
| Up, Down | what was sent before (the last 16) |
| Ctrl+V | pastes |
| Enter, Escape | send, close |

Only an address that starts with `http://` or `https://` is a link, and the engine checks it once more before
it hands it to the system (`Sys_OpenURL()`: no blanks, quotes or control characters, nothing but those two
schemes). Nothing is ever opened without the question.

It needs the engine of this tree, which says `cl_cgameChat 1`, hands its message mode commands to the cgame
(`cg_chatopen`, while the cgame has set `cg_chatHandler`) and has three more calls in the `ext` block of the
cgame import: the clipboard both ways and the address. In a stock `jamp.exe` the box shows the lines, and
typing stays with the line of the engine. Not run: nobody has seen the box.


### Chat bubbles

`cgame/bubble/cg_bubble.c` (2026-10-11). What a player says in the chat stands above his head for 2.5 to 8
seconds (longer for more text), in up to three lines, and three swelling dots stand there while he types. It is
all in the 3D scene: panel, edge, a soft light around it, the tip towards the head and the text are polygons
that face the camera and are hidden by walls.

| Cvar | Default | Meaning |
|------|---------|---------|
| `cg_chatBubbles` | 1 | 0: the balloon of the game as before |
| `cg_bubbleRange` | 1200 | how far away a bubble is still drawn. It fades over the last quarter |

- A bubble pops up a little too large and settles, and rises and fades at its end. Up close it keeps about the
  same size on the screen, then it gets smaller with the distance.
- Team chat has the colour of the team around it. Private lines never get a bubble. No bubble on a dead or
  cloaked player, and none on oneself in first person.
- The light around a bubble is a glow surface: `r_DynamicGlow 1` makes it shine.
- It works on any server: the cgame knows who said a line (the client number a server built on OpenJK appends,
  else the name in the line).

It needs the engine of this tree (`cl_cgameScene 1`): the text comes from its TrueType font as polygons
(`R_Text3D`), and the pictures of the panel are computed in the cgame and handed to the renderer
(`R_RegisterShaderRGBA`), so no file of a pk3 is involved and a pure server takes nothing away. In a stock
`jamp.exe`, beyond the range and with `cg_chatBubbles 0` the balloon of the game stays. A legacy client on the
same server sees the balloon as it always did. Not run: nobody has seen a bubble, the sizes and colours are
numbers.

### Statuses above the heads

`cgame/status/` and `game/status/g_status.c` (2026-10-11). One line in smaller letters right above the head, a
chat bubble stands on top of it.

| Cvar | Default | Meaning |
|------|---------|---------|
| `cg_playerStatuses` | 1 | show the lines of the others |
| `cg_shareMusic` | 0 | tell the server what music plays on this machine. Off by default: everybody on the server reads it |
| `cjp_caps` | 1, read only | in the userinfo: this cgame draws statuses, the server sends them to nobody else |

- "Away from keyboard": the server finds out who did not touch a key or the mouse for `g_afkTime` seconds, for
  legacy players too.
- "Listening to Spotify: artist - title", "Listening to YT Music: title", or "Listening to artist - title" for
  any other player of music. On Linux it is asked from the players over D-Bus (MPRIS) every five seconds:
  `libdbus-1.so.3` is loaded when `cg_shareMusic` is first 1 and is not needed otherwise, the questions are
  sent without waiting for the answers. On Windows it is read from the window titles of Spotify and of
  YouTube Music (the window has to show the tab). Not on macOS and not in the browser client.

Only on a server with the game module of this tree (`g_visionCaps` bit 1), and only clients of this tree with
its engine draw the lines. Not run: built for Linux, the Windows code was compiled with mingw.

### The server browser in a game, and servers with bots only

2026-10-11. The bar of the in-game menu has a tenth button, SERVERS (`ui/jamp/ingame.menu`, the file of the base
game with buttons 63 wide where they were 70). It opens `ui/jamp/ingame_servers.menu`: the browser of the main
menu on a dark pane over the game, with all its filters and pop-ups. Back and Escape return to the bar. Joining
asks first, since it leaves the server one is on.

Both browsers have "View Bots Only" (`ui_browserShowBotOnly`, default 1). With 0 a server on which nobody but
bots plays is left out. How the client knows:

- A server built on OpenJK says how many of its players are people (`g_humanplayers` in its info answer), some
  others say how many are bots (`bots`).
- A server that says neither (the original server, and JA+ on it) and has players is asked for its status,
  once a refresh: a bot has no ping there. The refresh goes on for up to two seconds while such answers may
  still come. A server that does not answer in time is shown.

The filter needs the engine of this tree. In a stock `jamp.exe` the switch does nothing. The menu files were
placed by their numbers and not looked at.

### The menu in the level

`cgame/menu3d/cg_menu3d.c` (2026-10-11). While a menu is open in a game, the view moves within a third of a
second to a camera behind the right shoulder of the player. He is at the left edge of the picture, seen down to
the hips, and the menu stands in the level to the right of him. The game goes on around it.

| Cvar | Default | Meaning |
|------|---------|---------|
| `cg_immersiveMenu` | 1 | 0: the flat menu of the base game |
| `r_menuBlur` | 1 | how far the scene behind the menu is out of focus (rd-vulkan-vision only): 0 not at all, 1 normal, up to 4 |
| `s_muffle` | set by the cgame | 0 to 1: the sound of the game through a low pass. Not stored |

- The menu is not painted onto a texture. The ui draws it as always, and the renderer puts that 2D onto a
  plane in the level (`Set2DPlane`, a matrix from the 2D grid to clip space). The cursor is drawn on the plane
  with the rest and the mouse moves it there. All menus work on it as they are.
- The plane has a place in the level, but no depth: its 2D lands at the front of the depth buffer like the
  flat 2D. With the depth of the plane, the frames and fills of a menu were cut out in the shape of whatever
  stood between camera and menu, the player first of all (the text was not, it never tests the depth).
- The layout is made in the picture, not in the level: the camera turns until the player is at
  `MENU3D_PLAYER_X` of the picture, the middle of the menu is at `MENU3D_PANEL_X`, and the menu takes
  `MENU3D_SCREEN_PART` of the height. How far away the plane is then changes nothing one can see. With a
  narrow field of view the camera is farther back, so the player takes the same part of the picture.
- A scene inside a menu (the model in the player menu) is a viewport and can not be tilted. The engine moves
  it to where its place is on the plane and gives it the size the plane has there (`CL_UIRenderScene`).
- Behind the menu the cgame adds a panel with an edge and a glow to the scene. Its shaders do not test the
  depth either (`RGBA_SHADER_OVER`) and are sorted last, so it stands over the whole scene like the menu: a
  wall or a pillar between camera and panel took a piece out of the panel and left the menu in the air. Moving
  the panel towards the camera until the level has room for it was tried first and can not work where the
  wall is next to the camera. The HUD is hidden meanwhile.
- The scene is put out of focus by rd-vulkan-vision (`VK_BlurFrame`): the frame goes into a buffer of half its
  size, then passes widen it, three at `r_menuBlur 1`. That is a blur of the whole picture, the player
  included, not one by depth: a real depth of field needs a pass that reads the depth in both devices of the
  renderer. rd-vanilla keeps the scene sharp. The first version (cvar `r_sceneBlur`) ended the game as soon as
  a menu opened, because the pass asked for the descriptor set of the frame where only the sets of the glow
  buffers were known, and after that was repaired it went through the 320 x 240 buffers of the glow, which
  left nothing of the scene to recognise.
- The sound of the game (effects, music, Steam Audio) and the voices of the voice chat are muffled. The low
  pass of the engine (`S_MufflePaint`) keeps what it has after a sample by the time of the sample: the mixer
  paints the same fifth of a second again with every frame, and a filter that just ran on from one paint
  into the next gave a crackle with every frame.
- The camera is pulled in where a wall is behind the player.

Not while spectating, dead, zoomed or at the end of a map (the camera has no player to stand behind), and not
in another engine (`cl_cgameUIPlane`). The first version was seen in the game on 2026-10-11 and changed after
that (silhouette of the player in the menu, camera, blur, crackle). The numbers of the layout as they are now
have not been seen yet.

### Bot mode

Not a JA+ feature, and client only (`cgame/botmode/cg_botmode.c`): no server knows of it, the game module and the
engine are not changed, so it runs on any server and in any client that loads this cgame. A key for on and off
is on the JA+ page of the controls menus (`cg_botMode !`).

- The route is the file the bots of a server walk, `botroutes/<map>.wnt`, read from the client's own pk3s
  (the format `SavePathData` of `ai_wpnav.c` writes). The waypoints are a trail in the order of the file. At
  a waypoint the next one of the trail is taken, one time in four another waypoint it names, and at the end of
  the trail the way back. Links that need a force jump are not taken. A waypoint that is not reached in 8
  seconds, or after five jumps and side steps against what is in the way, is given up for the nearest other
  one in view. A map without that file: the bot stands and fights.
- Enemies: the nearest player in view within 1536 units who is alive, not on my team and not in a duel of his
  own. In a duel only the duel partner. It walks to where the weapon reaches (72 units with a saber, 512 with
  a gun), steps to the sides and attacks when he is where it looks. The weapon is the one that is selected.
  Dead, it presses attack to come back.
- The input: the engine has no way in for a cgame to write a command. So the bot holds keys with the console
  commands of the keys (`+forward` without a key number is held until `-forward`), sets the yaw with
  `SetClientForceAngle`, and turns the pitch with `+lookdown` held all the time and the pitch override of
  `SetUserCmdValue` as its rate, set every frame from how far off the pitch is. `CG_Shutdown` lets the keys go.
- It does nothing as a spectator, in the intermission, in a vehicle and at an emplaced gun.
- The HUD says "BOT MODE".

Built, not run: nobody has seen it walk or fight, and the pitch control is worked out from the engine code, not
measured.

## Prediction

The movement code (`bg_*.c`) is shared with the server. The JA+ rules in it are switched by `JP_FEATURE( JPF_... )`,
which reads the global `bg_jpFeatures`. On the client that value comes from the key `jp_cinfo` of the server info
(`CG_ParseServerinfo`), the same way the plugin does it. A server cvar is not visible as a cvar on a remote client,
only on a listen server.

- Players of other duels and of the other dimension are left out of the traces of the predicted move
  (`CG_JPNonInterference` in `cg_predict.c`, same rules as `G_TempUnlink` on the server).
- A player with `EF_PASS_PLAYERS` (a ghost, or a duel that ended within the last 3 seconds) is predicted with the
  move mask of the server for that case: world and terrain only. The original plugin has no rule for this.
- Grip speed and the force combo setting are read from the server info as well.

## Not done

- The download of files over an URL: the plugin ui embeds libcurl, a download screen, a cache file
  (`japlus/urlRedirect_cache.txt`) and three menu scripts, and its cgame asks for the download when the server
  names a `jp_DlBaseURL`. OpenJK has no HTTP download, this would be a new dependency on every platform.
  `cp_pluginDisable` bit 14 belongs to it.

Where the port knowingly differs from the plugin is in [differences.md](differences.md).
