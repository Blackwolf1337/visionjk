# Testing the Vulkan renderer on a Mac

The game runs on macOS with the old OpenGL renderer (`rd-vanilla`). The new renderer, `rd-vulkan-vision`, runs
on Vulkan, which macOS only has through MoltenVK. It was changed for that without a Mac to try it on, so this
test is the first time it runs there. It takes about fifteen minutes.

What is wanted from it: does the renderer start, and if not, the log that says where it stopped.

## 1. Get the code and the tools

```
git checkout feature/macos
git pull
brew install cmake ninja sdl2-compat vulkan-headers shaderc molten-vk
```

## 2. Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF -DBuildMPRend2=OFF -DBuildMPRdVulkanVision=ON
cmake --build build
```

Run both lines even if the `build` folder is already there from last time. `-DBuildMPRdVulkanVision=ON` is the
part that is new.

Then check that the two new files are in the app:

```
ls build/openjk.arm64.app/Contents/MacOS/rd-vulkan-vision_arm64.dylib
ls build/openjk.arm64.app/Contents/Frameworks/libMoltenVK.dylib
```

If one of them is missing, or cmake printed a warning about `libMoltenVK.dylib`, stop here and send the whole
output of the two cmake commands.

## 3. Start the game with the new renderer

```
open build/openjk.arm64.app --args +set cl_renderer rd-vulkan-vision +set logfile 2
```

The setting is stored. If the game does not start any more, this brings the old renderer back:

```
open build/openjk.arm64.app --args +set cl_renderer rd-vanilla
```

## 4. If it starts, look at these

1. The main menu: is it there, are the texts and the background right?
2. Join a server (or `devmap mp/ffa3` in the console, Shift+Esc opens it).
3. In the level: walls, floor, sky, your own player model, the HUD. Anything black, white, missing or flickering?
4. Type `gfxinfo` in the console once. Its output goes into the log.
5. Type `screenshot` at two or three spots. If something looks wrong, take one there.
6. Switch back and forth once: `cl_renderer rd-vanilla; vid_restart`, then
   `cl_renderer rd-vulkan-vision; vid_restart`. Does the picture come back each time?

Known and not a fault: on a Retina display the picture of the new renderer is not as sharp as with `rd-vanilla`.

## 5. What to send back

Whether it worked or not:

- every `qconsole.log`, found with `find ~/Library/Application\ Support/OpenJK -name qconsole.log`
- `crashlog-*.txt` from `~/Library/Application Support/OpenJK/`, if the game crashed
- the screenshots, from the `screenshots` folder next to the log
- which Mac and which macOS (Apple menu, About This Mac)
- for each point of step 4: fine, or what was wrong

If it did not start at all, the log is what matters most. It says how far the start came.

## 6. If there are five more minutes: the voice chat

Only on a server that has the voice chat switched on.

1. For the menus of it, copy `build/assets/zz_japlus_client_assets.pk3` into
   `~/Library/Application Support/OpenJK/japlus/`.
2. In the game: Controls, JA+ Control, set a key for "Voice Chat (Talk)".
3. Hold the key. macOS asks for the microphone the first time, allow it.
4. Type `voice_status` in the console. Send what it says, and whether another player heard you.
