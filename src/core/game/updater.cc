#include "core/game/updater.h"

#include <algorithm>
#include <utility>

#include "core/entity/furniture.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/game/initializer.h"
#include "core/game/world.h"
#include "core/gfx/sprite_atlas.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/io/sound.h"
#include "core/item/inventory.h"
#include "core/item/items.h"
#include "core/item/recipes.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/pak/pak_reader.h"
#include "core/platform/file_system.h"
#include "core/platform/platform_context.h"
#include "core/platform/time_clock.h"
#include "core/platform/window_backend.h"
#include "core/saveload/save.h"
#include "core/screen/displays.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/logging.h"

namespace minicraft::game {

namespace {

const std::vector<item::Recipe>& RecipesForCrafter(int32_t crafter_type) {
  switch (static_cast<entity::Crafter::Type>(crafter_type)) {
    case entity::Crafter::Type::kWorkbench:
      return item::Recipes::workbench_recipes;
    case entity::Crafter::Type::kFurnace:
      return item::Recipes::furnace_recipes;
    case entity::Crafter::Type::kOven:
      return item::Recipes::oven_recipes;
    case entity::Crafter::Type::kAnvil:
      return item::Recipes::anvil_recipes;
    case entity::Crafter::Type::kEnchanter:
      return item::Recipes::enchant_recipes;
    case entity::Crafter::Type::kLoom:
      return item::Recipes::loom_recipes;
    case entity::Crafter::Type::kDyeVat:
      return item::Recipes::dye_vat_recipes;
  }
  return item::Recipes::workbench_recipes;
}

std::string TitleForCrafter(int32_t crafter_type) {
  switch (static_cast<entity::Crafter::Type>(crafter_type)) {
    case entity::Crafter::Type::kWorkbench:
      return "Workbench";
    case entity::Crafter::Type::kFurnace:
      return "Furnace";
    case entity::Crafter::Type::kOven:
      return "Oven";
    case entity::Crafter::Type::kAnvil:
      return "Anvil";
    case entity::Crafter::Type::kEnchanter:
      return "Enchanter";
    case entity::Crafter::Type::kLoom:
      return "Loom";
    case entity::Crafter::Type::kDyeVat:
      return "Dye Vat";
  }
  return "Crafting";
}

}  // namespace

Updater::~Updater() {
  if (!bridges_wired_) return;
  auto& host = screen::DisplayHost::Instance();
  host.respawn_player = {};
  host.change_level = {};
  host.reload_skins = {};
  host.reload_resource_packs = {};
  host.get_player = {};
  host.get_current_level = {};
  host.get_current_level_idx = {};
  host.get_signs = {};
  host.get_clipboard = {};
  host.set_clipboard = {};
  host.create_texture = {};
  host.destroy_texture = {};
  host.add_notification = {};
  host.clear_notifications = {};

  auto& gs = GameState::Instance();
  gs.sprite_linker = nullptr;
  gs.read_asset = {};
  gs.on_add_notification = {};
  gs.on_notify_all = {};
  gs.on_schedule_level_change = {};
  gs.on_open_book = {};
  gs.on_open_crafting = {};
  gs.on_open_container = {};
  gs.on_open_sign_editor = {};
  gs.on_remove_sign = {};
  gs.on_update_sign_overlay = {};
  gs.on_save_world = {};
  gs.on_end_game = {};
  gs.on_player_death = {};
  gs.on_get_sign_lines = {};
  gs.on_set_sign_lines = {};

  util::AchievementManager::Instance().SetOnUnlocked({});
  util::AdvancementSystem::Instance().SetCallbacks({}, {}, {});
}

void Updater::WireHostBridges(World& world, io::InputHandler& input,
                              const platform::PlatformContext& platform,
                              const pak::PakReader& pak,
                              gfx::SpriteLinker& linker,
                              const std::string& game_dir,
                              std::function<void()> on_quit,
                              std::function<void()> on_reload_packs) {
  bridges_wired_ = true;
  auto& host = screen::DisplayHost::Instance();
  host.input = &input;
  host.fs = platform.file_system;
  host.pak = &pak;
  host.game_dir = game_dir;

  host.set_display = [this](std::unique_ptr<screen::Display> d) {
    SetDisplay(std::move(d));
  };
  host.exit_display = [this](int32_t depth) { ExitDisplay(depth); };
  host.get_display = [this]() -> screen::Display* { return CurrentDisplay(); };
  host.quit_game = std::move(on_quit);

  host.start_world = [this, &world, &input, &platform, game_dir](
                         const std::string& world_name, bool is_new_world,
                         std::optional<int64_t> seed) -> bool {
    if (world.InitWorld(platform.file_system, game_dir, world_name,
                        is_new_world, seed, &input, this)) {
      world.MarkWorldEntered(platform.clock);
      return true;
    }
    return false;
  };

  host.exit_world = [this, &world, &platform]() {
    world.UnloadWorld(platform.clock);
    sign_overlay_.reset();
    std::lock_guard<std::mutex> lock(notifications_mutex_);
    notifications_.clear();
  };

  host.save_world = [this, &world, &platform, game_dir]() {
    auto& host_inst = screen::DisplayHost::Instance();
    const bool silent = std::exchange(host_inst.save_world_silent_once, false);
    if (platform.file_system && !GameState::Instance().current_world_name.empty()) {
      world.SaveCurrentWorldAsync(*platform.file_system, game_dir,
                                  GameState::Instance().current_world_name,
                                  !silent);
      autosave_tick_ = 0;
    }
  };

  host.save_prefs = [&input, &platform, game_dir]() {
    if (platform.file_system) {
      saveload::Save::SavePrefs(*platform.file_system, game_dir, &input,
                                &screen::DisplayHost::Instance()
                                     .active_resource_packs);
    }
  };

  host.respawn_player = [&world, &input]() { world.RespawnPlayer(&input); };
  host.change_level = [&world](int32_t dir) { world.ChangeLevel(dir); };
  auto skin_tex_ids = std::make_shared<std::vector<uint16_t>>();
  host.reload_skins = [&platform, &linker, game_dir, skin_tex_ids](
                          const std::string& /*skin_name*/) {
    Initializer::ReloadCustomSkins(platform, linker, game_dir,
                                   skin_tex_ids.get());
  };
  Initializer::ReloadCustomSkins(platform, linker, game_dir,
                                 skin_tex_ids.get());
  host.reload_resource_packs = std::move(on_reload_packs);

  host.add_notification = [this](const std::string& msg) {
    AddNotification(msg);
  };
  host.clear_notifications = [this]() {
    std::lock_guard<std::mutex> lock(notifications_mutex_);
    notifications_.clear();
    notetick_ = 0;
  };
  host.get_player = [&world]() -> entity::Player* {
    return world.GetPlayer();
  };
  host.get_current_level = [&world]() -> level::Level* {
    return world.CurrentLevel();
  };
  host.get_current_level_idx = [&world]() -> int32_t {
    return world.CurrentLevelIdx();
  };
  host.get_signs = [&world]() -> saveload::SignMap* { return &world.Signs(); };
  host.get_clipboard = [&platform]() -> std::string {
    return platform.window ? platform.window->GetClipboardText() : std::string{};
  };
  host.set_clipboard = [&platform](std::string_view text) {
    if (platform.window) platform.window->SetClipboardText(text);
  };
  host.create_texture = [&platform](int32_t w, int32_t h,
                                    const uint8_t* rgba) -> uint16_t {
    return platform.renderer ? platform.renderer->CreateTexture(w, h, rgba)
                             : 0;
  };
  host.destroy_texture = [&platform](uint16_t tid) {
    if (platform.renderer && tid > 0) platform.renderer->DestroyTexture(tid);
  };

  // Bind GameState runtime hooks so entities/tiles/advancements stay decoupled.
  auto& gs = GameState::Instance();
  gs.sprite_linker = &linker;
  gs.read_asset = [&pak](std::string_view path, std::string* out) -> bool {
    return pak.ReadTextEntry(path, out);
  };
  gs.on_add_notification = [this](const std::string& msg, int32_t /*time*/) {
    AddNotification(msg, 0);
  };
  gs.on_notify_all = [this](const std::string& msg, int32_t delay) {
    AddNotification(util::Localization::GetLocalized(msg), delay);
  };
  gs.on_schedule_level_change = [&world](int32_t dir,
                                          std::function<void()> on_change) {
    world.ScheduleLevelChange(dir, std::move(on_change));
  };
  gs.on_open_book = [this](const std::string& /*title*/,
                           const std::string& text, bool has_title) {
    SetDisplay(std::make_unique<screen::BookDisplay>(text, has_title));
  };
  gs.on_open_crafting = [this, &world](int32_t crafter_type) {
    if (entity::Player* p = world.GetPlayer()) {
      SetDisplay(std::make_unique<screen::CraftingDisplay>(
          RecipesForCrafter(crafter_type), TitleForCrafter(crafter_type), p,
          false));
    }
  };
  gs.on_open_container = [this, &world](entity::Chest* chest) {
    if (entity::Player* p = world.GetPlayer(); p != nullptr && chest != nullptr) {
      SetDisplay(std::make_unique<screen::ContainerDisplay>(p, chest));
    }
  };
  gs.on_open_sign_editor = [this](int32_t depth, int32_t x, int32_t y) {
    SetDisplay(std::make_unique<screen::SignDisplay>(depth, x, y));
  };
  gs.on_remove_sign = [this, &world](int32_t depth, int32_t x, int32_t y) {
    screen::SignDisplay::RemoveSign(depth, x, y);
    screen::SignDisplay::ExportToMap(&world.Signs());
    if (sign_overlay_ && sign_overlay_->Matches(depth, x, y)) {
      sign_overlay_.reset();
    }
  };
  gs.on_update_sign_overlay = [this](int32_t depth, int32_t x, int32_t y) {
    if (screen::SignDisplay::GetSign(depth, x, y)) {
      sign_overlay_ =
          std::make_unique<screen::SignDisplayMenu>(depth, x, y);
    } else {
      sign_overlay_.reset();
    }
  };
  gs.on_save_world = host.save_world;
  gs.on_end_game = [this]() {
    GameState::Instance().won_game = true;
    SetDisplay(std::make_unique<screen::EndGameDisplay>());
  };
  gs.on_player_death = [this, &world]() { world.ResetPlayerDeadTime(); };
  gs.on_get_sign_lines = [](int32_t depth, int32_t x,
                            int32_t y) -> std::vector<std::string> {
    if (const auto* lines = screen::SignDisplay::GetSign(depth, x, y)) {
      return *lines;
    }
    return {};
  };
  gs.on_set_sign_lines = [&world](int32_t depth, int32_t x, int32_t y,
                                  const std::vector<std::string>& lines) {
    screen::SignDisplay::UpdateSign(depth, x, y, lines);
    screen::SignDisplay::ExportToMap(&world.Signs());
  };

  // Wire achievement & advancement rewards.
  util::AchievementManager::Instance().SetOnUnlocked(
      [this](const std::string& id) {
        const auto* ach = util::AchievementManager::Instance().Find(id);
        std::string title = ach ? util::Localization::GetLocalized(ach->name) : id;
        AddNotification(util::Localization::GetLocalized(
            "minicraft.notification.achievement_unlocked", title));
        if (screen::DisplayHost::Instance().save_prefs) {
          screen::DisplayHost::Instance().save_prefs();
        }
      });

  util::AdvancementSystem::Instance().SetCallbacks(
      [&world](const util::ElementRewards& rewards) {
        for (const auto& rr : rewards.recipes) {
          item::Recipe r(rr.product, rr.costs);
          screen::CraftingDisplay::UnlockRecipe(r);
        }
        if (entity::Player* p = world.GetPlayer()) {
          for (const auto& item_spec : rewards.items) {
            if (auto it = item::Items::Get(item_spec, true)) {
              p->AddItemToInventory(std::move(it));
            }
          }
        }
      },
      []() { screen::QuestsDisplay::RefreshIfOpen(); },
      [](const std::string& key) {
        screen::TutorialDisplayHandler::UpdateCompletedElement(key);
      });

  // Sync any recipe unlocks restored from Unlocks.json before callbacks bound.
  for (const auto& el : util::AdvancementSystem::Instance().RecipeUnlocks()) {
    if (el && el->completed) {
      for (const auto& rr : el->rewards.recipes) {
        screen::CraftingDisplay::UnlockRecipe(
            item::Recipe(rr.product, rr.costs));
      }
    }
  }
}

void Updater::SetDisplay(std::unique_ptr<screen::Display> display) {
  if (!display) {
    pending_ops_.push_back({DisplayOp::Kind::kClear, nullptr, 0});
  } else {
    pending_ops_.push_back({DisplayOp::Kind::kPush, std::move(display), 0});
  }
}

void Updater::ExitDisplay(int32_t depth) {
  if (depth < 1) return;
  if (display_stack_.empty() && pending_ops_.empty()) {
    util::log::kGameHandler.Warn("Tried to exit display with none open.");
    return;
  }
  io::Sound::Play("craft");
  pending_ops_.push_back({DisplayOp::Kind::kPop, nullptr, depth});
}

void Updater::ClearDisplays() {
  pending_ops_.clear();
  display_stack_.clear();
}

screen::Display* Updater::CurrentDisplay() const {
  return display_stack_.empty() ? nullptr : display_stack_.back().get();
}

bool Updater::HasActiveDisplay() const { return !display_stack_.empty(); }

void Updater::ProcessPendingDisplayOps() {
  while (!pending_ops_.empty()) {
    DisplayOp op = std::move(pending_ops_.front());
    pending_ops_.pop_front();

    if (op.kind == DisplayOp::Kind::kClear) {
      while (!display_stack_.empty()) {
        auto top = std::move(display_stack_.back());
        display_stack_.pop_back();
        if (top) top->OnExit();
      }
    } else if (op.kind == DisplayOp::Kind::kPop) {
      for (int32_t i = 0; i < op.pop_depth && !display_stack_.empty(); ++i) {
        auto top = std::move(display_stack_.back());
        display_stack_.pop_back();
        if (top) top->OnExit();
      }
    } else if (op.kind == DisplayOp::Kind::kPush && op.display) {
      screen::Display* parent =
          display_stack_.empty() ? nullptr : display_stack_.back().get();
      op.display->Init(parent);
      display_stack_.push_back(std::move(op.display));
    }
  }
}

void Updater::AddNotification(std::string msg, int32_t initial_notetick) {
  if (msg.empty()) return;
  std::lock_guard<std::mutex> lock(notifications_mutex_);
  notifications_.push_back(std::move(msg));
  while (notifications_.size() > 3) {
    notifications_.erase(notifications_.begin());
  }
  notetick_ = initial_notetick;
}

void Updater::ResetAutosaveAndNotifications() {
  autosave_tick_ = 0;
  std::lock_guard<std::mutex> lock(notifications_mutex_);
  notifications_.clear();
  notetick_ = 0;
  update_notetick_ = false;
}

void Updater::AdvanceNoteTickIfNeeded(bool update) {
  std::lock_guard<std::mutex> lock(notifications_mutex_);
  update_notetick_ = update;
  if (update) {
    ++notetick_;
    if (notetick_ > 180 && !notifications_.empty()) {
      notifications_.erase(notifications_.begin());
      notetick_ = 0;
    }
  }
}

void Updater::ConsumeScreenshotRequest() {
  if (screenshot_requests_ > 0) --screenshot_requests_;
}

void Updater::SetSignDisplayMenu(
    std::unique_ptr<screen::SignDisplayMenu> menu) {
  sign_overlay_ = std::move(menu);
}

void Updater::RefreshSignOverlay(World& world) {
  entity::Player* player = world.GetPlayer();
  level::Level* lvl = world.CurrentLevel();
  if (!player || !lvl) {
    sign_overlay_.reset();
    return;
  }

  // Show sign text if standing near a sign tile facing or standing on it.
  const int32_t px = player->x >> 4;
  const int32_t py = player->y >> 4;
  const int32_t fx = px + entity::DirX(player->dir);
  const int32_t fy = py + entity::DirY(player->dir);

  int32_t sx = px;
  int32_t sy = py;
  bool found = false;
  if (lvl->GetTile(px, py)->id == level::Tiles::kSign &&
      screen::SignDisplay::GetSign(lvl->depth, px, py)) {
    found = true;
  } else if (lvl->GetTile(fx, fy)->id == level::Tiles::kSign &&
             screen::SignDisplay::GetSign(lvl->depth, fx, fy)) {
    sx = fx;
    sy = fy;
    found = true;
  }

  if (found) {
    if (!sign_overlay_ || !sign_overlay_->Matches(lvl->depth, sx, sy)) {
      sign_overlay_ =
          std::make_unique<screen::SignDisplayMenu>(lvl->depth, sx, sy);
    }
  } else {
    sign_overlay_.reset();
  }
}

void Updater::TickGameplayInput(World& world, io::InputHandler& input) {
  entity::Player* player = world.GetPlayer();
  auto& gs = GameState::Instance();
  level::Level* level = world.CurrentLevel();

  if (gs.bed_sleeping) {
    if (input.InputPressed("exit")) {
      gs.bed_sleeping = false;
      gs.bed_sleep_players = 0;
      gs.game_speed = 1.0f;
    }
    return;
  }

  if (input.InputPressed("F3")) {
    show_debug_info_ = !show_debug_info_;
  }
  if (input.InputPressed("pause")) {
    SetDisplay(std::make_unique<screen::PauseDisplay>());
    return;
  }
  if (player) {
    const bool menu_pressed = input.InputPressed("menu");
    const bool craft_pressed = input.InputPressed("craft");
    if ((menu_pressed || craft_pressed) && player->active_item != nullptr) {
      player->TryAddToInvOrDrop(player->active_item->Clone());
      player->active_item.reset();
      player->ResetFishing();
    }
    if (craft_pressed) {
      if (!player->UseNearby()) {
        SetDisplay(std::make_unique<screen::CraftingDisplay>(
            item::Recipes::craft_recipes, "minicraft.displays.crafting", player,
            true));
      }
      return;
    }
    if (menu_pressed) {
      if (!player->UseNearby()) {
        SetDisplay(std::make_unique<screen::PlayerInvDisplay>(player));
      }
      return;
    }
    if (input.InputPressed("info")) {
      SetDisplay(std::make_unique<screen::InfoDisplay>());
      return;
    }
    if (input.InputPressed("quests")) {
      SetDisplay(std::make_unique<screen::QuestsDisplay>());
      return;
    }
    if (input.InputPressed("quicksave") && !gs.saving) {
      gs.SaveWorld();
      return;
    }

    if (player->active_item && level &&
        (input.InputPressed("drop-one") || input.InputPressed("drop-stack"))) {
      if (player->active_item->Name() != "Power Glove") {
        if (input.InputPressed("drop-stack")) {
          level->DropItem(player->x, player->y, player->active_item->Clone());
          player->active_item.reset();
        } else {
          if (auto* st =
                  dynamic_cast<item::StackableItem*>(player->active_item.get());
              st != nullptr && st->count > 1) {
            auto one = st->Clone();
            static_cast<item::StackableItem*>(one.get())->count = 1;
            --st->count;
            level->DropItem(player->x, player->y, std::move(one));
          } else {
            level->DropItem(player->x, player->y,
                            player->active_item->Clone());
            player->active_item.reset();
          }
        }
      }
    }
  }

  // Creative level switch shortcuts
  if (gs.IsCreativeMode()) {
    if (input.InputPressed("SHIFT-S")) {
      SetDisplay(std::make_unique<screen::LevelTransitionDisplay>(-1));
    } else if (input.InputPressed("SHIFT-W")) {
      SetDisplay(std::make_unique<screen::LevelTransitionDisplay>(1));
    }
  }

  // Debug / cheat combos... 
  // I could prevent this from being used in the final version... but eh, what game doesn't have cheats?
  if (input.InputPressed("F3-T-1")) gs.SetTimeOfDay(TimeOfDay::kMorning);
  if (input.InputPressed("F3-T-2")) gs.SetTimeOfDay(TimeOfDay::kDay);
  if (input.InputPressed("F3-T-3")) gs.SetTimeOfDay(TimeOfDay::kEvening);
  if (input.InputPressed("F3-T-4")) gs.SetTimeOfDay(TimeOfDay::kNight);

  if (input.InputPressed("F3-F4-1")) {
    io::Settings::Set("mode", std::string("minicraft.settings.mode.survival"));
  } else if (input.InputPressed("F3-F4-2")) {
    io::Settings::Set("mode", std::string("minicraft.settings.mode.creative"));
  } else if (input.InputPressed("F3-F4-3")) {
    io::Settings::Set("mode", std::string("minicraft.settings.mode.score"));
  }

  if (gs.IsScoreMode() && input.InputPressed("F3-SHIFT-T")) {
    gs.score_time = GameState::kNormSpeed * 5;
  }

  if (input.InputPressed("F3-S-0")) {
    gs.game_speed = 1.0f;
  } else if (input.InputPressed("F3-S-equals")) {
    if (gs.game_speed < 1.0f) gs.game_speed *= 2.0f;
    else if (GameState::kNormSpeed * gs.game_speed < 2000.0f) gs.game_speed += 1.0f;
  } else if (input.InputPressed("F3-S-minus")) {
    if (gs.game_speed > 1.0f) gs.game_speed -= 1.0f;
    else if (GameState::kNormSpeed * gs.game_speed > 5.0f) gs.game_speed /= 2.0f;
  }

  if (player) {
    if (input.InputPressed("F3-h")) --player->health;
    if (input.InputPressed("F3-b")) --player->hunger;
    if (input.InputPressed("F3-M-0")) player->SetSpeed(1);
    if (input.InputPressed("F3-M-equals")) player->SetSpeed(player->Speed() + 1);
    if (input.InputPressed("F3-M-minus") && player->Speed() > 1) {
      player->SetSpeed(player->Speed() - 1);
    }
    if (level) {
      if (input.InputPressed("F3-Y")) {
        world.ChangeLevel(+1);
      }
      if (input.InputPressed("F3-H")) {
        world.ChangeLevel(-1);
      }
      if (input.InputPressed("F3-L")) {
        for (int32_t i = 0; i < 6; ++i) {
          if (const auto& lvl = world.Levels()[i]) {
            for (const auto& lp : lvl->Players()) {
              if (lp) {
                util::log::kGameHandler.Info(
                    "Player at lvl=" + std::to_string(lvl->depth) + " x=" +
                    std::to_string(lp->x >> 4) + " y=" +
                    std::to_string(lp->y >> 4));
              }
            }
          }
        }
      }
      if (input.InputPressed("F3-u")) {
        level->SetTile(player->x >> 4, player->y >> 4,
                       level::Tiles::Get(level::Tiles::kStairsUp), 0);
      }
      if (input.InputPressed("F3-d")) {
        level->SetTile(player->x >> 4, player->y >> 4,
                       level::Tiles::Get(level::Tiles::kStairsDown), 0);
      }
    }
  }
}

void Updater::Tick(World& world, io::InputHandler& input,
                   const platform::PlatformContext& platform,
                   const std::string& game_dir) {
  if (platform.window != nullptr) {
    if (input.InputPressed("FULLSCREEN")) {
      bool next = !platform.window->IsFullscreen();
      platform.window->SetFullscreen(next);
      io::Settings::Set("fullscreen", next);
    } else if (io::Settings::GetBool("fullscreen") !=
               platform.window->IsFullscreen()) {
      platform.window->SetFullscreen(io::Settings::GetBool("fullscreen"));
    }
  }
  if (input.InputPressed("screenshot")) {
    ++screenshot_requests_;
  }

  ProcessPendingDisplayOps();

  auto& gs = GameState::Instance();
  const bool has_focus =
      platform.window == nullptr || platform.window->IsFocused();
  if (!has_focus) {
    input.ReleaseAll();
    gs.paused = true;
    return;
  }

  const uint64_t now_ms =
      platform.clock ? static_cast<uint64_t>(platform.clock->NowMilliseconds())
                     : 0;
  input.Tick(now_ms);
  screen::TutorialDisplayHandler::Tick(input);

  if (screen::Display* cur = CurrentDisplay()) {
    cur->Tick(input);
    gs.paused = true;
    ProcessPendingDisplayOps();
    return;
  }

  gs.paused = false;

  // Autosave
  if (!gs.paused && world.ReadyToRenderGameplay()) {
    ++autosave_tick_;
    if (autosave_tick_ > kAutosaveIntervalTicks) {
      autosave_tick_ = 0;
      if (io::Settings::GetBool("autosave") && !gs.won_game &&
          world.GetPlayer() != nullptr && world.GetPlayer()->health > 0 &&
          platform.file_system != nullptr &&
          !gs.current_world_name.empty()) {
        world.SaveCurrentWorldAsync(*platform.file_system, game_dir,
                                    gs.current_world_name, true);
      }
    }
  }

  if (!gs.paused && world.ReadyToRenderGameplay()) {
    if (gs.bed_sleeping) {
      if (gs.game_speed != 20.0f) {
        gs.game_speed = 20.0f;
      }
      if (gs.game_time > GameState::kSleepEndTime) {
        gs.past_day1 = true;
        gs.game_time = 0;
      }
      if (gs.game_time <= GameState::kSleepStartTime &&
          gs.game_time >= GameState::kSleepEndTime) {
        gs.game_speed = 1.0f;
        gs.bed_sleeping = false;
        gs.bed_sleep_players = 0;
      }
    }

    gs.AdvanceGameTime(1);
    ++gs.tick_count;
    ++gs.tile_tick_count;
  }

  // Score mode timer
  if (gs.IsScoreMode() && !gs.paused && !gs.won_game &&
      world.ReadyToRenderGameplay()) {
    if (gs.score_time <= 0) {
      gs.won_game = true;
      SetDisplay(std::make_unique<screen::EndGameDisplay>());
    } else {
      --gs.score_time;
    }
  }

  if (!world.ReadyToRenderGameplay()) {
    ProcessPendingDisplayOps();
    return;
  }

  entity::Player* player = world.GetPlayer();
  if (player && player->removed) {
    world.IncrementPlayerDeadTime();
    if (world.PlayerDeadTime() > 60) {
      SetDisplay(std::make_unique<screen::PlayerDeathDisplay>());
      ProcessPendingDisplayOps();
      return;
    }
  } else if (int32_t dir = world.ConsumePendingLevelChange(); dir != 0) {
    SetDisplay(std::make_unique<screen::LevelTransitionDisplay>(dir));
    ProcessPendingDisplayOps();
    return;
  }

  TickGameplayInput(world, input);
  ProcessPendingDisplayOps();

  if (!HasActiveDisplay()) {
    if (level::Level* lvl = world.CurrentLevel()) {
      lvl->Tick(true);
    }
    RefreshSignOverlay(world);
  }

  ProcessPendingDisplayOps();
}

}  // namespace minicraft::game
