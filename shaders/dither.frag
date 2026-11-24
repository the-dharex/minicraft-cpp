#version 330 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_explicit_uniform_location : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec2 v_uv;

layout(binding = 0) uniform sampler2D u_light_tex;
layout(location = 0) uniform ivec2 u_offset;
layout(location = 1) uniform float u_darkness_alpha;
layout(location = 2) uniform ivec2 u_screen_size;

layout(location = 0) out vec4 frag_color;

const int kBayer[16] = int[16](
  0, 8, 2, 10,
  12, 4, 14, 6,
  3, 11, 1, 9,
  15, 7, 13, 5
);

void main() {
  int px = int(gl_FragCoord.x);
  int py = (u_screen_size.y - 1) - int(gl_FragCoord.y);
  int grade = int(round(texture(u_light_tex, v_uv).r * 255.0));
  int idx = ((px + u_offset.x) & 3) + (((py + u_offset.y) & 3) * 4);
  if (grade / 10 <= kBayer[idx]) {
    float a = (float(255 - grade) / 255.0) * u_darkness_alpha;
    frag_color = vec4(0.0, 0.0, 0.0, a);
  } else {
    discard;
  }
}
