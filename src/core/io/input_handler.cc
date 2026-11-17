#include "core/io/input_handler.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "core/gfx/font.h"

namespace minicraft {
namespace io {

namespace {

std::string GamepadButtonName(platform::GamepadButton btn) {
  using B = platform::GamepadButton;
  switch (btn) {
    case B::kDpadUp:
      return "DPAD-UP";
    case B::kDpadDown:
      return "DPAD-DOWN";
    case B::kDpadLeft:
      return "DPAD-LEFT";
    case B::kDpadRight:
      return "DPAD-RIGHT";
    case B::kA:
      return "A";
    case B::kB:
      return "B";
    case B::kX:
      return "X";
    case B::kY:
      return "Y";
    case B::kLeftBumper:
      return "LEFTBUMPER";
    case B::kRightBumper:
      return "RIGHTBUMPER";
    case B::kStart:
      return "START";
    case B::kBack:
      return "BACK";
    case B::kLeftStick:
      return "LEFTSTICK";
    case B::kRightStick:
      return "RIGHTSTICK";
    default:
      return "";
  }
}

void AppendCodepointUtf8(char32_t cp, std::string* out) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

namespace {

uint8_t ModifierBit(std::string_view key_name) {
  if (key_name == "SHIFT") return 1;
  if (key_name == "CTRL") return 2;
  if (key_name == "ALT") return 4;
  return 0;
}

}  // namespace

void InputHandler::PhysicalKey::Toggle(bool pressed, uint8_t mods) {
  down = pressed;
  if (pressed && !sticky) {
    ++presses;
    press_mods = mods;
  } else if (!pressed) {
    hold_ticks = 0;
  }
}

void InputHandler::PhysicalKey::Tick() {
  if (absorbs < presses) {
    ++absorbs;
    if (presses - absorbs > 3) absorbs = presses - 3;
    clicked = true;
    clicked_mods = press_mods;
  } else {
    if (!sticky) {
      sticky = (presses > 3) || (down && !stay_down && hold_ticks >= 30);
    } else {
      sticky = down;
    }
    clicked = sticky;
    presses = 0;
    absorbs = 0;
    press_mods = 0;
    clicked_mods = 0;
  }
  if (down) {
    if (hold_ticks < 60) ++hold_ticks;
  } else {
    hold_ticks = 0;
  }
}

void InputHandler::PhysicalKey::Release() {
  down = false;
  clicked = false;
  presses = 0;
  absorbs = 0;
  hold_ticks = 0;
  press_mods = 0;
  clicked_mods = 0;
  sticky = false;
}

InputHandler::InputHandler() {
  InitKeyMap();
  InitButtonMap();
  keyboard_["SHIFT"].stay_down = true;
  keyboard_["CTRL"].stay_down = true;
  keyboard_["ALT"].stay_down = true;
}

std::string InputHandler::ToUpperAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

void InputHandler::InitKeyMap() {
  keymap_.clear();
  action_order_.clear();

  auto bind = [&](const char* action, const char* keys) {
    keymap_[action] = keys;
    action_order_.emplace_back(action);
  };

  bind("MOVE-UP", "UP|W");
  bind("MOVE-DOWN", "DOWN|S");
  bind("MOVE-LEFT", "LEFT|A");
  bind("MOVE-RIGHT", "RIGHT|D");

  bind("CURSOR-UP", "UP");
  bind("CURSOR-DOWN", "DOWN");
  bind("CURSOR-LEFT", "LEFT");
  bind("CURSOR-RIGHT", "RIGHT");

  bind("SELECT", "ENTER");
  bind("EXIT", "ESCAPE");

  bind("QUICKSAVE", "R");

  bind("ATTACK", "C|SPACE|ENTER");
  bind("MENU", "X|E");
  bind("CRAFT", "Z|SHIFT-E");
  bind("PICKUP", "V|P");
  bind("DROP-ONE", "Q");
  bind("DROP-STACK", "SHIFT-Q");

  bind("SEARCHER-BAR", "SHIFT-F");
  bind("PAGE-UP", "PAGE_UP");
  bind("PAGE-DOWN", "PAGE_DOWN");

  bind("PAUSE", "ESCAPE");

  bind("POTIONEFFECTS", "P");
  bind("SIMPPOTIONEFFECTS", "O");
  bind("EXPANDQUESTDISPLAY", "L");
  bind("TOGGLEHUD", "F1");
  bind("SCREENSHOT", "F2");
  bind("INFO", "SHIFT-I");

  bind("FULLSCREEN", "F11");
}

void InputHandler::InitButtonMap() {
  using B = platform::GamepadButton;
  button_map_.clear();
  button_map_["MOVE-UP"] = B::kDpadUp;
  button_map_["MOVE-DOWN"] = B::kDpadDown;
  button_map_["MOVE-LEFT"] = B::kDpadLeft;
  button_map_["MOVE-RIGHT"] = B::kDpadRight;

  button_map_["CURSOR-UP"] = B::kDpadUp;
  button_map_["CURSOR-DOWN"] = B::kDpadDown;
  button_map_["CURSOR-LEFT"] = B::kDpadLeft;
  button_map_["CURSOR-RIGHT"] = B::kDpadRight;

  button_map_["SELECT"] = B::kA;
  button_map_["EXIT"] = B::kB;

  button_map_["ATTACK"] = B::kA;
  button_map_["MENU"] = B::kX;
  button_map_["CRAFT"] = B::kY;
  button_map_["PICKUP"] = B::kLeftBumper;

  button_map_["SEARCHER-BAR"] = B::kStart;
  button_map_["PAUSE"] = B::kStart;

  button_map_["DROP-ONE"] = B::kRightBumper;
  button_map_["DROP-STACK"] = B::kRightStick;
}

void InputHandler::ResetKeyBindings() { InitKeyMap(); }

void InputHandler::PushKeyEvents(
    const std::vector<platform::KeyEvent>& key_events) {
  for (const auto& ev : key_events) {
    std::string name = ToUpperAscii(ev.key_name);
    if (!ev.repeat) {
      TogglePhysicalKey(name, ev.pressed);
    }
    if (ev.pressed && (name == "BACKSPACE")) {
      key_typed_buffer_.push_back('\b');
    } else if (ev.pressed && (name == "ENTER")) {
      key_typed_buffer_.push_back('\n');
    }
  }
}

void InputHandler::PushCharEvents(
    const std::vector<platform::CharEvent>& char_events) {
  for (const auto& ev : char_events) {
    if (ev.codepoint == '\b' || ev.codepoint >= 0x20) {
      AppendCodepointUtf8(ev.codepoint, &key_typed_buffer_);
    }
  }
}

void InputHandler::UpdateGamepad(const platform::GamepadState& gamepad,
                                 uint64_t now_ms) {
  gamepad_connected_ = gamepad.connected;
  left_trigger_ = gamepad.left_trigger;
  right_trigger_ = gamepad.right_trigger;
  bool any_pressed = false;
  for (size_t i = 0; i < static_cast<size_t>(platform::GamepadButton::kCount);
       ++i) {
    bool cur = gamepad.connected && gamepad.buttons[i];
    if (cur && !gamepad_down_[i]) {
      gamepad_just_pending_[i] = true;
    }
    gamepad_down_[i] = cur;
    if (cur) any_pressed = true;
  }
  if (any_pressed) last_button_activity_ms_ = now_ms;
}

void InputHandler::Tick(uint64_t now_ms) {
  last_key_typed_ = std::move(key_typed_buffer_);
  key_typed_buffer_.clear();
  input_masks_.clear();

  for (size_t i = 0; i < static_cast<size_t>(platform::GamepadButton::kCount);
       ++i) {
    gamepad_just_[i] = gamepad_just_pending_[i];
    gamepad_just_pending_[i] = false;
  }

  bool any_key_down = false;
  for (auto& [name, key] : keyboard_) {
    key.Tick();
    if (key.down) any_key_down = true;
  }
  if (any_key_down) {
    last_key_activity_ms_ = now_ms;
  }

  if (left_trigger_cooldown_ > 0) --left_trigger_cooldown_;
  if (right_trigger_cooldown_ > 0) --right_trigger_cooldown_;
}

void InputHandler::ReleaseAll() {
  for (auto& [name, key] : keyboard_) {
    key.Release();
  }
  for (size_t i = 0; i < static_cast<size_t>(platform::GamepadButton::kCount);
       ++i) {
    gamepad_down_[i] = false;
    gamepad_just_[i] = false;
    gamepad_just_pending_[i] = false;
  }
}

void InputHandler::MaskInput(std::function<bool(std::string_view)> filter) {
  if (!filter) {
    input_masks_.push_back([](std::string_view) { return true; });
  } else {
    input_masks_.push_back(std::move(filter));
  }
}

bool InputHandler::IsModifier(std::string_view key_name) {
  return key_name == "SHIFT" || key_name == "CTRL" || key_name == "ALT";
}

std::string InputHandler::GetCurModifiers() {
  std::string out;
  if (PhysicalKeyDown("CTRL")) out += "CTRL-";
  if (PhysicalKeyDown("ALT")) out += "ALT-";
  if (PhysicalKeyDown("SHIFT")) out += "SHIFT-";
  return out;
}

void InputHandler::TogglePhysicalKey(std::string_view key_name, bool pressed) {
  std::string upper = ToUpperAscii(key_name);
  if (pressed && !key_to_change_.empty() && !IsModifier(upper)) {
    std::string new_bind =
        (overwrite_ ? std::string() : (keymap_[key_to_change_] + "|")) +
        GetCurModifiers() + upper;
    keymap_[key_to_change_] = std::move(new_bind);
    key_changed_ = key_to_change_;
    key_to_change_.clear();
    return;
  }
  uint8_t mods = 0;
  if (pressed && !IsModifier(upper)) {
    if (PhysicalKeyDown("SHIFT")) mods |= 1;
    if (PhysicalKeyDown("CTRL")) mods |= 2;
    if (PhysicalKeyDown("ALT")) mods |= 4;
  }
  keyboard_[upper].Toggle(pressed, mods);
}

bool InputHandler::EvalCompound(std::string_view expr, bool check_clicked) {
  if (expr.empty()) return false;

  // Split by '-' into physical key parts.
  std::vector<std::string> parts;
  size_t pos = 0;
  while (pos <= expr.size()) {
    size_t dash = expr.find('-', pos);
    std::string_view part = (dash == std::string_view::npos)
                                ? expr.substr(pos)
                                : expr.substr(pos, dash - pos);
    if (!part.empty()) {
      std::string upper = ToUpperAscii(part);
      for (const auto& m : input_masks_) {
        if (m(upper)) return false;
      }
      parts.push_back(std::move(upper));
    }
    if (dash == std::string_view::npos) break;
    pos = dash + 1;
  }
  if (parts.empty()) return false;

  if (parts.size() == 1) {
    const PhysicalKey& pk = keyboard_[parts[0]];
    return check_clicked ? pk.clicked : pk.down;
  }

  bool all_down = true;
  bool any_clicked = false;
  uint8_t req_mods = 0;
  bool has_non_mod = false;
  bool non_mods_ok = true;
  bool non_mod_clicked_with_mods = false;

  for (const std::string& upper : parts) {
    const PhysicalKey& pk = keyboard_[upper];
    if (!pk.down) all_down = false;
    if (pk.clicked) any_clicked = true;
    const uint8_t mb = ModifierBit(upper);
    if (mb != 0) {
      req_mods |= mb;
    } else {
      has_non_mod = true;
      if (!pk.down && !pk.clicked) non_mods_ok = false;
    }
  }
  if (all_down && (!check_clicked || any_clicked)) return true;

  if (check_clicked && req_mods != 0 && has_non_mod && non_mods_ok) {
    for (const std::string& upper : parts) {
      if (ModifierBit(upper) != 0) continue;
      const PhysicalKey& pk = keyboard_[upper];
      if (pk.clicked && (pk.clicked_mods & req_mods) == req_mods) {
        non_mod_clicked_with_mods = true;
        break;
      }
    }
    if (non_mod_clicked_with_mods) return true;
  }
  return false;
}

bool InputHandler::EvalOrGroup(std::string_view expr, bool check_clicked,
                               bool resolve_map) {
  size_t pos = 0;
  while (pos <= expr.size()) {
    size_t bar = expr.find('|', pos);
    std::string_view branch = (bar == std::string_view::npos)
                                  ? expr.substr(pos)
                                  : expr.substr(pos, bar - pos);
    if (!branch.empty()) {
      // Support '+' compound mappings (e.g. SHIFT+CURSOR-LEFT or SHIFT+SELECT).
      if (resolve_map && branch.find('+') != std::string_view::npos) {
        bool all_down = true;
        bool any_clicked = false;
        size_t p2 = 0;
        while (p2 <= branch.size()) {
          size_t plus = branch.find('+', p2);
          std::string_view sub = (plus == std::string_view::npos)
                                     ? branch.substr(p2)
                                     : branch.substr(p2, plus - p2);
          if (!InputDown(sub)) {
            all_down = false;
            break;
          }
          if (check_clicked && InputPressed(sub)) any_clicked = true;
          if (plus == std::string_view::npos) break;
          p2 = plus + 1;
        }
        if (all_down && (!check_clicked || any_clicked)) return true;
      } else if (EvalCompound(branch, check_clicked)) {
        return true;
      }
    }
    if (bar == std::string_view::npos) break;
    pos = bar + 1;
  }
  return false;
}

bool InputHandler::EvalKeyExpr(std::string_view expr, bool check_clicked,
                               bool resolve_map) {
  std::string upper = ToUpperAscii(expr);
  if (resolve_map) {
    auto it = keymap_.find(upper);
    if (it != keymap_.end()) {
      return EvalOrGroup(it->second, check_clicked, false);
    }
  }
  return EvalOrGroup(upper, check_clicked, resolve_map);
}

bool InputHandler::InputPressed(std::string_view mapping) {
  std::string upper = ToUpperAscii(mapping);
  if (EvalKeyExpr(upper, true, true)) return true;
  auto it = button_map_.find(upper);
  return it != button_map_.end() && ButtonPressed(it->second);
}

bool InputHandler::InputDown(std::string_view mapping) {
  std::string upper = ToUpperAscii(mapping);
  if (EvalKeyExpr(upper, false, true)) return true;
  auto it = button_map_.find(upper);
  return it != button_map_.end() && ButtonDown(it->second);
}

bool InputHandler::PhysicalKeyPressed(std::string_view key_name) {
  return EvalKeyExpr(key_name, true, false);
}

bool InputHandler::PhysicalKeyDown(std::string_view key_name) {
  return EvalKeyExpr(key_name, false, false);
}

bool InputHandler::ButtonPressed(platform::GamepadButton btn) const {
  return gamepad_just_[static_cast<size_t>(btn)];
}

bool InputHandler::ButtonDown(platform::GamepadButton btn) const {
  return gamepad_down_[static_cast<size_t>(btn)];
}

bool InputHandler::LeftTriggerPressed() {
  if (gamepad_connected_ && left_trigger_cooldown_ == 0 && left_trigger_ > 0.5f) {
    left_trigger_cooldown_ = 8;
    return true;
  }
  return false;
}

bool InputHandler::RightTriggerPressed() {
  if (gamepad_connected_ && right_trigger_cooldown_ == 0 &&
      right_trigger_ > 0.5f) {
    right_trigger_cooldown_ = 8;
    return true;
  }
  return false;
}

void InputHandler::SetKey(std::string_view keymap_key,
                          std::string_view keyboard_key) {
  std::string k = ToUpperAscii(keymap_key);
  if (keymap_.find(k) != keymap_.end()) {
    keymap_[k] = ToUpperAscii(keyboard_key);
  }
}

void InputHandler::ChangeKeyBinding(std::string_view action_key) {
  key_to_change_ = ToUpperAscii(action_key);
  overwrite_ = true;
}

void InputHandler::AddKeyBinding(std::string_view action_key) {
  key_to_change_ = ToUpperAscii(action_key);
  overwrite_ = false;
}

std::string InputHandler::GetChangedKey() {
  std::string out = key_changed_ + ";" + keymap_[key_changed_];
  key_changed_.clear();
  return out;
}

std::vector<std::string> InputHandler::GetKeyPrefs() const {
  std::vector<std::string> prefs;
  prefs.reserve(action_order_.size());
  for (const auto& action : action_order_) {
    auto it = keymap_.find(action);
    if (it != keymap_.end()) {
      prefs.push_back(action + ";" + it->second);
    }
  }
  return prefs;
}

std::string InputHandler::GetMapping(std::string_view action_key) const {
  std::string upper = ToUpperAscii(action_key);
  if (last_button_activity_ms_ > last_key_activity_ms_) {
    auto bit = button_map_.find(upper);
    if (bit != button_map_.end()) {
      return GamepadButtonName(bit->second);
    }
  }
  auto it = keymap_.find(upper);
  if (it != keymap_.end()) {
    std::string s = it->second;
    std::replace(s.begin(), s.end(), '|', '/');
    return s;
  }
  return "NO_KEY";
}

std::string InputHandler::SelectMapping(std::string_view key_map,
                                        std::string_view button_map) const {
  return (last_button_activity_ms_ > last_key_activity_ms_)
             ? std::string(button_map)
             : std::string(key_map);
}

int32_t InputHandler::GetLastInputType() const {
  return (last_button_activity_ms_ > last_key_activity_ms_) ? 1 : 0;
}

std::vector<std::string> InputHandler::GetAllPressedKeys() const {
  std::vector<std::string> out;
  for (const auto& [name, key] : keyboard_) {
    if (key.down) out.push_back(name);
  }
  return out;
}

void InputHandler::PushVirtualChar(char32_t cp) {
  AppendCodepointUtf8(cp, &last_key_typed_);
}

std::string InputHandler::AddKeyTyped(std::string_view typing,
                                      std::function<bool(char32_t)> filter) {
  return HandleBackspaceChars(GetKeysTyped(typing, std::move(filter)));
}

std::string InputHandler::GetKeysTyped(std::string_view typing,
                                       std::function<bool(char32_t)> filter,
                                       bool multiline) {
  std::string out(typing);
  if (!last_key_typed_.empty()) {
    size_t off = 0;
    while (off < last_key_typed_.size()) {
      char32_t cp = gfx::Font::NextUtf8Codepoint(last_key_typed_, &off);
      bool printable = (cp >= 0x20 && cp != 0x7F) || (cp == '\n' && multiline);
      if (cp == '\b' || (printable && (!filter || filter(cp)))) {
        AppendCodepointUtf8(cp, &out);
      }
    }
    last_key_typed_.clear();
  }
  return out;
}

std::string InputHandler::HandleBackspaceChars(std::string_view typing,
                                               bool keep_unevaluated) {
  std::vector<char32_t> stack;
  size_t off = 0;
  while (off < typing.size()) {
    char32_t cp = gfx::Font::NextUtf8Codepoint(typing, &off);
    if (cp == '\b' && !stack.empty() && stack.back() != '\b') {
      stack.pop_back();
    } else if (cp != '\b' || keep_unevaluated) {
      stack.push_back(cp);
    }
  }
  std::string out;
  out.reserve(typing.size());
  for (char32_t cp : stack) {
    AppendCodepointUtf8(cp, &out);
  }
  return out;
}

}  // namespace io
}  // namespace minicraft
