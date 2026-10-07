gfx/effects/sabers/RGBGlow
{
	cull	twosided
    {
        map gfx/effects/sabers/RGBGlow
        blendFunc GL_ONE GL_ONE
        glow
        
        rgbGen vertex
    }
}

gfx/effects/sabers/RGBLine
{
	cull	twosided
    {
        map gfx/effects/sabers/RGBline
        blendFunc GL_ONE GL_ONE
        
        rgbGen vertex
    }
}

gfx/effects/sabers/black_glow
{
	cull	twosided
    {
        clampmap gfx/effects/sabers/black_glow
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        glow
        rgbGen identity
        alphaGen vertex
    }
}

gfx/effects/sabers/black_line
{
	cull	twosided
    {
        clampmap gfx/effects/sabers/black_line
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen identity
        alphaGen vertex
    }
}

gfx/effects/sabers/blacksaberBlur
{
	cull	twosided
    {
        clampmap gfx/effects/sabers/black_trail_glow
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen identity
        alphaGen vertex
        glow
    }
    {
        clampmap gfx/effects/sabers/black_trail_core
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen identity
        alphaGen vertex
    }
}



gfx/effects/sabers/RGBcore2
{
	cull	disable
    {
        map gfx/effects/sabers/RGBcore2.tga
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
}
gfx/effects/sabers/RGBtrail2
{
    cull disable

    {
        clampmap gfx/effects/sabers/RGBblurglow2
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
    {
        clampmap gfx/effects/sabers/RGBblurcore2
        blendFunc GL_ONE GL_ONE
        rgbGen identity
    }
}


gfx/effects/sabers/RGBcore3
{
	cull	disable
    {
        map gfx/effects/sabers/RGBcore3.tga
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
}
gfx/effects/sabers/RGBtrail3
{
    cull disable

    {
        clampmap gfx/effects/sabers/RGBblurglow3
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
    {
        clampmap gfx/effects/sabers/RGBblurcore3
        blendFunc GL_ONE GL_ONE
        rgbGen identity
    }
    
}


gfx/effects/sabers/RGBcore4
{
	cull	disable
    {
        map gfx/effects/sabers/RGBcore4.tga
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
}

gfx/effects/sabers/RGBGlow4
{
	cull	twosided
    {
        map gfx/effects/sabers/RGBGlow4
        blendFunc GL_ONE GL_ONE
        glow
        
        rgbGen vertex
    }
}

gfx/effects/sabers/RGBtrail4
{
    cull disable

    {
        clampmap gfx/effects/sabers/RGBblurglow4
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
    {
        clampmap gfx/effects/sabers/RGBblurcore4
        blendFunc GL_ONE GL_ONE
        rgbGen identity
    }
    
}


gfx/effects/sabers/RGBcore5
{
	cull	disable
    {
        map gfx/effects/sabers/RGBcore5.tga
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
}


