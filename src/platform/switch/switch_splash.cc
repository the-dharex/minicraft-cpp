#include "platform/switch/switch_splash.h"

#include <glad/glad.h>

#include <algorithm>

#include "platform/switch/switch_clock.h"
#include "platform/switch/switch_window.h"
#include "stb_image.h"

namespace minicraft {
namespace platform {
namespace nx {

namespace {

constexpr const char* kSplashVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
  vUv = aUv;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kSplashFragmentShader = R"(
#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
  FragColor = texture(uTex, vUv);
}
)";

GLuint CompileSplashShader(GLenum type, const char* source) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint LinkSplashProgram() {
  GLuint vs = CompileSplashShader(GL_VERTEX_SHADER, kSplashVertexShader);
  GLuint fs = CompileSplashShader(GL_FRAGMENT_SHADER, kSplashFragmentShader);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
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
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

}  // namespace

SwitchSplashScreen::SwitchSplashScreen(SwitchWindow* window,
                                       const SwitchClock* clock)
    : window_(window), clock_(clock) {}

SwitchSplashScreen::~SwitchSplashScreen() {
  Destroy();
}

bool SwitchSplashScreen::Show(const std::vector<uint8_t>& png_bytes) {
  if (!window_ || png_bytes.empty()) return false;

  int w = 0;
  int h = 0;
  int channels = 0;
  stbi_uc* rgba = stbi_load_from_memory(
      png_bytes.data(), static_cast<int>(png_bytes.size()), &w, &h, &channels,
      4);
  if (!rgba || w <= 0 || h <= 0) {
    if (rgba) stbi_image_free(rgba);
    return false;
  }

  program_ = LinkSplashProgram();
  if (!program_) {
    stbi_image_free(rgba);
    return false;
  }

  width_ = w;
  height_ = h;

  const float vertices[] = {
      -1.0f, -1.0f, 0.0f, 1.0f,  1.0f, -1.0f, 1.0f, 1.0f,
       1.0f,  1.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f,
       1.0f,  1.0f, 1.0f, 0.0f, -1.0f,  1.0f, 0.0f, 0.0f,
  };

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba);
  stbi_image_free(rgba);

  RenderFrame();
  window_->PollEvents();
  shown_at_ms_ = clock_ ? clock_->NowMilliseconds() : 0;
  return true;
}

void SwitchSplashScreen::RenderFrame() {
  if (!window_ || !program_ || !texture_) return;

  int32_t fb_w = 0;
  int32_t fb_h = 0;
  window_->GetFramebufferSize(&fb_w, &fb_h);
  if (fb_w <= 0 || fb_h <= 0) return;

  const int32_t y_offset = std::max(0, window_->SurfaceHeight() - fb_h);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, y_offset, fb_w, fb_h);
  glViewport(0, y_offset, fb_w, fb_h);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);

  // Fill the full console display proportionally to hide loading cleanly.
  const float scale =
      std::min(static_cast<float>(fb_w) / static_cast<float>(width_),
               static_cast<float>(fb_h) / static_cast<float>(height_));
  const int32_t vp_w = std::max(1, static_cast<int32_t>(width_ * scale));
  const int32_t vp_h = std::max(1, static_cast<int32_t>(height_ * scale));
  const int32_t vp_x = (fb_w - vp_w) / 2;
  const int32_t vp_y = (fb_h - vp_h) / 2;

  glViewport(vp_x, y_offset + vp_y, vp_w, vp_h);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
  glDisable(GL_BLEND);

  window_->SwapBuffers();
}

void SwitchSplashScreen::WaitRemaining(int32_t min_duration_ms) {
  if (!window_ || !clock_ || !IsActive() || min_duration_ms <= 0) return;

  while (!window_->ShouldClose()) {
    const int64_t elapsed = clock_->NowMilliseconds() - shown_at_ms_;
    if (elapsed >= min_duration_ms) break;

    window_->PollEvents();
    const GamepadState pad = window_->PollGamepad();
    if (pad.buttons[static_cast<std::size_t>(GamepadButton::kA)] ||
        pad.buttons[static_cast<std::size_t>(GamepadButton::kB)] ||
        pad.buttons[static_cast<std::size_t>(GamepadButton::kStart)]) {
      break;
    }

    RenderFrame();
    clock_->SleepNanoseconds(16'000'000);
  }
}

void SwitchSplashScreen::Destroy() {
  if (texture_) {
    glDeleteTextures(1, &texture_);
    texture_ = 0;
  }
  if (vbo_) {
    glDeleteBuffers(1, &vbo_);
    vbo_ = 0;
  }
  if (vao_) {
    glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
  }
  if (program_) {
    glDeleteProgram(program_);
    program_ = 0;
  }
}

}  // namespace nx
}  // namespace platform
}  // namespace minicraft
