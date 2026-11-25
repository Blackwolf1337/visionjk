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

// The scars a saber melts into what it cuts (codemp/cgame/saber/cg_saberscar.c, pictures by
// tools/materials/saber_marks.py). Laid along the cut: s along it, t across it. The char layers multiply the surface
// by rgb + 1 - alpha (premultiplied), so the vertex colour (rgb and alpha together) fades them to the surface; the
// hot layers are added and glow (bloom), coloured by the vertices as they cool. The renderer of VisionJK lights the
// chars as grooves (vmat_vision_fx.oshader).

// the char, on the wide band: black glass in the groove, a low lip, uneven brown soot
gfx/vision/saberscar
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/saberscar
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// the molten core, on the narrow band: the melt creeps along the cut, barely; the embers (dark red flecks, their
// strength the alpha of the vertices) glow on, still, after the rest has cooled
gfx/vision/sabermolten
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/sabermolten
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		tcMod scroll 0.01 0
		glow
	}
	{
		map gfx/vision/saberembers
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.95 0.22 0.04 )
		alphaGen vertex
		glow
	}
}

// the heat around a fresh cut, on the wide band
gfx/vision/saberhalo
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/saberhalo
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		glow
	}
}

// a bead of slag that sags down a wall from a cut and the smear it leaves (the cgame moves their texture
// coordinates), and what stays of them; four outlines
gfx/vision/saberbead0
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead0
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
		glow
	}
}

gfx/vision/saberbead0_scar
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead0_scar
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/vision/saberbead1
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead1
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
		glow
	}
}

gfx/vision/saberbead1_scar
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead1_scar
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/vision/saberbead2
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead2
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
		glow
	}
}

gfx/vision/saberbead2_scar
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead2_scar
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/vision/saberbead3
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead3
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
		glow
	}
}

gfx/vision/saberbead3_scar
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberbead3_scar
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/vision/sabertrail
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/sabertrail
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
		glow
	}
}

gfx/vision/sabertrail_scar
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/sabertrail_scar
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// the melt pool where a blade rests in the wall, and the crater it leaves
gfx/vision/saberpool
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberpool
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		glow
	}
}

gfx/vision/saberpool_scar
{
	polygonOffset
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		clampmap gfx/vision/saberpool_scar
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// the flipbooks of the effects (tools/materials/flipbooks.py, keyword frames of the .efx, rd-vulkan-vision only:
// effects/vision/fb). Smoke and dust blended by their alpha, lit by the level there (v_litParticles); the fireball
// added. The vertices tint and fade them.
gfx/vision/fb/smoke
{
	cull	twosided
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/fb/smoke
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/vision/fb/dust
{
	cull	twosided
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/fb/dust
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/vision/fb/fireball
{
	cull	twosided
	surfaceparm	nomarks
	surfaceparm	trans
	q3map_nolightmap
	{
		map gfx/vision/fb/fireball
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

gfx/vision/impact_dust
{
	nopicmip
	nomipmaps
	surfaceparm	nomarks
	surfaceparm	trans
	cull	none
	q3map_nolightmap
	{
		clampmap gfx/vision/impact_dust
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// the leaves a Force Push or an explosion throws up from a floor of plants (cgame/level/cg_gust.c): four shapes in a
// sheet, tinted by the game with the light of the place
gfx/vision/leaves
{
	nopicmip
	surfaceparm	nomarks
	surfaceparm	trans
	cull	none
	q3map_nolightmap
	{
		map gfx/vision/leaves
		alphaFunc GE128
		rgbGen vertex
	}
}
