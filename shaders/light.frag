#version 330 core
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 v_local;
layout(location = 1) in float v_radius;

layout(location = 0) out vec4 frag_color;

void main() {
  if (v_radius < 0.0) {
    frag_color = vec4(1.0);
    return;
  }
  float d = length(v_local) / max(v_radius, 0.001);
  if (d > 1.0) {
    discard;
  }
  float l = clamp(1.0 - pow(d, 4.0), 0.0, 1.0);
  frag_color = vec4(l, l, l, 1.0);
}
