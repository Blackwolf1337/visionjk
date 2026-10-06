# rd-vulkan-vision: the Vulkan renderer

A second renderer for the OpenJK client next to `rd-vanilla`. It draws with Vulkan 1.3, keeps the level and the
models in buffers on the graphics card and does in shaders what `rd-vanilla` computes per vertex on the CPU every
frame. It is not a JA+ feature and it does not touch the server, the protocol or the game modules.

State: work in progress on the branch `feature/rd-vulkan-vision`. It runs the menu, the levels, players, effects
and the HUD. What is missing is listed below. `rd-vanilla` stays the default, and the fallback when the library
does not load.

## Building and switching

```
cmake -S . -B build64 -DBuildMPRdVulkanVision=ON
cmake --build build64 -j$(nproc)
```

Needs the Vulkan headers and `glslc` (package `shaderc`) on the build machine. The Vulkan library is not linked:
SDL loads it for the window, the engine hands its `vkGetInstanceProcAddr` to the renderer, and the renderer asks
that for every function it calls (`vk_funcs.h`, pointers named `qvk...` as `qgl...` in `rd-vanilla`). The result
is `codemp/rd-vulkan-vision/rd-vulkan-vision_x86_64.so`, it has to lie next to the engine binary.

The Windows DLL is cross compiled with mingw-w64 like the rest, with the same option:

```
cmake -S . -B buildwin32cl -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/i686-w64-mingw32.cmake -DCMAKE_BUILD_TYPE=Release -DBuildMPRdVulkanVision=ON -DBuildMPDed=OFF -DBuildMPRend2=OFF -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF
cmake --build buildwin32cl -j$(nproc)
```

mingw has no Vulkan headers of its own. They are the same on every platform, so the build copies the two
header directories of the host (`vulkan`, `vk_video`) into the build directory for the cross compiler.
`-DVulkanHeadersDir=` names another place. The result is `rd-vulkan-vision_x86.dll` next to `openjk.x86.exe`.

In the game:

```
cl_renderer rd-vulkan-vision; vid_restart
```

and `cl_renderer rd-vanilla; vid_restart` to go back. A graphics card without Vulkan 1.3 or without descriptor
indexing ends in an error that names `rd-vanilla`.

| Cvar | Meaning |
|------|---------|
| `r_vkDebug 1` | validation layer and its messages in the console, if the layer is installed (latched) |
| `r_vkStatic 0` | cheat: nothing from the device buffers, every vertex through the CPU as in `rd-vanilla`. For telling a fault of the buffers from a fault of the shading |
| `gfxinfo` | device, memory, what the last frame was made of and the shaders whose batches were still built on the CPU |

The cvars of `rd-vanilla` keep their names and meaning (`r_gamma`, `r_textureMode`, `r_picmip`,
`r_DynamicGlow` and its parameters, `r_dynamiclight`, `r_drawfog`, `r_fastsky`, `r_surfaceSprites`, ...). The ones
that only make sense for OpenGL extensions are registered and ignored.

## How it is built

The renderer does not copy the parts of `rd-vanilla` that are not graphics code. Its CMake target compiles them
from `codemp/rd-vanilla/` with `RD_VULKAN_VISION` defined: the BSP and model loaders, the `.shader` parser,
Ghoul2 with bones, bolts and collision, skins, marks, the light grid, the scene with PVS and culling, the cvars
and the export table, the image code up to the upload, the sky geometry, the surface functions for sprites,
beams and the like, the weather particles and the surface sprites.

What that needs in `rd-vanilla`: `#ifndef RD_VULKAN_VISION` around the OpenGL-only functions of `tr_init.cpp`
and `tr_image.cpp`, one function in `tr_ghoul2.cpp` that hands out the bone matrices, the depth test of a flare
in `tr_surface.cpp`, and one include at the end of `tr_local.h`. Without the macro the files are byte for byte the same after the preprocessor, that was checked
with `g++ -E -P` against `master`. The target links with `-Wl,-z,defs` and without libGL, so an OpenGL call that
is left in a shared file fails the build.

