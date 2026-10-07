models/weapons2/saber_caladien/skin
{
	q3map_nolightmap
	q3map_onlyvertexlighting	
	qer_editorimage	models/weapons2/saber_caladien/skin
    {
        map models/weapons2/saber_caladien/skin
        blendFunc GL_ONE GL_ZERO
        tcMod scale 3 1
    }
    {
        map models/weapons2/saber_caladien/skin
        blendFunc GL_ONE GL_ONE
        rgbGen wave sin 0.75 0.15 0 8
        tcMod scale 3 1
   }
    {
        map models/weapons2/saber_caladien/hilt_shockrip
        blendFunc GL_ONE GL_ONE
        rgbGen wave sin 1 0.25 0 8
        tcMod scroll 0 0.1
        tcMod scale 10 10
    }
    {
        map models/weapons2/saber_caladien/skin
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
	rgbGen lightingDiffuse
    }
{
        map gfx/effects/chr_inv
        blendFunc GL_DST_COLOR GL_SRC_COLOR
        tcGen environment
    }
    {
        map models/weapons2/saber_caladien/skin
        blendFunc GL_SRC_ALPHA GL_ONE
        detail
        alphaGen lightingSpecular
    }
    {
        map $lightmap
        blendFunc GL_DST_COLOR GL_SRC_COLOR
    }
}
