# Sandbox

Three tools for the props of a level, in the way of Garry's Mod: the physgun, the toolgun and the gravity gun.
It is step 1 of issue #3 ("Implement more functionality from Garry's Mod"). It is not a JA+ feature. The game
module of this tree does the work, so a server needs it. Only players with the client of this tree see colours and
materials and have the tool window, see "Clients without it".

The props are the ones of the map and the ones put down through the web panel (a crate, a ball and so on, see
[webadmin.md](webadmin.md)). The sandbox moves and changes them. It does not make new ones.

## For players

`/amsandbox` gives you three weapons and takes them away again with the next `/amsandbox`:

| Weapon | Tool |
|--------|------|
| stun baton | the physgun |
| Bryar pistol | the toolgun |
| concussion rifle | the gravity gun |

You get only the ones you did not have, and the command takes back only those, not what you had before. The
weapons do not fire any more while you have the sandbox, their buttons are the tools. The client of this tree
takes the baton out by itself, on another client it is reached with the next and previous weapon keys.

An admin of JA+ of any rank may use the command. With `vs_sandbox 1` everybody may. Otherwise it answers "You are
not allowed to execute this command!". The command replaces `/amphysgun`, which is gone.

### The physgun

The stun baton. It is described in [webadmin.md](webadmin.md), "the physgun": attack takes the prop you look at
into the beam (2048 units), the mouse wheel moves it, use next to attack turns it, alt attack next to attack
freezes it, use alone frees every frozen prop. The beam is blue.

### The gravity gun

The concussion rifle. Its beam is red.

- Right click (alt attack) takes the prop you look at, if it is within 384 units and has a mass of 25 at most.
  It hangs in front of you, 64 units from your eye (further for a large one). Right click again drops it.
- Left click (attack) punts it away. With nothing in the gun it punts the prop you look at, within 320 units.
- The mouse wheel moves a held prop away and closer, up to 384 units.
- R (use) is a shockwave: every free prop within 320 units that you can see from where you stand is thrown away
  from you (a bit upwards too), 1200 units a second close by and half of that at the edge. It can go off once
  every 1.5 seconds. A held prop is let go first.

A punt gives a speed of 1600 for a mass of 1. A heavier prop is slower, by the root of its mass: a mass of 16 goes
at 400. The impulse grows with the root of the mass, so a crate of mass 20 still flies.

### The toolgun

The Bryar pistol. A shot goes 4096 units and draws a green ray for 150 ms, whatever it hits. It works on the prop at
the end of the ray. Left click uses the tool, right click takes the setting from the prop into the tool (the
remover removes with both), R puts the prop back as it was.

| Tool | Left click | Right click | R |
|------|------------|-------------|---|
| Remover | removes the prop | removes the prop | |
| Color | paints the prop | takes its color | removes the color |
| Material | puts the material on it | takes its material | removes the material |
| Physical Properties | gives it mass, bounce and grip | takes them | gives it the values it had before |
| Freeze | freezes it where it is | frees it | frees all frozen props |

Color is red, green and blue, 0 to 255 each. A color goes on top of the light of the level: a painted prop is
still lit and shaded.

Materials, over the whole model in place of its own textures: Plain (white, takes the color), Chrome, Gold, Glass,
Ice, Shield and Lava. Color and material can be on a prop together.

Physical Properties:

| Setting | From | To | Default | Meaning |
|---------|------|----|---------|---------|
| Mass | 0.1 | 100 | 1 | how hard it is to move |
| Bounce | 0 | 0.95 | 0.25 | how much speed it keeps from a hit |
| Grip | 0 | 2 | 0.7 | how well it grips the ground. A ball has a rolling resistance instead: grip times 128 |

A frozen prop does not move for anything, it is in the way like a piece of the map, and the physgun or the
Freeze tool frees it.

### The tool window

`v_tools` opens and closes it. It is under Controls, Extras, "Tool Window (Sandbox)", and has no key by default. The
tools are a list at the left, the settings of the chosen one are at the right: three bars and eight colors to pick
for Color, the seven materials as buttons for Material, three bars and four presets (Light, Heavy, Bouncy,
Slippery) for Physical Properties. A line under the settings says what the buttons do. The mouse is free while the
window is open. The cross at its corner, Esc or the right mouse button close it.

The window only sets the cvars below. It also works with the toolgun not in the hand, the settings are sent when
you have the sandbox. With the window closed, the tool and what it does stand under the crosshair while the
toolgun is in the hand. The texts are in English, French, German and Spanish.

## Commands and cvars

| Name | Where | Meaning |
|------|-------|---------|
| `amsandbox` | server | the three weapons on or off |
| `vs_sandbox` | server cvar, 0 | 0: admins only. 1: everybody |
| `vs_tool` | to the server | `vs_tool remover`, `freeze`, `color <r> <g> <b>`, `material <name>`, `physprop <mass> <bounce> <grip>`. Values outside the limits are clamped. Without a tool name the server tells you what it holds |
| `vs_tool` | to the client | the server's reply, see "How it works" |
| `v_tools` | client | the tool window |
| `v_tool` | client cvar, remover | remover, color, material, physprop or freeze |
| `v_toolColor` | client cvar, `255 255 255` | the color of the Color tool |
| `v_toolMaterial` | client cvar, plain | plain, chrome, gold, glass, ice, shield or lava |
| `v_toolPhys` | client cvar, `1 0.25 0.7` | mass, bounce and grip |
| `v_cgMouse <x> <y>` | client | puts the pointer of the cgame on that place of the 640 x 480 of the HUD while the cgame has the keys (the tool window and the boombox window). For scripts, with `v_cgKey mouse1` for the click. Like `v_uiMouse` for the menus |

