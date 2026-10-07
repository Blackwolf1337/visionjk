# rd-vulkan-vision: the Vulkan renderer

A second renderer for the OpenJK client next to `rd-vanilla`. It draws with Vulkan 1.3, keeps the level and the
models in buffers on the graphics card and does in shaders what `rd-vanilla` computes per vertex on the CPU every
frame. It is not a JA+ feature and it does not touch the server, the protocol or the game modules.

Since 2026-10-07 it is the renderer a client starts with (on macOS that is still `rd-vanilla`). `rd-vanilla` is
the fallback: where the library is missing or the machine can not run it, the game goes on with `rd-vanilla` by
itself, see "Building and switching". The renderer is in `master`
since 2026-10-07 (it was written on the branch `feature/rd-vulkan-vision`), behind the CMake option
`BuildMPRdVulkanVision`, which is on by default since 2026-10-07: every build of the client makes it next to
`rd-vanilla`. rend2, the third renderer of OpenJK, was removed from the tree that day: nothing used it.

## State

As of 2026-10-07, after `rd-vanilla` was gone through against this renderer once more (every render command,
every cvar, entity flag, view flag, stage field and shader field its two back end files read, the functions of
the shared files that are left out here, the empty functions of this renderer): everything the base game and
JA+ draw is drawn. What is open is what was never run, and a few cvars.

| Part | State |
|------|-------|
| Menus, console, HUD, fonts, the picture of the game at the start | done |
| Levels: faces, patches, lightmaps and light styles, sky box, sky portal, global fog and fog volumes | done |
| Models: Ghoul2 with bones and bolts, MD3, skins, saber marks on models | done |
| Effects: sprites, beams, lines, saber glow, marks on walls, weather, surface sprites | done |
| Shader scripts: all color, alpha and texture coordinate generators, tcMods, deforms, blend and depth states, animated maps | done |
| Dynamic lights, also in the glow pass | done |
| Dynamic glow | done |
| Refraction: the wave of force push, the cloak, the trail of `cg_saberTrail 2` | done |
| Shadows: `cg_shadows 1`, `2` and `3` | done |
| Flares | done, two frames late (see the differences) |
| Gamma and overbright | done |
| Anti aliasing (`r_ext_multisample`) | done |
| Compressed textures, texture filter, anisotropic filter, picmip | done |
| Screenshots, recording to AVI | done |
| Automap | done |
| `r_showtris`, `r_shownormals`, `r_showImages`, `r_vkStatic`, `gfxinfo`, `imagelist` | done |
| Window: resized from outside, `r_swapInterval`, a window nobody sees, `vid_restart` between the renderers | done |
| Windows DLL, 64 bit | builds, never loaded into a game |
| Portals and mirrors, `alphaGen portal` | done, in a test room: no level of the base game has one (see "Checked") |
| Videos: a `.roq` on the screen, `videoMap` in a shader | done |
| Fullscreen, also at a size that is not the one of the desktop | done |
| `r_displayRefresh` in fullscreen | asked for as for an OpenGL window. No effect could be shown here, this desktop offers one refresh rate |
| A minimized window | written, not tried: the desktop of this machine (Hyprland) has no such state |
| macOS | not tried, would need MoltenVK |
| Other graphics cards and drivers, a long session, play by a person over a longer time | not tried |
| `r_dlightStyle 0`, `r_colorbits`, `r_depthbits`, `r_stencilbits`, `r_texturebits 16`, the developer cvars | not there and left out on purpose, see "Checked" |

The details of the open rows are under "Not done", what was compared and how under "Checked".

## Building and switching

```
cmake -S . -B build64
cmake --build build64 -j$(nproc)
```

Needs the Vulkan headers and `glslc` (package `shaderc`) on the build machine. Without one of them cmake says
so and builds the tree without the renderer, `-DBuildMPRdVulkanVision=OFF` leaves it out on purpose. The Vulkan library is not linked:
SDL loads it for the window, the engine hands its `vkGetInstanceProcAddr` to the renderer, and the renderer asks
that for every function it calls (`vk_funcs.h`, pointers named `qvk...` as `qgl...` in `rd-vanilla`). The result
is `codemp/rd-vulkan-vision/rd-vulkan-vision_x86_64.so`, it has to lie next to the engine binary.

The Windows DLL is cross compiled with mingw-w64 like the rest:

```
cmake -S . -B buildwin64 -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/x86_64-w64-mingw32.cmake -DCMAKE_BUILD_TYPE=Release -DBuildMPDed=OFF -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF
cmake --build buildwin64 -j$(nproc)
```

mingw has no Vulkan headers of its own. They are the same on every platform, so the build copies the two
header directories of the host (`vulkan`, `vk_video`) into the build directory for the cross compiler.
`-DVulkanHeadersDir=` names another place. The result is `rd-vulkan-vision_x86_64.dll` next to
`openjk.x86_64.exe`. Windows is built for 64 bit only, the configure line no longer needs
`-DCMAKE_SIZEOF_VOID_P=8`.

