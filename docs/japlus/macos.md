# macOS

The state of this tree on macOS.

The multiplayer game was built and run on a Mac for the first time on 2026-10-07: a MacBook Pro with an Apple
M5 Max, macOS 26 (Tahoe), Xcode clang, SDL from Homebrew (sdl2-compat on top of SDL3). It was played on a JA+
server with `rd-vanilla`. Everything before that date was written without a Mac, see below. The voice chat and
the Vulkan renderer compile on the Mac, but were not tried there yet.

After that a tester reported that the game only runs with `rd-vanilla` on his Mac. How `rd-vulkan-vision` failed
was not reported. The section "The Vulkan renderer on MoltenVK" says what was changed for it on the same day,
on Linux and without a Mac: none of that was built or run on macOS.

## What was already there

OpenJK builds on macOS upstream, and this tree is OpenJK 260c59c2 with additions: the app bundle
(`MakeApplicationBundles`, `codemp/macosx/OpenJKInfo.plist`), `MACOS_X` in the shared defines, the names of the
architectures including `arm64` for Apple Silicon, the module names for each of them, fullscreen as a desktop
window. None of that was touched.

## What this tree adds, and what was done for macOS

| Part | macOS | Done |
|------|-------|------|
| JA+ port in game, cgame, ui | plain C, no call that macOS lacks (searched: `malloc.h`, `MSG_NOSIGNAL` used bare, Linux only calls, x86 intrinsics, assembler) | `bg_props.c` defined the bone structure of `mdx_format.h` a second time, which clang refuses: fixed. Without it neither game nor cgame would have compiled |
| Web admin panel (`g_webadmin.c`) | sockets and files through POSIX, `SO_NOSIGPIPE` is set where it exists | nothing needed |
| Voice chat, relay (`g_voice.c`, `bg_net.h`) | UDP through POSIX, random numbers from `/dev/urandom` | nothing needed |
| Voice chat, client (`cg_voice.c`) | pthread and `getaddrinfo` | nothing needed |
| Voice chat, sound (`cg_voice_audio.c`) | miniaudio has a Core Audio back end | `MA_NO_RUNTIME_LINKING` for Apple, the cgame links CoreFoundation, CoreAudio and AudioToolbox, and the Info.plist of the app says what the microphone is for (`NSMicrophoneUsageDescription`), without which macOS ends a program that opens it |
| Opus (`lib/opus`) | plain C | nothing needed |
| `rd-vulkan-vision` | macOS has no Vulkan. MoltenVK runs it on top of Metal | see below |
| Engine: window for Vulkan (`sdl_window.cpp`) | SDL makes it, but finds the Vulkan library only in the places the system searches | `WIN_VK_LoadLibraryMac`: first in `Contents/Frameworks` of the bundle and next to the program, then what SDL finds, then `/opt/homebrew/lib` and `/usr/local/lib`. The console says which one was loaded (`Vulkan library: ...`) |
| Build files | the renderer is installed into the bundle like `rd-vanilla` | see the next section for what the first build on a Mac found |

### The Vulkan renderer on MoltenVK

- The instance is made with `VK_KHR_portability_enumeration` and its flag where the loader offers the extension.
  Without that the loader of the Vulkan SDK does not list MoltenVK at all.
- The device gets `VK_KHR_portability_subset` switched on where it offers it, which is a must for such a device.
- What a device of that kind may lack, and what the renderer does about each:

| May be missing | Renderer |
|----------------|----------|
| triangle fans | not used, everything is triangle lists and line lists |
| a lod bias in samplers | not used |
| vertex attributes that reach past their stride | none do |
| an image view that reorders or fixes channels | used once: the copy of the frame is read with alpha as 1 (`vk_post.cpp`). MoltenVK has it from macOS 10.15 on |
| depth with 24 bits and stencil | Apple GPUs do not have it. The renderer already takes 32 bit float with stencil then |
| textures in BC blocks | optional, the renderer stores them as they are without it |
| wide lines, lines as fill mode | lines are 1 wide, fill mode lines is optional (`r_showtris`) |

- It needs Vulkan 1.3 with dynamic rendering and descriptor indexing, as on every platform. That means a
  MoltenVK that reports Vulkan 1.3 (its 1.3 releases and newer) and a macOS and a Mac that MoltenVK can use
  Metal argument buffers on. An older one ends in the error that names `rd-vanilla`.
- The texture table was 4096 textures with a sampler each in one descriptor array. A graphics card takes a
  million of those, MoltenVK passes on what Metal says, which is about a thousand samplers a set. So the table
  is two arrays now, on every platform: 4096 images, and next to them the samplers they are read with, at most
  32 (the renderer never had more that differ). A draw names the texture and, above bit 16, its sampler
  (`vk_image.cpp`, `generic.frag`). The console prints what the device takes:
  `Vulkan: the device takes N textures and M samplers in one table, the renderer has 4096 and 32`. Checked on
  Linux: the same pictures as before (a floor at a flat angle with trilinear and 16 times anisotropic
  filtering, old build against new build: 0.002 % of the pixels off by more than 16), the pair tests against
  `rd-vanilla` on mp/ffa3 and in the test room as before, no message of the validation layer.
