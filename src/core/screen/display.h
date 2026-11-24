#ifndef MINICRAFT_CORE_SCREEN_DISPLAY_H_
#define MINICRAFT_CORE_SCREEN_DISPLAY_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/saveload/save.h"
#include "core/screen/menu.h"

namespace minicraft {
namespace entity {
class Player;
}
namespace gfx {
class Screen;
}
namespace io {
class InputHandler;
}
namespace level {
class Level;
}
namespace pak {
class PakReader;
}
namespace platform {
class IFileSystem;
}
}  // namespace minicraft

namespace minicraft::screen {

class Display;

// Clean host bridge connecting screen/displays to Game, World, and IFileSystem
// without introducing circular header includes.
struct DisplayHost {
  static DisplayHost& Instance();

  io::InputHandler* input = nullptr;
  const platform::IFileSystem* fs = nullptr;
  const pak::PakReader* pak = nullptr;
  std::string game_dir;
  std::vector<std::string> active_resource_packs;

  std::function<void(std::unique_ptr<Display>)> set_display;
  std::function<void(int32_t count)> exit_display;
  std::function<Display*()> get_display;
  std::function<void()> quit_game;
  std::function<bool(const std::string& world_name, bool is_new_world,
                     std::optional<int64_t> seed)>
      start_world;
  std::function<void()> exit_world;
  std::function<void()> save_world;
  bool save_world_silent_once = false;
  std::function<void()> save_prefs;
  std::function<void()> respawn_player;
  std::function<void(int32_t dir)> change_level;
  std::function<void(const std::string& skin_name)> reload_skins;
  std::function<void()> reload_resource_packs;

  std::function<entity::Player*()> get_player;
  std::function<level::Level*()> get_current_level;
  std::function<int32_t()> get_current_level_idx;
  std::function<saveload::SignMap*()> get_signs;
  std::function<std::string()> get_clipboard;
  std::function<void(std::string_view)> set_clipboard;
  std::function<uint16_t(int32_t w, int32_t h, const uint8_t* rgba)>
      create_texture;
  std::function<void(uint16_t texture_id)> destroy_texture;
  std::function<void(const std::string& msg)> add_notification;
  std::function<void()> clear_notifications;

  static std::string ClipboardText();
  static void SetClipboardText(std::string_view text);
  static uint16_t CreateTexture(int32_t w, int32_t h, const uint8_t* rgba);
  static void DestroyTexture(uint16_t texture_id);
  static void AddNotification(const std::string& msg);
  static void SetDisplay(std::unique_ptr<Display> disp);
  static void ExitDisplay(int32_t count = 1);
  static Display* GetDisplay();
};

class Display {
 public:
  Display();
  explicit Display(std::vector<std::unique_ptr<Menu>> menus);
  explicit Display(bool clear_screen);
  Display(bool clear_screen, std::vector<std::unique_ptr<Menu>> menus);
  Display(bool clear_screen, bool can_exit);
  Display(bool clear_screen, bool can_exit,
          std::vector<std::unique_ptr<Menu>> menus);
  virtual ~Display() = default;

  virtual void Init(Display* parent);
  virtual void OnExit() {}

  Display* GetParent() const { return parent_; }
  void DetachParent() {
    parent_ = nullptr;
    set_parent_ = false;
  }

  virtual void Tick(io::InputHandler& input);
  virtual void Render(gfx::Screen* screen) const;

  const std::vector<std::unique_ptr<Menu>>& Menus() const { return menus_; }

 protected:
  virtual void OnSelectionChange(int32_t old_sel, int32_t new_sel);

  std::vector<std::unique_ptr<Menu>> menus_;
  int32_t selection_ = 0;

 private:
  Display* parent_ = nullptr;
  bool set_parent_ = false;
  bool can_exit_ = true;
  bool clear_screen_ = false;
};

}  // namespace minicraft::screen

#endif  // MINICRAFT_CORE_SCREEN_DISPLAY_H_
