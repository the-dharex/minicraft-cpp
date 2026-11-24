#version 330 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec2 v_uv;
layout(binding = 0) uniform sampler2D u_scene_tex;
layout(location = 0) out vec4 frag_color;

void main() {
  frag_color = texture(u_scene_tex, v_uv);
}
