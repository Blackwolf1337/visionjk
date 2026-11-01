# Building VisionJK

This is the build guide for the multiplayer game (codemp): the engine, the dedicated server, the renderers and the three
modules `jampgame`, `cgame` and `ui`. Single player and Jedi Outcast are in the tree but are not built.

The mod directory is `japlus`. The engine of this tree starts in it by itself, another OpenJK needs `+set fs_game japlus`.

## What you need

| Thing | Used for |
|-------|----------|
| A C and C++ compiler | the whole tree |
| CMake 3.20 or newer | the build uses `file(GLOB ... CONFIGURE_DEPENDS)` and the commands here `cmake -S` and `ctest --test-dir` |
| SDL2 | the client. Found with `find_package(SDL2 CONFIG)`, or bundled in `lib/SDL2` (Windows only, 64 bit) |
| zlib, libpng, libjpeg | found on the system, or bundled in `lib/zlib`, `lib/libpng` and `lib/jpeg-9a` |
| OpenGL | the classic renderer |
| Python 3 | packs the assets into a pk3, see `BuildJPAssets` |
| Boost (unit_test_framework) | only for the unit tests, `-DBuildTests=ON` |
| Vulkan headers and `glslc` (package shaderc) | only for the Vulkan renderer and the GPU echo, see below |
| mingw-w64 | only for the Windows cross build |

The bundled libraries are switched by the options `UseInternalZlib`, `UseInternalPNG`, `UseInternalJPEG` and
`UseInternalSDL2`. They are off by default, and on by default for Windows (all four) and with `-DUseInternalLibs=ON`.
On macOS the JPEG and PNG ones are on. The voice chat takes Opus and miniaudio from `lib/`, they are built into the
cgame (`BuildJPVoice`). Steam Audio is a submodule, see "Steam Audio" below.

## 64 bit only

The tree is built for 64 bit only. `CMakeLists.txt` stops the configure with the error "32 bit builds are not supported,
use a 64 bit compiler" when the compiler has 4 byte pointers. A toolchain file for 32 bit, the SDL2 libraries for
32 bit and the code for 32 bit x86 were removed after commit `5ab290ba`. A stock `jamp.exe` is 32 bit and can not load
these modules, it plays with its own cgame and ui.

## Linux

64 bit Linux, with engine and dedicated server:

```
cmake -S . -B build64 -DCMAKE_BUILD_TYPE=Release
cmake --build build64 -j"$(nproc)"
cmake --install build64 --prefix release
```

The build directory is not meant to be looked into. The install collects what the build made.

`release/JediAcademy` is then a complete game folder, and its content goes into `GameData` as it is:

```
JediAcademy/
  visionjk.x86_64  visionjkded.x86_64  rd-vanilla_x86_64.so  rd-vulkan-vision_x86_64.so
  japlus/
    jampgamex86_64.so  cgamex86_64.so  uix86_64.so
    zz_japlus_client_assets.pk3
    server.cfg and the other configs of assets/configs
  base/
    jampgamex86_64.so  cgamex86_64.so  uix86_64.so
```

The renderers have to lie next to the engine and the modules in the mod directory. The same three modules are
in `base/` as well: the engine looks there when the client plays as base, which is every server that is not
`japlus` and a start with `+set fs_game base`. Without them that ends with `VM_CreateLegacy on ui failed`.

A start without arguments runs in `japlus`: it is the default of `fs_game` in this engine (`JAPLUSGAME` in
`codemp/qcommon/q_shared.h`), for the client and for the dedicated server. So the menus, sabers and effects of
the port are there from the first picture on.

A config that is already in the folder is kept, so an edited `server.cfg` survives the next install. Not in the folder,
because the build does not make them: `libphonon.so` of Steam Audio for `v_spatial` (when the build did not find it, see
below), and on Windows `libwinpthread-1.dll` of mingw. On macOS the modules are installed into the app as before.

### The assets pk3

