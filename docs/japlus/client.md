# Client: cgame and ui

The original JA+ client plugin is a replacement `cgamex86.dll` and `uix86.dll` plus assets. Its features are built into
the cgame and ui of this tree. The client reports itself to the server as plugin 1.4 (userinfo `cjp_client`, `1.4 ^5VisionJK ^7(^5jka.vision^7)`),
which is what switches the plugin dependent behaviour of the server on. A JA+ server reads the number at the start
of that value (anything below 1.0 is no plugin), so it has to stay first and stay 1.4, the version the cgame predicts as.

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
| Video | Video Mode | a list the ui fills: "Desktop", the size of the screen (`r_mode -2`), the sizes of `r_availableModes`, and common window sizes (1024 x 768 up to 3840 x 2160) that are not larger than the largest of those, so a display that lists only its own mode (Wayland) still offers smaller windows (`Item_Multi_FeedVideoModes()` in `codemp/ui/ui_shared.c`). A size is `r_mode -1` with `r_customwidth` and `r_customheight` |
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

The rows of the disruptor scope (Interface, heading Disruptor: Scope Lens, Zoom with Mouse Wheel, Motion Tracker Speed, the speed for the tracker) are described under "The disruptor scope". The classic menus do not have them.

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

A window that is resized while the game runs (a tiling window manager does that to every window, and the window is
resizable) keeps this layout. `CL_WindowSize()` in `codemp/client/cl_main.cpp` watches the drawable size of the
window every frame (`WIN_DrawableSize()` in `shared/sdl/sdl_window.cpp`). Once a new size has stayed the same for
200 ms, rd-vulkan-vision makes its swapchain and targets again in it between two frames (`re->ext.WindowResized`,
`VK_ResizeFrame()`), `v_2dBias` is set again and `cls.glconfig` is updated. Then the cgame and the ui read the
glconfig again on their next frame (`CG_CheckScreenSize()`, `UI_CheckScreenSize()`), so the grid, the menus and
the HUD fit the new size. Modules of this tree say that they do this with the cvars `v_cgameResize` and
`v_uiResize` (the engine resets both when it loads a module). With rd-vanilla, the OpenGL ES device, or a cgame
or ui of another tree the client does a `vid_restart` in the new size instead. That restart gets the size once
(`WIN_SetNextSize()`), it is not written into `r_mode` or `r_custom*`. A page in a browser keeps its own way
(`boot.js`). A fullscreen window is not resizable.

The modules read `v_2dBias`. In a stock `jamp.exe`, or with a renderer of another tree, the cvar is missing or
0 and they draw as they always did.

Not looked at in the game: it was built for Linux and nothing more. The live resize was built for Linux (Vulkan
device, OpenGL ES device, rd-vanilla) and Windows (mingw x86_64) and not run, not on Hyprland either. Open: the siege timer and the automap are
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

`tay_consoleClock` (default 0) puts the clock of the machine left of the version in the console's lower right
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

`qcommon/cmd.cpp` (2026-10-17), the commands of TaystJK with its syntax, under names that begin with `tay_` (TaystJK
calls them `delay`, `waitf`, `ifCvar`, `strSub` ..., so a script of TaystJK needs the prefix added).

| Command | What it does |
|---------|--------------|
| `tay_delay <ms>` | what follows it in the line or the bind runs that many milliseconds later: `bind x "+attack;tay_delay 300;-attack"`. Everything else goes on in the meantime |
| `tay_waitf <frames>` | the same in frames |
| `tay_delaycancel <text>`, `tay_waitfcancel <text>` | drop what waits and has the text in it, `""` all of it |
| `tay_ifCvar <cvar> <test> <count> <command ...> ...` | runs the first command whose test the cvar passes. A test is a value, or `$=` `$!=` `$>` `$<` `$>=` `$<=` with a number, `$contains` `$beginswith` `$endswith` with a text, or `$else`. A value that begins with `$` is that cvar's. `<count>` is the words of the command, itself included: `tay_ifCvar cg_thirdPerson 0 3 set cg_thirdPerson 1 $else 3 set cg_thirdPerson 0` |
| `tay_strSub <command ...>` | runs the command with `$cvar$` replaced by the value: `tay_strSub say_team my fov is $cg_fov$`. `$$` is a `$` |

- Quotes inside quotes: a quote that a letter follows opens a quote inside the quote, so
  `bind x "say "a b""` binds `say "a b"`. That holds for what runs on this machine (console, configs, binds),
  not for what a server or a client sends. A quote outside quotes opens one as it always did.
- At most 64 delayed lines wait at the same time.
- `tay_ifCvar` and `tay_strSub` give out no password: a cvar the engine hides and any cvar with `pass` or `pwd` in its
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
| `tay_killfeed` | 1 | the killfeed, 0 none |
| `tay_killfeedSize` | 1 | its size, 0.5 to 2 |
| `tay_killfeedTime` | 5000 | milliseconds a line stays, the last second fading |
| `tay_killfeedX`, `tay_killfeedY` | 0 | moves it left and down |

The movement HUD (`cgame/hud/cg_movehud.c`) is after TaystJK's `hud_strafehelper.c` (from jaPRO and the CGaz of
DeFRaG), written anew for the physics of the game (`bg_pmove.c`: `pm_accelerate` 10 on the ground,
`pm_airaccelerate` 1 in the air, friction 6, stop speed 100). The movement styles of jaPRO are left out. The bars
show where the view has to point. The middle of the screen is where it points now. Everything is off by default.

