# Threads and the frame loop

The question was how to spread the game over several threads, starting at the entry point and keeping in mind
that server and client run in one process, one after the other. This document records how a frame runs, why the
two halves do not go onto two threads, what was measured and what follows from it.

Result: nothing was built. On the hardware it was measured on, no part of the frame comes near the frame time: a
dedicated server with 29 bots uses about 6 % of one core, a client 12 %, and up to half of that is the wait loop
itself. The one stage that would be the first candidate is described at the end.

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

2026-10-06, local instances on 127.0.0.1, Ryzen 9 5900X, map mp/ffa3, `sv_fps 20`, windows of 30 to 45 seconds.
The dedicated server had 31 bots added, 29 of them stay. Two instruments:

`com_speeds 1` prints one line per frame:

```
frame:812 all:  2 sv:  0 ev: 48 cl:  0 gm:  2 rf:  0 bk:  0
```

`all` is the frame without the wait, `sv` is `SV_Frame` without `gm`, `ev` the event loops including the wait
in `NET_Sleep`, `cl` is `CL_Frame` without the renderer, `gm` the bot frame and the game module, `rf` and `bk`
the renderer front end and back end. The counter is `Sys_Milliseconds`, whole milliseconds, and a frame starts
right on a tick, because the wait loop of `Com_Frame` polls until the counter moves. So whatever runs first in a
frame and is done within that millisecond reads 0 every time, `all` reads low by up to 1 ms, and the split
between two short stages is not reliable.

The CPU time of the process (`utime + stime` in `/proc/<pid>/stat`) over the same window. It is exact, but it
includes the wait loop: the last millisecond before a frame is a poll with `NET_Sleep( 0 )`, up to 1 ms of CPU
per frame.

The work of a frame lies between `all` and the CPU time per frame.

| Run | Frame | `all` | Of that | CPU of the process | CPU per frame |
|-----|-------|-------|---------|--------------------|---------------|
| a: dedicated, 29 bots | 50 ms | 1.96 ms | `gm` 1.88, `sv` 0.08 | 5.8 % of a core | 2.9 ms |
| g2: a with one real client that follows bots | 50 ms | 1.77 ms | `gm` 1.61, `sv` 0.17 | 6.3 % | 3.1 ms |
| d: a with a server demo of every bot | 50 ms | 2.49 ms | `gm` 2.07, `sv` 0.41 | 6.8 % | 3.4 ms |
| d2: d with one real client | 50 ms | 2.16 ms | `gm` 1.49, `sv` 0.67 | 7.1 % | 3.6 ms |
| g: the client of g2, follows bots through saber fights, 800x600, 60 fps | 16 ms | 0.87 ms | `bk` 0.86 | 11.5 % | 1.8 ms |
| h: listen server, 3 bots, the local player in the game, 1280x720, 125 fps | 8 ms | 0.01 ms | | 12.2 % | 1.0 ms |

Run d stands in for a server full of real players. A bot gets no encoded snapshot, `SV_SendClientSnapshot`
returns after the build. With `sv_autoDemo 1` and `sv_autoDemoBots 1` every bot records a server demo and then
runs the complete path: delta against its last frame, Huffman, and the write to the demo file. Only the `sendto`
is missing.

What it says:

- A server tick is 2 to 3 ms of work in 50 ms, nearly all of it bot AI and the game module. That part moves
  by half a millisecond from run to run with what the bots do.
- The snapshot path for a full server is the difference between a, g2 and d, d2: 0.3 to 0.6 ms a tick in
  `sv`, about 1 % of a core in CPU time, close to the noise between runs. That and 32 `sendto` is all that
  threads could spread. Waking a pool of workers and collecting them again every tick costs a good part of it.
- The client needs 0.9 to 1.8 ms of a 16 ms frame while it watches saber fights, and the listen server run
  finishes the whole frame, server tick included, within the first millisecond in 99 of 100 frames. A render
  thread lets front end and back end overlap, it can save the smaller of the two at best, some tenths of a
  millisecond.
- The only part with weight is the game module, and it is the part that can not run in parallel: entities act
  on each other through traces and damage, every bot calls traces and the botlib with shared state.

Limits: the clients ran on a hidden workspace in small windows, on a GPU that is not the limit at these sizes.
Not measured: the resolution and settings of a real session, a server with 32 real players over a real network,
other hardware, the Windows builds.

To repeat: start an instance with `+set com_speeds 1` (or type `com_speeds 1` in the console), average the
lines of the log and read the CPU time of the process over the same time. A scripted client needs a check that
it really is in the game, a `status` on the server or a `screenshot`. On a client a `wait` in a script counts
twice a frame, `Cbuf_Execute` runs before the server half and before the client half.

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
