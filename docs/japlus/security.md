# Security pass

This is the result of a security audit of `codemp/` with a view to the network code: remote code execution,
buffer overflows and heap corruption first, then the rest. The server had to stay compatible with legacy clients
(jamp 1.01, the JA+ plugin, other forks), so nothing in the protocol, the configstrings, the userinfo keys, the
server commands or the snapshots was changed. Every fix is a bound, a check or a wait.

## The baseline

The engine (`server/`, `qcommon/`, `client/`, `sys/`) is OpenJK 260c59c2 unchanged but for six lines in
`q_shared.h` and `q_math.c`. The known idTech 3 holes are already closed there and were left as they are: the
bounded Huffman decode (`Huff_offsetReceive` with `maxoffset`), the fragment bounds in `Netchan_Process`, the
field counts in `MSG_ReadDeltaEntity` and `MSG_ReadDeltaPlayerstate`, the 1022 byte cap in `SV_SendServerCommand`,
the `cmdCount` and `reliableAcknowledge` checks, downloads limited to referenced pk3 (`FS_MV_VerifyDownloadPath`),
the rate limit on connectionless packets (`sv_maxOOBRateIP`), the stateless challenge, the
userinfo check in the game (`G_ValidateUserinfo`), and vote strings without `;`.

The attack surface this fork adds is the web admin panel (`codemp/game/webadmin/`, from JKAdmin, speaks HTTP to the
network), the JA+ port, the extra features and the client modules. The JA+ port itself was already careful
(`Q_strncpyz`, `Com_sprintf`, client numbers checked, `;\r\n` stripped before console lines). The findings were in
the panel, in old Raven code the panel now reaches from outside, and at two spots in the client.

## Fixed

Each line is one commit on the branch `security/hardening`.

| Where | Problem | Fix |
|-------|---------|-----|
| `g_webadmin.c` `WA_ReadRequest` | `Content-Length` read with `atoi`; a value near `INT_MAX` made `headLen + bodyLen` wrap, the bound passed, and the body's terminating zero was written ~2 GB before the buffer, before any login. Confirmed as an AddressSanitizer SEGV. | `WA_ParseLength` (digits only, capped at `WA_MAX_REQUEST`); the bound is now `bodyLen > WA_MAX_REQUEST - headLen`, which can not wrap. |
| `g_webadmin.c` POST handlers | No CSRF defence: a browser sends the panel login with any request a hostile page makes it send, so that page could run player actions and the entity editor. | `WA_SameOrigin` checks `Sec-Fetch-Site`/`Origin`/`Referer` against `Host` (or `X-Forwarded-Host`) on every POST. Requests with none of those headers (curl, scripts) still pass. `X-Frame-Options`, a frame-ancestors CSP and `Cross-Origin-Resource-Policy` on every response. |
| `wa_http.c` `WA_CheckAuth` | Unlimited password tries; `strcmp` compare leaks length through timing. | Constant time compare; after five wrong tries an address waits, doubling up to half an hour; a wrong try goes to the security log. |
| `wa_http.c` buffers, `g_webadmin.c` bind | `realloc`/`malloc` not checked, a negative `Q_vsnprintf` used as a length, default bind on every interface. | Allocations checked (a short response instead of a crash), negative length guarded, default bind 127.0.0.1 (every interface again since 2026-10-08, see "Known and left alone"). |
| `wa_http.c` `WA_Base64Decode` | Signed `int` shifted left for every header character: signed overflow (UBSan). | Unsigned accumulator; the decoded bytes are unchanged. |
| `g_japlus_admin.c` `G_amlogin` | Unlimited tries on the three admin passwords, over `amlogin` and the `cp_login` userinfo. | After three wrong tries a player waits, doubling; reset on success and at each map; logged. |
| `g_japlus_admin2.c` `Cmd_amvstr_f` | `vstr` block list missed cvars with secrets or outside content. | Added `g_password`, `jp_clanPass`, `vs_webPassword` and every `session<n>` (the IP and admin level of a player). |
| `g_utils.c` `AddRemap` | `targetshadername`/`targetshadernewname` entity keys `strcpy`'d into 64 byte global buffers; the entity editor can set them to any length. | `Q_strncpyz`. |
| `g_client.c` `SetupGameGhoul2Model` | Player model path built with `strcpy`/`strcat` into a MAX_QPATH stack buffer that the finished path does not fit. | `Com_sprintf`/`Q_strcat`; a name too long falls back to the default model. |
| `g_session.c` `G_ReadSessionData` | `sscanf` `%s` with no width into `siegeClass[64]` and `IP[48]`. | `%63s`, `%47s`. |
| `g_bot.c`, `ui/ui_gameinfo.c` | `.bot`/`.arena` file names from the pk3 list `strcpy`/`strcat`'d into 128 byte buffers. | `Com_sprintf`, skip a name that does not fit. |
| ban paths (`wa_actions.c`, `g_japlus_admin.c`) | The IP went into an `addip` console line unchecked. The IP is already the real one (the engine prepends it and wins the lookup, and `g_userinfoValidate` rejects a client `ip`), but a relaxed validation could let a newline or `;` through. | `G_CleanIP` keeps only the characters of an address before the console line. |
| `client/cl_parse.cpp` `CL_ParseSetGame` | Off by one: a server filling the name wrote the terminating zero one past `newGameDir[MAX_QPATH]`. | Loop to `MAX_QPATH-1`. |
| `bg_pmove.c` `PmoveSingle`, `w_force.c` `WP_InitForcePowers` | `forcesel` and `invensel` of the usercmd were tested with `1 << value`. A shift by 32 or more wraps on x86, so `forcesel` 32+n passed for power n and became `forcePowerSelected`, an index into the force arrays. The engine clears such values only while `sv_legacyFixes` is on. | Both have to be below `NUM_FORCE_POWERS` and `HI_NUM_HOLDABLE`; the init checks the range before it shifts. |
| `cgame/cg_japlus.c` auto replier | Builds `tell`/`say` with another player's name and sends it through the console; quotes and `%` were dropped but not a newline or `;`, so a crafted name ran as its own console command. | `FormatReply` drops control characters and `;` too. |

## Logging

Added after a reported attack that the logs could neither confirm nor rule out: nothing on the server recorded
what a player sent or what was done through the panel. `security.log` (and the console of a dedicated server)
now has one line for each of these:

| Line | When |
|------|------|
| `ClientCommand: <n> [<ip>] "<name>": <command> <arguments>` | every client command. Not `say`, `say_team`, `tell` (the game log has them) and `score`. `amlogin` and `clanpass` with `<hidden>` for the arguments. Arguments cut to 200 characters. Off with `vs_logClientCmds 0` |
| `CallVote: <n> [<ip>] "<name>": <console line>` | a vote starts: the line the console runs if it passes |
| `Web admin: <ip>: action ...` / `entity ...` / `wrong password` | every action and entity edit of the panel, and every wrong password |
| `Web admin: <ip>: command <console line>` | a line typed into the console of the panel. What follows a word with `pass` in it is replaced by `...`, here and in the echo on the console |
| `Web admin: <ip>: control start id <n>` / `control stop id <n>` | the panel takes a player over or lets him go |
| `amlogin: wrong password from <name> [<ip>]` | a wrong admin password |
| `Odd userinfo: <n> [<ip>] <bytes> bytes: ...` | a userinfo with control characters or of more than 800 bytes, the control characters as `\xNN`, the values of `password`, `cp_login` and `cp_clanPwd` left out |

