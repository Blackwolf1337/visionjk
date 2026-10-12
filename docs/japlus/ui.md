# Menus and HUD: the new look

Since 2026-10-11 the menus and the HUD of the client have a look of their own: dark panels with round corners
that let the level or a sky of stars show through, thin light blue lines, a clean font (Noto Sans), switches
and sliders in place of the words "Yes" and "No" and of pictures. It is not from JA+ and not from the game.

Nothing of it is painted. Every shape is a picture the code computes (`codemp/ui/ui_skin.c`) and hands to the
renderer, so no file of a pk3 is involved and a pure server takes nothing away.

The menus of the game and of JA+ are still there, untouched, and one cvar away. A mod that brings menus or a
HUD of its own gets them drawn as it always did, see "For mods".

## For players

| Cvar | Default | Meaning |
|------|---------|---------|
| `ui_classic` | 0 | 1: the menus as they were (the JA+ ones). Counts from the next start of the game, or after `vid_restart` |
| `cg_hud` | 1 | 0: the HUD of the game |
| `cg_hudFiles` | ui/jahud.txt | as before. 1 is still the HUD of plain numbers and wins over `cg_hud` |

Both are in Setup under Interface.

- Outside a game every page has a bar at the top with the four sections (Play, Profile, Controls, Setup), a
  heading, the explanation of what the mouse is on under it, and a bar at the bottom with its buttons: what
  goes back as words at the left, the button that goes on (Apply, Join, Next) as a filled block at the right.
- Setup and Controls are options screens, outside a game and in one: the pages of the screen as a row of
  tabs at the top, the settings of the open page as one list of rows, and beside the list the name of the
  row under the mouse with what it is for. A row has its name at the left and its widget at the right: a
  switch with both of its states (a click on a half sets it), a choice between two arrows, a slider with
  what it stands at, the keys of a command, a line to type in. The row under the mouse is filled. A list
  that is longer than its window moves with the wheel, the page keys, the bar at its right or the arrow
  keys, which walk through every row. Left and right change the row in focus from the keyboard. The bar at
  the bottom has the way back, the defaults, and the button that restarts the video once a video setting
  was changed.
- In a game the menus are panels over the level, which is out of focus behind them (`r_menuBlur`). Where the
  menu stands in the level (`cg_immersiveMenu 1`) the panel behind it is as large as what is open: the bar
  at the top alone is a bar.
- A skin without an icon has a tile with the outline of a head in the list of models, in place of the red
  question mark of JA+ (`Skin_NoPicture`).
- A blade in one of the seven fixed colours has no style: its row says "Fixed Color". The moment one of its
  three sliders is moved it gets the plain style of a mixed blade (`UI_SaberMixFrame`), so no slider of the
  page is without effect, and a click on one of the seven colours makes it a fixed one again.
- The scoreboard (`cg_hud 1`, since 2026-10-12): one panel in the middle of the screen. At its head the server,
  the kind of game and the level, at the right who leads or where one stands. Then a row for every player with
  the picture of his model (or the flag he carries, his class in a siege), his name, score (with deaths on a
  JA+ server), ping and time, by team in a team game with a line for each team and what it has, and who
  watches at the end. The own row is marked. Before the name of a player with a client of this tree stands the
  icon of the game, and what stands above his head is written after his name. Rows are as high as there is
  room, down to a limit, and everybody the server knows is listed, also who is not in the scores it sends. The
  rows can be clicked: see client.md, "Clickable players". The score of the leader at the upper right has the
  letters of the new look too.
- The chat box has the look as well: see client.md, "The chat box".
- Every line the HUD writes is in the letters of the look (since 2026-10-12, `CG_NewLookFont` in
  `cgame/cg_draw.c`): the message of the day and the other centre prints, the name under the crosshair, the
  names of weapons, force powers and items in their bars, what a spectator and somebody who follows is told,
  the warm-up, frames a second and the timer, the team overlay. A font of the game goes in, the font of the look
  comes out at a scale that takes about the same room, so what was laid out for the old letters still fits.
  The fixed letters of the game (`CG_DrawStringExt`) are replaced the same way, and whoever centred such a line
  by counting its letters asks for its width now (`CG_StringWidth`). The pictures of those parts (the icons of
  the bars, the flags, the radar, the health of a duel, everything of siege) are the pictures of the game.
