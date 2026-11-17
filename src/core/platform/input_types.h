#ifndef MINICRAFT_CORE_PLATFORM_INPUT_TYPES_H_
#define MINICRAFT_CORE_PLATFORM_INPUT_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

namespace minicraft {
namespace platform {

// Platform-agnostic key events pushed from the window backend into core InputHandler.
struct KeyEvent {
  std::string key_name;  // Canonical uppercase name: "A", "UP", "ENTER", "SHIFT", "F3", etc.
  bool pressed = false;
  bool repeat = false;
};

struct CharEvent {
  uint32_t codepoint = 0;
};

enum class GamepadButton : uint8_t {
  kDpadUp = 0,
  kDpadDown,
  kDpadLeft,
  kDpadRight,
  kA,
  kB,
  kX,
  kY,
  kLeftBumper,
  kRightBumper,
  kStart,
  kBack,
  kLeftStick,
  kRightStick,
  kCount
};

struct GamepadState {
  bool connected = false;
  bool buttons[static_cast<size_t>(GamepadButton::kCount)] = {};
  float left_trigger = 0.0f;
  float right_trigger = 0.0f;
};

}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PLATFORM_INPUT_TYPES_H_
