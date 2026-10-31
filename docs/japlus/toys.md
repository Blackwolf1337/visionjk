# Toys

Not a JA+ feature. `codemp/cgame/toys/cg_toys.c`, `codemp/game/toys/bg_toys.h`, `codemp/game/toys/g_toys.c`, `codemp/cgame/toys/cg_spray.c`, `codemp/cgame/toys/cg_emote.c`, `tools/toys/sprays.py`, `tools/toys/sounds.py`,
`MS_Toy()` in `codemp/master/ms_relay.c`, `tools/toys/textures.py`, issue #9, 2026-10-31.

## For players

The first toy is the snowball. The second is the water gun, the third is the RC car, the fourth is the spray and the fifth is the interactive emoji, all described below.

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

To spray a picture, use `v_spray` (Controls, Extras, "Spray"). It puts the picture of `v_sprayImage` on the wall or floor
you look at, up to 128 units away. The default is `heart`. Any emoji of the chat will do: write its name without the
colons, such as `thumbsup` or `fire`. The list is `gfx/vision/sprays.txt`.

The picture stands upright (on a floor the top points away from you), is 48 units across and is as bright as the light
there. It is cut off where the surface ends. A player has one spray, and a new one takes the place of the old one. It
stays for the level. You can spray once every 2 seconds at most, with the sound of a burst of air. Animated sprays and
pictures of your own are not there yet.

To send an emoji, use `v_emote` (Controls, Extras, "Send Emoji"). It sends the emoji of `v_emoteImage` (default
`laughing`) to the player under your crosshair: the visible player nearest to it, up to 2048 units away. You can also
name the emoji after the command, such as `v_emote thumbsup`. The names are the ones of the chat, without the colons.
You can send one emoji a second at most.

The emoji flies in an arc from your eyes to the head of the other player and follows them, at about 700 units a second
(0.35 to 2.2 s in all). Then it pops above the head: it grows, bobs, 10 small copies of it burst out, and it fades after
1.8 s. A soft whoosh plays when it leaves and a pop with a little chime when it arrives. For the player who gets it, in
first person the emoji pops on their screen (top centre) and the one flying at them fades near their eyes. In third
person the pop over their own head is drawn smaller.

An emoji with a mood also lets the voice of your model say a line, if that voice has one. Laughing ones say a gloat (or
a victory or taunt line). Thumbs up, clap, trophy and fire say a victory line, angry ones an anger line, thinking ones
a confused line, open mouth and skull a gasp, and tongue, wink and kappa a taunt. Emoji of your own, made on the
masterserver with a preview, are not there yet.

Only players of VisionJK see the toys. Others see nothing. It works on any server: on a VisionJK server through the game
module, on any other through the masterserver. `v_toys 0` hides the toys of others.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `+v_snowball` | | hold to wind up, let go to throw |
| `+v_water` | | hold to spray water |
| `v_rccar` | | puts the RC car down, or away again |
| `v_spray` | | sprays the picture on the wall you look at |
| `v_sprayImage` | heart | the emoji of the chat that is sprayed, without the colons |
| `v_emote` | | sends an emoji to the player under the crosshair, `v_emote <name>` sends that one |
| `v_emoteImage` | laughing | the emoji of the chat that `v_emote` sends, without the colons |
| `v_toys` | 1 | 0: the toys and emoji of other players are not drawn |

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

A spray is not a throw either. `TOY_SPRAY` carries the point, the pitch and the yaw of the surface (for a floor or
ceiling the yaw of the player instead, for the top of the picture) and the line number of the picture in
`gfx/vision/sprays.txt`, all in the fields of the speed. `bg_toys.c` checks that the angles and the number are in
range (`TOY_SPRAY_MOST`, 1400). `tools/toys/sprays.py` writes `sprays.txt` and `shaders/vision_sprays.shader`, which
has a shader `gfx/vision/spray/<number>` for each picture, with `polygonOffset`. A picture added to `gfx/emoji` moves
the numbers after it, which only matters between different versions.

The own spray is said again every 45 seconds, so a player who came later sees it too. The spray of a player who left
goes. It is drawn as a temporary mark each frame. The master and the game module have to know `TOY_SPRAY` (0.2.9-dev.27
and later).

An emoji is not a throw either. `TOY_EMOJI` (6) carries the point of the eyes of the sender, and the fields of the speed
carry the client number of the target, the line number of the picture in `gfx/vision/sprays.txt` and 0. `bg_toys.c`
checks that the client is below `MAX_CLIENTS`, the picture below `TOY_SPRAY_MOST` and the third value is 0. Every cgame
that hears of it lets the emoji fly on its own and follow the head of the target, so nothing about it is on the server
but the relay. The master and the game module have to know `TOY_EMOJI` (0.2.9-dev.28 and later). `tools/toys/sprays.py`
now also writes a sprite shader `gfx/vision/emote/<number>` for each picture (without `polygonOffset`), and the list of
pictures moved into `cg_toys.c`, shared with the sprays. The sounds are made by `tools/toys/sounds.py` and use no audio of
anybody else. The voice lines come from `CG_CharacterSound` in `cg_players.c`, which reads them through the new field
`soundDir` of `clientInfo_t`, for lines the game did not load for the gametype.

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

For the spray, a spray on the floor and on a wall (upright, not mirrored, cut off at the edge of the wall, dim in a dark
room). Two clients: the other client received the `TOY_SPRAY` server command with the right picture. There is a unit
test for the `TOY_SPRAY` ranges.

For the emoji, two clients on a local server, and both received the emoji (the server command `vs_toy` with kind 6). The
flight, the pop, the burst, the 2D pop in first person and the fading near the eyes were seen. There is a unit test
`emoji` in `tests/vjk/toys.cpp`.

Not run: emoji in a real game with people, hearing the sounds (the test used dummy audio), emoji on Windows, sprays in a real game with people, the RC car in a real game with people, a real game with people (snowball and water gun), the water gun with the OpenGL renderer, Windows, a server without the game module of VisionJK in a real game.
