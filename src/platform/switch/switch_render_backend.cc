#include "platform/switch/switch_render_backend.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include "platform/switch/precompiled_shaders.h"
#include "platform/switch/switch_window.h"

namespace minicraft {
namespace platform {
namespace nx {

namespace {

constexpr size_t kMaxBatchVertices = 65536;

GLuint CompileShaderGlsl(GLenum type, const char *source) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::cerr << "GLSL compile error: " << log << "\n";
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint CompileShaderSpirv(GLenum type, const uint8_t *spv_data,
                          size_t spv_size) {
  GLuint s = glCreateShader(type);
  glShaderBinary(1, &s, GL_SHADER_BINARY_FORMAT_SPIR_V, spv_data,
                 static_cast<GLsizei>(spv_size));
  glSpecializeShader(s, "main", 0, nullptr, nullptr);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::cerr << "SPIR-V specialization error: " << log << "\n";
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint LinkProgramFromPrecompiled(const char *vs_name, const char *fs_name,
                                  bool use_spirv) {
  const PrecompiledShader *vs_res = FindPrecompiledShader(vs_name);
  const PrecompiledShader *fs_res = FindPrecompiledShader(fs_name);
  if (!vs_res || !fs_res) {
    std::cerr << "Missing precompiled shader resource: " << vs_name << " / "
              << fs_name << "\n";
    return 0;
  }

  GLuint vs = use_spirv
                  ? CompileShaderSpirv(GL_VERTEX_SHADER, vs_res->spv_data,
                                       vs_res->spv_size)
                  : CompileShaderGlsl(GL_VERTEX_SHADER, vs_res->glsl_fallback);
  GLuint fs =
      use_spirv ? CompileShaderSpirv(GL_FRAGMENT_SHADER, fs_res->spv_data,
                                     fs_res->spv_size)
                : CompileShaderGlsl(GL_FRAGMENT_SHADER, fs_res->glsl_fallback);
  if (!vs || !fs) {
    if (vs)
      glDeleteShader(vs);
    if (fs)
      glDeleteShader(fs);
    return 0;
  }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    std::cerr << "Shader program link error (" << vs_name << " + " << fs_name
              << "): " << log << "\n";
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

inline void UnpackRgba(uint32_t rgba, float *r, float *g, float *b, float *a) {
  *r = static_cast<float>((rgba >> 24) & 0xFFU) / 255.0f;
  *g = static_cast<float>((rgba >> 16) & 0xFFU) / 255.0f;
  *b = static_cast<float>((rgba >> 8) & 0xFFU) / 255.0f;
  *a = static_cast<float>(rgba & 0xFFU) / 255.0f;
}

} // namespace

SwitchRenderBackend::SwitchRenderBackend(const SwitchWindow *window)
    : window_(window) {}

SwitchRenderBackend::~SwitchRenderBackend() { Shutdown(); }

bool SwitchRenderBackend::Init() {
  if (initialized_)
    return true;

  auto link_all = [this](bool use_spirv) {
    sprite_prog_ =
        LinkProgramFromPrecompiled("sprite.vert", "sprite.frag", use_spirv);
    light_prog_ =
        LinkProgramFromPrecompiled("light.vert", "light.frag", use_spirv);
    dither_prog_ =
        LinkProgramFromPrecompiled("quad.vert", "dither.frag", use_spirv);
    blit_prog_ =
        LinkProgramFromPrecompiled("quad.vert", "blit.frag", use_spirv);
    return sprite_prog_ && light_prog_ && dither_prog_ && blit_prog_;
  };

  const bool spirv_capable = GLAD_GL_VERSION_4_6 && glShaderBinary != nullptr &&
                             glSpecializeShader != nullptr;
  using_spirv_ = spirv_capable && link_all(true);
  if (!using_spirv_) {
    if (sprite_prog_) {
      glDeleteProgram(sprite_prog_);
      sprite_prog_ = 0;
    }
    if (light_prog_) {
      glDeleteProgram(light_prog_);
      light_prog_ = 0;
    }
    if (dither_prog_) {
      glDeleteProgram(dither_prog_);
      dither_prog_ = 0;
    }
    if (blit_prog_) {
      glDeleteProgram(blit_prog_);
      blit_prog_ = 0;
    }
    if (!link_all(false)) {
      Shutdown();
      return false;
    }
  }

  if (using_spirv_) {
    u_sprite_screen_size_ = 0;
    u_sprite_tex_ = -1; // Bound statically via layout(binding = 0)
    u_light_screen_size_ = 0;
    u_dither_light_tex_ = -1;
    u_dither_offset_ = 0;
    u_dither_darkness_alpha_ = 1;
    u_dither_screen_size_ = 2;
    u_blit_scene_tex_ = -1;
  } else {
    u_sprite_screen_size_ = glGetUniformLocation(sprite_prog_, "u_screen_size");
    u_sprite_tex_ = glGetUniformLocation(sprite_prog_, "u_tex");
    u_light_screen_size_ = glGetUniformLocation(light_prog_, "u_screen_size");
    u_dither_light_tex_ = glGetUniformLocation(dither_prog_, "u_light_tex");
    u_dither_offset_ = glGetUniformLocation(dither_prog_, "u_offset");
    u_dither_darkness_alpha_ =
        glGetUniformLocation(dither_prog_, "u_darkness_alpha");
    u_dither_screen_size_ = glGetUniformLocation(dither_prog_, "u_screen_size");
    u_blit_scene_tex_ = glGetUniformLocation(blit_prog_, "u_scene_tex");
  }

  glGenVertexArrays(1, &batch_vao_);
  glGenBuffers(1, &batch_vbo_);
  glBindVertexArray(batch_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, batch_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kMaxBatchVertices * sizeof(Vertex), nullptr,
               GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, r)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, mode)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, has_tint)));
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, tr)));

  const float quad_verts[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
      1.0f,  1.0f,  1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
      1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
  };
  glGenVertexArrays(1, &quad_vao_);
  glGenBuffers(1, &quad_vbo_);
  glBindVertexArray(quad_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glBindVertexArray(0);

  glGenVertexArrays(1, &light_vao_);
  glGenBuffers(1, &light_vbo_);
  glBindVertexArray(light_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, light_vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void *>(4 * sizeof(float)));
  glBindVertexArray(0);

  // Create Scene FBO (kScreenWidth x kScreenHeight).
  glGenFramebuffers(1, &scene_fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
  glGenTextures(1, &scene_tex_);
  glBindTexture(GL_TEXTURE_2D, scene_tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kScreenWidth, kScreenHeight, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         scene_tex_, 0);

  // Create Lightmap FBO (kScreenWidth x kScreenHeight).
  glGenFramebuffers(1, &light_fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, light_fbo_);
  glGenTextures(1, &light_tex_);
  glBindTexture(GL_TEXTURE_2D, light_tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kScreenWidth, kScreenHeight, 0, GL_RED,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         light_tex_, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  vertices_.reserve(16384);
  initialized_ = true;
  return true;
}

void SwitchRenderBackend::Shutdown() {
  for (auto &kv : custom_textures_) {
    if (kv.second.gl_id)
      glDeleteTextures(1, &kv.second.gl_id);
  }
  custom_textures_.clear();
  if (atlas_tex_.gl_id) {
    glDeleteTextures(1, &atlas_tex_.gl_id);
    atlas_tex_.gl_id = 0;
  }
  if (scene_tex_) {
    glDeleteTextures(1, &scene_tex_);
    scene_tex_ = 0;
  }
  if (scene_fbo_) {
    glDeleteFramebuffers(1, &scene_fbo_);
    scene_fbo_ = 0;
  }
  if (light_tex_) {
    glDeleteTextures(1, &light_tex_);
    light_tex_ = 0;
  }
  if (light_fbo_) {
    glDeleteFramebuffers(1, &light_fbo_);
    light_fbo_ = 0;
  }
  if (batch_vbo_) {
    glDeleteBuffers(1, &batch_vbo_);
    batch_vbo_ = 0;
  }
  if (batch_vao_) {
    glDeleteVertexArrays(1, &batch_vao_);
    batch_vao_ = 0;
  }
  if (quad_vbo_) {
    glDeleteBuffers(1, &quad_vbo_);
    quad_vbo_ = 0;
  }
  if (quad_vao_) {
    glDeleteVertexArrays(1, &quad_vao_);
    quad_vao_ = 0;
  }
  if (light_vbo_) {
    glDeleteBuffers(1, &light_vbo_);
    light_vbo_ = 0;
  }
  if (light_vao_) {
    glDeleteVertexArrays(1, &light_vao_);
    light_vao_ = 0;
  }
  if (sprite_prog_) {
    glDeleteProgram(sprite_prog_);
    sprite_prog_ = 0;
  }
  if (light_prog_) {
    glDeleteProgram(light_prog_);
    light_prog_ = 0;
  }
  if (dither_prog_) {
    glDeleteProgram(dither_prog_);
    dither_prog_ = 0;
  }
  if (blit_prog_) {
    glDeleteProgram(blit_prog_);
    blit_prog_ = 0;
  }
  initialized_ = false;
}

bool SwitchRenderBackend::UploadAtlas(int32_t width, int32_t height,
                                      const uint8_t *rgba) {
  if (!initialized_ || width <= 0 || height <= 0 || !rgba)
    return false;
  if (!atlas_tex_.gl_id) {
    glGenTextures(1, &atlas_tex_.gl_id);
  }
  atlas_tex_.width = width;
  atlas_tex_.height = height;
  glBindTexture(GL_TEXTURE_2D, atlas_tex_.gl_id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return true;
}

uint16_t SwitchRenderBackend::CreateTexture(int32_t width, int32_t height,
                                            const uint8_t *rgba) {
  if (!initialized_ || width <= 0 || height <= 0 || !rgba)
    return 0;
  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  uint16_t tex_id = next_custom_tex_id_++;
  if (tex_id == 0)
    tex_id = next_custom_tex_id_++;
  custom_textures_[tex_id] = {id, width, height};
  return tex_id;
}

void SwitchRenderBackend::DestroyTexture(uint16_t texture_id) {
  auto it = custom_textures_.find(texture_id);
  if (it != custom_textures_.end()) {
    if (it->second.gl_id)
      glDeleteTextures(1, &it->second.gl_id);
    custom_textures_.erase(it);
  }
}

void SwitchRenderBackend::PushQuad(float x0, float y0, float x1, float y1,
                                   float u0, float v0, float u1, float v1,
                                   float r, float g, float b, float a,
                                   float mode, float has_tint, float tr,
                                   float tg, float tb) {
  if (vertices_.size() + 6 > kMaxBatchVertices) {
    FlushBatch(0);
  }
  vertices_.push_back({x0, y0, u0, v0, r, g, b, a, mode, has_tint, tr, tg, tb});
  vertices_.push_back({x1, y0, u1, v0, r, g, b, a, mode, has_tint, tr, tg, tb});
  vertices_.push_back({x1, y1, u1, v1, r, g, b, a, mode, has_tint, tr, tg, tb});
  vertices_.push_back({x0, y0, u0, v0, r, g, b, a, mode, has_tint, tr, tg, tb});
  vertices_.push_back({x1, y1, u1, v1, r, g, b, a, mode, has_tint, tr, tg, tb});
  vertices_.push_back({x0, y1, u0, v1, r, g, b, a, mode, has_tint, tr, tg, tb});
}

void SwitchRenderBackend::FlushBatch(uint16_t texture_id) {
  if (vertices_.empty())
    return;

  GLuint gl_tex = atlas_tex_.gl_id;
  if (texture_id != 0) {
    auto it = custom_textures_.find(texture_id);
    if (it != custom_textures_.end()) {
      gl_tex = it->second.gl_id;
    }
  }

  glUseProgram(sprite_prog_);
  if (u_sprite_screen_size_ >= 0) {
    glUniform2f(u_sprite_screen_size_, static_cast<float>(kScreenWidth),
                static_cast<float>(kScreenHeight));
  }
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gl_tex);
  if (u_sprite_tex_ >= 0) {
    glUniform1i(u_sprite_tex_, 0);
  }

  glBindVertexArray(batch_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, batch_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kMaxBatchVertices * sizeof(Vertex), nullptr,
               GL_STREAM_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
                  vertices_.data());
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
  vertices_.clear();
}

void SwitchRenderBackend::ApplyLightOverlay(const FrameRenderData &frame,
                                            int32_t offset_x, int32_t offset_y,
                                            float darkness_alpha) {
  // Render lights into light_fbo_ using MAX blending.
  glBindFramebuffer(GL_FRAMEBUFFER, light_fbo_);
  glViewport(0, 0, kScreenWidth, kScreenHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  if (frame.light_count > 0 && frame.lights != nullptr) {
    struct LightVert {
      float x, y, lx, ly, radius;
    };
    std::vector<LightVert> lverts;
    lverts.reserve(frame.light_count * 6);

    for (size_t i = 0; i < frame.light_count; ++i) {
      const LightCmd &lc = frame.lights[i];
      float x = static_cast<float>(lc.x);
      float y = static_cast<float>(lc.y);
      float r = static_cast<float>(lc.radius);
      lverts.push_back({x - r, y - r, -r, -r, r});
      lverts.push_back({x + r, y - r, r, -r, r});
      lverts.push_back({x + r, y + r, r, r, r});
      lverts.push_back({x - r, y - r, -r, -r, r});
      lverts.push_back({x + r, y + r, r, r, r});
      lverts.push_back({x - r, y + r, -r, r, r});
    }

    glBindVertexArray(light_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, light_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(lverts.size() * sizeof(LightVert)),
                 lverts.data(), GL_STREAM_DRAW);

    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    glUseProgram(light_prog_);
    if (u_light_screen_size_ >= 0) {
      glUniform2f(u_light_screen_size_, static_cast<float>(kScreenWidth),
                  static_cast<float>(kScreenHeight));
    }
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(lverts.size()));
    glBlendEquation(GL_FUNC_ADD);
    glBindVertexArray(0);
  }

  // 2. Composite subtle 2% white lightening + Bayer dithered darkness over
  // scene_fbo_.
  glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
  glViewport(0, 0, kScreenWidth, kScreenHeight);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  PushQuad(0.0f, 0.0f, static_cast<float>(kScreenWidth),
           static_cast<float>(kScreenHeight), 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
           1.0f, 1.0f, 0.02f, 1.0f, 0.0f);
  FlushBatch(0);

  glUseProgram(dither_prog_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, light_tex_);
  if (u_dither_light_tex_ >= 0) {
    glUniform1i(u_dither_light_tex_, 0);
  }
  if (u_dither_offset_ >= 0) {
    glUniform2i(u_dither_offset_, offset_x, offset_y);
  }
  if (u_dither_darkness_alpha_ >= 0) {
    glUniform1f(u_dither_darkness_alpha_, darkness_alpha);
  }
  if (u_dither_screen_size_ >= 0) {
    glUniform2i(u_dither_screen_size_, kScreenWidth, kScreenHeight);
  }
  glBindVertexArray(quad_vao_);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}

void SwitchRenderBackend::RenderFrame(const FrameRenderData &frame) {
  if (!initialized_)
    return;

  glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
  glViewport(0, 0, kScreenWidth, kScreenHeight);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  float cr, cg, cb, ca;
  UnpackRgba(frame.clear_rgba, &cr, &cg, &cb, &ca);
  glClearColor(cr, cg, cb, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  vertices_.clear();
  uint16_t current_tex = 0;

  auto get_tex_dims = [this](uint16_t tex_id, float *tw, float *th) {
    if (tex_id == 0) {
      *tw = static_cast<float>(std::max(1, atlas_tex_.width));
      *th = static_cast<float>(std::max(1, atlas_tex_.height));
    } else {
      auto it = custom_textures_.find(tex_id);
      if (it != custom_textures_.end()) {
        *tw = static_cast<float>(std::max(1, it->second.width));
        *th = static_cast<float>(std::max(1, it->second.height));
      } else {
        *tw = 1.0f;
        *th = 1.0f;
      }
    }
  };

  for (size_t i = 0; i < frame.command_count; ++i) {
    const DrawCmd &cmd = frame.commands[i];

    switch (cmd.type) {
    case DrawCmdType::kSprite: {
      if (cmd.texture_id != current_tex) {
        FlushBatch(current_tex);
        current_tex = cmd.texture_id;
      }
      float tw = 1.0f, th = 1.0f;
      get_tex_dims(current_tex, &tw, &th);
      float u0 = static_cast<float>(cmd.src_x) / tw;
      float v0 = static_cast<float>(cmd.src_y) / th;
      float u1 = static_cast<float>(cmd.src_x + cmd.src_w) / tw;
      float v1 = static_cast<float>(cmd.src_y + cmd.src_h) / th;
      if (cmd.flags & kFlagMirrorX)
        std::swap(u0, u1);
      if (cmd.flags & kFlagMirrorY)
        std::swap(v0, v1);

      float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
      UnpackRgba(cmd.color_rgba, &r, &g, &b, &a);

      float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
      float has_tint = 0.0f;
      if (cmd.flags & kFlagUseWhiteTint) {
        UnpackRgba(cmd.tint_rgba, &tr, &tg, &tb, &ta);
        has_tint = 1.0f;
      }

      float mode = 0.0f;
      if (cmd.flags & kFlagFullBright) {
        mode = 2.0f;
      } else if (cmd.flags & kFlagUseSolidColor) {
        mode = 3.0f;
      }

      float x0 = static_cast<float>(cmd.x);
      float y0 = static_cast<float>(cmd.y);
      PushQuad(x0, y0, x0 + static_cast<float>(cmd.w),
               y0 + static_cast<float>(cmd.h), u0, v0, u1, v1, r, g, b, a, mode,
               has_tint, tr, tg, tb);
      break;
    }

    case DrawCmdType::kFillRect: {
      float r, g, b, a;
      UnpackRgba(cmd.color_rgba, &r, &g, &b, &a);
      float x0 = static_cast<float>(cmd.x);
      float y0 = static_cast<float>(cmd.y);
      PushQuad(x0, y0, x0 + static_cast<float>(cmd.w),
               y0 + static_cast<float>(cmd.h), 0, 0, 0, 0, r, g, b, a, 1.0f,
               0.0f);
      break;
    }

    case DrawCmdType::kDrawRect: {
      float r, g, b, a;
      UnpackRgba(cmd.color_rgba, &r, &g, &b, &a);
      float x0 = static_cast<float>(cmd.x);
      float y0 = static_cast<float>(cmd.y);
      float w = static_cast<float>(cmd.w);
      float h = static_cast<float>(cmd.h);
      // 4 pixel-crisp edges matching Java Graphics2D.drawRect(x, y, w, h).
      PushQuad(x0, y0, x0 + w + 1.0f, y0 + 1.0f, 0, 0, 0, 0, r, g, b, a, 1.0f,
               0.0f);
      PushQuad(x0, y0 + h, x0 + w + 1.0f, y0 + h + 1.0f, 0, 0, 0, 0, r, g, b, a,
               1.0f, 0.0f);
      PushQuad(x0, y0, x0 + 1.0f, y0 + h + 1.0f, 0, 0, 0, 0, r, g, b, a, 1.0f,
               0.0f);
      PushQuad(x0 + w, y0, x0 + w + 1.0f, y0 + h + 1.0f, 0, 0, 0, 0, r, g, b, a,
               1.0f, 0.0f);
      break;
    }

    case DrawCmdType::kDrawLine: {
      float r, g, b, a;
      UnpackRgba(cmd.color_rgba, &r, &g, &b, &a);
      int x0 = cmd.x, y0 = cmd.y, x1 = cmd.w, y1 = cmd.h;
      int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
      int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
      int err = dx + dy;
      for (;;) {
        PushQuad(static_cast<float>(x0), static_cast<float>(y0),
                 static_cast<float>(x0 + 1), static_cast<float>(y0 + 1), 0, 0,
                 0, 0, r, g, b, a, 1.0f, 0.0f);
        if (x0 == x1 && y0 == y1)
          break;
        int e2 = 2 * err;
        if (e2 >= dy) {
          err += dy;
          x0 += sx;
        }
        if (e2 <= dx) {
          err += dx;
          y0 += sy;
        }
      }
      break;
    }

    case DrawCmdType::kInvertLine: {
      FlushBatch(current_tex);
      int32_t rx0 = std::max(0, cmd.x);
      int32_t ry0 = std::max(0, cmd.y);
      int32_t rx1 = std::min(kScreenWidth, cmd.x + cmd.w);
      int32_t ry1 = std::min(kScreenHeight, cmd.y + cmd.h);
      int32_t rw = rx1 - rx0;
      int32_t rh = ry1 - ry0;
      if (rw <= 0 || rh <= 0)
        break;

      std::vector<uint8_t> pixels(static_cast<size_t>(rw) * rh * 4);
      glReadPixels(rx0, kScreenHeight - ry1, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE,
                   pixels.data());
      for (int32_t py = 0; py < rh; ++py) {
        float sy = static_cast<float>(ry1 - 1 - py);
        for (int32_t px = 0; px < rw; ++px) {
          size_t idx = (static_cast<size_t>(py) * rw + px) * 4;
          uint8_t pr = pixels[idx + 0];
          uint8_t pg = pixels[idx + 1];
          uint8_t pb = pixels[idx + 2];
          uint8_t max_c = std::max({pr, pg, pb});
          uint8_t min_c = std::min({pr, pg, pb});
          float c = (static_cast<float>(max_c + min_c) / 510.0f >= 0.5f) ? 0.0f
                                                                         : 1.0f;
          float sx = static_cast<float>(rx0 + px);
          PushQuad(sx, sy, sx + 1.0f, sy + 1.0f, 0, 0, 0, 0, c, c, c, 1.0f,
                   1.0f, 0.0f);
        }
      }
      FlushBatch(current_tex);
      break;
    }

    case DrawCmdType::kApplyLightOverlay: {
      FlushBatch(current_tex);
      ApplyLightOverlay(frame, cmd.w, cmd.h, cmd.extra_float);
      break;
    }
    }
  }
  FlushBatch(current_tex);

  // Present FBO to window framebuffer with exact full-screen GL_NEAREST
  // scaling on 16:9 Switch displays (4x at 1280x720, 6x at 1920x1080).
  int32_t fb_w = 0, fb_h = 0;
  window_->GetFramebufferSize(&fb_w, &fb_h);
  if (fb_w > 0 && fb_h > 0) {
    // Switch NWindow crops the top-left (fb_w x fb_h) region of the 1920x1080
    // EGL surface, while OpenGL Y=0 starts at the bottom edge of the surface.
    const int32_t y_offset = std::max(0, window_->SurfaceHeight() - fb_h);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, y_offset, fb_w, fb_h);
    glViewport(0, y_offset, fb_w, fb_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    int32_t vp_x = 0;
    int32_t vp_y = 0;
    int32_t vp_w = fb_w;
    int32_t vp_h = fb_h;
    if (static_cast<int64_t>(fb_w) * kScreenHeight !=
        static_cast<int64_t>(fb_h) * kScreenWidth) {
      float scale = std::min(static_cast<float>(fb_w) / kScreenWidth,
                             static_cast<float>(fb_h) / kScreenHeight);
      vp_w = std::max(1, static_cast<int32_t>(kScreenWidth * scale));
      vp_h = std::max(1, static_cast<int32_t>(kScreenHeight * scale));
      vp_x = (fb_w - vp_w) / 2;
      vp_y = (fb_h - vp_h) / 2;
    }

    glViewport(vp_x, y_offset + vp_y, vp_w, vp_h);
    glDisable(GL_BLEND);
    glUseProgram(blit_prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_tex_);
    if (u_blit_scene_tex_ >= 0) {
      glUniform1i(u_blit_scene_tex_, 0);
    }
    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    if (frame.capture_screenshot) {
      glViewport(0, y_offset, fb_w, fb_h);
      screenshot_w_ = fb_w;
      screenshot_h_ = fb_h;
      screenshot_rgba_.resize(static_cast<size_t>(fb_w) * fb_h * 4U);
      glReadPixels(0, y_offset, fb_w, fb_h, GL_RGBA, GL_UNSIGNED_BYTE,
                   screenshot_rgba_.data());
      // Flip vertically so row 0 is top of image.
      size_t stride = static_cast<size_t>(fb_w) * 4U;
      std::vector<uint8_t> row_tmp(stride);
      for (int32_t y = 0; y < fb_h / 2; ++y) {
        uint8_t *top =
            screenshot_rgba_.data() + static_cast<size_t>(y) * stride;
        uint8_t *bot = screenshot_rgba_.data() +
                       static_cast<size_t>(fb_h - 1 - y) * stride;
        std::memcpy(row_tmp.data(), top, stride);
        std::memcpy(top, bot, stride);
        std::memcpy(bot, row_tmp.data(), stride);
      }
    }
  }
}

bool SwitchRenderBackend::ReadBackScreenshot(int32_t *out_w, int32_t *out_h,
                                             uint8_t *out_rgba,
                                             size_t buffer_bytes) {
  if (screenshot_rgba_.empty() || !out_w || !out_h)
    return false;
  *out_w = screenshot_w_;
  *out_h = screenshot_h_;
  if (!out_rgba || buffer_bytes < screenshot_rgba_.size())
    return false;
  std::memcpy(out_rgba, screenshot_rgba_.data(), screenshot_rgba_.size());
  return true;
}

} // namespace nx
} // namespace platform
} // namespace minicraft