| Cvar | Default | Meaning |
|------|---------|---------|
| `tay_speedometer` | 0 | the speed along the ground under the crosshair: green when faster than running, gold when much faster |
| `tay_speedometerJumps` | 5 | under it, the speeds at the last jumps (when the ground was left), 0 none |
| `tay_speedometerSize`, `tay_speedometerX`, `tay_speedometerY` | 1, 0, 0 | its size and place |
| `tay_strafeHelper` | 0 | a bar under the crosshair for the keys that are held. Dim: speed starts to come. Bright: the best angles (from the best one to the one where the speed stays). Yellow: still faster, but the speed goes down past its end. The white mark is the view |
| `tay_strafeHelperColor`, `tay_strafeHelperY` | `0 1 0.4 0.75`, 0 | its colour and place |
| `tay_snapHud` | 0 | in the air, the zones of the angle where the rounding of the velocity after every move (the `SnapVector` at the end of `PmoveSingle`) gives a little more speed, in two alternating colours. They depend on the frame rate (`com_maxfps`, or `pmove_msec` with `pmove_fixed`) |
| `tay_snapHudColor1`, `tay_snapHudColor2`, `tay_snapHudY` | | its colours and place |
| `tay_pitchHud` | 0 | a mark where the view would be at the pitch `tay_pitchHudAngle` (15, down positive), colour `tay_pitchHudColor` |

For a player one follows, the keys are what the server says of them (the direction of the move), and the speed
he wishes is his full one.

Not tried in the game: how it feels while running, and whether the angles of the bars are right to the degree.
They were only checked against the formulas.

The team overlay (`cg_drawTeamOverlay 1`, `CG_DrawTeamOverlay` in `cg_draw.c`) got TaystJK's extras:

| Cvar | Default | Meaning |
|------|---------|---------|
| `tay_teamOverlayScale` | 1 | its size, 0.5 to 2.5 |
| `tay_teamOverlayWeapons` | 1 | the weapon each one holds, after the numbers. The game drew it on the armour number, and with a `?` for a weapon not seen yet. Now it uses the icons of all weapons, registered at the start |
| `tay_teamOverlayBar` | 1 | health and armour as a thin bar under the numbers, in their colour |
| `tay_teamOverlayMaxHP` | 200 | health and armour that fill the bar |

There are no force points: only jaPRO servers send them.

Smaller ones:

| Cvar | Default | Meaning |
|------|---------|---------|
| `tay_crosshairColor` | empty | the colour of the crosshair with nothing under it, `r g b a` from 0 to 1 (`1 0.5 0 1` orange). Empty: white, as before. Over players and things it keeps the colours of the game (red, green, yellow), and `cg_crosshairHealth 1` still wins |
| `tay_crosshairNamesOpacity` | 0.5 | how opaque the name of the player under the crosshair is, 0 to 1. The game had 0.5 |
| `tay_chatLog` | 0 | every chat line the box shows, without colours and with the time, appended to `chatlogs/<date>.log` in the game's home folder (`~/.local/share/visionjk/japlus/chatlogs` on Linux). Lines of hidden players are not written |

