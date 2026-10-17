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
| Sound | Sound in the Background (only the new look) | `v_soundUnfocused`: 1, the sound goes on playing while the window is not in front or is minimized. 0 (the default): the engine pauses its sound device when the window loses the focus (`SNDDMA_Activate`, `shared/sdl/sdl_input.cpp`), as it always did. Minimized the game runs at `com_maxfpsMinimized` (50), enough for the mixer. The voice chat plays on a device of its own and was never paused, unless its voices go through the 3D sound. Clicked in a hidden client, the loss of the focus itself was not tried |
| Options | Allow Downloads | Off, On (`cl_allowDownload`) |

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
| `v_2dAspect` | 1 | 1 the 2D keeps its shape on a wide window, 0 the old stretch. Latched, `vid_restart` |
| `v_2dBias` | set by the renderer | read only: the width beside the 640, in units of the grid. 0 on a 4:3 window, with `v_2dAspect 0` and on an engine that does not have this |
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

Pictures made for 16:9 (2026-10-17, as TaystJK): on a wide window the loading screen takes
`levelshots_16_9/<map>` and the start of the game `menu/splash_16_9` where a pak has them, a level without a
picture `menu/art/unknownmap_mp_16_9`, else the pictures of 4:3 as before (stretched over the window, as they
were). The map lists of the menus keep the 4:3 pictures, their boxes are 4:3. This tree ships no such pictures.

Shader overrides (2026-10-17, as TaystJK, `rd-vanilla/tr_shader.cpp`, both renderers): a shader in a file
`shaders/<name>.oshader` takes the place of the shader of that name in any `.shader` file, without a copy of the
rest of that file. Of two override files with the same shader the one whose name comes later in the alphabet
wins, as among `.shader` files. The console says at the start how many override files there are.

The modules read `v_2dBias`. In a stock `jamp.exe`, or with a renderer of another tree, the cvar is missing or
0 and they draw as they always did.

Not looked at in the game: it was built for Linux and nothing more. Open: the siege timer and the automap are
placed by the grid but not moved to an edge, and the text of the stock fonts is as soft as before (they are
bitmaps made for 640 x 480).

### UTF-8

