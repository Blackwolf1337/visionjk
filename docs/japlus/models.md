# Player models from the model store

Not a JA+ feature. `codemp/client/cl_models.cpp`, `shared/web/web_models.h`, `codemp/qcommon/files.cpp` (the model
store parts), `RE_ForgetMissing()` in `codemp/rd-vanilla/tr_model.cpp`, `R_ForgetMissingShaders()` in
`codemp/rd-vanilla/tr_shader.cpp`, `CG_ModelArrived_f()` in `codemp/cgame/cg_players.c`, issue #6, 2026-10-31.

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
| `v_modelStoreURL` | `https://jka.vision/models/` | the address of the store. https only. It can only be set on the command line |
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
The client reads it (256 KB at most) and fetches it again after ten minutes when a model is wanted.

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

A web address of `v_modelStoreURL` that is not https switches the store off with a line on the console.

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

## Tested

The window was tested on a local server against the live store with two test models. It showed both with their
icons, Get fetched one and its tile changed to "In the game".

A client on a local server set its model to a test model that only the store had. The model was fetched and drawn
within about a second, on the Vulkan renderer. The unit tests are in `tests/vjk/models.cpp`.

The TV page was tested in headless Chromium through nginx with the real config, against a local server with its TV.
A client on that server played with a model that only a local copy of the store had. The page fetched the model within
a second of the start and drew the followed player with it.

Not run: a pure server, a shader file that comes with a model after the restart of the renderer, Windows.
