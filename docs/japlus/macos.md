# macOS

The state of this tree on macOS. Read the first paragraph before anything else.

**Nothing here was ever built or run on a Mac.** There is none to do it on. What was done is everything that
could be done without one: the code was gone through for what macOS has differently, what was found was
changed, and the whole multiplayer tree was built with clang on Linux as a stand-in for the compiler of macOS.
That finds what clang refuses and gcc accepts. It does not find what only the headers, the libraries and the
linker of macOS would say. Expect a first build on a Mac to stop somewhere, and take the list at the end of
this file as the order to go through.

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
| Engine: window for Vulkan (`sdl_window.cpp`) | SDL makes it, but finds the Vulkan library only in the places the system searches | `WIN_VK_LoadLibraryMac`: also next to the program, in `Contents/Frameworks` of the bundle, in `/opt/homebrew/lib` and `/usr/local/lib` |
| Build files | the renderer is installed into the bundle like `rd-vanilla`, the linker flag that is for Linux only is set for Linux only | nothing needed |

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
- The texture table is 4096 textures with a sampler each in one descriptor array. Whether Metal takes that
  through MoltenVK is the largest open question. The renderer prints a line if the device says it takes fewer.

## Building on a Mac

Not tried. It should be close to this:

```
xcode-select --install
brew install cmake sdl2
brew install molten-vk vulkan-headers shaderc      # only for rd-vulkan-vision
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBuildMPRdVulkanVision=ON \
      -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF \
      -DBuildJK2SPEngine=OFF -DBuildJK2SPGame=OFF -DBuildJK2SPRdVanilla=OFF
cmake --build build -j
```

`-DVulkanHeadersDir=/opt/homebrew/include` if the headers are not found. Without `BuildMPRdVulkanVision` no
Vulkan, no MoltenVK and no `glslc` is needed. `-DBuildJPVoice=OFF` leaves the voice chat and its two libraries
out.

## Running

`cl_renderer rd-vulkan-vision; vid_restart`, as everywhere. If the window can not be made, the console says
`No Vulkan library (MoltenVK) found`. Then either copy `libMoltenVK.dylib` into `Contents/Frameworks` of the
app, or start with `SDL_VULKAN_LIBRARY=/path/to/libMoltenVK.dylib`.

The voice chat asks for the microphone the first time the talk key is pressed (the question comes from macOS,
with the sentence from the Info.plist). That only holds for the app built from this tree. Whether the Jedi
Academy app of the Mac App Store or of Steam loads this cgame at all was not looked at.

## For the first person with a Mac, in this order

1. Configure without the Vulkan renderer and without voice (`-DBuildJPVoice=OFF`). This is close to upstream.
2. Build. What stops here is most likely a header that macOS spells differently.
3. Start the game with `rd-vanilla`, load a level, join a server of this tree.
4. Build with the voice chat. Look at `cg_voice_audio.c` first if it does not compile: it is the one file
   whose macOS part (Core Audio through miniaudio) no compiler has seen.
5. `voice_devices`, then `+voice` on a server with `g_voice 1`: does macOS ask, does `voice_status` say
   connected, is a second player heard.
6. Build with `-DBuildMPRdVulkanVision=ON`.
7. `cl_renderer rd-vulkan-vision` with `r_vkDebug 1`, then `gfxinfo`. Lines to look for in the console:
   `Vulkan on top of another graphics interface` (the device is MoltenVK and the extension is on),
   `the device says it takes N textures in one table` (the open question above), and what the validation layer
   says if the Vulkan SDK is installed.
8. The test level for mirrors and portals (`devmap rdroom`, see renderer.md) and a normal level, next to
   `rd-vanilla`.

## Not done

- No build on macOS, no run, on neither kind of Mac.
- No cross compiler setup (osxcross) and no toolchain file for one.
- Nothing about signing and notarizing the app, and nothing about one app for both architectures.
- The window is not high resolution on a Retina display, with either renderer. That is as upstream.
- The single player parts of the tree were not looked at. This port is the multiplayer game.
