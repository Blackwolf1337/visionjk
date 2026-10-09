# The client in a browser

The multiplayer client built as WebAssembly: the engine, cgame, ui and the renderer rd-vulkan-vision run in a
web page. The dedicated server hands out that page itself and takes the page's game packets, so a player needs
nothing but a browser and the address of the server. It is not a JA+ feature.

Nothing changes for the clients of the desktop and for a server that does not switch it on.

## State

| Part | State |
|------|-------|
| Build with Emscripten (engine, cgame, ui) | builds, 6.0.12 |
| Engine in the page | starts in Chromium 153: file system, configs, up to the renderer |
| Page: fetch the pk3 files, keep them in the browser, start | run in headless Chromium, twice in a row (the second visit fetches no pk3) |
| Web host of the server: page, pk3 files, ranges | run on a live server, a whole 653 MB pack compared by hash |
| Web host: WebSocket peers as clients | one `getinfo` answered through it. No client has joined through it yet |
| Renderer: OpenGL ES 3 device of rd-vulkan-vision | in work, see [renderer.md](renderer.md) |
| cgame, ui, sound, mouse and keys in the page | never run |
| Firefox, Safari, Windows, a phone | never run |

Left out on purpose in the first version: the voice chat (`BuildJPVoice`), Steam Audio (`BuildJPSpatialAudio`),
compressed textures, threads. See "What is different in the page".

## For the owner of a server

```
+set sv_webPort 8081 +set sv_webRoot <folder with the build>
```

| Cvar | Default | Meaning |
|------|---------|---------|
| `sv_webPort` | 0 | TCP port of the web host, 0: off. Takes effect at once, also from the console |
| `sv_webBind` | 0.0.0.0 | the address it listens on, as `g_webBind` of the web panel: empty is every address, 127.0.0.1 keeps it on the machine |
| `sv_webRoot` | web | the folder with the page and the WebAssembly files: a full path, or under `fs_basepath` |

Only the dedicated server has it (`visionjkded`), a server started from the client does not.

The web host has no password. Whoever reaches the port gets:

- the files of `sv_webRoot` by their exact name (letters, digits, `.`, `_`, `-`), nothing below or above it,
- `/manifest.json`, the list of the pk3 files in the search path of the server with size and checksum,
- `/pk3/<path>`, each of those files, the game's own `assets0.pk3` to `assets3.pk3` among them,
- `/net`, a WebSocket whose messages are the datagrams of a client.

So opening the port to the internet hands the game data to everyone. The port has to be opened in the firewall
by hand, the server does not do that.

A player who comes through the page is a client like any other: the same slot, the same commands, the same
bans. The server knows them by their real address (the type `NA_WEB`, printed like an address of UDP), so the
limit of connections per address and a ban by address hold for both ways in. Behind a reverse proxy every such
player has the address of the proxy.

Limits of the web host: 60 connections, 16 from one address, 32 files being sent at once, 256 KB of file data
each time the server wakes up for the network, a request head of 16 KB, a WebSocket message of 4096 bytes.

## For players

`http://<server>:8081/` starts the client, `http://<server>:8081/?connect` starts it and joins the server the
page came from. `?args=<engine arguments>` adds to the command line, for example `?args=%2Bset%20s_volume%200`.

