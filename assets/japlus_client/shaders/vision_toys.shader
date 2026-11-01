// the snowball of issue #9 (cgame/toys/cg_toys.c), pictures from tools/toys/textures.py

// The ball itself and the lumps of a burst (models/vision/toys/snowball/, tools/toys/snowball.py): packed snow lit
// per pixel by rd-vulkan-vision, the light wrapping into its shadow as it scatters through snow, its crystals
// glinting; other renderers light the picture by the vertices
models/vision/toys/snowball/snow
{
	{
		map models/vision/toys/snowball/snow
		normalMap models/vision/toys/snowball/snow_n
		specular 0.12 16 0
		wrapLight 0.6
		sparkle 1.0
		rgbGen lightingDiffuse
	}
}

// the powder of the burst (effects/vision/snowball_hit.efx)
gfx/vision/toys/snowpuff
{
	nopicmip
	{
		map gfx/vision/toys/snowpuff
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// what stays on a wall and melts away (CG_ImpactMark)
gfx/vision/toys/snowsplat
{
	nopicmip
	polygonOffset
	{
		clampmap gfx/vision/toys/snowsplat
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// on the screen of the one who is hit
gfx/vision/toys/snowscreen
{
	nopicmip
	{
		clampmap gfx/vision/toys/snowsplat
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// the jet of the water gun: clear water, and over it the light that runs along it
gfx/vision/toys/waterstream
{
	nopicmip
	cull twosided
	{
		map gfx/vision/toys/waterstream
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
		tcMod scroll -3 0
	}
	{
		map gfx/vision/toys/waterstream
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		tcMod scroll -7 0
		tcMod scale 0.5 1
	}
}

// a drop of the splash (effects/vision/water_splash.efx)
gfx/vision/toys/waterdrop
{
	nopicmip
	{
		map gfx/vision/toys/waterdrop
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// a wet patch on a wall, it dries (the alpha of the mark)
gfx/vision/toys/wet
{
	nopicmip
	polygonOffset
	{
		clampmap gfx/vision/toys/wet
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// a puddle: darker ground, and the light that plays on its water
gfx/vision/toys/puddle
{
	nopicmip
	polygonOffset
	{
		clampmap gfx/vision/toys/puddle
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
	{
		clampmap gfx/vision/toys/puddleshine
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		tcMod turb 0 0.008 0 0.2
	}
}

// table tennis (cgame/toys/cg_pong.c, tools/toys/pingpong.py): the ball, a sprite, and the net of the table, seen from
// both sides, its holes cut out
gfx/vision/toys/pingball
{
	nopicmip
	{
		map gfx/vision/toys/pingball
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

models/map_objects/vision/pingpong_net
{
	cull none
	{
		map models/map_objects/vision/pingpong_net
		alphaFunc GE128
		rgbGen lightingDiffuse
		depthWrite
	}
}

// the tennis court lies half a unit over the floor, the offset keeps the floor from showing through it far away
models/map_objects/vision/tennis_court
{
	polygonOffset
	{
		map models/map_objects/vision/tennis_court
		rgbGen lightingDiffuse
	}
}

models/map_objects/vision/tennis_strings
{
	cull none
	{
		map models/map_objects/vision/tennis_strings
		alphaFunc GE128
		rgbGen lightingDiffuse
		depthWrite
	}
}

// the drawing board (cgame/toys/cg_board.c): board/<n> shows stream n of the engine (client/cl_video.cpp, the picture
// the cgame made), frame is its back and edge

gfx/vision/board/4
{
	nopicmip
	{
		videoMap *tv4
		rgbGen vertex
	}
}

gfx/vision/board/5
{
	nopicmip
	{
		videoMap *tv5
		rgbGen vertex
	}
}

gfx/vision/board/6
{
	nopicmip
	{
		videoMap *tv6
		rgbGen vertex
	}
}

gfx/vision/board/7
{
	nopicmip
	{
		videoMap *tv7
		rgbGen vertex
	}
}

gfx/vision/board/frame
{
	nopicmip
	{
		map models/map_objects/vision/pingpong_frame
		rgbGen vertex
	}
}

// the water gun (models/vision/toys/watergun.md3, made by tools/toys/watergun.py from Kenney's Blaster Kit, CC0):
// the colours of the kit lit as a model, and the shine of glossy plastic where the light reflects towards the eye
models/vision/toys/watergun
{
	{
		map models/vision/toys/watergun
		rgbGen lightingDiffuse
	}
	{
		map textures/common/env_chrome
		tcGen environment
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.3 0.3 0.3 )
		alphaGen lightingSpecular
	}
}

// The RC car (models/vision/toys/rccar/, made by tools/toys/rccar.py from Kenney's Toy Car Kit, CC0). rd-vulkan-vision
// lights its materials per pixel with their normal maps and their shine (normalMap, specular): candy paint under a
// clear coat, black plastic, rubber with knobs, anodized rims. Other renderers light the pictures by the vertices.
models/vision/toys/rccar/paint
{
	{
		map models/vision/toys/rccar/paint
		normalMap models/vision/toys/rccar/paint_n
		specular 0.6 48 1.0
		rgbGen lightingDiffuse
	}
}

models/vision/toys/rccar/trim
{
	{
		map models/vision/toys/rccar/trim
		normalMap models/vision/toys/rccar/trim_n
		specular 0.25 20 0
		rgbGen lightingDiffuse
	}
}

models/vision/toys/rccar/tire
{
	{
		map models/vision/toys/rccar/tire
		normalMap models/vision/toys/rccar/tire_n
		specular 0.06 10 0
		rgbGen lightingDiffuse
	}
}

models/vision/toys/rccar/rim
{
	{
		map models/vision/toys/rccar/rim
		normalMap models/vision/toys/rccar/rim_n
		specular 1.4 90 0.5
		rgbGen lightingDiffuse
	}
}

// its windows: dark glass under a clear coat
models/vision/toys/rccar/glass
{
	{
		map models/vision/toys/rccar/glass
		specular 1.2 160 1.0
		rgbGen lightingDiffuse
	}
}

// its lamps, sprites the cgame puts where they shine (cg_rccar.c): a warm white at the front, red at the back,
// brighter while it brakes
models/vision/toys/rccar/lamp
{
	cull none
	{
		map models/vision/toys/rccar/lamp
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		glow
	}
}

// a fast wheel (wheel_blur.skin, cg_rccar.c): the tread smeared along by the turn, the rim round its axle
models/vision/toys/rccar/tire_blur
{
	{
		map models/vision/toys/rccar/tire_blur
		normalMap models/vision/toys/rccar/tire_blur_n
		specular 0.06 10 0
		rgbGen lightingDiffuse
	}
}

models/vision/toys/rccar/rim_blur
{
	{
		map models/vision/toys/rccar/rim_blur
		specular 1.0 40 0.3
		rgbGen lightingDiffuse
	}
}

// the marks of its tires (cg_rccar.c): the tread pressed into soft ground and rubber on hard ground. The picture is
// how much darker the ground gets, the colour of the vertices fades it out.
models/vision/toys/rccar/track
{
	polygonOffset
	{
		map models/vision/toys/rccar/track
		blendFunc GL_ZERO GL_ONE_MINUS_SRC_COLOR
		rgbGen vertex
	}
}

models/vision/toys/rccar/skid
{
	polygonOffset
	{
		map models/vision/toys/rccar/skid
		blendFunc GL_ZERO GL_ONE_MINUS_SRC_COLOR
		rgbGen vertex
	}
}

// the smoke of its tires (effects/vision/tire_smoke.efx)
models/vision/toys/rccar/smoke
{
	nopicmip
	{
		map models/vision/toys/rccar/smoke
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
