# Toys

Not a JA+ feature. `codemp/cgame/toys/cg_toys.c`, `codemp/game/toys/bg_toys.h`, `codemp/game/toys/g_toys.c`,
`MS_Toy()` in `codemp/master/ms_relay.c`, `tools/toys/textures.py`, issue #9, 2026-10-31.

## For players

The first toy is the snowball. The second is the water gun, the third is the RC car, both described below.

To throw a snowball, hold the key (Controls, Extras, "Throw Snowball", or bind `+v_snowball`) and let go. The longer you hold, the further it flies: from 450 to 1150 units a second, full power after 0.8 s. You can
throw once every 0.3 s at most.

The ball bursts on what it hits: powder, chunks and the crunch of snow. It leaves a splat on walls and floors that
melts away like other marks. Whoever is hit gets snow on the screen for 3 seconds. It hurts nobody.

To spray water, hold `+v_water` (Controls, Extras, "Water Gun"). A jet of water leaves where you look, at 720 units a
second, a drop every 18 ms, and it falls with gravity. It splashes where it hits: drops, mist and the sound of water
steps. On a floor a puddle grows while the water comes (up to 36 units) and dries after 20 seconds, fading in the
second half. On a wall it leaves a wet patch (up to 18 units).

Who is hit in the face sees drops run down the view. These are the drops of rd-vulkan-vision when you come up out of
the water, and they need `v_fxWater`. The OpenGL renderer has no such effect. On rd-vulkan-vision the jet also bends
the picture behind it (`v_fxWater`). `v_toys 0` hides the water of others too.

To drive the car, use `v_rccar` (Controls, Extras, "RC Car"). It puts the mouse droid of the game in front of you. W and S
or the arrow keys drive (420 units a second forwards at most, 200 backwards), A and D steer, Space hops. The mouse turns
the camera around the car, and the right mouse button puts the camera behind it again. Esc or `v_rccar` puts the car
away. The camera follows the car.

While you drive, your player stands still and shows the dots of talking, so on a JA+ server nobody can hurt you. The car
rolls up steps and slopes, slides along walls, falls and lands, and stops at players. The keys are the keys W, A, S, D
and the arrows, whatever they are bound to.

Only players of VisionJK see the toys. Others see nothing. It works on any server: on a VisionJK server through the game
module, on any other through the masterserver. `v_toys 0` hides the toys of others.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `+v_snowball` | | hold to wind up, let go to throw |
| `+v_water` | | hold to spray water |
| `v_rccar` | | puts the RC car down, or away again |
| `v_toys` | 1 | 0: the toys of other players are not drawn |

## How it works

There is no entity on the server. A throw is a point and a speed (`bg_toys.h`, 19 bytes: the kind, three int32 for the
point, three int16 for the speed). Every cgame that hears of a throw lets the ball fly on its own: gravity 800, a
trace each frame against the level and the players, and the thrower is skipped for the first 0.15 s. So where the ball
lands can differ a little from one player to the next.

On a VisionJK server the throw goes through the game module. The cgame sends the client command `vs_toy`, and the
server sends the server command `vs_toy` to the clients with `v_caps` bit 3. The server says it does this in
`vs_caps` bit 9. It takes a throw only from a living player who plays, from within 160 units of his eyes, and 4 a
second at most.

On any other server it goes through the masterserver (`VOICE_TOY`). The master hands a throw to the sessions of the
room that said `TOY_HELLO` or threw, 4 a second at most. It refuses broken or too fast throws. The cgame says hello
every 20 s.

The water gun is not a throw. The jet is drops that fly on their own, each drawn as a piece of water (`RT_LINE`) to
the drop before it, so the jet bends as it falls. The others get `TOY_WATER` (the same 19 bytes as a snowball: where
the water leaves the gun and how fast) every 260 ms while it sprays, and their cgame sprays from there for 340 ms, so
the jet of somebody else turns in steps. The limits of the relays (4 a second) count snowballs and water together.

Puddles and wet patches are temporary marks drawn anew every frame with their size and wetness (24 at once). The
textures come from `tools/toys/textures.py` (`waterstream`, `waterdrop`, `wet`, `puddle`, `puddleshine`). The shine is
masked to the puddle shape, so the added light stays inside it. The splash is `effects/vision/water_splash.efx`. The
masterserver and the game module have to know `TOY_WATER` (0.2.9-dev.25 and later).

The RC car is not a throw either. Your own cgame drives it with traces against the level and the players. The others
get `TOY_RC` (where it is, its speed over the ground in x and y, and its yaw in the z of the speed) six times a second
and let it roll on from there between two of them. `TOY_RC_GONE` puts it away, and a car not heard of for 1.5 s is gone.
The relays now take 10 toys a second from a player (`TOY_PER_SECOND`), so the master and the game module have to be
0.2.9-dev.26 or later. The motor is the loop of the mouse droid, and it beeps when it is put down. The level far from
the player can lack what the server does not send him (other players, doors).

The snowball is drawn from these files:

- the textures are drawn by `tools/toys/textures.py` (`snowball`, `snowpuff`, `snowsplat`);
- `shaders/vision_toys.shader` holds their shaders;
- `effects/vision/snowball_hit.efx` is the burst. It needs the efx flag `useAlpha` for alpha blended particles;
- the ball and the splat are tinted by the light grid of the level, so they are not white in the dark.

The marks of the game had a bug: every mark with `rgbGen vertex` was drawn black. `cgame/cg_marks.c` kept the colour as
0 to 1 and wrote it into a byte. It is fixed. The black burn marks of the game look the same, because their pictures
are black.

## Tested

A local server with two clients placed in front of each other. The ball of the thrower flew, the other client was hit
and got snow on its screen. A throw at the floor showed the burst and the splat.

For the water gun, one client sprayed the floor (jet, splash, and a puddle that stays after). Two clients on a local
server, where the thrower sprayed the other: the target received the `TOY_WATER` server commands, drew the jet and saw
the drops run down its view. There is a unit test for `TOY_WATER` in `tests/vjk/toys.cpp`.

The masterserver relay was tested with a UDP script: hello, a throw handed on, four of six in a second, a too fast
one refused. The unit tests are in `tests/vjk/toys.cpp`.

For the RC car, one client drove the car (forwards, a turn, a hop, against a wall, back to the player with Esc). Two
clients on a local server: the driver showed the dots of talking, the other client received the `TOY_RC` server
commands, and the car stopped at the other player. There is a unit test for `TOY_RC`.

Not run: the RC car in a real game with people, a real game with people (snowball and water gun), the water gun with the OpenGL renderer, Windows, a server without the game module of VisionJK in a real game.
