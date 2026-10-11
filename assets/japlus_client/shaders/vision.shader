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
