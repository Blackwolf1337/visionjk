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

// The gold of the Vision Temple, on the surfaces of its map: the renderer of VisionJK shades it by where a place
// on it is, any other one draws this colour.
textures/vision/temple_gold
{
	surfaceparm	nolightmap
	q3map_vision gold
	{
		map $whiteimage
		rgbGen const ( 0.80 0.58 0.22 )
	}
}

// The light that stands in the air of the Vision Temple (the rings, what falls from the highest of them, the
// light over the podium): a brush of fog around it. Any other renderer has a fog there that nobody sees.
textures/vision/temple_light
{
	surfaceparm	trans
	surfaceparm	nonsolid
	surfaceparm	fog
	surfaceparm	nolightmap
	fogparms ( 1.0 0.8 0.5 ) 400000
	q3map_vision light
}
