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
the rate limit on connectionless packets and so on rcon (`sv_maxOOBRateIP`), the stateless challenge, the
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
| `wa_http.c` buffers, `g_webadmin.c` bind | `realloc`/`malloc` not checked, a negative `Q_vsnprintf` used as a length, default bind on every interface. | Allocations checked (a short response instead of a crash), negative length guarded, default bind 127.0.0.1. |
| `wa_http.c` `WA_Base64Decode` | Signed `int` shifted left for every header character: signed overflow (UBSan). | Unsigned accumulator; the decoded bytes are unchanged. |
| `g_japlus_admin.c` `G_amlogin` | Unlimited tries on the three admin passwords, over `amlogin` and the `cp_login` userinfo. | After three wrong tries a player waits, doubling; reset on success and at each map; logged. |
| `g_japlus_admin2.c` `Cmd_amvstr_f` | `vstr` block list missed cvars with secrets or outside content. | Added `g_password`, `jp_clanPass`, `g_webPassword` and every `session<n>` (the IP and admin level of a player). |
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
| `ClientCommand: <n> [<ip>] "<name>": <command> <arguments>` | every client command. Not `say`, `say_team`, `tell` (the game log has them) and `score`. `amlogin` and `clanpass` with `<hidden>` for the arguments. Arguments cut to 200 characters. Off with `g_logClientCmds 0` |
| `CallVote: <n> [<ip>] "<name>": <console line>` | a vote starts: the line the console runs if it passes |
| `Web admin: <ip>: action ...` / `entity ...` / `wrong password` | every action and entity edit of the panel, and every wrong password |
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
  `g_webBind` keeps working; only an unset one now means localhost instead of every interface.

## Known and left alone

- The panel speaks plain HTTP. Bind it to localhost (the default now) and reach it through an SSH tunnel, or put a
  reverse proxy with HTTPS in front.
- The entity editor can still end the dedicated server by placing a class whose spawn code raises a game error, or
  by deleting all spawn points. OpenJK makes every game error fatal on a dedicated server. Decided to leave as is.
- JKA loads the client DLLs from pk3. Whoever plays on a server with `cl_allowDownload 1` trusts that server; the
  start script sets 0.
- `getstatus`/`getinfo` stay usable as amplifiers for a spoofed sender, held down by the existing rate limits.
- Two signed-shift notes from UBSan are stock OpenJK and were left: `sv_init.cpp` (a pk3 checksum feed) and, until
  this pass, the panel's base64 decoder (now fixed).
- Legacy clients have their own bugs (`CL_ParseSetGame` above was one in the stock client). The server can not fix
  a client, but it sends nothing that triggers them and keeps bounding names, models and command length.

## Added after the pass: the voice chat

The voice chat ([voice.md](voice.md)) came after this pass and adds attack surface of its own: a UDP port of
the game module (`g_voice.c`, off unless `g_voice 1`), and in the cgame a UDP socket, a codec (Opus) and an
audio library (miniaudio), both third party code built into the module. What was done about it is in the
section "Security" of that file: every length checked in one place, a random token per connection plus the
address of the game connection, a limit per speaker, no answer to anything that is not a datagram of a client,
and the server never decodes. 12000 malformed datagrams at the relay under valgrind gave no error. It was not
part of the audit above and has not been looked at by anybody else.

## Recommended server settings

```
seta g_userinfoValidate "25165823"   // leave on: rejects a client ip field and control characters in userinfo
seta sv_legacyFixes "1"              // leave on: the engine clears out of range selections in the usercmd
seta g_securityLog "2"               // security.log written line by line
seta g_logClientCmds "1"             // client commands in security.log
seta sv_maxOOBRateIP "1"             // the per address rate limit on connectionless packets and rcon
seta g_webBind ""                    // localhost only; set 0.0.0.0 only behind a proxy or a firewall
seta g_webPassword "<long random>"   // the panel is off without it
seta rconpassword "<long random>"    // or empty to disable rcon
```

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