- The app brings MoltenVK along: the build copies `libMoltenVK.dylib` (of Homebrew, or of the Vulkan SDK through
  `VULKAN_SDK`) into `Contents/Frameworks` and signs it with the rest, and the window code looks there first. A
  Vulkan loader that is installed somewhere on the Mac is only used if the app has none, or if
  `SDL_VULKAN_LIBRARY` names it: a loader may be installed without any driver, and then it finds no graphics
  device.

## What the first build on a Mac found, and what was changed

| Found | Changed |
|-------|---------|
| The link failed for every program and module: `ld: unknown options: -z -z`. `-Wl,-z,relro -Wl,-z,now` are flags of the linker of Linux, the one of macOS does not know them | `CMakeLists.txt`: set only when not `APPLE` |
| `cmake --build` made an app that could not start: "Failed to load renderer". The renderer and the game modules are only put into the app by `cmake --install` | `codemp/CMakeLists.txt`, target `MacBundle` (part of `ALL`): after every build the renderers go into `Contents/MacOS`, game, cgame and ui into `Contents/MacOS/base` |
| The app needed SDL from Homebrew at `/opt/homebrew`, it did not run on a Mac without it | `cmake/MacBundle.cmake`, run by `MacBundle`: the Homebrew libraries the engine links (SDL2 = sdl2-compat) are copied into `Contents/Frameworks`, the engine is pointed there with `install_name_tool`. sdl2-compat loads SDL3 from next to itself, so SDL3 goes beside it as `libSDL3.dylib` |
| `install_name_tool` breaks the signature the linker made, and macOS on Apple Silicon ends a program without a valid one | `MacBundle.cmake` signs the libraries, the modules and then the app ad hoc (`codesign --sign -`). `codesign --verify --deep --strict` passes. This also gives the app its own entry in the privacy settings, which the microphone of the voice chat needs |
| The deployment target was macOS 10.9, older than any Mac the libraries of today build for, the linker warned about it | `CMAKE_OSX_DEPLOYMENT_TARGET` is 11.0, the first macOS on Apple Silicon |
| The window was not high resolution on a Retina display: the game rendered at the size in points and macOS scaled it up, blurred | `shared/sdl/sdl_window.cpp`: the window is made with `SDL_WINDOW_ALLOW_HIGHDPI`, and after the OpenGL context is made the game takes the size of the drawable (`SDL_GL_GetDrawableSize`) as its resolution. The console says `High DPI drawable: W x H` when it differs. New cvar `r_highDpi` (default 1, archived, `vid_restart`): 0 renders at the size in points, a quarter of the pixels, for a slow graphics chip. Only for the OpenGL window, the Vulkan one is as it was |
| With the high resolution drawable the mouse would move at half speed: SDL counts the motion in points, the game in pixels | `shared/sdl/sdl_input.cpp`: the relative motion is scaled by drawable size / window size. The mouse is put back into the middle of the window in points, not in pixels |
| In a window the cursor left the game while looking around | The cause was the cvar `in_nograb 1` in the `openjk.cfg` of the `japlus` folder: it switches the capture of the mouse off, by design. The code is more robust now as well: while the mouse is captured, the relative mode and the grab of SDL are checked every frame and set again if SDL lost them (a change of focus, a first call that failed). In a window the cursor also stays inside while the console is down or a level loads, as long as the window has the focus. `in_nograb 1` still switches all of it off |
| Paste in the console and text fields was only Ctrl+V and Shift+Insert | Cmd+V pastes too (`sdl_input.cpp`, only `MACOS_X`) |
| A microphone that the user refused in the privacy settings opens on macOS without an error and gives silence. The player would talk and nobody would hear them, with no message | `cg_voice_audio.c`: `CG_VoiceAudioMicDenied()` asks AVFoundation (`AVCaptureDevice authorizationStatusForMediaType:`) through the Objective-C runtime, so the file stays C. `cg_voice.c` asks it before the microphone is opened and says where to switch it on. The cgame links `AVFoundation` and `objc` on macOS |

Trackpad scrolling in the menus and the keyboard layout needed nothing: SDL sums the small steps of a trackpad
into whole wheel steps, and text comes through `SDL_TEXTINPUT` in the layout of the system. Cmd+Q ends the game
through `SDL_QUIT`. A game controller works as on the other platforms through `in_joystick 1` (the joystick
interface of SDL, not its game controller mapping); that was not tried, there was none to hand.

## Building on a Mac

```
xcode-select --install
brew install cmake ninja sdl2-compat vulkan-headers shaderc molten-vk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF -DBuildMPRend2=OFF -DBuildMPRdVulkanVision=ON
cmake --build build
```

The result is `build/openjk.arm64.app`, ready to start: renderer, modules and SDL are inside, it is signed. The
game data (`assets0.pk3` to `assets3.pk3`) goes into `~/Library/Application Support/OpenJK/base`.

