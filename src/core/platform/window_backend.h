#ifndef MINICRAFT_CORE_PLATFORM_WINDOW_BACKEND_H_
#define MINICRAFT_CORE_PLATFORM_WINDOW_BACKEND_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/platform/input_types.h"

namespace minicraft {
namespace platform {

// platform-agnostic windowing & input polling interface.
class IWindowBackend {
 public:
  virtual ~IWindowBackend() = default;

  virtual bool Create(int32_t width, int32_t height,
                      std::string_view title) = 0;
  virtual void Destroy() = 0;

  virtual bool ShouldClose() const = 0;
  virtual void RequestClose() = 0;
  virtual void PollEvents() = 0;
  virtual void SwapBuffers() = 0;

  virtual bool IsFocused() const = 0;
  virtual void SetFullscreen(bool fullscreen) = 0;
  virtual bool IsFullscreen() const = 0;
  virtual void GetFramebufferSize(int32_t* width, int32_t* height) const = 0;
  virtual int32_t GetMonitorRefreshRate() const = 0;
  virtual void SetIconRgba(int32_t width, int32_t height,
                           const uint8_t* rgba) = 0;

  virtual void DrainInputEvents(std::vector<KeyEvent>* out_keys,
                                std::vector<CharEvent>* out_chars) = 0;
  virtual GamepadState PollGamepad() const = 0;

  virtual std::string GetClipboardText() const = 0;
  virtual void SetClipboardText(std::string_view text) = 0;
};

}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PLATFORM_WINDOW_BACKEND_H_
