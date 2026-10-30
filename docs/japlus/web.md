# The client in a browser

The multiplayer client built as WebAssembly: the engine, cgame, ui and the renderer rd-vulkan-vision run in a
web page. The dedicated server hands out that page itself and takes the page's game packets, so a player needs
nothing but a browser and the address of the server. It is not a JA+ feature.

Nothing changes for the clients of the desktop and for a server that does not switch it on.

## State

| Part | State |
|------|-------|
| Build with Emscripten (engine, cgame, ui, renderer) | builds, 6.0.12 |
| Engine in the page | runs in Chromium 153, headless, with software GL and on the graphics card |
| Page: fetch the pk3 files, keep them in the browser, start | run twice in a row (the second visit fetches no pk3) |
| Web host of the server: page, pk3 files, ranges | run on a live server, a whole 653 MB pack compared by hash |
| Web host: WebSocket peers as clients | run on a live server: a client in a page joined, played as a spectator, was logged with its address |
| Renderer: OpenGL ES 3 device of rd-vulkan-vision | menu, level, players, sabers with glow, `vid_restart` drawn in the page. Compared with the Vulkan device on the desktop, see [renderer.md](renderer.md) |
| ui and cgame in the page | menus, the join, a game followed as a spectator. Nobody has played a round |
| Sound | starts (SDL audio of the browser). Not listened to |
| Voice chat | run on a live server, headless and muted: the page joined, got the chat through `/voice`, counted 529 frames of a talking test client in 10 seconds (a voice is 50 a second) and sent a tone for 5. Whether the others heard that tone was not looked at, and nobody has listened. See [voice.md](voice.md) |
| Sound that knows the level (`v_spatial`, Steam Audio) | built as a file of its own, `phonon.wasm`, that the engine loads when the sound system starts. Run headless and muted with a demo on mp/ffa3: 1.1 ms of a frame, and what it gives the mixer within 0.1 dB of the desktop's in each ear and band for the direct sound, within 1.1 dB with the ways round corners and the reverb. Run without the file too: one line, and the sound as before. Run on a live server as a player, with eight `snd_restart` in a row, and with the voice chat: the voice of a talking test client was a sound in the level there (913 units away, behind a wall). Nobody has listened. See [audio.md](audio.md) |
| Mouse and keys | never used by a person from here: a headless browser has neither |
| Fullscreen (`r_fullscreen`, the menu, Alt+Enter) | the page goes fullscreen, not the window of SDL. Run headless with a click before it: on, off and on again, the frame follows the page each time. Before that the first try ended the page with "The provided selector is empty" |
| A resized window | the page starts the renderer again with the new size when the window has kept it for a moment. Run headless, 1280 x 633 to 1000 x 700 |
| Firefox, Safari, Windows, a phone | never run |

The voice chat is in the build unless it is switched off (`-DBuildJPVoice=OFF`). Unless `-DBuildJPSpatialAudio=OFF`
says otherwise the build also has `phonon.wasm` in its web folder, Steam Audio for `v_spatial`. The engine is not linked against it: the
page takes the file along when the server has it, and a server that is not to have `v_spatial` leaves it out. Left out
on purpose in the first version: compressed textures, threads. See "What is different in the page".

## For the owner of a server

```
+set vs_clientWebPort 8081 +set vs_clientWebRoot <folder with the build>
```

| Cvar | Default | Meaning |
|------|---------|---------|
| `vs_clientWebPort` | 0 | TCP port of the web host, 0: off. Takes effect at once, also from the console |
| `vs_clientWebBind` | 0.0.0.0 | the address it listens on, as `vs_webBind` of the web panel: empty is every address, 127.0.0.1 keeps it on the machine |
| `vs_clientWebRoot` | web | the folder with the page and the WebAssembly files: a full path, or under `fs_basepath` |
| `vs_clientWebTVOnly` | 0 | 1: the web host answers only `GET /tv`, everything else gets 404 (see below) |

Only the dedicated server has it (`visionjkded`), a server started from the client does not.

The web host has no password. Whoever reaches the port gets:

