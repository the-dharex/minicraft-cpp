#include "platform/switch/switch_window.h"

#include <EGL/eglext.h>
#include <glad/glad.h>

#include <cstdio>
#include <cstring>

#include "platform/switch/switch_audio_backend.h"
#include "platform/switch/switch_clock.h"
#include "platform/switch/switch_file_system.h"

namespace minicraft::platform::nx {

namespace {

constexpr s32 kStickDeadzone = 0x4000;

struct UsbKeyMapping {
  HidKeyboardKey hid_key;
  const char* name;
  char lower_char;
  char upper_char;
};

constexpr UsbKeyMapping kUsbKeyMappings[] = {
    {HidKeyboardKey_A, "A", 'a', 'A'},
    {HidKeyboardKey_B, "B", 'b', 'B'},
    {HidKeyboardKey_C, "C", 'c', 'C'},
    {HidKeyboardKey_D, "D", 'd', 'D'},
    {HidKeyboardKey_E, "E", 'e', 'E'},
    {HidKeyboardKey_F, "F", 'f', 'F'},
    {HidKeyboardKey_G, "G", 'g', 'G'},
    {HidKeyboardKey_H, "H", 'h', 'H'},
    {HidKeyboardKey_I, "I", 'i', 'I'},
    {HidKeyboardKey_J, "J", 'j', 'J'},
    {HidKeyboardKey_K, "K", 'k', 'K'},
    {HidKeyboardKey_L, "L", 'l', 'L'},
    {HidKeyboardKey_M, "M", 'm', 'M'},
    {HidKeyboardKey_N, "N", 'n', 'N'},
    {HidKeyboardKey_O, "O", 'o', 'O'},
    {HidKeyboardKey_P, "P", 'p', 'P'},
    {HidKeyboardKey_Q, "Q", 'q', 'Q'},
    {HidKeyboardKey_R, "R", 'r', 'R'},
    {HidKeyboardKey_S, "S", 's', 'S'},
    {HidKeyboardKey_T, "T", 't', 'T'},
    {HidKeyboardKey_U, "U", 'u', 'U'},
    {HidKeyboardKey_V, "V", 'v', 'V'},
    {HidKeyboardKey_W, "W", 'w', 'W'},
    {HidKeyboardKey_X, "X", 'x', 'X'},
    {HidKeyboardKey_Y, "Y", 'y', 'Y'},
    {HidKeyboardKey_Z, "Z", 'z', 'Z'},
    {HidKeyboardKey_D0, "0", '0', ')'},
    {HidKeyboardKey_D1, "1", '1', '!'},
    {HidKeyboardKey_D2, "2", '2', '@'},
    {HidKeyboardKey_D3, "3", '3', '#'},
    {HidKeyboardKey_D4, "4", '4', '$'},
    {HidKeyboardKey_D5, "5", '5', '%'},
    {HidKeyboardKey_D6, "6", '6', '^'},
    {HidKeyboardKey_D7, "7", '7', '&'},
    {HidKeyboardKey_D8, "8", '8', '*'},
    {HidKeyboardKey_D9, "9", '9', '('},
    {HidKeyboardKey_Return, "ENTER", 0, 0},
    {HidKeyboardKey_Escape, "ESCAPE", 0, 0},
    {HidKeyboardKey_Backspace, "BACKSPACE", 0, 0},
    {HidKeyboardKey_Tab, "TAB", 0, 0},
    {HidKeyboardKey_Space, "SPACE", ' ', ' '},
    {HidKeyboardKey_Minus, "MINUS", '-', '_'},
    {HidKeyboardKey_Plus, "EQUALS", '=', '+'},
    {HidKeyboardKey_OpenBracket, "OPEN_BRACKET", '[', '{'},
    {HidKeyboardKey_CloseBracket, "CLOSE_BRACKET", ']', '}'},
    {HidKeyboardKey_Backslash, "BACK_SLASH", '\\', '|'},
    {HidKeyboardKey_Semicolon, "SEMICOLON", ';', ':'},
    {HidKeyboardKey_Quote, "QUOTE", '\'', '"'},
    {HidKeyboardKey_Backquote, "BACK_QUOTE", '`', '~'},
    {HidKeyboardKey_Comma, "COMMA", ',', '<'},
    {HidKeyboardKey_Period, "PERIOD", '.', '>'},
    {HidKeyboardKey_Slash, "SLASH", '/', '?'},
    {HidKeyboardKey_F1, "F1", 0, 0},
    {HidKeyboardKey_F2, "F2", 0, 0},
    {HidKeyboardKey_F3, "F3", 0, 0},
    {HidKeyboardKey_F4, "F4", 0, 0},
    {HidKeyboardKey_F5, "F5", 0, 0},
    {HidKeyboardKey_F6, "F6", 0, 0},
    {HidKeyboardKey_F7, "F7", 0, 0},
    {HidKeyboardKey_F8, "F8", 0, 0},
    {HidKeyboardKey_F9, "F9", 0, 0},
    {HidKeyboardKey_F10, "F10", 0, 0},
    {HidKeyboardKey_F11, "F11", 0, 0},
    {HidKeyboardKey_F12, "F12", 0, 0},
    {HidKeyboardKey_Delete, "DELETE", 0, 0},
    {HidKeyboardKey_Home, "HOME", 0, 0},
    {HidKeyboardKey_End, "END", 0, 0},
    {HidKeyboardKey_PageUp, "PAGE_UP", 0, 0},
    {HidKeyboardKey_PageDown, "PAGE_DOWN", 0, 0},
    {HidKeyboardKey_RightArrow, "RIGHT", 0, 0},
    {HidKeyboardKey_LeftArrow, "LEFT", 0, 0},
    {HidKeyboardKey_DownArrow, "DOWN", 0, 0},
    {HidKeyboardKey_UpArrow, "UP", 0, 0},
    {HidKeyboardKey_LeftShift, "SHIFT", 0, 0},
    {HidKeyboardKey_RightShift, "SHIFT", 0, 0},
    {HidKeyboardKey_LeftControl, "CTRL", 0, 0},
    {HidKeyboardKey_RightControl, "CTRL", 0, 0},
    {HidKeyboardKey_LeftAlt, "ALT", 0, 0},
    {HidKeyboardKey_RightAlt, "ALT", 0, 0},
};

}  // namespace

SwitchWindow::SwitchWindow(SwitchClock* clock, SwitchAudioBackend* audio,
                           SwitchFileSystem* fs)
    : clock_(clock), audio_(audio), fs_(fs) {}

SwitchWindow::~SwitchWindow() {
  Destroy();
}

void SwitchWindow::OnAppletHook(AppletHookType hook, void* param) {
  auto* self = static_cast<SwitchWindow*>(param);
  if (self) {
    self->HandleAppletHook(hook);
  }
}

void SwitchWindow::HandleAppletHook(AppletHookType hook) {
  switch (hook) {
    case AppletHookType_OnExitRequest:
      close_requested_ = true;
      if (fs_) fs_->CommitIfDirty();
      break;

    case AppletHookType_OnFocusState: {
      const AppletFocusState state = appletGetFocusState();
      const bool now_focused = (state == AppletFocusState_InFocus);
      focused_ = now_focused;
      if (clock_) clock_->SetPaused(!now_focused);
      if (audio_) audio_->SetPaused(!now_focused);
      if (!now_focused && fs_) fs_->CommitIfDirty();
      break;
    }

    case AppletHookType_OnResume:
      focused_ = (appletGetFocusState() == AppletFocusState_InFocus);
      if (clock_) clock_->SetPaused(!focused_);
      if (audio_) audio_->SetPaused(!focused_);
      UpdateOperationModeCrop();
      break;

    case AppletHookType_OnOperationMode:
    case AppletHookType_OnPerformanceMode:
      UpdateOperationModeCrop();
      break;

    default:
      break;
  }
}

void SwitchWindow::UpdateOperationModeCrop() {
  const AppletOperationMode mode = appletGetOperationMode();
  if (mode == AppletOperationMode_Console) {
    fb_width_ = 1920;
    fb_height_ = 1080;
  } else {
    fb_width_ = 1280;
    fb_height_ = 720;
  }

  if (nwindow_) {
    nwindowSetCrop(nwindow_, 0, 0, fb_width_, fb_height_);
  }
}

bool SwitchWindow::Create(int32_t /*width*/, int32_t /*height*/,
                          std::string_view /*title*/) {
  const AppletType type = appletGetAppletType();
  is_application_ =
      (type == AppletType_Application || type == AppletType_SystemApplication);

  if (R_SUCCEEDED(appletLockExit())) {
    exit_locked_ = true;
  }

  appletHook(&applet_hook_, OnAppletHook, this);
  hook_registered_ = true;

  if (is_application_) {
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
    appletSetPerformanceConfigurationChangedNotification(true);
    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);
  }

