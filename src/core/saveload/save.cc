#include "core/saveload/save.h"

#include <charconv>
#include <json.hpp>
#include <string>
#include <vector>

#include "core/entity/boat.h"
#include "core/entity/direction.h"
#include "core/entity/entity.h"
#include "core/entity/furniture.h"
#include "core/entity/mob.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/item/inventory.h"
#include "core/item/item.h"
#include "core/item/recipes.h"
#include "core/level/chunk_manager.h"
#include "core/level/level.h"
#include "core/platform/file_system.h"
#include "core/saveload/version.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/logging.h"

namespace minicraft::saveload {

namespace {

std::string JoinLines(const std::vector<std::string> &lines) {
  std::string out;
  size_t total = 0;
  for (const auto &l : lines)
    total += l.size() + 1;
  out.reserve(total);
  for (size_t i = 0; i < lines.size(); ++i) {
    out += lines[i];
    out.push_back('\n');
  }
  return out;
}

template <typename T, size_t N>
std::string SerializeChunkArrayCsv(const std::array<T, N> &arr) {
  std::string out;
  out.reserve(N * 4);
  char buf[32];
  for (size_t i = 0; i < N; ++i) {
    if (i > 0)
      out.push_back(',');
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), arr[i]);
    out.append(buf, ptr);
  }
  return out;
}

std::string SerializePotionEffects(const entity::Player &player) {
  std::string out;
  bool first = true;
  for (const auto &[effect_id, duration] : player.potion_effects) {
    if (duration <= 0)
      continue;
    if (!first)
      out.push_back(':');
    first = false;
    out += std::to_string(effect_id) + ";" + std::to_string(duration);
  }
  return out.empty() ? "None" : out;
}

} // namespace

std::string Save::JoinWorldPath(std::string_view base, std::string_view sub) {
  std::string out(base);
  if (!out.empty() && out.back() != '/' && out.back() != '\\') {
    out.push_back('/');
  }
  out.append(sub);
  return out;
}

std::vector<std::string> Save::WriteInventory(const item::Inventory &inv,
                                              const entity::Player *player) {
  std::vector<std::string> lines;
  lines.reserve(inv.InvSize() + 1);
  if (player && player->active_item) {
    lines.push_back("ACTIVE:" + player->active_item->GetData());
  }
  for (size_t i = 0; i < inv.InvSize(); ++i) {
    const item::Item *it = inv.Get(i);
    if (it) {
      lines.push_back(it->GetData());
    }
  }
  return lines;
}

std::string Save::WriteEntity(const entity::Entity &e, int32_t level_idx,
                              bool is_local_save) {
  (void)is_local_save;
  std::string name = e.GetEntityTypeName();
  if (name == "Player" || name == "Particle" || name == "FireParticle" ||
      name == "BurnParticle" || name == "SmashParticle" ||
      name == "SandParticle" || name == "WaterParticle" ||
      name == "TextParticle" || name == "Arrow" || name == "Spark" ||
      name == "FireSpark" || name == "ItemEntity" ||
      name == "ExplosionTileTicker") {
    return {};
  }

  std::vector<std::string> parts;
  parts.push_back(std::to_string(e.x));
  parts.push_back(std::to_string(e.y));

  if (const auto *mob = dynamic_cast<const entity::Mob *>(&e)) {
    parts.push_back(std::to_string(mob->health));
    if (const auto *enemy = dynamic_cast<const entity::EnemyMob *>(&e)) {
      parts.push_back(std::to_string(enemy->lvl));
    } else if (name == "AirWizard") {
      parts.push_back("1");
    } else if (const auto *knight =
                   dynamic_cast<const entity::ObsidianKnight *>(&e)) {
      parts.push_back(std::to_string(knight->Phase()));
    } else if (const auto *sheep = dynamic_cast<const entity::Sheep *>(&e)) {
      parts.push_back(sheep->cut ? "true" : "false");
    }
  } else if (const auto *bed = dynamic_cast<const entity::Bed *>(&e)) {
    name = bed->ColorName() + " Bed";
  } else if (const auto *chest = dynamic_cast<const entity::Chest *>(&e)) {
    if (const auto inv = chest->GetInventory()) {
      for (size_t i = 0; i < inv->InvSize(); ++i) {
        const item::Item *it = inv->Get(i);
        if (it)
          parts.push_back(it->GetData());
      }
    }
    if (const auto *dc = dynamic_cast<const entity::DeathChest *>(&e)) {
      parts.push_back(std::to_string(dc->Time()));
    } else if (const auto *dnc =
                   dynamic_cast<const entity::DungeonChest *>(&e)) {
      parts.push_back(dnc->is_locked ? "true" : "false");
    }
  } else if (const auto *spawner = dynamic_cast<const entity::Spawner *>(&e)) {
    parts.push_back(spawner->MobType());
    parts.push_back(std::to_string(spawner->MobLvl()));
  } else if (const auto *lantern = dynamic_cast<const entity::Lantern *>(&e)) {
    parts.push_back(std::to_string(static_cast<int32_t>(lantern->GetType())));
  } else if (const auto *statue =
                 dynamic_cast<const entity::KnightStatue *>(&e)) {
    parts.push_back(std::to_string(statue->BossHp()));
  } else if (const auto *boat = dynamic_cast<const entity::Boat *>(&e)) {
    parts.push_back(std::to_string(static_cast<int32_t>(boat->Dir())));
  }

  parts.push_back(std::to_string(level_idx));

  std::string line = name + "[";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0)
      line.push_back(':');
    line += parts[i];
  }
  line.push_back(']');
  return line;
}