- A vote is a panel at the left edge: what it is about, how it stands, how to answer, and a bar that empties
  with the time it still runs. The team vote is a line in the new letters.
- The list of who talks in the voice chat has a dark strip under each line, as the lines of the chat do.
- The line on the first page that says a new version is out is a pill of the glass under the two panels.
- Black letters (`^0`) are a dark grey in the letters of the look, wherever they are written: they stand on
  dark glass, where black can not be read (`R_FontTTF_CodeColor`).
- The Setup of the classic menus has an entry "New Menus" at the end of its list: it switches `ui_classic` off
  and `cg_hud` on and restarts the video. Without it the way back was the console.
- The HUD: health and shield at the bottom left, force at the bottom right with the ammo of the weapon or the
  stance of the saber under it, the score above. The health bar turns amber below half and red below a
  quarter.
- Setup has seven pages in place of eleven tabs: Video, Sound (with the 3D sound and the voice chat), Game
  Options (with the language and the bot mode), Interface, Chat & Social, Network and Mods. Controls has
  seven: Movement, Interaction, Weapons, Force Powers (both old tabs), Mouse/Joystick, Other and Extras
  (what JA+ and VisionJK add). The settings that only the console had are in them: see the table at the end.
- The pages of the player are one screen too: Profile, Character, Lightsaber, and in a game Force and Moves, as
  a strip at the top that leads from one to the other and marks the one that is open. What is set stands at the
  left, what it makes at the right: the figure in a pane of its own (profile, character), the powers (force),
  the saber under both columns (lightsaber). The profile has the name, the team colour and the models as larger
  pictures. The character has species, colour (the swatches and red, green and blue as sliders, both always
  there) and head, torso and legs as three tabs. The lightsaber has type and hilt at the left and for each
  blade its seven colours, the style of its core and red, green and blue at the right: the page of sliders
  that was behind a button is gone. The bar at the bottom has the way back and the button that takes over.
- What this tree added to the menus, and what its cgame writes by itself, is in the language of the game:
  English, German (`tools/menus/texts.py`), and since 2026-10-12 French and Spanish
  (`tools/menus/texts_more.py`). Nobody who speaks French or Spanish as a first language has read those two.
- The icon of the game is the box of the website (white lid, a yellow and a blue side). On the first page it
  is a body on a panel of its own, in the pose of the icon: seen over one edge from above, yellow at the
  left, blue at the right, the lid hinged on the yellow side and a little open. It sways a few degrees and
  its lid opens and closes slowly, it does not spin. It stands in the bar at the top of every page, in the
  panel of the screens while the game connects and while a level loads, and it is the icon of the window and
  of the program.
- While the game connects, the sky of the menus fills the window and a panel at the bottom says the server,
  how far the connection is and what the server answers (full, wrong password), with a piece of a bar that
  runs to and fro. While a file comes from the server the same panel has its name, how much of it is
  here, how fast it comes and how long it will take.
- While a level loads, its picture fills the window and the same panel at the same place says the name of
  the level, the kind of game, the server, what is being loaded and how far it is.
- The console is dark glass with a light blue line under it, in place of the picture of the game.

It needs the engine of this tree (`cl_uiSkin 1`). In another engine the ui module loads the menus of before by
itself, and the cgame draws the HUD of the game.

## For mods

What the ui loads:

| | New look | With `ui_classic 1`, in another engine, or with a mod's menus |
|---|----------|------|
| outside a game | `ui/visionmenus.txt`, which lists `ui/vision/*.menu` | `ui/jampmenus.txt`, which lists `ui/jamp/*.menu` |
| in a game | `ui/visioningame.txt` | `ui/jampingame.txt` |
| HUD | `ui/vision/hud.menu` in addition to the HUD files of the game | only the HUD files of the game (`cg_hudFiles`) |

