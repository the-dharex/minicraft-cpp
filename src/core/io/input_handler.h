#ifndef MINICRAFT_CORE_IO_INPUT_HANDLER_H_
#define MINICRAFT_CORE_IO_INPUT_HANDLER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/platform/input_types.h"

namespace minicraft {
namespace io {

class InputHandler {
 public:
  InputHandler();

  void InitKeyMap();
  void InitButtonMap();
  void ResetKeyBindings();

  // Consumes pending events from the platform window and advances key state.
  void PushKeyEvents(const std::vector<platform::KeyEvent>& key_events);
  void PushCharEvents(const std::vector<platform::CharEvent>& char_events);
  void UpdateGamepad(const platform::GamepadState& gamepad, uint64_t now_ms);
  void Tick(uint64_t now_ms = 0);

  void ReleaseAll();

  void MaskInput(std::function<bool(std::string_view)> filter);

  // Action/Key state queries (zero heap allocations on hot paths).
  bool InputPressed(std::string_view mapping);
  bool InputDown(std::string_view mapping);

  bool PhysicalKeyPressed(std::string_view key_name);
  bool PhysicalKeyDown(std::string_view key_name);

  bool ButtonPressed(platform::GamepadButton btn) const;
  bool ButtonDown(platform::GamepadButton btn) const;
  bool AnyControllerConnected() const { return gamepad_connected_; }
  bool LeftTriggerPressed();
  bool RightTriggerPressed();

  // Key binding customization.
  void SetKey(std::string_view keymap_key, std::string_view keyboard_key);
  void ChangeKeyBinding(std::string_view action_key);
  void AddKeyBinding(std::string_view action_key);
  bool IsListeningForKey() const { return !key_to_change_.empty(); }
  std::string GetChangedKey();
  std::vector<std::string> GetKeyPrefs() const;
  const std::vector<std::string>& ActionOrder() const { return action_order_; }

  std::string GetMapping(std::string_view action_key) const;
  std::string SelectMapping(std::string_view key_map,
                            std::string_view button_map) const;
  int32_t GetLastInputType() const;

  std::vector<std::string> GetAllPressedKeys() const;

  // Text input helpers.
  void PushVirtualChar(char32_t cp);
  std::string AddKeyTyped(std::string_view typing,
                          std::function<bool(char32_t)> filter = nullptr);
  std::string GetKeysTyped(std::string_view typing,
                           std::function<bool(char32_t)> filter = nullptr,
                           bool multiline = false);
  static std::string HandleBackspaceChars(std::string_view typing,
                                          bool keep_unevaluated = false);

 private:
  struct PhysicalKey {
    int32_t presses = 0;
    int32_t absorbs = 0;
    int32_t hold_ticks = 0;
    uint8_t press_mods = 0;
    uint8_t clicked_mods = 0;
    bool down = false;
    bool clicked = false;
    bool sticky = false;
    bool stay_down = false;

    void Toggle(bool pressed, uint8_t mods = 0);
    void Tick();
    void Release();
  };

  void TogglePhysicalKey(std::string_view key_name, bool pressed);
  bool EvalKeyExpr(std::string_view expr, bool check_clicked, bool resolve_map);
  bool EvalOrGroup(std::string_view expr, bool check_clicked, bool resolve_map);
  bool EvalCompound(std::string_view expr, bool check_clicked);
  std::string GetCurModifiers();
  static bool IsModifier(std::string_view key_name);
  static std::string ToUpperAscii(std::string_view s);

  std::unordered_map<std::string, std::string> keymap_;
  std::vector<std::string> action_order_;
  std::unordered_map<std::string, PhysicalKey> keyboard_;
  std::unordered_map<std::string, platform::GamepadButton> button_map_;

  std::string key_to_change_;
  std::string key_changed_;
  bool overwrite_ = false;

  std::vector<std::function<bool(std::string_view)>> input_masks_;

  std::string last_key_typed_;
  std::string key_typed_buffer_;

  bool gamepad_connected_ = false;
  bool gamepad_down_[static_cast<size_t>(platform::GamepadButton::kCount)] = {};
  bool gamepad_just_[static_cast<size_t>(platform::GamepadButton::kCount)] = {};
  bool gamepad_just_pending_[static_cast<size_t>(
      platform::GamepadButton::kCount)] = {};
  float left_trigger_ = 0.0f;
  float right_trigger_ = 0.0f;
  int32_t left_trigger_cooldown_ = 0;
  int32_t right_trigger_cooldown_ = 0;

  uint64_t last_key_activity_ms_ = 0;
  uint64_t last_button_activity_ms_ = 0;
};

}  // namespace io
}  // namespace minicraft

#endif  // MINICRAFT_CORE_IO_INPUT_HANDLER_H_