Text inside the client is UTF-8 (2026-10-11). The original game knows one byte a character, Windows-1252 in the
western languages, and so do all legacy clients and servers. What follows from that:

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_utf8` | 1 | text is UTF-8 inside the client. 0: one byte a character as in the original game |
| `v_utf8Text` | set by the engine | read only, in the userinfo: 1 while text really is UTF-8. It is 0 with `v_utf8 0` and with a language that has a code page of its own (`se_language` russian, polish, korean, taiwanese, japanese, chinese, thai), where everything stays as it was |
| `vs_caps` | 1, game module | read only, in the server info. Bit 0: the server passes UTF-8 chat on |

- Typing: a character arrives as the bytes of UTF-8, one char event each, so the modules get what they always
  got (`K_CHAR_FLAG`). The edit fields of the engine and of the menus move and delete by character.
- Reading: one decoder (`Q_UTF8_Decode()` in `shared/qcommon/q_string.c`) takes a byte that starts no valid
  UTF-8 sequence as Windows-1252. So names and chat of legacy players, the strings of the game and old configs
  show right without being converted anywhere.
- Drawing (`rd-common/tr_font.cpp`, both renderers): a character of Windows-1252 that the picture of a font has
  comes from that picture as before. Any other character is made from Noto Sans
  (`fonts/visionjk/NotoSans-Regular.ttf` in the client assets, OFL) with stb_truetype (`rd-common/tr_font_ttf.cpp`),
  in the point size of the font it stands in. That covers Latin, Greek and Cyrillic. Chinese, Japanese and Korean
  come from Noto Sans CJK (`fonts/visionjk/NotoSansCJKjp-Regular.otf`, OFL, 8.8 MB): the JP face of
  `NotoSansCJK-Regular.ttc` 2.004, cut down by fontTools to the glyphs its 44810 characters name, without layout
  tables, since stb_truetype does not shape (`pyftsubset <face>.otf --unicodes='*' --layout-features='' --no-hinting
  --name-IDs='*' --name-languages='*'`). Han characters have their Japanese forms. A character none of the three
  fonts has is a `?`, and so is a script that needs shaping to be read (Thai, Arabic, the Indic ones), which is
  drawn letter by letter. `fontlist` says how many glyphs were made.
- The font file: the engine reads it once at its start and keeps it (`client/cl_font.cpp`), because a renderer
  that starts on a pure server can not read the pk3 of this tree. A file with another SHA-256 than the one the
  build shipped is refused: a server can send a pk3 with a file of that name. So another font can not be
  dropped in without building the client anew.
- Sending: chat goes out as UTF-8 only to a server whose server info has `vs_caps` bit 0. To any other
  server every command is converted to Windows-1252 on the way out, with `?` for what that lacks. The userinfo
  (the name) goes out as Windows-1252 to every server, since a name is handed to all clients alike.
- On a server of this tree the game module gives each recipient its form (`JP_ChatFor()` in
  `game/japlus/g_japlus_chat.c`): what a UTF-8 client wrote reaches a legacy client as Windows-1252. Umlauts
  and accents arrive there as they should, Cyrillic or Greek as `?`. Nothing else is converted.

Limits. The chat line of the engine (what a stock cgame or `v_chatInput 0` types in) is drawn with the charset
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
| Ctrl with Backspace or Delete | deletes the word before or after the cursor, also in the chat line and the chat box (2026-10-17, as TaystJK) |

`v_consoleClock` (default 0) puts the clock of the machine left of the version in the console's lower right
corner: 1 the time, 2 the date and the time (TaystJK's `con_datetime`, 2026-10-17).

Ctrl+C with nothing marked anywhere clears the input line, as it always did. Text of an Asian language can not
be marked: its rows are drawn as strings of another height. Not run: nobody has seen the console of this build.

### Keys

`client/cl_keys.cpp`, `shared/sdl/sdl_input.cpp` (2026-10-17, taken over from TaystJK).

- A key can have a bind with a modifier as well as its plain one: `bind ctrl+x kill`, `bind alt+1 "say hi"`,
  `bind shift+mouse2 +use`. `unbind ctrl+x`, `bindlist` and the config know them (`bind "CTRL+X" "kill"`).
- Only the left Alt, Ctrl and Shift make such binds. With several held, Alt counts before Ctrl, Ctrl before
  Shift. A key that has no bind for what is held runs its plain one, so Shift for walking and a key pressed with
  it keep working as before. A key that goes up ends what it started when it went down, also when the modifier
  was let go first.
- A modifier itself can not be bound with a modifier (`ctrl+shift` is refused).
- The right keys are keys of their own: `RCTRL`, `RALT`, `RSHIFT`. `bind rctrl +attack` binds the right one only.
  A right key without a bind runs the bind of the left one, so `bind ctrl ...` covers both, as before.
  Everything else (Ctrl+C in the console, the chat box, the menus) takes either key as Ctrl, and the menus and the
  cgame are told the left key's code for a right key.
- The bind menu of Setup still shows and sets the plain binds only.

Not run with real keys: a hidden window gets none, so the right keys and the modifiers were only built.

### Console scripts

`qcommon/cmd.cpp` (2026-10-17), the commands of TaystJK with its syntax, so its scripts and binds work here.

| Command | What it does |
|---------|--------------|
| `delay <ms>` | what follows it in the line or the bind runs that many milliseconds later: `bind x "+attack;delay 300;-attack"`. Everything else goes on in the meantime |
| `waitf <frames>` | the same in frames |
| `delaycancel <text>`, `waitfcancel <text>` | drop what waits and has the text in it, `""` all of it |
| `ifCvar <cvar> <test> <count> <command ...> ...` | runs the first command whose test the cvar passes. A test is a value, or `$=` `$!=` `$>` `$<` `$>=` `$<=` with a number, `$contains` `$beginswith` `$endswith` with a text, or `$else`. A value that begins with `$` is that cvar's. `<count>` is the words of the command, itself included: `ifCvar cg_thirdPerson 0 3 set cg_thirdPerson 1 $else 3 set cg_thirdPerson 0` |
| `strSub <command ...>` | runs the command with `$cvar$` replaced by the value: `strSub say_team my fov is $cg_fov$`. `$$` is a `$` |

- Quotes inside quotes: a quote that a letter follows opens a quote inside the quote, so
  `bind x "say "a b""` binds `say "a b"`. That holds for what runs on this machine (console, configs, binds),
  not for what a server or a client sends. A quote outside quotes opens one as it always did.
- At most 64 delayed lines wait at the same time.
- `ifCvar` and `strSub` give out no password: a cvar the engine hides and any cvar with `pass` or `pwd` in its
  name reads as empty there, so a config from a server can not put one into a chat line. TaystJK has no such
  check.

### The chat box

`cgame/chat/cg_chat.c` replaces the chat box of the base game (2026-10-11).

The size of the box is set in Setup (Chat & Social: Chat Box Size, Width and Rows) and by hand while it is
open: its upper right corner, marked by two short lines, is dragged with the left button (to the right it gets
wider, upwards it gets more rows), and Ctrl with the wheel makes the box and its letters larger or smaller in
steps of 5 %. Both write the three cvars. `v_chatDrag <dx> <dy>` moves the corner for a script. The box is
never wider than the screen, whatever width and size say together.

With the HUD of the new look (`v_hud 1`, [ui.md](ui.md)) the box has that look too, since 2026-10-12: a panel
of dark glass with the letters of the menus, the tabs as words with a line under the open one, a thin bar for
where in the history one is. The lines that stand at the lower left while nobody types each lie on a dark strip
as wide as their words, so green text can be read on a bright floor. The names of the tabs and the words in
front of the line to type in are in the language of the game (`CG_VisionText`, see "Clickable players"). With
`v_hud 0`, in another engine and next to the HUD of a mod the box is drawn as before.

| Cvar | Default | Meaning |
|------|---------|---------|
| `cg_chatBox` | 10000 | as before: milliseconds a line stands at the lower left while nobody types, 0 none |
| `cg_chatBoxHeight` | 350 | as before: the lower edge of the lines. While the scoreboard shows, the lines of the closed box stand at the bottom of the screen. The open box stays where it is, also over a scoreboard that is kept open, and never lower than its input line has room (2026-10-12: it moved down with the lines, and what one typed was under the edge of the screen) |
| `v_chatBoxWidth` | 380 | the width lines are wrapped to at a size of 1, 200 to 600 |
| `v_chatBoxScale` | 0.85 | the size of the box with everything in it, the letters first of all: 0.6 to 1.6. Since 2026-10-12, when the box was 1 and "quite big" |
| `v_chatBoxRows` | 10 | rows of history in the open box, 4 to 24, and no more than fit under the top of the screen |
| `v_chatInput` | 1 | the message mode keys open the box of the cgame. 0: the line of the engine, as in the base game |

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

It needs the engine of this tree, which says `v_cgameChat 1`, hands its message mode commands to the cgame
(`v_chatOpen`, while the cgame has set `v_chatHandler`) and has three more calls in the `ext` block of the
cgame import: the clipboard both ways and the address. In a stock `jamp.exe` the box shows the lines, and
typing stays with the line of the engine. Not run: nobody has seen the box.


### HUD parts of TaystJK

Taken over from TaystJK on 2026-10-17. They are drawn by this tree's cgame, so they work on every server it runs
on, and nobody has to download anything for them.

The killfeed (`cgame/hud/cg_killfeed.c`, after TaystJK's `hud_obituary.c`) is at the right edge, under the FPS,
the timer and the team overlay. Each line shows who killed whom with what, and fades out. The icons are the weapon
icons of the game, plus the sentry and the dark force. A death without a weapon to show (a fall, lava, a
telefrag, a death by oneself) is the console's sentence, in the language of the game. The box behind a name has
the team colour, green for oneself and grey for everyone else. The obituary line in the console stays as it was.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_killfeed` | 1 | the killfeed, 0 none |
| `v_killfeedSize` | 1 | its size, 0.5 to 2 |
| `v_killfeedTime` | 5000 | milliseconds a line stays, the last second fading |
| `v_killfeedX`, `v_killfeedY` | 0 | moves it left and down |