The files of the old look that this tree ships carry a mark in their first line, `visionjk-classic`:
`ui/jampmenus.txt`, `ui/jamp/main.menu`, `ui/jampingame.txt`, `ui/jamp/ingame.menu` and `ui/hud.menu`. When the
file the game finds under one of those names does not have the mark, somebody else's pk3 put it there, and the
ui (or the cgame, for the HUD) takes the old path: your menus are loaded and drawn exactly as a stock client
draws them. So a mod made for the game keeps working without knowing any of this.

To change the new look, replace files under `ui/vision/`. They are plain menu files.

### New words of the menu files

All optional. A file that uses none of them is read and drawn by the code of before.

| Where | Word | |
|-------|------|---|
| menuDef | `skin 1` | its items are drawn in the new form: yes/no is a switch, a slider a thin track with its value behind it, an edit field and a bound key stand in a box, lists have a thin scroll bar and lit rows, and the item in focus has a mark behind it in place of text that pulses. `focusColor` is the colour of all that |
| menuDef | `backdrop 1` | the sky with stars behind the menu |
| menuDef | `openSlide <units>` | the menu comes up from that far below when it opens |
| menuDef, itemDef | `style WINDOW_STYLE_PANEL` | a panel with round corners: `backcolor` its fill, `bordercolor` its rim (one pixel, when its alpha is above 0) |
| menuDef, itemDef | `radius <units>`, `shadow <units>`, `sheen <alpha>`, `glowcolor <r g b a>` | of such a panel: how round, how far its shadow reaches, a light line along its top, a light around it (blended, see below) |
| itemDef | `font 5`, `font 6` | Noto Sans and DejaVu Sans Mono, every letter from the TrueType font and made for the size it has in the window. 18 units at `textscale 1` |
| itemDef | `caps 1` | its text in capital letters |
| itemDef | `type ITEM_TYPE_BAR`, `type ITEM_TYPE_VALUE`, `stat <name>` | a bar that is filled by a number of the game, or that number as text. `forecolor` on `backcolor`, or the colour of the first `addColorRange` the number is in. `align ITEM_ALIGN_RIGHT` fills a bar from the right |
| itemDef | `showStat <name>` | the item is only there while that number is above 0 |
| itemDef | `type ITEM_TYPE_CUBE`, `type ITEM_TYPE_LOGO` | the box of the icon in the rectangle of the item: as a body in the pose of the icon that sways and opens its lid, or as a flat picture (its `forecolor` gives the alpha) |
| itemDef | `wide 1` | its window reaches the sides of a screen that is wider than the 640 units of a menu, as the background of a menu does: the bar at the top of a page. With the menu on its panel in the level it ends with the panel |
| itemDef | `page <n>` | the item is only there while page n of its menu is open (1 to 15). An item without it is on every page |
| itemDef | `tab <n>` | a button that opens page n when it is clicked, before its `action` runs. The one whose page is open is bright and has a line under it. The menu opens on the lowest page a tab names. Tabs that follow each other in the file at the same height are as wide as their words and stand in a row from where the first one is |
| itemDef | `flow 1` | a button of a bar: laid out with its neighbours like tabs, with a short line between two of them. A hidden one takes no room |
| itemDef | `flow 2`, `flow 3` | an entry of a strip that leads to other menus, laid out the same way and drawn as a tab is: 2 a button, 3 the entry of the menu that is open (bright, a line under it) |
| menuDef | `scrollRect <x y w h>`, `scrollStep <units>` | the window of the list of a page and the height of one of its rows. The list is moved by whole rows |
| itemDef | `scroll 1` | the item stands in that list: it moves with it and is only there while all of it is inside the window |
| itemDef | `row 1` | a setting (yes/no, multi, slider, bind, edit field, button) drawn as a row: its `text` at the left without the colon of the old menus, its widget in the right third, filled while it has the focus. A multi with two settings whose names fit is a switch that shows both. `row 2`: an edit field that shows stars unless it is being typed in. `row 3`: the widget has more than half of the row (a name) |
| itemDef | `type ITEM_TYPE_DESC` | in its rectangle: the `text` of the item in focus in capitals and its `descText` in lines that fit, or those of the tab of the open page. A menu that has one does not draw the line of `descX` |
| itemDef | `type ITEM_TYPE_PAGESCROLL` | the bar of the list of the open page in its rectangle, to be dragged. Not there while the list fits its window |
| script | `setpage <n>` | opens that page |
| itemDef | `otherText <text>` | what a choice (`ITEM_TYPE_MULTI`) shows while its cvar has a value that is none of its settings, in place of "Custom" |

