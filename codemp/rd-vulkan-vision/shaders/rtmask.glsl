// For the passes that draw the masks of cg_shadows 2 (shaders/rtshadow.frag, smshadow.frag, lightmask.glsl), after
// W names globals.rt.

// The pixel of the frame this pixel of the mask stands for: the masks of the rays and of the moving lights are drawn at
// half the size of the frame (W[15].z 2, vk_rt.cpp), each pixel for the upper left one of its four.
vec2 FramePixel() {
	return floor( gl_FragCoord.xy ) * W[15].z + 0.5;
}