  focused_ = (appletGetFocusState() == AppletFocusState_InFocus);
  if (clock_) clock_->SetPaused(!focused_);

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad_);
  padUpdate(&pad_);
  pad_initialized_ = true;
  was_pad_connected_ = (pad_.active_id_mask != 0 || pad_.active_handheld);

  hidInitializeKeyboard();

  nwindow_ = nwindowGetDefault();
  if (!nwindow_) {
    std::fprintf(stderr, "SwitchWindow: nwindowGetDefault returned nullptr\n");
    Destroy();
    return false;
  }

  nwindowSetDimensions(nwindow_, kSurfaceWidth, kSurfaceHeight);
  UpdateOperationModeCrop();

  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY) {
    std::fprintf(stderr, "SwitchWindow: eglGetDisplay failed (0x%x)\n",
                 eglGetError());
    Destroy();
    return false;
  }

  if (eglInitialize(display_, nullptr, nullptr) == EGL_FALSE) {
    std::fprintf(stderr, "SwitchWindow: eglInitialize failed (0x%x)\n",
                 eglGetError());
    Destroy();
    return false;
  }

  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
    std::fprintf(stderr, "SwitchWindow: eglBindAPI failed (0x%x)\n",
                 eglGetError());
    Destroy();
    return false;
  }

  static const EGLint kFramebufferAttrs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_STENCIL_SIZE, 8,
      EGL_NONE,
  };

  EGLConfig config = nullptr;
  EGLint num_configs = 0;
  if (eglChooseConfig(display_, kFramebufferAttrs, &config, 1, &num_configs) ==
          EGL_FALSE ||
      num_configs == 0) {
    std::fprintf(stderr, "SwitchWindow: eglChooseConfig failed (0x%x)\n",
                 eglGetError());
    Destroy();
    return false;
  }

  surface_ = eglCreateWindowSurface(display_, config, nwindow_, nullptr);
  if (surface_ == EGL_NO_SURFACE) {
    std::fprintf(stderr, "SwitchWindow: eglCreateWindowSurface failed (0x%x)\n",
                 eglGetError());
    Destroy();
    return false;
  }

  static const EGLint kContextAttrs[] = {
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_NONE,
  };

  context_ =
      eglCreateContext(display_, config, EGL_NO_CONTEXT, kContextAttrs);
  if (context_ == EGL_NO_CONTEXT) {
    std::fprintf(stderr, "SwitchWindow: eglCreateContext failed (0x%x)\n",
                 eglGetError());
    Destroy();
    return false;
  }

  if (eglMakeCurrent(display_, surface_, surface_, context_) == EGL_FALSE) {
    std::fprintf(stderr, "SwitchWindow: eglMakeCurrent failed (0x%x)\n",
                 eglGetError());
    Destroy();
    return false;
  }

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(eglGetProcAddress))) {
    std::fprintf(stderr, "SwitchWindow: gladLoadGLLoader failed\n");
    Destroy();
    return false;
  }

  eglSwapInterval(display_, 1);
  return true;
}

