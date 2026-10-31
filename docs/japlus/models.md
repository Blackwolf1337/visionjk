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

The model store, the updater and the paks a server names by a web address share one web request at a time in the
client. They wait for each other, so a model can come a moment later while another download runs.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `v_modelStore` | 1 | fetch the models players use that the game does not have. 0: never. Saved in the config |
| `v_modelStoreURL` | `https://jka.vision/models/` | the address of the store. https only. It can only be set on the command line |
| `v_models` | | prints the address, whether the store is on, and what came |
| `v_models get <name>` | | fetches a model whether a player uses it or not |

The browser TV page (watch) does not fetch models yet. A model browser menu in the game comes later.

## How it works

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

## Tested

A client on a local server set its model to a test model that only the store had. The model was fetched and drawn
within about a second, on the Vulkan renderer. The unit tests are in `tests/vjk/models.cpp`.

Not run: a pure server, a shader file that comes with a model after the restart of the renderer, Windows, the
browser build.
