#ifndef MINICRAFT_CORE_GAME_GAME_STATE_H_
#define MINICRAFT_CORE_GAME_GAME_STATE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace minicraft {
namespace gfx {
class SpriteLinker;
}  // namespace gfx
namespace item {
class Inventory;
class Recipe;
}  // namespace item
namespace entity {
class Chest;
class Player;
}  // namespace entity
namespace level {
class Level;
}  // namespace level
}  // namespace minicraft

namespace minicraft::game {

enum class TimeOfDay : uint8_t {
  kMorning = 0,
  kDay = 1,
  kEvening = 2,
  kNight = 3,
};

// Decoupled runtime game state and UI/world action bridge used by level, entity,
// and item systems without creating circular header dependencies on Game/World/Screen.
class GameState {
 public:
  static constexpr int32_t kNormSpeed = 60;
  static constexpr int32_t kDayLength = 64800;  // 18 minutes at 60 tps
  static constexpr int32_t kSleepEndTime = kDayLength / 8;  // 8100: wake up in morning
  static constexpr int32_t kSleepStartTime =
      kDayLength / 2 + kDayLength / 8;  // 40500: earliest sleep tick

  static GameState& Instance();

  void ResetForNewWorld();

  // Mode & Difficulty helpers (backed by Settings)
  bool IsMode(std::string_view mode_name) const;
  bool IsCreativeMode() const;
  bool IsScoreMode() const;
  bool IsHardcoreMode() const;
  bool IsSurvivalMode() const;
  int32_t GetDifficultyIdx() const;  // 0=Easy, 1=Normal, 2=Hard

  // Time helpers
  TimeOfDay GetTimeOfDay() const;
  void AdvanceGameTime(int32_t ticks = 1);
  void SetTimeOfDay(TimeOfDay tod);
  void SetGameTime(int32_t time);

  int32_t GenerateUniqueEid();

  // Public state accessed across subsystems
  int32_t game_time = 6000;
  int32_t tick_count = 0;
  int32_t tile_tick_count = 0;
  int32_t score_time = 0;
  bool past_day1 = false;
  int32_t score = 0;
  float game_speed = 1.0f;
  std::atomic<bool> saving{false};
  bool paused = false;
  bool has_world = false;
  bool player_dead = false;
  bool won_game = false;

  // Boss & world progression
  bool air_wizard_beaten = false;
  bool obsidian_knight_beaten = false;
  bool obsidian_knight_summoned = false;

  // Bed & multiplayer-stub sleep tracking
  bool bed_sleeping = false;
  int32_t bed_sleep_players = 0;

  // Skin / world metadata
  int32_t selected_skin_index = 0;
  std::string selected_skin_name = "minicraft.skin.norman";
  std::string current_world_name;

  // Wall-clock milliseconds passed from platform clock
  uint64_t current_time_ms = 0;
  uint64_t last_world_enter_ms = 0;

  // Global sprite linker (owned by Renderer/Game)
  gfx::SpriteLinker* sprite_linker = nullptr;

  // Asset loading callback (reads text/binary entry from loaded pak/resource packs)
  std::function<bool(std::string_view, std::string*)> read_asset;

  // Hooks bound by Game / Updater / World / Display
  std::function<void(const std::string& msg, int32_t tick_time)> on_add_notification;
  std::function<void(const std::string& msg, int32_t delay)> on_notify_all;
  std::function<void(int32_t dir, std::function<void()> on_change)>
      on_schedule_level_change;
  std::function<void(const std::string& title, const std::string& text, bool has_title)>
      on_open_book;
  std::function<void(int32_t crafter_type)> on_open_crafting;
  std::function<void(entity::Chest* chest)> on_open_container;
  std::function<void(int32_t depth, int32_t x, int32_t y)> on_open_sign_editor;
  std::function<void(int32_t depth, int32_t x, int32_t y)> on_remove_sign;
  std::function<void(int32_t depth, int32_t x, int32_t y)> on_update_sign_overlay;
  std::function<void()> on_save_world;
  std::function<void()> on_end_game;
  std::function<void()> on_player_death;
  std::function<void(bool sleeping)> on_set_sleeping;
  std::function<std::vector<std::string>(int32_t depth, int32_t x, int32_t y)>
      on_get_sign_lines;
  std::function<void(int32_t depth, int32_t x, int32_t y,
                     const std::vector<std::string>& lines)>
      on_set_sign_lines;
  std::function<void(int32_t current_depth, int32_t x, int32_t y)> on_portal_warp;

  // Convenience dispatchers (safe no-ops if unbound)
  void AddNotification(const std::string& msg, int32_t tick_time = 200) const;
  void NotifyAll(const std::string& msg, int32_t delay = 0) const;
  void ScheduleLevelChange(int32_t dir,
                           std::function<void()> on_change = nullptr) const;
  void OpenBook(const std::string& title, const std::string& text,
                bool has_title) const;
  void OpenCrafting(int32_t crafter_type) const;
  void OpenContainer(entity::Chest* chest) const;
  void OpenSignEditor(int32_t depth, int32_t x, int32_t y) const;
  void RemoveSign(int32_t depth, int32_t x, int32_t y) const;
  void UpdateSignOverlay(int32_t depth, int32_t x, int32_t y) const;
  void SaveWorld() const;
  void EndGame() const;
  void TriggerPlayerDeath() const;
  bool ReadAssetText(std::string_view path, std::string* out) const;
  std::vector<std::string> GetSignLines(int32_t depth, int32_t x,
                                        int32_t y) const;
  void SetSignLines(int32_t depth, int32_t x, int32_t y,
                    const std::vector<std::string>& lines) const;

 private:
  GameState() = default;
};

}  // namespace minicraft::game

#endif  // MINICRAFT_CORE_GAME_GAME_STATE_H_