bool Save::CaptureWorldSnapshot(WorldSaveContext &ctx, WorldSaveSnapshot *out) {
  if (!out || ctx.world_dir.empty() || !ctx.levels || !ctx.player ||
      !*ctx.player) {
    return false;
  }

  auto &gs = game::GameState::Instance();
  auto report = [&](std::string_view msg) {
    if (ctx.on_progress)
      ctx.on_progress(std::string(msg));
  };

  WorldSaveSnapshot snap;
  snap.world_dir = ctx.world_dir;
  snap.directories.push_back(ctx.world_dir);

  const auto &player = **ctx.player;
  const int32_t cur_lvl_idx =
      ctx.current_level_idx ? *ctx.current_level_idx : 3;

  // Game.miniplussave
  report("minicraft.displays.loading.message.world");
  {
    std::vector<std::string> lines;
    lines.push_back(std::string(Version::kCurrentVersion));
    lines.push_back(io::Settings::GetString("mode"));
    lines.push_back(std::to_string(gs.score_time));
    lines.push_back(std::to_string(gs.game_time));
    lines.push_back(io::Settings::GetString("diff"));
    lines.push_back(gs.air_wizard_beaten ? "true" : "false");
    lines.push_back(gs.obsidian_knight_beaten ? "true" : "false");
    lines.push_back(gs.obsidian_knight_summoned ? "true" : "false");
    lines.push_back(std::to_string(cur_lvl_idx));
    lines.push_back(std::to_string(gs.tick_count));
    lines.push_back(gs.past_day1 ? "true" : "false");
    snap.files.push_back({JoinWorldPath(ctx.world_dir, "Game.miniplussave"),
                          JoinLines(lines), true});
  }

  // Levels (Level0..Level5)
  report("minicraft.displays.loading.message.levels");
  for (int32_t i = 0; i < 6; ++i) {
    const auto &lvl = (*ctx.levels)[i];
    if (!lvl)
      continue;

    const std::string lvl_dir =
        JoinWorldPath(ctx.world_dir, "Level" + std::to_string(i));
    snap.directories.push_back(lvl_dir);

    std::vector<std::string> idx_lines;
    idx_lines.push_back(std::to_string(lvl->w));
    idx_lines.push_back(std::to_string(lvl->h));
    idx_lines.push_back(std::to_string(lvl->seed));
    idx_lines.push_back(std::to_string(lvl->depth));

    auto &cm = lvl->GetChunkManager();
    for (auto &[key, chunk_ptr] : cm.ChunksMutable()) {
      (void)key;
      if (!chunk_ptr)
        continue;
      idx_lines.push_back(std::to_string(chunk_ptr->chunk_x) + "," +
                          std::to_string(chunk_ptr->chunk_y) + "," +
                          std::to_string(chunk_ptr->stage));

      const std::string suffix = std::to_string(chunk_ptr->chunk_x) + "." +
                                 std::to_string(chunk_ptr->chunk_y) +
                                 ".miniplussave";
      snap.files.push_back({JoinWorldPath(lvl_dir, "t." + suffix),
                            SerializeChunkArrayCsv(chunk_ptr->tile_ids),
                            false});
      snap.files.push_back({JoinWorldPath(lvl_dir, "d." + suffix),
                            SerializeChunkArrayCsv(chunk_ptr->tile_data),
                            false});
      chunk_ptr->dirty = false;
    }

    snap.files.push_back({JoinWorldPath(lvl_dir, "index.miniplussave"),
                          JoinLines(idx_lines), false});
  }

  // Player.miniplussave
  report("minicraft.displays.loading.message.entities");
  {
    nlohmann::json unlocked_json = nlohmann::json::object();
    if (ctx.unlocked_recipes) {
      for (const auto &r : *ctx.unlocked_recipes) {
        auto prod = r.GetProduct();
        std::string prod_name = prod ? prod->Name() : r.Product();
        nlohmann::json costs = nlohmann::json::array();
        for (const auto &[cost_name, cost_amt] : r.Costs()) {
          costs.push_back(cost_name + "_" + std::to_string(cost_amt));
        }
        unlocked_json[prod_name + "_" + std::to_string(r.Amount())] =
            std::move(costs);
      }
    }

    std::vector<std::string> p_lines;
    p_lines.push_back(std::to_string(player.x));
    p_lines.push_back(std::to_string(player.y));
    p_lines.push_back(std::to_string(player.spawnx));
    p_lines.push_back(std::to_string(player.spawny));
    p_lines.push_back(std::to_string(player.spawn_depth));
    p_lines.push_back(std::to_string(player.health));
    p_lines.push_back(std::to_string(player.hunger));
    p_lines.push_back(std::to_string(player.stamina));
    p_lines.push_back(std::to_string(player.armor));
    p_lines.push_back(std::to_string(player.armor_damage_buffer));
    p_lines.push_back(player.cur_armor ? player.cur_armor->GetData() : "None");
    p_lines.push_back(std::to_string(player.shirt_color));
    p_lines.push_back(player.skinon ? "true" : "false");
    p_lines.push_back(std::to_string(player.GetScore()));
    p_lines.push_back(std::to_string(cur_lvl_idx));
    p_lines.push_back(SerializePotionEffects(player));
    p_lines.push_back(std::to_string(player.max_health));
    p_lines.push_back(unlocked_json.dump());
    snap.files.push_back({JoinWorldPath(ctx.world_dir, "Player.miniplussave"),
                          JoinLines(p_lines), false});
  }

  // Inventory.miniplussave
  report("minicraft.displays.loading.message.entities");
  {
    auto inv_lines = WriteInventory(player.GetInventory(), &player);
    snap.files.push_back(
        {JoinWorldPath(ctx.world_dir, "Inventory.miniplussave"),
         JoinLines(inv_lines), false});
  }

  // Entities.miniplussave
  report("minicraft.displays.loading.message.entities");
  {
    std::vector<std::string> ent_lines;
    for (int32_t i = 0; i < 6; ++i) {
      const auto &lvl = (*ctx.levels)[i];
      if (!lvl)
        continue;
      for (const auto &e : lvl->Entities()) {
        if (!e || e->removed)
          continue;
        std::string serialized = WriteEntity(*e, i, true);
        if (!serialized.empty()) {
          ent_lines.push_back(std::move(serialized));
        }
      }
    }
    snap.files.push_back({JoinWorldPath(ctx.world_dir, "Entities.miniplussave"),
                          JoinLines(ent_lines), false});
  }

  // advancements.json
  snap.files.push_back(
      {JoinWorldPath(ctx.world_dir, "advancements.json"),
       util::AdvancementSystem::Instance().SaveAdvancementsJson(), false});

  // signs.json
  if (ctx.signs) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto &[coord, lines] : *ctx.signs) {
      nlohmann::json entry;
      entry["level"] = coord.level;
      entry["x"] = coord.x;
      entry["y"] = coord.y;
      entry["lines"] = lines;
      j.push_back(std::move(entry));
    }
    snap.files.push_back(
        {JoinWorldPath(ctx.world_dir, "signs.json"), j.dump(2), false});
  }

  *out = std::move(snap);
  return true;
}

