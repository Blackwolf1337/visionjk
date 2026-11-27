#version 460

// The saber blades of the view of the level that stand behind thick glass (vk_backend.cpp RB_GlassLayers), added
// before that glass is drawn, so the pane shows them moved with the hand that holds them. See saber.glsl.

#define EARLY_PASS
#include "saber.glsl"
