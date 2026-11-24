#include "core/game/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/entity/mob.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/game/updater.h"
#include "core/game/world.h"
#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/font_style.h"
#include "core/gfx/screen.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/item/inventory.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/level/level.h"
#include "core/level/tile.h"
#include "core/platform/file_system.h"
#include "core/platform/platform_context.h"
#include "core/platform/render_backend.h"
#include "core/platform/time_clock.h"
#include "core/platform/window_backend.h"
#include "core/saveload/save.h"
#include "core/saveload/version.h"
#include "core/screen/display.h"
#include "core/screen/displays.h"
#include "core/screen/entries.h"
#include "core/screen/menu.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"

namespace minicraft::game {

namespace {

item::PotionType EffectToPotionType(int32_t effect_id) {
  switch (static_cast<entity::Player::PotionEffect>(effect_id)) {
    case entity::Player::PotionEffect::kSpeed:
      return item::PotionType::kSpeed;
    case entity::Player::PotionEffect::kLight:
      return item::PotionType::kLight;
    case entity::Player::PotionEffect::kSwim:
      return item::PotionType::kSwim;
    case entity::Player::PotionEffect::kEnergy:
      return item::PotionType::kEnergy;
    case entity::Player::PotionEffect::kRegen:
      return item::PotionType::kRegen;
    case entity::Player::PotionEffect::kHealth:
      return item::PotionType::kHealth;
    case entity::Player::PotionEffect::kTime:
      return item::PotionType::kTime;
    case entity::Player::PotionEffect::kLava:
      return item::PotionType::kLava;
    case entity::Player::PotionEffect::kShield:
      return item::PotionType::kShield;
    case entity::Player::PotionEffect::kHaste:
      return item::PotionType::kHaste;
  }
  return item::PotionType::kAwkward;
}

const char* PotionName(int32_t effect_id) {
  return item::GetPotionSpec(EffectToPotionType(effect_id)).name;
}

int32_t PotionColor(int32_t effect_id) {
  return item::GetPotionSpec(EffectToPotionType(effect_id)).disp_color;
}

const char* TimeOfDayName(TimeOfDay tod) {
  switch (tod) {
    case TimeOfDay::kMorning:
      return "Morning";
    case TimeOfDay::kDay:
      return "Day";
    case TimeOfDay::kEvening:
      return "Evening";
    case TimeOfDay::kNight:
      return "Night";
  }
  return "Day";
}

double ComputeDarkFactor(int32_t cur_lvl, const GameState& gs) {
  constexpr double kMaxDark = 128.0;
  if (cur_lvl >= 5) return kMaxDark;
  if (cur_lvl < 3) return kMaxDark;

  constexpr int32_t kTransTime = GameState::kDayLength / 4;
  const int32_t gt = ((gs.game_time % GameState::kDayLength) +
                      GameState::kDayLength) %
                     GameState::kDayLength;
  const double rel_time =
      static_cast<double>(gt % kTransTime) / static_cast<double>(kTransTime);
  double dark = 0.0;
  switch (gs.GetTimeOfDay()) {
    case TimeOfDay::kMorning:
      dark = gs.past_day1 ? (1.0 - rel_time) * kMaxDark : 0.0;
      break;
    case TimeOfDay::kDay:
      dark = 0.0;
      break;
    case TimeOfDay::kEvening:
      dark = rel_time * kMaxDark;
      break;
    case TimeOfDay::kNight:
      dark = kMaxDark;
      break;
  }
  if (cur_lvl > 3) {
    dark -= (dark < 10.0 ? dark : 10.0);
  }
  return dark;
}

}  // namespace

void Renderer::Render(const World& world, Updater& updater, gfx::Screen& screen,
                      const platform::PlatformContext& platform,
                      const std::string& game_dir, int32_t fps, int32_t tps) {
  screen.Clear(0);

  if (world.ReadyToRenderGameplay()) {
    RenderLevel(world, screen);
    if (world.GetPlayer() != nullptr && world.GetPlayer()->render_gui) {
      RenderGui(world, updater, screen, fps, tps);
    }
  }

  if (const screen::Display* cur = updater.CurrentDisplay()) {
    cur->Render(&screen);
  }

  if (platform.window != nullptr && !platform.window->IsFocused()) {
    RenderFocusNagger(screen);
  }

  const bool capture = updater.PendingScreenshots() > 0;
  screen.Flush(platform.renderer, capture);

  if (capture) {
    CaptureScreenshotIfRequested(updater, platform, game_dir);
  }
}

void Renderer::RenderLevel(const World& world, gfx::Screen& screen) const {
  const level::Level* level = world.CurrentLevel();
  const entity::Player* player = world.GetPlayer();
  if (!level || !player) return;

  const int32_t cur_lvl = world.CurrentLevelIdx();
  const int32_t x_scroll = player->x - gfx::Screen::w / 2;
  const int32_t y_scroll = player->y - (gfx::Screen::h - 8) / 2;

  if (cur_lvl > 3 && screen.Linker()) {
    const gfx::SheetRect* cloud =
        screen.Linker()->GetSheet(gfx::SpriteType::kTile, "cloud_background");
    if (cloud) {
      for (int32_t y = 0; y < 28; ++y) {
        for (int32_t x = 0; x < 48; ++x) {
          screen.Render(x * 8 - ((x_scroll / 4) & 7),
                        y * 8 - ((y_scroll / 4) & 7), 0, 0, 0, cloud);
        }
      }
    }
  }

  level->RenderBackground(&screen, x_scroll, y_scroll);
  level->RenderSprites(&screen, x_scroll, y_scroll);

  const auto& gs = GameState::Instance();
  const bool need_darkness =
      (cur_lvl != 3 || gs.GetTimeOfDay() != TimeOfDay::kDay) &&
      !gs.IsCreativeMode();
  if (need_darkness) {
    const bool has_light =
        player->potion_effects.count(
            static_cast<int32_t>(entity::Player::PotionEffect::kLight)) > 0;
    level->RenderLight(&screen, x_scroll, y_scroll, has_light ? 12 : 8);
    screen.Overlay(cur_lvl, x_scroll, y_scroll, ComputeDarkFactor(cur_lvl, gs));
  }
}

void Renderer::RenderGui(const World& world, Updater& updater,
                         gfx::Screen& screen, int32_t fps, int32_t tps) const {
  const entity::Player* player = world.GetPlayer();
  const level::Level* level = world.CurrentLevel();
  if (!player) return;

  const auto& gs = GameState::Instance();
  const gfx::SheetRect* hud =
      screen.Linker() ? hud_sheet_.GetSheet(*screen.Linker()) : nullptr;

  // Bottom active item slot background
  if ((!gs.IsCreativeMode() || player->active_item != nullptr) && hud) {
    for (int32_t x = 10; x < 26; ++x) {
      screen.Render(x * 8, gfx::Screen::h - 8, 5, 2, 0, hud);
    }
  }

  if (player->active_item) {
    player->active_item->RenderHud(&screen, 10 * 8, gfx::Screen::h - 8,
                                   gfx::color::kWhite);
  }

  // Bow arrow count
  if (auto* tool = dynamic_cast<const item::ToolItem*>(player->active_item.get())) {
    if (tool->type == item::ToolType::kBow && player->GetInventoryPtr()) {
      auto arrow = item::Items::Get("Arrow");
      const int32_t ac = arrow ? player->GetInventory().Count(*arrow) : 0;
      const std::string txt =
          (gs.IsCreativeMode() || ac >= 10000) ? " x^" : (" x" + std::to_string(ac));
      gfx::Font::DrawBackground(txt, &screen, 84, gfx::Screen::h - 16);
      if (hud) {
        screen.Render(10 * 8 + 4, gfx::Screen::h - 16, 4, 1, 0, hud);
      }
    }
  }

  // Permanent status / notifications
  std::vector<std::string> perm_status;
  if (gs.saving) {
    perm_status.push_back(util::Localization::GetLocalized(
        "minicraft.display.gui.perm_status.saving",
        static_cast<int32_t>(std::round(screen::LoadingDisplay::GetPercentage()))));
  }
  if (gs.bed_sleeping) {
    perm_status.push_back(util::Localization::GetLocalized(
        "minicraft.display.gui.perm_status.sleeping"));
    const std::string exit_key =
        screen::DisplayHost::Instance().input
            ? screen::DisplayHost::Instance().input->GetMapping("exit")
            : "ESC";
    perm_status.push_back(util::Localization::GetLocalized(
        "minicraft.display.gui.perm_status.sleep_cancel", exit_key));
  }

  const auto notifs = perm_status.empty() ? updater.Notifications()
                                          : std::vector<std::string>{};
  if (!perm_status.empty()) {
    gfx::FontStyle style(gfx::color::kWhite);
    style.SetYPos(gfx::Screen::h / 2 - 25)
        .SetRelTextPos(gfx::RelPos::kTop)
        .SetShadowType(gfx::color::kDarkGray, false);
    gfx::Font::DrawParagraph(perm_status, &screen, style, 1);
    updater.AdvanceNoteTickIfNeeded(false);
  } else if (!notifs.empty()) {
    updater.AdvanceNoteTickIfNeeded(true);
    const size_t start = notifs.size() > 3 ? notifs.size() - 3 : 0;
    std::vector<std::string> print;
    for (size_t i = start; i < notifs.size(); ++i) {
      auto wrapped = gfx::Font::GetLines(notifs[i], gfx::Screen::w,
                                         gfx::Screen::h, 0);
      print.insert(print.end(), wrapped.begin(), wrapped.end());
    }
    gfx::FontStyle style(gfx::color::kWhite);
    style.SetShadowType(gfx::color::kDarkGray, false)
        .SetYPos(gfx::Screen::h * 2 / 5)
        .SetRelTextPos(gfx::RelPos::kTop, false);
    gfx::Font::DrawParagraph(print, &screen, style, 0);
  } else {
    updater.AdvanceNoteTickIfNeeded(false);
  }

  // Score mode timer & score
  if (gs.IsScoreMode()) {
    int32_t seconds = static_cast<int32_t>(
        std::ceil(gs.score_time / static_cast<double>(GameState::kNormSpeed)));
    int32_t minutes = seconds / 60;
    int32_t hours = minutes / 60;
    minutes %= 60;
    seconds %= 60;
    int32_t time_col = gfx::color::Get(0, 555);
    if (gs.score_time < 3600) {
      time_col = gfx::color::Get(400, 555);
    } else if (gs.score_time < 18000) {
      time_col = gfx::color::Get(330, 555);
    }
    gfx::Font::Draw(
        util::Localization::GetLocalized(
            "minicraft.display.gui.score.time_left",
            hours > 0 ? (std::to_string(hours) + "h ") : "", minutes, seconds),
        &screen, gfx::Screen::w / 2 - 9 * 8, 2, time_col);

    const std::string score_str = util::Localization::GetLocalized(
        "minicraft.display.gui.score.current_score", player->GetScore());
    gfx::Font::Draw(score_str, &screen,
                    gfx::Screen::w - gfx::Font::TextWidth(score_str) - 2, 11,
                    gfx::color::kWhite);
    const std::string mult_str = "X" + std::to_string(player->GetMultiplier());
    gfx::Font::Draw(mult_str, &screen,
                    gfx::Screen::w - gfx::Font::TextWidth(mult_str) - 2, 20,
                    gfx::color::kWhite);
  }

  // Tool durability %
  if (auto* tool = dynamic_cast<const item::ToolItem*>(player->active_item.get())) {
    const int32_t max_dur =
        std::max(1, item::ToolItem(tool->type, tool->level).dur);
    const int32_t dura = std::clamp(tool->dur * 100 / max_dur, 0, 100);
    const int32_t green = static_cast<int32_t>(dura * 2.55f);
    gfx::Font::DrawBackground(std::to_string(dura) + "%", &screen, 164,
                              gfx::Screen::h - 16,
                              gfx::color::Get(1, 255 - green, green, 0));
  }

  // Watering can %
  if (auto* wc = dynamic_cast<const item::WateringCanItem*>(
          player->active_item.get())) {
    const int32_t dura =
        std::clamp(wc->content * 100 / item::WateringCanItem::kCapacity, 0, 100);
    const int32_t green = static_cast<int32_t>(dura * 2.55f);
    gfx::Font::DrawBackground(std::to_string(dura) + "%", &screen, 164,
                              gfx::Screen::h - 16,
                              gfx::color::Get(1, 255 - green, green, 0));
  }

  // Quests overlay
  RenderQuestsDisplay(world, screen);

  // Potion effects overlay (P to toggle visibility, O to toggle compact mode)
  if (player->show_potion_effects && !player->potion_effects.empty()) {
    const std::string hide_hint = util::Localization::GetLocalized(
        "minicraft.display.gui.potion_effects.hide_hint");
    if (!player->simp_potion_effects) {
      gfx::Font::DrawBackground(hide_hint, &screen, 180,
                                9 + potion_render_offset_, gfx::color::kWhite);
    }
    int32_t idx = 0;
    for (const auto& [eff_id, dur] : player->potion_effects) {
      const int32_t p_time = dur / GameState::kNormSpeed;
      const int32_t mins = p_time / 60;
      const int32_t secs = p_time % 60;
      const std::string pname = util::Localization::GetLocalized(PotionName(eff_id));
      if (player->simp_potion_effects) {
        std::string abbr = pname.empty() ? "?" : pname.substr(0, 1);
        char sbuf[32];
        std::snprintf(sbuf, sizeof(sbuf), "%s %d:%02d", abbr.c_str(), mins, secs);
        const int32_t tx = gfx::Screen::w - gfx::Font::TextWidth(sbuf) - 17;
        gfx::Font::DrawBackground(
            sbuf, &screen, tx,
            9 + idx * gfx::Font::TextHeight() + potion_render_offset_,
            PotionColor(eff_id));
      } else {
        const std::string text = util::Localization::GetLocalized(
            "minicraft.display.gui.potion_effects.potion_dur", pname, mins,
            secs);
        gfx::Font::DrawBackground(
            text, &screen, 180,
            17 + idx * gfx::Font::TextHeight() + potion_render_offset_,
            PotionColor(eff_id));
      }
      ++idx;
    }
  }

  // Status icons: hearts, armor, stamina, hunger
  if (!gs.IsCreativeMode() && hud) {
    for (int32_t i = 1; i <= 30; ++i) {
      if (i < 11) {
        screen.Render((i - 1) * 8, gfx::Screen::h - 16, 0, 1, 0, hud);
      }
      if (i < player->health + 1 && i < 11) {
        screen.Render((i - 1) * 8, gfx::Screen::h - 16, 0, 0, 0, hud);
      } else if (i < player->health + 1 && i < 21 && i >= 11) {
        screen.Render((i - 11) * 8, gfx::Screen::h - 16, 0, 2, 0, hud);
      } else if (i < player->health + 1 && i >= 21) {
        screen.Render((i - 21) * 8, gfx::Screen::h - 16, 0, 3, 0, hud);
      }
    }

    constexpr int32_t kMaxStat = 10;
    for (int32_t i = 0; i < kMaxStat; ++i) {
      const int32_t armor =
          player->armor * kMaxStat / entity::Player::kMaxArmor;
      if (i <= armor && player->cur_armor != nullptr) {
        gfx::LinkedSprite aspr = player->cur_armor->sprite;
        screen.Render(i * 8, gfx::Screen::h - 24, aspr);
      }

      if (player->StaminaRechargeDelay() > 0) {
        if ((player->StaminaRechargeDelay() / 4) % 2 == 0) {
          screen.Render(i * 8, gfx::Screen::h - 8, 1, 2, 0, hud);
        } else {
          screen.Render(i * 8, gfx::Screen::h - 8, 1, 1, 0, hud);
        }
      } else {
        if (i < player->stamina) {
          screen.Render(i * 8, gfx::Screen::h - 8, 1, 0, 0, hud);
        } else {
          screen.Render(i * 8, gfx::Screen::h - 8, 1, 1, 0, hud);
        }
      }

      if (i < player->hunger) {
        screen.Render(i * 8 + (gfx::Screen::w - 80), gfx::Screen::h - 16, 2, 0,
                      0, hud);
      } else {
        screen.Render(i * 8 + (gfx::Screen::w - 80), gfx::Screen::h - 16, 2, 1,
                      0, hud);
      }
    }
  }

  // Boss health bars
  if (level && !player->removed) {
    if (level->depth == 1) {
      for (const auto& e : level->Entities()) {
        if (auto* boss = dynamic_cast<const entity::AirWizard*>(e.get());
            boss != nullptr && !boss->removed) {
          RenderBossbar(screen,
                        boss->health * 100 / std::max(1, boss->max_health),
                        "Air wizard");
          break;
        }
      }
    } else if (level->depth == -4) {
      for (const auto& e : level->Entities()) {
        if (auto* boss = dynamic_cast<const entity::ObsidianKnight*>(e.get());
            boss != nullptr && !boss->removed) {
          RenderBossbar(screen,
                        boss->health * 100 / std::max(1, boss->max_health),
                        "Obsidian Knight");
          break;
        }
      }
    }
  }

  screen::TutorialDisplayHandler::Render(&screen);
  if (updater.SignOverlay()) {
    updater.SignOverlay()->Render(&screen);
  }
  RenderDebugInfo(world, updater, screen, fps, tps);
}

void Renderer::RenderBossbar(gfx::Screen& screen, int32_t bar_length,
                             const std::string& title) const {
  const gfx::SheetRect* hud =
      screen.Linker() ? hud_sheet_.GetSheet(*screen.Linker()) : nullptr;
  if (!hud) return;

  constexpr int32_t x = gfx::Screen::w / 4 - 24;
  constexpr int32_t y = gfx::Screen::h / 8 - 24;
  constexpr int32_t kMaxBarLen = 100;
  constexpr int32_t kInactive = 4;
  constexpr int32_t kActive = 5;

  bar_length = std::clamp(bar_length, 0, kMaxBarLen);

  screen.Render(x + kMaxBarLen * 2, y, 0, kInactive, 1, hud);
  for (int32_t bx = 0; bx < kMaxBarLen; ++bx) {
    screen.Render(x + bx * 2, y, 3, kInactive, 0, hud);
  }
  screen.Render(x - 5, y, 0, kActive, 0, hud);
  for (int32_t bx = 0; bx < bar_length; ++bx) {
    screen.Render(x + bx * 2, y, 3, kActive, 0, hud);
  }
  gfx::Font::DrawCentered(title, &screen, y + 8, gfx::color::kWhite);
}

void Renderer::RenderQuestsDisplay(const World& world,
                                   gfx::Screen& screen) const {
  if (!screen::TutorialDisplayHandler::InQuests() ||
      !io::Settings::GetBool("showquests")) {
    potion_render_offset_ = 0;
    return;
  }

  const bool expanded =
      world.GetPlayer() != nullptr && world.GetPlayer()->quest_expanding > 0;
  std::vector<std::unique_ptr<screen::ListEntry>> entries;
  const auto& series = util::AdvancementSystem::Instance().QuestSeriesList();
  for (const auto& qs : series) {
    if (!qs || !qs->unlocked || qs->completed) continue;
    if (expanded) {
      int32_t done_cnt = 0;
      const int32_t total_cnt = static_cast<int32_t>(qs->quests.size());
      for (const auto& [qk, q] : qs->quests) {
        (void)qk;
        if (q && q->completed) ++done_cnt;
      }
      entries.push_back(std::make_unique<screen::StringEntry>(
          util::Localization::GetLocalized(qs->key) + " (" +
              std::to_string(done_cnt) + "/" + std::to_string(total_cnt) + ")",
          gfx::color::kWhite, false));
      if (entries.size() >= 5) break;
    } else {
      for (const auto& qk : qs->quest_order) {
        auto it = qs->quests.find(qk);
        if (it == qs->quests.end() || !it->second) continue;
        const auto& q = *it->second;
        if (!q.unlocked || q.completed) continue;
        entries.push_back(std::make_unique<screen::StringEntry>(
            util::Localization::GetLocalized(q.key), gfx::color::kWhite, false));
        if (entries.size() >= 2) break;
      }
      if (entries.size() >= 2) break;
    }
  }

  if (!entries.empty()) {
    potion_render_offset_ =
        9 + static_cast<int32_t>(std::min<size_t>(entries.size(), 3)) * 8 + 16;
    screen::Menu::Builder b(true, 0, gfx::RelPos::kRight, std::move(entries));
    b.SetPositioning({gfx::Screen::w - 9, 9}, gfx::RelPos::kBottomLeft)
        .SetTitle("Quests")
        .SetSelectable(false);
    b.CreateMenu()->Render(&screen);
  } else {
    potion_render_offset_ = 0;
  }
}

void Renderer::RenderDebugInfo(const World& world, const Updater& updater,
                               gfx::Screen& screen, int32_t fps,
                               int32_t tps) const {
  if (!updater.ShowDebugInfo()) return;

  const auto& gs = GameState::Instance();
  const entity::Player* player = world.GetPlayer();
  const level::Level* lvl = world.CurrentLevel();

  std::vector<std::string> info;
  info.push_back("VERSION: " + std::string(saveload::Version::kCurrentVersion));
  info.push_back(std::to_string(fps) + " fps");
  info.push_back("Day tiks: " + std::to_string(gs.game_time) + " (" +
                 TimeOfDayName(gs.GetTimeOfDay()) + ")");
  info.push_back(
      std::to_string(static_cast<int32_t>(GameState::kNormSpeed * gs.game_speed)) +
      " tps");

  if (player) {
    info.push_back("walk spd: " + std::to_string(player->Speed()));
    info.push_back("X: " + std::to_string(player->x >> 4) + "-" +
                   std::to_string(player->x & 15));
    info.push_back("Y: " + std::to_string(player->y >> 4) + "-" +
                   std::to_string(player->y & 15));
    if (lvl) {
      if (const level::Tile* t = lvl->GetTile(player->x >> 4, player->y >> 4)) {
        info.push_back("Tile: " + t->name);
      }
    }
    if (gs.IsScoreMode()) {
      info.push_back("Score: " + std::to_string(player->GetScore()));
    }
  }

  if (lvl) {
    info.push_back("Mob Cnt: " + std::to_string(lvl->mob_count) + "/" +
                   std::to_string(lvl->max_mob_count));
    if (world.CurrentLevelIdx() == 5) {
      info.push_back(lvl->chest_count > 0
                         ? ("Chests: " + std::to_string(lvl->chest_count))
                         : "Chests: Complete!");
    }
    info.push_back("Seed: " + std::to_string(lvl->seed));
  }

  if (player) {
    info.push_back("Hunger: " + std::to_string(player->hunger) + " Stam: " +
                   std::to_string(player->stamina));
    if (player->armor > 0) {
      info.push_back("Armor: " + std::to_string(player->armor));
      info.push_back("Dam buffer: " + std::to_string(player->armor_damage_buffer));
    }
  }

  gfx::FontStyle style(gfx::color::kWhite);
  style.SetShadowType(gfx::color::kBlack, true).SetXPos(1).SetYPos(2);
  gfx::Font::DrawParagraph(info, &screen, style, 2);
}

void Renderer::RenderFocusNagger(gfx::Screen& screen) const {
  const gfx::SheetRect* hud =
      screen.Linker() ? hud_sheet_.GetSheet(*screen.Linker()) : nullptr;
  const std::string msg = "Click to focus!";
  const int32_t xx = (gfx::Screen::w - gfx::Font::TextWidth(msg)) / 2;
  const int32_t yy = (gfx::Screen::h - 8) / 2;
  const int32_t w = static_cast<int32_t>(msg.size());

  if (hud) {
    screen.Render(xx - 8, yy - 8, 0, 6, 0, hud);
    screen.Render(xx + w * 8, yy - 8, 0, 6, 1, hud);
    screen.Render(xx - 8, yy + 8, 0, 6, 2, hud);
    screen.Render(xx + w * 8, yy + 8, 0, 6, 3, hud);
    for (int32_t x = 0; x < w; ++x) {
      screen.Render(xx + x * 8, yy - 8, 1, 6, 0, hud);
      screen.Render(xx + x * 8, yy + 8, 1, 6, 2, hud);
      screen.Render(xx + x * 8, yy, 3, 6, 0, hud);
    }
    screen.Render(xx - 8, yy, 2, 6, 0, hud);
    screen.Render(xx + w * 8, yy, 2, 6, 1, hud);
  }

  const int32_t ticks = GameState::Instance().tick_count;
  const int32_t col = ((ticks / 20) % 2 == 0) ? gfx::color::Get(1, 153)
                                               : gfx::color::Get(5, 255);
  gfx::Font::Draw(msg, &screen, xx, yy, col);
}

void Renderer::CaptureScreenshotIfRequested(
    Updater& updater, const platform::PlatformContext& platform,
    const std::string& game_dir) const {
  updater.ConsumeScreenshotRequest();
  if (!platform.renderer || !platform.file_system || game_dir.empty()) return;

  int32_t w = 0;
  int32_t h = 0;
  // Query dimensions & read back RGBA8 framebuffer.
  if (platform.window) {
    platform.window->GetFramebufferSize(&w, &h);
  }
  if (w <= 0 || h <= 0) {
    w = gfx::Screen::w;
    h = gfx::Screen::h;
  }
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);
  if (!platform.renderer->ReadBackScreenshot(&w, &h, rgba.data(), rgba.size())) {
    return;
  }
  rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);

  const std::string dir = saveload::Save::JoinWorldPath(game_dir, "screenshots");
  platform.file_system->CreateDirectories(dir);
  const std::string stamp =
      platform.clock ? platform.clock->FormatLocalTime("%Y-%m-%d_%H.%M.%S")
                     : "screenshot";
  std::string path = saveload::Save::JoinWorldPath(dir, stamp + ".png");
  int32_t count = 1;
  while (platform.file_system->Exists(path)) {
    path = saveload::Save::JoinWorldPath(
        dir, stamp + "_" + std::to_string(count++) + ".png");
  }
  platform.file_system->SaveScreenshotPng(path, w, h, rgba.data());
}

}  // namespace minicraft::game
