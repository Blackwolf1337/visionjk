models/players/jedi_hm/test
{
	cull	twosided

	{

	map gfx/effects/flamejet
	blendFunc GL_ONE GL_ONE
	tcGen environment
                    tcMod scroll -0.2 0.2
       }

}










models/players/jedi_hm/face_blonde_show7
{
    {
        map models/players/jedi_hm/face_blonde_show7
        blendFunc GL_ONE GL_ZERO
        //rgbGen lightingDiffuseEntity
        rgbGen lightingDiffuse
    }
    {

map gfx/effects/flamejet
	blendFunc GL_ONE GL_ONE
	tcGen environment
                    tcMod scroll -0.2 0.2
       }
    {
        map models/players/jedi_hm/face_blonde_show7
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
       rgbGen lightingDiffuse
    }
    
    
}

models/players/jedi_hm/blonde_head_show7
{
    {
        map models/players/jedi_hm/blonde_head
        blendFunc GL_ONE GL_ZERO
        //rgbGen lightingDiffuseEntity
        rgbGen lightingDiffuse
    }
    
    {
        map models/players/jedi_hm/blonde_head_show7
        blendFunc GL_ONE GL_ONE
        detail
        rgbGen wave noise 0.14 0.12 0 0.8
    }
    
    
}


models/players/jedi_hm/icon_head_sw7
{
	nopicmip
	nomipmaps
    {
        map models/players/jedi_hm/icon_head_sw7
        rgbGen identity
    }
    {
        map gfx/misc/bolt1_green
        blendFunc GL_ONE GL_ONE
        tcMod scroll 0 -5
        rgbGen wave noise 0.14 0.12 0 0.7
    }
    {
        map gfx/misc/blueline1_green
        blendFunc GL_SRC_ALPHA GL_ONE
        tcMod scroll 0 -3
        rgbGen wave noise 0.14 0.12 0 0.7
    }
    
}

























models/players/jedi_hm/vest03_show7
{
    {
        map models/players/jedi_hm/vest03
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuseEntity
    }
    {

map gfx/effects/flamejet
	blendFunc GL_ONE GL_ONE
	tcGen environment
                    tcMod scroll -0.2 0.2
       }
    {
        map models/players/jedi_hm/vest03
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
       rgbGen lightingDiffuse
    }
    {
    
        map gfx/effects/chr_white_add_mild.tga
        blendFunc GL_SRC_ALPHA GL_ONE
        detail
        tcGen environment
    	}
    
}

models/players/jedi_hm/vest03_arms_clothes_show7
{
    {
        map models/players/jedi_hm/vest03_arms
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuse
    }
    
}

models/players/jedi_hm/vest03_arms_show7
{
    {
        map models/players/jedi_hm/vest03_arms
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuse
    }
}

models/players/jedi_hm/vest03_arms2_show7
{
    {
        map models/players/jedi_hm/vest03_arms
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuse
    }
     {
		map models/players/show7/tob
		blendFunc GL_DST_COLOR GL_ONE
		blendFunc add
		rgbGen wave noise 0.2 0.7 0 1
		tcMod scroll 0.35 0.15 
		tcMod turb 0.55 0.25 0 0.15
    }
    
}


models/players/jedi_hm/icon_torso_sw7
{
	nopicmip
	nomipmaps
    {
        map models/players/jedi_hm/icon_torso_f1
        rgbGen identity
    }
    
    {
        map models/players/jedi_hm/icon_torso_f1
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen vertex
    }
    {

map gfx/effects/flamejet
	blendFunc GL_ONE GL_ONE
	tcGen environment
                    tcMod scroll -0.2 0.2
       }
    {
        map models/players/jedi_hm/icon_torso_sw7
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen vertex
    }
    
    
    {
        map models/players/jedi_hm/icon_torso_sw7_2
        blendFunc GL_ONE GL_ONE
        
    }
    
    
    
}

































models/players/jedi_hm/icon_torso_sw6
{
	nopicmip
	nomipmaps
    {
        map models/players/jedi_hm/icon_torso_c1
        rgbGen identity
    } 
    {
        map models/players/jedi_hm/icon_torso_c1
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen vertex
    }
    {
        map gfx/misc/bolt1
        blendFunc GL_ONE GL_ONE
        rgbGen wave noise 0.4 0.7 0 2
        tcMod scroll 0 -0.9
        tcMod turb 0.55 0.25 0 0.15
    }
    {
        map gfx/misc/blueline1
        blendFunc GL_SRC_ALPHA GL_ONE
        rgbGen wave noise 0.4 0.7 0 2
        tcMod scroll 0 -0.55
        tcMod turb 0.55 0.25 0 0.15
    }   
    
    
   
    {
        map models/players/jedi_hm/icon_torso_sw6
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen vertex
    }
    
    
    {
        map models/players/jedi_hm/icon_torso_sw6_2
        blendFunc GL_ONE GL_ONE
        
    }
    
    
}

