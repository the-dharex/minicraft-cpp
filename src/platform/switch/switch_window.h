#pragma once

#include <EGL/egl.h>
#include <switch.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/platform/input_types.h"
#include "core/platform/window_backend.h"

namespace minicraft::platform::nx {

class SwitchAudioBackend;
class SwitchClock;
class SwitchFileSystem;

// Native Nintendo Switch window, EGL/OpenGL 4.3+ Core context, Npad/USB keyboard
// input backend, and Horizon OS applet lifecycle handler (Home Menu focus,
// Docked/Handheld dynamic resolution crop, ExitLock, Controller Support applet,
// and atomic save commits).
class SwitchWindow final : public IWindowBackend {
 public:
  static constexpr int32_t kSurfaceWidth = 1920;
  static constexpr int32_t kSurfaceHeight = 1080;

  SwitchWindow(SwitchClock* clock, SwitchAudioBackend* audio,
               SwitchFileSystem* fs);
  ~SwitchWindow() override;

  bool Create(int32_t width, int32_t height, std::string_view title) override;
  void Destroy() override;

  bool ShouldClose() const override;
  void RequestClose() override;
  void PollEvents() override;
  void SwapBuffers() override;
  bool IsFocused() const override;

  void SetFullscreen(bool fullscreen) override;
  bool IsFullscreen() const override;
  void GetFramebufferSize(int32_t* out_w, int32_t* out_h) const override;
  int32_t GetMonitorRefreshRate() const override;
  void SetIconRgba(int32_t width, int32_t height,
                   const uint8_t* rgba) override;

  void DrainInputEvents(std::vector<KeyEvent>* out_keys,
                        std::vector<CharEvent>* out_chars) override;
  GamepadState PollGamepad() const override;

  std::string GetClipboardText() const override;
  void SetClipboardText(std::string_view text) override;

  // Exposed for SwitchRenderBackend and SwitchSplashScreen so bottom-left GL
  // coordinates match NWindow's top-left crop on the 1920x1080 surface.
  int32_t SurfaceHeight() const { return kSurfaceHeight; }

 private:
  static void OnAppletHook(AppletHookType hook, void* param);
  void HandleAppletHook(AppletHookType hook);
  void UpdateOperationModeCrop();
  void CheckControllerConnection();
  void PollUsbKeyboard();
  void DestroyEgl();

  SwitchClock* clock_ = nullptr;
  SwitchAudioBackend* audio_ = nullptr;
  SwitchFileSystem* fs_ = nullptr;

  NWindow* nwindow_ = nullptr;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLSurface surface_ = EGL_NO_SURFACE;
  EGLContext context_ = EGL_NO_CONTEXT;

  int32_t fb_width_ = 1280;
  int32_t fb_height_ = 720;

  AppletHookCookie applet_hook_{};
  bool hook_registered_ = false;
  bool exit_locked_ = false;
  bool is_application_ = false;
  bool focused_ = true;
  mutable bool close_requested_ = false;

  PadState pad_{};
  bool pad_initialized_ = false;
  bool was_pad_connected_ = false;

  HidKeyboardState prev_kbd_{};
  bool prev_kbd_valid_ = false;
  std::vector<KeyEvent> pending_keys_;
  std::vector<CharEvent> pending_chars_;

  std::string clipboard_text_;
};

}  // namespace minicraft::platform::nx