A client that has no renderer stored in its config starts with `rd-vulkan-vision`. A config that was written
before 2026-10-07 has `cl_renderer "rd-vanilla"` in it and keeps that: the stored value wins over the default.
In the game:

```
cl_renderer rd-vulkan-vision; vid_restart
```

and `cl_renderer rd-vanilla; vid_restart` to go back.

A machine that can not run it gets `rd-vanilla` without anybody doing anything: no library of Vulkan, no driver
for it, Vulkan older than 1.3, no graphics device with descriptor indexing, or the renderer library is not
there. The console says which (`rd-vulkan-vision can not run here: ...`). `cl_renderer` is not changed by
that, so the next start and every `vid_restart` try again, for example after a new driver. How it is done:
`VK_InitVulkan()` returns instead of ending the game, the renderer hands an empty `glconfig` back, and
`CL_InitRenderer()` of the client loads `rd-vanilla` for the rest of the run. No exception crosses the library
border for it (the Windows builds link libgcc statically).

What that does not catch: a driver that crashes inside Vulkan instead of refusing. Then
`+set cl_renderer rd-vanilla` on the command line is the way out.

The fallback was built for Linux and not run: no start without Vulkan was tried, neither by taking the library
away nor on a real machine. Windows was not built with it.

| Cvar | Meaning |
|------|---------|
| `r_vkDebug 1` | validation layer and its messages in the console, if the layer is installed (latched) |
| `r_ext_multisample 2`, `4`, `8` | anti aliasing with that many samples a pixel, the cvar `rd-vanilla` uses for it (latched) |
| `r_ext_compress_textures 0` | textures as 32 bit and not packed into blocks, the cvar `rd-vanilla` uses for it (latched, the default is 1) |
| `r_vkStatic 0` | cheat: nothing from the device buffers, every vertex through the CPU as in `rd-vanilla`. For telling a fault of the buffers from a fault of the shading |
| `r_vkMerge 0` | cheat: a batch ends with its entity as in `rd-vanilla`, the sprites of an effect are a draw each again |
| `gfxinfo` | device, memory, how many textures are compressed and how long packing them took, what the last frame was made of (draws, triangles, transforms, bones), the shaders whose batches were still built on the CPU, how many swapchains the window has had and how many frames found no image to be shown in, how many of the 1024 Ghoul2 instances of the engine are taken and how many of those hold no model (with `developer 1` a line for each model that is held; the same list is printed when the engine stops with "Out of ghoul2 info slots"). The count has to be 0 in the menu after leaving a server, apart from the models the menu shows itself |

The cvars of `rd-vanilla` keep their names and meaning (`r_gamma`, `r_textureMode`, `r_picmip`,
`r_DynamicGlow` and its parameters, `r_dynamiclight`, `r_drawfog`, `r_fastsky`, `r_surfaceSprites`, ...). The ones
that only make sense for OpenGL extensions are registered and ignored.

## How it is built

The renderer does not copy the parts of `rd-vanilla` that are not graphics code. Its CMake target compiles them
from `codemp/rd-vanilla/` with `RD_VULKAN_VISION` defined: the BSP and model loaders, the `.shader` parser,
Ghoul2 with bones, bolts and collision, skins, marks, the light grid, the scene with PVS and culling, the cvars
and the export table, the image code up to the upload, the sky geometry, the surface functions for sprites,
beams and the like, the weather particles, the surface sprites and the shadows (`cg_shadows 2` and `3`).

What that needs in `rd-vanilla`: `#ifndef RD_VULKAN_VISION` around the OpenGL-only functions of `tr_init.cpp`
and `tr_image.cpp` and around the two functions of `tr_shadows.cpp` that are not about shadows, two functions in
`tr_ghoul2.cpp` (the bone matrices, the vertices of saber marks), a call where the data of a saber mark is freed
(`G2_gore.cpp`, `G2_misc.cpp`), the depth test of a flare in `tr_surface.cpp`,
an early way out for triangles a mark does not reach in `tr_marks.cpp` (see the speed steps below),
and one include at the end of `tr_local.h`. Without the macro the files are byte for byte the same after the preprocessor, that was checked
with `g++ -E -P` against `master`. The target links with `-Wl,-z,defs` and without libGL, so an OpenGL call that
is left in a shared file fails the build.

`rd-vulkan-vision/vk_hooks.h` is what the shared files see in the place of OpenGL:

- the texture upload calls of `tr_image.cpp` go to the texture table (`vk_image.cpp`)
- drawing with `glBegin`/`glEnd`, client arrays, the matrix calls, `glDepthRange` and `glFog` go to `vk_gl.cpp`,
  which collects the vertices and draws them like everything else. That is how the sky box, the weather and the
  surface sprites work without a change to their files
- the automap of `tr_world.cpp` (`r_autoMap`) is drawn the same way, one `glBegin` per surface of the level,
  collected into a few draws
- the stencil calls of the shadows go there too. The volume of a model comes as one `glBegin` to `glEnd` per
  edge: while the color mask is off they are collected and become one draw, with the stencil test as dynamic
  state
- the rest (line width, display lists) is nothing

