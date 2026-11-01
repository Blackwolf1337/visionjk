// the snowball of issue #9 (cgame/toys/cg_toys.c), pictures from tools/toys/textures.py

// the ball in flight, a sprite lit by the light around it
gfx/vision/toys/snowball
{
	nopicmip
	{
		map gfx/vision/toys/snowball
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
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
