

gfx/menus/jediacademy_japlus
{
	//nopicmip
	//nomipmaps
    {
        map gfx/menus/jediacademy
        rgbGen vertex
    }
    
    {
        map gfx/menus/env_new_logo
        blendFunc GL_ONE_MINUS_SRC_ALPHA GL_SRC_COLOR
        detail
        alphaGen const 0.25
        tcMod scroll -0.1 0.1
        tcMod scale 2 2
    }
    {
        map gfx/menus/jediacademy
        blendFunc GL_SRC_ALPHA GL_SRC_COLOR
        detail
    }
}

gfx/menus/menu_side_text_japlus
{
    {
        map gfx/menus/menu_side_text_b
        blendFunc GL_ONE_MINUS_DST_ALPHA GL_SRC_COLOR
        rgbGen identityLighting
    }
    {
        map gfx/menus/env_new_logo
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_COLOR
        detail
        alphaGen const 0.25
        tcMod scale 0.5 0.2
    }
    {
        map gfx/menus/menu_side_text
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_COLOR
        detail
        rgbGen identityLighting
        tcMod scroll 0 0.025
    }
}

gfx/menus/menu_side_text_right_japlus
{
    {
        map gfx/menus/menu_side_text_b
        blendFunc GL_ONE_MINUS_DST_ALPHA GL_SRC_COLOR
        rgbGen identityLighting
    }
    {
        map gfx/menus/env_new_logo
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_COLOR
        detail
        alphaGen const 0.25
        tcMod scale 0.3 0.5
    }
    {
        map gfx/menus/menu_side_text_right
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_COLOR
        detail
        rgbGen identityLighting
        tcMod scroll 0 0.025
    }
}