void SwitchWindow::DestroyEgl() {
  if (display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, context_);
      context_ = EGL_NO_CONTEXT;
    }
    if (surface_ != EGL_NO_SURFACE) {
      eglDestroySurface(display_, surface_);
      surface_ = EGL_NO_SURFACE;
    }
    eglTerminate(display_);
    display_ = EGL_NO_DISPLAY;
  }
}

void SwitchWindow::Destroy() {
  if (fs_) {
    fs_->CommitIfDirty();
  }

  DestroyEgl();

  if (hook_registered_) {
    appletUnhook(&applet_hook_);
    hook_registered_ = false;
  }
  if (exit_locked_) {
    appletUnlockExit();
    exit_locked_ = false;
  }
}

bool SwitchWindow::ShouldClose() const {
  return close_requested_;
}

void SwitchWindow::RequestClose() {
  close_requested_ = true;
}

void SwitchWindow::CheckControllerConnection() {
  const bool connected = (pad_.active_id_mask != 0 || pad_.active_handheld);
  if (is_application_ && focused_ && was_pad_connected_ && !connected &&
      !close_requested_) {
    if (fs_) fs_->CommitIfDirty();
    if (clock_) clock_->SetPaused(true);
    if (audio_) audio_->SetPaused(true);

    HidLaControllerSupportArg arg{};
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = 1;
    arg.hdr.player_count_max = 1;
    arg.hdr.enable_take_over_connection = 1;
    arg.hdr.enable_left_justify = 1;
    arg.hdr.enable_permit_joy_dual = 1;
    arg.hdr.enable_single_mode = 1;

    HidLaControllerSupportResultInfo result_info{};
    hidLaShowControllerSupport(&result_info, &arg);

    padUpdate(&pad_);
    if (audio_) audio_->SetPaused(!focused_);
    if (clock_) clock_->SetPaused(!focused_);
  }
  was_pad_connected_ = (pad_.active_id_mask != 0 || pad_.active_handheld);
}

