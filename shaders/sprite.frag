#version 330 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_mode;
layout(location = 3) in float v_has_tint;
layout(location = 4) in vec3 v_tint;

layout(binding = 0) uniform sampler2D u_tex;

layout(location = 0) out vec4 frag_color;

void main() {
  if (v_mode > 0.5 && v_mode < 1.5) {
    frag_color = v_color;
    return;
  }
  vec4 texel = texture(u_tex, v_uv);
  if (texel.a < 0.01) {
    discard;
  }
  if (v_has_tint > 0.5 && texel.r > 0.998 && texel.g > 0.998 && texel.b > 0.998) {
    frag_color = vec4(v_tint, 1.0);
  } else if (v_mode > 2.5) {
    frag_color = vec4(v_color.rgb, 1.0);
  } else if (v_mode > 1.5) {
    frag_color = vec4(1.0);
  } else {
    frag_color = vec4(texel.rgb, 1.0);
  }
}
