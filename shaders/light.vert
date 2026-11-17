#version 330 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_local;
layout(location = 2) in float a_radius;

layout(location = 0) uniform vec2 u_screen_size;

layout(location = 0) out vec2 v_local;
layout(location = 1) out float v_radius;

void main() {
  vec2 ndc = vec2(
    (a_pos.x / u_screen_size.x) * 2.0 - 1.0,
    1.0 - (a_pos.y / u_screen_size.y) * 2.0
  );
  gl_Position = vec4(ndc, 0.0, 1.0);
  v_local = a_local;
  v_radius = a_radius;
}
