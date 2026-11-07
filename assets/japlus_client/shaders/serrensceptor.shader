// Shader for ixion saber

models/weapons2/saber_serren/Saber
{
// Diffuse texture settings

// Specularity texture settings

	q3map_nolightmap
    {
        map models/weapons2/saber_serren/saber
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuse
    }
    {
        map models/weapons2/saber_serren/saber_spec
        blendFunc GL_SRC_ALPHA GL_ONE
        detail
        alphaGen lightingSpecular
    }
    {
        map models/weapons2/saber_serren/saber_enviro
        blendFunc GL_DST_COLOR GL_SRC_COLOR
        tcGen environment
    }
}

models/weapons2/saber_serren/extras
{
// Diffuse texture settings

// Specularity texture settings

	q3map_nolightmap
    {
        map models/weapons2/saber_serren/extras
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuse
    }
    {
        map models/weapons2/saber_serren/extras_spec
        blendFunc GL_SRC_ALPHA GL_ONE
        detail
        alphaGen lightingSpecular
    }
    {
        map models/weapons2/saber_serren/saber_enviro
        blendFunc GL_DST_COLOR GL_SRC_COLOR
        tcGen environment
    }
}

models/weapons2/saber_serren/extras2
{
// Diffuse texture settings

// Specularity texture settings

	q3map_nolightmap
    {
        map models/weapons2/saber_serren/extras2
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuse
    }
    {
        map models/weapons2/saber_serren/extras2_spec
        blendFunc GL_SRC_ALPHA GL_ONE
        detail
        alphaGen lightingSpecular
    }
    {
        map models/weapons2/saber_serren/saber_enviro
        blendFunc GL_DST_COLOR GL_SRC_COLOR
        tcGen environment
    }
}

models/weapons2/saber_serren/saber_glow
{
	qer_editorimage	models/weapons2/saber_serren/saber_glow
	surfaceparm	nomarks
	surfaceparm	nonsolid
	q3map_nolightmap
    {
        map models/weapons2/saber_serren/saber_glow
        rgbGen wave random 0 1 0 2
        tcMod scroll 0 0.5
    }
}

models/weapons2/saber_serren/saber_glow2
{
	qer_editorimage	models/weapons2/saber_serren/saber_glow2
	qer_trans	0.5
	surfaceparm	nonsolid
	surfaceparm	nonopaque
	surfaceparm	trans
	q3map_material	Glass
	q3map_novertexshadows
    {
        map models/weapons2/saber_serren/saber_glow2
        blendFunc GL_SRC_ALPHA GL_SRC_COLOR
        rgbGen wave inversesawtooth 0 1 0 2
    }
}