`vs_sandbox` is archived and is in the table of the web panel.

## How it works

The code is in the files below.

| File | Content |
|------|---------|
| `codemp/game/props/g_props.c` | the section "The sandbox (amsandbox)": `G_SandboxToggle`, who may (`Prop_SandboxAllowed`), the physgun, the gravity gun (`Prop_GravTake`, `Prop_GravPunt`, `Prop_GravWave`, the `GRAV_*` defines), `Prop_GunFrame` and `G_PropGunCmd` (use as the reload key) |
| `codemp/game/props/g_tools.c` | the toolgun: the tools, `vs_tool`, the ray |
| `codemp/game/props/bg_tools.h` | the tools and the materials, for the game module and the cgame |
| `codemp/cgame/sandbox/cg_tools.c` | the cvars, sending them, the line under the crosshair |
| `codemp/cgame/sandbox/cg_tools_ui.c` | the tool window |
| `codemp/cgame/cg_window.c`, `cg_window.h` | the drawing helpers of the window of the boombox and the tool window |
| `assets/japlus_client/shaders/vision_tools.shader` | the materials and the ray |

The three weapons are real weapons with their attack buttons dropped by the move code (`PM_Weapon`, while
`CARRY_PHYSGUN` is set in `stats[STAT_CARRY]`). The server reads the buttons from the command. In its frame
(`Prop_GunFrame`) it looks at which weapon is in the hand: the baton goes to the physgun, the pistol to
`G_ToolFrame`, the rifle to the gravity gun. The beam of the physgun and the gravity gun is the entity
`PROP_BEAM`, the ray of the toolgun is the same kind with an end point (`origin2`) instead of an entity and lives for
150 ms (`CG_PhysgunBeam` draws all three).

The tool a player chose is kept per slot in `g_tools.c` and starts at the defaults after he leaves
(`G_ToolClear`).

### The tool and its settings

The window sets the cvars, `CG_ToolsFrame` turns them into the command `vs_tool ...` (numbers always in the same
form, `CG_ToolCommand`) and sends it while he has the sandbox. The server answers every `vs_tool` with

```
vs_tool set <tool> <r> <g> <b> <material> <mass> <bounce> <grip>
```

The client keeps that as what the server holds and sends again until its cvars give the same command. This is
needed because the flood protection of the server drops a command that comes soon after the last one: the client
sends at most one every 1100 ms and a dragged bar, which changes a cvar every frame, still ends with the last
value. Right click with a tool sends `vs_tool take ...` with the same fields, and the client puts them into its
cvars so the window shows what was taken. A print "Toolgun: n frozen props freed." answers R with Freeze.

### Colour and material on the clients

The prop is an `ET_GENERAL` entity, so its entityState goes to everybody with no new field.

- `customRGBA[3]` is `TOOL_MARK` (0x80) plus `TOOL_MARK_COLOR` (0x01) and `TOOL_MARK_MATERIAL` (0x02) for what the
  prop has. No other `ET_GENERAL` sets `customRGBA[3]`, so the mark tells a prop of the toolgun.
- The color is in `customRGBA[0]` to `[2]`.
- The material is in `genericenemyindex`: its number in `toolMaterials` plus one. Turrets and siege items keep an
  icon there, the mark keeps them apart.

`CG_Ent` (cg_ents.c) reads that: a color sets `RF_RGB_TINT` and the color, a material sets the custom shader
(`vision/tools/<name>`, Lava is `gfx/effects/lava` of the base game). Without a mark the prop is drawn as before.

The materials are shaders in `vision_tools.shader`. A custom shader goes over the whole model, so they do not use
the texture coordinates of the model: Chrome, Gold and Ice use `tcGen environment`, Plain is a lit white. Their
stages that are lit say `rgbGen lightingDiffuse`.

### The renderer

`RF_RGB_TINT` used to replace the color of every stage with the color of the entity, which made a lit stage flat.
Now a stage with `rgbGen lightingDiffuse` stays lit with the color on it (`CGEN_LIGHTING_DIFFUSE_ENTITY`), and the
stages of effects, which have no light, take the color as it is. Both renderers have it (`vk_shade.cpp` of
rd-vulkan-vision, `tr_shade.cpp` of rd-vanilla, search "VisionJK: a lit stage").

## Clients without it

A client of any other kind joins and plays as before. It does not see colors or materials (the prop looks as it
does without the toolgun) and it has no tool window or ray. It also fires the three weapons as usual on its own
machine, while the server ignores the shots, so the player sees a shot that does nothing. An admin on such a client
can use the commands, but the settings of the toolgun reach the server only with `vs_tool` typed by hand.

## Checked

Only headless, with a Linux build: a dedicated server with `vs_sandbox 1`, a crate put down through the web panel.
A client shot it with Color, Chrome and Gold, grabbed it and punted it with the gravity gun, and the tool window was
clicked through with `v_cgMouse` and `v_cgKey` (in German too).

Not checked in the game: the physgun after this change, the shockwave, the remover, Freeze and Physical Properties,
other players who watch, and a player without the client of this tree.

## What comes next

The rest of #3 is later steps: the joints (Weld, Rope and so on) and the spawners (Balloon, Thruster and so on).
