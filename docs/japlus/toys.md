# Toys

Not a JA+ feature. `codemp/cgame/toys/cg_toys.c`, `codemp/game/toys/bg_toys.h`, `codemp/game/toys/g_toys.c`, `codemp/cgame/toys/cg_spray.c`, `codemp/cgame/toys/cg_emote.c`, `codemp/cgame/toys/cg_pong.c`, `codemp/cgame/toys/cg_board.c`, `tools/toys/sprays.py`, `tools/toys/pingpong.py`, `tools/toys/sounds.py`,
`tools/toys/textures.py`, issue #9, 2026-10-31.

## For players

The first toy is the snowball. The second is the water gun, the third is the RC car, the fourth is the spray, the fifth is the interactive emoji, the seventh is table tennis (with tennis on a court next to it) and the eighth is the drawing board, all described below.

To throw a snowball, hold the key (Controls, Extras, "Throw Snowball", or bind `+v_snowball`) and let go. The longer you hold, the further it flies: from 450 to 1150 units a second, full power after 0.8 s. You can
throw once every 0.3 s at most.

The ball bursts on what it hits: powder, chunks and the crunch of snow. It leaves a splat on walls and floors that
melts away like other marks. Whoever is hit gets snow on the screen for 3 seconds. It hurts nobody.

To spray water, hold `+v_water` (Controls, Extras, "Water Gun"). A jet of water leaves where you look, at 720 units a
second, a drop every 18 ms, and it falls with gravity. It splashes where it hits: drops, mist and the sound of water.
On a floor a puddle grows while the water comes (up to 36 units) and dries after 20 seconds, fading in the
second half. On a wall it leaves a wet patch (up to 18 units).

On rd-vulkan-vision the ground gets wet where the water lands: darker, a little shiny, with a ragged edge. On a floor
standing water forms as more water comes. It mirrors the room, is choppy where the jet lands, and drops that fall back
make small rings. On a wall the water runs down in a few rivulets that end in a drop. The sizes are as before (puddle
up to 36 units, wet patch up to 18 units). Everything dries from the thin edges in: it stays wet for the first third of
the 20 seconds, then the edge goes in, and standing water goes first. The splash throws droplets of water, stretched
along their flight, forward and to the sides, and a soft mist. Into standing water there are more droplets, they fly
higher, and they go all round. rd-vanilla and the browser build keep the old marks and the sprite splash.

The sounds are a hissing loop at the gun while it sprays, a click of the trigger and pump when it starts, the jet
breaking on a hard surface, the bubbling of the jet into standing water, and a face full of water. They replace the
footstep and body fall sounds used before. None of them is recorded audio.

The jet leaves the muzzle of a water gun. While you spray, and for 450 ms after, you hold it in your right hand: your
weapon (also the saber, its blades are not drawn) is put away until then, your body takes the pose of a rifle held up
(`TORSO_WEAPONREADY3`), and the gun points where you look. The same goes for the other players of VisionJK, each client
poses them itself. In first person the water gun replaces the weapon you see, 0.7 times as large, low at the right; with
a saber selected the game is in third person anyway. In third person with `cg_thirdPersonAngle` the jet goes where the
player looks, not where the camera looks.

On rd-vulkan-vision the jet is drawn as real water: it bends the picture behind it with a little colour fringing,
mirrors the scene at a flat angle, has a highlight along the jet, darker edges and a brighter middle, small waves that
travel with the water, and white foam where it tears. It cuts into the level where they really meet. The jet is whole
for 170 ms after it leaves the gun, then falls apart into drops that become round beads of their own size, up to 650 ms,
and loose drops are stretched along their flight. The OpenGL renderer and rd-vulkan have no such water and keep the
old jet of sprites.

Who is hit in the face by a water gun sees drops land on the view, on the side the water came from: large ones and fine
spray. The large ones run down in fits and leave a trail with small beads. Each drop shows a small upside-down picture
of what is behind it, and the view is soft in the film of water around them. They dry after 3 to 6 seconds. They need
`v_fxWater`, like the other water looks. An older engine shows the drops of coming up out of water instead. The OpenGL
renderer has no such effect. On rd-vulkan-vision the jet also bends the picture behind it (`v_fxWater`). `v_toys 0`
hides the water of others too.

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

