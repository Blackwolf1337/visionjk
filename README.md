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

## License

VisionJK is free software under the GNU General Public License version 2, see [LICENSE.txt](LICENSE.txt).

It is OpenJK plus the changes of this repository. The engine is the work of the
[OpenJK maintainers and contributors](https://github.com/JACoders/OpenJK/graphs/contributors), of the ioquake3
contributors, Raven Software and id Software. The copyright notices in the source files name them and stay.
The icons are the OpenJK icons, see [shared/icons/license.txt](shared/icons/license.txt).