The engine got a window type for Vulkan (`GRAPHICS_API_VULKAN`, `SDL_WINDOW_VULKAN`) and three functions at the
end of `refimport_t`: the instance extensions the window needs, the surface, and the entry point of the Vulkan
library. The other renderers do not use them.

| File | Content |
|------|---------|
| `vk_main.cpp`, `vk_funcs.h` | the functions of Vulkan, instance, device, swapchain, start and shutdown, `gfxinfo` |
| `vk_memory.cpp` | device memory in 64 MB blocks with free lists, buffers |
| `vk_image.cpp` | the texture table, uploads batched in one command buffer, samplers, render targets |
| `vk_bc.cpp` | textures packed into BC1 and BC3 blocks, the worker threads that do it |
| `vk_pipeline.cpp` | descriptor layouts, shader modules, pipelines |
| `vk_frame.cpp` | a frame: targets, gamma pass, copy to the window, buffers per frame, screenshots, video frames |
| `vk_shade.cpp` | a batch with a `.shader` script becomes draws: colors, tcMods, fog, dynamic lights |
| `vk_backend.cpp` | the render commands: pictures, views, entities, draw order |
| `vk_world.cpp` | the level in a device buffer, which batches can use it |
| `vk_model.cpp` | Ghoul2 and MD3 vertices in a device buffer, the saber marks on models, bones per frame |
| `vk_post.cpp` | dynamic glow, the copy of the frame for refraction, the depth test of flares |
| `vk_gl.cpp` | the immediate mode drawing of the shared files |
| `shaders/` | `stage.glsl` (one stage of a script on a vertex), `generic.vert`, `skinned.vert`, `generic.frag`, `post.vert`, `gamma.frag`, `blur.frag`, `copy.frag`. `glslc -mfmt=c` turns them into headers while building |

## What runs where

| | rd-vanilla | rd-vulkan-vision |
|---|-----------|------------------|
| Level faces, meshes, patches | copied into the tess arrays every frame | one device buffer, per frame only indexes |
| Ghoul2 models | every vertex moved by its bones on the CPU | vertices and triangles in device buffers, bones per skeleton per frame, skinning in `skinned.vert` |
| MD3 with one frame | decoded and copied every frame | same device buffer, one identity bone |
| Saber marks on models | their vertices taken from the surface every frame, after the CPU moved it | the vertices of a mark in a device buffer of the marks, with the bones they have in the surface, moved by `skinned.vert` |
| tcMod, tcGen, rgbGen, alphaGen | per vertex on the CPU | one matrix and two colors per draw, the rest in `stage.glsl` |
| rgbGen lightingDiffuse, alphaGen lightingSpecular and portal | per vertex on the CPU | vertex shader |
| deformVertexes wave, bulge, move | per vertex on the CPU | vertex shader, for a shader with one of them |
| Dynamic lights | per triangle on the CPU, one pass per light | per pixel in `generic.frag`, one pass per light |
| Global fog | fixed function fog | per pixel in `generic.frag` |
| Fog volumes | fog pass with coordinates per vertex on the CPU, color fade per vertex on the CPU | the same pass, coordinates and fade in `stage.glsl` from two vectors per draw |
| Textures | bound per stage | one table of all textures, indexed in the shader |
| State of a stage | set per stage | only the blend function needs a pipeline, depth, cull and offset are dynamic state |
| A draw | arrays set up and `glDrawElements` per stage | one indexed draw per stage. Which draw it is comes as its instance number, the vertex streams of the frame are bound once and a batch names its first vertex, model triangles are runs in an index buffer on the device |
| Effects (sparks, glows, lines: one entity for each) | a batch and a draw for every entity, unless the shader says `entityMergable` | one batch for all entities in a row that have the same shader, where the shader takes nothing from its entity but the vertices |
| Gamma, overbright | ramp of the display or baked into textures | one pass over the finished frame |
| Anti aliasing | a window with several samples a pixel | the scene and its glow go into targets with several samples, each pass ends by resolving them into the plain targets the other passes read |

Still on the CPU, through the tess arrays as in `rd-vanilla`: the sky, shaders with the other kinds of
`deformVertexes` (autosprite, text, normals) or with more than one of them,
`alphaGen blend` (terrain), vertex lit surfaces with more than one light style, MD3 with several frames, the
saber marks on models while they fade (the last tenth of their time, the color of every vertex changes then),
all effects the game hands over as polygons, sprites, beams and lines, marks on walls, the weather,
surface sprites, the HUD.

MD3 with several frames stay there on purpose. Of the 604 MD3 files of the base game and of JA+ 42 have more
than one frame. 19 of those are the hand models of the weapons, which have tags and not a single vertex (the
weapon in the hand is a model with one frame). Most of the others are objects of single player levels. What is
left for a multiplayer game are the flags (151 vertices each), the shell of force push (20) and an explosion
(29). A vertex program and buffers of their own for blending two frames would not pay for those.

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
- The glow buffers have 8 bits per channel. `rd-vanilla` makes its glow textures with 16, but fills them with
  copies of the frame buffer of the window, which has 8, so there is no more in them.
