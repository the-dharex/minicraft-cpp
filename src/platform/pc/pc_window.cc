#include "platform/pc/pc_window.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <utility>

namespace minicraft {
namespace platform {
namespace pc {

namespace {

std::string GlfwKeyToName(int key) {
  if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
    return std::string(1, static_cast<char>('A' + (key - GLFW_KEY_A)));
  }
  if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
    return std::string(1, static_cast<char>('0' + (key - GLFW_KEY_0)));
  }
  if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
    return "F" + std::to_string(1 + (key - GLFW_KEY_F1));
  }
  switch (key) {
    case GLFW_KEY_UP: return "UP";
    case GLFW_KEY_DOWN: return "DOWN";
    case GLFW_KEY_LEFT: return "LEFT";
    case GLFW_KEY_RIGHT: return "RIGHT";
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return "ENTER";
    case GLFW_KEY_ESCAPE: return "ESCAPE";
    case GLFW_KEY_SPACE: return "SPACE";
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT: return "SHIFT";
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL: return "CTRL";
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT: return "ALT";
    case GLFW_KEY_BACKSPACE: return "BACKSPACE";
    case GLFW_KEY_TAB: return "TAB";
    case GLFW_KEY_PAGE_UP: return "PAGE_UP";
    case GLFW_KEY_PAGE_DOWN: return "PAGE_DOWN";
    case GLFW_KEY_HOME: return "HOME";
    case GLFW_KEY_END: return "END";
    case GLFW_KEY_DELETE: return "DELETE";
    case GLFW_KEY_INSERT: return "INSERT";
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT: return "MINUS";
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD: return "EQUALS";
    case GLFW_KEY_COMMA: return "COMMA";
    case GLFW_KEY_PERIOD: return "PERIOD";
    case GLFW_KEY_SLASH: return "SLASH";
    case GLFW_KEY_SEMICOLON: return "SEMICOLON";
    case GLFW_KEY_APOSTROPHE: return "QUOTE";
    case GLFW_KEY_LEFT_BRACKET: return "OPEN_BRACKET";
    case GLFW_KEY_RIGHT_BRACKET: return "CLOSE_BRACKET";
    case GLFW_KEY_BACKSLASH: return "BACK_SLASH";
    case GLFW_KEY_GRAVE_ACCENT: return "BACK_QUOTE";
    default: return "";
  }
}

}  // namespace

PcWindow::~PcWindow() { Destroy(); }

bool PcWindow::Create(int32_t width, int32_t height, std::string_view title) {
  return CreateWithVisibility(width, height, title, true);
}

bool PcWindow::CreateWithVisibility(int32_t width, int32_t height,
                                    std::string_view title, bool visible) {
  if (!glfwInit()) {
    return false;
  }

  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  std::string title_str(title);
  window_ = glfwCreateWindow(width, height, title_str.c_str(), nullptr, nullptr);
  if (!window_) {
    glfwTerminate();
    return false;
  }

  // Query actual window size (scaled by monitor DPI when GLFW_SCALE_TO_MONITOR
  // is enabled).
  glfwGetWindowSize(window_, &windowed_w_, &windowed_h_);
  glfwGetWindowPos(window_, &windowed_x_, &windowed_y_);

  // Center on primary monitor.
  if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
    int mx = 0;
    int my = 0;
    glfwGetMonitorPos(mon, &mx, &my);
    if (const GLFWvidmode* mode = glfwGetVideoMode(mon)) {
      int cx = mx + (mode->width - windowed_w_) / 2;
      int cy = my + (mode->height - windowed_h_) / 2;
      glfwSetWindowPos(window_, cx, cy);
      windowed_x_ = cx;
      windowed_y_ = cy;
    }
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(0);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
    return false;
  }

  glfwSetWindowUserPointer(window_, this);
  glfwSetKeyCallback(window_, KeyCallback);
  glfwSetCharCallback(window_, CharCallback);
  glfwSetWindowFocusCallback(window_, FocusCallback);
  focused_ = glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
  return true;
}

void PcWindow::Show() {
  if (window_) {
    glfwShowWindow(window_);
    glfwFocusWindow(window_);
    focused_ = glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
  }
}

void PcWindow::MakeContextCurrent() {
  if (window_) {
    glfwMakeContextCurrent(window_);
  }
}

void PcWindow::Destroy() {
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
  }
}

bool PcWindow::ShouldClose() const {
  return window_ == nullptr || glfwWindowShouldClose(window_) != 0;
}

void PcWindow::RequestClose() {
  if (window_) {
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
  }
}

void PcWindow::PollEvents() { glfwPollEvents(); }

void PcWindow::SwapBuffers() {
  if (window_) {
    glfwSwapBuffers(window_);
  }
}

bool PcWindow::IsFocused() const { return focused_; }

void PcWindow::SetFullscreen(bool fullscreen) {
  if (!window_ || fullscreen_ == fullscreen) {
    return;
  }
  fullscreen_ = fullscreen;
  if (fullscreen_) {
    glfwGetWindowPos(window_, &windowed_x_, &windowed_y_);
    glfwGetWindowSize(window_, &windowed_w_, &windowed_h_);
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
    if (mon && mode) {
      glfwSetWindowMonitor(window_, mon, 0, 0, mode->width, mode->height,
                           mode->refreshRate);
    }
  } else {
    glfwSetWindowMonitor(window_, nullptr, windowed_x_, windowed_y_,
                         windowed_w_, windowed_h_, 0);
  }
}

