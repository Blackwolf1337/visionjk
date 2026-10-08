# The page of the web admin panel

The page is one HTML file for the browser and many files here. `index.html` is the template: the markup, and a
line `<!--#include name -->` for every other file, in the order they go into the page. The build puts them
together (`cmake/Modules/BundlePage.cmake`) and compiles the result into the game module. With `g_webRoot` set to
this folder the server does the same from these files at every load of the page, so a change shows with a reload.

All the script files are one script: they share their names, and a function of a later file can be called by an
earlier one once the page has loaded. What runs while the page loads (the lines outside of functions) runs in the
order of the template, so a file may only use there what a file before it has made. No modules, no build tools.

| File | What is in it |
|---|---|
| `style.css` | all the styles |
| `js/util.js` | small helpers, the tables of names, the matrices |
| `js/bsp.js` | reading a `.bsp`: surfaces, patches, lightmaps, brushes, the light grid |
| `js/gl.js` | the WebGL context, compiling programs, textures, the simple meshes |
| `js/map.js` | loading the map that runs into what the view draws |
| `js/scripts-programs.js` | the GLSL programs of the script renderer |
| `js/scripts-parse.js` | reading the shader scripts of the game, materials, images |
| `js/scripts-world.js` | the sky, the quads that turn to the viewer, the map drawn with its scripts |
| `js/scripts-models.js` | the light grid and models drawn with their scripts |
| `js/pick.js` | the wireframe, and the ray that says what a click hits |
| `js/camera.js` | the camera around a point and the fly camera |
| `js/state.js` | the state feed: players, items, movers, shots |
| `js/players.js` | the panel of a player with its actions |
| `js/chat.js` | the chat |
| `js/input.js` | mouse and keyboard on the map |
| `js/frame.js` | what a frame draws, the two modes and the tabs |
| `js/editor-world.js` | the entity list of the World tab |
| `js/editor-props.js` | the Models tab |
| `js/config.js` | the server config |
| `js/files.js` | the file manager |
| `js/editor-classes.js` | the entity definitions of the game code |
| `js/editor-lines.js` | boxes, links, arrows, what fired |
| `js/editor-move.js` | the grid, moving, copy and paste |
| `js/editor-panel.js` | the panel of an entity and the values to pick from |
| `js/routes.js` | the route of the bots |
| `js/mapreport.js` | the report of the map, what a surface is made of, entities to look at |
| `js/log.js` | the log of the game |
| `js/load.js` | the tab Load |
| `js/editor-templates.js` | templates, import of `.ent` files, where the mouse is |
| `js/bans.js` | the bans |
| `js/editor-select.js` | several entities at once |
| `js/editor-history.js` | undo and redo |
| `js/g2.js` | Ghoul2 models with their animations |
| `js/picker.js` | the class picker with its preview |
| `js/weapons.js` | weapons, sabers, shots, the view through the eyes of a player |
| `js/hud.js` | the HUD of that view |
| `js/boot.js` | what starts the page |