- Refraction (the wave of force push, entities with `RF_DISTORTION`): `rd-vanilla` copies a square of the screen
  into a texture for each such entity, here the whole frame is copied and the draw is told where its square is.
  The picture is the same.
- The other kind of refraction, the shader `internal_distortion`: the cgame asks for it by its number, 2, for a
  cloaked player (with `cg_renderToTextureFX 1` he is drawn with nothing else), for his holstered saber and for
  the trail of `cg_saberTrail 2`. `rd-vanilla` draws such a surface into the stencil buffer only and fills that
  shape with a stretched copy of a part of the screen when the view is done (`RB_DistortionFill`). Here the
  same is done with the same numbers (`vk_backend.cpp`), from the copy of the whole frame. Until 2026-10-07
  these surfaces were left out and a cloaked player was not drawn at all: the shader had been searched for by
  name only. The copy of the frame is read without its alpha channel, as a texture `rd-vanilla` copies from a
  frame buffer without one.
- Flares (the shine around a lamp): `rd-vanilla` reads the depth buffer under the flare while it draws, to see
  whether something covers it. Here that pixel is copied at the end of the view and read when the frame has
  run on the device, so a flare shows up and goes away two frames late. Its place and size are the same.
- Screenshots hold the frame after the gamma pass.
- A body that burns away (disruptor) swells along the line of the fire. `rd-vanilla` moves the vertices again
  before every stage of the shader, here all stages use what the first one has. A kill with the disruptor was seen in the game on 2026-10-07 and
  looks right. It was not put next to `rd-vanilla`.
- `r_showImages`: `rd-vanilla` sizes the tiles by the width of the window but draws them in the 640 x 480 of
  the menus, so most are outside of a large window. Here all of them are on the screen.
- `r_shownormals`: the lines are made from the vertices of a batch, so while it is set nothing comes from the
  device buffers, as with `r_vkStatic 0`.
- Anti aliasing costs memory for three more targets with that many samples a pixel: about 44 MB at 1280x720
  with four samples, about 400 MB at 3840x2160.
- Compressed textures (`r_ext_compress_textures 1`, the default of the engine): `rd-vanilla` names a compressed
  format and the OpenGL driver packs the texture. Vulkan takes the blocks as they are stored, so this renderer
  packs them itself (`vk_bc.cpp`): BC1 for a texture without alpha, BC3 for one with. `rd-vanilla` asks for the
  format with alpha for both unless `r_texturebits` is 16, the colors are stored the same way in the two, so a
  texture without alpha is half the size here. The pixels are not the ones the driver would make. Which images
  are compressed is decided by the code of `rd-vanilla`: not the lightmaps (unless `r_ext_compress_lightmaps`),
  not the images of a shader with `noTC`, not the ones the renderer makes itself. An image with less than 4
  pixels in a direction stays as it is. `imagelist` shows the format of each.
- Device memory: mp/ffa3 takes 282 MB with compressed textures and 581 MB without. About 200 MB of that are the
  buffers and targets the renderer always has at 1280x720.
- Packing is work at every level load: 96 million pixels for mp/ffa3. Up to seven worker threads share it with
  the main thread, which goes on when a texture is done: 0.47 s for that level on this machine, 1.5 s on one
  core. The workers do nothing else and sleep while the game runs.

## Not done

The Windows DLL builds with mingw-w64 for 64 bit and imports nothing but the C runtime, kernel32 and
libwinpthread. It was built on 2026-10-07 from the last state of the renderer, without a warning in the files
of the renderer (32 bit was built until then too and is gone from the tree). It was never loaded into a game. macOS was not tried, it would need
MoltenVK.

Not moved to the device, with the numbers of the fight demo. Sprites, lines and saber glow: building them is
6 % of the instructions of a frame for about 1360 entities. A vertex shader could make the four corners of a
sprite, but not the lines and glows, it would need a third program with pipelines of its own, and the step
before this one (the saber marks, 3 % of the instructions) came out as 2 % in the timedemo. `alphaGen blend`
and `rgbGen blend` are for generated terrain: no shader of the base game or of JA+ has them.

## Checked

2026-10-06, RTX 4070 SUPER, Linux, NVIDIA 615.71. Every run with the validation layer.

Until the evening of that day the messages of the layer that are errors never reached the log: the renderer
printed them with `PRINT_ERROR`, a level `CL_RefPrintf()` of the engine drops. Only warnings would have shown,
and there were none. With that fixed the layer named two faults, both fixed since. The fragment shaders need the
device feature `shaderDemoteToHelperInvocation` (glslc turns `discard` into it for Vulkan 1.3), which was not
switched on. And with `r_swapInterval 1` the semaphore a present waits for was one of the frame, where it has to
be one of the swapchain image. After that these runs were repeated and have no message: the pairs of mp/ffa3
with glow and of the player, the 23 levels, the fight demo (plain, with glow and four samples, with stencil
shadows, as a video), the shadows, the automap, `r_showtris`, `r_showImages` and the window tests below. `gfxinfo`
prints how many messages the layer had, that number is the thing to look at.

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
validation layer, 0.1 to 3.2 % of the pixels off, one view of mp/ffa5 with 10.6 % (a column of flowing lava).
From noon to the evening of that day the test profile had `r_dynamiclight 0` stored (see the speed section), so
the runs in between had no dynamic lights in either renderer. The last run of the 23 levels, the validation
series and a video of the fight demo by both renderers (2.2 to 3.8 % of the pixels of a frame off, the light of
the blades on the floor is the same) were made with them.
Three of those levels have a sky portal. Five levels one after the other and a `vid_restart` in one session:
the device memory in use goes up and down with the level (400 to 570 MB) and is the same when a level comes
again.

