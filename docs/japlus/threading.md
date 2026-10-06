# Threads and the frame loop

The question was how to spread the game over several threads, starting at the entry point and keeping in mind
that server and client run in one process, one after the other. This document records how a frame runs, why the
two halves do not go onto two threads, what was measured and what follows from it.

Result: nothing was built. On current hardware no part of the frame is limited by the CPU, the process waits for
94 to 96 % of every frame. The one stage that would be the first candidate is described at the end.

## How a frame runs

`shared/sys/sys_main.cpp` `main`: platform and console setup, `Com_Init`, then an endless loop that calls
`Com_Frame()`. The engine starts no thread of its own, there is no thread creation in `codemp/` or `shared/`.
The only second thread is the audio callback that SDL starts.

`codemp/qcommon/common.cpp` `Com_Frame`, one pass:

1. Wait. The frame length is `SV_FrameMsec()` (`1000 / sv_fps`) on a dedicated server, `1000 / com_maxfps`
   otherwise. `NET_Sleep` is a `select` on the sockets until the frame is due, a packet wakes it earlier.
2. `IN_Frame`, `Com_EventLoop`: keys and mouse from the event queue, the packets from the network, and the
   loopback packets the local client and the local server sent each other (`SV_PacketEvent`,
   `CL_PacketEvent`).
3. `Cbuf_Execute`, then `SV_Frame( msec )` (`server/sv_main.cpp`): `sv.timeResidual` collects the time,
   `SV_BotFrame` runs the bot AI, every `sv_fps` tick that is due runs one `GVM_RunFrame` (the game module: all
   entities and players), then `SV_SendClientMessages` builds and sends a snapshot for every client that is
   due.
4. Only when not dedicated: `Com_EventLoop` again (it picks up the snapshots of step 3 without a frame of
   delay), `Cbuf_Execute`, `CL_Frame( msec )` (`client/cl_main.cpp`): `CL_SendCmd`, `CL_SetCGameTime`,
   `SCR_UpdateScreen` (cgame, renderer front end, then at once the back end, there is no render thread),
   `S_Update`.

So "switching" between server and client means: the same thread runs the server half, then the client half. A
dedicated server has no client half. A client that is connected to another server returns from `SV_Frame` at
once (`com_sv_running 0`). The server tick is already independent of the frame rate through `timeResidual`.
The only link between the client and a local server are the loopback buffers in `qcommon/net_chan.cpp`
(`loopbacks[2]`, 16 messages each, no lock, one reader).

## Why server and client do not go onto two threads

The loopback would be a clean seam. Nothing behind it is.

| Shared | Where | Problem |
|--------|-------|---------|
| Cvars | `qcommon/cvar.cpp` | one hash table, `cvar_modifiedFlags`, `Cvar_VariableString` returns a pointer into the cvar. Both halves read and set them all the time. |
| Command buffer and tokenizer | `qcommon/cmd.cpp` | `cmd_text`, static `cmd_argv`; `Cmd_Argv` returns pointers into it. |
| Static return buffers | `qcommon/q_shared.c` | `va()` rotates 4 static buffers, `Info_ValueForKey` 2. `MSG_ReadString` and `COM_Parse` have one each. |
| File system, zone and hunk memory, `Com_Printf` | `qcommon/` | file handles, search paths, allocator lists, the print redirect buffer: no lock anywhere. |
| Collision model | `qcommon/cm_trace.cpp` | every trace does `cmg.checkcount++` and marks the brushes it visited with that value. Two traces at once (game module on the server, prediction in cgame) give wrong results, not just a race. A lock around the trace would serialise the hottest path. |
| Ghoul2 | `server/` | the server calls `re->G2API_*` at 60 places (collision, bolts), in the same renderer library instance the client draws with. |
| Errors and cross calls | `common.cpp`, `sv_init.cpp` | `Com_Error` is a C++ `throw` up to the `catch` in `Com_Frame`; an `ERR_DROP` of the server calls `CL_Disconnect`. `SV_SpawnServer` calls `CL_MapLoading` and `CL_ShutdownAll`. |

The gain would be small as well: see the numbers below. The realistic way to use more cores is to run one
expensive stage of a frame in parallel and join before the frame goes on. There are two candidates: the
per client snapshots on the server and the renderer back end on the client.

## Measured

2026-10-06, `com_speeds 1`, about a minute per run, local instances on 127.0.0.1, Ryzen 9 5900X, map mp/ffa3.
`com_speeds` prints one line per frame:

```
frame:812 all:  2 sv:  0 ev: 48 cl:  0 gm:  2 rf:  0 bk:  0
```

