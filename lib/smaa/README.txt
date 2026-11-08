SMAA, the anti aliasing of v_smaa that works on the finished picture of the level
(codemp/rd-vulkan-vision/shaders/smaa*.frag, vk_cmd.cpp VK_SmaaPass).

MIT (LICENSE.txt), by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez.
https://github.com/iryoku/smaa

The copies are SMAA.hlsl, Textures/AreaTex.h, Textures/SearchTex.h and LICENSE.txt of commit
71c806a838bdd7d517df19192a20f0c61b3ca29d (2013-11-06), SHA256 as downloaded (CRLF line ends, which git turns
into LF in the repository):

  SMAA.hlsl    4db53d92ef0b45661f8450303589ffabce93c352cae4282a6f32dd2b80efbeb9
  AreaTex.h    1933bf43ce86ba71f7ed5f0fa94b148610c634dc3395b9bc67a817d51ee73af2
  SearchTex.h  1fac315ab5f87b60083c9b8387af17f056204d3d1bf21500f2acc5b3e8c1a37a
  LICENSE.txt  2b848b67ba50ac40b53e11cf1ccdaee2a190ca83b4652cba695a5f1ce81f52ae

They are not changed. SMAA.hlsl is compiled as GLSL 4 (SMAA_GLSL_4) by glslc for the Vulkan device only, the
two headers are the lookup textures it reads. To update: replace them with the files of a newer commit.
