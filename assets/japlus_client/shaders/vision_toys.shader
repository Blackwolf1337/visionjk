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
