// The materials of the material tool of the toolgun (codemp/game/props/bg_tools.h, issue #3). They go over a
// whole model in place of its own shaders, so they look right on any model: a colour, or a reflection that
// comes from the view (tcGen environment) and not from the texture coordinates of the model. Made of pictures
// of the base game. A colour of the colour tool goes on top of the lit ones (rgbGen lightingDiffuse).

vision/tools/plain
{
	q3map_nolightmap
	{
		map $whiteimage
		rgbGen lightingDiffuse
	}
}

vision/tools/chrome
{
	q3map_nolightmap
	{
		map textures/common/environ1
		tcGen environment
		rgbGen lightingDiffuse
	}
}

vision/tools/gold
{
	q3map_nolightmap
	{
		map textures/common/environ1
		tcGen environment
		rgbGen lightingDiffuse
	}
	{
		map $whiteimage
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 1.0 0.76 0.28 )
	}
}

vision/tools/glass
{
	q3map_nolightmap
	surfaceparm trans
	cull twosided
	{
		map textures/common/glass2
		tcGen environment
		blendFunc GL_ONE GL_ONE
		rgbGen identity
	}
}

vision/tools/ice
{
	q3map_nolightmap
	{
		map $whiteimage
		rgbGen lightingDiffuse
	}
	{
		map $whiteimage
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 0.62 0.82 1.0 )
	}
	{
		map textures/rift/env_crystal
		tcGen environment
		blendFunc GL_ONE GL_ONE
		rgbGen identity
	}
}

vision/tools/shield
{
	q3map_nolightmap
	surfaceparm trans
	cull twosided
	{
		map gfx/effects/p_shield
		blendFunc GL_ONE GL_ONE
		tcMod scroll 0.2 0.1
		rgbGen identity
	}
}

// the ray of a shot of the toolgun (cgame/cg_ents.c, CG_PhysgunBeam): white, its colour comes from the vertices
vision/tools/ray
{
	q3map_nolightmap
	cull twosided
	{
		map $whiteimage
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}