The movement HUD (`cgame/hud/cg_movehud.c`) is after TaystJK's `hud_strafehelper.c` (from jaPRO and the CGaz of
DeFRaG), written anew for the physics of the game (`bg_pmove.c`: `pm_accelerate` 10 on the ground,
`pm_airaccelerate` 1 in the air, friction 6, stop speed 100). The movement styles of jaPRO are left out. The bars
show where the view has to point. The middle of the screen is where it points now. Everything is off by default.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_speedometer` | 0 | the speed along the ground under the crosshair: green when faster than running, gold when much faster |
| `v_speedometerJumps` | 5 | under it, the speeds at the last jumps (when the ground was left), 0 none |
| `v_speedometerSize`, `v_speedometerX`, `v_speedometerY` | 1, 0, 0 | its size and place |
| `v_strafeHelper` | 0 | a bar under the crosshair for the keys that are held. Dim: speed starts to come. Bright: the best angles (from the best one to the one where the speed stays). Yellow: still faster, but the speed goes down past its end. The white mark is the view |
| `v_strafeHelperColor`, `v_strafeHelperY` | `0 1 0.4 0.75`, 0 | its colour and place |
| `v_snapHud` | 0 | in the air, the zones of the angle where the rounding of the velocity after every move (the `SnapVector` at the end of `PmoveSingle`) gives a little more speed, in two alternating colours. They depend on the frame rate (`com_maxfps`, or `pmove_msec` with `pmove_fixed`) |
| `v_snapHudColor1`, `v_snapHudColor2`, `v_snapHudY` | | its colours and place |
| `v_pitchHud` | 0 | a mark where the view would be at the pitch `v_pitchHudAngle` (15, down positive), colour `v_pitchHudColor` |

For a player one follows, the keys are what the server says of them (the direction of the move), and the speed
he wishes is his full one.

Not tried in the game: how it feels while running, and whether the angles of the bars are right to the degree.
They were only checked against the formulas.

### Chat bubbles

`cgame/bubble/cg_bubble.c` (2026-10-11). What a player says in the chat stands above his head for 2.5 to 8
seconds (longer for more text), in up to three lines, and three swelling dots stand there while he types. It is
all in the 3D scene: panel, edge, a soft light around it, the tip towards the head and the text are polygons
that are hidden by walls. A bubble is a thing in the level: a letter of it is 4.2 units tall at any distance, so
it is small far away and large up close as the player under it is, and it stands upright. It turns to the one
who looks about its upright axis only and does not lean with a look up or down.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_chatBubbles` | 1 | 0: the balloon of the game as before |
| `v_bubbleRange` | 1200 | how far away a bubble is still drawn. It fades over the last quarter |