Compressed textures (2026-10-07). The packing was run outside of the game over 35 textures of the base game,
unpacked again by a decoder written from the description of the formats and compared with the original: 32 to
45 dB for the colors of walls, skins and weapons, 43 to 50 dB for the alpha of fonts and HUD pictures, a block
of one color is off by 1 of 255 at most. With the worker threads the numbers are the same. In the game, both
renderers with their compressed textures: the two views of mp/ffa3 0.1 and 0.3 %, the player from the front
with the HUD 0.1 %, no message of the validation layer, also not over the restarts of the renderer in between
(the workers are started and ended with it). The Windows DLL still builds and imports what it did. The 23
levels again, with compressed textures and the marks from the device: no crash, no error, no message of the
validation layer, 0.1 to 3.4 % of the pixels off and 10.3 % in the lava view of mp/ffa5, as before.

Saber marks on models from the device (2026-10-07). A build that also moved the vertices of every such mark on
the CPU and worked out what the vertex shader makes of the copy on the device played the fight demo: 78000
marks drawn, 2.85 million vertices, the positions differ by 0.0007 units at most (the weights of the bones have
16 bits on the device) and the normals not at all. That check is not in the code. The first 30 seconds of the
demo as a video by the build before and by this one: 0.1 to 1.6 % of the pixels of a frame differ, the sparks.
No message of the validation layer. The marks are gone from the list of shaders `gfxinfo` names as built on the
CPU. `rd-vanilla` is the same after the preprocessor in the three shared files this touches.

What an audit of `rd-vanilla` against this renderer found on 2026-10-07 (the three files that are not built
function by function, every cvar, entity flag, view flag and field they read, the guarded parts of the shared
files, the cvars the menus test). The cloak: the own player with the cloak on, from the front, by both
renderers from the same spot (`tools/clienttests/rd_cloak.cfg`): the same shape filled with the bent wall behind
it in both, also with four samples a pixel. The fight demo with `cg_saberTrail 2` as a video by both: 2.4 to
4.1 % of the pixels of a frame differ, as with the plain trail. `r_textureMode` was only in force until the
next `vid_restart`: `rd-vanilla` sets the filter at every start of the renderer, which was missing here. With
`GL_NEAREST` 17.9 % of the pixels of a view changed over a `vid_restart`, now 0.1 % as in `rd-vanilla`
(`rd_texmode.cfg`). `r_ext_texture_filter_anisotropic_avail` was never set, so the setup menus did not offer the
anisotropic filter. It is 16 on this card now. The picture of the game that `rd-vanilla` puts on the window as
soon as there is one (`R_Splash`, `menu/splash`) was missing, the window stayed black until the menu was drawn.
A screenshot asked for on the command line shows it now. No message of the validation layer in any of these
runs.

Portals and mirrors (2026-10-07). They have their clip plane (`gl_ClipDistance` from a plane in the transform)
and the flipped culling, but no level of the base game has one (all levels of single and multiplayer were
searched for a surface with such a shader), so there was nothing to look at. `tools/rdroom.py` writes a level
for it without a map compiler, the file lump by lump: two rooms that are boxes, in the first a mirror, a mirror
that fades with the distance (`textures/system/mirror_yavin`), a mirror in the floor whose stage reads the
alpha of `alphaGen portal`, a portal (`textures/system/portal_512`) that looks into the second room through a
`misc_portal_camera`, two pillars, and a row of panels with materials of the base game (glow, scrolling, alpha
test, color waves, rotation, an animated map) with a video above it. `devmap rdroom` loads it,
`tools/clienttests/rd_room.cfg` takes nine views as a player, whose own body is in the mirrors, by both
renderers from the same spot.

| View | Pixels off by more than 16 | With four samples a pixel and glow |
|------|----------------------------|-------------------------------------|
| the mirror from the front, with the player in it | 0.2 % | 0.3 % |
| the mirror from the side | 0.2 % | 0.2 % |
| the portal from the middle of the room | 1.2 % | 1.2 % |
| the portal from close by | 4.8 % | 5.4 % |
| the mirror that fades with the distance | 0.0 % | 0.0 % |
| the mirror in the floor, looking down | 1.4 % | 1.4 % |
| mirror and portal in one view (only the first one is looked through, by both) | 1.6 % | 1.6 % |
| the materials | 5.7 % | 5.7 % |
| the video on the wall (two moments of it) | 1.7 % | not run |

