// The shaders are compiled from these sources twice. As GLSL 4.60 for Vulkan: glslc takes the files as they are,
// with the first line of each .vert and .frag. As GLSL ES 3.00 for the device on WebGL2: there the includes are
// put in and that first line is taken out while building (cmake/Modules/BundleShader.cmake), and the device puts
// gles_prefix.glsl in front, which defines VK_GLES. What the two write differently is a macro here, in
// common.glsl or in post.glsl.

#ifdef VK_GLES
// the device gives blocks and samplers their place by name, and a stage finds its varyings by name
#define BINDING( set_, binding_ )
#define VARYING( location_ )
#define VERTEX_INDEX	gl_VertexID
// what a block is laid out as when it does not say
layout( std140 ) uniform;
#else
#define BINDING( set_, binding_ )	layout( set = set_, binding = binding_ )
#define VARYING( location_ )		layout( location = location_ )
#define VERTEX_INDEX	gl_VertexIndex
#endif
