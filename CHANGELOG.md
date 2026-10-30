This file will generally summarise the Github repo commit history
Key: [-] removed, [+] added, [\*] modified

# VisionJK

* [+] The TV of the server (issue #7): a spectator in no slot and without a connection, which no player sees and the game module does not know. It follows a player who plays and goes on to the next one after `sv_tvSwitch` seconds (20) or when its player stops playing, and stands as a spectator of its own while nobody plays. `tvrecord <name>` writes `demos/<name>.dm_26` (plays with `demo` in any client) until `tvstop` or the end of the level. Live, the web host of the dedicated server (`vs_clientWebPort`) takes WebSocket viewers on `GET /tv` (`vs_tv`, default 1; 32 at once, 4 per address, 503 when full, 404 when off or no level runs), and the client plays it as a demo that does not end with `tv <address:port>` (plain TCP), `tv <name>` (a name with no port, over https to `wss://<name>/tv`, through a web server in front of the game server) or `tv "wss://name/path"` (also `ws://`; the address with `://` goes in quotes, the console takes `//` as a comment; through libcurl 7.86 or newer on Linux and macOS, WinHTTP on Windows 8 or newer, in a thread) Nothing is built while nobody watches or records. See `docs/japlus/server.md`, "The TV of the server". `vs_clientWebTVOnly 1` makes the web host answer `GET /tv` alone (404 for the page, `/manifest.json`, `/pk3/...`, `/net` and `/voice`), so a server can offer its TV without the game files or players. `vs_clientWebProxy` names the reverse proxies in front of the web host (IPv4 addresses): for a TV viewer that comes from one, its `X-Real-IP` header counts for the limit of 4 viewers per address.
* [\*] The client for the browser builds and starts again: the reputation and updater code of the client had no browser version of `Sys_ResolveIPv4` and `Sys_ProgramFolder`, and Emscripten 6.0.13 has no `FS.filesystems` (the page takes `IDBFS` from the module now, and works with 6.0.12 too).
* [\*] Every demo ended with an error on playback: `CL_ReadDemoMessage` checked the message number before it read the end marker. It reads the marker first now.
* [+] The masterserver has a panel again, its own this time: `--http <port>` (default 0: no panel) opens it on 127.0.0.1 only, and the service of the host uses `--http 8081`, reached with `ssh -L 8081:127.0.0.1:8081 <host>`. The page is built into the program and shows sessions, rooms, kbit/s in and out, voice and reputation packets, dropped packets, the boombox, three graphs of the last hour and the flagged game servers, each with a button "Forget". Only a Host header of 127.0.0.1 or localhost is answered (no DNS rebinding), a POST needs the header `X-Vision-Panel`, and there is no login. The master mode of the web panel of the game module is left over (#7).
* [\*] The masterserver (`visionjk-master`, built with `-DBuildMasterServer=ON`) is a small program of its own again and no longer the dedicated server of the game: no engine, no game module, no map, no client slot. It has one UDP port (29170) and answers only the voice datagrams (the relay) and the reputation packets (`idsChallenge`, `idsReport`, `idsList`); `getinfo`, `getstatus`, `getchallenge`, `connect`, `rcon`, `getservers` and anything else are dropped without an answer, and a line once a minute says how many. The cvars are options now: `--bind`, `--port`, `--kbps` (was `vs_masterKbps`), `--boxes`, `--max-seconds`, `--queue`, `--ytdlp`, `--ffmpeg`, `--ytdlp-args`, `--rep`, `--rep-threshold`, `--rep-local` and `--home` (where `reputation.txt` is kept; an old file is read on as it is), for example `visionjk-master.x86_64 --bind 0.0.0.0 --port 29170 --kbps 4000 --home /var/lib/visionjk-master`. `ms_repstatus`, `ms_repclear` and `quit` work on the console (stdin). It ends cleanly on SIGINT and SIGTERM and writes `reputation.txt` through `reputation.txt.new`. `master.cfg` and the `VISION_MASTER` blocks of the engine are gone. The web panel of the masterserver that was added just before is gone again with the game module (a new one is planned, issue #7). Clients need no change: same host, same port, same protocol. Checked on Linux with a UDP script, under valgrind and with SIGTERM, and on Windows in wine for the packets; not run on the host of VisionJK.
* [+] The tab "Config" of the web panel is "Settings" with a switch Form, Text and Cleanup. Form shows every cvar of the server as a field of its kind (text, number with limits, on or off, a list, a set of bits, a password), in groups with a search, and marks what changed, what needs the next map or a restart, and what `local.cfg` sets back at the next start. Save changes the last statement of the cvar in `server.cfg` in place (else a line in a block `// set by the web panel`), keeps `server.cfg.bak`, and sets the values on the console, so the server is not dropped on a protected cvar. Each cvar is answered live, map, restart or refused with the reason. Owner role only. Text is the old editor.
* [+] The masterserver runs the web panel (`vs_master`): the routes of a masterserver only, the tabs Settings (on `master.cfg`), Files, Log, Performance, Account and a new tab Masterserver with sessions, rooms, kbit/s and flagged servers (`ms_statSessions`, `ms_statRooms`, `ms_statKbps`, `ms_statFlagged`), and buttons for `ms_repstatus` and `ms_repclear all`. `master.cfg` sets `vs_webBind 127.0.0.1` and `vs_webPort 8081`, the first account is `vs_webPassword` in `local.cfg` of the home of the master. The masterserver also refuses players who connect to it, with the message "This is the masterserver of VisionJK, not a game server." (before, anyone who knew the address could take its one slot and stand alone on its level); `getinfo`, `getstatus` and the relay are not affected.
* [+] `cvar_cleanup`: the engine notes the day each cvar was last used (`cvarseen.txt`), and `cvar_cleanup` lists the cvars you made that nothing used for 30 days, `cvar_cleanup remove <name ...|all>` removes them and writes the config again. A name behind `vstr` in a value or a binding is never offered. The client has a window for it (Setup, Network, "Clean Up Settings") and the web panel a view Cleanup in Settings, which also lists statements of `server.cfg` that a later one overrides.
* [\*] 3D Sound (`v_spatial`) no longer leaves threads behind when the sound stops. Every start of the sound system with it (`v_spatial 1`, each `snd_restart`, each switch in the menu) made a new Embree device of Steam Audio, and each one left its pool of threads when the sound stopped (23 threads on a CPU of 24 threads, made when the first level was built in it). A player who switched 3D Sound on and off had 445 threads after 28 starts, and the game then hung while it closed the sound device (in PipeWire). This was so before the echo on the graphics card too. Now Steam Audio stays loaded from its first start to the end of the game, with its context and its Embree device: 51 threads after six restarts with a level, as after the first.
* [\*] Menus, HUD and console keep their shape on a window narrower than 4:3 too (a window the desktop tiled tall, 630 x 1090 for example), where they were stretched and the letters of the menus ran into each other. The grid keeps its width and gets taller (read-only `v_2dBiasY`, next to `v_2dBias`): the 640 x 480 of the menus and the HUD is a band across the middle of the window, menu backgrounds reach above and below it, the console of the engine comes down over the whole window, cinematics get black above and below, and the menu in the level (`v_immersiveMenu`) is no wider than the window and stands in the middle. `v_2dAspect 0` still stretches as before.
* [+] `v_spatialGpu` (default 1, Setup, Sound, row Echo Calculation): the rays of the echo of 3D Sound are traced on the graphics card with Vulkan ray queries instead of the CPU (Embree), where the card has them. It can be switched while playing and counts from the next pass. Only the real-time echo and `v_spatialSources` use it, occlusion, paths and bakes stay on the CPU, and without such a card (or a build without the Vulkan headers and `glslc`) the CPU traces the rays and the console says why. Steam Audio asks for the closest walls and for the way to each source per bounce, so both are traced in one trip to the card, one trip per bounce. On mp/ffa3 (RTX 4070 SUPER, 8192 rays, 32 bounces, 3000 frames) the CPU time of the client was 26.5 s with Embree and 14.0 s on the card (6.5 s with reflections off), a pass 94 ms of CPU and 24 ms long with Embree and 35 ms and 20 ms on the card, with the same loudness of the echo. `v_spatialInfo` names the card and the trips of the last pass.
* [+] A window "What's new" lists what changed in each version, with the versions at the left and the entry at the right (tags "Development build", "Installed" and "Not installed yet"; bold words, code and links; a link asks for a second click before it opens in the browser). It opens from a pill "What's new" on the main menu ("What's new in <version>" while an update is offered), from the row "Changelog" in Setup and Network, and once after an update. It has a button "Update Now" while a newer version is out, and works in the classic menus too. The text is `base/visionjk/changelog.md` of the zip, and `changelog-fetched.md` that the engine fetches from `<v_updateURL>/changelog.md` (`/dev/changelog.md` on the dev channel, at most 256 KB) with the file list; the newer of the two is shown. New cvars `v_version`, `v_changelog`, `v_uiURL` (read only) and `v_updated` (set by the engine after an update). New font `fonts/visionjk/NotoSans-Bold.ttf` (`vision_sans_bold`, `font 7` of a menu file). `tools/menus` has a row kind `menu` and `FRAMES`; the parser has the unit test `tests/vjk/markdown.cpp`.
* [+] The client reports to the masterserver (`v_idsShare`, `v_idsReputation`, `v_idsMaster`): a server it left for an attack, or crashed on, is told to `master.jka.vision` (the address of the server and what happened, nothing about the player; the first report prints a line). Servers that players of many networks reported are fetched with the server browser and marked with an orange `IDS` in the ping column, and the first connect to one is refused with the reason. The masterserver counts a /24 network once, keeps `reputation.txt`, and has `ms_rep`, `ms_repThreshold`, `ms_repLocal`, `ms_repstatus` and `ms_repclear`. With `v_ids 0` nothing is sent or fetched.
* [+] The memory monitor of the client (`v_memwatch`, on by default, needs `v_ids`): changed game code, code from a deleted file, a memfd or a temporary folder, and a child process the game did not start make the client leave the server (it is remembered like the other servers it left); writable and executable memory, new executable memory from no file and libraries from outside the game and the system folders are only written down. A crash on a server is noted at the next start and the server is refused once. It can not stop code that already runs in the process and knows of it. Linux and Windows, macOS only the crash part.
* [\*] Windows writes a crash log for an unhandled exception too (before, it left nothing).
* [+] The intrusion detection of the client can be seen: each event is a notice with a red bar, `v_idsstatus` lists the events of the run and the servers left, `v_idsforget` forgets a server, and the server browser has a button Intrusions with the last 64 lines of `ids.log`.
* [\*] The paths and libraries of the client: `fs_game` of a server must be one folder name (`..` in any part is refused), `cl_renderer` must be a plain name, libraries load from the folder of the program, and on Windows the current folder is out of the library search (`SetDefaultDllDirectories`). The caches of textures and sounds are read only from the profile folder, not from a pak.
* [\*] The download review blocks more: a `.blocked` list that is larger than 16 MB or can not be read blocks the whole pak (it blocked nothing before), names are classified with `/` for `\`, files of the other kind are blocked together with menus, module names and bundle folders count as programs, and on macOS a module bundle from a pak is loaded only with `com_unpackLibraries`. A pak entry larger than 512 MB or with control characters, quotes or `;` in its name is left out like a blocked one, a pak with more than about a million entries is refused, and `FS_ReadFile` refuses a file larger than 512 MB.
* [\*] A cvar a player made with `set` or `seta` can not be set by a server any more, nor can any cvar of this tree (`v_`) unless the engine made it a systeminfo one. `nextdemo` is registered so a server can not create it, and the key file path refuses a network path.
* [\*] The client was audited again (2026-10-28, see `docs/japlus/security.md`). Configstrings and server commands of the cgame are copied with a bound, the StringEd text of `print`, `cp` and `disconnect` is expanded by one bounded function (`Q_StringEdExpand`), demo playback refuses a negative message length and message numbers that go back, and a demo does not change `fs_game`. Maps (both renderers and the collision map), models (MD3, MDXM, MDXA) and pictures (PNG, TGA, at most 16384 a side) are checked against the file and each other before they are used. Siege, effect, saber, vehicle and override files are read with bounded copies. The protocol is unchanged, legacy servers and clients work as before.
* [\*] Every loose folder of the search path is listed once, and a file that is not in the listing is not tried there before the paks: failed opens of a run of `join-test.sh` 39815 before, 3107 after (those of the sounds 4732 before, 0 after). A file put into a folder by another program is seen after the next level change or restart of the file system, a .cfg is always tried. `v_fsIndex 0` turns it off.
* [+] The download of a pak from a web address (`jp_DlBaseURL`) has the rest of what the JA+ plugin does: two buttons in the overlay, Skip and Skip all (`v_downloadReview_do skip` and `skipall`, the script `abordURLRedirectCurrDownload`), a pak the web server answered 404 for is noted in `japlus/urlRedirect_cache.txt` and not asked there again for six days, bit 14 of `cp_pluginDisable` turns the web download off as `v_downloadHttp 0` does, and a `jp_DlBaseURL` that is not http or https is named in the console once.
* [\*] A level of a server no longer restarts the file system of the client, nor of the server itself, when only the checksum feed changed: the pure checksums are made anew from what each pak kept (0 ms, it was 21 to 41 ms on the client).
* [+] `tools/test/join-test.sh`: a dedicated server with bots and a headless client that joins three times and sees a level change, with the join and load reports.
* [\*] The client also answers an `echo` request from a server it asked for its info or status and still waits for, when the text to send back is `echoResponse getinfo <random>` or `echoResponse getstatus <random>` (a pending ping of the server browser or of `ping`, a pending `serverstatus`, or the bot/human check of the browser). Servers like SkyMod (SkyTemple) answer getinfo and getstatus only after that echo, and showed up in the server browser with no info. The text counts with or without quotes (the whole rest of the line). Any other echo is still refused and logged by `v_ids`.
* [\*] A build between two tags (`git describe` gives `v0.2.9-dev.4-3-gabc1234`) takes the version of the tag it was made after and is offered a strictly newer version on its channel, so a self-built client updates when a new release or development build is out. Its own tag is not offered as a repair. A build with no version the updater can read (`vUNKNOWN`, `v0.1.0-rc1`) still gets no updates and says so in the console.
* [\*] The project is named VisionJK: binaries `visionjk.<arch>` and `visionjkded.<arch>`, configs `visionjk.cfg` and `visionjk_server.cfg`, home folder `visionjk` (`VisionJK` on Windows and macOS). Everything below this section is the history of OpenJK, which VisionJK is based on.
* [-] Removed the `ja_guid` userinfo field, the key file `jakey` it was made from, and the cvars `cl_enableGuid` and `cl_guidServerUniq`. The client no longer identifies itself to servers.
* [\*] `cl_motd` is 0 by default: the client does not contact the update server on connect unless it is switched on.
* [\*] Menus, HUD and console keep their shape on a wide window instead of being stretched over it (`v_2dAspect`, on by default). HUD blocks sit at the edges of the window, menus in the middle.
* [\*] A new install starts in the size of the desktop as a window without a border (`r_mode -2`, `r_noborder 1`) and not in 800 x 600. A stored config keeps its values.
* [\*] The SDL2 that ships with the Windows build is 2.32.10 (was 2.0.12 from 2020).
* [+] Text inside the client is UTF-8 (`v_utf8`). Characters the fonts of the game do not have are drawn from Noto Sans (Latin, Greek, Cyrillic). Chat is UTF-8 between clients of this tree on a server of this tree (`vs_caps`), legacy clients get Windows-1252 as before, and so does every other server.
* [+] Text in the console can be marked with the mouse and copied (Ctrl+C or the right button), the input line with Shift and the arrow keys. The console is drawn with a TrueType font (`con_font`) and shows every character.
* [+] A new chat box in the cgame: 256 lines of history with tabs, scrolling with the wheel, text that can be marked and copied, links that open after a question, and its own line to type in (`v_chatInput`, `v_chatBoxWidth`).
* [+] Chat bubbles above the heads, in the 3D scene: what a player says, and dots while he types (`v_chatBubbles`, `v_bubbleRange`).
* [+] Statuses above the heads: away from the keyboard (found out by the server, `vs_afkTime`) and, when a player switches it on, the music he listens to (`v_shareMusic`).
* [+] A new look for menus and HUD: see-through panels with round corners, a clean font, switches and sliders, a sky of stars behind the menus outside a game. Drawn by code, no art in a pk3. The menus of before are a cvar away (`v_classic 1`, `v_hud 0`) and are what a mod's own menu files get. New words for menu files (`skin`, `WINDOW_STYLE_PANEL`, `font 5`, items bound to a number of the game), see docs/japlus/ui.md.
* [+] The icon of the game is the box of the website: it stands on the first page in the pose of the icon, swaying and opening its lid, in the bar at the top of the menus, and in the panel of the connect and loading screens, and it is the icon of the window and of the Windows program. The connect screen, the loading screen and the console have the new look too.
* [\*] The server names VisionJK and jka.vision next to JA+ in its greeting and its message of the day, and tells a VisionJK client (`v_vision` in the userinfo) that it is one, with the JA+ client plugin built in. What a client plugin of JA+ reads (`gamename`) is unchanged.
* [\*] The client reports its plugin version (`cjp_client`) as `1.4 VisionJK (jka.vision)` in the colours of VisionJK instead of `1.4B4`, so JA+ servers of others show the name in their logs and plugin lists. They still read it as plugin 1.4.
* [+] The live view: with `vs_liveView 1` the port of the web panel also answers below `/live/`, for everybody and read only: the map view of the panel without the panel, with a camera that flies by itself and the names of the players it sees. No addresses, no admin state, no sounds. jka.vision shows its own server that way behind the headline (docs/japlus/webadmin.md).
* [\*] The web admin panel and its live view have a WebSocket per page (`/api/stream`, `/live/stream`): the state is pushed twenty times a second with only what changed, and the chat, the log, the entity list, the clips and the texture swaps as subscriptions when they change. Without the socket the pages ask over HTTP as before. Behind Nginx Proxy Manager the proxy host needs "Websockets Support" on.
* [-] The JA+ macro scan of the client is off: key bindings are no longer disabled when a server asks for the scan.
* [+] Setup has entries for the settings that only the console had: three new tabs (Interface, Chat & Social, Network) and more rows under Sound.
* [\*] Setup and Controls are options screens: their pages as tabs at the top (seven each, in place of eleven and eight tabs at the side), the settings of a page as one list of rows with switches, sliders and choices between arrows, what the row under the mouse is for beside the list, and a bar at the bottom. A list that is longer than its window moves with the wheel, its bar or the keys. Every other page outside a game has the same bar at the bottom. New entries: Dynamic Glow, the clan password, the key file. What this tree added to the menus is German in a German game (`strings/*/vision.str`). New words for menu files (`page`, `tab`, `scroll`, `row`, `flow`, `scrollRect`, `ITEM_TYPE_DESC`, `ITEM_TYPE_PAGESCROLL`, the script command `setpage`), see docs/japlus/ui.md.
* [\*] Profile, character, lightsaber and force powers are one screen: a strip at the top leads from one to the other, what is set stands at the left and the figure, the saber or the powers beside it. The colour of a character and of a blade has its sliders on the page itself, head, torso and legs are tabs, and the pictures that had to be clicked to get to the other pages are gone.
* [\*] The cvars and commands of VisionJK begin with `v_` (client) or `vs_` (server): `cg_chatBubbles` is `v_chatBubbles`, `g_webPort` is `vs_webPort`, `+voice` is `+v_voice` (the whole list is `codemp/qcommon/cvar_renames.h`). Stored settings and binds with the old names are taken over. Voice chat, statuses, boombox and the key login need both sides from after this change.
* [\*] rd-vulkan-vision: models keep their detail by the pixels they cover, as at 640 x 480, and not by the share of the screen (`v_lodPixels`): on a screen of 1080 lines a player keeps his full model more than twice as far.
* [+] Paks of a server that names a web address for them (`jp_DlBaseURL`, as JA+ servers do) come from there over HTTP or HTTPS, at the address the JA+ client plugin uses, through both windows of the download (`v_downloadHttp`, Setup, Network). What does not come from there is asked from the server itself if it sends files.
* [\*] The clock of the client keeps as far behind the newest snapshot as the line needs: the margin follows the jitter of the snapshots (`v_netSmooth`, at most 40 ms, 0 for the old 5 ms), so a line that jitters no longer makes what moves stutter. `v_netStats 1` prints how the snapshots come.
* [-] Bot mode (the client playing by itself) is removed: its code, its settings in Setup and its key under Controls.
* [-] Focus and the Vision Temple are removed again (with the visit of another server, the temple of a dedicated server, `q3map_vision` and the bloom of rd-vulkan-vision). The masterserver keeps its relay and starts with `master.cfg`.
* [+] Players can be clicked: with the mouse free (`+v_cursor`: the left Alt key, unless that key has a job of your own; the alternate fire it has in the defaults stays on the second mouse button) the player under the pointer has a glowing outline, and a click opens a card with what you can do about him (his voice, his chat lines, a whisper, a vote, what an admin may).
* [+] The clan radio (`+v_voiceclan`) and the statuses above the heads also work on servers without the game module of this tree: they go through the masterserver, as the plain voice chat does there. Clients and masters from before are not sent anything new.
* [\*] A player who has the mouse free to point at somebody is no longer shown as typing (the dots over his head, which the others saw too), and the server does not hold him still. The engine leaves the talk button out of his commands while the cgame says its keys are held for pointing only (`v_catchQuiet`).
* [\*] Small things of the new look: a long name or title of music stays on the card of a player, a model without an icon has a tile with the outline of a head in place of the red question mark, the style of a blade in a fixed colour reads "Fixed Color" and not "Custom" (and a moved colour slider makes the blade a mixed one), and the panel behind a menu that stands in the level is as large as what is open on it, so the bar at the top alone is a bar. New word for menu files: `otherText`.
* [+] The scoreboard in the new look (with `v_hud 1`): one panel with the server and the game at its head, a row for every player with the picture of his model, his team in a team game, who watches at the end, and what stands above the head of a player after his name. Everybody the server knows is listed, also who is not in the scores it sends. The score of the leader at the upper right and the chat box have the look too, and the lines of the chat that stand over the game lie on dark strips.
* [+] The rows of the scoreboard can be clicked: the card of that player opens beside the board, also for somebody who is not in sight. The key of the scoreboard pressed twice quickly keeps the board open with the mouse free (`v_scoresPick`, command `v_scores_pick`). The card says more: with what a player plays, his time on the server, his model and saber.
* [+] Who plays with VisionJK: the scoreboard has the icon of the game before the name of such a player, and his card says the version. A server of this tree tells it to the clients that ask (server command `jpclient`, `vs_caps` bit 3, `v_caps` bit 1). On any other server a client of this tree is known by what comes from it through the masterserver. Command `v_peers`.
* [\*] What the cgame writes by itself (the card of a player, the scoreboard, the chat box, the lines above the heads) is in the language of the game, German so far (`CG_VisionText`, `tools/menus/texts.py`).
* [\*] A cgame that has just started asks the server once for what stands above everybody (`vs_status all`): after a restart of the video the lines above the heads of the others were gone until they changed.
* [+] The chat box has a size: `v_chatBoxScale` for the box with its letters (0.85 by default, it was 1), `v_chatBoxRows` for the rows of the open box, both in Setup next to its width. With the box open its upper right corner can be dragged with the mouse, and Ctrl with the wheel changes the size.
* [\*] With the new HUD every line the HUD writes is in the letters of the new look: centre prints, the name under the crosshair, the names in the weapon, force and item bars, what a spectator is told, the warm-up, frames a second and the timer, the team overlay. A vote is a panel with a bar for its time, the list of who talks lies on dark strips, and the line on the first page that says a new version is out is a pill of the glass.
* [\*] Black letters (`^0`) are a dark grey in the letters of the new look: on its dark glass black could not be read. The status line over the own head is not drawn any more, two status lines of players who stand close no longer lie on each other, and the card of a clicked player stands beside him and not on his arm.
* [+] The Setup of the classic menus has an entry "New Menus" that leads back to the menus and the HUD of VisionJK.
* [+] French and Spanish for everything the new menus and the cgame say (`tools/menus/texts_more.py`). They read English before.
* [\*] A demo can be started from the console while the program is a server (a level played alone). It stopped with "CM_ClipHandleToModel: bad handle": the server ended a frame after the demo had loaded its level, and took the collision map of that level with it.
* [\*] Found by test runs of every kind of game: the cgame asks the server again when its question for who plays with what got lost in the flood protection, the bubble over the own head is not as large as a wall when the camera is pushed up to the player, the rows of the team overlay do not touch in the new letters, the chat box is never wider than the screen, the scoreboard makes room for the card of a player on a 4:3 window, and the mouse goes back to the game when a level restarts.
* [\*] Two old accesses past the end of an array that a build with sanitizers found in the game: the count of picked up powerups (`g_log.c`, the Ysalamiri and the flags were counted in the row of the next player) and the name of a model surface of fewer than four letters (`tr_ghoul2.cpp`).
* [+] The server browser can be opened in a game (SERVERS in the in-game menu). Both browsers can leave out servers on which only bots play (`v_browserShowBotOnly`).
* [+] While a menu is open in a game it stands in the level next to the player, seen over his shoulder, with the scene behind it out of focus and the sound muffled (`v_immersiveMenu`, `v_menuBlur` for how far out of focus, rd-vulkan-vision and the client in a browser). The focus is at the menu: the player next to it stays sharp, the level behind goes soft with its distance.
* [\*] The updater of the client on Windows: an update that changed the asset pack was fetched and never put in place ("can not be replaced"). The game that started itself again held the pack through a handle it got from the one that ended. Versions 0.1.1 and 0.1.2 have that.
* [+] The updater also looks at the files when the game is the newest version already, and offers a repair with one click for a file of the game that is missing or from another version, and for an old game module in the folder of the player that would be loaded in place of the game's (it is renamed, not deleted). Before, a program of the new version with a module of the old one stayed that way.
* [\*] The console no longer says "Sys_LoadGameDll(...) failed" for a folder that simply has no such module. The line stays for a module that is there and does not load.
* [\*] The login with a key (`v_amkey login`) sent its commands faster than a server with `sv_floodProtect` takes them and ended with "The server did not answer the login." They keep that distance now and are sent again when one is dropped. An ssh-agent that has no Ed25519 key is told so.
* [-] The server takes no rcon any more: the password went over the wire in the clear with every command. Use the web panel or the console of the server. A `rconpassword` line in a config does nothing, the client keeps its `rcon` command for other servers.
* [\*] The three admin rank passwords and `cp_login` only count with `vs_adminPasswords 1`. This changes a running server at the update: its admins need a line in `admin_keys.txt` or a one-time code.
* [+] Admin login with an Ed25519 key instead of a password: `admin_keys.txt` on the server (lines as in authorized_keys behind a rank), `v_amkey new`, `v_amkey show` and `v_amkey login` on the client, from a key file or an ssh-agent such as 1Password (`v_authAgent`). For clients that can not sign, `amcode <rank>` on the server console makes a code that works once within a minute (`amlogin <code>`).
* [\*] The web admin panel has accounts in place of its one password: three roles (moderator, admin, owner), passwords as Argon2id hashes, sessions in a cookie, a second step by time with recovery codes, tokens for scripts, a login with a key of the browser, one-time codes for the admin login in the game. `vs_webPassword` is read once, to make the first account `owner`. `webuser` on the server console manages the accounts.
* [+] `vs_webProxy`: behind a reverse proxy with HTTPS only the proxy is let in to the web admin panel, and the visitor is who the proxy says he is.
* [\*] The Linux build of a release had the web admin panel as a page without its style sheet and scripts, and its editor without the lists of the entity classes and their keys: the build scripts that put them together did nothing with a CMake before 4.0 (the release is built with 3.25). Releases 0.1.0 to 0.1.4 for Linux have that fault, the Windows builds do not.
* [+] Intrusion detection in the engine. Server (`vs_ids`, on): packets no client of the game sends are written to `ids.log`, their address is blocked for a while and a player they come from is dropped. Client (`v_ids`, on): what a server sends that no server of the game does is written down, and a server that keeps at it is left.
* [+] `vs_antiWallhack` (off by default): a player no line of sight reaches is not sent to a client. Players behind walls are not heard then either.
* [\*] The client answers an `echo` request only from the server it is connected to.
* [\*] Voice chat: the room rings half as loud from the own voice as before (with `v_spatial`), and the setup menus have a slider for it (`v_voiceOwnReverb`, "Own Voice In The Room").
* [+] Voice chat: a clan radio. `+v_voiceclan` talks to the players with the same clan password (`cp_clanPwd`) wherever they are, without a place and with the sound of a radio (`v_voiceRadioFX`). `vs_voiceRadio 0` on the server switches it off. Not on servers without the game module.
* [+] The boombox: boxes in the level that play songs of YouTube and YouTube Music for everybody near them (`v_boombox play <link or words>`, or click a box with the pointer for its card, drag it to move it). Only VisionJK players see and hear them, legacy clients are told nothing. A VisionJK server fetches the songs itself (yt-dlp and ffmpeg) and sends them over the relay of the voice chat, on any other server the masterserver keeps and plays the boxes (`vs_masterBoxes`). With `v_spatial` a box is a sound of the level that rings in the room. `v_boomboxes`, `v_boomboxVolume`, `vs_boombox*`, see docs/japlus/boombox.md. Built for Linux and Windows, never run.
* [+] The boombox has a window, from "Open the player" on the card of a box: who put it down, the song and who asked for it, a bar to jump in the song, pause, skip, stop, mute for you, the volume for everybody, the line of songs with a cross to take one out, add a song, move it, take it away. Needs a server or masterserver of this version for the jump and the line; older ones get the window without them.
* [+] Chinese, Japanese and Korean are drawn (Noto Sans CJK in the client assets) instead of `?`. On a server that is no VisionJK server what you type still goes out as Windows-1252, so these characters arrive there as `?`.
* [+] The boombox plays playlists: a link with `list=` (a playlist, a video in a playlist or a mix, an album of YouTube Music) puts up to 100 of its songs into the line of the box, what does not fit waits behind the line and follows. Needs a server or masterserver of this version.
* [\*] The card of a box says who put it down and who asked for the song, and shows the right next song (it showed a number since 0.1.7). What a box does is no longer written into the chat box of everybody near it; a line for you alone goes into the console and onto the card.
* [\*] The card and the name of a clicked player are drawn over the scene: floors and walls beside him no longer cut them. Very near they are no larger than 1.5 times their size, and they stay whole on the screen.
* [\*] The card of a player offers his voice (mute, quieter, louder) only when he is known to play VisionJK.
* [+] Downloads from a server: a window first lists the paks it offers, to tick, keep in `<game>/downloads` or have for this session only (`downloads/session`, emptied at start and quit), then a window shows what is in them by kind before anything is used. Program files in a downloaded pak are blocked (`<pak>.blocked`, the pak keeps its checksum), with the window or without it. Downloaded paks load only while a server references them. `v_downloadReview`, `v_downloadKeep`, see docs/japlus/downloads.md.
* [\*] Security fixes of the audit of 2026-10-20 (docs/japlus/security.md, "Audit of 2026-10-20"): the duel configstrings are read with a bound (a server could write past a buffer of the cgame), a server can no longer set protected cvars of a client by their old names, downloads from `jp_DlBaseURL` never go to this machine or a private network and have 512 MB at most, the masterserver limits token answers and boombox actions, the web panel takes several proxies (`vs_webProxy`), answers the live view before the wait after wrong logins, hashes a login beside the game and sends the hash of its scripts as CSP.
* [+] `vs_voiceKbps` (6000): what the voice chat of a game server may send.
* [\*] Linux: the game no longer freezes when its stderr (the journal of a desktop start) stops taking text: a write that would block is dropped.
* [+] Sound system: a DJ table with two big speakers (`BOX_KIND_SYSTEM`) that plays one song in stereo, the left channel from the left speaker and the right channel from the right one. The speakers are placed separately, are heard twice as far as a boombox and have twice its bandwidth. L and R marks show while the pointer is on the system or carries one of its speakers. It runs through the game module of this tree and through the masterserver, as the boombox does. The client has 16 box streams.
* [+] Quick wheel `+v_radial` (unbound, row under Controls): a ring of slices to place a boombox or a sound system and to open the player. Aimed at a box it offers play, pause/resume, skip, stop, louder, quieter, move and remove. A carried piece turns with the mouse wheel.
* [+] Boombox beat shockwave: a ring of refraction goes out of the speakers on every beat (rd-vulkan-vision). The beats are found from the energy of the song, and the ring faces the way the speaker does. rd-vulkan-vision has room for 1024 surfaces drawn last.
* [\*] The volume of the boombox and the sound system follows `s_volume` (effects).
* [\*] Show My Music shares only the players ticked in Setup (`v_shareMusicPlayers`, empty by default: nothing is shared until a player is ticked). A window lists the players on the bus now.
* [\*] 3D sound (`v_spatial`): sounds are heard from the head of the player and not from the camera. 128 voices, walls fade in and out over 50 ms. Brush models (doors, lifts) are in the scene and their own sounds are heard from their side. Rays are traced by Embree where the library has it. Bakes use the numbers of Valve (levels are baked again once: files `_v3.paths`, `_v2.echo`).
* [+] `v_spatialEcho`: 0 works the echo out live, with own echoes of the loudest sounds. 1 bakes it once per level into a file and looks it up where the listener stands (Setup, Sound, row Echo).
* [+] `v_spatialDebug`: shows in the level what becomes of the sounds (labels, ways round corners, rays, numbers and the wall under the crosshair).
* [\*] 3D sound threads: a pass uses a quarter of the cores (1 to 4) and a bake half of them, and a pass waits at least twice as long as the last one took, so the threads rest (before: all cores but two, and back to back on a slow CPU). `v_spatialThreads` sets the count by hand (0 automatic, from the next `snd_restart`).
* [+] Hot patches of the game, cgame and ui modules in developer builds (`BuildDevHotpatch`, `v_devHotpatch`, tools/hotpatch.py), not in releases.
* [+] Blade style SFX from the VisionJK of 2019 (it came from JA++): `v_saberStyle 1` draws a thin white core in a glow of its colour, smeared over where the blade is and where it was a moment ago, so a swing is one surface. It is for the six colours of the game and the plain RGB blade, the other RGB styles and black stay classic. The saber preview of the menus shows it too. `v_saberTrailLength` is the length of the trail (both styles, default 1), `v_saberCoreScale` and `v_saberGlowScale` are the thickness of core and glow (both styles, 0.25 to 3). The shaders are in shaders/sfx_sabers.shader, the images in gfx/sfx_sabers. Setup, Game page, heading Lightsabers.
* [+] Duels, from the VisionJK of 2019: `v_duelEffect` sets how players look who are in a duel you are not part of. 0 is as the game (dark with a blue bubble, default), 1 is only tinted in `v_duelColour`, 2 to 8 are tinted and drawn as a shader instead of the skin (shield, damaged shield, force shell, sight shell, half shield, cloaked, sight bubble). `v_duelColour` is "r g b a forceAlpha" (0 to 255, forceAlpha 1 makes the alpha count), default "50 50 255 100 0". `v_duelShowSabers 0` hides the blades of those players. Setup, Game page, heading Duels.
* [+] Notifications, after the ones of the VisionJK of 2019: lines at the left edge that slide in, stay and slide out. They come for a tell to you, a chat line with your name (the same match as `tay_notify`), a line of a boombox for you, and a newer version of the game while you play. `v_notify` (0 off, 1 on, 2 with the sound of tay_notify), `v_notifyTime` (ms, 6000), `v_notifyScale`, `v_notifyX`, `v_notifyY`. The command `v_notify_show <text>` shows a line, for scripts and binds. It has a glass look with the new HUD and a dark box with the classic one. The chat box and the console keep their lines. Downloads at the connect are not shown: the cgame does not run yet then. Setup, Chat & Social page, heading Notifications.
* [+] The voice set of chiss (sounds of the 2019 tree): sound/chars/chiss and sound/chr_d/chiss with models/players/chiss/sounds.cfg in the client assets. The model of the game had no voice of its own.
* [\*] The blade style SFX (`v_saberStyle 1`) fills the area a blade sweeps with `cg_saberTrail` on again, in the colour of the saber, as the VisionJK of 2019 did. Only the four edges were drawn, so a swing looked hollow and dark.
* [\*] The volume of the boombox and the sound system no longer follows `s_volume`, it is `v_boomboxVolume` alone again (Setup, Sound) as before 0.2.4. With a low effects volume the songs were too quiet.
* [\*] The zone memory manager of the engine and the `BG_Alloc` pool of the game modules now hand out blocks on the alignment their data needs. Some blocks were misaligned (the bot data among them). This was undefined behaviour, harmless on most PCs but a risk on other processors and compilers.
* [\*] Fixed three integer shifts that were undefined behaviour: in `MSG_WriteBits` (signed values), in the checksum of a server's file system and in the mp3 music decoder. Fixed a wrong cast in the effects parser (`FxTemplate`).
* [\*] The updater now empties a file list it refuses completely. It never used such a list, but nothing of it is kept now.
* [\*] The classic HUD no longer asks for the font `fonts/reallybigfont`, which no game file has, so the console no longer warns about it at every start.
* [+] Every release is now tested before it goes out: the release itself on Linux and Windows (both renderers) and as a dedicated server with a bot, a build with AddressSanitizer and UndefinedBehaviorSanitizer, unit tests for what the game reads from outside, static checks, joining stock OpenJK servers and letting stock clients join, and the updater end to end on Linux and Windows. See `docs/japlus/testing.md`.
* [+] Update channels. `v_updateChannel` is `stable` (default) or `dev` (saved): `dev` offers development builds (`0.2.8-dev.2`, tag `v0.2.8-dev.2`) and the release where that is newer. Order: `0.2.8-dev.1` < `0.2.8-dev.2` < `0.2.8` < `0.2.9-dev.1`. Setup, Network has the row "Update Channel", and Development asks first ("Stay on Stable", "Switch to Development"). Changing the channel asks the website at once. A build between two tags is still never updated. Players on 0.2.7 and older get the setting with 0.2.8.
* [+] A development build on the stable channel is offered the older release ("Back to stable version Y. Click here to switch"). Files only the development build had go into the journal (`gone <path>`) and are renamed to `.vjkold` at the next start, from the list `lists/<version>/<system>.txt` of the website. If that list can not be fetched the update goes through and the console says so. Docs: docs/japlus/update.md.
* [+] `tools/test/all.sh` runs all release tests in parallel (smoke, sanitize, lint, with `--updater` the updater on Linux and Windows). `tools/test/updater/e2e.sh` has the scenarios `channel`, `back` and `devstable`, `tests/vjk/update.cpp` checks versions with a dev part.
* [\*] Renderer: up to 4095 refEntities a frame instead of 2047 (`REFENTITYNUM_BITS` 12, a 64 bit sort key for the draw surfaces). On the JoF server the ambient effects of the map filled the 2047 slots and models and effects flickered.
* [\*] The Git tag of a build (the version the game shows) is read at every build (`cmake/GitVersion.cmake`, target `vjk_version`) and not only when the build folder is configured. A build folder configured at v0.2.4 had shown v0.2.4 at v0.2.7.
* [\*] Menu texts in German, French and Spanish reworded (German "du" throughout, Spanish "tú"), and the messages of the updater in plainer English.
* [+] The window can be resized while the game runs. It is made resizable (before, SDL told Wayland it could not change, and a tiling window manager such as Hyprland showed the frame cut off, with the right side of the HUD missing). rd-vulkan-vision builds swapchain and targets again in the new size between two frames, and the cgame and ui draw their 2D in it. With rd-vanilla, the OpenGL ES device, or a cgame or ui of another tree the client does a `vid_restart` in the new size instead. The video mode list also offers common window sizes up to the size of the desktop. Run on Linux (sway, Wayland, headless, AMD Radeon 610M with RADV): with rd-vulkan-vision the frame followed a window made 800x500 and then 1200x500 without a `vid_restart`, and the HUD stayed at the edges. With rd-vanilla the client restarted the renderer in the new size; the new window of that restart is tiled again by sway, so on a tiling desktop the size of the tile is what stays, and a size dragged on a floating window does not survive that path. Hyprland and Windows were not run.
* [+] Disruptor scope lens (rd-vulkan-vision, both devices, `v_scope`, on): the zoomed view is drawn through a large, clear lens (0.45 of the view height) with little distortion and colour split, a thin rim of light and a fine reticle with a lit red dot. Around the rim on a dark ring three segmented arcs show the ammo (right, red when low), the zoom (left) and the charge of the alt fire (bottom). Outside the lens the level stays visible, soft, dimmed and a little grey. The old mask art is left out; the cgame draws only the zoom as text like "8.5x" left of the lens. rd-vanilla keeps the old mask. Built for Linux, Windows and the OpenGL ES device, not run in the game yet (the scope could not be brought up by a script).
* [+] Disruptor scope tracker (`v_motionThreshold`): while zoomed, the key bound to `saberAttackCycle` (the middle mouse button in the defaults) turns it on and off, and outside the scope it cycles the saber styles as always. No command, no key of its own. The choice is kept in the archived cvar `v_scopeMode` (0 off, 1 on) and used again at the next zoom and the next start of the game. A light under the lens shows that it is on, with a pip for each contact.
* [+] Disruptor scope tracker boxes: every visible mover in the lens who moves faster than the threshold gets a box of four corner brackets that locks on, follows him softly, shows his distance in metres and stays 450 ms where he was last seen. Only what can be seen is boxed (no wallhack); cloaked and dead players and the own player never are. The box size follows the bounding box of the player, so a crouched player has a lower box. The lens is dim and red with the edges of the level drawn, scan lines and grain, but inside the boxes the level keeps its normal colours, a little brighter, like a stencil of the box. The cgame passes up to 8 boxes a frame and the renderer clears them into a mask (both devices); the brackets are bent as the lens bends the picture. While something is tracked a ping repeats, faster the nearer the nearest box is; the sound `sound/vision/tracker_ping.wav` is in the client pk3, so a pure server without the pk3 has no ping. Every ping leaves the boxes of that moment behind as echoes: brackets at the spot in the level where the mover stood, without distance, fading over 1.5 seconds and shrinking a little, up to 32 at once. Now and then the tracker takes an edge or a corner of the level for a mover for a moment. rd-vanilla has the boxes and echoes without the tint. Built for Linux, Windows and the OpenGL ES device, not run in the game yet (the scope could not be brought up by a script).
* [+] Screen effects (`v_screenFx`, `v_fxBlast`, `v_fxHeat`, `v_fxHit`, `v_fxWater`, `v_fxForce`, `v_fxSaber`, all on): the cgame passes sources and looks to the renderer once a frame (`re.ext.SetScreenEffect`) and rd-vulkan-vision draws them after the bloom: the wave and colour split of an explosion close by, heat haze over the jetpack, the flamethrower, a lit blade and the own barrel after long fire, the edge of a hit toward the attacker with a drain of colour and a heartbeat at low health, sway, fog, caustics and drops under and after water, the looks of the Force powers (rage tunnel with veins, protect dome with refraction and a ripple on hits, absorb streams, speed blur, a wave along a push or pull) and a flash where blades clash. Force Sight is a spirit world: a grade, a mist around the eye, players as smoking ghosts and smoke that smears when the camera turns. rd-vanilla keeps the old looks. Built for Linux, Windows and the OpenGL ES device. Run on Linux (RADV): Force Sight (the tint and the streaks). The other looks were not looked at one by one.
* [+] Sound for the screen effects: `v_muffle` has one owner in the cgame (the menu in the level, under water, a blast close by, Force Sight). `v_tinnitus` adds a ringing of two tones (3700 and 3790 Hz) after a blast close by, and `v_dreamReverb` adds a long Steam Audio reverb, one per ear, in Force Sight (only with `v_spatial 1`). New sounds `heartbeat.wav` and `rage_hum.wav` in `sound/vision`.
* [+] Binoculars (`v_binoculars`, on): two lenses through the scope pass, the mouse wheel zooms from 40 to 5 degrees once they have zoomed in by themselves, and the HUD shows the distance to the crosshair in metres, a compass strip and the zoom factor. rd-vanilla or 0 keeps the old binocular art. Not run in the game (could not be brought up by a script).
* [+] Rocket lock-on (`v_lockOn`, on): brackets around the target box close in over 8 steps, amber while locking, red and pulsing when locked, with 8 pips, a beep a step (`sound/vision/lock_1.wav` to `lock_8.wav`) and `lock_on.wav`. Vehicles and `v_lockOn 0` keep the old circle. Works with rd-vanilla. Not run in the game (could not be brought up by a script).
* [\*] rd-vulkan-vision: the light of a dynamic light uses the vertex normals and lights a floor it is just under (up to 12 units), which ended stripes on mp/ffa4.
* [+] Photo mode (`v_photo`, also a button in the bar of the menu in the level and a key row under Controls, Extras): the HUD goes and the camera circles the own player. The mouse turns it, the wheel moves it, the left button sets the depth of field focus (rd-vulkan-vision only), keys 1 to 5 pick a colour filter, the right button or Enter saves a PNG to the screenshots folder, Esc leaves. It leaves by itself on death, spectator or a menu. Run on Linux (RADV): the HUD went, the camera circled the player, the help line and the depth of field showed.
* [+] `v_soundTinnitus` (on, saved, row "Ringing Ears" under Setup): the dull sound and the ringing ears after an explosion close by are a setting of their own. They no longer need `v_screenFx` or `v_fxBlast` and work with any renderer. `v_fxBlast` is only the wave and the colour split now.
* [\*] A box of the boombox gets one of the reflection slots (`v_spatialSources`) first and never loses it to a louder sound. Before, a shot close by took the slot and the song jumped in loudness while shooting.
* [\*] Key login (`v_amkey`) is bound to the server by a host key (protocol version 2). The server makes a 32 byte random host key once and keeps it as base64 in `auth_host.key` in its game folder, next to `admin_keys.txt` (only when `admin_keys.txt` has keys; if the file can not be written the key is new at every start). It sends it with the nonce: `vs_auth nonce <nonce> <host key>`. The signed message has the host key in it, so a signature made for one server is no use on another. `vs_caps` is 399 now (was 143): bit 8 (256, `AUTH_CAPS_HOST`) is the key login with the host key. The client keeps the host key of every server address in `admin_hosts.txt` in its home folder (one line `<address> <host key>`, at most 8192 bytes, the oldest lines go first). The first login on an address keeps the key, like ssh `known_hosts`. A server that sends another key than the one kept for its address, or a key kept for another address, is not signed for and the player gets a message, also after the automatic login. `v_amkey trust` logs in and keeps the key the server sends now (for a server that was set up again, or one with a second address), and `v_amkey forget` forgets the host key of the address too. A server without a host key (older game module) is signed for only when the player typed `v_amkey login`, never by itself. Compatibility: a server of this version takes a version 1 signature (older clients) only when `vs_authHosts` is set. Without it an older client gets `vs_auth fail version` and is not logged in, so those players have to update, or the owner of the server sets `vs_authHosts`. Still open: the first manual login on a hostile server that the client never logged in to, and that has no `vs_authHosts`. Unit tests in `tests/vjk/auth.cpp`, and two local dedicated servers with a headless client (first login, next login, other key at a kept address, kept key at a new address, `v_amkey trust`, a server and a client of 0.2.8). See docs/japlus/security.md.
* [\*] Voice relay: from the server machine (the path of the web host, `vs_clientWebPort`) a datagram is taken with the token alone. More than 20 wrong tokens in a second from that machine now make the relay take nothing from it for 10 seconds, with one line in the security log. That stops the voice of browser clients for that time too. 2^32 tokens then take decades.
* [\*] The dedicated server catches SIGTERM and SIGINT (Linux, macOS) and quits at the start of the next frame as on `quit`: players are told, the game module ends and the web panel saves its sessions. A second signal while it quits ends the process at once. The warning "select() syscall failed: Interrupted system call" is no longer printed for a signal.
* [\*] The entity editor of the web panel refuses more that would end a dedicated server: a `misc_maglock` that starts inside a wall, deleting, clearing or editing away the last entity of a spawn class (`info_player_deathmatch`, `team_CTF_redplayer`, `team_CTF_blueplayer`, `team_CTF_redspawn`, `team_CTF_bluespawn`, `info_player_siegeteam1`, `info_player_siegeteam2`), and doing the same to the only entity with a targetname that a `trigger_shipboundary` or `trigger_hyperspace` names as `target` or `target2`. A clear keeps such entities and says "n deleted, m kept: why".
* [\*] `vs_clientWebBind` is read with `inet_pton` like `vs_webBind`. Before, `inet_addr` took "0" for every interface and "1" for 0.0.0.1. A value that is no IPv4 address still means 127.0.0.1, with a warning.
* [\*] rd-vulkan-vision, Vulkan and OpenGL ES device: sprites, lines, saber glow, oriented quads, cylinders, the bolts of lightning, the sparks of effects and the rain and snow are made in a vertex shader (`sprite.vert`). The CPU still works out where each one is, but writes one record a quad instead of four vertices and six indexes. `v_vkSprites 0` (cheat) builds the corners on the CPU as before. On the fight demo `clip_brawl1` the triangles built on the CPU in a frame went from 5505 to 1264, the pictures differ in 0.04 % of the pixels (0.75 % with sparks, which are random, as two runs of the old way are).
* [+] rd-vulkan-vision: the grass (surface sprites of the level) is placed once when the level loads and drawn from buffers with `grass.vert`, with the same fade, wind and sway. The ground under it is drawn from the buffer of the level too. `deformVertexes autosprite` and `autosprite2` on the level, saber marks that grow or fade, and vertex lit surfaces with several light styles also left the CPU. `v_vkGrass 0` (cheat, latched) places the grass on the CPU every frame. Known difference: the wind point is worked out at the sprite and not at the corners. On mp/ctf3 the triangles built on the CPU in a frame went from 906 to 416. Run on Linux (RADV) and with the OpenGL ES device on the desktop, no message from the validation layer. See docs/japlus/renderer.md.

# Features

## Single- and Multiplayer

* [\*] OpenJK now writes configs/screenshots/etc to `fs_homepath` directory. No longer have to run as administrator on Windows. (Multi-User support)
* [+] Added mouse-wheel to console
* [+] Add `r_noborder` option (windowed mode feature).
* [+] Add `r_centerWindow` option (windowed mode feature).
* [+] Add `r_mode -2` option to use desktop resolution.
* [\*] Removed cheat protection from `r_we` cmd, `r_dynamicGlow*` cvars
* [-] Removed CD Check Code
* [-] Removed Anti-Tamper Code. (It serves no purpose with source code available.)
* [\*] Shift-Escape will now also open the console as an alternate (e.g.: keyboard doesn't support the normal console key)
* [\*] Escape key will now close the console before anything else if the console is open
* [\*] Removed shift key requirement to open console
* [+] Raw Mouse Input is now available on all platforms with the in_mouse cvar at the current default value. (SDL2 uses raw mouse input by default)
* [\*] Drastically improved command/cvar tab-completion including auto-completion of arguments (e.g. `map mp/ffa1`)
* [+] Clipboard paste support added to all supported platforms now with SDL2.
* [+] Add `stopmusic` command
* [\*] Fixed segfault crashing in ragdoll code.
* [\*] Fixed `r_overbrightbits 1` resulting in blackness.
* [\*] Made the md4 (filesys) checksum code compatible with 64-bit operating systems.
* [\*] Fixed radar and rocket locking drawing not rendering properly.  Can now support all types of shaders with multiple images if wanted too.
* [\*] MAX_PATCH_PLANES does not occur on some maps anymore with OpenJK (this isn't related because OpenJK specifically changed something to cause the error, but because of newer compilers affecting optimizations on decimal numbers)
* [\*] `OpenJK` folder is now searched as a fallback location if `base` and `fs_game` folders do not contain the appropriate gamecode to load.
* [\*] Error log on crashing exits are now reported to a crashlog text file in your OpenJK homepath.
* [-] Removed `r_allowSoftwareGL` cvar as it is no longer useful or needed.
* [+] Added filename completion to `cinematic` command.
* [\*] No more viewlog console for client or server.
* [+] Added improved dedicated server console with color (0-7) support and arrow key support and tab completion that functions the same as in-game console.
* [-] Removed blackbars around screen when rendering in widescreen.
* [\*] Fixed white screen during load with aformentioned black bar removal.

## Singleplayer only

* [+] Now searches for jagamex86.dll (or equivalent) in the mod's folder, too, meaning SP Code mods are possible.
* [+] NPCs support alternate saber colours
* [\*] Fallback location for jagame mod bin is now `OpenJK` instead of `base`
* [+] Add `modelscale` and `modelscale_vec` support to `misc_model_ghoul`
* [\*] Fix external lightmap support
* [\*] `r_flares` defaults to 1 now like Multiplayer.

### Gamecode (only available in `fs_game openjk` and derived mods)

* [+] Added `cg_smoothCamera` (default 1)
* [+] Added `cg_dynamicCrosshair` (default 1)
* [+] Added simple hud from multiplayer with tweaks to match real HUD options. `cg_hudFiles 1`

## Multiplayer only

* [\*] Gamecode DLL files on Windows are no longer extracted to homepath or basepath, but a temporary file path.
* [\*] Drastically improved status (server) command
* [\*] Tweaked `forcetoggle` rcon command.
* [+] Added `weapontoggle` rcon server command similar to `forcetoggle` command.
* [\*] Fixed memory leak related to NPC navigation on map changes.
* [+] Cheats are now defaulted to 1 in menu. Do not be alarmed, starting normally will disable them or connecting to a non-cheat server. This allows cheats to work properly while playing back demos.
* [+] Add `sv_lanForceRate` (Defaults to 1) Feature was already enabled, but not toggleable.
* [\*] `globalservers` master server command now supports multiple master servers with the `sv_master1..5` cvars
* [+] Added `fontlist` command.  Useful for when making mods with custom fonts.
* [+] New serverside kick commands `kickall`, `kickbots` and `kicknum` (alias to `clientkick`).
* [-] Removed demo restriction code.
* [\*] `svsay` command prints to dedicated console
* [\*] Cvars will be sorted alphabetically when saved to disk
* [+] Added `ja_guid` userinfo field to uniquely track players for statistics
* [+] Added `cvar_modified` to show which cvars have been changed from default values
* [+] Added `s_doppler` sound effect for moving sound sources (rockets)
* [+] Added support for `surfaceSprites flattened` in MP.  (Fixes surface sprites on t2_trip)
* [+] Added in-engine ban code from ioquake3. Cmds: `sv_rehashbans`, `sv_listbans`, `sv_banaddr`, `sv_exceptaddr`, `sv_bandel`, `sv_exceptdel`, `sv_flushbans`. CVar: `sv_banFile`
* [+] `addFavorite` command added to add current or specified server to favorites list.
* [+] Add ability to paste in text files in the UI
* [+] Add `cl_motdServer1..5` cvars. `cl_motd` points to which one is used or 0 to turn off
* [+] Add QuakeLive style mouse accel option (`cl_mouseAccelStyle`, `cl_mouseAccelOffset`)
* [+] Server side demo recording per client (from their pov) support
* [+] Add `sv_blockJumpSelect` cvar to help prevent use of modded clients using an exploit with `FP_LEVITATION` with old mods
* [+] Add external lightmap support from SP
* [+] Added ability to substitute BSP entities with ones from an external .ent file when loading a map (for easier entity modding)

### Gamecode (only available in `fs_game openjk` and derived mods)

* [\*] Tweaked simple hud with adjustments to match real HUD options. `cg_hudFiles 1`
* [+] Added `pmove_float` cvar (default off) for no velocity snapping resulting in framerate-dependent jump heights.
* [+] `clientlist` displays clients by id/name and if they are a bot. (Shows real client id unlike `serverstatus`)
* [+] Added `cg_fovAspectAdjust` to correct field of view on non-4:3 aspect ratios
* [+] Added `cg_fovViewmodel` to adjust the field of view for first-person weapons
* [+] Added `cg_chatBeep` and `cg_teamChatBeep` to toggle the chat message sound
* [+] Added JK2 gametypes
* [\*] Rewrote `callvote` code to allow disabling specific votes, added more options (e.g. display map list)
* [+] Add server command `toggleallowvote` to easily adjust the bit values of `g_allowVote`
* [+] Add userinfo validation options (`g_userinfoValidate`)
* [+] Add server command `toggleuserinfovalidation` to easily adjust the bit values of `g_userinfoValidate`
* [+] `fx_wind` entity
* [\*] `fx_rain` entity supports most options from SP now. LIGHTNING/shaking not supported, acidrain doesn't actually hurt
* [\*] more `customRGBA` options from SP on NPCs
* [+] Force Sight surfaces (cgame modification required)
* [+] Human Merc NPC spawner
* [\*] Make `target_location` entities logical (no gentity space used in most cases)

# Increased/lifted limits

## Single- and Multiplayer

* [\*] Increased Command Buffer from 16384 to 128*1024
* [\*] Increased max cvars from 1224 to 8192

## Singleplayer only

* [\*] `MAX_SHADER_FILES` bumped to 4096 from 1024 to match MP


# Bugfixes

## Single- and Multiplayer

* [\*] Fixed incorrect `alphagen` enum usage.
* [\*] Fixed widescreen resolution changes causing black screen when UI restarted.
* [\*] Fixed crash when trying to run custom resolutions with a listen server.
* [\*] Fixed Gamma Clamp on WinXP+
* [\*] Fixed weather system incorrectly throwing up a warning with shader data if weather system was unable to parse a vector correctly.
* [\*] Windows now uses correct memory status code for > 2gb when checking if low on physical memory.
* [\*] Tweaks to the cvar code to make it more strict in terms of read only/cheats/init. Fixes a lot of broken rules with cvars.
* [\*] Fixed cvar commands that allow you to `cg_thirdPerson !` (i.e. use a value of `!`) prevent you from typing out longer strings starting with a `!` as the value.
* [\*] FX Flashes now properly scale to fov and window aspect.
* [\*] Fixed a lot of formatting security holes.
* [\*] Several Out-of-bounds memory access and memory leaks fixed.
* [\*] Improved command line parsing based off of ioquake3 patches.
* [\*] Improved `echo` command by preserving colors based off of ioquake3 patches.
* [\*] Improved `GL_Extensions` printing using ioquake3 fix to prevent crashes on newer cards.
* [\*] Alt-tab works properly
* [\*] ~~Fix Windows issue where pressing alt key in windowed mode caused a temporary freeze~~
* [\*] Fixed never fading shadow in text
* [\*] Fix up font renderer glyph positioning
* [\*] Fixed a hang with some weird music sample rates
* [\*] Fixed a crash at startup when `r_dynamicGlow` was set to 2
* [\*] Fix Out-Of-Bounds access in `CM_EdgePlaneNum`
* [\*] Clamp `scr_conspeed` to be in the range 1-100
* [\*] Fix stencil shadows not working if a model has more than 500 vertexes.

### Gamecode (only available in `fs_game openjk` and derived mods)

* [\*] Fixed overstrike mode when using any UI edit box
* [\*] Fix crash when standing on an NPC who has been knocked down

## Singleplayer only

* [\*] Fixed a nasty memory issue with clipboard pasting
* [\*] Fixed `MiniHeapSize` issue
* [\*] Fixed intro cinematic only displaying as white screen when on non 4:3 aspect ratio.
* [\*] Fixed potential out of bounds in sound code.
* [\*] Fixed buffer overflow in filesystem code which prevented use of some maps (atlantica). Raven made a `Com_Error` to prevent this from happening, we removed that as well.
* [\*] Rosh no longer randomly dies due to falling damage on the first level.
* [\*] Fix force absorb capping to 100 instead of maximum force points
* [\*] Fix some invalid chunk length errors on x86_64 platforms

## Multiplayer only

* [\*] Fixed several parsers that broke with characters which wrapped to being negative.
* [+] Added misc security fixes to allow servers to deal with connectionless packets.
* [\*] Added security fixes to prevent servers from spoofing clients with connect packets.
* [\*] Added security fixes to prevent anyone from spoofing clients with print packets.
* [\*] Fixed clients being able to set IP via cvar.
* [\*] Dedicated server binaries now correctly print sectioned prints without adding newlines erroneously.
* [\*] Fixed client crash related to ragnos NPC
* [\*] Fixed some item prediction errors in team games. (Item prediction still sucks in general though)
* [\*] Fixed buffer overflow in client side rcon command.
* [\*] Fixes names that contain `*` or `**` at start incorrectly showing up in notify top box and chat box when sending messages.
* [\*] Server side / `viewlog` console now strips the `[skipnotify]` and `*` properly too.
* [+] (Un)pausing the game in solo play now results in smoother transition.
* [\*] `timescale` frametime fixes. Frametime < 1 is no longer possible (bad things happen)
* [\*] Fixed invalid `r_textureBitsLM` resulting in fullbright mode without cheats
* [\*] CVar code in engine is more robust.  Protects against clients setting systeminfo cvars they should not etc.  More accurate warning messages when servers/vms/clients set cvars they should not.
* [\*] Fix exploit with `usercmd` bytes set to -128

### Gamecode (only available in fs_game openjk and derived mods)

* [\*] Fixed very bad out of bounds access in `G_LogWeaponFire` with NPCs.
* [\*] Fixed some voting issues relating to clients disconnecting and/or switching teams.
* [\*] Fixed some voting issues relating to bad percentage calculation for passing/failing.
* [\*] Fixed color code stacking in names where colors were supposed to be stripped. (This relates to engine and gamecode for full effect)
* [\*] Fixed saber lock crash
* [\*] Force updates of clients at a fixed interval to prevent certain exploits.
* [\*] Prevent fast teamswitch exploit
* [\*] Disable "scoreboard" team
* [\*] Fix some looping sound issues
* [\*] Fix spectators being stuck when they stop following dead/disintegrated players
* [\*] Precache weapons on `map_restart` if `g_weaponDisable` changes
* [\*] Properly detect server settings (weapon/force disable)
* [\*] Filter multiple "@" characters in a name resulting in a localised string lookup (translation)
* [\*] Default/missing model+saber is now "kyle"
* [\*] Fixed tournament queueing (for duel/power duel)
* [\*] Fixed issues where spectators had trouble flying through doors close together
* [\*] Fix `cg_smoothClients` affecting local player negatively
* [\*] Fix 2d screen tints with some effects being too dark (protect/abosrb/rage/ysalamiri/inlava/inwater/inslime)
* [+] Filter out servers with invalid chars in their information from the browser (`ui_browserFilterInvalidInfo`)
* [+] UI now supports multiple master servers
* [\*] Fix teamoverlay not making you look dead in siege when in limbo
* [\*] Fix glowing lights bugs on players and dead spectators in siege (in client mod)
* [\*] Properly load siege sounds for all models that have them, fallback to old broken location if necessary.


# Cleanup

## Single- and Multiplayer

* [-] MPlayer code removed.
* [-] Removed force feedback code (requires unavailable commercial library)
* [\*] Updated the JPG library
* [\*] Updated the PNG library
* [\*] Updated the zLib and minizip libraries
* [\*] Refactor binds code in the UI
* [-] Removed (unused) RMG code
* [\*] General buffer safety cleanup

## Multiplayer only

* [\*] Tweaked serverbrowser client engine code.
* [\*] Pure server code cleanups in client and server.

### Gamecode (only available in fs_game openjk and derived mods)

* [\*] Optimised .sab parsing and fixed some potential memory corruption
* [\*] Optimised .veh and .vwp parsing
