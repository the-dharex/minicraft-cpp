#ifndef MINICRAFT_CORE_GAME_WORLD_H_
#define MINICRAFT_CORE_GAME_WORLD_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "core/level/level.h"
#include "core/saveload/save.h"

namespace minicraft {
namespace entity {
class Player;
}
namespace io {
class InputHandler;
}
namespace platform {
class IClock;
class IFileSystem;
}
}  // namespace minicraft

namespace minicraft::game {

class Updater;

// Manages the 6 world depth layers (-3, -2, -1, 0, 1, -4), the active player,
// world generation/loading, level transitions, and placed sign texts.
class World {
 public:
  static constexpr std::array<int32_t, 6> kIdxToDepth = {-3, -2, -1,
                                                         0,  1,  -4};
  static constexpr int32_t kMinLevelDepth = -4;
  static constexpr int32_t kMaxLevelDepth = 1;
  static constexpr int32_t kSurfaceLevelIdx = 3;

  World() = default;
  ~World();

  World(const World&) = delete;
  World& operator=(const World&) = delete;

  static int32_t LvlIdx(int32_t depth);

  void ResetGame(io::InputHandler* input, bool keep_player = true,
                 bool respawn_into_level = false);

  bool InitWorld(const platform::IFileSystem* fs, const std::string& game_dir,
                 const std::string& world_name, bool is_new_world,
                 std::optional<int64_t> seed, io::InputHandler* input,
                 Updater* updater);

  void UnloadWorld(const platform::IClock* clock = nullptr);

  bool SaveCurrentWorld(const platform::IFileSystem& fs,
                        const std::string& game_dir,
                        const std::string& world_name);

  bool SaveCurrentWorldAsync(const platform::IFileSystem& fs,
                             const std::string& game_dir,
                             const std::string& world_name,
                             bool notify_on_complete = true);

  void WaitForPendingSave();
  bool IsSaving() const {
    return save_in_progress_.load(std::memory_order_acquire);
  }

  void ScheduleLevelChange(int32_t dir,
                           std::function<void()> on_change_action = nullptr);
  void ChangeLevel(int32_t dir);

  void RespawnPlayer(io::InputHandler* input);

  // Accessors
  std::array<std::unique_ptr<level::Level>, 6>& Levels() { return levels_; }
  const std::array<std::unique_ptr<level::Level>, 6>& Levels() const {
    return levels_;
  }
  level::Level* CurrentLevel() const;
  int32_t CurrentLevelIdx() const { return current_level_; }
  void SetCurrentLevelIdx(int32_t idx);

  std::shared_ptr<entity::Player>& PlayerPtr() { return player_; }
  const std::shared_ptr<entity::Player>& PlayerPtr() const { return player_; }
  entity::Player* GetPlayer() const { return player_.get(); }

  saveload::SignMap& Signs() { return signs_; }
  const saveload::SignMap& Signs() const { return signs_; }

  int64_t WorldSeed() const { return seed_; }
  void SetWorldSeed(int64_t seed) { seed_ = seed; }

  int32_t PlayerDeadTime() const { return player_dead_time_; }
  void IncrementPlayerDeadTime() { ++player_dead_time_; }
  void ResetPlayerDeadTime() { player_dead_time_ = 0; }

  int32_t PendingLevelChange() const { return pending_level_change_; }
  int32_t ConsumePendingLevelChange();

  bool ReadyToRenderGameplay() const {
    return ready_to_render_gameplay_.load(std::memory_order_acquire);
  }
  void SetReadyToRenderGameplay(bool ready) {
    ready_to_render_gameplay_.store(ready, std::memory_order_release);
  }

  int64_t LastWorldExitTimeMs() const { return last_world_exit_time_ms_; }
  int64_t LastWorldEnterTimeMs() const { return last_world_enter_time_ms_; }
  void MarkWorldEntered(const platform::IClock* clock);

 private:
  static void FindSafeStartPos(level::Level& level, entity::Player& player,
                               int64_t seed);

  std::array<std::unique_ptr<level::Level>, 6> levels_;
  int32_t current_level_ = kSurfaceLevelIdx;
  std::shared_ptr<entity::Player> player_;
  saveload::SignMap signs_;

  int32_t world_size_ = 128;
  int64_t seed_ = 0;
  int32_t player_dead_time_ = 0;
  int32_t pending_level_change_ = 0;
  std::function<void()> on_change_action_;
  std::atomic<bool> ready_to_render_gameplay_{false};
  std::thread save_worker_;
  std::atomic<bool> save_in_progress_{false};

  int64_t last_world_exit_time_ms_ = 0;
  int64_t last_world_enter_time_ms_ = 0;
};

}  // namespace minicraft::game

#endif  // MINICRAFT_CORE_GAME_WORLD_H_