models/players/jedi_hm/robes03_arms_show7_2
{
    {
        map models/players/jedi_hm/robes03_arms
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuseEntity
    }
    {
        map gfx/misc/bolt1
        blendFunc GL_ONE GL_ONE
        rgbGen wave noise 0.4 0.7 0 2
        tcMod scroll 0 -0.9
        tcMod turb 0.55 0.25 0 0.15
    }
    {
        map gfx/misc/blueline1
        blendFunc GL_SRC_ALPHA GL_ONE
        rgbGen wave noise 0.4 0.7 0 2
        tcMod scroll 0 -0.55
        tcMod turb 0.55 0.25 0 0.15
    }
    
    
    
    
    {
        map models/players/jedi_hm/robes03_arms
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen lightingDiffuse
    }
    {
        map models/players/jedi_hm/robes03_arms_show7
        blendFunc GL_ONE GL_ONE
        detail
        rgbGen wave noise 0.1 0.25 0 0.8
        
    }
    
}

models/players/jedi_hm/robes03_show7_2
{
    {
        map models/players/jedi_hm/robes03
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuseEntity
    }
    {
		map models/players/show7/tob
		blendFunc GL_DST_COLOR GL_ONE
		blendFunc add
		rgbGen wave noise 0.2 0.7 0 1
		tcMod scroll 0.35 0.15 
		tcMod turb 0.55 0.25 0 0.15
    }
   
   
    {
        map models/players/jedi_hm/robes03
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA        
        rgbGen lightingDiffuse
        
    }
    {
        map models/players/jedi_hm/robes03_show7
        blendFunc GL_ONE GL_ONE
        detail
        rgbGen wave noise 0.3 0.2 0 0.7
        
    }
    
}














models/players/jedi_hm/icon_lower_sw6
{
	nopicmip
	nomipmaps
    {
        //map models/players/jedi_hm/icon_lower_a1
        map models/players/jedi_hm/icon_lower_sw6
        rgbGen identity
    }
    {
        map gfx/misc/dr1
        blendFunc GL_ONE GL_ONE
        tcMod scale 1 0.5
        tcMod scroll 0 3
        rgbGen wave noise 0.3 0.2 0 0.7
    }
    {
        map models/players/jedi_hm/icon_lower_sw6
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen vertex
    }
}

models/players/jedi_hm/pants01_show7_2
{
    {
        map models/players/jedi_hm/pants01
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuseEntity
    }
    {
		map models/players/show7/tob_fire
		blendFunc GL_DST_COLOR GL_ONE
		blendFunc add
		rgbGen wave noise 0.2 0.7 0 1
		tcMod scroll 0.35 0.15 
		tcMod turb 0.55 0.25 0 0.15
    }  
    {
        map models/players/jedi_hm/pants01
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen lightingDiffuse
    }
    {
        map models/players/jedi_hm/pants01_show7
        blendFunc GL_ONE GL_ONE
        detail
        alphaGen lightingSpecular
        
    }
}

models/players/jedi_hm/pants01_boots_show7_2
{
    {
        map models/players/jedi_hm/pants01
        blendFunc GL_ONE GL_ZERO
        rgbGen lightingDiffuseEntity
    }
    {
		map models/players/show7/tob_fire
		blendFunc GL_DST_COLOR GL_ONE
		blendFunc add
		rgbGen wave noise 0.2 0.7 0 1
		tcMod scroll 0.35 0.15 
		tcMod turb 0.55 0.25 0 0.15
    }  
    {
        map models/players/jedi_hm/pants01
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen lightingDiffuse
    }
    {
    
        map gfx/effects/chr_white_add_mild.tga
        blendFunc GL_SRC_ALPHA GL_ONE
        detail
        tcGen environment
    	}
    	{
        map models/players/jedi_hm/pants01_show7
        blendFunc GL_ONE GL_ONE
        detail
        alphaGen lightingSpecular
        
    }
}