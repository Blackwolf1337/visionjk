# Client: cgame and ui

The original JA+ client plugin is a replacement `cgamex86.dll` and `uix86.dll` plus assets. Its features are built into
the cgame and ui of this tree. The client reports itself to the server as plugin 1.4 Build 4 (userinfo `cjp_client`),
which is what switches the plugin dependent behaviour of the server on.

## Cvars

| Cvar | Meaning |
|------|---------|
| `cp_pluginDisable` | bit field, each bit turns one feature off, see below |
| `cp_sbRGB1`, `cp_sbRGB2` | RGB color of the first and second saber |
| `cp_holster` | holster model of the player |
| `cp_login`, `cp_clanPwd` | admin password and clan password sent at the join |
| `cp_hookModel` | model of the hook |
| `cp_altDimAlpha` | how visible players of the other dimension are |
| `cp_autoReplierFile` | file with the auto replier texts |

Bits of `cp_pluginDisable`:

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

## Features

- Sabers: RGB sabers (colors 6 to 10) and the black saber (11) with trails and light, `sabercolor` and `amcolor`.
- Sabers carried on the body (`cg_players.c`, `CG_JPHolster`): a saber that is not in the hand hangs on the right
  hip, the second one on the left hip. Staffs and sabers with the saber key `idleInBack` hang across the back and
  come to the hand in the middle of the draw animations. The bolts `*back`, `*hip_l` and `*hip_r` are made on the
  model of the clientinfo in `CG_RegisterClientModelname`, so every copy of the model has them at the same index.
  The holster model of the player is shown for sabers with `idleInBack 1`.
- Alternate dimension: HUD text, screen overlay, players of the other dimension are faded (`cp_altDimAlpha`).
- Private duels with `jp_DuelAlpha`: players outside the own duel are faded or hidden together with their
  sprites, shadows, push effect, jetpack flames and thrown sabers. The crosshair ignores them.
- Hook: model, shot effect, rope from the right hand.
- Effects: flamethrower jet, fire on burning players, drain hand, kothos beam, stab effect, rage and heal at the
  bolts the plugin uses, wide drain effect, force sight overlay.
- Cameras (`cg_japlus.c`): rotation around the player after a won duel, view compensation in two player
  animations, more distance in the kick combo and the back attacks.
- Auto replier (`cg_japlus.c`): answers at duel start, duel end, own join and the join of another player from the
  texts in `cp_autoReplierFile`.
- Macro scan when the server asks for it, model scale of players, score and deaths on the scoreboard of JA+
  servers from 2.1 on, highlander rules on the loading screen, yaw speed limit with dmflags 128.
- ui: the script commands `voteSleep`, `voteSilence` and `sendModSayMsg` the JA+ menus use.

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

- The download of files over an URL (the plugin ui embeds libcurl, a download menu and a cache file,
  `cp_pluginDisable` bit 14).
- The holster choice in the player menu. The "Holsters" list of the menu stays empty, `cp_holster` has to be set
  by hand.
- Plugin code that has no string, registered handle or `cp_pluginDisable` switch of its own was not found and not
  compared, see [porting-method.md](porting-method.md).