- A bubble pops up a little too large and settles, and rises and fades at its end.
- Over the own head it is no larger on the screen than it is with the camera 80 units away, where it usually
  is: a wall behind the player pushes the camera up to his neck, and what he had just written stood across the
  whole screen.
- Seen in a mirror of the level a bubble is turned as far as the mirror turns the view: it faces the one who
  looks, not his reflection.
- Team chat has the colour of the team around it. Private lines never get a bubble. No bubble on a dead or
  cloaked player, and none on oneself in first person.
- The light around a bubble is a glow surface: `r_DynamicGlow 1` makes it shine.
- It works on any server: the cgame knows who said a line (the client number a server built on OpenJK appends,
  else the name in the line).

It needs the engine of this tree (`v_cgameScene 1`): the text comes from its TrueType font as polygons
(`R_Text3D`), and the pictures of the panel are computed in the cgame and handed to the renderer
(`R_RegisterShaderRGBA`), so no file of a pk3 is involved and a pure server takes nothing away. In a stock
`jamp.exe`, beyond the range and with `v_chatBubbles 0` the balloon of the game stays. A legacy client on the
same server sees the balloon as it always did. Not run: nobody has seen a bubble, the sizes and colours are
numbers.

### Statuses above the heads

`cgame/status/` and `game/status/g_status.c` (2026-10-11). One line in smaller letters right above the head, a
chat bubble stands on top of it.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_playerStatuses` | 1 | show the lines of the others |
| `v_shareMusic` | 0 | tell the server what music plays on this machine. Off by default: everybody on the server reads it. "Show My Music" in Setup, Voice Chat |
| `v_caps` | 3, read only | in the userinfo, what this cgame draws that a server sends to nobody else: bit 0 statuses, bit 1 who plays with a client of this tree ("Who plays with VisionJK") |
| `v_vision` | the version, read only | in the userinfo: this is the client of VisionJK, and which one. A server of this tree greets it by that name and not as "client plugin 1.4B4". `cjp_client` stays 1.4B4: it is what a JA+ server of somebody else reads to switch the plugin features on |

- The line of the player the view is behind (oneself in third person, or whom one follows) is not drawn since
  2026-10-12: it stood right before the camera, as large as a wall. The card and the scoreboard say it.
- Two players who stand close have their lines at the same height. The one that would lie on another on the
  screen is lifted above it (`Bubble_LiftLine`), and a bubble stands on top of that.
- "Away from keyboard": the server finds out who did not touch a key or the mouse for `vs_afkTime` seconds, for
  legacy players too.
- "Listening to Spotify: artist - title", "Listening to YT Music: title", or "Listening to artist - title" for
  any other player of music. On Linux it is asked from the players over D-Bus (MPRIS) every five seconds:
  `libdbus-1.so.3` is loaded when `v_shareMusic` is first 1 and is not needed otherwise, the questions are
  sent without waiting for the answers. On Windows it is read from the window titles of Spotify and of
  YouTube Music (the window has to show the tab). Not on macOS and not in the browser client.

On a server with the game module of this tree (`vs_caps` bit 1) the server passes the statuses on. On
any other server they go through the masterserver since 2026-10-11 ([voice.md](voice.md), "On a server without
it"), for the players whose voice chat is connected to it (`v_voice 1`, `v_voiceMaster 1`): every client says
its own status there every five seconds and finds out by itself whether its player is away (a minute without
a key or a turn of the view). The music is still only told with `v_shareMusic 1`, and there it also passes
the machine of the master. Only clients of this tree with its engine draw the lines.

| Command | |
|---------|---|
| `v_statuses` | what stands above whom, and whether the server or the masterserver tells it |
| `v_status_test <text>`, `v_status_test stop` | said as the music of this player in place of what plays, for tests. `v_shareMusic` still decides |

On Windows the lines by the server, through the relay for servers without the module and from the title of a window ran on 2026-10-11,
see [windows.md](windows.md).

### Clickable players

`cgame/select/cg_select.c` (2026-10-11). While the mouse is free, the player under the pointer has a glowing
outline and his name beside him, and a click on him opens a card: who he is and with what he plays, his score,
ping and time, what stands above his head, his model and saber, and what you can do about him.

The mouse is free while the key of `+v_cursor` is held (Controls, Extras, "Free the Mouse"). The left Alt key
gets it once, the first time the menus start, if no key has the command and Alt can be spared: it is free, or
it holds the `+altattack` of the game's defaults while another key (the second mouse button there) fires the
same. Before 2026-10-12 only a free Alt was taken, and since the defaults bind it a new install had no key.
Any other bind on Alt stays. The mouse is also free after `cursor` until `cursor` again or Escape, while the chat box is open, and
while the scoreboard is kept open (below). While it is free the view does not turn and the game does not get
the keys.

The scoreboard (2026-10-12, `cgame/hud/cg_scores.c`, see [ui.md](ui.md)): a click on the row of a player opens
the same card beside the board, also for somebody who is not in sight, who watches, or for oneself (the own
card says who one is and offers nothing). The key of the scoreboard is held while the board shows and the mouse
turns the view, and holding it together with left Alt switches windows on most desktops, so the board can be
kept: its key pressed twice within 350 ms, or the command `v_scores_pick` (Controls, Extras, "Scoreboard with
Mouse"), leaves it standing with the mouse free. The key again, Escape or the right button lets go of it. The
head of the board says which. `v_scoresPick 0`: no double tap. A press is the key going down: what the system
sends again while a key is held (key repeat, as `+scores` without a `-scores` between, or as the key itself
while the cgame has the keys) is none (`CG_ScoresPress()`, `CG_ScoresRelease()`). Until 2026-10-12 it counted,
so a key that was held was a double tap after a moment, and from then on the repeats kept the board and let go
of it by turns: the owner saw the board "spamming" while he held Tab. Checked with a script that sends what a
held key sends (`clienttests/scores_hold.cfg` of the test tools, four pictures, before and after). On a window that has no room for the card at
the right of the board (4:3) the board stands further left while it is kept. When the level restarts the mouse
goes back to the game: the board is let go, an open card closes, and a pointer that `cursor` switched on is
off.

| On the card | |
|-------------|---|
| Mute his voice, Hear his voice again | `v_voice_mute`, for you alone |
| Voice quieter, Voice louder | this player from silent to twice as loud, in quarters, for you alone and until the level changes |
| Hide his chat lines, Show them again | his lines are left out of your chat box |
| Whisper to him | the chat box opens for a line only he gets |
| Copy his name | to the clipboard, without colours |
| Watch him | as a spectator |
| Call a vote to kick him | asks for a second click |
| Admin: kick, silence, sleep, slap, go to him, bring him here | only after a login with a key (`v_amkey login`) said you are an admin. The server decides as always |

A row is run with a click or with its number. A click beside the card, the right button or Escape closes it.
"Watch him" and the two admin rows that move somebody also let go of a scoreboard that is kept: it would be in
the way of what was asked for. A card ends 60 units above the lower edge of the screen, above the bars of the
HUD. A row or a line of the head that is a little too long is set smaller (down to 80 %), and only what is
still too long is cut. The own figure is never lit, also not while the own card is open.

| Cvar | Default | |
|------|---------|---|
| `v_selectPlayers` | 1 | 0: none of it |
| `v_selectOutline` | 1 | 0: no outline, four corners around the player in its place |
| `v_scoresPick` | 1 | 0: the key of the scoreboard pressed twice does not keep the board open |

| Command | |
|---------|---|
| `+v_cursor` | the mouse is free while it is held |
| `cursor` | switches that on and off |
| `v_scores_pick` | the scoreboard stays open with the mouse free, or lets go of it |
| `v_cgKey <key> [up]` | a key or a mouse button for the cgame while it has the keys, by name (esc, tab, enter, mouse1, mouse2, wheelup, wheeldown, pgup, pgdn) or number: for a script in place of a hand, as `v_uiKey` is for the menus |
| `v_peers` | who plays with what, as this client knows it, and from where |
| `cursor on <player>`, `cursor at <x> <y>`, `cursor click`, `cursor row <n>` | the pointer onto a player or a place, a click, a row of the open card: for a key or a script in place of a mouse |

- Everything happens in your client. What a row sends is the command it stands for, and no other player sees
  that you pointed at him. An unchanged client has none of it and notices none of it.
- Who points does not type. An engine marks its player as talking while anything holds his keys, the game
  module shows that to everybody (the dots over his head) and holds him still for it. While the keys are held
  for pointing only, the cgame says so (`v_catchQuiet 1`) and an engine of this tree leaves the talk button
  out of his commands (`CL_CmdButtons`). A key that was down when the mouse was freed stays down: who runs
  keeps running. In another engine he is shown as talking, as it was until 2026-10-12 here too.
- The three lines at the head of the card stay on the card: a long name or a long title of music is set a
  little smaller, and what is still too long ends in three dots.
- A player behind a wall, a dead one, a cloaked one and yourself can not be pointed at. A challenge to a duel
  is not on the card: the server wants your aim on the other player for that.
- The outline is a thin line at the edge of the figure and a soft light around it, and nothing of either lies on
  the figure.
  - With the dynamic glow of the renderer (`r_DynamicGlow 1`) the light is a real blur. The model is added once
    more with a shader that only the buffer of the glow gets (`RGBA_SHADER_BLUR`), pushed out a little along its
    normals, in the colour of the outline. The renderer blurs that buffer as it does for every light of the game
    and adds it to the picture. The player himself and the blades of his saber are drawn after that
    (`RF_AFTERGLOW`: both renderers run their list a third time for such entities, after the glow has been
    added), so he lies on the light. How wide and how strong it is follows `r_DynamicGlowPasses`,
    `r_DynamicGlowDelta` and `r_DynamicGlowIntensity`, like every glow.
  - Without it (`r_DynamicGlow 0`) the light is the model six more times, each pushed out further and fainter
    (0.5 to 4 units), with the player drawn after everything else of the scene (`RF_FORCEPOST`).
  - The line is the innermost of those shells in both cases.
  - The price in both: a blended thing of somebody else that passes in front of that player (a blade, smoke) is
    covered by him while he is marked.
  - The engine of this tree makes the shaders (`RGBA_SHADER_HULL_WIDTH`), so the outline is there on a pure
    server without the pack. Another engine takes `gfx/vision/select_hull` from the pack, one shell and no
    fade, and without that the player gets four corners.
  - Found while it was made: a model surface is freed the last time it is drawn in a view, which was "in the
    glow pass if its shader glows". The hilt of a saber glows, was freed there and drawn again after the glow:
    a crash on both renderers at the first outline. `R_GhoulSurfaceDone()` is the one rule now, for both.
- The name and the card are things in the level, as the chat bubbles are (`Select_Boards()`, called while the
  scene is put together): as large there at any distance (a unit of the card is 0.3 units of the level, which
  gives its name letters as tall as those of a bubble and makes the card about 45 units wide and 42 high with
  seven rows), upright, turned to the one who looks about their upright axis only. They stand beside their player: at his right as the camera sees him, 21 units from his middle (13 until 2026-10-12, where they lay on his arm),
  their top at the height of his head. At his left where the screen ends at his right, and lifted, lowered or
  moved aside as far as they would reach over an edge of the screen (since 2026-10-17; before it was 40 units at
  most, and the card of a box at the feet of the player hung over the edge): the view can not be turned to them
  while the mouse is free. Very near they are never larger than 1.5 times the size they were made for
  (`SELECT_NEAR_MAX`, 2026-10-17): a step in front of a box they were larger than the screen. They follow their place a little late (most of the way in 90 ms), so a step of his
  is no jump of theirs. The card opens where the name stood.
- A row is clicked through the level: the way of the pointer from the eye is cut with the plane of the card
  (`Board_Pointer()`), and the place on it says which row.
- Too far away to be read, the card grows (`Select_Card()`): when its rows would be lower than 15 pixels it
  pops, a little too large for a moment (180 ms), to the size it was made for and keeps that size on the screen
  however far away its player is. It shrinks back the same way when its rows at the size of a thing in the level
  would be higher than 17.5 pixels again. The measure is pixels, so the distance depends on the window: about
  330 units at 2560 x 1440, 250 at 1920 x 1080, 165 at 1280 x 720 with a field of view of 80. Grown, its rows are
  at least 18.5 pixels high, so growing never makes it smaller.
- Grown, it is still the plane beside its player, seen by the camera of the scene and moving with it. A first
  version drew it flat on the screen with the HUD: it trembled against the level with every move of the camera
  ("very jittery").
- Name and card are drawn in a scene of their own right after the scene, with the same camera and nothing else
  in it (`CG_SelectOverScene()`): nothing of the level, no figure and no light added afterwards lies on them.
  Until 2026-10-17 only a grown card was drawn there, and a card at the size of a thing in the level was in the
  scene and depth tested: the floor and the walls beside its player cut it ("half of the menu appears in the
  ground or walls"). Seen in a hidden run since: the rows of a card that reach over the floor are whole.
- The name of a far player does the same without a pop: its letters are never lower than 14 pixels. From the
  distance where they would be it grows as much as he is further away.
- Since they are drawn over the scene, a figure or a wall in front of them no longer covers them.
- The rows for his voice (mute, quieter, louder) are only there for a player known to play with VisionJK
  (`CG_PeerVision() == 1`, 2026-10-17). Before, everybody nobody knew about got them, which on a legacy server
  is nearly everybody. Whisper (`tell`), hiding his chat lines, copying his name, watching, the vote and the
  admin rows work with any client and stay.
- An engine that can not write in the scene gets name and card on the screen with the HUD, at one size, at the
  same place beside the player. That path is the code of before and was not run again.
- The pointer is drawn after the card, by `CG_SelectDraw()` also while the chat box is open, which used to draw
  it itself before the card.

Checked 2026-10-11 with two scripted clients on the test room (`tools/uitest/select.sh`): the outline around
the other player, his name, the card with its seven rows beside him, also after the view turned away, and the
rows for his loudness and his chat lines changing what the card says. It found
a crash at the first outline, fixed before it was committed (the picture of the shader was handed to the
renderer from memory that can not be written to). Not checked: a real mouse and the key, a team game, the
admin rows, another engine.

What the card says and offers was written in English only until 2026-10-12. Now the cgame asks for its texts
in the language of the game: `CG_VisionText( "english" )` looks in `strings/<language>/vision.str` under a key
made from the English, as `tools/menus/texts.py` makes it, which has the list (`CGAME`) and the German. A text
with a value in it (`CG_VisionTextWith`) gets the value where it has `%i` or `%s`. It is never handed to printf
as a format: the file can come out of the pk3 of anybody. The same holds for the scoreboard, the chat box and
the lines above the heads.

### Who plays with VisionJK

`cgame/status/cg_peers.c`, `game/status/g_status.c` (2026-10-12). The scoreboard has the icon of the game before
the name of a player who has a client of this tree, and the card of a player says "VisionJK" with its version,
or "Another client".

- On a server with the game module of this tree: the cgame names itself in its userinfo (`v_vision`), and the
  game module tells a cgame that asks (`v_caps` bit 1) with the server command `jpclient <client> <version>
  [...]`, "-" for any other client. One player when his userinfo says something new, everybody in one command
  when a cgame asks for all of it: a cgame that has just started knows nothing (a new level, but also a restart
  of the video, of which the server hears nothing), so it sends `vs_status all` and is told who plays with
  what and what stands above everybody. The server info says that the module does this (`vs_caps` bit 3).
  An older module is not asked.
- The question is not sent in the first moment, when the voice chat says its hello, and it is sent again while
  no answer came (after 1.5 s, then every 3.5 s, six times at most): the flood protection of a server
  (`sv_floodProtect`, on by default) drops a command that comes within a second of another one without a word.
  The first version asked once and was only ever run against servers with the protection off.
- On any other server nobody tells. There a client of this tree is known by what comes from it through the
  voice chat: its voice, or what stands above its head, which every client says to the masterserver every five
  seconds. So it is known of the players whose voice chat is linked to the master (`v_voice 1`,
  `v_voiceMaster 1`, how a client starts out), within some seconds, and without a version. That somebody
  plays with something else is never known there: who was not heard may have the voice chat switched off. The
  card says nothing then.
- A bot is no client. The own player is always known.
- It is what a client says of itself. A mark, nothing an admin can rely on.

Checked 2026-10-12 with four scripted clients of the build on a server of the build (`BUILD=1
CAM=ui_scores_cam.cfg tools/sitescene3.sh` of the work folder): the icon before all four names, also after the
one who looks restarted his video, the version on the cards, and `v_peers` naming the server as the source. And
with three scripted clients on a server that tells nothing, linked through the relay of the masterserver on this machine (a program of its own then)
(`SOCIAL=1 tools/voicetest/e2e_master.sh`, three new checks): each knows of the two others that they play with
VisionJK, and that the masterserver is where it knows it from. Not checked: a client that is no client of
this tree on either server (no such program was run, bots stand in), the masterserver on its real machine, the
internet.

### The server browser in a game, and servers with bots only

2026-10-11. The bar of the in-game menu has a tenth button, SERVERS (`ui/jamp/ingame.menu`, the file of the base
game with buttons 63 wide where they were 70). It opens `ui/jamp/ingame_servers.menu`: the browser of the main
menu on a dark pane over the game, with all its filters and pop-ups. Back and Escape return to the bar. Joining
asks first, since it leaves the server one is on.

Both browsers have "View Bots Only" (`v_browserShowBotOnly`, default 1). With 0 a server on which nobody but
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
| `v_immersiveMenu` | 1 | 0: the flat menu of the base game |
| `v_menuBlur` | 1 | how far the level behind the menu is out of focus (rd-vulkan-vision and the client in a browser): 0 not at all, 1 normal, up to 4 |
| `v_muffle` | set by the cgame | 0 to 1: the sound of the game through a low pass. Not stored |

- The panel behind the menu is as large as what is open on it: the bar at the top alone is a bar, and the
  panel grows when a page opens under it. The ui says once a frame how much of the 640 x 480 its menus take
  (the cvar `v_openBox`, "x y w h", `Menus_OpenBox`), and the panel follows within a moment. With a ui that
  says nothing it is the whole 640 x 480, as it was.
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
- Behind the menu the cgame adds a panel with an edge and a soft light to the scene. Its shaders do not test
  the depth either (`RGBA_SHADER_OVER`) and are sorted last, so it stands over the whole scene like the menu: a
  wall or a pillar between camera and panel took a piece out of the panel and left the menu in the air. Moving
  the panel towards the camera until the level has room for it was tried first and can not work where the
  wall is next to the camera. The HUD is hidden meanwhile. The inside of the panel lets a tenth of the level
  through and its light is blended, not added (`RGBA_SHADER_BEHIND`): at half and with an added light its rim
  was white in front of a bright wall.
- The level behind the menu is out of focus, by distance (`VK_BlurFrame` of rd-vulkan-vision, both of its
  devices): the frame goes into a buffer of half its size, then passes widen it, three at `v_menuBlur 1`. A
  last pass (`shaders/focus.frag`) reads the depth of the scene and gives every pixel of that soft picture how
  much of it shows: nothing up to the distance of the menu, which the cgame hands over
  (`R_SetDepthOfField`), all of it at two and a half times that distance. So the player next to the menu and
  the floor he stands on stay sharp. The panel is left out of the scene while it is drawn and put onto the
  frame afterwards, as sharp as the menu. What is blended and writes no depth (a saber blade, smoke) is as
  soft as what is behind it. Around the player the soft picture has some of his colour in it: it is a blur of
  the whole frame. rd-vanilla keeps the scene sharp. The first version (cvar `r_sceneBlur`) ended the game as
  soon as a menu opened, because the pass asked for the descriptor set of the frame where only the sets of the
  glow buffers were known, and after that was repaired it went through the 320 x 240 buffers of the glow,
  which left nothing of the scene to recognise. Until 2026-10-11 the whole picture was soft, the player
  included.
- The sound of the game (effects, music, Steam Audio) and the voices of the voice chat are muffled. The low
  pass of the engine (`S_MufflePaint`) keeps what it has after a sample by the time of the sample: the mixer
  paints the same fifth of a second again with every frame, and a filter that just ran on from one paint
  into the next gave a crackle with every frame.
- The camera is pulled in where a wall is behind the player. It is behind where he looks, followed over a
  moment: with a menu open he does not turn by himself, but a teleporter or an admin can turn him. At first
  the camera stayed where he looked when the menu opened, and after such a turn it looked him in the face
  from inside a wall.

Not while spectating, dead, zoomed or at the end of a map (the camera has no player to stand behind), and not
in another engine (`v_cgameUIPlane`). The first version was seen in the game on 2026-10-11 and changed after
that (silhouette of the player in the menu, camera, blur, crackle). The layout as it is now was seen in
pictures of scripted clients the same day (rd-vanilla, so without the blur): the player at the left from
behind, the menu to the right of him over the level, the model of the player menu inside its frame.

### Admin login with a key

`cgame/auth/cg_auth.c`, `cgame/auth/cg_auth_agent.c` (2026-10-11), the server part is in [server.md](server.md).
An admin logs in with an Ed25519 key, the kind ssh uses, and no password goes over the wire.

| Command | What it does |
|---------|--------------|
| `v_amkey new [comment]` | makes a key and writes it to a file only the player can read, in the format of OpenSSH, with its `.pub` next to it |
| `v_amkey show` | prints the public lines of the keys there are. The owner of a server puts one into his `admin_keys.txt` |
| `v_amkey login` | logs in on this server, and from then on by itself whenever the player joins it |
| `v_amkey forget` | not by itself on this server any more |

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_authKeyFile` | empty | the key file, `~/` is the home folder of the user. Empty: `admin_ed25519` in the home folder of the game |
| `v_authAgent` | 0 | 1: the Ed25519 keys of the ssh-agent are used too |
| `v_authAgentPath` | empty | where the agent listens. Empty: `SSH_AUTH_SOCK` (Linux, macOS), the pipe of OpenSSH (Windows) |
| `v_authAgentKey` | empty | only the key of the agent whose comment or line has this text. Empty: each in turn, eight at most |
| `v_authServers` | empty | where the login runs by itself, kept by `v_amkey login` and `v_amkey forget` |

