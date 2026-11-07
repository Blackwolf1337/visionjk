// The shaders of the screen effects of VisionJK (codemp/cgame/screenfx/cg_screenfx.c).

// A player Force Sight shows, drawn over him as a ghost (CG_ScreenFxGhostShell), made of smoke and not a shell:
// - a cloudy noise (gfx/vision/ghost_smoke.tga: its alpha the noise, its colour bright where the noise is thin) cut
//   by the alpha of the entity, so holes wander through him and what is left glows at its frayed edges. It is laid
//   on him along his model's axes (tcGen vector), not along the seams of his skin, and rises up him.
// - finer threads of vapour (gfx/vision/ghost_wisp.tga) that rise faster, the same way.
// - a faint rim where his surface turns away from the eye.
// The outline breathes a little. Added to what is there; the colour of the entity is the colour of the ghost and how
// strong it is. The pictures are made by a script (2026-10-24, 2026-10-25).
gfx/vision/ghost
{
	q3map_nolightmap
	deformVertexes wave 40 sin 0.6 0.6 0 0.35
	{
		map gfx/vision/ghost_smoke
		tcGen vector ( 0.012 0.008 0 ) ( 0 0 0.018 )
		tcMod turb 0 0.08 0 0.25
		tcMod scroll 0.03 -0.5
		blendFunc GL_ONE GL_ONE
		alphaFunc GE128
		rgbGen entity
		alphaGen entity
	}
	{
		map gfx/vision/ghost_wisp
		tcGen vector ( 0.025 -0.02 0 ) ( 0 0 0.035 )
		tcMod turb 0 0.1 0 0.4
		tcMod scroll -0.02 -0.8
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen entity
		alphaGen const 0.6
	}
	{
		map gfx/vision/ghost_rim
		tcGen environment
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen entity
		alphaGen const 0.3
	}
}