`rd-vulkan-vision/vk_hooks.h` is what the shared files see in the place of OpenGL:

- the texture upload calls of `tr_image.cpp` go to the texture table (`vk_image.cpp`)
- drawing with `glBegin`/`glEnd`, client arrays, the matrix calls, `glDepthRange` and `glFog` go to `vk_gl.cpp`,
  which collects the vertices and draws them like everything else. That is how the sky box, the weather and the
  surface sprites work without a change to their files
- the rest (stencil setup of the overdraw counter, line width, display lists) is nothing

The engine got a window type for Vulkan (`GRAPHICS_API_VULKAN`, `SDL_WINDOW_VULKAN`) and three functions at the
end of `refimport_t`: the instance extensions the window needs, the surface, and the entry point of the Vulkan
library. The other renderers do not use them.

| File | Content |
|------|---------|
| `vk_main.cpp`, `vk_funcs.h` | the functions of Vulkan, instance, device, swapchain, start and shutdown, `gfxinfo` |
| `vk_memory.cpp` | device memory in 64 MB blocks with free lists, buffers |
| `vk_image.cpp` | the texture table, uploads batched in one command buffer, samplers, render targets |
| `vk_pipeline.cpp` | descriptor layouts, shader modules, pipelines |
| `vk_frame.cpp` | a frame: targets, gamma pass, copy to the window, buffers per frame, screenshots, video frames |
| `vk_shade.cpp` | a batch with a `.shader` script becomes draws: colors, tcMods, fog, dynamic lights |
| `vk_backend.cpp` | the render commands: pictures, views, entities, draw order |
| `vk_world.cpp` | the level in a device buffer, which batches can use it |
| `vk_model.cpp` | Ghoul2 and MD3 vertices in a device buffer, bones per frame |
| `vk_post.cpp` | dynamic glow, the copy of the frame for refraction, the depth test of flares |
| `vk_gl.cpp` | the immediate mode drawing of the shared files |
| `shaders/` | `stage.glsl` (one stage of a script on a vertex), `generic.vert`, `skinned.vert`, `generic.frag`, `post.vert`, `gamma.frag`, `blur.frag`, `copy.frag`. `glslc -mfmt=c` turns them into headers while building |

## What runs where

| | rd-vanilla | rd-vulkan-vision |
|---|-----------|------------------|
| Level faces, meshes, patches | copied into the tess arrays every frame | one device buffer, per frame only indexes |
| Ghoul2 models | every vertex moved by its bones on the CPU | vertices in a device buffer, bones per skeleton per frame, skinning in `skinned.vert` |
| MD3 with one frame | decoded and copied every frame | same device buffer, one identity bone |
| tcMod, tcGen, rgbGen, alphaGen | per vertex on the CPU | one matrix and two colors per draw, the rest in `stage.glsl` |
| rgbGen lightingDiffuse, alphaGen lightingSpecular and portal | per vertex on the CPU | vertex shader |
| deformVertexes wave, bulge, move | per vertex on the CPU | vertex shader, for a shader with one of them |
| Dynamic lights | per triangle on the CPU, one pass per light | per pixel in `generic.frag`, one pass per light |
| Global fog | fixed function fog | per pixel in `generic.frag` |
| Fog volumes | fog pass with coordinates per vertex on the CPU, color fade per vertex on the CPU | the same pass, coordinates and fade in `stage.glsl` from two vectors per draw |
| Textures | bound per stage | one table of all textures, indexed in the shader |
| State of a stage | set per stage | only the blend function needs a pipeline, depth, cull and offset are dynamic state |
| Gamma, overbright | ramp of the display or baked into textures | one pass over the finished frame |

Still on the CPU, through the tess arrays as in `rd-vanilla`: the sky, shaders with the other kinds of
`deformVertexes` (autosprite, text, normals) or with more than one of them,
`alphaGen blend` (terrain), vertex lit surfaces with more than one light style, models with gore on them, MD3 with several frames, all
effects the game hands over as polygons, sprites, beams and lines, marks, the weather, surface sprites, the HUD.

