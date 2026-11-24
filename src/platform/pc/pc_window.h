#ifndef MINICRAFT_PLATFORM_PC_PC_WINDOW_H_
#define MINICRAFT_PLATFORM_PC_PC_WINDOW_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/platform/window_backend.h"

struct GLFWwindow;

namespace minicraft {
namespace platform {
namespace pc {

class PcWindow final : public IWindowBackend {
 public:
  PcWindow() = default;
  ~PcWindow() override;

  PcWindow(const PcWindow&) = delete;
  PcWindow& operator=(const PcWindow&) = delete;

  bool Create(int32_t width, int32_t height,
              std::string_view title) override;
  bool CreateWithVisibility(int32_t width, int32_t height,
                            std::string_view title, bool visible);
  void Show();
  void MakeContextCurrent();
  void Destroy() override;

  bool ShouldClose() const override;
  void RequestClose() override;
  void PollEvents() override;
  void SwapBuffers() override;

  bool IsFocused() const override;
  void SetFullscreen(bool fullscreen) override;
  bool IsFullscreen() const override;
  void GetFramebufferSize(int32_t* width, int32_t* height) const override;
  int32_t GetMonitorRefreshRate() const override;
  void SetIconRgba(int32_t width, int32_t height,
                   const uint8_t* rgba) override;

  void DrainInputEvents(std::vector<KeyEvent>* out_keys,
                        std::vector<CharEvent>* out_chars) override;
  GamepadState PollGamepad() const override;

  std::string GetClipboardText() const override;
  void SetClipboardText(std::string_view text) override;

 private:
  static void KeyCallback(GLFWwindow* win, int key, int scancode, int action,
                          int mods);
  static void CharCallback(GLFWwindow* win, unsigned int codepoint);
  static void FocusCallback(GLFWwindow* win, int focused);

  GLFWwindow* window_ = nullptr;
  bool focused_ = true;
  bool fullscreen_ = false;
  int32_t windowed_x_ = 100;
  int32_t windowed_y_ = 100;
  int32_t windowed_w_ = 864;
  int32_t windowed_h_ = 576;

  std::vector<KeyEvent> pending_keys_;
  std::vector<CharEvent> pending_chars_;
};

}  // namespace pc
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_PC_PC_WINDOW_H_
