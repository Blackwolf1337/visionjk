# Player models from the model store

Not a JA+ feature. `codemp/client/cl_models.cpp`, `shared/web/web_models.h`, `codemp/qcommon/files.cpp` (the model
store parts), `RE_ForgetMissing()` in `codemp/rd-vulkan-vision/common/tr_model.cpp`, `R_ForgetMissingShaders()` in
`codemp/rd-vulkan-vision/common/tr_shader.cpp`, `CG_ModelArrived_f()` in `codemp/cgame/cg_players.c`, issue #6, 2026-10-31.

## For players

The site jka.vision has a model store (jka.vision/models.html). Anyone can upload a player model pk3 there. It is
checked on the site: the pk3 may have only the files of one model (`models/players/<name>/`, `sound/chars/<name>/`,
`shaders/<name>.shader` and a readme `.txt`), and the name may not be the name of a model of the game. That part
lives in the repository of the website.

When a player on the server uses a model your game does not have, and the store has it, the game fetches it in the
background and shows it at once. You do not reconnect. The pak is saved as `base/modelstore/<name>.pk3` in your home
folder and stays there, so the model is found at every later start.

Nothing is fetched:

- on a pure server (`sv_pure 1`): such a server only allows its own paks;
- while a demo plays.

A shader file that comes with a model is read only when the renderer starts the next time (`vid_restart`, or the next
start of the game). A model that uses only textures looks right at once.

The in-game window shows the store. `v_modelbrowser` opens it (Controls, Extras, "Model Store"). It shows the
models of the store a page at a time (5 by 2), each with its icon and its size and a Get button. A model the game
already has says "In the game". The mouse wheel or the arrows turn the pages. Esc or a right click closes it. A model
you get is in the game at once, and the character list of the join menu is made again when a model came, so you can
pick it there.

The model store, the updater and the paks a server names by a web address share one web request at a time in the
client. They wait for each other, so a model can come a moment later while another download runs.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `v_modelStore` | 1 | fetch the models players use that the game does not have. 0: do not fetch the models of other players by yourself. What you ask for (the window, `v_models get`) is still fetched. Saved in the config |
| `v_modelStoreURL` | `https://jka.vision/models/` | the address of the store. https only. It can only be set on the command line (`+set v_modelStoreURL https://...`), not later in the game |
| `v_models` | | prints the address, whether the store is on, and what came |
| `v_models get <name>` | | fetches a model whether a player uses it or not |
| `v_models icons` | | fetches the icons of the models of the list (the window asks for this when it opens) |
| `v_modelbrowser` | | opens and closes the window of the store |
| `v_modelStoreChanged` | 0 | read only. Counts up when the list, an icon or a model came |
| `v_modelStoreFetching` | | read only. The name of the model that is fetched now |

The command is `v_modelbrowser` and not `v_modelstore` because the names of cvars and commands ignore case, and the
cvar `v_modelStore` would take that name.

## How it works

The window is `codemp/cgame/modelstore/cg_modelstore.c`. The engine keeps the list in `base/modelstore/index.txt`
and the icons in `base/modelstore/icons/`, and fetches the icons when the window asks (`v_models icons`). The window
reads those files and looks at `v_modelStoreChanged` to know when to read them again. `v_modelStoreFetching` tells it
which tile is busy.

The store has a list, `index.txt`, with a line `<name> <bytes> <sha256>` for each model (lines with `#` are none).
The client reads it (256 KB at most) and fetches it again after ten minutes when a model is wanted, or when the masterserver says it changed (see "The push of changes").

- Once a second `cl_models.cpp` looks at the players of the server. A model name that the game has no files for and
  that is in the list is fetched. The name is 2 to 32 characters of `a-z`, `0-9` and `_`, and a pak has 100 MB at most.
- The fetch runs in a thread of its own with `Sys_HttpGet()`. The game looks at it once a frame and never waits.
- A pak is taken only with the size and the SHA-256 of its line in `index.txt`. A pak with another size or checksum is
  deleted.
- `FS_AddModelPak()` puts the pak at the head of the search path while the game runs, so its files can be read at
  once. It refuses a pak that has a file that is not of its model.
- `RE_ForgetMissing()` makes the renderer look again for what was not found under `models/players/<name>/`: a model
  that was not there, a skin without surfaces, a shader made up because its image was missing.
  `R_ForgetMissingShaders()` does the same for the shaders. The places they took stay used until the next level.
- The engine runs `v_modelArrived <name>`. `CG_ModelArrived_f()` loads again every player that uses the model and
  had the default one.

The file system loads the paks of `base/modelstore` at every start, but only when every file in a pak belongs to the
model its name says (`FS_ModelPakFits()`, with `WEB_ModelEntryAllowed()`). A pak put there by hand with other files
is not loaded, and the console says so. Those paks are not tied to what a server references, as the paks of
`downloads` are.

A web address of `v_modelStoreURL` that is not https switches the store off with a line on the console. The cvar can be
set on the command line. It was registered read only before, which made the engine force the default, so the
command line never worked.