- `-DBuildJPVoice=OFF` leaves the voice chat and its two libraries out.
- `-DBuildMPRdVulkanVision=ON` builds the Vulkan renderer as well. A new build folder does that by itself on
  macOS when the Vulkan headers and `glslc` are installed, a build folder that was configured before keeps
  what it had, so name it. The build needs `vulkan-headers` and `shaderc`, and `molten-vk` for the library
  that goes into the app. cmake warns if it finds no `libMoltenVK.dylib` (`-DMacBundleMoltenVK=<file>` names
  one). Without the three packages leave the option out, the app then has `rd-vanilla` only.
- `-DBuildMPRend2=OFF`: rend2 was not tried.

The SDL of Homebrew is built for the macOS it was installed on (macOS 26 here). The app carries that SDL, so it
starts on the same macOS or newer, not on older ones, although the program itself is built for 11.0.

## Running

```
open build/openjk.arm64.app --args +connect <server>
```

`+set logfile 2` writes the console to `~/Library/Application Support/OpenJK/<mod>/qconsole.log`, flushed after
every line. A crash writes `crashlog-<date>.txt` one folder above.

- The mouse is not captured: look for `in_nograb` in the `openjk.cfg` of the mod folder (`japlus/`, not only
  `base/`), it has to be 0.
- `cl_renderer` names a renderer that is not in the app (an old config had `rd-metal`, which this tree does not
  have): the engine says so and falls back to `rd-vanilla`, nothing to do.
- After every build the ad hoc signature is new, and macOS may ask for the microphone again.

The Vulkan renderer is not the one the game starts with, `rd-vanilla` is. To start with it:

```
open build/openjk.arm64.app --args +set cl_renderer rd-vulkan-vision +set logfile 2
```

or in the console `cl_renderer rd-vulkan-vision; vid_restart`. The setting is stored. If the game does not
start with it any more, `--args +set cl_renderer rd-vanilla` goes back.

If it does not start, these lines of the console (or of `qconsole.log`) say how far it came:

| Line | Meaning |
|------|---------|
| `rd-vulkan-vision` is not loaded, the engine names `rd-vanilla` | the renderer is not in the app: it was built without `-DBuildMPRdVulkanVision=ON` |
| `No Vulkan library (MoltenVK) found` | no `libMoltenVK.dylib` in `Contents/Frameworks` of the app and none installed. `brew install molten-vk` and build again, or start with `SDL_VULKAN_LIBRARY=/path/to/libMoltenVK.dylib` |
| `Vulkan library: ...` | which library was loaded |
| `rd-vulkan-vision needs Vulkan 1.3, this system has ...` | the MoltenVK is too old |
| `found no graphics device with Vulkan 1.3 and descriptor indexing` | the library knows no device that can do it. With a loader: it has no driver (MoltenVK) registered |
| `Vulkan on top of another graphics interface` | the device is MoltenVK, as expected |
| `Vulkan: the device takes N textures and M samplers in one table` | what Metal offers. The renderer needs 4096 and 32 |
| `Vulkan: ... failed with ...` | a call of Vulkan failed, the line names it |

`r_vkDebug 1` needs the validation layer and with it the loader of the Vulkan SDK:
`SDL_VULKAN_LIBRARY=<sdk>/lib/libvulkan.1.dylib`. The window of the Vulkan renderer is not high resolution on a
Retina display yet (`r_highDpi` is for the OpenGL window).

The voice chat asks for the microphone the first time the talk key is pressed (the question comes from macOS,
with the sentence from the Info.plist). If it was refused, the console says where to switch it on. Whether the
Jedi Academy app of the Mac App Store or of Steam loads this cgame at all was not looked at.

## Still open, in this order

The steps for somebody who tests on a Mac, and what to send back, are in [macos-test.md](macos-test.md).

1. The voice chat on the Mac: `voice_devices`, then `+voice` on a server with `g_voice 1`. Does macOS ask, does
   `voice_status` say connected, is a second player heard.
2. Fullscreen (`r_fullscreen 1`): Cmd+Tab, Mission Control, the notch of the built in display. Fullscreen is a
   desktop window (upstream), the mouse is let go when the game loses the focus.
3. `r_highDpi` on the built in Retina display. The first run was on an external display at 1x, where drawable and
   window have the same size and nothing changes.
4. The Vulkan renderer with MoltenVK: a first start with what was changed after the report (MoltenVK in the app,
   the split texture table), and if it fails the lines of the table in "Running". Then `gfxinfo`, a level, the
   menus, a fight. High resolution for its window.

## Not done

- One app for both architectures (universal). The SDL of Homebrew is arm64 only, and the names of the modules
  carry the architecture (`rd-vanilla_arm64.dylib`), so one app would need both sets. An Intel Mac needs its own
  build there, with an SDL for Intel.
- Signing with a developer certificate and notarizing. The app is signed ad hoc, which is enough on the Mac
  that built it; on another Mac Gatekeeper asks (right click, Open).
- A game controller through the game controller interface of SDL.
- No cross compiler setup (osxcross).
- The single player parts of the tree were not looked at. This port is the multiplayer game.