`all` is the frame without the wait, `sv` is `SV_Frame` without `gm`, `ev` the event loops including the wait
in `NET_Sleep`, `cl` is `CL_Frame` without the renderer, `gm` the bot frame and the game module, `rf` and `bk`
the renderer front end and back end. The counter is `Sys_Milliseconds`, so a value below 1 ms only means
something as the mean over many frames.

Mean per frame in ms:

| Run | Frames | Frame | `gm` | `sv` | `cl` | `rf` | `bk` | Waiting |
|-----|--------|-------|------|------|------|------|------|---------|
| a: dedicated, 31 bots, `sv_fps 20` | 1280 | 50 | 1.97 | 0.085 | | | | 96 % |
| a2: the same with one real client | 834 | 50 | 1.62 | 0.104 | | | | 97 % |
| c: that client, 60 fps | 2500 | 16 | | | 0.00 | 0.00 | 0.70 | 96 % |
| b: listen server, 3 bots, 125 fps | 2650 | 7.9 | 0.00 | 0.00 | 0.00 | 0.00 | 0.47 | 94 % |

Longest single frame: 4 ms (a), 3 ms (c), 2 ms (b).

What it says:

- Snapshots in parallel would spread `sv`. That is the whole engine side of the server with the snapshot build
  for 31 clients: 0.1 ms of a 50 ms tick, 0.2 % of one core. A bot gets no encoded snapshot, only the built
  one. The one real client raised `sv` by about 0.02 ms; 32 real players stay below 1 ms a tick. Waking a pool
  of workers and collecting them again costs the same order.
- A render thread lets front end and back end overlap. Front end and cgame together are below what the counter
  shows, so there is nothing to overlap.
- The only part with weight is the game module, 4 % of a core with 31 bots, and it is the part that can not
  run in parallel: entities act on each other through traces and damage, every bot calls traces and the botlib
  with shared state.

Limits: the test client stood still on a hidden workspace, a fight with many players in view costs more. Ten
times as much would still be 7 ms. Not measured: other hardware, a server with 32 real players.

To repeat: start any instance with `+set com_speeds 1` (or type `com_speeds 1` in the console) and average the
lines of the log.

## The first candidate, if a load ever needs it

Per client snapshots on a dedicated server. After `GVM_RunFrame` the game state does not change any more in
that frame, and `SV_SendClientMessages` (`server/sv_snapshot.cpp`) handles the clients one by one. A pool of
workers would take one client each and be joined before the function returns, behind a latched cvar that is
off by default. Game module, protocol and the content of a snapshot stay the same, so legacy clients see no
difference. What has to change, found by reading the path:

- `sv.snapshotCounter` and the mark `svEnt->snapshotCounter` (they keep an entity from entering a snapshot
  twice through a portal) become a bit mask over `MAX_GENTITIES` per worker.
- The ring `svs.snapshotEntities` with `svs.nextSnapshotEntities++` per entity: the list is known before the
  copy, so a worker reserves its slots with one atomic add.
- `SV_AddEntitiesVisibleFromPoint` writes `ent->s.number = e` into the shared entity when it is wrong: one pass
  on the main thread before the dispatch.
- Stay on the main thread: clients with a download (`SV_WriteDownloadToClient` reads files), with
  `unsentFragments`, with a running server demo, bots, and the whole frame while `sv_autoDemo` is on
  (`SV_BeginAutoRecordDemos`).
- Prints and errors on the path (`Com_DPrintf` and `Com_Error` in `SV_BuildClientSnapshot`,
  `SV_WriteSnapshotToClient`, `SV_SendClientSnapshot`, the `showpackets` lines in `net_chan.cpp`) become flags
  per job, the main thread prints or throws after the join.
- Cvars the path reads (`sv_maxRate`, `sv_minRate`, `sv_fps`, `sv_lanForceRate`, `sv_padPackets`,
  `com_timescale`, `showpackets`) are copied before the dispatch. `SV_RateMsec` sets a cvar when a rate is
  below 1000: that clamp moves in front of the dispatch.
- Lazy first calls are forced once on the main thread: `MSG_Init` (`msgInit`, `g_nOverrideChecked`) and
  `Sys_IsLANAddress` (`Cvar_Get` of `net_forcenonlocal`).
- `SV_Shutdown` (`server/sv_init.cpp`) joins the pool before it frees `svs.snapshotEntities`. `CMakeLists.txt`
  links no thread library yet.

Already safe: the Huffman tables are only read after `MSG_initHuffman`, `SV_SendClientSnapshot` and
`Netchan_Transmit` work on stack buffers, `SV_Netchan_Encode` has no shared state, `sendto` is thread safe per
socket, the `CM_*` calls of the visibility check only read.

Not worth it at any load: bot AI in parallel, entity thinks in parallel, loading the map on a second thread
(hunk allocator), the web panel (it never blocks).