The file is buffered and written when the buffer is full or the map ends. `seta g_securityLog 2` writes every
line at once, which is what a server under watch should use.

## Build hardening

`-Wformat -Wformat-security` on every target. Linux (both game modules and the dedicated server):
`-fstack-protector-strong` (the runtime is in libc, no new dependency), `-D_FORTIFY_SOURCE=2` in the optimised
configs, `-Wl,-z,relro -Wl,-z,now`. Windows DLLs: `-Wl,--nxcompat -Wl,--dynamicbase` for DEP and ASLR. No stack
protector on mingw, since there it would add a dependency on `libssp-0.dll`. Checked on the built files: the Linux
binaries carry RELRO, BIND_NOW and the canary; the DLLs have the DEP and ASLR bits and import no `libssp-0.dll`.
Off when a sanitizer build is configured.

## Unchanged

- Packet format, Huffman, netchan, snapshots, usercmd, the protocol number, the configstring layout, server
  commands.
- Every JA+ command and cvar with its behaviour. New are only the waits after wrong passwords and the longer
  `amvstr` block list.
- The panel in use and API. Requests without an `Origin` header (curl, scripts) still work. A config that sets
  `vs_webBind` keeps working; only an unset one now means localhost instead of every interface (taken back on
  2026-10-08, see "Known and left alone").

## Known and left alone

- The panel speaks plain HTTP. Bind it to localhost (`vs_webBind 127.0.0.1`) and reach it through an SSH tunnel,
  or put a reverse proxy with HTTPS in front.
- The panel listens on every interface by default again (2026-10-08). This pass had made an unset `vs_webBind`
  mean 127.0.0.1; the owner of the fork opens the panel from other machines and decided for `0.0.0.0` as the
  default, an empty value included. What that costs: whoever sets a password and a port has a plain HTTP login
  on every address of the machine, and a config written to the recommendation of this file (`vs_webBind ""`)
  is open to the network after the update. What holds it: the panel is off without `vs_webPassword`, wrong
  passwords are slowed down, and a value that is no address falls back to 127.0.0.1.
- The entity editor can still end the dedicated server by placing a class whose spawn code raises a game error, or
  by deleting all spawn points. OpenJK makes every game error fatal on a dedicated server. Decided to leave as is.
  Later the panel got a guard for what can be told from the keys (`WA_SpawnRefused`, [webadmin.md](webadmin.md)).
  On 2026-10-08 a `misc_bsp` placed in the editor did end a server: that class is refused now and every class
  the editor offers is placed bare by a check. Since then it also refuses a `misc_maglock` that starts inside a
  wall, deleting the last entity of a spawn class, and deleting the only entity that a `trigger_shipboundary` or
  `trigger_hyperspace` names as target (see "Limits" in [webadmin.md](webadmin.md)).
- In this tree no module is loaded from a pak on Linux and Windows unless `com_unpackLibraries 1` is given at
  start, programs in downloaded paks are blocked, and on macOS a module bundle from a pak is loaded only with
  `com_unpackLibraries`. Whoever plays on a server with `cl_allowDownload 1` still trusts that server; the start
  script sets 0.
- `getstatus`/`getinfo` stay usable as amplifiers for a spoofed sender, held down by the existing rate limits.
- Two signed-shift notes from UBSan are stock OpenJK and were left: `sv_init.cpp` (a pk3 checksum feed) and, until
  this pass, the panel's base64 decoder (now fixed).
- Legacy clients have their own bugs (`CL_ParseSetGame` above was one in the stock client). The server can not fix
  a client, but it sends nothing that triggers them and keeps bounding names, models and command length.

## Added after the pass: the console of the panel

The Log tab of the panel has a line that runs on the server console (`POST /api/command`, `wa_log.c`). That is
the right the panel had already: "Save and run on server" of the config tab writes `server.cfg` and executes it,
which runs any console line. The new line needs the same login and the same origin check as every POST, takes
one line of 480 characters at most with control characters removed, and goes to the security log. To show what
the console answers the panel reads `qconsole.log` of the server folder and sets `logfile 2` when it is 0, so a
server whose panel console was opened once keeps that file until it is stopped.

## Added after the pass: the player control of the panel

`POST /api/control` (`wa_control.c`) replaces the moves, buttons and view of the commands of one player while
the page holds him. It writes nothing but the usercmd of that player, with the moves clamped to what a client
may send and the buttons masked to the ones a key can press.

## Added after the pass: the voice chat

The voice chat ([voice.md](voice.md)) came after this pass and adds attack surface of its own: a UDP port of
the game module (`g_voice.c`, off unless `vs_voice 1`), and in the cgame a UDP socket, a codec (Opus) and an
audio library (miniaudio), both third party code built into the module. What was done about it is in the
section "Security" of that file: every length checked in one place, a random token per connection plus the
address of the game connection, a limit per speaker, no answer to anything that is not a datagram of a client,
and the server never decodes. 12000 malformed datagrams at the relay under valgrind gave no error. It was not
part of the audit above and has not been looked at by anybody else.

## Removed: rcon

The server takes no rcon since 2026-10-11 (`SVC_NoRemoteCommand` in `server/sv_main.cpp`). The protocol has no
way to be made safe: the password went over the wire in the clear with every command, a guess cost one packet,
and what a command printed went back to whatever address the packet claimed to come from. An `rcon` packet is
answered with one line (at most once a second) and counted by the intrusion detection.

- The cvar `rconPassword` no longer exists on a dedicated server. A config that still sets it gets one yellow
  line at the first map, the value does nothing.
- What rcon was used for is done on the console of the server or in the web panel (`webadmin.md`), whose
  console tab runs the same commands.
- The client keeps its `rcon` command and its `rconPassword` and `rconAddress` cvars: they are for other
  servers.
- The JA+ rank named rcon stays, it is the name of the fourth admin rank and has nothing to do with the
  protocol. The `rcon` flag in the command table of the game module stays too: it means "from the console of
  the server".

## Added later: admin login without a password

Since 2026-10-11 (`game/auth`, `cgame/auth`, see server.md and client.md). The three rank passwords of JA+ and
`cp_login` are off unless `vs_adminPasswords 1`: this changes a server at the update, its admins need a line
in `admin_keys.txt` or a one-time code.

- Signatures are Ed25519 from Monocypher 4.0.2 (`lib/monocypher`, as its authors ship it, compared with the
  tag on GitHub). Nonces and codes come from the system (`/dev/urandom`, `CryptGenRandom`), a server without
  random bytes lets nobody in.