What is seen through a mirror or the portal lies on the same pixels in both: the best fit of the two pictures
is at no shift at all, and the mean color of the mirror and of the portal differs by 0.1 of 255. What differs
is the pose of the player (his idle animation, the pictures are seconds apart), the grain of a sand floor seen
at a flat angle through the portal (the 4.8 %), and the materials that move (the 5.7 %). No message of the
validation layer.

Two things that are not the renderers. The plain mirror of the base game, `textures/system/mirror1`, is a
script without a stage, and this engine makes its default shader of such a script, so that surface is a grey
tile with a white cross in both renderers. The room brings a mirror shader with a stage. And for a while after
`setviewpos` the game draws the own player see-through in some places, which ends with the next restart of the
renderer: the first picture of a pair had a see-through player and the second a solid one, 8 % of the pixels,
with either renderer alone too. The script restarts the renderer before the first picture as well.

Not brought over. For developers: `r_measureOverdraw`, `r_nobind`, `r_logFile`, `r_primitives` and `r_finish`
do nothing, `r_speeds 1` shows no megabytes of textures and `r_speeds 4` no vertices of lights. `r_dlightStyle 0`
(the lights of Quake 3, a texture projected on the triangles, where the default 1 is the light of this game)
changes nothing, the lights are always the ones of the default. That one was found on 2026-10-07, it is not
saved with the config and no menu sets it. Two that the video menu of the base game does set: `r_colorbits`
(color depth) and `r_texturebits` (texture quality, 16 or 32 bit). They change nothing here, and neither do
`r_depthbits` and `r_stencilbits`: 32, 24 and 8 bits always, which is what a card that runs this renderer has.
These are left out on purpose: nothing a player of today would miss, and the light of Quake 3 would need a
second way of lighting in the shaders for a cvar nobody sets.

Fullscreen (2026-10-07). Two views of mp/ffa3 by both renderers from the same spot
(`tools/clienttests/rd_fullscreen.cfg`). Under Wayland at the size of the desktop, 2560 x 1440, with the window
of this renderer on the whole screen: 0.1 and 0.3 % of the pixels off, one swapchain, no message of the
validation layer. Through X11 (Xwayland) with `r_mode 4`: the frame is 800 x 600 and is stretched into the
window, which is as large as the screen there, the pictures of both renderers are 800 x 600 and 0.1 and 0.3 %
apart. Into a window and back into fullscreen with a `vid_restart` each: no message. With `in_nograb 1` the
engine refuses fullscreen. It does that in the function a renderer calls once a frame for its window
(`WIN_Present`), which this renderer never called, so its window stayed in fullscreen where the one of
`rd-vanilla` did not. It is called now. `r_displayRefresh` was only applied to an OpenGL window. The window of
this renderer asks for the rate now too (`shared/sdl/sdl_window.cpp`), and the mode it got is printed. What
that does could not be seen: Wayland and Xwayland offer a mode with the rate of the desktop only, a request for
60 was answered with 144.

Videos (2026-10-07). Nobody had played one with this renderer, and the first one ended the game: the picture of
a video on the whole screen is a batch without a shader, and the function it was handed to reads the shader
(for the list in `gfxinfo` and for the normals). Fixed. `cinematic ja01.roq` from the main menu, two pictures of
it in a run with each renderer (`tools/clienttests/rd_cinematic.cfg`): the same picture the right way up and in
the right colors, 1.8 and 4.6 % of the pixels off because the two runs are a frame of the video apart. The same
video as a `videoMap` on a wall of the test room: the same picture in both, 1.7 % off (two moments of it).

The 23 levels once more with the build that has these three changes, before it went into `master`: no crash, no
error, no message of the validation layer, 0.1 to 3.4 % of the pixels off and 10.3 % in the lava view of
mp/ffa5, as every time before. The Windows client and renderer still build for 32 and 64 bit.

Dynamic lights in the glow pass: `rd-vanilla` runs its light pass in the glow pass too, so a surface whose shader
has a stage that glows gets the light on its texture added to the glow. This renderer left the lights out
there since its first day, without a reason that was written down. They are in since 2026-10-07. The fight demo
with `r_DynamicGlow 1` as a video does not show it in numbers: 2.3 to 3.0 % of the pixels of a frame differ from
`rd-vanilla` with and without the change, and the two builds differ from each other as two runs of one build
do (0.1 to 0.8 %, the sparks). No message of the validation layer.

Anti aliasing, `r_ext_multisample 4` in both renderers: mp/ffa3 0.2 and 0.4 %, with `r_DynamicGlow 1` 0.1 and
0.3 %, mp/ffa4 with its flares 0.1 and 0.1 %. Edges are smooth where `rd-vanilla` has them smooth. A fight with
glow and four samples was looked at as a picture. In the timedemo below four samples cost little: 673 and 673
frames a second against 707, 674 and 688 without, both measured while another test ran on the machine.

A fight, frame by frame: the first 30 seconds of that demo recorded to AVI by both renderers
(`tools/clienttests/rd_benchvideo.cfg`) and compared with `tools/rdvideodiff.py`. Every frame of such a file
is 40 ms of the demo, so both show the same moments. 75 frames out of 747: 2.1 to 3.7 % of the pixels off by
more than 16. What differs are the sparks (random numbers), the fine grating of the floor and the noise of
Motion JPEG. The file of `rd-vanilla` is three frames behind: it reads the frame buffer before the frame the
video command belongs to is drawn, and what is in the buffer then is that old on this machine.