void SwitchWindow::PollUsbKeyboard() {
  HidKeyboardState cur{};
  const size_t read = hidGetKeyboardStates(&cur, 1);
  if (read == 0) {
    prev_kbd_valid_ = false;
    return;
  }

  if (!prev_kbd_valid_) {
    prev_kbd_ = cur;
    prev_kbd_valid_ = true;
    return;
  }

  const bool shift =
      hidKeyboardStateGetKey(&cur, HidKeyboardKey_LeftShift) ||
      hidKeyboardStateGetKey(&cur, HidKeyboardKey_RightShift);

  for (const auto& m : kUsbKeyMappings) {
    const bool was_down = hidKeyboardStateGetKey(&prev_kbd_, m.hid_key);
    const bool is_down = hidKeyboardStateGetKey(&cur, m.hid_key);
    if (is_down != was_down) {
      pending_keys_.push_back(KeyEvent{m.name, is_down, false});
      if (is_down && m.lower_char != 0) {
        const char ch = shift ? m.upper_char : m.lower_char;
        pending_chars_.push_back(CharEvent{static_cast<uint32_t>(ch)});
      }
    }
  }

  prev_kbd_ = cur;
}

void SwitchWindow::PollEvents() {
  if (!appletMainLoop()) {
    close_requested_ = true;
    if (fs_) fs_->CommitIfDirty();
    return;
  }

  if (pad_initialized_) {
    padUpdate(&pad_);
    CheckControllerConnection();
  }

  PollUsbKeyboard();

  if (audio_) {
    audio_->Update();
  }
  if (fs_) {
    fs_->TryCommitIfDirty();
  }
}

void SwitchWindow::SwapBuffers() {
  if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
    eglSwapBuffers(display_, surface_);
  }
}

bool SwitchWindow::IsFocused() const {
  return focused_;
}

void SwitchWindow::SetFullscreen(bool /*fullscreen*/) {
  // Nintendo Switch always runs full-display; no windowed mode exists.
}

bool SwitchWindow::IsFullscreen() const {
  return true;
}

void SwitchWindow::GetFramebufferSize(int32_t* out_w, int32_t* out_h) const {
  if (out_w) *out_w = fb_width_;
  if (out_h) *out_h = fb_height_;
}

int32_t SwitchWindow::GetMonitorRefreshRate() const {
  return 60;
}

void SwitchWindow::SetIconRgba(int32_t /*width*/, int32_t /*height*/,
                               const uint8_t* /*rgba*/) {
  // Switch application icons are packaged into NRO/NACP assets.
}

void SwitchWindow::DrainInputEvents(std::vector<KeyEvent>* out_keys,
                                    std::vector<CharEvent>* out_chars) {
  if (out_keys) {
    out_keys->insert(out_keys->end(), pending_keys_.begin(),
                     pending_keys_.end());
  }
  if (out_chars) {
    out_chars->insert(out_chars->end(), pending_chars_.begin(),
                      pending_chars_.end());
  }
  pending_keys_.clear();
  pending_chars_.clear();
}

GamepadState SwitchWindow::PollGamepad() const {
  GamepadState state;
  if (!pad_initialized_ || !focused_) {
    return state;
  }

  const u64 held = padGetButtons(&pad_);
  const HidAnalogStickState lstick = padGetStickPos(&pad_, 0);

  auto set_btn = [&](GamepadButton btn, bool down) {
    state.buttons[static_cast<std::size_t>(btn)] = down;
  };

  set_btn(GamepadButton::kDpadUp,
          (held & HidNpadButton_Up) != 0 || lstick.y > kStickDeadzone);
  set_btn(GamepadButton::kDpadDown,
          (held & HidNpadButton_Down) != 0 || lstick.y < -kStickDeadzone);
  set_btn(GamepadButton::kDpadLeft,
          (held & HidNpadButton_Left) != 0 || lstick.x < -kStickDeadzone);
  set_btn(GamepadButton::kDpadRight,
          (held & HidNpadButton_Right) != 0 || lstick.x > kStickDeadzone);

  set_btn(GamepadButton::kA, (held & HidNpadButton_A) != 0);
  set_btn(GamepadButton::kB, (held & HidNpadButton_B) != 0);
  set_btn(GamepadButton::kX, (held & HidNpadButton_X) != 0);
  set_btn(GamepadButton::kY, (held & HidNpadButton_Y) != 0);
  set_btn(GamepadButton::kLeftBumper, (held & HidNpadButton_L) != 0);
  set_btn(GamepadButton::kRightBumper, (held & HidNpadButton_R) != 0);
  set_btn(GamepadButton::kStart, (held & HidNpadButton_Plus) != 0);
  set_btn(GamepadButton::kBack, (held & HidNpadButton_Minus) != 0);
  set_btn(GamepadButton::kLeftStick, (held & HidNpadButton_StickL) != 0);
  set_btn(GamepadButton::kRightStick, (held & HidNpadButton_StickR) != 0);

  return state;
}

std::string SwitchWindow::GetClipboardText() const {
  return clipboard_text_;
}

void SwitchWindow::SetClipboardText(std::string_view text) {
  clipboard_text_.assign(text);
}

}  // namespace minicraft::platform::nx
