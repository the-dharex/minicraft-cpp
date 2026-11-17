#ifndef MINICRAFT_CORE_GAME_UPDATER_H_
#define MINICRAFT_CORE_GAME_UPDATER_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/screen/display.h"
#include "core/screen/displays.h"

namespace minicraft {
namespace io {
class InputHandler;
}
namespace pak {
class PakReader;
}
namespace platform {
struct PlatformContext;
}
namespace gfx {
class SpriteLinker;
}
}  // namespace minicraft

namespace minicraft::game {

class World;

// Fixed-timestep (60 TPS) coordinator: manages display navigation stack,
// notifications, autosave timer, bed sleep speedup, score countdown,
// gameplay hotkeys, and GameState/DisplayHost hooks.
class Updater {
 public:
  static constexpr int32_t kAutosaveIntervalTicks = 7200;  // 2 minutes at 60Hz.

  Updater() = default;
  ~Updater();

  Updater(const Updater&) = delete;
  Updater& operator=(const Updater&) = delete;

  void WireHostBridges(World& world, io::InputHandler& input,
                       const platform::PlatformContext& platform,
                       const pak::PakReader& pak, gfx::SpriteLinker& linker,
                       const std::string& game_dir,
                       std::function<void()> on_quit,
                       std::function<void()> on_reload_packs);

  void SetDisplay(std::unique_ptr<screen::Display> display);
  void ExitDisplay(int32_t depth = 1);
  void ClearDisplays();
  screen::Display* CurrentDisplay() const;
  bool HasActiveDisplay() const;

  void Tick(World& world, io::InputHandler& input,
            const platform::PlatformContext& platform,
            const std::string& game_dir);

  void AddNotification(std::string msg, int32_t initial_notetick = 0);
  void ResetAutosaveAndNotifications();

  std::vector<std::string> Notifications() const {
    std::lock_guard<std::mutex> lock(notifications_mutex_);
    return notifications_;
  }
  int32_t NoteTick() const {
    std::lock_guard<std::mutex> lock(notifications_mutex_);
    return notetick_;
  }
  void AdvanceNoteTickIfNeeded(bool update);

  int32_t PendingScreenshots() const { return screenshot_requests_; }
  void ConsumeScreenshotRequest();

  const screen::SignDisplayMenu* SignOverlay() const {
    return sign_overlay_.get();
  }
  void SetSignDisplayMenu(std::unique_ptr<screen::SignDisplayMenu> menu);

  bool ShowDebugInfo() const { return show_debug_info_; }

 private:
  struct DisplayOp {
    enum class Kind { kPush, kPop, kClear };
    Kind kind = Kind::kPush;
    std::unique_ptr<screen::Display> display;
    int32_t pop_depth = 1;
  };

  void ProcessPendingDisplayOps();
  void TickGameplayInput(World& world, io::InputHandler& input);
  void RefreshSignOverlay(World& world);

  std::vector<std::unique_ptr<screen::Display>> display_stack_;
  std::deque<DisplayOp> pending_ops_;

  mutable std::mutex notifications_mutex_;
  std::vector<std::string> notifications_;
  int32_t notetick_ = 0;
  bool update_notetick_ = false;

  int32_t autosave_tick_ = 0;
  int32_t screenshot_requests_ = 0;
  bool show_debug_info_ = false;
  bool bridges_wired_ = false;

  std::unique_ptr<screen::SignDisplayMenu> sign_overlay_;
};

}  // namespace minicraft::game

#endif  // MINICRAFT_CORE_GAME_UPDATER_H_