Emoji in the chat box (as TaystJK, `cgame/chat/cg_chat.c`): a code in a line is drawn as the picture
`gfx/emoji/<file>.png`, as high as a row. The list is read from the paks at every start of the cgame, so a pak (also
one a server sends) can bring more, and `tay_emoji` lists them. The file names are TaystJK's: a `` ` `` is a colon of
the code, a `~` is a `>`, and `!` makes the next letter a capital (`` `kappa`.png `` is `:kappa:`, `#~`!D.png` is
`#>:D`); a name without those is the code between colons (`smile.png` is `:smile:`). Of two files with one code
the first one counts. This tree ships 78 pictures of Noto Emoji for the faces and things, under the names TaystJK
uses, and the 109 other pictures of TaystJK's set, its emotes of Twitch among them (2026-10-18, with permission),
so 187 emoji in all, the same codes TaystJK has (`assets/japlus_client/gfx/emoji` with a README of where they come
from). A Noto picture is 64 x 64: the image loader takes sides of a power of two only, a picture of 72 is refused
and no emoji shows. The real emoji characters of UTF-8 chat that have a picture are pictures too.
`tay_chatEmoji 0` shows the text. Only the chat box draws them: the bubbles above the heads and the console show
the text, and so does a client of another tree.

While a word that begins with a colon is typed in the chat box, with a letter more (`:ka`), a list of up to eight
emoji whose codes begin with it stands above the line, with their pictures: Up and Down choose, Tab, Enter or a
click puts the code in with a blank after it. Up and Down go through what was sent before again once the word is
done. The line to type in shows the codes as text. Checked in a hidden run: `:ka`, Down twice, Enter gave
`:kappapride:` and the line it sent showed the picture (`clienttests/lvl_emoji_list.cfg`, with `v_cgKey <code> char`
for the letters).

### More of TaystJK (round 2)

Taken over on 2026-10-19, after an in-depth comparison of TaystJK and this tree. Client only, unless said.

| Cvar | Default | Meaning |
|------|---------|---------|
| `tay_notify` | 2 | a chat line of somebody else that holds the own name (3 letters at least) or a word of `tay_notifyWords`, colours and case aside, stands on a gold strip in the chat box, 1, and chimes too, 2 (TaystJK's `con_notifywords`). The sender's name in front of the line does not count, the own lines never do |
| `tay_notifyWords` | empty | more words for it, separated by blanks or commas |
| `tay_hitSounds` | 0 | a short tick when the own shots or blade hit someone (jaPRO's `cg_hitsounds`). The server counts the hits anyway (`PERS_HITS`), the game had the sound commented out |
| `tay_killSound` | 0 | two notes when the own player kills someone else (jaPRO's `cg_killSounds`) |
| `tay_scoreboardClock` | 0 | the time of day on the scoreboard (TaystJK's `cg_scoreboardTime`): after the level in the head of the new one, in the upper right corner of the old one |
| `tay_timerMsec` | 0 | the timer of the level (`cg_drawTimer 1`) with milliseconds |
| `tay_timerCountdown` | 0 | the timer shows what is left of the time limit, when the server has one |
| `tay_movementKeys` | 0 | the keys of moving on the screen, crouch, forward and jump above, left, back and right below, lit while held (jaPRO's and TaystJK's `cg_movementKeys`). For a followed player what the server says: the direction of his last move while he moves, ducked, jump held |
| `tay_movementKeysSize`, `tay_movementKeysX`, `tay_movementKeysY` | 1, 0, 0 | its size and place |
| `tay_startGoal` | 0 | the speedometer is gold from this speed on, in the place of its own colours (TaystJK's `cg_startGoal`) |
| `tay_jumpGoal` | 0 | a jump at least this fast is gold in the list of jumps (TaystJK's `cg_jumpGoal`) |
| `tay_specHud` | 0 | while one watches (spectator or following), a map of the players around at the upper right in the place of the enemy info, in the picture of the radar of siege (TaystJK's `cg_specHud`, `hud/cg_spechud.c`): every player the snapshot has as an arrow turned the way he looks, red and blue by team, white without teams, larger when higher, smaller when lower; the edge is 1500 units away, farther ones stay at it. TaystJK's own map leaves the distance out (its line is commented out) and puts everybody in the middle |
| `tay_specHudScale` | 1 | its size, 0.5 to 2 |
| `tay_leadIndicator` | 0 | with a weapon whose shot flies (bryar, blaster, bowcaster, repeater, DEMP2, flechette, rocket, concussion), a dotted red line from every moving opponent in sight to where a shot fired now would meet him, and a mark there (jaPRO's `cg_leadIndicator`, `hud/cg_lead.c`). The speeds are the game module's. Not for team mates or players of the other JA+ dimension, whom shots pass through. The flight is worked out twice to the point it aims at; the alt fire of the repeater falls, so its mark is raised. On a server of this tree only the flight counts, since the server starts a missile as far along as the shooter's lag (`tay_unlagged` 2); elsewhere the own ping is added, as jaPRO does. 2: the ping is added everywhere. Many players count this as an aim aid, and no server of this tree can forbid it yet |

The three sounds (`sound/tay/hit.wav`, `kill.wav`, `notify.wav`) are tones made by `tools/sounds/make_tay_sounds.py`,
so no audio of anybody else ships. Nobody has listened to them yet.

Deaths on the scoreboard, which TaystJK has as `cg_scoreDeaths`, need nothing new: the scoreboards show score/deaths on
JA+ servers from 2.1 on, and the server of this tree is one.

The camera behind the player: `cg_smoothCamera` (OpenJK, default 0) has a row now. The damping of the camera
(`cg_view.c`) takes the time of a frame in steps of 50 ms through `Q_powf`, whose exponent is a whole number: above
20 frames a second it is 0, so every frame moves the camera the same part of the way, and it follows four times
as fast at 250 frames a second as at 60. Worked out for a running player (300 units a second, damping 0.3): 11.7
units behind at 60 frames, 5.6 at 125, 2.8 at 250; with `cg_smoothCamera 1`, which takes the real power, 39.6,
40.9 and 41.5. The default stays 0, since 1 makes the camera hang much further back than players know it. That is
what TaystJK's `cg_cameraFPS` is about too.

The spectator map and the lead indicator were checked with the same script and two other client scripts
(`A=lvl_lead_a.cfg B=lvl_lead_b.cfg`): the second client strafes left and right 400 units in front of the first, who
aims the rocket launcher (the dotted line ran from him to a mark on the side he ran to) and then watches him as a
spectator (his arrow ahead of the middle of the map, turned towards the spectator).

Checked in hidden runs on 2026-10-19 (`agent/japlus_port/tools/tay2test.sh`, a dedicated server and two clients):
the timer counting down with milliseconds, forward and left lit while held, the speed gold over a start goal of
200, the two lines of the second client that named the first one on the gold strip, the clock on the scoreboard.
The click pass of the new rows set every cvar (`clienttests/ui_tay2_rows.cfg`).

### The tab TaystJK

Everything that came from TaystJK has a name that begins with `tay_` (cvars and commands, also the commands
`tay_look` between client and server) and its rows in Setup, tab TaystJK (2026-10-17, `tools/menus/options.py`,
texts in four languages): HUD (Killfeed, Team Overlay Size, Crosshair Colour, Name Opacity, Scoreboard Clock,
Timer Milliseconds, Timer Counts Down, Hit Sounds, Kill Sound, Spectator Map, Lead Indicator), Movement (Speedometer, Strafe Helper, Snap HUD,
Pitch HUD, Strafe Trails, Movement Keys, Start Goal, Jump Goal), Hats and Capes (Hat, Cape, Show Them, Seasonal
Hats), Looks (Enemy Model, Ally Model, Saber Team Colours, Two-Coloured Staff, Jedi Outcast Shots, Disruptor Beam,
Smooth Camera), Vulkan Renderer (Bloom, Bloom Strength, Bloom Threshold, Render Scale, see renderer.md) and Chat
and Console (Chat Emoji, Chat Log, Name Alert, Console Clock). The binds with modifiers, word delete, `.oshader` and
the 16:9 pictures have no cvar.

For one evening the cvars began with `v_` and the script commands had TaystJK's names. `cvar_renames.h` has those
old names, so a value or a bind stored with one goes to the new name, with a line in the console.

Crosshair Colour is a row of a new kind of the generator (`multistr`, a `cvarStrList`), since its cvar holds a text.
The ui registers the cvars of the cgame that have a row (`ui_xcvar.h`), so the menus outside a game show their
defaults. Clicked in hidden runs on 2026-10-17: every row set its cvar as clicked
(`agent/japlus_port/tools/clienttests/ui_taystjk_rows.cfg`, then on the tab `ui_tay_rows.cfg`).

### Looks of TaystJK

Taken over from TaystJK on 2026-10-17. They only change what this player sees.

| Cvar | Default | Meaning |
|------|---------|---------|
| `tay_forceEnemyModel` | empty | every opponent in this model, `model/skin` (`kyle/default`) or a model alone (its skin `default`). In a game without teams everybody is an opponent |
| `tay_forceAllyModel` | empty | every team mate in this model |
| `tay_saberTeamColors` | 1 | in team games every blade in its team's colour, as the game does. 0: a blade keeps its colour, unless that is the other team's colour or one past purple (RGB of JA+, black). Not in siege |
| `tay_saberStaffColors` | 0 | the second blade of a staff in the player's second saber colour (also its RGB colour) |
| `tay_jk2Effects` | 0 | engine (`client/FxScheduler.cpp`, TaystJK's `fx_jk2`): the parts of 30 shot effects (blaster, bryar, flechette, repeater, rocket, turrets, vehicles ...) live one millisecond where their file gives no life, so a shot is drawn where it is in this frame without the smear of the frames before, as in Jedi Outcast. Counts from the next level (the effects are read at a level's start). No files of Jedi Outcast are needed |
| `tay_disruptorBeam` | 0 | the disruptor shot as a line with a glow, 1, or with a spiral of sparks around it as the rail of Quake 3, 2 (TaystJK's `cg_disruptorNew` and `cg_disruptorSpiral`). 0 the shot of the game |
| `tay_disruptorColor` | empty | its colour, `r g b` from 0 to 1, empty cyan. In team games the team's colour |
| `tay_strafeTrail` | 0 | strafe trails as jaPRO and TaystJK have them (`cg_strafetrail.c`): a glowing line on the ground along where a player ran, 1 oneself, 2 everybody. Oneself green, the teams in their colours, the others white. Only from the places this client sees anyway |
| `tay_strafeTrailLife` | 3 | seconds a trail takes to fade, 0.5 to 30 |

These work without `cg_forceModel`, which puts everybody in one's own model. When both are set, these two win for the players they cover. A model
this machine lacks shows as the model of the game, as with any player. When one's own team changes, everybody
is looked at again (`CG_ForcedModelsCheck`).

### Hats and capes

`cgame/look/cg_look.c`, `game/status/g_status.c`, `master/ms_relay.c` (2026-10-17), after the cosmetics of TaystJK
and jaPRO. Their files are laid out the same way, so their paks work:

- A hat is `models/cosmetics/hats/<name>.md3`. It sits on the bolt `*head_top` of the player.
- A cape is `models/cosmetics/capes/<name>.md3` on `*back`.
- `settings/cosmetics/hats/<name>.cosmetic` (or `capes/`), JSON in TaystJK's format, moves one for a model and
  skin. The skin is matched by its name, or by the longest prefix written with a `*`. Without a skin match, the
  model's offsets apply when `modelFallback` is true, else none. TaystJK added the offsets along the axes of the
  world. Here they go along the bolt: x forward, y right, z up. For a file that only lifts or lowers, that is the
  same.

This tree ships all 24 hats and 8 capes of jaPRO and TaystJK, with permission for all of them (2026-10-17):
afro, beard, bucket, cap, cringe (a propeller cap), crown, fedora to fedora4, glasses, gradcap, headcrab, horns,
mario, mask, metalhelm, plaguemask, predatorhelm, pumpkin, santahat, sombrero, supersaiyan, tophat, and the
capes ak47, crowbar, goose, grogucape, royalcape, rpg, vadercape, yodacape (`assets/japlus_client/models/cosmetics`,
with a README of where they come from, and `shaders/vision_cosmetics.shader`). Setup, Interface, Looks has the
rows Hat and Cape with these names. With the pumpkin there, Seasonal Hats has its Halloween hat too.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `tay_hat`, `tay_cape` | empty | what this player wears, the name of the file |
| `tay_cosmetics` | | lists the hats and capes of the paks and what one wears. `tay_cosmetics hats <name>`, `tay_cosmetics capes <name>` (`none` takes it off), `tay_cosmetics clear` |
| `tay_showCosmetics` | 1 | 0 nobody wears anything here, 1 everybody what he says, 2 everybody what this player wears |
| `tay_seasonalCosmetics` | 0 | a player without a hat wears `santahat` from 22 November to 7 January and `pumpkin` on 31 October, where the paks have them |

Who sees them is every player of this tree, on any server:

- On a server with the game module of this tree (`vs_caps` bit 7), the cgame sends `tay_look <hat> <cape>`.
  The server hands `tay_look <client> <hat> <cape>` to every cgame that draws them (`v_caps` bit 2), also with
  `vs_status all`. The cgame sends again every four seconds until it hears its own look back, at most six times.
- On any other server, the look goes through the masterserver (`VOICE_LOOK`, see `bg_voice.h`). It needs the voice
  chat linked to the master, as the statuses do. The master hands it only to clients that send looks themselves,
  so a VisionJK client from before never gets a type it does not know. A look the master did not say again for 15
  seconds is over.
- TaystJK's way, so its players and those of this tree see each other's hats on any server (2026-10-18): the name
  after the digits of the saber colours. `color1` "3tophat" is the saber colour 3 and the hat tophat, `color2` the
  cape. The cgame writes the own hat and cape there once a second (a server keeps 15 characters, so a name that
  does not fit with the digits is left out, TaystJK takes 14 at most) and reads the others' from `c1` and `c2` of
  their info. The game module, a stock one and as far as known any other passes the text on as it is. Everything
  that reads a saber colour reads the number in front alone (`atoi`), the menus too, so the colour stays.
- On a server of jaPRO: `c5` of a player's info, a bit for every jaPRO cosmetic he unlocked there (the userinfo
  `cp_cosmetics`, which the server checks). All 32 bits are models shipped here. A mask, a beard, the plague mask
  or the glasses are worn with a hat, the goose with a cape, as TaystJK draws them. `cp_cosmetics` is written from
  the own hat and cape, so a jaPRO server shows them where its player unlocked them.
- What decides: what a player says through the server or the masterserver, else the names in his saber colours,
  else the bits of jaPRO. `tay_cosmetics` lists for each player what he wears and how it came.
- A player whose hat this machine lacks wears none here. Legacy clients see no hat at all: they would need the
  models by download.

Nothing else passes between TaystJK clients: they share the stock JA+ things (RGB sabers in `c3` and `c4`, the
typing flag) with this tree already, the camera and net settings in their userinfo go to the server alone, and
the emoji are text in the chat (TaystJK's emotes of Twitch stay text here).

The server part (game module and masterserver) only works once those run this build on the host.

Checked on 2026-10-17 with two hidden clients of the build (`agent/japlus_port/tools/looktest.sh`). One wore a
stand-in hat and cape (a coffee pot of the game, copied into the test profiles only), the other said with
`tay_cosmetics` what it heard: "hat coffeepot, cape potcape", through the server of the build, and with `MASTER=1`
through the masterserver of the build on a server with the stand-in for a foreign module. On 2026-10-18 the
same with `MASTER=1 NOMASTER=1`, a server that passes nothing on and no master: "hat crown, cape goose, in the saber
colours (TaystJK)". The bits of jaPRO were not tried, there is no jaPRO server here. The first runs found
two faults: the arguments of `tay_look` were read from one buffer, so the cape came as the hat, and
`BG_VoiceUnpack` refused the new packet type. In a level the stand-in hat sat on the head of a bot
(`tay_showCosmetics 2`) and the cape on his back. All hats and capes were then looked at on the own player in a level, the hats from the front, the capes from
behind and the side (`clienttests/lvl_hats.cfg`, `lvl_hats2.cfg`, `lvl_cape.cfg`): each sits where it should on the default model, the beard on the
chin, the glasses and the plague mask on the face. Other player models and their skins were not looked at.

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
| `v_shareMusicPlayers` | empty | the players of music whose songs may be shown, their names split by spaces (`spotify firefox`). Only those players are asked what they play. With nothing ticked nothing is shown, and a new player is not shown until it is ticked. "Choose Players of My Music ..." in Setup, Chat & Social |
| `v_caps` | 7, read only | in the userinfo, what this cgame draws that a server sends to nobody else: bit 0 statuses, bit 1 who plays with a client of this tree ("Who plays with VisionJK"), bit 2 hats and capes ("Hats and capes") |
| `v_vision` | the version, read only | in the userinfo: this is the client of VisionJK, and which one. A server of this tree greets it by that name and not as "client plugin 1.4B4". `cjp_client` starts with 1.4: it is what a JA+ server of somebody else reads to switch the plugin features on, the name after the number shows in its log and its plugin lists |

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

"Choose Players of My Music ..." (Setup, Chat & Social) opens a page with the players that run right now and the
ticked ones that do not run (marked "not running"). A click ticks one or takes the tick away. On Linux the list is
the MPRIS players on the desktop bus, each named by the part of its bus name after `org.mpris.MediaPlayer2.` up to
the next dot (`spotify`, `firefox`, `chromium`, `vlc`, ...). On Windows it is always the two the game can find
there: Spotify and YouTube Music (the keys `spotify` and `ytmusic`). macOS and the browser client have none. The
list is only looked for while the page is open.

Whoever had `v_shareMusic 1` before this version has to tick their players once: until then nothing is shown.

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

### The disruptor scope

`cgame/scope/cg_scope.c` (2026-10), with the lens pass of rd-vulkan-vision (see "Disruptor scope" in
[renderer.md](renderer.md)). While the disruptor is zoomed, the renderer draws the view of the level through a lens:
large and clear, bent and coloured a little towards its rim, sharp around what the crosshair is on. The renderer
also draws a fine reticle with a lit red dot, and around the rim, on a dark ring, three segmented arcs: the ammo on
the right (red when low), the zoom on the left, the charge of the alt fire at the bottom. Outside the lens the level
stays visible, soft, dimmed and a little grey. The lens has 0.45 of the height of the view as its radius
(`SCOPE_RADIUS`). The black mask of the game (`CG_DrawZoomMask()` in `cgame/cg_draw.c`) is left out then, with all
its art: no rotating ring, no ticks, no charge bar. The cgame draws only the zoom as text like "8.5x" left of the
lens, and the light of the mode under it.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_scope` | 1 | 1 the lens of the renderer, 0 the old mask. Saved. Setup, Interface, "Scope Lens" |
| `v_scopeWheelZoom` | 1 | the mouse wheel zooms while the zoom is locked. Saved. Setup, Interface, "Zoom with Mouse Wheel" |
| `v_scopeMode` | 0 | the tracker: 0 off, 1 on (an old saved 2 counts as 1). Saved, no menu row. The mode key turns it on and off (see below) |
| `v_motionThreshold` | 40 | units a second a player must be faster than to get a box of the tracker. Saved. Setup, Interface, "Motion Tracker Speed" (10 to 200) |
| `v_cgameScope` | 1 | set by the engine: it has the scope call (`re->ext.SetScopeEffect`). Read only |
| `v_scopeWheel` | 0 | set by the cgame while the wheel is taken for the zoom. The engine resets it when the cgame ends. Read only |

| Command | What it does |
|---------|--------------|
| `v_scopezoom in`, `v_scopezoom out` | one step of the zoom while it is locked, as a notch of the wheel does |

- The wheel: while the zoom is locked (the alt fire was let go) the cgame sets `v_scopeWheel` to 1, and
  `CL_ParseBinding()` in `codemp/client/cl_keys.cpp` sends the wheel as `v_scopezoom in` and `v_scopezoom out`
  instead of its binding, so a `weapnext` on the wheel works as before outside of the scope. A step is 1.25 times
  the field of view, kept between 3 and 50, eased over 80 ms, with a tick sound. Only the view changes. The server
  takes nothing of the zoom field of view (`WP_DisruptorAltFire`), so it works on every server.
  `v_scopeWheelZoom 0` turns it off.
- The tracker: the lens is dim and red with the edges of the level drawn, scan lines and grain, but inside the boxes
  the level keeps its normal colours, a little brighter, like a stencil of the box. Every visible mover in the lens
  (faster than `v_motionThreshold`, from `currentState.pos.trDelta`; line of sight; not cloaked, not dead, not the
  own player; he counts for a second after he stopped) gets a box of four corner brackets. It locks on: it starts
  larger and blinking and closes in over 220 ms (`TRACK_LOCK_MSEC`), then follows the mover softly. Beside it
  stands his distance in metres (36 units a metre), and the box stays 450 ms where he was last seen
  (`TRACK_LOST_MSEC`). The size of the box comes from the bounding box the server gave the player (packed in
  `entityState_t` `solid`), so a crouched player has a lower box.
- The boxes for the renderer: each frame the cgame passes up to 8 boxes in `scopeEffect_t` (`numBoxes`, `boxes`, the
  flag `SCOPEFX_TRACKER`, `rd-common/tr_types.h`). The renderer makes the stencil from them (see "Disruptor scope"
  in [renderer.md](renderer.md)). The brackets in 2D are bent as the lens bends the picture (`CG_ScopeBend()`). The
  shader reads the mask where the picture comes from, so the boxes for the mask use the place of the player before
  the bend.
- The echoes: every ping leaves the boxes of that moment behind (`CG_ScopeLeaveEchoes()`): brackets at the spot in
  the level where the mover, or a false box, stood, without a distance. They fade over 1.5 s
  (`TRACK_ECHO_MSEC`), shrink a little and are up to 32 at once (`TRACK_ECHOES`). They are 2D only and not part of
  the mask of the colour cut-out.
- The ping: while something is tracked a ping repeats, faster the nearer the nearest box is (every 180 to
  950 ms). It is `sound/vision/tracker_ping.wav`, a synthesized sound shipped in the client pk3 like the sounds of
  `sound/tay`, so a pure server without the pk3 has no ping.
- False boxes: every 2.5 to 8 seconds the tracker takes an edge or a corner of the level in the lens for a mover.
  A spot is traced and two beside it: a jump in distance is an edge, walls facing other ways are a corner. The
  false box lives 250 to 900 ms and flickers. Up to three at once (`TRACK_PHANTOMS`).
- rd-vanilla draws the brackets over the old mask too, but without the tint and the stencil.
- The light under the lens shows that the tracker is on, with a pip for each contact.
- The tracker key: the tracker has no command and no menu row. While the disruptor is zoomed, the key bound to
  `saberAttackCycle` (the middle mouse button in the defaults of the game, `mpdefault.cfg`) turns it on and off. The
  cgame catches the bound command before it runs (`CG_ScopeKeyCommand()` in `codemp/cgame/scope/cg_scope.c`, called
  from `CG_IncomingConsoleCommand()` in `codemp/cgame/cg_main.c`). The saber styles do nothing while zoomed anyway.
  Outside of the scope the key cycles the styles as always. Typed in the console, `saberAttackCycle` is not caught.
  The choice is kept in the archived cvar `v_scopeMode`: it is used again at the next zoom and at the next start of
  the game.
- rd-vanilla and a cgame without the call: `v_scope` does nothing and the old mask stays. The tracker has the boxes
  and the echoes without the tint.

Built for Linux, Windows and the OpenGL ES device. Not run in the game in this form: neither the lens, the wheel,
the tracker, its echoes, its ping nor the key was seen working.

### Binoculars and rocket lock-on

`cgame/scope/cg_scope.c`. Both use the scope pass or the HUD of the cgame, and both can be switched off.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_binoculars` | 1 | 1 the binoculars (zoom mode 2) are drawn as two lenses through the scope pass, 0 the old binocular art. Saved |
| `v_lockOn` | 1 | 1 the new lock of the rocket launcher, 0 the old circle. Saved |

- Binoculars: two lenses are drawn through the scope pass (`SCOPEFX_BINOCULAR`). Once they have zoomed in by
  themselves, the mouse wheel zooms from a field of view of 40 down to 5 degrees. The HUD shows the distance to
  what the crosshair is on in metres, a compass strip and the zoom factor. With rd-vanilla or `v_binoculars 0` the
  old binocular art stays.
- Lock-on: brackets stand around the box of the target and close in step by step (8 steps). While it locks they are
  amber, once it is locked they are red and pulse. Eight pips show the steps. Each step has a beep
  (`sound/vision/lock_1.wav` to `lock_8.wav`) and the lock has `sound/vision/lock_on.wav`. At a vehicle and with
  `v_lockOn 0` the old circle is drawn. It works with rd-vanilla, since the cgame draws all of it.

Not run in the game in this form.

### Screen effects

`cgame/screenfx/cg_screenfx.c`, with the screen effect pass of rd-vulkan-vision (see "Screen effects" in
[renderer.md](renderer.md)). Each frame the cgame collects the sources of the effects (a ring, a heat line, a puff of
smoke, a ripple, a flash, a ghost) and the looks of the player (health, water, Force powers), and passes them to the
renderer with `re->ext.SetScreenEffect` before `R_RenderScene`. The cgame gets it as `trap->ext.R_SetScreenEffect`,
gated by the ROM cvar `v_cgameScreenFx`. rd-vanilla returns qfalse and the cgame keeps the old looks.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_screenFx` | 1 | all screen effects. Saved |
| `v_fxBlast` | 1 | the wave and the colour split of an explosion close by. Saved |
| `v_fxHeat` | 1 | heat haze. Saved |
| `v_fxHit` | 1 | the look of a hit, of low health and the heartbeat. Saved |
| `v_fxWater` | 1 | the look under water and the drops after surfacing. Saved |
| `v_fxForce` | 1 | the looks of the Force powers. Saved |
| `v_fxSaber` | 1 | the flash where blades clash. Saved |
| `v_cgameScreenFx` | 1 | set by the engine: it has the screen effect call (`re->ext.SetScreenEffect`). Read only |
| `v_tinnitus` | 0 | engine, `CVAR_TEMP`, set by the cgame: 0 to 1, how strong the ringing is |
| `v_dreamReverb` | 0 | engine, set by the cgame from Force Sight: 0 to 1, how strong the long reverb is |

- Explosions: a wave runs out from a blast close by, the picture splits into colours at its edge, the sound is
  muffled and the ears ring for a few seconds.
- Heat: the air shimmers over the jetpack and the flamethrower. The barrel of the own repeater or blaster shimmers
  and smokes after long fire. A lit blade has heat around it.
- Hits: the edge of the picture shows the side the hit came from. At low health the picture drains of colour and a
  heartbeat is heard (`sound/vision/heartbeat.wav`).
- Water: under water the picture sways, has fog and caustics. After surfacing, drops run on the screen.
- Force rage: a tunnel with veins at the edge and a hum (`sound/vision/rage_hum.wav`).
- Force protect: an energy dome with refraction, and a ripple where it is hit.
- Force absorb: streams run in toward the player.
- Force speed: a radial blur.
- Force push and pull: a wave along the direction of the push.
- Force Sight: the spirit world. The picture is graded, a mist stands around the eye, and the players are ghosts
  that smoke (shader `gfx/vision/ghost`). The smoke smears when the camera turns. The ghosts show through walls only
  as far as the game already shows players through walls, from Force Sight level 2.
- Saber clash: a bright flash where the blades meet.

- Sound: `v_muffle` has one owner in the cgame now: the menu in the level, under water, a blast close by and Force
  Sight set it. `v_tinnitus` makes `S_TinnitusPaint()` in the mixer (`client/snd_mix.cpp`) add a ringing of two
  tones, 3700 and 3790 Hz, and lower the rest. `v_dreamReverb` adds a long parametric Steam Audio reverb, one per
  ear, in `S_SpatialRenderDream()` (`client/spatial/snd_spatial_render.cpp`). The reverb works only with Steam
  Audio (`v_spatial 1`).
- Dynamic lights: the light of a dynamic light now uses the vertex normals and also lights a floor it is just under
  (up to 12 units). That ended stripes on mp/ffa4 (`DynamicLight` in `shaders/generic.frag`, rd-vulkan-vision).
- New files: `sound/vision/heartbeat.wav`, `rage_hum.wav`, `lock_1.wav` to `lock_8.wav` and `lock_on.wav`;
  `gfx/vision/ghost_rim.tga`, `ghost_wisp.tga` and `ghost_smoke.tga`; the shader script
  `shaders/vision_fx.shader`. The sounds and textures are made by scripts (generated audio and textures).

Not run in the game in this form.

### Photo mode

`cgame/photo/cg_photo.c`. The console command `v_photo` turns it on and off. The same command is the button PHOTO in the bar of the menu in the level (the menu closes first) and the key row "Photo Mode" under Controls, Extras. It is client only and works on any server.

The HUD is hidden and the camera circles the own player, held off the walls like the camera of the third person view. It leaves by itself when the player dies, goes spectator or a menu opens.

| Input | What it does |
|-------|--------------|
| mouse | turns the camera around the player |
| wheel | moves the camera nearer or farther |
| left button | sets the depth of field focus on what is in the middle of the view |
| middle button | clears the focus |
| 1 to 5 | colour filter: 1 black and white, 2 warm, 3 cold, 4 old photo, 5 dream. 0 is none |
| right button or Enter | takes the picture |
| Esc | leaves |

The picture is a PNG in the screenshots folder of the game, without the hint line at the bottom of the screen. A line says so for two seconds. The depth of field needs rd-vulkan-vision, with rd-vanilla the left button does nothing. The texts are in the CGAME table of `tools/menus/texts.py`.

Not run in the game in this form.

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

### From the VisionJK of 2019

`codemp/ui/ui_saberfx.c` and `.h`, `codemp/cgame/hud/cg_notify.c` (2026-10-22). Three things of the VisionJK client
of 2019 are back: a blade style, a look for duels and notifications. The 2019 tree had them as `h_newFXsabers`,
`h_saberTrailLength`, `h_saberWidth`, `h_saberBladeCoreMulti`, `h_saberBladeGlowMulti`, `h_duelEffect`,
`h_duelColour`, `h_duelShowSabers` and `h_notification*`. The old names are not taken over, because their values
meant other things (`h_saberWidth` was an absolute radius), so there is no entry for them in
`codemp/qcommon/cvar_renames.h`.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `v_saberStyle` | 0 | 1: the blade is a thin white core in a glow of its colour (Setup, Game, Lightsabers) |
| `v_saberTrailLength` | 1 | length of the trail, both styles |
| `v_saberCoreScale`, `v_saberGlowScale` | 1 | thickness of core and glow, both styles, 0.25 to 3 |
| `v_duelEffect` | 0 | the look of players in a duel you are not part of: 0 as the game, 1 tinted, 2 to 8 tinted and drawn as a shader (shield, damaged shield, force shell, sight shell, half shield, cloaked, sight bubble) |
| `v_duelColour` | `50 50 255 100 0` | "r g b a forceAlpha", 0 to 255. forceAlpha 1 makes the alpha count |
| `v_duelShowSabers` | 1 | 0 hides the blades of those players |
| `v_notify` | 1 | 0 off, 1 on, 2 on with the sound of `tay_notify` |
| `v_notifyTime` | 6000 | how long a line stays, in ms |
| `v_notifyScale`, `v_notifyX`, `v_notifyY` | 1, 10, 140 | size and place of the lines |
| `v_notify_show <text>` | | command: shows a line, for scripts and binds |

Blade style. `v_saberStyle 1` is for the six colours of the game and the plain RGB blade. The other RGB styles and
black stay classic. The glow and the core are drawn over four surfaces: the blade, the blade a moment ago, and the
two sides between them, each a glow (`RT_SABER_GLOW`) and a core (`RT_LINE`). So a swing is one surface and not a row of
blades. `ui_saberfx.c` does that and is built into cgame and ui like `ui_skin.c`. `CG_SFXSaberTrail` and
`CG_DoSFXSaber` in `codemp/cgame/cg_players.c` keep the blade of a moment ago: it follows the blade, shorter the
longer a frame takes, and a trail older than 500 ms starts again at the blade. `UI_DoSFXSaber` in
`codemp/ui/ui_saber.c` draws a blade that stands still, for the saber preview of the menus. With `cg_saberTrail 0` the
SFX blade has no smear. With it on, `CG_SFXSaberTrailFill` also fills the area the blade swept in the colour of the
saber, with the shader `saberBlur` of the classic trail (it fades from the blade to where it was a moment ago), as the
2019 tree did. The four edges alone looked hollow and dark. The shaders are in `shaders/sfx_sabers.shader`, the images in `gfx/sfx_sabers`. The old
default of the glow (`h_saberBladeGlowMulti` 1.25) is the look at `v_saberGlowScale 1`.

Duels. `CG_DuelLook` in `cg_players.c` sets the look. The numbering of `v_duelEffect` differs from `h_duelEffect` by one,
because 0 is now the look of the game. With `v_duelEffect` above 0 the second pass of the game (the blue sight
bubble over a dueling player) is left out, as the 2019 tree did.

Notifications. Lines at the left edge that slide in (250 ms), stay and slide out. There are 8 at most, a new one pushes
the oldest out. They come from these places:

- A tell: `CG_ChatAdd` in `chat/cg_chat.c` is fed with the chat kind `CHAT_TELL` from somebody else.
- A mention: the line has your name, the same match as `tay_notify`.
- A line of a boombox for you: `Box_Note` in `boombox/cg_boombox.c`.
- A newer version: `v_updateState` and `v_updateText` of the engine. The cgame looks at them once a second and only
  a change to 1 while the level runs counts. The main menu has its own line for it.

A colour bar at the left shows the kind: purple for a tell, gold for a mention, light blue for the rest. With the
new HUD the box is glass, with the classic one it is a dark box. The chat box and the console keep their lines.
Downloads at the connect are not shown, the cgame does not run yet then.

The voice set of chiss (`sound/chars/chiss`, `sound/chr_d/chiss` and `models/players/chiss/sounds.cfg`) comes from
the same tree. The model of the game had no voice of its own.

Built for Linux on 2026-10-22, not yet seen in the game.

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

## The clock behind the snapshots

Not JA+, in the engine (`client/cl_cgame.cpp`, 2026-10-19), for every server. The game is drawn a little behind the
newest snapshot, so that the cgame can move everything between two snapshots it has. How far behind is worked out
as in Quake 3: the clock goes 1 ms forward with every snapshot, and 2 ms back when a frame came closer than a margin
to the newest one. The margin was a fixed 5 ms. On a line whose snapshots come unevenly that is too close: frames
run past the newest snapshot, the cgame has to extrapolate them, and what moves stutters. Now the margin is twice
the jitter of the snapshots (how much the time between the server making one and the client taking it in changes
from one to the next, smoothed over about 16, as RFC 3550 measures it), at least 5 ms and at most `v_netSmooth`.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_netSmooth` | 40 | the largest margin, ms. 0: always 5 ms, as before |
| `v_netStats` | 0 | 1: a line every five seconds: snapshots and bytes a second, frames past the newest snapshot, jitter, margin, and how far the game is behind the newest snapshot |

Measured with `tools/nettest.sh` of the test tools (a server of this tree on this machine with 12 bots on mp/ffa3,
the client watches one through `tools/lagproxy.py` at 125 frames a second), lines of five seconds:

| line | margin | frames past the newest snapshot | behind the newest snapshot |
|------|--------|-------------------------------|----------------------------|
| even (no delay, jitter 1.8 ms) | 5 ms with either setting | 14 to 30 of 615 | 13 to 16 ms |
| 50 ms, 0 to 30 ms jitter, `v_netSmooth 0` | 5 ms | 30 to 63 of 615 | 16 to 20 ms |
| the same, `v_netSmooth 40` | 16 to 22 ms | 0 to 2 of 625 once it settled (12 and 28 in the first five seconds) | 27 to 33 ms |

So on a line that jitters, what moves is smooth again for about 13 ms more delay, and an even line keeps the delay
it had. The delay counts where the server shifts the others back for a shot (`tay_unlagged` in server.md), since
the time of a command is the time the client drew; it is not made up for the saber. Not played by a person with
it, and not run over a real line.

The same test showed that the rate of the server is not what limits a full server: with 31 bots a client got 8 to
12 KB a second, `sv_clientRate` is 50000, so it stays as it is.

## When the game crashes

A crash writes `crashlog-<date>_<time>.txt` into the home folder of the game (`~/.local/share/visionjk` on
Linux, or the `fs_homepath` a start gives it): the version, the signal, the address, the thread that crashed,
its stack, and the last of the console. Then the crash goes on as before, so the system keeps its core
(`coredumpctl list visionjk.x86_64`, `coredumpctl info <pid>` shows the stacks of all threads). On Linux since
2026-10-19 for a bad address, a bad instruction, a division by zero and `abort()`; before, only a fatal error
of the engine itself (the error window) wrote one, and a segfault left nothing. Windows: only the fatal error.

The stack names only what a file exports. The offset after a file is a function of that build:

```
addr2line -f -C -e visionjk.x86_64 0x151eb
```

For the whole console up to the crash, also when it is longer than what the crash log keeps, start with
`+set logfile 2` or put `set logfile 2` into `japlus/autoexec.cfg`: `qconsole.log` is written line by line.
`logfile` is not kept in `visionjk.cfg`.

## Not done

- The download screen of the plugin for files from an URL, its cache file (`japlus/urlRedirect_cache.txt`, the
  paks that were not at the address, for six days) and its three menu scripts. The download itself is in the
  engine since 2026-10-19: a server's `jp_DlBaseURL` is used with the plugin's address and through the windows of
  [downloads.md](downloads.md). `cp_pluginDisable` bit 14 belongs to the plugin's screen and does nothing here.

Where the port knowingly differs from the plugin is in [differences.md](differences.md).