- A nonce is good for one answer and 30 seconds, per client. A client may name eight keys in 30 seconds.
- A code has 40 bits, lives 60 seconds, works once, and wrong ones run into the wait of wrong passwords.
- `admin_keys.txt` is read through the file system of the game, public keys are no secret. Comments from it
  and from key files are printed without `^`, `"` and `%`.
- The login is bound to the server by a host key (protocol version 2). The server makes 32 random bytes once and
  keeps them as base64 in `auth_host.key` in its game folder, and sends them with the nonce. The signed message
  has the host key in it. The client keeps the host key of every server address in `admin_hosts.txt` (the first
  login on an address keeps it, like ssh `known_hosts`) and does not sign when a server sends another key than
  the one kept for its address, or one kept for another address. A server that passes on the question of
  another one has a key that is not the key of that other server, so the signature is no use there. The host
  key is no secret on purpose: the protection is what the client kept. A server that proved it has a secret
  would not help, a hostile server could pass that challenge on as well.
- A server of this version takes a version 1 signature (older clients, no host key) only when `vs_authHosts` is
  set. Without it the older client gets `vs_auth fail version`: those players have to update, or the owner sets
  `vs_authHosts`. A client signs for a server without a host key (older game module) only when the player typed
  `v_amkey login`, never by itself.
- Open: the first manual login on a hostile server, one the client never logged in to and that has no
  `vs_authHosts` on its side. Nothing is kept for that address yet. Tested with two local dedicated servers and
  a headless client, and in `tests/vjk/auth.cpp` (the message in both versions, `admin_hosts.txt` find and
  set). A key that was kept, a server at a kept address with another key, a key kept for another address and
  `v_amkey trust` all behave as above.

## Added later: the web panel has accounts

Since 2026-10-11 (`webadmin/wa_auth.c`, `wa_totp.c`, see webadmin.md). What was one password in every request
is now: accounts with three roles, passwords as Argon2id hashes, sessions in a cookie no script and no other
site gets, a second step by time, tokens for scripts, a login with a key of the browser, and a proxy mode in
which only the reverse proxy is let in. `vs_webPassword` is read once, to make the first owner.

- A visitor who is not logged in gets the page of the login (a file of its own, 9 KB) and two requests:
  `/api/login` and `/api/login/key`. Everything else answers 401.
- Roles are checked on the server for every request, by its path, and for `/api/action` by the action.
- The check that a POST comes from the page itself runs before the login too, so no other site can log a
  visitor in to an account of its own.
- What is kept on disk: hashes of passwords, of tokens, of recovery codes and of session cookies. The secret
  of the second step is kept as it is, the server has to work with it.
- Tested apart from the game (`japlus_port/tools/authtest`): HMAC-SHA1 against RFC 2202, the codes against RFC
  6238, base32 against RFC 4648, Argon2id of the Monocypher of the tree against RFC 9106. The QR code of the
  page was read back with zbarimg for texts of every size it takes.
- Run on 2026-10-11 against a server on this machine (`japlus_port/tools/sectest/e2e.sh`, part 1: 59 checks
  over HTTP, part 4: the pages in a headless browser). The login and what it refuses, the cookie, the roles
  route by route, the token, the second step with the code that does not count twice and a recovery code that
  works once, accounts made and refused, a signature made for the game refused by the panel, the login with a
  key, the proxy mode both ways, a session after a change of the map. In the browser: the page of the login,
  the tab Account, the QR code read back from the picture of the page, and a key the browser made. Two things
  were changed for it: a request to add an account says what is wrong with it before it says "come again",
  and what is typed on the console of the server no longer makes the next login wait.

## Added later: intrusion detection

Since 2026-10-11. `server/sv_ids.cpp` and `client/cl_ids.cpp`, in the engine, so it is there for every game
module. The bounds checks of the engine and the game keep a bad packet from doing harm: it is thrown away. The
detection is told about each of them, so that somebody who keeps sending them is written down and, on the
server, no longer listened to. It finds what looks like the known attacks on games of this engine and what no
client of the game sends. It does not find a hole nobody knows.

### On the server

| Cvar | Default | Meaning |
|------|---------|---------|
| `vs_ids` | 2 | 0: off. 1: events are written down. 2: addresses are blocked and players dropped as well |
| `vs_antiWallhack` | 0 | 1: a player no line of sight reaches is not sent to a client, see below |

Every event has a weight. The weights add up for the address and fall off again, two a second. At 100 the
address is blocked for a minute, and for twice as long each time it happens again, up to an hour: its
connectionless packets are dropped unread, and a player the event came from is dropped.

| Event | Weight |
|-------|--------|
| a connectionless packet that is no command, or one over 1400 bytes | 5 |
| rcon (the server takes none) | 10 |
| connectionless packets past the rate limit of the address (`sv_maxOOBRateIP`) | 1 |
| a connect with a wrong challenge | 3 |
| from a player: a message that can not be read, a number of moves or an acknowledgement out of range, a fragment that does not fit | 25 |
| from a player: reliable commands that are missing | 10 |
| from a player: a weapon or a force power in a move that does not exist | 5 |
| from a player: commands past the flood protection | 1 |
| from a player: a download he may not have / one whose path leaves the game or is no pk3 | 10 / 50 |
| from a player: a line break inside a command | 30 |
| from a player: `../` in a command that is no chat | 20 |
| from a player: 48 bytes above 127 in a row of which most are no UTF-8 | 35 |
| from a player: a userinfo that is too long | 25 |

- Format marks (`%s`, `%n`) are not in the list: the engine turns every `%` into a dot when it reads a string
  from a message, so none ever reaches a command. A first version looked for them and could never have found
  one, which the check of 2026-10-11 showed.
- Every event is a line in `ids.log` of the game folder and on the console: the time, the address, the player,
  what it was, and what came with it as plain characters. At most one line in two seconds for an address and
  an event (the rest is counted and named with the next line) and thirty a second overall, so a flood does not
  fill the disk. The web panel shows the file (Log tab, "Intrusions", for an admin and up).
- `vs_idsstatus` on the console lists the addresses with their scores and blocks, `vs_idsunblock <address|all>`
  lifts a block.
- The address of a packet can be made up, and a connectionless packet is taken without an answer. So a block
  only ever stops connectionless packets (getinfo, getstatus, connect), never the game of somebody who is on
  the server, and connectionless events do not count at all for an address that has a player on the server.
  The worst somebody can do with made up addresses is keep a stranger from connecting for a while, which a
  flood does too. Packets inside a connection can be made up as well by somebody who guesses the port number
  of the client (65536 tries); he could throw that player off before this, by the sequence numbers.
- Players behind one address are one address: when one of them is blocked, the others play on, but none of
  them can connect again until the block ends.