A frame on mp/ffa3, 1280x720, from `gfxinfo`: as a spectator at the first spawn point 4551 triangles come from
device buffers and 1144 vertices from the CPU. Following a bot in a fight about 4600 triangles from device
buffers and 1500 vertices from the CPU. Before the device buffers that scene had 8000 to 10000 vertices from the
CPU.

mp/ffa4, a level that lies in a fog volume, two views as a spectator: 4018 and 5271 triangles from device
buffers, 548 and 436 built on the CPU. While the fog pass still took its coordinates from the CPU it was 3458
and 4170 against 1108 and 1537. mp/ffa2, two views: 624 and 540 triangles from the CPU, 1701 and 1609 while
`alphaGen lightingSpecular` still was CPU work (one weapon model with such a stage had 1077 of them). What is
left in these views are the effects of the game (flames, smoke, light cones) and the HUD.

## Differences to rd-vanilla

- Patches are drawn with all the rows and columns they were subdivided into. `rd-vanilla` leaves some out with
  the distance (`r_lodCurveError`), so a patch far away is a little rounder here.
- The frame is cleared at its start. Where nothing of the level is drawn (looking out of the world, the camera
  inside a model) this renderer shows black, `rd-vanilla` shows what the last frame left there.
- Dynamic lights are evaluated per pixel with the same formula, not per triangle.
- The glow buffers have 8 bits per channel, `rd-vanilla` asks for 16.
- Refraction (the wave of force push, entities with `RF_DISTORTION`): `rd-vanilla` copies a square of the screen
  into a texture for each such entity, here the whole frame is copied and the draw is told where its square is.
  The picture is the same.
- Flares (the shine around a lamp): `rd-vanilla` reads the depth buffer under the flare while it draws, to see
  whether something covers it. Here that pixel is copied at the end of the view and read when the frame has
  run on the device, so a flare shows up and goes away two frames late. Its place and size are the same.
- Screenshots hold the frame after the gamma pass.
- A body that burns away (disruptor) swells along the line of the fire. `rd-vanilla` moves the vertices again
  before every stage of the shader, here all stages use what the first one has. This was not seen in the game.
- `r_showImages`: `rd-vanilla` sizes the tiles by the width of the window but draws them in the 640 x 480 of
  the menus, so most are outside of a large window. Here all of them are on the screen.
- Textures are not compressed (32 bit with mip levels). mp/ffa3 takes about 550 MB of device memory, 180 MB of
  that are the buffers and targets the renderer always has.

## Not done

Stencil and projection shadows (`cg_shadows 2` and `3`), the automap, `r_shownormals`, anti aliasing.

Surfaces with the shader `internal_distortion` are left out. `rd-vanilla` cuts them into the stencil buffer and
fills the hole with a stretched copy of the screen, but nothing in the game code or the data of the base game
and of JA+ asks for that shader.

The Windows DLL builds with mingw-w64 for 32 and 64 bit and has no import but the C runtime. It was never
loaded into a game. macOS was not tried, it would need MoltenVK.

Portals and mirrors have their clip plane (`gl_ClipDistance` from a plane in the transform) and the flipped
culling, but no level of the base game has one, so that was never seen. The same goes for `alphaGen portal`.

## Checked

2026-10-06, RTX 4070 SUPER, Linux, NVIDIA 615.71. Every run with the validation layer, no message in any of them.

Views are compared as pairs: one session, the camera placed as a spectator with `setviewpos`, a screenshot with
`rd-vanilla`, `cl_renderer rd-vulkan-vision; vid_restart`, a screenshot from the very same spot, and back.
`japlus_port/tools/rdshot.sh` runs such a script, `rdviews.py` writes one for any map from its spawn points,
`rddiff.py` counts the pixels that differ by more than 16 in a channel and writes both pictures and their
difference side by side.

