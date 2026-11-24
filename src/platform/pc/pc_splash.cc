#include "platform/pc/pc_splash.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <thread>

#include "stb_image.h"

namespace minicraft {
namespace platform {
namespace pc {

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

GLFWwindow* CreateSplashWindow(int32_t w, int32_t h, bool transparent) {
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,
                 transparent ? GLFW_TRUE : GLFW_FALSE);
  return glfwCreateWindow(w, h, "Minicraft Plus", nullptr, nullptr);
}

}  // namespace

PcSplashScreen::~PcSplashScreen() { Destroy(); }

bool PcSplashScreen::Show(const std::vector<uint8_t>& png_bytes) {
  if (png_bytes.empty()) return false;

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

  if (!glfwInit()) {
    stbi_image_free(rgba);
    return false;
  }

  width_ = w;
  height_ = h;
  window_ = CreateSplashWindow(w, h, true);
  if (!window_) {
    window_ = CreateSplashWindow(w, h, false);
  }
  if (!window_) {
    stbi_image_free(rgba);
    return false;
  }

  int actual_w = w;
  int actual_h = h;
  glfwGetWindowSize(window_, &actual_w, &actual_h);

  if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
    int mx = 0;
    int my = 0;
    glfwGetMonitorPos(mon, &mx, &my);
    if (const GLFWvidmode* mode = glfwGetVideoMode(mon)) {
      glfwSetWindowPos(window_, mx + (mode->width - actual_w) / 2,
                       my + (mode->height - actual_h) / 2);
    }
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(0);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    stbi_image_free(rgba);
    glfwDestroyWindow(window_);
    window_ = nullptr;
    return false;
  }

  program_ = LinkSplashProgram();
  if (!program_) {
    stbi_image_free(rgba);
    glfwDestroyWindow(window_);
    window_ = nullptr;
    return false;
  }

  // Flip vertically in UV space so PNG row 0 maps to top (+1.0 NDC).
  const float vertices[] = {
      -1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 1.0f,
      1.0f,  1.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f,
      1.0f,  1.0f,  1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 0.0f,
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
  glfwPollEvents();
  shown_at_ = std::chrono::steady_clock::now();
  return true;
}

void PcSplashScreen::RenderFrame() {
  if (!window_ || !program_) return;
  glfwMakeContextCurrent(window_);
  int fb_w = width_;
  int fb_h = height_;
  glfwGetFramebufferSize(window_, &fb_w, &fb_h);
  glViewport(0, 0, std::max(1, fb_w), std::max(1, fb_h));
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glfwSwapBuffers(window_);
}

void PcSplashScreen::WaitRemaining(int32_t min_duration_ms) {
  if (!window_ || min_duration_ms <= 0) return;

  const auto deadline =
      shown_at_ + std::chrono::milliseconds(min_duration_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    glfwPollEvents();
    if (glfwWindowShouldClose(window_) ||
        glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
        glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

void PcSplashScreen::Destroy() {
  if (!window_) return;
  glfwMakeContextCurrent(window_);
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
  glfwMakeContextCurrent(nullptr);
  glfwDestroyWindow(window_);
  window_ = nullptr;
}

}  // namespace pc
}  // namespace platform
}  // namespace minicraft