## The push of changes

The model store of the site tells the masterserver whenever the list of the models, of the chat emojis or of the
interactive emojis really changed, and the masterserver tells the games. With `v_modelStore` or `v_emojiStore` on, the
engine (`cl_master.cpp`) sends the packet `storeWatch` to the masterserver (`v_master`) every 25 seconds, in the menu
too. The masterserver does not answer it. The 25 seconds also keep the way through the router of the player open, as a
router forgets a UDP way after 30 seconds or more.

When a change comes, the masterserver sends `storeChanged <store> [<store> ...]` to every game that said
`storeWatch` in the last 90 seconds. `<store>` is `models`, `emoji` or `emotes`. The engine takes it only from the
address of the masterserver. It marks that list as old after a random 0 to 5 seconds, so the games of the players do
not all fetch together, and the list is fetched again the next time it is wanted. The emojis and the interactive
emojis are fetched at once in the background. For the models, a model that a player uses and that was not in the
store before is looked for again, and that fetches the list. In a test it took about 5 seconds from the upload on the
site to the new emoji in the game.

The fetch of a list after ten minutes stays as a fallback, and so does `v_emotes refresh`.

## In the browser

The browser client, and with it the TV page (jka.vision/watch/), fetches models too. The `__EMSCRIPTEN__` part at the
end of `cl_models.cpp` does not fetch by itself. Once a second it names the next model a player uses that the game
does not have to the page (`vjk.wantModel`, through `EM_ASM`).

The function `wantModel` in `shared/sys/web/page/boot.js` does the fetch:

- it fetches `/models/index.txt` of its own host once,
- it takes the pak `/models/files/<name>.pk3` only with the size (100 MB at most) and the SHA-256 of that list, checked
  with WebCrypto,
- it writes the pak to `/home/base/modelstore/<name>.pk3` in the file system of the engine and runs `v_modelPak <name>`.

The engine then loads the pak as the desktop client does: `FS_AddModelPak()`, the renderer forgets what was missing,
the cgame loads the players again. The TV plays as a demo, and in the page a demo does not stop the fetch.
`v_modelStore 0` turns it off there too.

The page needs nothing new in the CSP. The request goes to its own host (`connect-src 'self'`), and `EM_ASM` is
already allowed on `/watch/` (`'unsafe-eval'`).

## Chat emojis of the store

Not a JA+ feature. `codemp/client/cl_models.cpp` (the part "The chat emojis of the store"), `Chat_EmojiReady()` in
`codemp/cgame/chat/cg_chat.c`, issue #9.

### For players

On jka.vision/models.html the section "Chat emojis" takes uploads. Anyone can upload a PNG, 16 to 256 pixels each way
and 64 KB at most, with a name of 2 to 24 characters of a-z, 0-9 and `_`. The name may not be the name of an emoji of
the game or of another upload. One address can upload 10 emojis an hour and 30 a day, and the store holds 300 at
most. The upload answers a delete key. It is shown once, so they should write it down: with it they can delete the
emoji later. Anyone can report an emoji, and the reports go to the owner.

In the chat a player writes `:name:`. Other players see the picture if their VisionJK has fetched it, else they see
the text. `v_emote` also sends an emoji of the store (see the interactive emojis below). `v_sprayImage` also takes an emoji of the
store (see the sprays below).

## Sprays of the store

Not a JA+ feature. `codemp/client/cl_sprays.cpp`, `codemp/cgame/toys/cg_spray.c`, issue #9.

Pictures of their own that players spray (a png, jpg, tga or animated gif of the `sprays` folder) go to a store of
sprays on the site. The engine makes a square RGBA PNG sheet of the picture (a still one, or up to 16 frames in a grid,
with the PNG chunk `vjAn` for the frames) and uploads it with `v_sprays upload <file>`. The store gives it a number.
The same file gets the same number again. The number goes to the other players with the spray, and their engine
fetches the sheet with `v_sprays get`.

The service checks the format: a PNG that is square, 64 to 512 pixels, a power of two, 512 KB at most, with a valid
`vjAn` chunk, and its pixels must unpack to exactly the rows of the picture (so a small file can not hold a huge one).
The limits are 30 uploads an hour and 120 a day per address, and the store keeps the newest 20000. The owner takes one
down with `modelstore.py remove-spray <number>` and lists them with `modelstore.py sprays`. The code is in
`deploy/models/modelstore.py` of the website, and the files are served by number like the drawings. The unit tests
(`test_modelstore.py`) cover sizes, frames, the same file twice and a broken sheet.

### For the owner

On the host the owner lists the emojis with `modelstore.py emoji` and takes one down with
`modelstore.py remove-emoji <name>`. With `--block` the name can not come back. `modelstore.py reports` shows the
reports. The moderation is the same as for the models: anyone uploads, the service checks only type and size, and the
owner deletes afterwards. The service is the one of the model store (`deploy/visionjk-models.service`, with the new
`--base-emoji`), and nginx passes `/models/emoji/api/(upload|delete|report)` with a body limit of 64 KB. The files are
`/models/emoji/index.json`, `/models/emoji/index.txt` (like the one of the models, with the icon "png") and
`/models/emoji/files/<name>.png`. That part lives in the repository of the website.

