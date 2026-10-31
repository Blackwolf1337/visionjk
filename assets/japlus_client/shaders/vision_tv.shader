// the screens of the TV of the boombox (issue #9, codemp/cgame/boombox/cg_tv.c): screen<n> shows the video of stream n
// of the engine (client/cl_video.cpp, "videoMap *tv<n>"), behind a faint reflection of glass; off is the dark glass of a
// TV that shows nothing. The skins models/vision/tv/screen_<n>.skin put them on the middle screen of
// models/map_objects/imperial/monitor.md3.

gfx/vision/tv/screen0
{
	nopicmip
	surfaceparm nolightmap
	{
		videoMap *tv0
		rgbGen identity
	}
	{
		map models/map_objects/vision/boombox_env
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen const ( 0.07 0.07 0.08 )
	}
}

gfx/vision/tv/screen1
{
	nopicmip
	surfaceparm nolightmap
	{
		videoMap *tv1
		rgbGen identity
	}
	{
		map models/map_objects/vision/boombox_env
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen const ( 0.07 0.07 0.08 )
	}
}

gfx/vision/tv/screen2
{
	nopicmip
	surfaceparm nolightmap
	{
		videoMap *tv2
		rgbGen identity
	}
	{
		map models/map_objects/vision/boombox_env
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen const ( 0.07 0.07 0.08 )
	}
}

gfx/vision/tv/screen3
{
	nopicmip
	surfaceparm nolightmap
	{
		videoMap *tv3
		rgbGen identity
	}
	{
		map models/map_objects/vision/boombox_env
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen const ( 0.07 0.07 0.08 )
	}
}

gfx/vision/tv/off
{
	nopicmip
	surfaceparm nolightmap
	{
		map models/map_objects/vision/boombox_env
		tcGen environment
		rgbGen const ( 0.05 0.05 0.06 )
	}
}