The numbers (`codemp/cgame/hud/cg_hud.c`): `health`, `armor`, `force`, `ammo` (not there with a saber),
`saberstyle` (only with a saber), `jetpack`, `cloak`, `score`, `alive`, `spectator`, `team` (1 red, 2 blue),
`duel`, `altdim`. Only the cgame has them: in a menu of the ui such an item draws nothing.

`ui/jamp/menudef.h` has the new numbers (`WINDOW_STYLE_PANEL` 6, `ITEM_TYPE_BAR` 16, `ITEM_TYPE_VALUE` 17,
`ITEM_TYPE_CUBE` 18, `ITEM_TYPE_LOGO` 19, `ITEM_TYPE_DESC` 20, `ITEM_TYPE_PAGESCROLL` 21).

A filled window that is less than a unit high is drawn as a line of one pixel at any size of the window. While
the menus stand on their panel in the level a line that thin would break into dashes, so there it is a strip
with soft edges.

## How the menus are made

`tools/menus/build.py` reads every menu of the two lists of the game (the JA+ ones of this tree, and the rest
out of `assets1.pk3` of the game), changes it and writes it to `assets/japlus_client/ui/vision/`. 54 files.
What an item does is never changed: its cvar, its scripts, its feeder and its owner draw are taken as they
are.

- `theme.py`, the rules for every menu: the pictures of the old frame go (backgrounds, side texts, the glow
  behind buttons), boxes become panels, every text gets the new font at the size and place it had, the
  colours of the old menus become the ones of the theme (also inside the scripts that mark the open tab).
- `pages.py`, the layout:
  - A page outside a game (a menu that fills the screen) has the bar at the top with the four sections (made
    for a page that had no buttons for them), its heading under it in capitals, the line about what the mouse
    is on under the heading, its content under that with its left edge under the heading, and a bar at the
    bottom with its buttons: what goes back as words at the left (`flow 1`, the first begins where the
    content does), what goes on as filled blocks at the right. A page that needs all of the height (the
    server browser, create server) has its heading in the bar after the name of the game, no button that
    leaves the game, and the line about the mouse between the buttons.
  - A column of four or more buttons (the sections of Setup, Controls and the rules, the choices of a page
    that is one list) is a list on a panel of its own, its texts at the left. The panel beside it begins and
    ends where the list does.
  - A window (a menu that does not fill the screen: every window in a game, every question on a page) has one
    frame, around what is in it, that hides what is behind it. The pictures of the old frames were larger
    than the menu or lay beside it and are gone. The buttons in its lowest row are pills, buttons one under
    the other stay a list. The line about the mouse stands under the frame. Outside a game the page behind
    a window is darkened.
  - The first page and the bar at the top in a game.
- `options.py`, the options screens (Setup and Controls, outside a game and in one): two tables say which
  pages a screen has and what is on each, as headings, rows of the old menu (by group, in an order that can
  be given) and rows the old menus did not have. A row of the old menu keeps its cvar, its list and its
  script, without the commands that showed and marked the old tabs. The layout is the same for all four: in a
  game the screen is a window under the bar of the in-game menu.
- `profile.py`, the pages of the player (profile, character, lightsaber, force powers, outside a game and in
  one). They stay menus of their own and every item keeps its name, group, cvar, feeder, owner draw and
  script, because the code of the game finds, shows and hides them by name. The script puts them into one
  frame (the strip, a column of settings, a pane, the bar at the bottom) and takes out what the old pages
  needed to get around: the pictures to click for the other pages, the page of colour sliders behind a
  button, and the commands that showed, hid and moved things for it. Head, torso and legs of the character
  are pages of its menu (`tab`, `page`).
