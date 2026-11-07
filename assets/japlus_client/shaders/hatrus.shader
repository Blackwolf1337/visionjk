models/weapons2/proton/energy  //energy.jpg with tcMod rotate
{
{
        map models/weapons2/proton/energy
        rgbGen lightingDiffuse
    }
    {
	map models/weapons2/proton/energy
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
                    tcMod rotate -0.50 0.50
       }
}

models/weapons2/thunder/skin   //skin.jpg with chrome shader
{
{
        map models/weapons2/thunder/skin
        rgbGen lightingDiffuse
    }
    {
	map gfx/env_chrome2
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
                    tcMod scroll -0.0 0.0
       }
}

models/weapons2/thunder/gold   //gold.jpg with gold shader
{
{
        map models/weapons2/thunder/gold
        rgbGen lightingDiffuse
    }
    {
	map models/weapons2/thunder/gold
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
                    tcMod scroll -0.0 0.0
       }
}

models/weapons2/thunder/3   //skin.jpg with gold shader
{
{
        map models/weapons2/thunder/2
        rgbGen lightingDiffuse
    }
    {
	map models/weapons2/thunder/gold
	blendFunc GL_ONE GL_ONE
	tcGen environment
                    tcMod scroll -0.0 0.0
       }
}

models/weapons2/thunder/4   //skin.jpg 
{
{
        map models/weapons2/thunder/4
        rgbGen lightingDiffuse
    }
    {
	map gfx/env_chrome2
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
                    tcMod scroll -0.0 0.0
       }
}

models/weapons2/favori/logo  //logo.jpg with chrome shader
{
{
        map models/weapons2/favori/logo
       rgbGen lightingDiffuse
    }
    {
	map gfx/env_chrome2
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
                    tcMod scroll -0.0 0.0
       }
}

//2 skin.jpg with no shader



gfx/env_chrome
{
{
        map gfx/env_chrome
        rgbGen lightingDiffuse
    }
    {
	map gfx/env_chrome
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
    }
}

gfx/env_chrome2
{
{
        map gfx/env_chrome2
        rgbGen lightingDiffuse
    }
    {
	map gfx/env_chrome2
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
    }
}




gfx/eyes
{
{
        map gfx/eyes
        rgbGen lightingDiffuse
    }
    {
	map gfx/eyes
	blendFunc GL_SRC_ALPHA GL_ONE
	tcGen environment
                    tcMod scroll -0.10 0.10
       }
}