### How it works

The engine fetches the list of the emojis, again after 10 minutes when it is old or when the masterserver says it changed, and then every emoji of it in the
background, after the models. It does this only while the player is in a game that is not a demo. On a pure server it
works too, see "The interactive emojis of the store" below. The picture is saved as `base/gfx/emoji/<name>.png` in the home folder, and only when its size and its SHA-256
match the list.

The code is a table of named stores in `cl_models.cpp` (the part "Named stores"): the chat emojis (`emoji/index.txt`
into `base/gfx/emoji`) and the own sounds of the interactive emojis (`emotes/sounds.txt` into `base/sound/emotes`, 96 KB
at most for each). `base/emojistore.txt` in the home folder records which emojis came from the store. When the next list comes, an emoji
the store no longer has (it was taken down) is deleted. `v_emojiStoreChanged` counts up when one came or went, and
the chat then lists its emojis again.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_emojiStore` | 1 | fetch the emojis of the store. 0 stops it |
| `v_emojiStoreChanged` | 0 | read only. Counts up when an emoji came or went |

The unit tests of the service are in `deploy/models/test_modelstore.py` of the website (`test_emoji`: upload, the
name rules, a name of the game, no PNG, too large, broken, too many pixels, delete with the key). The game side
builds. A test against the live store follows the deploy.

## Interactive emojis of the store

Not a JA+ feature. `codemp/client/cl_models.cpp` (the part "Named stores"), `codemp/cgame/toys/cg_emote.c`,
`codemp/game/toys/bg_toys.h`, `tools/toys/emotes.py`, `tools/toys/sounds.py`, issue #9.

### For players

jka.vision/emotes.html has a maker for interactive emojis. A player picks the emoji, the burst, two colours, the path,
the spin, the size, the pop, the sound and the voice (the choices are in [toys.md](toys.md)), sees a live preview and
uploads it. The preview is drawn with the same numbers, pieces and sounds as the game. Names are shared with the chat
emojis both ways: a name that a chat emoji has can not be used here, and the other way round. The store holds 500 at most. One address can upload 10 a hour and 30 a day. The upload answers a delete
key, shown once. Anyone can report an emoji. In the game `v_emote <name>` sends one, and every chat emoji of the store
is one of them too.

An own sound goes up as a base64 WAV. The server makes it 16 bit mono 22050 Hz, trims the silence, cuts it to 2 seconds
with a fade, sets the peak to -1 dBFS and lowers it so that the RMS is at most -14 dBFS.

### For the owner

The owner lists the emojis with `modelstore.py emotes` and takes one down with `modelstore.py remove-emote <name>
[--block]`. The fifth store of the service is `emotes/`. The files are `index.json` (for the page), `index.txt` and
`sounds.txt` (for the game), `numbers.json` (keeps the number of each emoji stable) and the files by name. The page is
`site/emotes.html`. `tools/emote-media.sh` of the website copies the pieces and sounds from the game repository. That part
lives in the repository of the website.

### How it works

The engine fetches the list `emotes/index.txt` into `base/emotes/index.txt` for the cgame. A line is `number name emoji
burst colour colour path spin size pop sound voice bytes sha256`. Then it fetches the own sounds of the list
(`sounds.txt`, into `base/sound/emotes`, 96 KB at most for each) and the chat emojis. A file the store no longer has is
deleted again. The cvar `v_emoteStoreChanged` counts up each time the list or a sound came, and the cgame reads the list
again. A number the cgame does not know makes it run `v_emotes refresh`, and the engine fetches the lists again, at most
once a minute.

The chat emojis and the sounds are fetched on pure servers now too. Before, the chat emojis were not. `FS_IsToyFile`
in `files.cpp` replaces `FS_IsToyPicture` and reads `gfx/emoji/*.png`, `sound/emotes/*.wav` and `emotes/index.txt` from
the folder on a pure server, besides the files it read before.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `v_emoteStoreChanged` | 0 | read only. Counts up when the list or a sound of the interactive emojis came or went |
| `v_emotes refresh` | | fetches the lists again, at most once a minute |

Tested: the unit tests of the store, the page in headless Chromium, and the game headless (see [toys.md](toys.md)). Not
tested: the live store.

## Tested

The window was tested on a local server against the live store with two test models. It showed both with their
icons, Get fetched one and its tile changed to "In the game".

A client on a local server set its model to a test model that only the store had. The model was fetched and drawn
within about a second, on the Vulkan renderer. The unit tests are in `tests/vjk/models.cpp`.

The TV page was tested in headless Chromium through nginx with the real config, against a local server with its TV.
A client on that server played with a model that only a local copy of the store had. The page fetched the model within
a second of the start and drew the followed player with it.

Not run: the spray store between two players, a pure server with the emoji stores, a shader file that comes with a model after the restart of the renderer, Windows.