bool PcWindow::IsFullscreen() const { return fullscreen_; }

void PcWindow::GetFramebufferSize(int32_t* width, int32_t* height) const {
  if (!window_) {
    if (width) *width = 0;
    if (height) *height = 0;
    return;
  }
  int w = 0, h = 0;
  glfwGetFramebufferSize(window_, &w, &h);
  if (width) *width = w;
  if (height) *height = h;
}

int32_t PcWindow::GetMonitorRefreshRate() const {
  GLFWmonitor* mon = glfwGetPrimaryMonitor();
  if (!mon) return 60;
  const GLFWvidmode* mode = glfwGetVideoMode(mon);
  if (!mode || mode->refreshRate <= 10 || mode->refreshRate > 300) {
    return 60;
  }
  return mode->refreshRate;
}

void PcWindow::SetIconRgba(int32_t width, int32_t height, const uint8_t* rgba) {
  if (!window_ || !rgba || width <= 0 || height <= 0) return;
  GLFWimage img{};
  img.width = width;
  img.height = height;
  img.pixels = const_cast<unsigned char*>(rgba);
  glfwSetWindowIcon(window_, 1, &img);
}

void PcWindow::DrainInputEvents(std::vector<KeyEvent>* out_keys,
                                std::vector<CharEvent>* out_chars) {
  if (out_keys) {
    out_keys->clear();
    out_keys->swap(pending_keys_);
  }
  if (out_chars) {
    out_chars->clear();
    out_chars->swap(pending_chars_);
  }
}

GamepadState PcWindow::PollGamepad() const {
  GamepadState state{};
  for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
    if (glfwJoystickIsGamepad(jid)) {
      GLFWgamepadstate gp{};
      if (glfwGetGamepadState(jid, &gp)) {
        state.connected = true;
        auto btn = [&](GamepadButton b, int glfw_b) {
          state.buttons[static_cast<size_t>(b)] =
              gp.buttons[glfw_b] == GLFW_PRESS;
        };
        btn(GamepadButton::kDpadUp, GLFW_GAMEPAD_BUTTON_DPAD_UP);
        btn(GamepadButton::kDpadDown, GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
        btn(GamepadButton::kDpadLeft, GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
        btn(GamepadButton::kDpadRight, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
        btn(GamepadButton::kA, GLFW_GAMEPAD_BUTTON_A);
        btn(GamepadButton::kB, GLFW_GAMEPAD_BUTTON_B);
        btn(GamepadButton::kX, GLFW_GAMEPAD_BUTTON_X);
        btn(GamepadButton::kY, GLFW_GAMEPAD_BUTTON_Y);
        btn(GamepadButton::kLeftBumper, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
        btn(GamepadButton::kRightBumper, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
        btn(GamepadButton::kStart, GLFW_GAMEPAD_BUTTON_START);
        btn(GamepadButton::kBack, GLFW_GAMEPAD_BUTTON_BACK);
        btn(GamepadButton::kLeftStick, GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
        btn(GamepadButton::kRightStick, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);

        // Also support left analog stick as D-pad fallback.
        if (gp.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -0.5f)
          state.buttons[static_cast<size_t>(GamepadButton::kDpadUp)] = true;
        if (gp.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] > 0.5f)
          state.buttons[static_cast<size_t>(GamepadButton::kDpadDown)] = true;
        if (gp.axes[GLFW_GAMEPAD_AXIS_LEFT_X] < -0.5f)
          state.buttons[static_cast<size_t>(GamepadButton::kDpadLeft)] = true;
        if (gp.axes[GLFW_GAMEPAD_AXIS_LEFT_X] > 0.5f)
          state.buttons[static_cast<size_t>(GamepadButton::kDpadRight)] = true;

        state.left_trigger = (gp.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
        state.right_trigger = (gp.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
        break;
      }
    }
  }
  return state;
}

std::string PcWindow::GetClipboardText() const {
  if (!window_) return "";
  const char* txt = glfwGetClipboardString(window_);
  return txt ? std::string(txt) : "";
}

void PcWindow::SetClipboardText(std::string_view text) {
  if (!window_) return;
  std::string str(text);
  glfwSetClipboardString(window_, str.c_str());
}

void PcWindow::KeyCallback(GLFWwindow* win, int key, int scancode, int action,
                           int mods) {
  auto* self = static_cast<PcWindow*>(glfwGetWindowUserPointer(win));
  if (!self) return;
  const bool is_repeat = (action == GLFW_REPEAT);
  std::string name = GlfwKeyToName(key);
  if (!name.empty()) {
    self->pending_keys_.push_back(
        {std::move(name), action != GLFW_RELEASE, is_repeat});
  }
}

void PcWindow::CharCallback(GLFWwindow* win, unsigned int codepoint) {
  auto* self = static_cast<PcWindow*>(glfwGetWindowUserPointer(win));
  if (!self) return;
  self->pending_chars_.push_back({static_cast<uint32_t>(codepoint)});
}

void PcWindow::FocusCallback(GLFWwindow* win, int focused) {
  auto* self = static_cast<PcWindow*>(glfwGetWindowUserPointer(win));
  if (!self) return;
  self->focused_ = (focused != 0);
}

}  // namespace pc
}  // namespace platform
}  // namespace minicraft
