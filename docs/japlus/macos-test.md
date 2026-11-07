# Testing the game on a Mac

The game on macOS draws with `rd-vulkan-vision`, on Vulkan, which macOS only has through MoltenVK. The app
brings MoltenVK along. The renderer was changed for that, and the build was checked on a Mac on 2026-11-07, but
the game itself has not run there with this renderer yet. This test is that first run. It takes about fifteen
minutes.

What is wanted from it: does the game start and draw, and if not, the log that says where it stopped.

## 1. Get the code and the tools

```
git checkout feature/macos
git pull
git submodule update --init
brew install cmake ninja python sdl2-compat sdl3 vulkan-headers shaderc molten-vk
```

## 2. Build and install

```
cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac
cmake --install build-mac --prefix release-mac
```

The last lines of the install say `Signed ad hoc: .../visionjk.arm64.app`. Then check that the app has what it
needs:

```
ls release-mac/JediAcademy/visionjk.arm64.app/Contents/MacOS/rd-vulkan-vision_arm64.dylib
ls release-mac/JediAcademy/visionjk.arm64.app/Contents/Frameworks/libMoltenVK.dylib
```

If one of them is missing, or cmake printed a warning about `libMoltenVK.dylib`, or the install ended with an
error, stop here and send the whole output of the three cmake commands.

## 3. The game data

Copy `assets0.pk3` to `assets3.pk3` of Jedi Academy into `release-mac/JediAcademy/base/`, or into
`~/Library/Application Support/VisionJK/base/`.

## 4. Start the game

```
open release-mac/JediAcademy/visionjk.arm64.app --args +set logfile 2
```

## 5. If it starts, look at these

1. The main menu: is it there, are the texts and the background right, does the pointer follow the mouse?
2. Join a server (or `devmap mp/ffa3` in the console, Shift+Esc opens it).
3. In the level: walls, floor, sky, your own player model, the HUD. Anything black, white, missing or flickering?
4. Type `gfxinfo` in the console once. Its output goes into the log.
5. Type `screenshot` at two or three spots. If something looks wrong, take one there.
6. `vid_restart` once: does the picture come back?

Known and not a fault: on a Retina display the picture is not as sharp as it could be (it is drawn at the size
in points).

## 6. What to send back

Whether it worked or not:

- every `qconsole.log`, found with `find ~/Library/Application\ Support/VisionJK -name qconsole.log`
- `crashlog-*.txt` from `~/Library/Application Support/VisionJK/`, if the game crashed
- the screenshots, from the `screenshots` folder next to the log
- which Mac and which macOS (Apple menu, About This Mac)
- for each point of step 5: fine, or what was wrong

If it did not start at all, the log is what matters most. It says how far the start came.

## 7. If there are five more minutes: the voice chat

Only on a server that has the voice chat switched on (`vs_voice 1`).

1. In the game: Controls, set a key for "Voice Chat (Talk)".
2. Hold the key. macOS asks for the microphone the first time, allow it.
3. Type `v_voice_status` in the console. Send what it says, and whether another player heard you.