- the files of `vs_clientWebRoot` by their exact name (letters, digits, `.`, `_`, `-`), nothing below or above it,
- `/manifest.json`, the list of the pk3 files in the search path of the server with size and checksum,
- `/pk3/<path>`, each of those files, the game's own `assets0.pk3` to `assets3.pk3` among them,
- `/net`, a WebSocket whose messages are the datagrams of a client.
- `/voice`, a WebSocket for the voice chat, only for an address a client of the game plays from through this
  host. See [voice.md](voice.md), "In a browser".
- `/tv`, a WebSocket that sends the TV of the server as demo records, for the client command `tv` and the page
  opened with `?tv`. See [server.md](server.md), "The TV of the server".

With `vs_clientWebTVOnly 1` it answers `/tv` alone and sends 404 for all the rest, so a server can offer its TV
without the files or the players.

So opening the port to the internet hands the game data to everyone. The port has to be opened in the firewall
by hand, the server does not do that.

A player who comes through the page is a client like any other: the same slot, the same commands, the same
bans. The server knows them by their real address (the type `NA_WEB`, printed like an address of UDP), so the
limit of connections per address and a ban by address hold for both ways in. Behind a reverse proxy every such
player has the address of the proxy.

Limits of the web host: 60 connections, 16 from one address, 32 files being sent at once and 6 of them to one
address, 256 KB of file data each time the server wakes up for the network, a request head of 16 KB, a WebSocket
message of 4096 bytes, 32 packets at once from a WebSocket and 1000 a second (more are dropped, as a datagram
would be). A WebSocket whose peer is not a client of the game is closed after 60 seconds, the page opens the next
one when it has something to send. Beyond the limit for files the answer is 503 with the seconds to wait, and the
page asks again. A WebSocket of the voice chat takes two of the 60 connections, an address has 4 of them, its
messages are 251 bytes at most and 60 a second, and it is closed 10 seconds after the last client of its address
left the game.

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

`buildwasm/web` is then the folder for `vs_clientWebRoot`: `index.html`, `boot.js`, `visionjk.wasm32.js`,
`visionjk.wasm32.wasm` and the modules `cgamewasm32.wasm`, `uiwasm32.wasm`, `rd-vulkan-vision_wasm32.wasm`.
`cmake --install buildwasm --prefix <folder>` puts it into `<folder>/JediAcademy/web`, where a server installed
into the same folder finds it without `vs_clientWebRoot`.

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

### Fullscreen

`r_fullscreen` makes the page go fullscreen (`Sys_WebFullscreen()`), the window SDL has is never fullscreen. SDL
would ask the browser with callbacks that carry its window, and that window is made anew with every
`vid_restart`, which the page runs itself when its size changed. The frame has the size of the page before and
after, as with any other change of size. A browser only goes fullscreen while a click or a key of the player is
fresh, so with `r_fullscreen 1` from the stored config the start stays in the window and the cvar goes back to
0. A player leaves it with the key of the browser (Esc), the cvar follows.

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
`sv_web_http.cpp` (requests, files, ranges), `sv_web_ws.cpp` (the upgrade, frames), `sv_web_voice.cpp` (the
voice chat between its WebSocket and the relay of the game module). Its sockets are in the
`select()` of `NET_Sleep()`, those with something left to send in a write set, so a packet is passed on at once
and a file is sent in the time the server would have slept. A datagram that does not fit into the buffer of its
connection is dropped, as UDP would drop it. `shared/web` has what the web panel of the game module needs as
well: the headers of a request, base64, SHA-1.

## What is different in the page

| | Desktop | Page |
|---|---------|------|
| Renderer | Vulkan device | OpenGL ES 3 device (WebGL2), the same renderer otherwise |
| Voice chat | yes | yes, through `/voice` of the web host. Talking needs https or the machine of the server itself, hearing does not. See [voice.md](voice.md) |
| `v_spatial` (Steam Audio) | the library is loaded when the sound system starts, two threads work for it, off by default | the library is loaded the same way, as `phonon.wasm`, when the server has that file, and `v_spatial` is on by default. The page's one thread does the work, so the reverb follows the listener twice a second and not seven times, and a level of more than 2000 probes has no sound round corners. See [audio.md](audio.md) |
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