Every build also packs the assets of the port, `assets/japlus_client` (the files of the JA+ client plugin 1.4B4 with the
menus of the port, the JA+ sabers and effects, and the JA+ animations), into `<build>/assets/zz_japlus_client_assets.pk3`,
and `cmake --install` puts it into `JediAcademy/japlus/` (`-DJPAssetsDir=` names another mod directory). A file that was
changed, added or removed there is packed again by the next build. The pk3 is the same for every platform: the files are
kept byte for byte (`assets/.gitattributes`) and `assets/make_pk3.py` writes them in the order of
`assets/japlus_client.order`, which the player menu and the pure check depend on. Packing needs Python 3,
`-DBuildJPAssets=OFF` builds without it. A client needs this pk3 in `japlus/` next to the modules, and so does
the server: it reads the sabers and the animations from it.

The configs of the JA+ package are in `assets/configs` and are not packed: `cmake --install` puts them into the
same directory as loose files, and only where there is no file of that name yet, so a config an admin edited
stays. `server.cfg` is the example of the package with the passwords of the three admin ranks emptied (and no rcon
password, the server takes no rcon): the ones it came with can be looked up by everybody. Set your own before a server
runs with it.

## Windows

The Windows build in this repository is a cross build with mingw-w64 from Linux. It makes the client, the renderers and
the three modules (`jampgamex86_64.dll`, `cgamex86_64.dll`, `uix86_64.dll`). The toolchain file is
`cmake/Toolchains/x86_64-w64-mingw32.cmake`. The dedicated server is switched off for it:

```
cmake -S . -B buildwin64 -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/x86_64-w64-mingw32.cmake -DCMAKE_BUILD_TYPE=Release -DBuildMPDed=OFF
cmake --build buildwin64 -j"$(nproc)"
cmake --install buildwin64 --prefix release-win
```

The install of the Windows build directory gives the `.exe`, `SDL2.dll` and the `.dll` files in the same layout as
above. The libraries are the bundled ones, as `WIN32` turns the `UseInternal*` options on. The Windows client run is
described in [docs/japlus/windows.md](docs/japlus/windows.md).

`CMakeLists.txt` has flags for MSVC, but no file in this repository says that a build with it works or was tried.

## CMake options

Set with `-DName=value` on the configure line. The text is short, the full text is in `CMakeLists.txt`.

What is built:

| Option | Default | What it does |
|--------|---------|--------------|
| `BuildMPEngine` | ON | the client (`visionjk`) |
| `BuildMPRdVanilla` | ON | the default renderer, `rd-vanilla` |
| `BuildMPRdVulkanVision` | ON | the Vulkan renderer, `rd-vulkan-vision`. Needs the Vulkan headers and `glslc` |
| `BuildMPRdVisionGLES` | OFF | `rd-vulkan-vision` with its OpenGL ES 3 device as a library for the desktop. That device is what the client for the browser draws with |
| `BuildMPDed` | ON | the dedicated server (`visionjkded`) |
| `BuildMPGame` | ON | the server-side game code (`jampgamex86_64`) |
| `BuildMPCGame` | ON | the client-side game code (`cgamex86_64`) |
| `BuildMPUI` | ON | the UI code (`uix86_64`) |
| `BuildJPVoice` | ON | the cgame gets the voice chat (Opus and miniaudio from `lib/`), the game code gets the boombox (Opus) |
| `BuildJPAssets` | ON | packs the directories of `assets/` into pk3 files. Needs Python 3 |
| `BuildJPSpatialAudio` | ON | the client can use Steam Audio for its sound. Needs the submodule `lib/steamaudio`, the library is loaded at run time |
| `BuildMasterServer` | OFF | the masterserver (its code is private, see below) |
| `BuildTests` | OFF | the unit tests. Needs Boost |
| `BuildPortableVersion` | OFF | a portable version: does not read or write files from your user or home directory |
| `MakeApplicationBundles` | ON | macOS only: builds `.app` bundles for the engines |

Single player and Jedi Outcast, all OFF and not documented here: `BuildSPEngine`, `BuildSPGame`, `BuildSPRdVanilla`,
`BuildJK2SPEngine`, `BuildJK2SPGame`, `BuildJK2SPRdVanilla`.

Libraries:

| Option | Default | What it does |
|--------|---------|--------------|
| `UseInternalZlib` | OFF, ON on Windows | use the bundled zlib |
| `UseInternalPNG` | OFF, ON on Windows and macOS | use the bundled libpng |
| `UseInternalJPEG` | OFF, ON on Windows and macOS | use the bundled libjpeg |
| `UseInternalSDL2` | OFF, ON on Windows | use the bundled SDL2 |

