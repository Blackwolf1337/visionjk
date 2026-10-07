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

Everything on the video page is applied by its "Apply" with a `vid_restart`. Taken out, because they did
nothing or did harm: Color Depth and Texture Quality (`rd-vulkan-vision` ignores `r_colorbits` and
`r_texturebits`), Sound Quality (11 or 22 kHz, the engine runs at 44), EAX, and the button for the URL redirect
cache. The menus of the base game, which a client shows while it plays as base, still have the EAX and Sound
Quality entries: the ui hides them (`Item_ApplyHacks`).

None of the pages was looked at in the game after these changes: the rows were placed by their numbers.

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