An emoji with a mood also lets the voice of your model say a line, if that voice has one. Laughing ones say a gloat
(or a victory or taunt line). Thumbs up, clap, trophy and fire say a victory line, angry ones an anger line, thinking
ones a confused line, open mouth and skull a gasp, and tongue, wink and kappa a taunt. Players can also upload chat
emojis of their own on jka.vision/models.html (section "Chat emojis"). The game fetches them in the background into
`base/gfx/emoji/` of the home folder, so they can be thrown like the emojis of the game; the upload is described in
[models.md](models.md#chat-emojis-of-the-store).

To play table tennis, use `v_pingpong` (Controls, Extras, "Table Tennis"). It puts a table in front of you and you play
from its near end. It needs a flat floor and room for the table at any angle. Another player who stands in a box at the far end (from 30 units inside the
table's far end to 90 units behind it, and up to 30 units beside the table) and uses `v_pingpong` there plays against you. `v_pingpong` again or Esc leaves. When the owner of the table leaves, the table goes.

While you play, the camera stands behind your end of the table and your player is not drawn for you (the others see
them). The mouse moves the paddle, sideways and up and down. A click serves when it is your serve. The players stand
still and show the dots of talking, like the RC car, so on a JA+ server nobody can hurt them.

A game goes to 11 points and you need 2 ahead. The serve changes every 2 points, and at 10 all every point. A hit aims
for the other half: where the ball meets the paddle and how fast the mouse moves sideways steer it. A ball that bounces
on the half of the hitter, lands off the table or does not get over the net gives the point to the other player. A ball
that bounces twice on the half of the receiver, or is not returned, gives the point to the hitter.

Everybody near sees the table, the paddles, the ball and the points over the net.

To play tennis, use `v_tennis` (Controls, Extras, "Tennis"). It puts a court on the floor in front of you: 440 x 200
units inside the lines (about half a real court), a net 24 units high and green ground round the lines. It needs a flat
floor and room for the area inside the lines. If the court does not fit in the direction you look, it turns round you in
steps of 15 degrees until it fits. If it fits nowhere, the screen says "No room for a court here". Another player who
stands on the far half of the court, or up to 220 units behind it (and not more than 120 units beside its lines), and
uses `v_tennis` there plays against you. `v_tennis` again, dying, or walking more than 600 units from the middle of the
court leaves. When the owner of the court leaves, the court goes.

While you play, you run about your half with the normal movement keys and the view is forced to third person, from
behind. Each player has a racket in the right hand. A ball that comes within 34 units of the racket (it is to the right
of the player, and the ball counts from the feet to a little above the head) is hit back by itself, toward where you
look: left and right by where you look, long or short by how high you look. Looking up goes longer, and too far goes
out. The Use key serves, from your own half. The screen says "Press Use to serve", or "Go back to your half to serve"
when you are on the other half.

A hit has to bounce in the other half inside the lines. Hitting the ball before it bounces (a volley) is allowed. Two
bounces on your half lose the point, a ball that lands outside the lines gives the point to the other player, and a ball
in the net falls back on the side it came from. The points count as in tennis: 0, 15, 30, 40, deuce, advantage. A game
needs 4 points and 2 ahead, and the serve changes every game. The first to 3 games wins. The winner is shown for 4
seconds, then a new match starts.

Everybody near (up to 1600 units) sees "name games points : points games name" over the net. `v_toys 0` hides the courts
of others, as for tables.

To draw, bind `v_board` (Controls, Extras, "Drawing Board") and press it. A window opens with a picture of 256 x 128
pixels and 32 colours, which you paint with the mouse. There are brush sizes 1 to 4, Fill, Undo and Clear. Save 1 to 4
and Load 1 to 4 keep four presets in `drawings/preset<n>.vjd` of the home folder. Three buttons hold the board up in
front of you, hold it over your head, or put it away. Esc or Close shuts the window, and the board stays where it is.
Without the window, `v_board hold <0|1|2>`, `v_board save <1-4>`, `v_board load <1-4>` and `v_board import <file>` work
from a bind.

The Picture button, right of the brush sizes, lists the picture files (png, jpg, jpeg, tga) of the `drawings` folder:
`japlus/drawings` of the home folder, and `base/drawings` and the paks. It shows up to 64 files, 10 to a page, with `<`
and `>` when there are more. Click one and it goes on the board: fitted with its sides kept and centred, the rest white,
transparent parts white, turned into the 32 colours with dithering. Undo takes it back. Click Picture again to close the
list. When the folder has no pictures, the list shows the full path to put them in. `v_board import <file>` takes a file
name of `drawings/`, no subfolders, and prints a line on the console when there is no such file. Like any drawing, the
picture goes to the store when the board is held up.

Holding the board in front, both arms are stretched forward and the hands hold its top edge, the board hangs in front of
them and the head stays visible. Over the head, the arms are up and the hands hold its bottom edge. The arms move into
the pose in 250 ms and follow the body again at once when the board is put away. A model that is not humanoid keeps the
old place of the board and has no pose, and a dead player has no pose.

Other players of VisionJK see the board and the pose, with the picture, wherever you go. A
picture that changed goes to the store of jka.vision at most every 8 seconds. Until the others have it, they see the
previous picture or a white board. The picture is public: anybody can upload one, so draw nothing you want to keep to
yourself.

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
| `v_pingpong` | | puts a table down and plays, or joins the player at the far end, or leaves |
| `v_tennis` | | puts a tennis court down and plays, or joins a player on the far half, or leaves |
| `v_board` | | opens the drawing board window, or shuts it |
| `v_board hold <0\|1\|2>` | | 0 puts the board away, 1 holds it in front of you, 2 over your head |
| `v_board save <1-4>` | | keeps the picture as a preset |
| `v_board load <1-4>` | | loads a preset |
| `v_board import <file>` | | puts a picture file of `drawings/` on the board |
| `v_emoteImage` | laughing | the emoji of the chat that `v_emote` sends, without the colons |
| `v_cgameLiquid` | 1 | 1: the cgame draws the jet of the water gun as real water when the renderer can (rd-vulkan-vision), 0: always the sprites |
| `v_cgameWet` | 1 | set by the engine: 1 when the cgame can hand wet spots to the renderer (`ext.R_AddWet`) |
| `v_screenFxDrops` | 1 | set by the engine: 1 when the screen effect can draw the drops of the water gun on the view |
| `v_toys` | 1 | 0: the toys and emoji of other players are not drawn |

## How it works

There is no entity on the server. A throw is a point and a speed (`bg_toys.h`, 19 bytes: the kind, three int32 for the
point, three int16 for the speed). Every cgame that hears of a throw lets the ball fly on its own: gravity 800, a
trace each frame against the level and the players, and the thrower is skipped for the first 0.15 s. So where the ball
lands can differ a little from one player to the next.

On a VisionJK server the throw goes through the game module. The cgame sends the client command `vs_toy`, and the
server sends the server command `vs_toy` to the clients with `v_caps` bit 3. The server says it does this in
`vs_caps` bit 9. It takes a throw only from a living player who plays, from within 160 units of his eyes (640 for `TOY_TABLE`,
`TOY_TABLE_GONE`, `TOY_PONG_JOIN` and `TOY_PONG_SCORE`, because they carry the middle of the table or court and a court
player can be far from it), and 4 a second at most.

On any other server it goes through the masterserver (`VOICE_TOY`). The master hands a throw to the sessions of the
room that said `TOY_HELLO` or threw, 4 a second at most. It refuses broken or too fast throws. The cgame says hello
every 20 s.

The water gun is not a throw. The jet is drops that fly on their own, each linked to the drop before it, so the jet
bends as it falls. With rd-vulkan-vision each link is a piece of water for the renderer (see below), with any other
renderer it is a sprite line (`RT_LINE`). The others get `TOY_WATER` (the same 19 bytes as a snowball: where
the water leaves the gun and how fast) every 260 ms while it sprays, and their cgame sprays from there for 340 ms, so
the jet of somebody else turns in steps. A player who sprays holds the water gun for `WATER_HOLD_MSEC` (450 ms) after
the last spray. In that time `cg_players.c` removes his weapon model and does not put it back, `CG_WaterTorsoAnim` plays
`TORSO_WEAPONREADY3` on his torso on every client (client side only, nothing new on the network), and `CG_WaterGun`
draws the gun at the `*r_hand` bolt pointing where he looks. The jets of others start at the muzzle (`tag_flash`) of the
gun this cgame draws in their hand, the own jet at the muzzle of the own gun. In first person `CG_WaterViewGun` (called
from `CG_AddViewWeapon` in `cg_weapons.c`) draws it as the view weapon. The aim is the `viewangles` of the player state,
not `cg.refdef.viewangles`, which follow the camera in third person. The limits of the relays (4 a second) count snowballs and water together.

Puddles and wet patches are temporary marks drawn anew every frame with their size and wetness (24 at once). The
textures come from `tools/toys/textures.py` (`waterstream`, `waterdrop`, `wet`, `puddle`, `puddleshine`). The shine is
masked to the puddle shape, so the added light stays inside it. The splash is `effects/vision/water_splash.efx`. The
masterserver and the game module have to know `TOY_WATER` (0.2.9-dev.25 and later).

On rd-vulkan-vision the marks are wet spots for the renderer. The call is `re.ext.AddWet` (`wetSpot_t` in
`rd-common/tr_types.h`, up to `WET_MAX_SPOTS` (32) a frame). The cgame gets it as `ext.R_AddWet` when the engine sets
the ROM cvar `v_cgameWet` to 1. `cg_water.c` hands over each puddle and wet patch every frame (`Water_AddWet`): the
wetness, the puddle amount from the water that came, the flow while water lands, the hit point and a seed.
rd-vulkan-vision draws them in `VK_DrawWet` / `VK_WetPass` with `shaders/wet.vert` and `wet.frag`. The level under each
pixel comes from the depth buffer. Wet ground is darkened by 0.55 at full wetness, after Lagarde's physically based wet
surfaces, with Fresnel at F0 0.02. The mirror of standing water is a march of the mirrored ray through the depth (32
steps), with more mirror than Fresnel alone gives, so a puddle reads as water. Without this pass (rd-vanilla, OpenGL ES)
the marks with the textures of `tools/toys/textures.py` stay as before.

The splash droplets are simulated in `cg_water.c` (`Water_Splash`, 256 at once, 2 to 4 per landing drop, 3 to 5 into
standing water from `WATER_POOL` (0.3) of the water on) and drawn as pieces of `re.ext.AddLiquid`. With the renderer
water the splash effect is `effects/vision/water_mist.efx` (mist only), otherwise `water_splash.efx`.

The drops on the view are sources of kind `SCREENFX_DROP` (8) of the screen effect. They are simulated in
`cg_screenfx.c` (`CG_ScreenFxWet` takes the velocity of the water, `CG_ScreenFxDrops`, 24 at once) and drawn by
`screenfx.frag`. This only runs when the engine sets the ROM cvar `v_screenFxDrops` to 1.

The sounds are all made by `tools/toys/sounds.py`.

The shape of the jet is in `cgame/toys/cg_water.c`. The jet stays whole for `WATER_TEAR_MSEC` (170 ms) after it leaves
the gun, then falls apart until `WATER_TORN_MSEC` (650 ms): the links between drops break with a growing breakup, each
drop becomes a bead of its own size (Plateau-Rayleigh) and loose drops are stretched along their flight. The radius at
the gun is 0.75 units, a little uneven along the jet. A coordinate that moves with the water (flow) lets the waves
travel with it.

The renderer call is `re.ext.AddLiquid` (`RE_AddLiquid` in `rd-vanilla/tr_cmds.cpp`, a shared front end). It takes
pieces of water, `liquidSegment_t` in `rd-common/tr_types.h`: the points a and b, two radii, the flow at both ends and
a breakup from 0 to 1, up to `LIQUID_MAX_SEGMENTS` (1024) a frame. The engine passes it to the cgame as
`ext.R_AddLiquid` when `v_cgameLiquid` is 1. rd-vanilla returns `qfalse`, and then the cgame draws the old sprite jet.

rd-vulkan-vision draws the pieces in `VK_DrawLiquids` (`vk_post.cpp`), right after the main view of the level and before
its glow. The frame is copied (`vk.screen`), then each piece is a quad made in the vertex shader (`shaders/liquid.vert`,
program `VKP_LIQUID`, records in the bone buffer like `sprite.vert`, batches of 127 for the OpenGL ES uniform block).
`shaders/liquid.frag` casts the ray of every pixel against the capsule (a round body with rounded ends, the radius going
from one end to the other) and writes the depth of the hit, so water and level cut each other where they meet. It then
shades it: what lies behind, bent by refraction (Snell, limited to a few jet widths on the screen) with slight colour
fringing and absorption; the mirrored scene by Fresnel's law (Schlick) from the frame copy; an anisotropic highlight
along the jet (Ashikhmin-Shirley) from the light grid of the level and a light from above; a lens effect (darker edges,
brighter middle); small waves riding with the water; milkiness and white foam where the jet tears. The light of the
level is taken at both ends of each piece and blended along it. The edge is soft over one pixel. It works on the OpenGL
ES device too (the shaders pass the ES check, not tested in a browser yet).

The model is `models/vision/toys/watergun.md3` with `watergun.png`, made by `tools/toys/watergun.py` from `blaster-e` of
Kenney's Blaster Kit 2.1 (CC0, www.kenney.nl): the grip is at the origin, the barrel along +x, 30 units long, `tag_flash`
at the muzzle. It is split into two surfaces because the renderers take at most 999 vertices a surface. The shader
`models/vision/toys/watergun` in `shaders/vision_toys.shader` lights the colours as a model and adds the shine of glossy
plastic (`env_chrome` by `tcGen environment`, `alphaGen lightingSpecular`).

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

Table tennis is not a throw either. No server keeps the game. The owner says where the table is and who plays the far
end (`TOY_TABLE`, every 5 s). The paddles say where they are six times a second (`TOY_PONG_PADDLE`). A hit says where
the ball is and how fast, and every cgame lets it fly (gravity, bounces on the table, the net). The player the ball flies
to judges the point and sends the score (`TOY_PONG_SCORE`). A table not heard of for 16 s is gone. A ball message is
moved on by half the ping when it arrives. The new kinds are `TOY_TABLE` (7) to `TOY_PONG_PADDLE` (12) in
`bg_toys.h`, checked by `bg_toys.c`, so the master and the game module have to be 0.2.9-dev.30 or later. The code is
`cg_pong.c`. `tools/toys/pingpong.py` makes the assets and `tools/toys/sounds.py` the sounds `pong_hit.wav` and
`pong_bounce.wav`.

Tennis is a table with `z` 1. The court goes as `TOY_TABLE` with z 1, so older builds refuse it and see nothing.
`TOY_TABLE_GONE`, `TOY_PONG_JOIN`, `TOY_PONG_BALL` and `TOY_PONG_SCORE` are shared with the table. A court sends no paddle
positions (`TOY_PONG_PADDLE`), the rackets are drawn in the hands of the players. The score of a court player goes as
games * 8 + points. The game module and the masterserver have to be 0.2.9-dev.33 or later for courts. The code is in
`cg_pong.c`. `tools/toys/pingpong.py` now also writes `tennis_court.md3` and `tennis_racket.md3` with their pictures
(`tennis_court.png`, `tennis_strings.png`, `tennis_frame.png`) in `assets/japlus_client/models/map_objects/vision/`. The
ball is the table tennis ball sprite, yellow. `tools/toys/sounds.py` writes `tennis_hit.wav` and `tennis_bounce.wav`.

The drawing board is not a throw either. The picture is 256 x 128 pixels, a byte a pixel, each one of 32 colours. Your
own cgame draws the board on the player and shows the picture through a stream of the engine (`ext.Video_Picture`, the
streams 4 to 7, the TVs use 0 to 3). A held picture that changed goes to the store of jka.vision at most every 8
seconds (`Sys_HttpPost`, `POST /models/drawings/api/upload`). The store gives it a number and the board says the number
to the others as `TOY_BOARD` (13), through the game module or the masterserver, with how it is held. Their engine
fetches `GET /models/drawings/files/<number>.vjd` into `base/drawings/` of the home folder and shows it. The code is
`cg_board.c`, and `CL_VideoPicture` and the drawing part of `cl_models.cpp` in the engine. The store checks only the
format: the exact size and that every pixel is one of the 32 colours. The limits are 60 uploads an hour and 300 a day
per address, and the store keeps the newest 5000. `files/<number>.png` is the same picture to look at. The master and
the game module have to be 0.2.9-dev.32 or later.

Pictures are brought in with a new call of the renderer, `re.ext.LoadImageFit` (`R_LoadImageFit` in
`rd-common/tr_image_load.cpp`), which the cgame gets as `ext.R_LoadImageFit` when `v_cgameImage` is 1. For these
pictures the PNG loader takes any size and palette and grey PNGs; textures still need power-of-two sizes and 24 or 32
bits, as before. The cgame fits the picture into 256 x 128 and dithers it with Floyd-Steinberg.

The poses are made by each client from what it already hears (`TOY_BOARD`), nothing new goes over the network. In front,
the arms take a frame of `BOTH_FORCE_2HANDEDLIGHTNING_HOLD`, over the head the end of `TORSO_SURRENDER_START`.

On a pure server (`sv_pure 1`) the engine read only a few file types from folders (.cfg, .menu and similar). So the
presets, the pictures of the boards of others (`drawings/<number>.vjd`) and the upload file could not be read there, and
the drawing board did not work on pure servers since it came (0.2.9-dev.32). Now files directly in `drawings/` with
.vjd, .png, .jpg, .jpeg or .tga are read from the folder and listed on a pure server too (`FS_IsDrawing` in
`qcommon/files.cpp`).

The engine got three changes for it: `Sys_HttpPost` for curl and WinHTTP, a WinHTTP reader that no longer shares one
static buffer between requests (it was wrong since the TV streams run beside other requests), and 8 video streams
instead of 4.

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

The real water, the water gun in the hand and the view weapon were run in headless test runs only. Not tested yet in a
real game with players.

The wet ground was run in headless test runs on mp/ffa3: a puddle on the floor while spraying and while drying, a wet
wall with rivulets, the drops on the view and the splash droplets. The game audio was recorded to a file to check that
the sounds play. The Vulkan validation layers showed no error from the new pass. The Windows build compiles, it was not
run.

The masterserver relay was tested with a UDP script: hello, a throw handed on, a too fast one refused. The unit tests
are in `tests/vjk/toys.cpp`.

For the RC car, one client drove the car (forwards, a turn, a hop, against a wall, back to the player with Esc). Two
clients on a local server: the driver showed the dots of talking, the other client received the `TOY_RC` server
commands, and the car stopped at the other player. There is a unit test for `TOY_RC`.

For the spray, a spray on the floor and on a wall (upright, not mirrored, cut off at the edge of the wall, dim in a dark
room). Two clients: the other client received the `TOY_SPRAY` server command with the right picture. There is a unit
test for the `TOY_SPRAY` ranges.

For the emoji, two clients on a local server, and both received the emoji (the server command `vs_toy` with kind 6). The
flight, the pop, the burst, the 2D pop in first person and the fading near the eyes were seen. There is a unit test
`emoji` in `tests/vjk/toys.cpp`.

For table tennis, two clients on a local server: a table put down, the second player joined at the far end, a serve, a
rally of three hits, a point judged by the receiver and the score sent (1 : 0). Both views were seen: the camera behind
the own end, the paddle of the other player, the ball and the score over the net. There is a unit test `table_tennis`
in `tests/vjk/toys.cpp`.

For tennis, two clients headless on mp/ctf4: the court was put down (it turned to fit), the second player joined, the
serve, a point judged and counted (15:0), and with a test build with a longer reach a rally of several returns between
both players. At the spawn points of 12 stock maps a court fitted on 3 (mp/ctf1, mp/ctf4, mp/siege_desert): courts need
big open rooms.

For the drawing board, there are unit tests (the board in `tests/vjk/toys.cpp`, `test_drawing` in
`deploy/models/test_modelstore.py`). Then two clients headless with the live store: a preset was loaded and held up,
uploaded (the store gave it a number), and the other client fetched the picture and showed the board on the holder with
that picture. The window was drawn with all its parts.

For the pictures and poses, headless: an RGBA PNG of 400 x 300, a palette PNG, a grey PNG and a JPG were imported on a
listen server with `sv_pure 1`. Then two clients on a dedicated server with `sv_pure 1`: the holder was seen from the
front and the side in both poses, and the other client saw the pose.

Not run: clicking in the picture list with a real mouse, the pictures and poses on Windows, with other player models than the default, the store upload of an imported picture (the store was off in the test), painting with a real mouse, the drawing board on Windows (the WinHTTP POST) and in a real game with people, table tennis in a real game with people and a mouse, table tennis through the masterserver, table tennis on Windows, tennis in a real game with people, hitting at the real reach of 34 units, tennis through the masterserver, tennis on Windows, emoji in a real game with people, hearing the sounds (the test used dummy audio), emoji on Windows, sprays in a real game with people, the RC car in a real game with people, a real game with people (snowball and water gun), the water gun with the OpenGL renderer, Windows, a server without the game module of VisionJK in a real game.