The first visit fetches the pk3 files of the server (1.2 GB with the game's own files) and keeps them in the
browser (IndexedDB, in pieces of 16 MB). Later visits read them from there and only fetch what changed. The
config and what else the game writes is kept in the browser too. All of it is in memory while the game runs: the
tab needs about 1.3 GB next to what the game itself takes.

A click on the game captures the mouse, Esc releases it. A tab that is not shown does not run, and the server
drops a client that is silent for too long.

Only the server the page came from can be joined: a page can not send UDP, its packets go through the WebSocket
of that server.

## Building it

```
git clone https://github.com/emscripten-core/emsdk && emsdk/emsdk install 6.0.12 && emsdk/emsdk activate 6.0.12
source emsdk/emsdk_env.sh
cmake -S . -B buildwasm -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/wasm32-emscripten.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build buildwasm
```

`buildwasm/web` is then the folder for `sv_webRoot`: `index.html`, `boot.js`, `visionjk.wasm32.js`,
`visionjk.wasm32.wasm` and the modules `cgamewasm32.wasm`, `uiwasm32.wasm`, `rd-vulkan-vision_wasm32.wasm`.
`cmake --install buildwasm --prefix <folder>` puts it into `<folder>/JediAcademy/web`, where a server installed
into the same folder finds it without `sv_webRoot`.

The tree is built for 64 bit only. This build is the one exception: WebAssembly as Emscripten makes it has
pointers of 4 bytes (`wasm32`).

The version of Emscripten matters, see "The modules" below.

## How it is built

### The modules

The engine is a main module of Emscripten, cgame, ui and the renderer are side modules, as they are libraries of
their own on the desktop. Each of them has its own copies of the shared files (`q_shared`, `q_math`, `bg_*`), so
they can not be linked into one program.

`dlopen()` and `dlclose()` of Emscripten are not used. Its `dlclose()` never unloads, and its `dlopen()` hands
back the instance it has, with the statics as they were left. The engine counts on fresh statics with every load
(a map change, `vid_restart`). So the page compiles the three files once, and the loader in
`shared/sys/web/sys_web.js` makes a new instance for every load, with a data block of its own that is freed at
unload. It calls `loadWebAssemblyModule()` of Emscripten's `libdylink.js` for that, which is no public
interface: with another version of Emscripten read that file first. What does not come back at unload are the
entries a module took in the function table.

A side module is only linked against the engine when it is loaded, and a function nobody has would show when it
is called. The build checks it instead (`cmake/Modules/CheckWasmImports.js`): every function a module imports
has to be one the engine exports, one of Emscripten's JavaScript side, or one of WebGL.

`Com_Error()` throws through the C code of the modules and libjpeg and libpng jump: everything is compiled with
`-fwasm-exceptions`.

### Frames

A page has one thread and no loop of its own. `main()` returns after the start, and `Sys_WebFrame()`
(`shared/sys/web/sys_web.cpp`) is called when the page is about to paint. A call that comes before `com_maxfps`
allows a frame is left out, since `Com_Frame()` would wait for the rest of its time and nothing may wait there.
`Sys_Sleep()` returns at once.

While a level loads the page does not paint: the loading screen is drawn and never shown.

### Network

`codemp/qcommon/net_ws.cpp` stands in for `net_ip.cpp`. One WebSocket to `ws://<host of the page>/net` (`wss`
when the page came over https), one binary message a datagram, nothing added. What comes in between two frames
is copied into a queue and handed to the client by `NET_Sleep()`. A packet to another address than the server's
is dropped. A name can not be looked up from a page: `connect` takes numbers or the name of the host of the
page.

### Files

The page writes the pk3 files into the file system of Emscripten under `/game/<dir>/` (memory, no copy) before
`main()` runs, and mounts the browser's database at `/home` for the home path, with every write stored by
itself. The engine is started with `fs_basepath /game` and `fs_homepath /home`.

### The page

`shared/sys/web/page/index.html` and `boot.js`, plain JavaScript without a build step. For a test the page
offers `window.vjk`: `phase` (`manifest`, `packs`, `modules`, `starting`, `running`, `error`), `log` (the lines
of the engine console), `state()` (the `connstate_t` of the client, 8 is in the game), `command(text)`.

### The web host

`codemp/server/web`: `sv_web.cpp` (listener, a fixed table of connections, the sets for `select()`),
`sv_web_http.cpp` (requests, files, ranges), `sv_web_ws.cpp` (the upgrade, frames). Its sockets are in the
`select()` of `NET_Sleep()`, those with something left to send in a write set, so a packet is passed on at once
and a file is sent in the time the server would have slept. A datagram that does not fit into the buffer of its
connection is dropped, as UDP would drop it. `shared/web` has what the web panel of the game module needs as
well: the headers of a request, base64, SHA-1.

## What is different in the page

| | Desktop | Page |
|---|---------|------|
| Renderer | Vulkan device | OpenGL ES 3 device (WebGL2), the same renderer otherwise |
| Voice chat | yes | no |
| `s_spatial` (Steam Audio) | yes | no |
| Compressed textures | `r_ext_compress_textures` | no |
| Other servers, the server list | yes | only the server of the page |
| Loading screen | drawn | not shown |
| A hidden window | runs | stands still |

## Trying it without a person

`japlus_port/tools/panelshot/steps_web.js` drives the page in headless Chromium:

```
cd japlus_port/tools/panelshot
node shoot.js steps_web.js                  # http://127.0.0.1:8081/, picture shots/web_start.png
WEB_CONNECT=1 node shoot.js steps_web.js    # /?connect, waits for state 8, shots/web_connect.png
```

It prints the phase of the page, the state of the client, the end of the engine console and where each pack came
from. `WEB_URL`, `WEB_ARGS`, `SHOOT_GL=gpu` and `SHOOT_PROFILE=<folder>` (a browser profile that is kept, to
see the packs come from the browser's storage) change what it does.
