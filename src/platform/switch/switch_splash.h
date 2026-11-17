#ifndef MINICRAFT_PLATFORM_SWITCH_SWITCH_SPLASH_H_
#define MINICRAFT_PLATFORM_SWITCH_SWITCH_SPLASH_H_

#include <cstdint>
#include <vector>

namespace minicraft {
namespace platform {
namespace nx {

class SwitchClock;
class SwitchWindow;

// Renders the Minicraft+ splash logo onto the active Switch EGL window while
// the core assets are loaded, then holds up to `min_duration_ms` unless
// dismissed or exit is requested.
class SwitchSplashScreen {
 public:
  SwitchSplashScreen(SwitchWindow* window, const SwitchClock* clock);
  ~SwitchSplashScreen();

  SwitchSplashScreen(const SwitchSplashScreen&) = delete;
  SwitchSplashScreen& operator=(const SwitchSplashScreen&) = delete;

  bool Show(const std::vector<uint8_t>& png_bytes);
  void WaitRemaining(int32_t min_duration_ms);
  void Destroy();

  bool IsActive() const { return program_ != 0 && texture_ != 0; }

 private:
  void RenderFrame();

  SwitchWindow* window_ = nullptr;
  const SwitchClock* clock_ = nullptr;
  uint32_t program_ = 0;
  uint32_t vao_ = 0;
  uint32_t vbo_ = 0;
  uint32_t texture_ = 0;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int64_t shown_at_ms_ = 0;
};

}  // namespace nx
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_SWITCH_SWITCH_SPLASH_H_
