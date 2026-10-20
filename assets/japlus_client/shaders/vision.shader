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

// The boombox (codemp/cgame/boombox/cg_boombox.c; model and pictures from tools/boombox/build.py). The box, whose
// dial and lights glow where the renderer has the dynamic glow:
models/map_objects/vision/boombox
{
	{
		map models/map_objects/vision/boombox
		rgbGen lightingDiffuse
	}
	{
		map models/map_objects/vision/boombox_lights
		blendFunc GL_ONE GL_ONE
		glow
	}
}

// its handle, knobs and antenna: chrome, which shows what it reflects
models/map_objects/vision/boombox_chrome
{
	{
		map models/map_objects/vision/boombox_env
		tcGen environment
		rgbGen lightingDiffuse
	}
}

// its two speakers (boombox_cone.md3, stretched with the song by the cgame)
models/map_objects/vision/boombox_cone
{
	{
		map models/map_objects/vision/boombox_cone
		rgbGen lightingDiffuse
	}
}

// The sound system of the boombox (models and pictures from tools/boombox/build_system.py): the DJ table, whose
// front and mixer glow where the renderer has the dynamic glow,
models/map_objects/vision/sound_desk
{
	{
		map models/map_objects/vision/sound_desk
		rgbGen lightingDiffuse
	}
	{
		map models/map_objects/vision/sound_desk_lights
		blendFunc GL_ONE GL_ONE
		glow
	}
}

// a record on its platter, turned by the cgame while a song plays,
models/map_objects/vision/sound_platter
{
	{
		map models/map_objects/vision/sound_platter
		rgbGen lightingDiffuse
	}
}

// and the cabinet of a speaker, whose cones are the one of the boombox, larger
models/map_objects/vision/sound_speaker
{
	{
		map models/map_objects/vision/sound_speaker
		rgbGen lightingDiffuse
	}
	{
		map models/map_objects/vision/sound_speaker_lights
		blendFunc GL_ONE GL_ONE
		glow
	}
}