- `texts.py`, the texts this tree adds, English with German next to it. `build.py` writes them as a string
  package of their own (`strings/<language>/vision.str`, `@VISION_<KEY>` in a menu file), and says which text
  of an old menu has no entry.
- `menufile.py` reads and writes a menu file as a tree.

The main menu has an entry of its own that is no page: Vision Temple, under Play (`main_page()` of `pages.py`,
see [client.md](client.md), "The Vision Temple").

`ui/vision/hud.menu` is written by hand. Its three words are in the language of the game since 2026-10-12: the
score with the word of the game (`@MP_INGAME_SCORE`), the saber and the alternate dimension with texts of
`texts.py` (`@VISION_SABER`, `@VISION_ALTERNATE_DIMENSION`), all three with `caps 1`.

Run it again after a change to a menu of the old look or to the rules, and commit what it writes:

```
tools/menus/build.py [<assets1.pk3 of the game>]
```

It stops with the file and the line when a script of a menu is longer than the menu code keeps
(`MAX_SCRIPT_CHARS` in `ui/ui_shared.c`, 4096 since 2026-10-12). The game kept 2048 characters, every word
counted with its quotes and a blank, and dropped what ran over without a word. The script that opens its siege
menu needs 2033 of them, and with the colours of the new look, which have more digits, 2161: its last two
commands were lost, the ones that show the team and the class of somebody who opens the menu while he plays
(found 2026-10-12 by a run of siege in both looks; `ingame_objectives` was over too). A menu file of a mod that
is longer than that now gets a warning in the console when it is read.

## In the code

| File | |
|------|---|
| `ui/ui_skin.c`, `ui_skin.h` | the pictures (a disc and its rim for every radius in pixels, a soft light, the sky), panels, the widgets, the pointer of the mouse, the icon as a picture and as a box of polygons in a scene of its own, the panel of the connect and loading screens (`Skin_LoadPanel`). Built into ui and cgame |
| `ui/ui_shared.c` | the new words, the panel style, the branches of the widgets, items bound to a number, and the options screens: pages (`Menu_SetPage`), the list of a page (`Menu_ScrollTo`, `Menu_ScrollFrame`), a setting as a row (`Item_Row_Paint`, `Item_Row_HandleKey`), the buttons of a row by the width of their words (`Menu_TabsLayout`), what the item in focus is for (`Item_Desc_Paint`) |
| `ui/ui_main.c` | which list is loaded (`UI_NewLookList`), the pointer |
| `cgame/hud/cg_hud.c` | the numbers by name, loading and painting the HUD, the screen while a level loads (`cg_showLoading 1`, a cheat, holds it over the game to look at it), whether the new look is the one drawn (`CG_HudNewLook`), texts in the language of the game (`CG_VisionText`) and cut to a width (`CG_TextCut`) |
| `cgame/hud/cg_scores.c` | the scoreboard of the new look, its rows for the pointer, keeping it open |
| `cgame/chat/cg_chat.c`, `cgame/select/cg_select.c` | the chat box and the card of a player, which draw with the same pieces |
| `client/cl_console.cpp` | the background of the console, unless `ui_classic 1` |
| `shared/sdl/sdl_icon.h`, `shared/icons/VisionJK.ico` | the icon of the window and of the Windows program, made from `img/logo-512.png` of the website |
| `rd-common/tr_font.cpp`, `tr_font_ttf.cpp` | `vision_sans` and `vision_mono` as whole fonts for `R_RegisterFont` |
| `client/cl_uiapi.cpp`, `ui/ui_public.h` | `R_RegisterShaderRGBA` in the ext block of the ui, announced by the cvar `cl_uiSkin` |

A shape with a round corner is made for the pixels its radius has in the window and is put on whole pixels,
so its edge is as sharp at any size of the window. Nothing keeps time by itself: what moves goes by the clock
of the menus.

## Settings that got an entry

