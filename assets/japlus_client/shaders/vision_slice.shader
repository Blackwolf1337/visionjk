// The saber slicing of VisionJK (cgame/cg_slice.c, rd-vulkan-vision ext.AddCutEntityToScene).

// The cut face of a body a saber cut through: the cut half drawn once more with its back faces alone, which are seen
// only through the opening, so they fill it as a solid face. Its colour comes from the cgame (orange hot, fading
// through red to dark: the afterglow), the glow stage gives it the halo of r_DynamicGlow.
gfx/vision/slice/cut
{
	cull back
	{
		map gfx/vision/slice/embers
		rgbGen entity
		tcMod scale 3 3
	}
	{
		// the embers that glow: brighter veins over the crust, drifting a little, and the halo only from them
		map gfx/vision/slice/embers
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcMod scale 5 5
		tcMod scroll 0.02 0.03
		glow
	}
}
