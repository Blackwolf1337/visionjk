# macOS

The state of this tree on macOS.

The multiplayer game was built and run on a Mac for the first time on 2026-10-07: a MacBook Pro with an Apple
M5 Max, macOS 26 (Tahoe), Xcode clang, SDL from Homebrew (sdl2-compat on top of SDL3). It was played on a JA+
server with `rd-vanilla`. That was the branch `feature/macos`, which then stood still while develop went on.

On 2026-11-07 (issue #1) develop was merged into the branch, and the tree was built, installed and checked again on
the same Mac, now with macOS 27, Xcode clang 21 and from Homebrew sdl2-compat 2.32.74, SDL3 3.4.20, MoltenVK 1.4.2,
vulkan-headers 1.4.363 and shaderc 2026.4. In between `rd-vanilla` was removed (2026-11-02), so on a Mac the game
runs only with `rd-vulkan-vision`, on Vulkan through MoltenVK, which the app brings along. There is no renderer of
its own for Metal and none is planned: MoltenVK is the way. The dedicated server ran a level with bots, and the
client ran without a window (SDL with its offscreen driver, MoltenVK with a surface that is not on the screen): the
menus, mp/ffa3 with a bot, `vid_restart`. See "The first start of the client". Nobody has played it in a window yet.

## What was already there

OpenJK builds on macOS upstream, and this tree is OpenJK 260c59c2 with additions: the app bundle
(`MakeApplicationBundles`, `codemp/macosx/VisionJKInfo.plist`), `MACOS_X` in the shared defines, the names of the
architectures including `arm64` for Apple Silicon, the module names for each of them, fullscreen as a desktop
window. None of that was touched.

## What this tree adds, and what was done for macOS

| Part | macOS | Done |
|------|-------|------|
| JA+ port in game, cgame, ui | plain C, no call that macOS lacks (searched: `malloc.h`, `MSG_NOSIGNAL` used bare, Linux only calls, x86 intrinsics, assembler) | `bg_props.c` defined the bone structure of `mdx_format.h` a second time, which clang refuses. Develop fixed it the same way in the meantime |
| Web admin panel (`g_webadmin.c`) | sockets and files through POSIX, `SO_NOSIGPIPE` is set where it exists | nothing needed |
| Voice chat, relay (`g_voice.c`, `bg_net.h`) | UDP through POSIX, random numbers from `/dev/urandom` | nothing needed |
| Voice chat, client (`voice/cg_voice.c`) | pthread and `getaddrinfo` | nothing needed |
| Voice chat, sound (`voice/cg_voice_audio.c`) | miniaudio has a Core Audio back end | `MA_NO_RUNTIME_LINKING` for Apple, the cgame links CoreFoundation, CoreAudio and AudioToolbox, and the Info.plist of the app says what the microphone is for (`NSMicrophoneUsageDescription`), without which macOS ends a program that opens it |
| Opus (`lib/opus`) | plain C | nothing needed |
| `rd-vulkan-vision` | macOS has no Vulkan. MoltenVK runs it on top of Metal | see below |
| Engine: window for Vulkan (`sdl_window.cpp`) | SDL makes it, but finds the Vulkan library only in the places the system searches | `WIN_VK_LoadLibraryMac`: first in `Contents/Frameworks` of the bundle and next to the program, then what SDL finds, then `/opt/homebrew/lib` and `/usr/local/lib`. The console says which one was loaded (`Vulkan library: ...`) |
| Spatial sound on the graphics card (`v_spatialGpu`) | needs ray queries (`VK_KHR_ray_query`), MoltenVK 1.4.2 has none | nothing: the CPU traces the rays of the echo, as in a browser |
| Build files | see the two tables of what the builds on a Mac found | |

### The Vulkan renderer on MoltenVK

- The instance is made with `VK_KHR_portability_enumeration` and its flag where the loader offers the extension.
  Without that the loader of the Vulkan SDK does not list MoltenVK at all. MoltenVK alone, which is what the app
  brings, does not need it.
- The device gets `VK_KHR_portability_subset` switched on where it offers it, which is a must for such a device.
  The console says `Vulkan on top of another graphics interface (VK_KHR_portability_subset)`.
- What a device of that kind may lack, and what the renderer does about each:

| May be missing | Renderer |
|----------------|----------|
| triangle fans | not drawn as such: `vk_gl.cpp` turns fans and polygons into triangle lists |
| a lod bias in samplers | not used |
| vertex attributes that reach past their stride | none do |
| an image view that reorders or fixes channels | used once: the copy of the frame is read with alpha as 1 (`vk_texture.cpp`). MoltenVK has it from macOS 10.15 on |
| depth with 24 bits and stencil | Apple GPUs do not have it. The renderer takes 32 bit float with stencil then |
| textures in BC blocks | optional, the renderer stores them as they are without it |
| wide lines, lines as fill mode | lines are 1 wide, fill mode lines is optional (`r_showtris`) |

- It needs Vulkan 1.3 with dynamic rendering and descriptor indexing, as on every platform. MoltenVK 1.4.2 on the
  M5 Max says: Vulkan 1.3 for the device, and every feature the renderer asks for (clip distance, dynamic
  rendering, demote to helper, a runtime array of textures with non uniform indexing, partially bound, update
  after bind). Read with a small program that makes no window, not by the game.
- The texture table is 4096 textures with a sampler each in one descriptor array. On 2026-10-07 MoltenVK passed on
  about a thousand samplers a set, and the branch split the table into images and samplers for it. MoltenVK
  1.4.2 says 500000 samplers and 1000000 images for a set that is updated after binding, so the split was not
  taken over into develop's renderer. The console prints what the device takes:
  `Vulkan: the device takes N textures and M samplers in one table, the renderer has 4096`, with a warning
  where that is less.
- The app brings MoltenVK along: the install copies `libMoltenVK.dylib` (of Homebrew, or of the Vulkan SDK through
  `VULKAN_SDK`) into `Contents/Frameworks` and signs it with the rest, and the window code looks there first. A
  Vulkan loader that is installed somewhere on the Mac is only used if the app has none, or if
  `SDL_VULKAN_LIBRARY` names it: a loader may be installed without any driver, and then it finds no graphics
  device. Without MoltenVK in the app and without a Vulkan installed, the game on a Mac has no renderer at all.

## What the first build on a Mac found, and what was changed (2026-10-07)

| Found | Changed |
|-------|---------|
| The link failed for every program and module: `ld: unknown options: -z -z`. `-Wl,-z,relro -Wl,-z,now` are flags of the linker of Linux, the one of macOS does not know them | `CMakeLists.txt`: set only when not `APPLE`. Develop still had them, they came with the merge of 2026-11-07 |
| `cmake --build` made an app that could not start: "Failed to load renderer". The renderer and the game modules are only put into the app by `cmake --install` | a target `MacBundle` that put everything into the app after every build. Since 2026-11-07 the install does it, see below |
| The app needed SDL from Homebrew at `/opt/homebrew`, it did not run on a Mac without it | `cmake/MacBundle.cmake`: the Homebrew libraries the engine links (SDL2 = sdl2-compat) are copied into `Contents/Frameworks`, the engine is pointed there with `install_name_tool`. sdl2-compat loads SDL3 from next to itself, so SDL3 goes beside it as `libSDL3.dylib` |
| `install_name_tool` breaks the signature the linker made, and macOS on Apple Silicon ends a program without a valid one | `MacBundle.cmake` signs the libraries, the modules and then the app ad hoc (`codesign --sign -`). This also gives the app its own entry in the privacy settings, which the microphone of the voice chat needs |
| The deployment target was macOS 10.9, older than any Mac the libraries of today build for, the linker warned about it | `CMAKE_OSX_DEPLOYMENT_TARGET` is 11.0, the first macOS on Apple Silicon |
| The window was not high resolution on a Retina display: the game rendered at the size in points and macOS scaled it up, blurred | `shared/sdl/sdl_window.cpp`: the OpenGL window is made with `SDL_WINDOW_ALLOW_HIGHDPI`, and the game takes the size of the drawable (`SDL_GL_GetDrawableSize`) as its resolution. The console says `High DPI drawable: W x H` when it differs. Cvar `v_highDpi` (`r_highDpi` until the merge, default 1, archived, `vid_restart`): 0 renders at the size in points. The multiplayer game has no OpenGL window since `rd-vanilla` is gone, so this is for the OpenGL ES library (`BuildMPRdVisionGLES`) only now, see "Still open" |
| With the high resolution drawable the mouse would move at half speed: SDL counts the motion in points, the game in pixels | `shared/sdl/sdl_input.cpp`: the relative motion is scaled by drawable size / window size. The mouse is put back into the middle of the window in points, not in pixels. Only on macOS |
| In a window the cursor left the game while looking around | The cause was the cvar `in_nograb 1` in the config of the `japlus` folder: it switches the capture of the mouse off, by design. The code is more robust now as well: while the mouse is captured, the relative mode and the grab of SDL are checked every frame and set again if SDL lost them. In a window the cursor also stays inside while the console is down or a level loads, as long as the window has the focus. `in_nograb 1` still switches all of it off. Compiled only on macOS |
| Paste in the console and text fields was only Ctrl+V and Shift+Insert | Cmd+V pastes too (`sdl_input.cpp`, only `MACOS_X`) |
| A microphone that the user refused in the privacy settings opens on macOS without an error and gives silence. The player would talk and nobody would hear them, with no message | `voice/cg_voice_audio.c`: `CG_VoiceAudioMicDenied()` asks AVFoundation (`AVCaptureDevice authorizationStatusForMediaType:`) through the Objective-C runtime, so the file stays C. `voice/cg_voice.c` asks it before the microphone is opened and says where to switch it on. The cgame links `AVFoundation` and `objc` on macOS |

Trackpad scrolling in the menus and the keyboard layout needed nothing: SDL sums the small steps of a trackpad
into whole wheel steps, and text comes through `SDL_TEXTINPUT` in the layout of the system. Cmd+Q ends the game
through `SDL_QUIT`.

## What the build of 2026-11-07 found, and what was changed (issue #1)

| Found | Changed |
|-------|---------|
| The merge of develop: 14 files in conflict, most of them in `rd-vulkan-vision`, which had become a shared part with a Vulkan and an OpenGL ES device since the branch | the renderer of develop, with the part for MoltenVK of the branch carried over (portability, the line of the texture table). The split texture table was left out, see above. `r_highDpi` became `v_highDpi`, the microphone check went to `voice/cg_voice.c` |
| `cmake --install` put game, cgame and ui into `Contents/MacOS/VisionJK` of the app, a folder the engine never looks in (it looks in the mod folder and `base`). The client only found its modules in `Contents/MacOS/base` | the modules, the pk3 and the configs are installed next to the app, into `JediAcademy/japlus` and `JediAcademy/base`, as on Linux. The client finds them there: `fs_basepath` of an app is the folder the app is in |
| The dedicated server of a macOS install had no game module: "VM_CreateLegacy on game failed". It is installed next to the app and does not look into it | the same: it finds `japlus/jampgamearm64.dylib` next to it now. Run with mp/ffa3 and `bot_minplayers 3`: the module loads, three bots come in, SIGTERM quits as on `quit` |
| A pk3 or a config in `Contents/MacOS` breaks the signature of the app: codesign takes every file there for code ("code object is not signed at all, in subcomponent ... chat.cfg") | none are in the app any more, the app has only code |
| The `fixup_bundle` of OpenJK changed the app in the build folder ahead of the copy, and the install then failed on "no LC_RPATH load command with path: /opt/homebrew/lib" after every build that linked the engine again | gone. `cmake/MacBundle.cmake` runs at the end of `cmake --install`, on the installed app, after every other install rule, and no longer as a step of the build: the install collects what the build made, as on Linux and Windows |
| sdl2-compat opens SDL3 when it runs and is not linked against it, so nothing that follows the links sees it. Out of the build folder the engine finds it through the path to Homebrew the linker wrote into it, the installed app has no such path and would stop at once with a dialog "Failed loading SDL3 library" | SDL3 goes beside it (as on the branch), only where the SDL2 is sdl2-compat. Without an SDL3 to copy the install stops |
| sdl2-compat of Homebrew brings a search path of its own, `@loader_path/../../../../opt/sdl3/lib`, which points out of the app | the search paths of the libraries copied into the app are taken out |
| | the signature is checked at the end of the install with `codesign --verify --deep --strict`, which passes. `otool -L` and `otool -l` of every program and library of the install name nothing outside the app and the system |
| `rd-vanilla` is gone, so MoltenVK in the app is a must now | the configure warns when it finds no `libMoltenVK.dylib` |
| Every JPEG of the game failed to load in the client: "Wrong JPEG library version: library is 90, caller expects 80" (392 times up to the first level), and the walls of a level were white. The Vulkan headers were found in the include directory of Homebrew, which went onto the include path as a whole, with the `jpeglib.h` of jpeg-turbo in it ahead of the one of `lib/jpeg-9a` the renderer is linked with | `cmake/Modules/VulkanHeaders.cmake`: on macOS the two directories of the headers are copied into the build folder, as for a cross build. The copies are of the directories themselves, not of the links Homebrew has to them |
| The libraries of Homebrew are built for the macOS they were installed on: SDL2 and SDL3 say macOS 27, MoltenVK 12.0, the program and the renderer 11.0 | nothing. Such an app starts on macOS 27 only. See "Not done" |

The branch was also built on Linux (Debian 12 on arm64, gcc 12) after the merge: everything builds.

## The first start of the client (2026-11-07)

The client was started from the install, without a window: `SDL_VIDEODRIVER=offscreen` (SDL3 under sdl2-compat
takes it), which makes the Vulkan window with `VK_EXT_headless_surface` of MoltenVK, and `SDL_AUDIODRIVER=dummy`.
The game data was `assets0.pk3` to `assets3.pk3` through `fs_cdpath`, the home folder a new one. A config ran the
steps (`+exec`) and the game took its own screenshots. The engine shows a dialog of macOS when it ends with an error,
which would take the focus: a small library put in with `DYLD_INSERT_LIBRARIES` turned it into a line on stderr for
the test. It was not needed, there was no error.

| | Result |
|---|---|
| Start | `Vulkan library: .../visionjk.arm64.app/Contents/Frameworks/libMoltenVK.dylib`, `Vulkan device: Apple M5 Max, Vulkan 1.3.357`, `Vulkan on top of another graphics interface (VK_KHR_portability_subset)`, `the device takes 1000000 textures and 500000 samplers in one table`. The offscreen driver has no display modes, so the game fell back to `r_mode 4` |
| The menus | the first page of the new look as on Linux, fonts and panels (picture: [macos-menu.jpg](../images/macos-menu.jpg)). The profile page in the level with the model icons of the game |
| A level | `devmap mp/ffa3`: game, cgame and ui load, the level is drawn with its textures, the HUD, the hilt in the hand, the message of the server, the items (picture: [macos-level.jpg](../images/macos-level.jpg)) |
| Joining, a bot | `team free`, `addbot alora 3` |
| `vid_restart` | the renderer starts again and draws the level, `swapchains made: 1, frames that were not shown: 0` |
| Voice chat | `Voice chat on UDP port 29071`, `Voice chat: on`. Nothing was sent or heard |
| End | `quit` ends the game, exit code 0 |

What it found: the JPEG of the table above, which made the level white in the first run. And one thing that is not
explained: the figure in the middle of the first page and of the profile page is empty, only the light under it is
there. The instance of the model is made (`ghoul2 instances: 8 of 1024 slots in use, 0 of them without a model`).
Whether it is empty on Linux as well with a new home folder (the model is `human_merc/key_carrier`) was not looked
at.

## Building on a Mac

```
xcode-select --install
brew install cmake ninja python sdl2-compat sdl3 vulkan-headers shaderc molten-vk
git submodule update --init
cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac
cmake --install build-mac --prefix release-mac
```

`release-mac/JediAcademy` is then a complete game folder, as on Linux:

```
JediAcademy/
  visionjk.arm64.app/Contents/
    MacOS/        visionjk.arm64  rd-vulkan-vision_arm64.dylib  libphonon.dylib
    Frameworks/   libSDL2-2.0.0.dylib  libSDL3.dylib  libMoltenVK.dylib
  visionjkded.arm64
  japlus/
    jampgamearm64.dylib  cgamearm64.dylib  uiarm64.dylib
    zz_japlus_client_assets.pk3
    server.cfg and the other configs of assets/configs
  base/
    jampgamearm64.dylib  cgamearm64.dylib  uiarm64.dylib
```

Its content goes next to the `base/` with `assets0.pk3` to `assets3.pk3` of the game, or the four pk3 go into
`~/Library/Application Support/VisionJK/base`, the home folder of the game on macOS.

- The build folder is not meant to be run from: the app there has no SDL, MoltenVK or modules in it.
- `libphonon.dylib` of Steam Audio is fetched as on Linux (`lib/osx/libphonon.dylib` of the release zip, for arm64
  and x86_64) and goes into the app next to the program.
- The install signs the app ad hoc, which is enough on the Mac that built it. On another Mac Gatekeeper asks
  (right click, Open), and macOS may ask for the microphone again after every build.

## Running

```
open release-mac/JediAcademy/visionjk.arm64.app --args +set logfile 2
```

`+set logfile 2` writes the console to `~/Library/Application Support/VisionJK/japlus/qconsole.log`, flushed after
every line. The dedicated server is started in the folder: `./visionjkded.arm64 +map mp/ffa3`.

If the game does not start, these lines of the console (or of `qconsole.log`) say how far it came:

| Line | Meaning |
|------|---------|
| `Vulkan library: ...` | which library was loaded. In an install it is `Contents/Frameworks/libMoltenVK.dylib` of the app |
| `No Vulkan library (MoltenVK) found` | none in the app and none installed. `brew install molten-vk` and install again, or start with `SDL_VULKAN_LIBRARY=/path/to/libMoltenVK.dylib` |
| `rd-vulkan-vision can not run here: it needs Vulkan 1.3, this system has ...` | the MoltenVK is too old |
| `rd-vulkan-vision can not run here: no graphics device with Vulkan 1.3 and descriptor indexing` | the library knows no device that can do it. With a loader: it has no driver (MoltenVK) registered |
| `Vulkan on top of another graphics interface` | the device is MoltenVK, as expected |
| `Vulkan: the device takes N textures and M samplers in one table` | what Metal offers. The renderer needs 4096 of each |
| `Vulkan: ... failed with ...` | a call of Vulkan failed, the line names it |

`v_vkDebug 1` needs the validation layer and with it the loader of the Vulkan SDK:
`SDL_VULKAN_LIBRARY=<sdk>/lib/libvulkan.1.dylib`.

The voice chat asks for the microphone the first time the talk key is pressed (the question comes from macOS,
with the sentence from the Info.plist). If it was refused, the console says where to switch it on.

## A cross build

A build for macOS on another system (osxcross on Linux) can be compared with the native build of 2026-11-07: the
file list above, `otool -L` of each file, and the lines of the install. What the build files expect from it:

- a toolchain file with `CMAKE_SYSTEM_NAME Darwin`, `CMAKE_OSX_ARCHITECTURES arm64` (an Intel build is a build of
  its own, see "Not done"), and `CMAKE_INSTALL_NAME_TOOL` set to the `install_name_tool` of the cross tools.
  `MacBundle.cmake` takes that one and the `otool` with the same prefix next to it.
- SDL2 for macOS (or sdl2-compat with SDL3) found by `find_package(SDL2 CONFIG)`, SDL3 and MoltenVK found by
  `find_library`, or named with `-DMacBundleSDL3=<file>` and `-DMacBundleMoltenVK=<file>`.
- `glslc` and Python 3 of the build machine: the shaders are SPIR-V and the pk3 is the same for every platform.
- `MacBundle.cmake` copies the libraries the engine links by the path `otool -L` names for them, and only those
  under `/opt/homebrew` and `/usr/local`. A library of a cross build has the path it has on a Mac (its install
  name), not where it lies on the build machine: that part of the script does not fit a cross build yet.
- There is no `codesign` on Linux. The install then leaves the app unsigned and says so: `No codesign: the
  installed app is not signed, sign it before it runs on Apple Silicon`. `rcodesign sign` (apple-codesign) signs
  ad hoc on Linux. A program for arm64 without a valid signature is ended by macOS at once.

## Still open, in this order

The steps for somebody who tests on a Mac, and what to send back, are in [macos-test.md](macos-test.md).

1. The client in a window, played by a person: the mouse and the keyboard, a fight, a server on the internet, the
   picture on the display. It ran once without a window, see above. The empty figure of the first page.
2. High resolution on a Retina display for the Vulkan window. It is made without `SDL_WINDOW_ALLOW_HIGHDPI`, so
   it renders at the size in points and macOS scales it up. `v_highDpi` is for the OpenGL window, which the
   multiplayer game no longer has. The mouse code above scales by drawable size / window size, which is 1 then.
3. The voice chat on the Mac: `v_voice_devices`, then talk on a server with `vs_voice 1`. Does macOS ask, does
   `v_voice_status` say connected, is a second player heard.
4. Fullscreen (`r_fullscreen 1`): Cmd+Tab, Mission Control, the notch of the built in display.
5. The capture of the mouse in a window (2026-10-07) with the pointer of the menus of the new look.

## Not done

- One app for both architectures (universal). The SDL of Homebrew is arm64 only, and the names of the modules
  carry the architecture (`rd-vulkan-vision_arm64.dylib`), so one app would need both sets. An Intel Mac needs its
  own build, with an SDL for Intel.
- SDL and MoltenVK built for macOS 11.0. The ones of Homebrew are built for the macOS they were installed on, and
  the app that carries them starts only there or later. A release needs its own builds of them.
- Signing with a developer certificate and notarizing. The app is signed ad hoc.
- A game controller through the game controller interface of SDL.
- The single player parts of the tree were not looked at. This port is the multiplayer game.