Developer only, never for a release:

| Option | Default | What it does |
|--------|---------|--------------|
| `BuildDevHotpatch` | OFF | the engine and the dedicated server put patches of the game, cgame and ui modules into them while they run (`v_devHotpatch`, `tools/hotpatch.py`). Needs Linux x86_64 with GCC |
| `BuildDevIdsTest` | OFF | also the dedicated server `visionjkded-idstest`, which does on purpose what no server of the game does toward its clients, to run the intrusion detection of the client against it (`tools/test/ids-test.sh`). Not installed |
| `UseAddressSanitizer` | OFF | the runtime address sanitizer |
| `UseUndefinedSanitizer` | OFF | the runtime Undefined Behavior sanitizer |

## Steam Audio

The client can use Steam Audio for its sound (`v_spatial`, [docs/japlus/audio.md](docs/japlus/audio.md)). The engine is
not linked against it: the header comes from the submodule and the library is loaded at run time. Get the submodule once
after a clone:

```
git submodule update --init
```

Without `lib/steamaudio` checked out the client is built without `v_spatial`, and the configure says so. With the
submodule, the build looks for the library of the system in `SteamAudioSDK`, a folder with the content of the release
zip of Steam Audio (default `lib/steamaudio-sdk`). When the folder does not have it and `FetchSteamAudio` is ON (the
default), the build downloads the release zip of Valve once (181 MB), checks its SHA256 and takes the one library out
of it. `SteamAudioURL` names another place for the zip, `file://` for one that is on the disk already. The library
(`libphonon.so`, `phonon.dll`, `libphonon.dylib`) is then installed next to the client. When there is no library, the
client is built with `v_spatial` anyway, and the library has to be put next to it by hand. Without the library at run
time there is no 3D sound. `-DBuildJPSpatialAudio=OFF` leaves Steam Audio out.

## The Vulkan renderer and the GPU echo

Both need only the headers of Vulkan at build time, the library is loaded at run time. The headers are looked for by
`cmake/Modules/VulkanHeaders.cmake`, `-DVulkanHeadersDir=<dir with vulkan/vulkan.h>` names them if they are not found.
The shaders are compiled with `glslc` (package shaderc).

The renderer `rd-vulkan-vision` is built by default. Without the headers, or without `glslc`, the configure gives a
warning, the renderer is not built and the rest is. `-DBuildMPRdVulkanVision=OFF` silences the warning. The classic
renderer stays, and the game switches to it by itself when the Vulkan one can not run.

The rays of the echo of Steam Audio can be traced on the graphics card (`v_spatialGpu`). That needs the same two things.
Without them the configure says "no Vulkan headers or no glslc, the CPU traces all the rays of the echo", and the CPU
does the work. In a browser the CPU does it as well.

## The masterserver

The code of the masterserver is not in this repository. The hosts of VisionJK check its private repository out at
`codemp/master` (git-ignored), then:

```
cmake -S . -B build64 -DCMAKE_BUILD_TYPE=Release -DBuildMasterServer=ON
cmake --build build64 -j"$(nproc)" --target visionjk-master.x86_64
```

This builds `visionjk-master.<arch>`. It is for the hosts of VisionJK itself, it is never part of a release, and it has
no install rule. How it works and runs is described in the private repository, not here. What it does for players is
in [docs/japlus/server.md](docs/japlus/server.md), the section "The masterserver".

## Unit tests

```
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Debug -DBuildTests=ON
cmake --build build-test -j"$(nproc)" --target UnitTests
ctest --test-dir build-test --output-on-failure
```

The target is `UnitTests` (the test `unittests` in CTest), and it needs Boost with the unit_test_framework. The tests
are in `tests/vjk/`. The test scripts of `tools/test/` (smoke test of the zips, sanitizers and valgrind, static
checks, the release flow) are described in [docs/japlus/testing.md](docs/japlus/testing.md).

## Releases

The release zips are made by `tools/build-game.sh` of the separate website repository (visionjk-website). The Linux
build runs in a Debian 12 container, which has glibc 2.36.
