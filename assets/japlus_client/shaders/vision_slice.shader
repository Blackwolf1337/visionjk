// The saber slicing of VisionJK (cgame/cg_slice.c, rd-vulkan-vision ext.AddCutEntityToScene).

// The cut face of a body a saber cut through: the cut half drawn once more with its back faces alone, which are seen
// only through the opening, so they fill it as a solid face. Its colour comes from the cgame (white hot, fading
// through red to dark: the afterglow), the glow stage gives it the halo of r_DynamicGlow.
gfx/vision/slice/cut
{
	cull back
	{
		map $whiteimage
		rgbGen entity
		glow
	}
}