| Page of Setup | Entries the old menus did not have |
|---------------|-------------------------------------|
| Video | Frame Limit (`com_maxfps`), Wide Screen Layout (`r_2dAspect`), Dynamic Glow (`r_DynamicGlow`) |
| Sound | Room Reflections (`s_spatialReflections`), Sound Around Corners (`s_spatialPaths`), Doppler Effect (`s_doppler`) |
| Interface | New HUD (`cg_hud`), Classic Menus (`ui_classic`), Menu in the Level (`cg_immersiveMenu`), Menu Blur (`r_menuBlur`), Show Frame Rate (`cg_drawFPS`), Show Timer (`cg_drawTimer`), Show Lagometer (`cg_lagometer`), Field of View (`cg_fov`), Camera Distance (`cg_thirdPersonRange`), Crosshair Size (`cg_crosshairSize`) |
| Chat & Social | Chat Bubbles (`cg_chatBubbles`), Bubble Range (`cg_bubbleRange`), Player Statuses (`cg_playerStatuses`), Show My Music (`cg_shareMusic`), Keep Scoreboard on Double Tap (`cg_scoresPick`), Chat Box Size (`cg_chatBoxScale`), Chat Box Rows (`cg_chatBoxRows`), Select Players (`cg_selectPlayers`), Selection Outline (`cg_selectOutline`), Own Chat Line (`cg_chatInput`), Chat Box Width (`cg_chatBoxWidth`), Chat Beep (`cg_chatBeep`), Sharp Console Font (`con_font`), Console Text Size (`con_scale`), Console Opacity (`con_opacity`), Clan Password (`cp_clanPwd`, shown as stars) |
| Network | Check for Updates (`cl_updateCheck`), Intrusion Log (`cl_ids`), Key Login by Agent (`cg_authAgent`), Key File (`cg_authKeyFile`), UTF-8 Text (`cl_utf8`) |
| Controls, Extras | Free the Mouse (`+cursor`), Scoreboard with Mouse (`scores_pick`), Focus (`focus`), Vision Temple (`temple`) |

Left to the console on purpose: the names of sound devices (`cg_voiceCapture`, `cg_voicePlayback`, a menu
can not list them), addresses (`cg_voiceMasterHost`, `cg_authAgentPath`, `cl_updateURL`), the finer numbers
of the spatial sound and of the renderers, and what the game had no entry for either. The classic menus do
not have the new entries.

A setting of the cgame (most of Interface and Chat & Social) does not exist before the first level of a
session was loaded unless the stored config has it. Outside a game its row then shows it as off, whatever
its default is, and a click sets it.

The meeting word of the Vision Temple (`cg_templeLink`) has a row under Setup, Chat & Social, Vision Temple
(2026-10-12).

## Checked

2026-10-11, Linux, hidden windows, both by pictures the game took of itself (`tools/uishot.sh` with the scripts
`ui_pages_a`, `ui_pages_b`, `ui_ingame`, `ui_hud` of `tools/clienttests`, on `rd-vulkan-vision` at 1280 x 720):
the first page, Play, the server browser, create server, solo game, demos, rules, the profile pages, the saber
page, Setup with its old and new tabs, Controls, quit; in a game the bar at the top, Setup, Controls, the
player page, call vote, about, the server browser; the HUD with a saber and with a gun.

2026-10-11 later, the same way: the first page with the box at three moments of its turn, the icon in the bar
at the top, the console over the menu, the loading screen held over a level (`ui_cube`, `ui_rice`,
`ui_loading`).

2026-10-11 evening, after the first look at it in the real game ("it looks weird"): the first page, the
screen while the game connects (a server that is not there keeps it up, `ui_connect`) and the loading screen,
on `rd-vulkan-vision` and `rd-vanilla` with `r_DynamicGlow 1` and 8 samples at 2560 x 1440, and on
`rd-vulkan-vision` at 1280 x 1024 and 2560 x 1080. The panel of a download with the cvars of a download set
by hand and a line in the ui, for that picture only, that took the state for "connected" (`ui_connect_dl`):
no file was downloaded. What was wrong:

