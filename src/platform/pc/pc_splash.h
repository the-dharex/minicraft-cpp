#ifndef MINICRAFT_PLATFORM_PC_PC_SPLASH_H_
#define MINICRAFT_PLATFORM_PC_PC_SPLASH_H_

#include <chrono>
#include <cstdint>
#include <vector>

struct GLFWwindow;

namespace minicraft {
namespace platform {
namespace pc {

// Borderless splash window displayed while the main window initializes assets.
class PcSplashScreen {
 public:
  PcSplashScreen() = default;
  ~PcSplashScreen();

  PcSplashScreen(const PcSplashScreen&) = delete;
  PcSplashScreen& operator=(const PcSplashScreen&) = delete;

  // Decodes the PNG splash image and opens a centered splash window.
  bool Show(const std::vector<uint8_t>& png_bytes);

  // Pumps window events until at least `min_duration_ms` have elapsed since
  // Show(), or until the user clicks/closes the splash.
  void WaitRemaining(int32_t min_duration_ms);

  // Frees splash OpenGL resources and destroys the splash window.
  void Destroy();

  bool IsActive() const { return window_ != nullptr; }

 private:
  void RenderFrame();

  GLFWwindow* window_ = nullptr;
  uint32_t program_ = 0;
  uint32_t vao_ = 0;
  uint32_t vbo_ = 0;
  uint32_t texture_ = 0;
  int32_t width_ = 0;
  int32_t height_ = 0;
  std::chrono::steady_clock::time_point shown_at_;
};

}  // namespace pc
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_PC_PC_SPLASH_H_
