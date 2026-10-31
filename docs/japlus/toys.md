# Toys

Not a JA+ feature. `codemp/cgame/toys/cg_toys.c`, `codemp/game/toys/bg_toys.h`, `codemp/game/toys/g_toys.c`,
`MS_Toy()` in `codemp/master/ms_relay.c`, `tools/toys/textures.py`, issue #9, 2026-10-31.

## For players

The first toy is the snowball. Hold the key (Controls, Extras, "Throw Snowball", or bind `+v_snowball`) and let go to
throw. The longer you hold, the further it flies: from 450 to 1150 units a second, full power after 0.8 s. You can
throw once every 0.3 s at most.

The ball bursts on what it hits: powder, chunks and the crunch of snow. It leaves a splat on walls and floors that
melts away like other marks. Whoever is hit gets snow on the screen for 3 seconds. It hurts nobody.

Only players of VisionJK see it. Others see nothing. It works on any server: on a VisionJK server through the game
module, on any other through the masterserver. `v_toys 0` hides the toys of others.

| Cvar or command | Default | Meaning |
|-----------------|---------|---------|
| `+v_snowball` | | hold to wind up, let go to throw |
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

The look:

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

The masterserver relay was tested with a UDP script: hello, a throw handed on, four of six in a second, a too fast
one refused. The unit tests are in `tests/vjk/toys.cpp`.

Not run: a real game with people, Windows, a server without the game module of VisionJK in a real game.
