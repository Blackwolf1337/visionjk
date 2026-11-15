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

// The marks a saber leaves on a wall (codemp/cgame/cg_players.c CG_CreateSaberMarks), laid along the whole swing.
// The burn: the wall times rgb + 1 - alpha (the picture is premultiplied, tools/materials/saber_marks.py), so the
// colour of the vertices fades it to nothing (rgb and alpha to 0) and not to black. The renderer of VisionJK also
// lights it as a groove cut into the wall (vmat_vision_fx.oshader).
gfx/vision/saberburn
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/saberburn
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// The hot core of a fresh cut, added; the cgame gives it the colour of the cooling metal (white, orange, dark red)
// and it glows (bloom).
gfx/vision/saberglow
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/saberglow
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		glow
	}
}