- There is no speed check. The plan had one by the time of the moves (a move that is ahead of the time of the
  server comes from a clock that runs fast), and it was written and then taken out on 2026-10-11: a client
  whose every frame counted three times was run against it and was never more than a few frames ahead, because
  the client keeps its time at the time the server tells it, and the game cuts a move back to 200 ms ahead
  anyway. The hack gains nothing in this game. What the count would have caught is every player after the
  server stood still for a quarter of a second.
- `vs_antiWallhack 1`: the map tells a client of every player in the rooms it can see into, and a cheat draws
  them through the walls. With the cvar a player is left out of a snapshot when no line from the eye of the
  client reaches his box (its middle, top, bottom and corners, widened by 16 units and by where he will be in
  a fifth of a second). Only brushes of the map stop a line, and none that are seen through or not drawn;
  doors never hide anybody. Never hidden: a player within 384 units, anybody from a spectator, anybody from
  a player who uses Force Sight. Off by default because it changes the game: a client that is not told of a
  player does not hear him either, so steps and sabers behind a wall are silent until he comes into view.
  It costs up to eleven traces for a pair of players each snapshot.

### On the client

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_ids` | 1 | what a server sends that no server of the game does is written to `ids.log`, and a server that keeps at it is left. 0: off |
| `v_memwatch` | 1 | the memory monitor, see "Added later: the memory monitor of the client" |
| `v_idsShare`, `v_idsReputation`, `v_idsMaster` | 1, 1, `master.jka.vision:29170` | reports to the masterserver, see "Added later: reports to the masterserver" |
| `v_idsSeq`, `v_idsLast`, `v_idsSeqShown` | | the events for the notices of the cgame, see below |

- Written down, and the client leaves: a path in the place of the game folder (at once), data of a download
  that was not asked for and fragments that do not fit (at the third). Also written down, and the client
  leaves at once: more data of a download than it said it has (the name and both sizes are in the line). The
  download of a pak by UDP gave the size in the first block, and a server could go on sending data until the
  disk was full. This is a check of the engine: it happens with `v_ids 0` too, only the line in `ids.log` needs
  `v_ids 1`. With `v_ids 1` it is the intrusion detection that leaves, so the server is remembered (see below).
  What came of that download is deleted now.
  With `v_ids 0` the engine ends the connection itself. A download from the web address of a server stops at 512 MB, as before.
- Written down: a message the engine ends the connection for (an index past its table, a message that ends
  early, an unknown command), more than 400 commands in a second, an escape character in a command (it is
  taken out: it means nothing to the game and is there for the terminal a console may be printed to), and
  all cvars a server may not set in one systeminfo, in one line (`cvars it may not set`, with key=value of
  each). The client keeps such a cvar as it is and warns on the console, as it did before. It does not leave
  for them, because some mod servers (old JA+ ones) set client cvars that way. A protected cvar (for example
  `fs_basepath`) is checked first. A server that sends one in its systeminfo is written down (`a cvar no server
  may set`, with key=value) and the client leaves at once, so the server is remembered like the others (refused
  once on the next connect). With `v_ids 0` the engine still ends the connection, without the line. Before, a
  protected cvar was only refused as a cvar the server may not set, because that check came first, and the drop
  for protected cvars in the engine could never be reached.
- Written down and thrown away: a list of servers (getserversResponse) that nobody asked for. A list is taken
  only from a master server the client asked, and only within 10 seconds of asking. From anybody else, or
  later, it is thrown away (with `v_ids 0` too), written down as `a list of servers nobody asked for`, and
  never acted on. Anybody could put made up servers into the browser this way, and the browser pings every
  address of such a list, so a forged list made clients ping third parties.
- Changed besides: everything the engine prints to the terminal and to its console log file (client and
  dedicated server, Linux and Windows) has control characters replaced by a dot: every character below 32
  except line break and tab, DEL, and the C1 controls in UTF-8 (C2 80 to C2 9F, which includes the CSI). The
  colours of the terminal are not affected, they are made after this from the ^ codes. Before, only server
  commands were cleaned (that stays, with its line in `ids.log`). Now hostnames, player names of
  serverstatus, OOB prints and anything else can not move the cursor or clear the screen either. A side
  effect: the separator JKA puts into chat lines (character 0x19) shows as a dot on the terminal. The console
  inside the game is not changed.
- Written down and never acted on: a connectionless packet that claims to be from the server and comes from
  another address (print, disconnect, connectResponse, challengeResponse). Leaving for those would let
  anybody throw a player off any server.
- Changed besides: the client answers an `echo` request only from its server. It answered anybody with
  whatever the packet asked for, to wherever the packet said it came from.
  One more case is answered: an `echo` from a server the client asked for its info or status and still waits
  for, when the text to send back is `echoResponse getinfo <random>` (a pending ping of the server browser or
  of `ping`) or `echoResponse getstatus <random>` (a pending `serverstatus` or the bot/human check of the
  browser). Some servers (the SkyMod mod, for example SkyTemple) answer getinfo and getstatus only after the
  address that asked has echoed such a text back, so that a forged sender cannot use them for a reflection
  flood. Without this answer these servers showed up in the browser with no info. An echo with that text
  from any other address is not answered, because then anybody could send getinfo to such a server with the
  player's address as sender and the client would confirm it. Every other echo is refused and written down
  as before.
  For such an asked echo the whole rest of the line counts as the text, so it is the same with or without
  quotes (the engines only echo the first word when the server did not quote the text, and the client would
  have refused it). The answer is that whole text. The `ids.log` line of a refused echo names the whole text.
  An echo from the connected server is answered as before.
- Changed besides: the client reads the whole download message of a server before it decides anything: the
  block number, the size of the file in block zero (or -1 and the reason the server gives), the length of the
  block and its data. Before, a download message nobody asked for (and one that came while a pak came from the
  web address of the server) was left after its first byte, and the rest of it was taken for the next commands
  of the server. So a server could hide commands in such a message, and the client ended the connection for a
  message it could not read instead of writing down the download nobody asked for. Same for block zero coming
  again after the client had it: the size in it was not read before. ioq3 and OpenJK read it the same old way.
- A server the client left for the intrusion detection is remembered until the game ends (at most the last 16
  servers, by address and port; nothing is written to disk). The first connect to it again is refused with the
  reason: `You left <address> before: it sent <what>.` and that `ids.log` has the details. The next connect goes
  through, so the player can still join on purpose. If the intrusion detection makes the client leave that server
  again, it is refused once more the next time. The server browser shows a red `IDS` in the column of the ping
  instead of the number. It is not in front of the name: the name is chosen by the server, so any server could
  put that mark there itself, while the ping is measured by the client. The engine gives the ui the mark as a
  key of its own (`ids` 1 in the info of the server). The ui of VisionJK shows it. A ui module of another mod
  that does not know the key shows the ping as before, and the refused connect works there too because the
  engine does it. With `v_ids 0` none of this happens.

The live view, since 2026-10-28:

| What | Where |
|------|-------|
| `v_idsstatus` | prints the last 32 events of this run (time, address, what it was, what came with it) and the servers left this run, each with the reason |
| `v_idsforget <address\|all>` | forgets a server that was left, so the next connect to it is not refused |
| the notice | each event is a notice of the HUD with a red bar (`v_notify`) |
| the window Intrusions | a button of the server browser; shows the last 64 lines of `ids.log`, the newest first |

- The cgame learns of an event from three cvars of the engine. `v_idsSeq` (read only) counts the events of the run.
  `v_idsLast` (read only) is the last event, made of the address and the fixed name of the event from the engine
  table, never of text the server sent. `v_idsSeqShown` is the number the cgame has shown a notice for. An event
  during the connect is shown after the loading, and a map change shows nothing twice.
- The window cleans every line again (colour codes and control characters become dots), though the engine did it
  when it wrote the line.
- With `v_ids 0` there are no events, so there are no notices.

Tested apart from the game (`japlus_port/tools/idstest`, 38 checks with a clock the test sets): how the
weights add up and fall off, when an address is blocked and for how long, that connectionless events do not
count against an address with a player, `vs_ids 1`, the machine itself, what is taken for an attack in a
command and what is not (chat, percent signs, long words of Cyrillic, Chinese and accented letters).

Run on 2026-10-11 against a server on this machine (`japlus_port/tools/sectest/e2e.sh`): unknown, oversized
and rcon packets written down, the address blocked after ten rcon packets and its packets not read,
`vs_idsstatus`, `vs_idsunblock`, a userinfo with a path that goes up written down for the player who sent it and
nobody dropped for one of them. `vs_antiWallhack`: a client in the first room of the test level was told of
the player next to it and not of one in the third room, and of both once the cvar was off (what the server
sends was listed with `cl_shownet 3`). A player in the second room was told also with the cvar on, and rightly:
the portal of the first room looks into the second. The client side noted nothing about a server that does
nothing wrong, and was not run against one that does.

Run on 2026-10-27: a unit test of the filter of the terminal (escape, bell, carriage return, backspace, DEL,
the CSI in UTF-8, the chat separator; Cyrillic text and a non-breaking space are left as they are; a cut at
the end of the buffer does not split a pair). A dedicated server that printed a text with a CSI in UTF-8
showed a dot in its place. A client with a master server on this machine (a small script) took the list of
that master, and wrote down and threw away a list from another port of the same address and one from the
master 12 seconds after it was asked. A client on a local server was made to leave it by a test command that was
taken out again afterwards. The server was then marked, another address was not, the next connect was refused
with the reason and the one after it went through to the game. The mark was looked at in the browser of a game
on the desktop, against the test server with vs_idsTest 4, first in front of the name and then in the column of
the ping.
These now run with `tools/test/ids-test.sh` (see [testing.md](testing.md), "The intrusion detection of the
client"). On 2026-10-27 all six scenarios passed: none, cvars, protected, gamedir, size, unasked. For size the
client wrote down `135688 bytes said, 137216 sent`, left, was refused once on the next connect and left again on
the one after it. For protected the client left, was refused once on the next connect and left again. unasked
was also run with the old way of reading the message: it failed there with `Illegible server message` (the
client dropped at the first message), and it passes with the new one.

Run on 2026-10-28: `tools/test/ids-test.sh` passes all six scenarios again, now also with a check of
`v_idsstatus` and of the notice (`v_idsSeqShown` is 3 in the scenario `cvars`), and a screenshot of the first
connect for each scenario. The notice and the window Intrusions were not looked at by a person.

## Added later: the memory monitor of the client

Since 2026-10-28 (`client/cl_memwatch.cpp`, `shared/sys/sys_memwatch.h`, `sys_memwatch_rules.c`). The intrusion
detection above looks at what a server sends. The monitor looks at the game itself, for what the code of an
attacker that got in would leave behind: a payload for a hole nobody knows is found by what it does to the
process, not by how it came.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_memwatch` | 1 | 1: the monitor runs. 0: off. It needs `v_ids 1`. The default is 0 in the hotpatch and the sanitizer builds, which write code or make executable memory themselves |

