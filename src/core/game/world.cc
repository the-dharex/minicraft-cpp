#include "core/game/world.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "core/entity/mob.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/game/updater.h"
#include "core/io/settings.h"
#include "core/item/inventory.h"
#include "core/item/recipes.h"
#include "core/level/level_gen.h"
#include "core/level/tile.h"
#include "core/level/tiles.h"
#include "core/platform/file_system.h"
#include "core/platform/time_clock.h"
#include "core/saveload/load.h"
#include "core/saveload/save.h"
#include "core/screen/displays.h"
#include "core/util/advancements.h"
#include "core/util/logging.h"
#include "core/util/random.h"

namespace minicraft::game {

World::~World() { WaitForPendingSave(); }

void World::WaitForPendingSave() {
  if (save_worker_.joinable()) {
    save_worker_.join();
  }
  save_in_progress_.store(false, std::memory_order_release);
}

int32_t World::LvlIdx(int32_t depth) {
  if (depth > kMaxLevelDepth) return LvlIdx(kMinLevelDepth);
  if (depth < kMinLevelDepth) return LvlIdx(kMaxLevelDepth);
  if (depth == -4) return 5;
  return depth + 3;
}

level::Level* World::CurrentLevel() const {
  if (current_level_ < 0 || current_level_ >= 6) return nullptr;
  return levels_[current_level_].get();
}

void World::SetCurrentLevelIdx(int32_t idx) {
  current_level_ = std::clamp<int32_t>(idx, 0, 5);
}

void World::FindSafeStartPos(level::Level& level, entity::Player& player,
                             int64_t seed) {
  util::Random rng(seed);
  const int32_t cx = level.w / 2;
  const int32_t cy = level.h / 2;

  // Prefer Grass or Sand within central 64x64 chunks.
  for (int32_t attempt = 0; attempt < 256; ++attempt) {
    const int32_t tx = std::clamp(cx + rng.NextInt(64) - 32, 4, level.w - 5);
    const int32_t ty = std::clamp(cy + rng.NextInt(64) - 32, 4, level.h - 5);
    const level::Tile* t = level.GetTile(tx, ty);
    if (t && (t->id == level::Tiles::kGrass || t->id == level::Tiles::kSand)) {
      const int32_t px = tx * 16 + 8;
      const int32_t py = ty * 16 + 8;
      if (entity::Mob::CheckStartPos(level, px, py)) {
        player.x = px;
        player.y = py;
        player.SetSpawnPosition(px, py, level.depth);
        return;
      }
    }
  }

  // Deterministic outward spiral fallback.
  for (int32_t r = 0; r < 64; ++r) {
    for (int32_t dy = -r; dy <= r; ++dy) {
      for (int32_t dx = -r; dx <= r; ++dx) {
        if (std::abs(dx) != r && std::abs(dy) != r) continue;
        const int32_t tx = cx + dx;
        const int32_t ty = cy + dy;
        const int32_t px = tx * 16 + 8;
        const int32_t py = ty * 16 + 8;
        if (entity::Mob::CheckStartPos(level, px, py)) {
          player.x = px;
          player.y = py;
          player.SetSpawnPosition(px, py, level.depth);
          return;
        }
      }
    }
  }

  // Ultimate guarantee: carve safe center tile.
  level.SetTile(cx, cy, level::Tiles::kGrass, 0);
  const int32_t px = cx * 16 + 8;
  const int32_t py = cy * 16 + 8;
  player.x = px;
  player.y = py;
  player.SetSpawnPosition(px, py, level.depth);
}

void World::ResetGame(io::InputHandler* input, bool keep_player,
                      bool respawn_into_level) {
  util::log::kWorld.Debug("Resetting game state...");
  player_dead_time_ = 0;
  current_level_ = kSurfaceLevelIdx;

  player_ = std::make_shared<entity::Player>(keep_player ? player_ : nullptr,
                                             input);

  if (respawn_into_level && levels_[current_level_] != nullptr) {
    RespawnPlayer(input);
  }
}

void World::RespawnPlayer(io::InputHandler* /*input*/) {
  current_level_ = LvlIdx(player_ ? player_->spawn_depth : 0);
  level::Level* lvl = CurrentLevel();
  if (!lvl) {
    current_level_ = kSurfaceLevelIdx;
    lvl = CurrentLevel();
  }
  if (!lvl || !player_) return;

  player_->ResetForRespawn();
  player_->x = player_->spawnx;
  player_->y = player_->spawny;

  // Ensure the player won't respawn trapped inside solid walls/lava/water.
  const int32_t tx = player_->x >> 4;
  const int32_t ty = player_->y >> 4;
  const level::Tile* t = lvl->GetTile(tx, ty);
  if (!t || !t->MayPass(*lvl, tx, ty, *player_)) {
    FindSafeStartPos(*lvl, *player_, seed_);
  }

  GameState::Instance().player_dead = false;
  player_dead_time_ = 0;
  lvl->Add(player_);
}

bool World::InitWorld(const platform::IFileSystem* fs,
                      const std::string& game_dir,
                      const std::string& world_name, bool is_new_world,
                      std::optional<int64_t> seed, io::InputHandler* input,
                      Updater* updater) {
  WaitForPendingSave();
  SetReadyToRenderGameplay(false);
  util::log::kWorld.Info("Initializing world: " + world_name);

  auto& gs = GameState::Instance();
  gs.ResetForNewWorld();
  gs.current_world_name = world_name;
  signs_.clear();
  pending_level_change_ = 0;
  on_change_action_ = nullptr;
  if (updater) {
    updater->ResetAutosaveAndNotifications();
  }

  player_ = std::make_shared<entity::Player>(nullptr, input);
  gs.score_time = io::Settings::GetInt("scoretime") * 60 * GameState::kNormSpeed;

  screen::LoadingDisplay::SetPercentage(0.0f);

  if (!is_new_world && fs != nullptr) {
    screen::CraftingDisplay::ResetRecipeUnlocks();
    std::vector<item::Recipe> unlocked_recipes;
    saveload::WorldSaveContext ctx;
    ctx.world_dir = saveload::Save::JoinWorldPath(
        saveload::Save::JoinWorldPath(game_dir, "saves"), world_name);
    ctx.levels = &levels_;
    ctx.player = &player_;
    ctx.current_level_idx = &current_level_;
    ctx.signs = &signs_;
    ctx.unlocked_recipes = &unlocked_recipes;
    ctx.on_progress = [](const std::string& key) {
      screen::LoadingDisplay::SetMessage(key, true);
      screen::LoadingDisplay::Progress(18.0f);
    };
    if (!saveload::Load::LoadWorld(*fs, ctx)) {
      util::log::kWorld.Error("Failed loading saved world: " + world_name);
      return false;
    }
    screen::CraftingDisplay::LoadUnlockedRecipes(std::move(unlocked_recipes));
    for (const auto& el : util::AdvancementSystem::Instance().RecipeUnlocks()) {
      if (el && el->completed) {
        for (const auto& rr : el->rewards.recipes) {
          screen::CraftingDisplay::UnlockRecipe(
              item::Recipe(rr.product, rr.costs));
        }
      }
    }
    if (player_) {
      player_->SetInput(input);
      util::AdvancementSystem::Instance().TriggerInventoryChanged(
          player_->GetInventory().ToSnapshot());
    }
    screen::TutorialDisplayHandler::Reset(false);
    screen::SignDisplay::SyncFromMap(signs_);
  } else {
    world_size_ = io::Settings::GetInt("size");
    if (world_size_ < 64) world_size_ = 128;

    seed_ = seed.value_or(
        static_cast<int64_t>(std::time(nullptr)) ^
        (static_cast<int64_t>(util::GlobalRandom().NextInt(1000000000)) << 21));
    util::Random rng(seed_);

    levels_ = {};
    constexpr float kLoadingInc =
        100.0f / static_cast<float>(kMaxLevelDepth - kMinLevelDepth + 1);
    for (int32_t d = kMaxLevelDepth; d >= kMinLevelDepth; --d) {
      const char* d_name = "Cave";
      if (d == 1) d_name = "Sky";
      else if (d == 0) d_name = "Surface";
      else if (d == -1) d_name = "Iron";
      else if (d == -2) d_name = "Gold";
      else if (d == -3) d_name = "Gem";
      else if (d == -4) d_name = "Dungeon";
      screen::LoadingDisplay::SetMessage(d_name, false);
      level::Level* above =
          (d < kMaxLevelDepth) ? levels_[LvlIdx(d + 1)].get() : nullptr;
      levels_[LvlIdx(d)] = std::make_unique<level::Level>(
          world_size_, world_size_, rng.NextLong(), d, above, false);
      screen::LoadingDisplay::Progress(kLoadingInc);
    }

    current_level_ = kSurfaceLevelIdx;
    level::Level* surface = levels_[current_level_].get();
    gs.past_day1 = false;
    gs.SetTimeOfDay(TimeOfDay::kMorning);
    FindSafeStartPos(*surface, *player_, seed_);
    surface->Add(player_);
    surface->FlushPendingEntities();

    screen::CraftingDisplay::ResetRecipeUnlocks();
    util::AdvancementSystem::Instance().ResetAll();
    screen::TutorialDisplayHandler::Reset(true);
    screen::SignDisplay::ResetSignTexts();
  }

  screen::LoadingDisplay::SetPercentage(100.0f);
  if (updater) {
    updater->SetSignDisplayMenu(nullptr);
  }
  SetReadyToRenderGameplay(true);
  return true;
}

void World::UnloadWorld(const platform::IClock* clock) {
  WaitForPendingSave();
  auto& gs = GameState::Instance();
  gs.bed_sleeping = false;
  gs.bed_sleep_players = 0;
  gs.game_speed = 1.0f;
  ready_to_render_gameplay_ = false;
  levels_ = {};
  player_.reset();
  signs_.clear();
  pending_level_change_ = 0;
  on_change_action_ = nullptr;
  player_dead_time_ = 0;
  current_level_ = kSurfaceLevelIdx;
  if (clock) {
    last_world_exit_time_ms_ = clock->NowMilliseconds();
  }
}

bool World::SaveCurrentWorld(const platform::IFileSystem& fs,
                             const std::string& game_dir,
                             const std::string& world_name) {
  WaitForPendingSave();
  if (world_name.empty() || !player_) return false;
  screen::SignDisplay::ExportToMap(&signs_);

  std::vector<item::Recipe> unlocked_recipes =
      screen::CraftingDisplay::UnlockedRecipes();
  saveload::WorldSaveContext ctx;
  ctx.world_dir = saveload::Save::JoinWorldPath(
      saveload::Save::JoinWorldPath(game_dir, "saves"), world_name);
  ctx.levels = &levels_;
  ctx.player = &player_;
  ctx.current_level_idx = &current_level_;
  ctx.signs = &signs_;
  ctx.unlocked_recipes = &unlocked_recipes;
  return saveload::Save::SaveWorld(fs, ctx);
}

bool World::SaveCurrentWorldAsync(const platform::IFileSystem& fs,
                                  const std::string& game_dir,
                                  const std::string& world_name,
                                  bool notify_on_complete) {
  if (world_name.empty() || !player_ || IsSaving()) return false;
  if (save_worker_.joinable()) {
    save_worker_.join();
  }

  auto& gs = GameState::Instance();
  gs.saving = true;
  save_in_progress_.store(true, std::memory_order_release);
  screen::LoadingDisplay::SetPercentage(0.0f);

  screen::SignDisplay::ExportToMap(&signs_);
  std::vector<item::Recipe> unlocked_recipes =
      screen::CraftingDisplay::UnlockedRecipes();
  saveload::WorldSaveContext ctx;
  ctx.world_dir = saveload::Save::JoinWorldPath(
      saveload::Save::JoinWorldPath(game_dir, "saves"), world_name);
  ctx.levels = &levels_;
  ctx.player = &player_;
  ctx.current_level_idx = &current_level_;
  ctx.signs = &signs_;
  ctx.unlocked_recipes = &unlocked_recipes;

  saveload::WorldSaveSnapshot snapshot;
  if (!saveload::Save::CaptureWorldSnapshot(ctx, &snapshot)) {
    gs.saving = false;
    save_in_progress_.store(false, std::memory_order_release);
    return false;
  }

  save_worker_ = std::thread(
      [this, &fs, snapshot = std::move(snapshot), notify_on_complete]() {
        saveload::Save::WriteWorldSnapshot(
            fs, snapshot, notify_on_complete, [](float pct) {
              screen::LoadingDisplay::SetPercentage(pct);
            });
        GameState::Instance().saving = false;
        save_in_progress_.store(false, std::memory_order_release);
      });
  return true;
}

void World::ScheduleLevelChange(int32_t dir,
                                std::function<void()> on_change_action) {
  on_change_action_ = std::move(on_change_action);
  pending_level_change_ = dir;
}

int32_t World::ConsumePendingLevelChange() {
  int32_t d = pending_level_change_;
  pending_level_change_ = 0;
  return d;
}

void World::ChangeLevel(int32_t dir) {
  if (!player_) return;
  if (levels_[current_level_]) {
    levels_[current_level_]->Remove(player_.get());
    levels_[current_level_]->PurgeRemovedEntities();
  }

  int32_t next_level = current_level_ + dir;
  if (next_level < 0) next_level = 5;
  if (next_level >= 6) next_level = 0;
  current_level_ = next_level;

  player_->x = (player_->x >> 4) * 16 + 8;
  player_->y = (player_->y >> 4) * 16 + 8;

  if (levels_[current_level_]) {
    levels_[current_level_]->Add(player_);
  }

  if (on_change_action_) {
    on_change_action_();
    on_change_action_ = nullptr;
  }

  auto& gs = GameState::Instance();
  if (current_level_ == 0) {
    util::AchievementManager::Instance().SetAchievement(
        "minicraft.achievement.lowest_caves", true, gs.IsCreativeMode());
  } else if (current_level_ == 5) {
    util::AchievementManager::Instance().SetAchievement(
        "minicraft.achievement.obsidian_dungeon", true, gs.IsCreativeMode());
  }
}

void World::MarkWorldEntered(const platform::IClock* clock) {
  if (clock) {
    last_world_enter_time_ms_ = clock->NowMilliseconds();
  }
}

}  // namespace minicraft::game
