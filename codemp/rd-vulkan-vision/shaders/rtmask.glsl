// For the passes that draw the masks of cg_shadows 2 (shaders/rtshadow.frag, smshadow.frag, lightmask.glsl), after
// W names globals.rt.

// Closer to the eye than a wall can be (the hull of the player keeps those 15 units away): the weapon in first person.
// Its depth is squeezed in front of everything (RF_DEPTHHACK), so it would seem to lie inside the head of the player,
// whose own body casts shadows too, and go dark.
#define VIEW_WEAPON_RANGE	12.0

// The pixel of the frame this pixel of the mask stands for: the masks of the rays and of the moving lights are drawn at
// half the size of the frame (W[15].z 2, vk_rt.cpp), each pixel for the upper left one of its four.
// (MASK_COORD: where a shader draws more than one mask into its target, lightmask.glsl)
#ifndef MASK_COORD
#define MASK_COORD	gl_FragCoord.xy
#endif
#ifndef MASK_SCALE
#define MASK_SCALE	W[15].z
#endif
vec2 FramePixel() {
	return floor( MASK_COORD ) * MASK_SCALE + 0.5;
}
