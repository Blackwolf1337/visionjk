// VisionJK: shaders of the new look that an engine without its calls takes from this pack

// The outline around the player the mouse points at (codemp/cgame/select/cg_select.c), for an engine that
// can not make this shader itself: the model a second time, a little larger and only where it faces away, in
// the colour of the entity.
gfx/vision/select_hull
{
	cull	back
	deformVertexes	wave	100 sin 1.1 0 0 0
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen entity
		alphaGen entity
		glow
	}
}

// The sea of clouds of the Vision Temple (maps/vision_temple.bsp, tools/temple/build.py): the brush of a fog.
// The renderer of VisionJK fills it with clouds (q3map_vision, docs/japlus/renderer.md), any other one
// with this fog. The numbers: the round place the clouds leave free (x, y, radius: the terrace around the
// podium) and the lane (from x, to x, half its width: the way to it).
textures/vision/temple_clouds
{
	surfaceparm	trans
	surfaceparm	nonsolid
	surfaceparm	fog
	surfaceparm	nolightmap
	fogparms ( 0.80 0.62 0.48 ) 3000
	q3map_vision clouds 0 0 208 -524 -170 28
}