Shadows: the player on a bright floor of mp/ctf2 from above, with `cg_shadows 1`, `2` and `3`, next to
`rd-vanilla`: the blob, the stencil shadow and the projected shadow have the same shape and place. The fight
demo recorded with `cg_shadows 2` by both renderers: 2.2 to 3.8 % as without. Stencil shadows take most of the
speed of both: the volumes are built on the CPU by the same code. The timedemo below runs at 165 and 165 frames
a second in `rd-vanilla` with them, at 193 and 200 here.

The automap (`r_autoMap 1` and `2`, a team game on mp/ffa3, `r_autoMapDisable 0`): filled and as lines it is the
same picture as in `rd-vanilla`.

Recording to AVI (`video` while a demo plays): a demo recorded on mp/ffa3, written as Motion JPEG and as raw
frames (`cl_aviMotionJpeg 0`). Both files hold the same picture for a frame, upright and with the right
colors, and the file `rd-vanilla` writes from the same demo differs from it in 0.6 % of the pixels.

The window. `r_mode -1` with a size the console can not draw in (`r_customwidth 0`, or less than 22 pixels) ended
the game with the fatal error "Con_CheckResize: Window too small to draw a console", with both renderers. The
engine now refuses such a size like any other mode that does not exist and falls back on mode 4 (800 x 600),
at the start and at a `vid_restart`. A window of 22 x 1 runs. The window resized from outside while a level is
drawn (900 x 500, 1600 x 900, 300 x 200 and back, under X11 through Xwayland, with and without
`r_swapInterval 1`): a new swapchain each time, the frame is stretched into it. Under Wayland the compositor
left the window of the game at its size. `r_swapInterval 1` in a window nobody sees: the images of the swapchain
come back late or not at all, under Wayland and under X11. A frame waits 50 ms for one at most, and after a
wait that ran out the frames of the next second do not wait at all and are shown when an image happens to be
free, so the game and its connection go on at their speed (`gfxinfo` counts the frames that were not shown).
Before that a frame waited without end, and under X11 the game stood still. Fullscreen is further down. A
minimized window was not tried, the desktop of this machine has no such state.

What is left are animated shaders (the two pictures are seconds apart), the snow and the swaying grass, the
patches, and texture filter noise. Also seen without a number: main menu and console, a fight of bots with
blades, glow, marks and the light of the blades on the walls, the wave of a force push next to the one of
`rd-vanilla`, `vid_restart` between the two renderers in both
directions (13 renderer starts in one session).

Speed: a demo of 90 seconds, 17 bots fighting on mp/duel1, seen by a spectator from a fixed place, played as a
`timedemo` (1729 frames, the same ones for both renderers), 1280x720, validation layer off, every run from the
defaults of the engine (dynamic lights on, `cg_shadows 1`, no glow), the renderers by turns in one profile.

| | frames a second | a frame |
|---|---|---|
| `rd-vanilla` | 335, 341, 339, 325 (median 337) | 3.0 ms |
| `rd-vulkan-vision` | 876, 884, 884, 891, 880, 879, 877, 866 (median 880) | 1.1 ms |

With `r_skipBackEnd 1` both play the demo at the 1000 frames a second the engine allows (997 and 996): the
game, the cgame and the front end of the renderer need less than a millisecond for a frame together. So the
difference is nearly all back end: `rd-vulkan-vision` takes 1.8 ms less for a frame of this scene. How long each
back end takes by itself can not be read from this, only that it is more than 1.9 ms in `rd-vanilla` and more
than 0.1 ms here.

Single runs scatter by a few percent and a whole series by as much from one hour to the next, so a step is
judged by two builds run by turns in the same profile, eight runs each, and their medians. An earlier version of
this section had numbers from two test profiles of which one had `r_dynamiclight 0` stored from a single
diagnostic run. That made `rd-vanilla` 6 % faster there and showed a gain for a step that was a loss (the
sixth row of the table below). The start script of the tests deletes the stored config before every run now.

A frame of that demo has 600 to 850 draws, 37000 to 65000 triangles from device buffers and 5200 to 5700 built
on the CPU. Those are all effects of the game: saber marks on walls, sparks, saber glow. How the numbers came
down, each step measured with the same demo:

