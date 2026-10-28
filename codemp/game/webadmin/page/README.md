# The page of the web admin panel

The page is one HTML file for the browser and many files here. `index.html` is the template: the markup, and a
line `<!--#include name -->` for every other file, in the order they go into the page. The build puts them
together (`cmake/Modules/BundlePage.cmake`) and compiles the result into the game module. With `vs_webRoot` set to
this folder the server does the same from these files at every load of the page, so a change shows with a reload.

All the script files are one script: they share their names, and a function of a later file can be called by an
earlier one once the page has loaded. What runs while the page loads (the lines outside of functions) runs in the
order of the template, so a file may only use there what a file before it has made. No modules, no build tools.

| File | What is in it |
|---|---|
| `style.css` | all the styles |
| `js/util.js` | small helpers, the tables of names, the matrices |
| `js/bsp.js` | reading a `.bsp`: surfaces, patches cut into grids as the game cuts them, flares, lightmaps, brushes, fogs, the entity text, the light grid |
| `js/gl.js` | the WebGL context, compiling programs, textures with the mipmaps and the filter of the game, the simple meshes |
| `js/map.js` | loading the map that runs into what the view draws, the level of detail of its patches |
| `js/scripts-programs.js` | the GLSL programs of the script renderer |
| `js/scripts-parse.js` | reading the shader scripts of the game, materials, images |
| `js/scripts-world.js` | the sky, the fogs, the quads that turn to the viewer, the map drawn with its scripts in the order of the game |
| `js/scripts-models.js` | the light grid and models drawn with their scripts |
| `js/pick.js` | the wireframe, and the ray that says what a click hits |
| `js/camera.js` | the camera around a point and the fly camera, the projection of the game (fov, near and far plane) |
| `js/state.js` | the state feed: players, items, movers, shots |
| `js/stream.js` | the WebSocket of the page: the state pushed, the chat, the entities, the clips and the log as subscriptions, HTTP when there is no socket |
| `js/players.js` | the panel of a player with its actions |
| `js/chat.js` | the chat |
| `js/input.js` | mouse and keyboard on the map |
| `js/ui-inputs.js` | the fields of the page: which keys are theirs, who keeps the focus, the wheel on a number, the width of the side panel |
| `js/frame.js` | what a frame draws, the two modes and the tabs |
| `js/items.js` | the items of a map as the client draws them |
| `js/movers.js` | the model a mover draws besides its brushes |
| `js/trace.js` | a line through the map: where it hits first |
| `js/marks.js` | marks on the map: shadows, what a shot or a blade leaves |
| `js/weather.js` | the weather of a map: snow, rain, dust |
| `js/editor-world.js` | the entity list of the World tab |
| `js/editor-props.js` | the tab Physical props |
| `js/config.js` | the text editor of the server config (the view Text of the tab Settings) |
| `js/settings.js` | the tab Settings: the form of the cvars, saving, the marks, the view Cleanup |
| `js/master.js` | the tab Masterserver, shown only when `/api/me` says `master` |
| `js/files.js` | the file manager |
| `js/editor-entities.js` | the entities of the World tab: the poll of the list, the list, the key table, every change to an entity, placing |
| `js/editor-classes.js` | the entity definitions of the game code |
| `js/editor-lines.js` | boxes, links, arrows, what fired |
| `js/editor-move.js` | the grid, moving, copy and paste |
| `js/editor-panel.js` | the panel of an entity and the values to pick from |
| `js/editor-complete.js` | the keys that are offered while one is typed |
| `js/editor-drafts.js` | the drafts: entities that are only in the page until they are placed |
| `js/editor-labels.js` | the names of the entities above their markers |
| `js/routes.js` | the route of the bots |
| `js/control.js` | a player who is controlled from the page |
| `js/mapreport.js` | the report of the map, what a surface is made of, entities to look at |
| `js/log.js` | the log of the game, the console of the server and the line that runs a command on it |
| `js/load.js` | the tab Performance |
| `js/editor-templates.js` | templates, import of `.ent` files, where the mouse is |
| `js/editor-form.js` | the form of an entity: the keys its class takes as fields of their kind |
| `js/editor-kits.js` | kits: entities that belong together, placed as drafts that are wired |
| `js/editor-clips.js` | the clip editor: the clips of the map turned off and on, and put aside for a wall that is moved |
| `js/editor-remaps.js` | the texture swap: a shader of the map drawn as another one, for every player, with the pictures of the shaders and the marks on the map |
| `js/editor-textures.js` | for the texture swap: the textures of the game to choose from, below the map view, and the test object that is drawn with one |
| `js/bans.js` | the bans |
| `js/live-head.js`, `js/live-tail.js` | the live view (`live.js`, the template of `/live/live.js`): one function around all of its files |
| `js/live-stubs.js` | the live view: where the drawing asks (`/live/api/`), and what it asks of the parts of the panel it leaves out |
| `js/live-boot.js` | the live view: the camera that flies by itself, the names only of who it sees, what the website is told |
| `js/qr.js` | a QR code for a short text, for the address an authenticator app scans |
| `js/account.js` | the tab Account: the password, the second step of the login, tokens, one-time codes for the game, the accounts; and what a role does not show |
| `login.html` | the page of the login, a file of its own that is not part of the template: a visitor who is not logged in gets it and nothing else |
| `js/editor-select.js` | several entities at once |
| `js/editor-history.js` | undo and redo |
| `js/g2.js` | Ghoul2 models with their animations |
| `js/g2-angles.js` | how a player model is turned: legs, torso, head |
| `js/player-draw.js` | the players and the NPCs drawn |
| `js/player-fx.js` | what is drawn at a player besides his model: sprites, shells, the flag, the lightning and the drain of the Force, the jetpack |
| `js/picker.js` | the class picker with its preview |
| `js/weapons.js` | weapons, sabers, shots, the grappling hook, the view through the eyes of a player |
| `js/sabers.js` | the blade of a saber as the client draws it |
| `js/dlights.js` | the dynamic lights of a frame, on the map and on models |
| `js/world-ents.js` | what the client game puts into a map by itself: the sky portal, the models of misc_model_static |
| `js/flares.js` | the flares of a map |
| `js/fx-parse.js` | the effect files of the game (.efx) read into what the page plays |
| `js/fx-run.js` | the effects that are playing: what the feed tells of them, how their parts move |
| `js/fx-draw.js` | the effects drawn |
| `js/fx-weapons.js` | which effect a weapon has for its muzzle, its shot and what it hits |
| `js/events.js` | the events of the game: what a client is told once and shows for a moment |
| `js/hud.js` | the HUD of that view |
| `js/hud-scores.js` | the scoreboard of a client while the one it follows is dead, the centre print of a kill |
| `js/hud-console.js` | the last lines of the console over the picture: what the server prints for all, the kills |
| `js/hud-font.js` | the fonts of the game: the measures of the glyphs and their image |
| `js/hud-world.js` | the crosshair and what it points at, the radar |
| `js/boot.js` | what starts the page |