- A light that was added to what is behind it (the glow of a panel, of the box, of the tip of a slider) was
  a picture marked `RGBA_SHADER_GLOW`, the only way to an added picture. What that marks also goes through
  the dynamic glow of the renderer, which smeared it into a row of blots beside the panel. Every test
  picture before had been taken with `r_DynamicGlow 0`. The soft light is blended now, everywhere, and
  `glowcolor` with it.
- The box was seen through a wide lens from close by and turned all the way round: most of the time it
  showed one side, and its lid floated flat above it. Now it is seen from far away through a narrow lens,
  so it has the outline of the icon, and it keeps that pose.
- The connect screen had the box over the picture and the text of the old game.

2026-10-11 night, after the second look at it in the real game ("Lots of menus have wrong colors or not
proper layout. Not centered etc."), with two pictures: Setup with its sections in the orange of the old game,
and the window of an error as two frames that did not fit each other, see-through, over the first page. Every
picture before had been of a page a console command had opened, never of one after a click. So:

- The ui has a command for scripts, `ui_mouse <x> <y> [click]`: the pointer goes to a place in the 640 x 480
  units of the menus and the left button is pressed there. `ui_openmenu <menu> over` opens a menu on top of
  what is open, as a button of a page does.
- `agent/japlus_port/tools/uitest/plan.py` writes a script that opens every menu of both lists, clicks every
  button of it that shows a group (the sections and tabs) and every button that opens a window, and takes a
  picture after each step: 100 pictures outside a game, 78 in one. `uitest/sheets.sh` puts them on sheets of
  nine with their names, `uitest/audit.py` lists every colour of the generated files that is not one of the
  theme, `uitest/outline.py` prints a menu as one line an item.
- All of them were looked at, on `rd-vulkan-vision` at 1280 x 720 with `r_DynamicGlow 1` and 8 samples, the
  in-game ones with `cg_immersiveMenu 0`. The two screens of the report again at 2560 x 1440, 2560 x 1080 and
  1280 x 1024 and on `rd-vanilla`, the error with the test error of the engine (`error`, `developer 1`).

What was wrong, and is what the rules above are for: colours set by click scripts were only translated when
written one way (`1 .682 0 1;` was missed), and greys, a dark orange, gold and the red and blue of the teams
not at all. Pages without buttons for the sections had no bar and began at the top edge of the screen.
Sections stood as texts set to the right with nothing around them. Windows had the frame of the menu and the
picture frame of the old game at once. Their panels let the level and the page show through. Their buttons
were bare words, and the line about the mouse lay across their content. A setting written `fullScreen
MENU_FALSE` was read as yes. Rows of settings that were white in the old menus had the colour of a chosen tab.

2026-10-11 late night, their reports from the game they play in German, with pictures: the mark under the mouse
larger than the row it marks and "Charakter-Modell" above its bar (profile), sliders that showed 110.31,
settings that stand twice or do nothing, a second panel behind the filters of the server browser, the bar at the
top reaching out of the panel in the level, two tabs of Setup at once after coming back to it. All passes since
then run with `+set se_language german` as well.

- A slider of a menu with `skin 1` sets and shows whole numbers: whole units where its scale is twelve units or
  longer (field of view, the colours), whole hundredths on a shorter one, shown as percent with 1 as 100 %.
- A text that is too long for its place is set smaller, down to six tenths: the place is the item for a text in
  its middle or at its left, and what lies between the left edge and the mark for the label of a setting.
- `caps 1` knows the small letters with marks (it wrote ZURüCK).
- `tidy()` in `pages.py`, for every menu: a caption that stands on a bar is put on the middle of the bar, and a
  setting whose rectangle is much higher than its line is cut to the line, so the mark under the mouse is as
  large as the row.
- A list of sections is made of buttons only: the four switches of the server browser's filters had been taken
  for one. The gold of the old menus that click scripts put on edges and on the heads of columns is translated.
  A file that says `forecolor` twice (the game takes the last) keeps only that one: three rows of Controls had
  the purple of the old game from it.
- Setup hides the new tabs when it opens. It showed its first tab over the one left open.
- Left out of the new look (`LEFT_OUT`): Chat Bubbles and Show My Music under Voice Chat (they are under Chat &
  Social), Allow Downloads under Game Options (it is under Network), Voice Master under Network (it is "On
  Other Servers" under Voice Chat), View Swaying (`ui_disableWeaponSway` is read by nothing in this tree) and the
  axes of a joystick as buttons (`joy_xbutton`, `joy_ybutton`, the same). Every other row of Setup and Controls
  names a cvar that the engine, a renderer or the cgame reads. The Vulkan renderer is built from the files of
  rd-vanilla that read the video settings, so those count for both.

The options screens (2026-10-11 night, after "Maybe you could make a menu like this?" with a picture of an
options screen: tabs at the top, a list of rows with switches, sliders and arrows, what a row is for beside
it, a bar at the bottom, and "instead of lots of submenus"). Setup and Controls were laid out anew that way
(`tools/menus/options.py`), every other page outside a game got the same bar at the bottom, and the rows of
the server list a line in place of a frame each. Seen in hidden runs with rd-vulkan-vision, `r_DynamicGlow 1`,
8 samples and `se_language german` at 1280 x 720: every page of both screens with the end of its list,
outside a game, in a game as a window and in a game on the panel in the level (`clienttests/ui_options.cfg`,
`ui_options_in.cfg`, and the full pass of `uitest/plan.py`, 116 and 92 pictures). Used by script
(`ui_options_use.cfg`, the log says what the cvars stand at afterwards): a click on either half of a switch,
both arrows of a choice, the track of a slider, down, left, right and enter from the keyboard, the arrow keys
walking a list past the end of its window, the bar of a list dragged, a key waiting to be bound. Found on
the way: the rows of Controls named their click sound without quotes, which the menu reader takes apart at
the slashes (no sound, three lines in the console, in the game as shipped). They are quoted now.

The pages of the player (2026-10-11 night, after "Mach doch bitte aber das Profil deutlich besser, orientiere
dich oben. Auch die Charakterauswahl, sowie Lichtschwerter und Force."): laid out anew on the frame of the
options screens (`tools/menus/profile.py`). Seen in hidden runs with the same settings: the seven menus outside
a game and in one, as a window and on the panel in the level, the three parts of the character, one and two
sabers (`clienttests/ui_profile.cfg`, `ui_profile_in.cfg`, the full pass). Used by script
(`ui_profile_use.cfg`): from the profile over the strip to the character and to the lightsaber, two sabers
chosen, back to the profile, a model clicked (the figure changed), back to the first page.

The words of a text list (2026-10-12, a picture of the hilt list: "These are not centered"): they stood above
the middle of their rows, by another amount in every list, because each menu file sets `textaligny` for the
letters of the old fonts. In a menu of the new look the list code puts them on the middle of the row, where the
mark of the chosen row is, and does not read that number. The same code draws all 25 text lists of the 14 menus
that have one (hilts, maps, servers, demos, mods, players to vote on, sets of force powers, moves). Seen at
2560 x 1440: the hilts, the maps of a new game, the sets of force powers, the maps of a vote.

French and Spanish (2026-10-12): the options screens were looked at in both, at 1920 x 1080 (`clienttests/ui_lang.cfg`).

Not checked: the pages of siege in a game of siege (their pictures are missing outside one), most in-game
windows standing in the level (`cg_immersiveMenu 1`, the default: Setup and the vote window were looked at
that way, at 2560 x 1440), a mouse in a hand, the icon of the window
and of the program on a desktop, a real download, the connect screen when a server refuses, another language
than English and German, a mod's menus next to the new look, how it feels to use. Of the options screens:
a line typed into (the clan password, the key file), the Mods page with a mod in it, the defaults and the
video restart carried out, another size of window than 1280 x 720, Windows and the browser (built, not
run). Of the pages of the player: the buttons that take over (apply, join a team, save a set of powers)
carried out, a server without the force or with Jedi against mercenaries (the code hides and shows items
there), siege, the colours of a blade set by the sliders in a fight, the moves list reached from the strip.
