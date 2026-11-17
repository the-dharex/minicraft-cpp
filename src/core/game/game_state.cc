#include "core/game/game_state.h"

#include <algorithm>
#include <cctype>

#include "core/io/settings.h"
#include "core/util/localization.h"
#include "core/util/random.h"

namespace minicraft::game {

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

GameState& GameState::Instance() {
  static GameState instance;
  return instance;
}

void GameState::ResetForNewWorld() {
  game_time = 0;
  tick_count = 0;
  tile_tick_count = 0;
  score_time = 0;
  past_day1 = false;
  score = 0;
  game_speed = 1.0f;
  saving = false;
  paused = false;
  player_dead = false;
  won_game = false;
  air_wizard_beaten = false;
  obsidian_knight_beaten = false;
  obsidian_knight_summoned = false;
  bed_sleeping = false;
  bed_sleep_players = 0;
}

bool GameState::IsMode(std::string_view mode_name) const {
  return EqualsIgnoreCase(io::Settings::GetString("mode"), mode_name);
}

bool GameState::IsCreativeMode() const {
  return IsMode("minicraft.settings.mode.creative") || IsMode("creative");
}

bool GameState::IsScoreMode() const {
  return IsMode("minicraft.settings.mode.score") || IsMode("score");
}

bool GameState::IsHardcoreMode() const {
  return IsMode("minicraft.settings.mode.hardcore") || IsMode("hardcore");
}

bool GameState::IsSurvivalMode() const {
  return IsMode("minicraft.settings.mode.survival") || IsMode("survival");
}

int32_t GameState::GetDifficultyIdx() const {
  return std::clamp<int32_t>(static_cast<int32_t>(io::Settings::GetIdx("diff")),
                             0, 2);
}

TimeOfDay GameState::GetTimeOfDay() const {
  int32_t t = game_time % kDayLength;
  if (t < 0) t += kDayLength;
  constexpr int32_t kQuarter = kDayLength / 4;
  if (t < kQuarter) return TimeOfDay::kMorning;
  if (t < kQuarter * 2) return TimeOfDay::kDay;
  if (t < kQuarter * 3) return TimeOfDay::kEvening;
  return TimeOfDay::kNight;
}

void GameState::AdvanceGameTime(int32_t ticks) {
  game_time += ticks;
  while (game_time >= kDayLength) {
    game_time -= kDayLength;
    past_day1 = true;
  }
  while (game_time < 0) {
    game_time += kDayLength;
  }
}

void GameState::SetTimeOfDay(TimeOfDay tod) {
  constexpr int32_t kQuarter = kDayLength / 4;
  switch (tod) {
    case TimeOfDay::kMorning:
      game_time = 0;
      break;
    case TimeOfDay::kDay:
      game_time = kQuarter;
      break;
    case TimeOfDay::kEvening:
      game_time = kQuarter * 2;
      break;
    case TimeOfDay::kNight:
      game_time = kQuarter * 3;
      break;
  }
}

void GameState::SetGameTime(int32_t time) {
  game_time = time % kDayLength;
  if (game_time < 0) game_time += kDayLength;
}

int32_t GameState::GenerateUniqueEid() {
  int32_t id = util::GlobalRandom().NextInt(2000000000) + 1;
  return id;
}

void GameState::AddNotification(const std::string& msg,
                                int32_t tick_time) const {
  if (on_add_notification) {
    on_add_notification(msg, tick_time);
  }
}

void GameState::NotifyAll(const std::string& msg, int32_t delay) const {
  if (on_notify_all) {
    on_notify_all(msg, delay);
  } else if (on_add_notification) {
    on_add_notification(util::Localization::GetLocalized(msg), 200);
  }
}

void GameState::ScheduleLevelChange(int32_t dir,
                                      std::function<void()> on_change) const {
  if (on_schedule_level_change) {
    on_schedule_level_change(dir, std::move(on_change));
  }
}

void GameState::OpenBook(const std::string& title, const std::string& text,
                         bool has_title) const {
  if (on_open_book) {
    on_open_book(title, text, has_title);
  }
}

void GameState::OpenCrafting(int32_t crafter_type) const {
  if (on_open_crafting) {
    on_open_crafting(crafter_type);
  }
}

void GameState::OpenContainer(entity::Chest* chest) const {
  if (on_open_container) {
    on_open_container(chest);
  }
}

void GameState::OpenSignEditor(int32_t depth, int32_t x, int32_t y) const {
  if (on_open_sign_editor) {
    on_open_sign_editor(depth, x, y);
  }
}

void GameState::RemoveSign(int32_t depth, int32_t x, int32_t y) const {
  if (on_remove_sign) {
    on_remove_sign(depth, x, y);
  }
}

void GameState::UpdateSignOverlay(int32_t depth, int32_t x, int32_t y) const {
  if (on_update_sign_overlay) {
    on_update_sign_overlay(depth, x, y);
  }
}

void GameState::SaveWorld() const {
  if (on_save_world) {
    on_save_world();
  }
}

void GameState::EndGame() const {
  if (on_end_game) {
    on_end_game();
  }
}

void GameState::TriggerPlayerDeath() const {
  if (on_player_death) {
    on_player_death();
  }
}

bool GameState::ReadAssetText(std::string_view path, std::string* out) const {
  if (read_asset) {
    return read_asset(path, out);
  }
  return false;
}

std::vector<std::string> GameState::GetSignLines(int32_t depth, int32_t x,
                                                 int32_t y) const {
  if (on_get_sign_lines) {
    return on_get_sign_lines(depth, x, y);
  }
  return {};
}

void GameState::SetSignLines(int32_t depth, int32_t x, int32_t y,
                             const std::vector<std::string>& lines) const {
  if (on_set_sign_lines) {
    on_set_sign_lines(depth, x, y, lines);
  }
}

}  // namespace minicraft::game
