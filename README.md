# VisionJK

VisionJK is an engine and game code for Jedi Academy multiplayer. It is based on
[OpenJK](https://github.com/JACoders/OpenJK) (commit 260c59c2, 2026-09-29) and keeps its compatibility with the
existing game, servers and clients.

What it has on top of OpenJK:

- the features of the JA+ mod v2.4 Build 7, on the server and in the client ([docs/japlus](docs/japlus/README.md))
- a Vulkan renderer, `rd-vulkan-vision`, next to `rd-vanilla` ([renderer.md](docs/japlus/renderer.md))
- voice chat in the client module, with a relay in the game module ([voice.md](docs/japlus/voice.md))
- 3D sound with Steam Audio ([audio.md](docs/japlus/audio.md))
- a web admin panel in the game module ([webadmin.md](docs/japlus/webadmin.md))

The tree is built for 64 bit only. Single player and Jedi Outcast are still in the source but are not built.

## Privacy

- The client has no `ja_guid`. OpenJK makes a key file (`jakey`) and sends an identifier made from it to every
  server it joins. VisionJK does neither. A server sees it like the original game, without a GUID.
- `cl_motd` is 0 by default. Switched on, the client asks the update server for a news line on every connect and
  sends the name and vendor of the graphics card, version, platform, joystick setting and color depth along.

## Installing

Jedi Academy has to be installed. Put the content of a release folder into its `GameData` folder and run
`visionjk.x86_64` (Linux) or `visionjk.x86_64.exe` (Windows). The dedicated server is `visionjkded.x86_64`.

The engine starts in the mod folder `japlus` by itself. Settings are written to `~/.local/share/visionjk` on
Linux and to `My Games\VisionJK` in the documents folder on Windows, as `visionjk.cfg` (client) and
`visionjk_server.cfg` (dedicated server).

## Building

```
cmake -S . -B build64 -DCMAKE_BUILD_TYPE=Release
cmake --build build64 -j"$(nproc)"
cmake --install build64 --prefix release
```

`release/JediAcademy` is then a complete game folder. More, also the Windows cross build with mingw-w64, is in
[docs/japlus/README.md](docs/japlus/README.md).

## Documentation

Everything is in [docs/](docs). Start with [docs/japlus/README.md](docs/japlus/README.md): it says where the JA+
code is, how the tree is built and released, and has the same list as below.

What this tree adds, in [docs/japlus/](docs/japlus):

| File | Content |
|------|---------|
| [README.md](docs/japlus/README.md) | Overview of the JA+ port: what it was rebuilt from, where the code is, building, the release folder |
| [server.md](docs/japlus/server.md) | The game module: files, state, features, commands, what it tells the client |
| [client.md](docs/japlus/client.md) | cgame and ui: the features of the JA+ client plugin, cvars, prediction |
| [differences.md](docs/japlus/differences.md) | Where the port differs from the original JA+, and which OpenJK behaviour was kept or changed |
| [porting-method.md](docs/japlus/porting-method.md) | How the JA+ binaries were read and how the code was carried over |
| [verification.md](docs/japlus/verification.md) | What was compared against the original modules, what was seen in the game, what is open |
| [webadmin.md](docs/japlus/webadmin.md) | The web admin panel (not a JA+ feature): cvars, requests, how it ties into JA+, limits |
| [voice.md](docs/japlus/voice.md) | Voice chat with the microphone: in the client module, relayed by the game module, engine unchanged, clients without it unaffected |
| [audio.md](docs/japlus/audio.md) | `s_spatial`, sound that knows the level, with Steam Audio: walls muffle, rooms ring, sounds come round corners |
| [renderer.md](docs/japlus/renderer.md) | `rd-vulkan-vision`: how it is built, the state of each part, differences to `rd-vanilla`, what is left |
| [security.md](docs/japlus/security.md) | Security pass over the network code: findings, fixes, build hardening, server settings |
| [threading.md](docs/japlus/threading.md) | How a frame runs, why server and client share one thread, timing numbers |

Notes on the engine that come from OpenJK, in [docs/](docs):

| File | Content |
|------|---------|
| [libraries.md](docs/libraries.md) | How the engine finds and loads the game, cgame, ui and renderer modules |
| [renderer-architecture.md](docs/renderer-architecture.md) | Front end and back end of the renderer |
| [language strings.md](docs/language%20strings.md) | Format of the string files for the translations |
| [save games.md](docs/save%20games.md) | Layout of a single player save game |
| [sound-code-declutter.md](docs/sound-code-declutter.md) | Notes on the state of the sound code and ideas for tidying it |

## License

VisionJK is free software under the GNU General Public License version 2, see [LICENSE.txt](LICENSE.txt).

It is OpenJK plus the changes of this repository. The engine is the work of the
[OpenJK maintainers and contributors](https://github.com/JACoders/OpenJK/graphs/contributors), of the ioquake3
contributors, Raven Software and id Software. The copyright notices in the source files name them and stay.
The icons are the OpenJK icons, see [shared/icons/license.txt](shared/icons/license.txt).
