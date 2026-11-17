#include "core/screen/display.h"

#include "core/gfx/screen.h"
#include "core/io/input_handler.h"
#include "core/io/sound.h"
#include "core/screen/entries.h"

namespace minicraft::screen {

namespace {
std::string g_fallback_clipboard;
}  // namespace

DisplayHost& DisplayHost::Instance() {
  static DisplayHost inst;
  return inst;
}

std::string DisplayHost::ClipboardText() {
  auto& h = Instance();
  if (h.get_clipboard) return h.get_clipboard();
  return g_fallback_clipboard;
}

void DisplayHost::SetClipboardText(std::string_view text) {
  g_fallback_clipboard.assign(text);
  auto& h = Instance();
  if (h.set_clipboard) h.set_clipboard(text);
}

uint16_t DisplayHost::CreateTexture(int32_t w, int32_t h, const uint8_t* rgba) {
  auto& host = Instance();
  return host.create_texture ? host.create_texture(w, h, rgba) : 0;
}

void DisplayHost::DestroyTexture(uint16_t texture_id) {
  if (texture_id == 0) return;
  auto& host = Instance();
  if (host.destroy_texture) host.destroy_texture(texture_id);
}

void DisplayHost::AddNotification(const std::string& msg) {
  auto& host = Instance();
  if (host.add_notification) host.add_notification(msg);
}

void DisplayHost::SetDisplay(std::unique_ptr<Display> disp) {
  auto& h = Instance();
  if (h.set_display) h.set_display(std::move(disp));
}

void DisplayHost::ExitDisplay(int32_t count) {
  auto& h = Instance();
  if (h.exit_display) h.exit_display(count);
}

Display* DisplayHost::GetDisplay() {
  auto& h = Instance();
  return h.get_display ? h.get_display() : nullptr;
}

Display::Display() : Display(false, true, {}) {}

Display::Display(std::vector<std::unique_ptr<Menu>> menus)
    : Display(false, true, std::move(menus)) {}

Display::Display(bool clear_screen) : Display(clear_screen, true, {}) {}

Display::Display(bool clear_screen, std::vector<std::unique_ptr<Menu>> menus)
    : Display(clear_screen, true, std::move(menus)) {}

Display::Display(bool clear_screen, bool can_exit)
    : Display(clear_screen, can_exit, {}) {}

Display::Display(bool clear_screen, bool can_exit,
                 std::vector<std::unique_ptr<Menu>> menus)
    : menus_(std::move(menus)),
      can_exit_(can_exit),
      clear_screen_(clear_screen) {}

void Display::Init(Display* parent) {
  if (!set_parent_) {
    set_parent_ = true;
    parent_ = parent;
  }
}

void Display::OnSelectionChange(int32_t /*old_sel*/, int32_t new_sel) {
  selection_ = new_sel;
}

void Display::Tick(io::InputHandler& input) {
  if (can_exit_ && input.InputPressed("exit")) {
    DisplayHost::ExitDisplay();
    return;
  }

  if (menus_.empty()) return;

  const int32_t num_menus = static_cast<int32_t>(menus_.size());
  selection_ = std::clamp(selection_, 0, num_menus - 1);

  bool changed_selection = false;
  if (num_menus > 1 && menus_[selection_] && menus_[selection_]->IsSelectable()) {
    const int32_t prev_sel = selection_;
    const bool is_setting =
        dynamic_cast<SettingListEntry*>(menus_[selection_]->GetCurEntry()) !=
        nullptr;
    const std::string_view left_key =
        is_setting ? "shift+cursor-left" : "cursor-left";
    const std::string_view right_key =
        is_setting ? "shift+cursor-right" : "cursor-right";

    int32_t delta = 0;
    if (input.InputPressed(left_key) || input.LeftTriggerPressed()) delta--;
    if (input.InputPressed(right_key) || input.RightTriggerPressed()) delta++;

    if (delta != 0) {
      io::Sound::Play("select");
      int32_t cur = prev_sel;
      do {
        cur = (cur + delta + num_menus) % num_menus;
      } while ((!menus_[cur] || !menus_[cur]->IsSelectable()) &&
               cur != prev_sel);

      if (cur != prev_sel) {
        changed_selection = true;
        OnSelectionChange(prev_sel, cur);
      }
    }
  }

  if (!changed_selection && menus_[selection_]) {
    menus_[selection_]->Tick(input);
  }
}

void Display::Render(gfx::Screen* screen) const {
  if (!screen) return;
  if (clear_screen_) {
    screen->Clear(0);
  } else if (set_parent_ && parent_) {
    parent_->Render(screen);
  }

  if (menus_.empty()) return;

  const int32_t n = static_cast<int32_t>(menus_.size());
  int32_t idx = std::clamp(selection_, 0, n - 1);
  for (int32_t count = 0; count < n; ++count) {
    idx = (idx + 1) % n;
    if (menus_[idx] && menus_[idx]->ShouldRender()) {
      menus_[idx]->Render(screen);
    }
  }
}

}  // namespace minicraft::screen