Regions of memory and child processes are looked at every 2 seconds. The code of the game is hashed (SHA-256)
64 KB a frame.

| Sign | Linux | Windows | Kind |
|------|-------|---------|------|
| game code changed | the executable sections of the program, the renderer, the cgame and the ui (`dl_iterate_phdr`) are hashed when loaded and again while they run | the same, from the section headers of the module | strong |
| code from a deleted file, a memfd or a temporary folder | `/proc/self/maps` | `VirtualQuery` | strong |
| a child process the game did not start | `/proc/self/task/<tid>/children` | a snapshot of the processes | strong |
| writable and executable memory | `/proc/self/maps` | `VirtualQuery` | weak |
| new executable memory from no file | `/proc/self/maps` | `VirtualQuery` | weak |
| a library outside the program folder and the folders of the system | `/proc/self/maps` | `VirtualQuery` and the name of the module | weak |

- A child process is no finding within 5 seconds of `Sys_OpenURL` (the browser), and `pulseaudio` is no finding.
- Strong: a line in `ids.log`, a notice, and on a server the client leaves it and remembers it like the other
  servers it left (the first connect again is refused with the reason). The reason reads `this client found <what>`.
- Weak: a line in `ids.log` and a notice, nothing more, at most 8 lines of a kind a run. Drivers, overlays (Steam,
  Discord, RTSS) and injected libraries make these signs as well, so they are no ground to leave.
- What existed at the first look counts as the start of the game and is not written down, except the strong signs.
- macOS has none of this, only the crash part below.

The crash part. On a server the file `ids_session` in the home folder holds the address of the server, from the
connect to the disconnect. If it is still there at the next start, the last run ended on that server. That is
written down as a `crash` event, and the first connect to that server is refused with the reason. It tells the
player that the server may have been the cause. A bug of the game makes a crash too, which is why a crash weighs
less than an attack in the reports below.

Windows now writes a crash log too (`SetUnhandledExceptionFilter` in `sys_win32.cpp`). Before, an unhandled
exception left nothing. It names the exception, the address and the module, like the one of Linux (see
[client.md](client.md), "When the game crashes").

The limit, stated plainly: code of an attacker that already runs in the process can switch the monitor off. What
the monitor catches is a payload that does not know of it, and the line in `ids.log` is written before anything
else is done about it.

Tested apart from the game: `tests/vjk/memwatch.cpp` (the parser of the maps lines and the rules for Linux and
Windows paths).

Run on 2026-10-28 on Linux against a local server, with a temporary test command (taken out again): memory that
is writable and executable was written down and the client stayed. Code from a memfd, changed code and a child
process each made it leave, and the next connect was refused once. A crash that was forced was noted at the next
start, and the server was refused once. In the normal play time on the AMD GPU (RADV) under gamescope there was no
false finding. The same with the Windows build under Wine: rwx memory written down, a code change and a `cmd.exe`
child made it leave, the crash log names the exception, the address and the module, and the crash was noted at the
next start.