bool Save::WriteWorldSnapshot(const platform::IFileSystem &fs,
                              const WorldSaveSnapshot &snapshot,
                              bool notify_on_complete,
                              const std::function<void(float)> &on_percent) {
  auto &gs = game::GameState::Instance();
  gs.saving = true;

  for (const auto &dir : snapshot.directories) {
    fs.CreateDirectories(dir);
  }

  const size_t total = snapshot.files.size();
  for (size_t i = 0; i < total; ++i) {
    const auto &entry = snapshot.files[i];
    if (!fs.WriteTextFile(entry.path, entry.content) && entry.critical) {
      util::log::kSaveLoad.Error("Failed writing Game.miniplussave");
      gs.saving = false;
      return false;
    }
    if (on_percent && total > 0) {
      on_percent(100.0f * static_cast<float>(i + 1) /
                 static_cast<float>(total));
    }
  }

  gs.saving = false;
  if (notify_on_complete) {
    gs.NotifyAll("minicraft.notification.world_saved");
  }
  return true;
}

bool Save::SaveWorld(const platform::IFileSystem &fs, WorldSaveContext &ctx) {
  auto &gs = game::GameState::Instance();
  gs.saving = true;

  WorldSaveSnapshot snapshot;
  if (!CaptureWorldSnapshot(ctx, &snapshot)) {
    gs.saving = false;
    return false;
  }

  return WriteWorldSnapshot(fs, snapshot, true);
}

