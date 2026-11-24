#version 330 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_mode;
layout(location = 4) in float a_has_tint;
layout(location = 5) in vec3 a_tint;

layout(location = 0) uniform vec2 u_screen_size;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_mode;
layout(location = 3) out float v_has_tint;
layout(location = 4) out vec3 v_tint;

void main() {
  vec2 ndc = vec2(
    (a_pos.x / u_screen_size.x) * 2.0 - 1.0,
    1.0 - (a_pos.y / u_screen_size.y) * 2.0
  );
  gl_Position = vec4(ndc, 0.0, 1.0);
  v_uv = a_uv;
  v_color = a_color;
  v_mode = a_mode;
  v_has_tint = a_has_tint;
  v_tint = a_tint;
}
