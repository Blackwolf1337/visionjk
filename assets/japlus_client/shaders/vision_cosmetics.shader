// The shaders of the hats of jaPRO and TaystJK that need more than their picture (models/cosmetics/README.txt).
// Taken from TaystJK's assets/japro/shaders/japro_hats.shader, 2026-10-17.

models/cosmetics/hats/cap
{
q3map_nolightmap
cull	twosided
{
map models/cosmetics/hats/cap
rgbGen lightingDiffuse
}
}

models/cosmetics/hats/cringe
{
q3map_nolightmap
cull	twosided
{
map models/cosmetics/hats/cringe
rgbGen lightingDiffuse
}
}

models/cosmetics/hats/cringe_prop
{
q3map_nolightmap
cull	twosided
{
map models/cosmetics/hats/cringe_prop
alphaFunc GE128
rgbGen vertex
tcMod rotate 1000
}
}