bool Save::SavePrefs(const platform::IFileSystem &fs, std::string_view game_dir,
                     const io::InputHandler *input,
                     const std::vector<std::string> *resource_packs) {
  if (game_dir.empty())
    return false;
  fs.CreateDirectories(game_dir);

  auto &gs = game::GameState::Instance();

  nlohmann::json prefs;
  prefs["version"] = std::string(Version::kCurrentVersion);
  prefs["sound"] = io::Settings::GetBool("sound");
  prefs["autosave"] = io::Settings::GetBool("autosave");
  prefs["fps"] = io::Settings::GetMaxFps();
  prefs["lang"] = util::Localization::Instance().SelectedLocaleTag();
  prefs["diff"] = io::Settings::GetString("diff");
  prefs["fullscreen"] = io::Settings::GetBool("fullscreen");
  prefs["tutorials"] = io::Settings::GetBool("tutorials");
  prefs["showquests"] = io::Settings::GetBool("showquests");
  prefs["skinIdx"] = gs.selected_skin_index;
  prefs["skin"] = gs.selected_skin_name;

  if (input) {
    prefs["keymap"] = input->GetKeyPrefs();
  }
  if (resource_packs) {
    prefs["resourcePacks"] = *resource_packs;
  }

  const bool ok_prefs = fs.WriteTextFile(
      JoinWorldPath(game_dir, "Preferences.json"), prefs.dump(2));

  nlohmann::json unlocks;
  unlocks["achievements"] =
      util::AchievementManager::Instance().GetUnlockedIds();
  try {
    unlocks["unlockedRecipes"] = nlohmann::json::parse(
        util::AdvancementSystem::Instance().SaveRecipeUnlocksJson());
  } catch (...) {
    unlocks["unlockedRecipes"] = nlohmann::json::object();
  }
  nlohmann::json visible_score_times = nlohmann::json::array();
  if (const auto *st_entry = io::Settings::GetEntry("scoretime")) {
    for (size_t i = 0; i < st_entry->int_options.size(); ++i) {
      if (st_entry->IsOptionVisible(i)) {
        visible_score_times.push_back(st_entry->int_options[i]);
      }
    }
  }
  unlocks["visibleScoreTimes"] = std::move(visible_score_times);

  const bool ok_unlocks = fs.WriteTextFile(
      JoinWorldPath(game_dir, "Unlocks.json"), unlocks.dump(2));

  return ok_prefs && ok_unlocks;
}

} // namespace minicraft::saveload