Not tested: real Windows (only Wine), overlays like Steam, Discord and RTSS, the NVIDIA GPU, macOS.

## Added later: reports to the masterserver

Since 2026-10-28 (`client/cl_reputation.cpp`; the packets were in `server/sv_reputation.cpp` until 2026-10-30). A client that
left a server for an attack tells the masterserver. The masterserver counts the networks that did and hands the
list to other clients, which warn their players. A player that was thrown off once is no longer alone with it.
With `v_ids 0` nothing is sent and nothing is fetched. The masterserver is not part of this repository.

| Cvar | Default | Meaning |
|------|---------|---------|
| `v_idsShare` | 1 | 1: a server the client left, or crashed on, is reported to the masterserver. 0: off |
| `v_idsReputation` | 1 | 1: the list of flagged servers is fetched, marked in the server browser and the first connect to one is refused. 0: off |
| `v_idsMaster` | `master.jka.vision:29170` | the masterserver for both |
| `v_idsShareTold` | 0 | set to 1 after the first report, which prints one console line that says what is sent |
| `v_idsShareLocal` | 0 | 1: servers on the local network are reported too. For tests |

What is reported:

| Event | Name | Weight |
|-------|------|--------|
| a download nobody asked for, fragments that do not fit, a path as game folder, more download data than said, a protected cvar | `download`, `fragment`, `gamedir`, `size`, `protected` | 2 |
| changed game code, code from a deleted or temporary file, a child process | `code`, `exec`, `child` | 2 |
| a crash on the server | `crash` | 1 |

- Only events that made the client leave over the connection, the strong signs of the memory monitor, and the
  crash. Never connectionless events, escape characters or refused cvars.
- The report has the address of the server and the name of the event, nothing about the player. The masterserver
  sees the address the report comes from and keeps its first three numbers (the /24 network).
- The first report prints a yellow line on the console that says so, and how to turn it off.

Packets, on the port of the masterserver:

| Packet | Answer |
|--------|--------|
| `idsChallenge` | `idsChallengeResponse <c>` |
| `idsReport <c> <address:port> <event>` | `idsReportResponse ok`, `wait` or `bad` |
| `idsList <c>` | `idsListResponse <part> <parts> <address:port:networks> ...` |

- `<c>` is a keyed hash of the address and port of the sender and of the minute. It proves that the sender gets
  the packets of the address it writes from. Without it anybody could report in the name of another address, and
  a list, which is longer than its question, could be sent to an address that never asked.
- A server is flagged when networks reported it in the last 7 days: three with an attack, or six with a crash. The
  masterserver counts a network (/24) once for a server, limits how often an address may report and ask for the
  list, refuses reports of private or local server addresses, and keeps what it counted in a file of its own.

The masterserver is a small program of its own, and its one UDP port answers nothing but the voice datagrams and
the `ids` packets above. `getinfo`, `getstatus`, `getchallenge`, `connect`, `rcon`, `getservers` and anything else
are dropped without an answer, so the port has no game server query to ask, no rcon and no client slot.

The client looks the name of the master up on a thread of its own (`Sys_ResolveIPv4`), so the game does not wait
for the lookup. It fetches the list at start and with the server browser, at most every 10 minutes. A flagged
server has an orange `IDS` in the ping column of the browser. The red one is for a server the client left itself,
also for its own memory finds. The first connect to a flagged server is refused with the reason: how many
networks reported it in the last 7 days. The next connect goes through.

The limit, stated plainly: somebody with addresses in many /24 networks can still get a server flagged. The owner
of the masterserver can clear it. A flag is a warning to players, nobody is blocked by it.

Run on 2026-10-28 on one machine: a master, the IDS test server (`vs_idsTest 2`) and two clients, with
`v_idsShareLocal 1`. Client 1 left and reported. Client 2 fetched the list, and its first connect was refused with
the reason, and the second went through.

Not tested: the orange mark and the window Intrusions looked at by a person, the master on the real host.

## Added later: the TV of the boombox

The TV service of the masterserver is an HTTP server that only the web server of the site reaches. It answers
`GET /tv/<video id>?t=<second>` and nothing else: a bad id or second, any other parameter or any other path gets 400
or 404, and it limits sessions, connections per address and the time a request may take. Viewers of one video and
second share one session, so many players cost one.

On the client the picture is fetched by a background thread from `v_tvService`. The cvar is protected, so a server
can not point it elsewhere, and the engine takes only https, or http to 127.0.0.1 for tests. The stream is decoded
with pl_mpeg (`lib/pl_mpeg`, MIT). A picture larger than 640x360 is refused and so is more than 4 MB of waiting
data. Damaged copies of a stream were decoded under AddressSanitizer: with a broken motion vector pl_mpeg reads up to
a line and a byte past the end of a frame, so every block it allocates gets 4096 zeroed bytes more
(`client/cl_video.cpp`, `lib/pl_mpeg/README.txt`). After that 300 damaged streams decoded without an error. Not
tested: nginx on the host, and the service with real YouTube videos.

## Added later: the chat emojis of the store

