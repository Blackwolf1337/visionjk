#version 460

// The shadow maps of a device without ray tracing (vk_rt.cpp VK_SM_DrawAtlas): the skinned positions of a player
// (in the space of his entity) into a tile of the atlas, depth only. push.mvp: the view of the tile times the place
// of the entity.

layout( location = 0 ) in vec3 a_position;

layout( push_constant ) uniform Push {
	mat4	mvp;
} push;

void main() {
	gl_Position = push.mvp * vec4( a_position, 1.0 );
}