| Step | Triangles from the CPU in a frame | Timedemo |
|------|-----------------------------------|----------|
| first measurement | 11000 to 27000 | 659, 635, 611 |
| `deformVertexes` in the vertex shader: the shells around a player (shield, lightning, ysalamiri) are the whole model drawn once more with such a shader | 8000 to 15000 | 648, 646, 655 |
| less work per batch and per draw (streams bound once, draw number as instance, index buffer for models) | the same | 689, 691, 673 |
| models with saber marks on them from the device, only the vertices of the marks on the CPU | 7100 to 8000 | 736, 707, 725 |
| effects in one batch: a fight has about 1360 entities in a frame that are one sprite or one line each, and each was a batch and a draw. Now they share a batch where the shader takes nothing from its entity. 2200 to 2400 draws become 600 to 850 | the same | 880 against 736 with `r_vkMerge 0` (medians, the build of the table above) |
| tried and taken out: only the first set of texture coordinates of a vertex written where no stage reads the others. Four tenths less to copy, but piece by piece into the memory of the device | the same | 803 against 848, a loss of 5 % |
| the normals of a vertex left out of the copy where no stage reads them | the same | 900 against 863 |
| marks: a triangle the planes of a mark leave nothing of is passed over before it is copied and chopped (`tr_marks.cpp`, for this renderer only). The game asks for the shadow mark of every player in every frame, and a floor that is a patch gave it 150 triangles a call | the same | 863 against 803 |
| saber marks on models from the device: the vertices of a mark go into a buffer once, with the bones they have in the surface, and are not moved on the CPU every frame any more (about 1500 vertices a frame) | 5200 to 5700 | 921 against 901, which is about as little as this measurement can tell apart |

The first four rows were measured without dynamic lights (the stored cvar named above), the others with the
defaults. The pairs of numbers in the last five rows are medians of eight runs by turns of two builds that
differ in that step alone.

Where a frame of that demo goes, counted in instructions with callgrind (`tools/rdprof.sh`, not a measure of
time), after the effects went into shared batches: 62 % are the cgame (36 % its player code, 12 % the saber
marks it puts on walls, 10 % the front end of the renderer it calls), 36 % the back end. Before that step the
back end had 45 % and half as many instructions again, before the three steps ahead of it 54 %. What it still
spends them on: the bones of the skeletons 7 %, the draw calls 6 %, building sprites and beams 5 %, the
vertices of saber marks on models 4 % (those have gone to the device since). The profile counts every byte of
a large copy as an instruction, so it says nothing about the copies.

### After the profile of 2026-10-07

The timedemo has reached its limit: in a `timedemo` the engine lets a frame last at least one millisecond
(`Com_Frame`, `minMsec = 1`), so nothing above 1000 frames a second can show, and the fight demo plays at 950 to
990 now. From here on steps are counted in instructions with callgrind over the same 1729 frames (not time, but
without that limit, and the same from run to run within about one percent).

Where the instructions of a frame of the fight demo went before these steps: 54 % the cgame (its player code 28 %,
of that the saber blades 13 %), 30 % the back end of the renderer, 10 % its front end. Three things in it were
work for nothing, and none of them was drawing:

| Step | Before | After |
|------|--------|-------|
| The effect list of the engine (`FX_GetValidEffect`, both renderers): the free slot with the lowest number was looked for from the first slot on for nearly every new effect, 1230 steps a search in a fight. A bit for every slot now | 8 % of all instructions | gone. Timedemo 949 against 920 (medians of eight runs by turns) |
| `G2_SetupModelPointers` runs in front of nearly every Ghoul2 call, 380 times a frame, and read the cvar `dedicated` by name and looked the model up by its file name each time. The cvar is asked for once, the model is looked up only when the handle no longer names a model of that file | 16.6 billion instructions for the frames, the cgame part 10.1 | 15.4 billion, the cgame part 8.9 |
| `FindShaderInShaderText`: a shader that is just an image is not in the table of the shader text, and for each one the whole text was walked over to find that out again, 1350 times in a level load | 3.6 billion instructions outside of the frames (loading) | 1.7 billion |

Each of the three gives what the old code gave. That was checked with builds that ran both ways: 320000 slot
searches, 800000 Ghoul2 calls and 2250 shader names over the fight demo, fights of bots and five level loads
with a `vid_restart`, no difference. The second and third are under `RD_VULKAN_VISION` in `G2_API.cpp` and
`tr_shader.cpp`, `rd-vanilla` is the same after the preprocessor. The first is in `codemp/client/FxUtil.cpp`.

Looked at and left, with the numbers:

- Sprites, lines and saber glow in a vertex shader: see "Not done". The three steps above are each larger.
- Only the bones a drawn surface names into the frame: 81 % of the bones that are uploaded are named by a
  surface that is drawn, so this would take a fifth off the 7 % the bones cost.
- Link time optimization (`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`): 16.0 instead of 16.6 billion instructions
  for the frames. All of it is the cgame (9.5 instead of 10.1) and the front end, where `DotProduct`, `VectorMA`
  and the like are calls into `q_math.c`. The back end is the same with and without. Not switched on: it changes
  how every module is compiled, the Windows ones that never ran included, for 4 %.

In a view without a crowd both renderers reach the 1000 frames a second the engine allows (a frame lasts at
least 1 ms, in a `timedemo` too), so there is nothing to measure there. See [threading.md](threading.md) for why
neither renderer is the limit at 60 or 125 frames a second on this machine. `tools/clienttests/rd_benchrec.cfg`
records such a demo, `rd_bench.cfg` plays it.

Not checked: other graphics cards and drivers, Windows at run time, macOS, a long game session, play by a person.