The service of the model store also takes emoji uploads (issue #9, `/models/emoji/api/upload`). It checks the type and
the size: the file must be a PNG that decodes, 16 to 256 pixels each way, 64 KB at most, and the name has 2 to 24
characters of a-z, 0-9 and `_` and is not a name of the game or of another upload. Nothing else is checked, the owner
takes an emoji down afterwards (`modelstore.py remove-emoji <name> [--block]`). The limits are 10 uploads an hour and
30 a day per address, 300 emojis in the store, and nginx allows a body of 64 KB on the three POSTs. The upload
answers a delete key, shown once.

The client takes an emoji only from the list of the store, and only when the file has the size and the SHA-256 of the
list. Otherwise it does not keep the file. It fetches nothing in a demo, and `v_emojiStore 0`
stops it. On a pure server it fetches now, see the interactive emojis below. `base/emojistore.txt` records which files came from the store. When a new list lacks one of them, the client
deletes it, so an emoji the owner took down goes from the players too. Files that did not come from the store are
never deleted. Not tested: the store on the host.

## Added later: the interactive emojis of the store

The emote store of the model service (`/models/emotes/`, issue #9) takes the emojis players make on
jka.vision/emotes.html. The upload is JSON with the choices, and the service validates each one against its list (the
emoji, the burst, the colours, the path, the spin, the size from 60 to 160, the pop, the sound, the voice). A name is
shared with the chat emojis both ways, so a name belongs to one store only. The limits are 10 uploads an hour and 30 a
day per address, and 500 emojis in the store. An own sound comes as a base64 WAV and the server cleans it: it makes it
16 bit mono 22050 Hz, trims the silence, cuts it to 2 seconds with a fade, sets the peak to -1 dBFS and keeps the RMS at
most -14 dBFS. So a sound can not be long, large or loud, whatever the file was. The delete is by key, anyone can
report, and the owner takes one down with `modelstore.py remove-emote <name> [--block]`. Not tested: the store on the
host.

The client takes the list `emotes/index.txt` and the sounds only from the store. A sound is kept only with the size and
the SHA-256 of the list and at most 96 KB, and a file the store no longer has is deleted. A received `TOY_EMOTE_STORE`
carries only a number, and the cgame uses it only to pick an entry of its own local list. A number outside the list
fetches nothing but the list again, at most once a minute, and a number outside the ranges is dropped by `BG_ToyValid`.

The chat emojis and the sounds of the emotes are fetched on pure servers now too. `FS_IsToyFile` in `files.cpp` (it
replaces `FS_IsToyPicture`) reads `gfx/emoji/*.png`, `sound/emotes/*.wav` and `emotes/index.txt` from the folder of the
player on a pure server, with the files it read before. This is harmless for the pure check: the files are pictures
and sounds that are only seen and heard, and the list is text that the cgame parses for choices from fixed lists. None
of them changes the game. Not tested: a pure server with the stores in a real game.

## Added later: the drawings of the board

The drawing board of issue #9 uploads its picture to the same store (`/models/drawings/api/upload`). The engine got
`Sys_HttpPost` for it (curl, and WinHTTP on Windows). Anyone can upload, as with the emojis. The service checks only the
format: the exact size (`VJD1` and 256 x 128 bytes) and that every pixel is one of the 32 colours. The limits are 60
uploads an hour and 300 a day per address, and the store keeps the newest 5000. The owner takes one down with
`modelstore.py remove-drawing <number>`. The client fetches a picture only by a number below the limit and keeps the
file only when it has exactly the size of a drawing. Not tested: the POST on Windows.

The board can also take the picture of the system clipboard (`v_clipboardImage`, `codemp/client/cl_clipboard.cpp`).
It accepts only a PNG or JPEG (on Windows also a bitmap, written as a TGA), and a picture of 32 MB or less. It writes
only into `drawings/` of the home folder, through the file system of the engine. The path must not contain `..` or a
drive letter or a backslash, and must not start with `/`. On X11 the engine asks the owner of the clipboard on a
connection of its own, nothing waits for it, and the answer is dropped after 3 s. The picture is then loaded by the
normal image loaders, as any picture of the Picture button is. Not tested: Windows and macOS.

## Added later: the sprays of the store

The spray store of the model service (issue #9, the upload of `v_sprays upload`) takes pictures of the players. It
checks the format: a square RGBA PNG of 64 to 512 pixels, a power of two, 512 KB at most, with a valid `vjAn` chunk for
the frames, and the pixels must unpack to exactly the rows (no decompression bomb). The same file gets the same
number. The limits are 30 uploads an hour and 120 a day per address, and the store keeps the newest 20000. The owner
takes one down with `modelstore.py remove-spray <number>`. The client fetches a sheet only by the number that came with a spray, into `sprays/store/<number>.png`.

On a pure server the files of `sprays/` and `sprays/store/` are read from the folder (`FS_IsToyFile` in
`files.cpp`, which replaces `FS_IsDrawing`, with the files of `drawings/`). The game decodes a gif with stb_image
(`lib/stb/stb_image.h`) from the folder of the player, not from the server. Not tested: the store between two players,
Windows.

## Recommended server settings

```
seta g_userinfoValidate "25165823"   // leave on: rejects a client ip field and control characters in userinfo
seta sv_legacyFixes "1"              // leave on: the engine clears out of range selections in the usercmd
seta g_securityLog "2"               // security.log written line by line
seta vs_logClientCmds "1"             // client commands in security.log
seta sv_maxOOBRateIP "1"             // the per address rate limit on connectionless packets
seta vs_webBind "127.0.0.1"           // this machine only. The default is every interface: fine on your own network or behind a firewall
seta vs_webProxy "<address>"          // or: the address of the reverse proxy with HTTPS in front of the panel, nobody else is let in
seta vs_authHosts "<address:port ...>" // the addresses clients reach the server by, for the login with a key
```

There is no `rconpassword` line: the server takes no rcon, see above. `vs_adminPasswords` stays 0.

## Tested

A sanitizer build (`-fsanitize=address,undefined`) of the dedicated server and the game module ran on a private
server on 127.0.0.1 (own port, own `fs_homepath`, master servers off), the public server untouched. Against it:

- The panel: `Content-Length` of `2147483647`, `4294967295`, `-1`, forty digits, non-numeric, empty (the old
  build crashed on the first, the fixed one survives every one); a POST from a foreign `Origin`/`Referer` and with
  `Sec-Fetch-Site: cross-site` (403), the same from the panel's own origin and with no origin header (200), and
  through an `X-Forwarded-Host` (200); five wrong passwords then a sixth (429), the right one refused while
  blocked.
- Connectionless packets: 461 malformed `getstatus`/`getinfo`/`getchallenge`/`rcon` and random `connect` and junk
  packets; the server stayed up and answered a valid `getinfo` at the end.
- No AddressSanitizer memory error over the whole run. The one UBSan note that was mine (the base64 shift) is
  fixed.

A scripted client that is not logged in sent all 38 JA+ admin commands, the rcon rank commands, the admin gun
and cycle commands, the blocking wall commands and the cheats of the base game (59 commands,
`japlus_port/tools/clienttests/noadmin.cfg`): every one was refused and none had an effect on the server. A second
script (`seclog.cfg`) and three panel requests checked the log lines above and that the two passwords it sent
appear in no log.

`cppcheck` and `gcc -fanalyzer` on the changed and new files showed nothing. All four shipping configurations
build. The JA+ regression (`tools/smoke_test.sh`, `tools/gcheck.sh`) was run to confirm the mod side still works.

Not checked from here: a real jamp 1.01 with the original plugin (needs Wine and the game install), the Windows
DLLs at run time (they build and link), macOS. Their compatibility follows from the unchanged protocol.

## Audit of 2026-10-20

An audit of master `c8bb609b` and of the website repository, eight areas. The report is
`agent/reports/security-2026-10-20.md` in the project folder outside the repository. The fixes are on master
since the merge `1290abdca` ("Merge branch 'feature/security'"). The releases since then were tested with
`tools/test` (smoke, sanitizers, compat).

| Where | Problem | Fix |
|-------|---------|-----|
| `cgame/cg_servercmds.c` `CG_DuelField` | the duelist configstrings (30, 31) were copied into a 64 byte stack buffer without a bound (stock JKA), so a server could write past it, on Windows without a stack protector | one bounded field reader; the duel winner and the duelist numbers outside 0..31 become -1 |
| `qcommon/cvar/cvar.cpp` `Cvar_CurrentName` | a server (or a module) that set a cvar by its old name (`cvar_renames.h`) passed the checks of `Cvar_Server_Set` and `Cvar_VM_Set`, which saw a name nobody has, and the rename forwarded the set with force: protected and archived cvars such as `v_voiceMasterHost` and `v_authServers` could be set by any server | old names are mapped to the new one before the flags are read |
| webadmin | behind a proxy without `vs_webProxy` every visitor was one address, so five wrong logins of anybody locked everybody out, and the live view came after that wait | `vs_webProxy` takes a list, the live routes come first, a wrong password no longer makes the account wait, Argon2id runs on a thread of its own, the pages carry a script hash CSP |
| `client/cl_download.cpp` | `jp_DlBaseURL` could point at the network of the player; a pak entry name of 256 bytes or more was read without its end (also in `FS_LoadZipFile`); 2 GB per pak | no private hosts, terminated names, 512 MB |

Two more fixes of the audit were in the masterserver (the limits of what a session may ask of a box, and of the
token answers of the relay). It is not part of this repository.

Smaller ones: the score count is clamped from below (`cg_servercmds.c`), the disruptor beam length is capped,
the dots of the lead indicator are capped, chat log lines have no control characters, lag compensation is capped at
the measured ping plus a frame (`unlagged/g_unlagged.c`), the relay of a game server takes only HELLO, AUDIO and
RADIO and has `vs_voiceKbps`, the client keeps its master token against made up answers, and for the boombox:
`SIGTERM` before `SIGKILL`, `--no-plugin-dirs`, playlist ids of 12 characters or more and not `RDMM`, songs up to
ten hours, error lines with paths not shown to players.

Left as it is: the updater files are served from the same container that runs the game server and the
masterserver. With the accepted updater without a signature, a root on that host would reach every client. The room
key of a server without the module stays known to a kicked player until the next map (see voice.md).

## Audit of the client, 2026-10-28

An audit of the client: the engine client, the cgame, the ui, both renderers and the `bg_` code the cgame runs. The
report is `agent/visionjk/reports/security-2026-10-28-client.md` in the project folder outside the repository. Most
of the places are stock OpenJK or JKA code. Every fix keeps legacy servers and clients working.

| Where | Problem | Fix |
|-------|---------|-----|
| cgame configstring and server command copies (`cg_main.c`, `cg_servercmds.c`, `cg_players.c`: model names, shader state, NPC sound folders, siege state) | copied into fixed buffers without a bound | bounded; a model name that does not fit is no model |
| the StringEd text of `print`, `cp` and `disconnect` (cgame and two engine copies) | the text after a replacement was written without a bound | one bounded function `Q_StringEdExpand` (`shared/qcommon/q_string.c`, unit test `tests/vjk/stringed.cpp`) for all three |
| `kg2` and `ircg` server commands | entity and weapon numbers were not checked | only numbers the game sends are taken |
| key login of the cgame (`cg_auth.c`) | server text was part of a format string | passed as text |
| siege files (`bg_saga.c`, `cg_saga.c`, `g_saga.c`) | values copied without a bound, team texts used as a format | the two parser functions take the size of their buffer (all callers), `Q_strncpyz` for the texts, the file is ended after it is read |
| demo playback (`cl_main.cpp`, `cl_parse.cpp`, `files.cpp` `FS_Read`) | a negative message length passed the check | refused; `FS_Read` refuses a negative length; message numbers that go back are refused; a demo does not change `fs_game` |
| BSP loading in both renderers and the collision map (`tr_bsp.cpp`, `cm_load.cpp`, `qfiles.h` `BSP_LumpsInFile`) | counts and offsets of the file were trusted | lumps, surface counts and ranges, patch sizes, shader and fog numbers, visibility data, the entity lump, brush sides, nodes, leaves and submodels are checked against the file and each other; sub BSP models (`tr_model.cpp`) are taken again by name and limited to `MAX_SUB_BSP` |
| model loading (`tr_model.cpp` `R_LoadMD3`, `tr_ghoul2.cpp` `R_LoadMDXM`, `R_LoadMDXA`) | offsets and counts were taken from the file | every part is checked against the real file length and every index against what it names, before the model goes into the model cache; names in the files are ended; the traps that copy the GLA and surface names use `Q_strncpyz` |
| pictures (`tr_image_png.cpp`, `tr_image_tga.cpp`) | width times height could wrap | at most `IMAGE_MAX_SIDE` 16384 a side (`rd-common/tr_common.h`); PNG with 16 bits a channel are reduced to 8; the row length is checked; TGA reads stay inside the file |
| pak entries (`files.cpp`) | no limit on the size, the name or the number of entries | an entry larger than 512 MB or with a name with control characters, quotes or `;` is left out of the hash and the lists as a blocked one is (it still counts for the pure checksum); paks with more than about a million entries are refused; `FS_ReadFile` refuses a file larger than 512 MB |
| RoQ video sound (`cl_cin.cpp`) | a chunk larger than its buffer was decoded | skipped |
| effect files (`FxTemplate.cpp`), saber files (`bg_saberLoad.c`, size parameter through `ui_saber.c`), vehicle files (`bg_vehicleLoad.c`), `sounds.cfg` of a model (`cg_players.c`), netf and psf override files (`msg.cpp`) | unbounded copies, unlimited override widths | bounded copies, override widths limited to -32..32 |
| format strings from pak names in the ui (head names, force config names, the orders scripts accept one number only) and the font name formats (`tr_font.cpp`) | the names were used as a format | passed as text |
| cvars a server may set (`cvar.cpp` `Cvar_Server_Set`) | a cvar the player made with `set` or `seta` could be set by a server, and any cvar of this tree (`v_`) | neither can be set by a server, unless the engine made it a systeminfo one; `nextdemo` is registered so a server can not create it; the key file path refuses a network path |
| download review (`cl_download.cpp`, `files.cpp`) | a `.blocked` list that was too large or unreadable blocked nothing; names were classified with `\` not `/`; files of the "other" kind were not blocked with menus; module names and bundle folders were not programs | the whole pak is blocked when the list is larger than 16 MB or unreadable; names are classified with `/` for `\`; files of the "other" kind are blocked together with menus; module names and bundle folders count as programs; on macOS a module bundle from a pak is loaded only with `com_unpackLibraries` |
| caches (`vk_texcache.cpp`, `snd_cache.cpp`, `snd_spatial_bake.cpp`) | read from a pak as well | read only from the profile folder; texture sizes bounded |
| paths (`cl_parse.cpp`, `FS_CheckDirTraversal`, `cl_renderer`, library loading) | `fs_game` of a server and `cl_renderer` were taken as they came; libraries were searched outside the program folder | `fs_game` of a server must be one folder name, and `FS_CheckDirTraversal` refuses any `..` part; `cl_renderer` must be a plain name; libraries load from the folder of the program, and on Windows `SetDefaultDllDirectories` keeps the current folder out of the search |

Smaller ones: `ids.log` is capped at 4 MB a run, the text scroll of the ui keeps its buffer, the species list of the
ui is bounded, and `exec` in a HUD menu of the cgame does nothing (it called a NULL function).

Left as it is: the DNS lookup thread of the voice chat is waited for when the cgame unloads, since its code is in
the module (a slow lookup can hold a map change). The bundled libjpeg of the Windows build is 9a of 2014. The admin
rows of the player menu follow the word of the server, the server decides.
