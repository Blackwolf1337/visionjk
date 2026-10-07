gfx/2d/altdim
{
	qer_editorimage	gfx/2d/droid_view
	nomipmaps
	cull	twosided
    {
        map textures/altdim1
        blendFunc GL_ONE GL_ONE
        glow
        tcMod rotate 5
        tcMod stretch sin 1.4 0.05 1 0.1        
        rgbGen wave noise 0.8 0.7 0 0.8
    }
    {
        map textures/altdim2
        blendFunc GL_ONE_MINUS_DST_COLOR GL_ONE
        tcMod stretch sin 1.4 0.07 1 0.1
        tcMod rotate 2
        rgbGen wave noise 0.34 0.31 0 0.8
    }
}