- The key file is what `ssh-keygen -t ed25519` writes, so an existing key can be named with
  `v_authKeyFile ~/.ssh/id_ed25519`. A key with a passphrase is not read: that is what the agent is for.
- With `v_authAgent 1` the secret half never reaches the game: the agent signs. 1Password is such an agent
  (Linux: `v_authAgentPath ~/.1password/agent.sock` unless `SSH_AUTH_SOCK` points there already, Windows: its
  pipe is the one of OpenSSH). It asks before it signs, and nothing waits for that: the question is sent and
  looked at once a frame, the game goes on. Two minutes are given, and if the 30 seconds of the server ran out
  meanwhile the login runs once more by itself.
- Only Ed25519 keys of the agent are used, the server checks no other kind. An agent that has only RSA or
  ECDSA keys gets a line that says so from `v_amkey show` and `v_amkey login` (1Password offers the keys of the
  vaults its `agent.toml` names, of whatever kind they are).
- The client signs only for a server the player named with `v_amkey login`, and only right after it asked
  itself. A server that sends a question nobody asked for gets no answer.
- A server with flood protection (`sv_floodProtect`, on by default) drops a command that comes within a second
  of the one before and says nothing. So the commands of a login go out that far apart (the client reads the
  cvar from the serverinfo), and one that gets no answer within three seconds is sent again, twice at most.
  With several keys in the agent the server is asked for one after the other, about a second and a third each:
  `v_authAgentKey` names the right one. Until 2026-10-11 the commands went out at once, and on a server with
  flood protection the login ended with "The server did not answer the login." unless the first key was the
  right one and the agent took longer than a second to sign.
- What is signed starts with the line `VisionJK admin login v1`. What ssh lets a key sign when it logs in
  somewhere starts with four bytes of a length, so no server can use this login to get a signature that opens
  an ssh account.
- Works in a jamp.exe too (client commands, a file and a socket are all it needs). Not in the browser client:
  a page has neither a key file nor a way to an agent.

Run on 2026-10-11 against a server on this machine with scripted clients (`japlus_port/tools/sectest/e2e.sh`):
a key file made with `v_amkey new` (mode 600, read back by `ssh-keygen`) and its login as council, a login as
knight through an `ssh-agent` started for the test, a one-time code, and a rank password that is refused with
a line that says why. Not run: an agent that asks its user first (1Password). On Windows the key file and the
OpenSSH agent of Windows both logged in on 2026-10-11, see [windows.md](windows.md). That server had
`sv_floodProtect 0`, which is why the run did not show the dropped commands. The spacing of the commands is
not run against any server yet.

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