| Scene | Pixels off by more than 16 |
|-------|----------------------------|
| mp/ffa3, six views (sky box, lightmaps, patches, models) | 0.1 to 1.2 % |
| mp/ffa3 with `r_DynamicGlow 1` in both | the same |
| mp/ffa2, five views (fog volumes, items with a specular stage) | 0.0 to 0.2 % |
| mp/ffa4, five views (the level lies in a fog volume, lamps with flares) | 0.0 to 0.3 % |
| mp/ctf2, three views (global fog, snow) | 0.1 to 0.5 % |
| mp/ctf3, four views (grass sprites) | 0.1 to 1.3 % |
| the player from the front with HUD | 0.2 to 0.4 % |

All 23 levels of `maps/mp`, two views each (`tools/rdsmoke.sh`): no crash, no error, no message of the
validation layer, 0.1 to 3.1 % of the pixels off, one view of mp/ffa5 with 10.5 % (a column of flowing lava).
Three of those levels have a sky portal. Five levels one after the other and a `vid_restart` in one session:
the device memory in use goes up and down with the level (400 to 570 MB) and is the same when a level comes
again.

A fight, frame by frame: the first 30 seconds of that demo recorded to AVI by both renderers
(`tools/clienttests/rd_benchvideo.cfg`) and compared with `tools/rdvideodiff.py`. Every frame of such a file
is 40 ms of the demo, so both show the same moments. 30 frames out of 747: 2.2 to 3.8 % of the pixels off by
more than 16. What differs are the sparks (random numbers), the fine grating of the floor and the noise of
Motion JPEG. The file of `rd-vanilla` is three frames behind: it reads the frame buffer before the frame the
video command belongs to is drawn, and what is in the buffer then is that old on this machine.

Recording to AVI (`video` while a demo plays): a demo recorded on mp/ffa3, written as Motion JPEG and as raw
frames (`cl_aviMotionJpeg 0`). Both files hold the same picture for a frame, upright and with the right
colors, and the file `rd-vanilla` writes from the same demo differs from it in 0.6 % of the pixels.

What is left are animated shaders (the two pictures are seconds apart), the snow and the swaying grass, the
patches, and texture filter noise. Also seen without a number: main menu and console, a fight of bots with
blades, glow, marks and the light of the blades on the walls, the wave of a force push next to the one of
`rd-vanilla`, `vid_restart` between the two renderers in both
directions (13 renderer starts in one session).

Speed: a demo of 90 seconds, 17 bots fighting on mp/duel1, seen by a spectator from a fixed place, played as a
`timedemo` (1729 frames, the same ones for both renderers), 1280x720, three runs each, validation layer off.

| | frames a second | a frame |
|---|---|---|
| `rd-vanilla` | 362, 355, 357 | 2.8 ms |
| `rd-vulkan-vision` | 648, 646, 655 | 1.5 ms |

With `r_skipBackEnd 1` both play the demo at the 1000 frames a second the engine allows (997 and 996): the
game, the cgame and the front end of the renderer need less than a millisecond for a frame together, and they
are the same code for both. So the difference is all back end: the one of `rd-vulkan-vision` takes 1.2 ms less
for a frame of this scene. How long each back end takes by itself can not be read from this, only that it is
more than 1.8 ms in `rd-vanilla` and more than 0.6 ms here.

A frame of that demo has about 2000 draws, 33000 to 56000 triangles from device buffers and 8000 to 15000 built
on the CPU: saber marks, sparks, saber glow, and models with gore on them. Before `deformVertexes` was in the
vertex shader it was 11000 to 27000, and the timedemo ran at 659, 635 and 611 frames a second: the shells
around a player (shield, lightning, ysalamiri) are the whole model drawn once more with such a shader.

In a view without a crowd both renderers reach the 1000 frames a second the engine allows (a frame lasts at
least 1 ms, in a `timedemo` too), so there is nothing to measure there. See [threading.md](threading.md) for why
neither renderer is the limit at 60 or 125 frames a second on this machine. `tools/clienttests/rd_benchrec.cfg`
records such a demo, `rd_bench.cfg` plays it.

Not checked: other graphics cards and drivers, Windows at run time, macOS, a long game session, play by a person.
