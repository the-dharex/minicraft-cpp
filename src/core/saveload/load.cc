#include "core/saveload/load.h"

#include <algorithm>
#include <array>
#include <cctype>
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
#include "core/item/items.h"
#include "core/item/recipes.h"
#include "core/level/chunk_manager.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/platform/file_system.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/logging.h"

namespace minicraft::saveload {

namespace {

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) {
      std::string line(text.substr(pos));
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) out.push_back(std::move(line));
      break;
    }
    std::string line(text.substr(pos, nl - pos));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
    pos = nl + 1;
    if (pos == text.size()) break;
  }
  return out;
}

std::vector<std::string_view> SplitSv(std::string_view s, char delim) {
  std::vector<std::string_view> out;
  size_t pos = 0;
  while (true) {
    size_t d = s.find(delim, pos);
    if (d == std::string_view::npos) {
      out.push_back(s.substr(pos));
      break;
    }
    out.push_back(s.substr(pos, d - pos));
    pos = d + 1;
  }
  return out;
}

std::string_view TrimSv(std::string_view s) {
  while (!s.empty() &&
         std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() &&
         std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

int32_t ParseInt(std::string_view s, int32_t def = 0) {
  s = TrimSv(s);
  int32_t val = def;
  std::from_chars(s.data(), s.data() + s.size(), val);
  return val;
}

int64_t ParseInt64(std::string_view s, int64_t def = 0) {
  s = TrimSv(s);
  int64_t val = def;
  std::from_chars(s.data(), s.data() + s.size(), val);
  return val;
}

bool ParseBool(std::string_view s) {
  s = TrimSv(s);
  return s == "true" || s == "TRUE" || s == "1";
}

// Bracket-aware comma splitter matching Java Load.splitUnwrappedCommas, used
// to parse Java `.miniplussave` files alongside C++ line-separated saves.
std::vector<std::string> SplitUnwrappedCommas(std::string_view input) {
  std::vector<std::string> out;
  int32_t depth = 0;
  bool in_quotes = false;
  bool escaped = false;
  size_t start = 0;

  auto push_token = [&](size_t end) {
    std::string_view tok = TrimSv(input.substr(start, end - start));
    std::string cleaned;
    cleaned.reserve(tok.size());
    for (char c : tok) {
      if (c != '\r' && c != '\n') cleaned.push_back(c);
    }
    out.push_back(std::move(cleaned));
  };

  for (size_t i = 0; i < input.size(); ++i) {
    char ch = input[i];
    if (in_quotes) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_quotes = false;
      }
      continue;
    }
    if (ch == '"' && depth > 0) {
      in_quotes = true;
    } else if (ch == '{' || ch == '[' || ch == '(') {
      ++depth;
    } else if (ch == '}' || ch == ']' || ch == ')') {
      if (depth > 0) --depth;
    } else if (ch == ',' && depth == 0) {
      push_token(i);
      start = i + 1;
    }
  }

  std::string_view tail = TrimSv(input.substr(start));
  if (!tail.empty() && !(tail.size() == 1 && tail[0] == ',')) {
    push_token(input.size());
  }
  return out;
}

bool IsCommaSaveFormat(std::string_view text,
                       const std::vector<std::string>& lines) {
  if (lines.empty()) return false;
  if (lines.size() == 1 && lines[0].find(',') != std::string::npos) return true;
  if (!Version(lines[0], false).IsValid() &&
      text.find(',') != std::string_view::npos) {
    return true;
  }
  return false;
}

std::string MapJavaTileName(std::string_view raw_name, const Version& world_ver,
                            int16_t* data) {
  std::string name(raw_name);
  auto ieq = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  };

  if (ieq(name, "Wool")) {
    if (world_ver < Version(2, 0, 6, 4)) {
      switch (*data) {
        case 1:
          return "Red Wool";
        case 2:
          return "Yellow Wool";
        case 3:
          return "Green Wool";
        case 4:
          return "Blue Wool";
        case 5:
          return "Black Wool";
        default:
          return "White Wool";
      }
    } else if (world_ver < Version(2, 3, 0, 1)) {
      return "White Wool";
    }
  }
  if (world_ver <= Version(2, 3, 0, 1) && ieq(name, "Flower")) {
    *data = 0;
  }
  return name;
}

std::string ReplaceAll(std::string str, std::string_view from,
                       std::string_view to) {
  size_t pos = 0;
  while ((pos = str.find(from, pos)) != std::string::npos) {
    str.replace(pos, from.size(), to);
    pos += to.size();
  }
  return str;
}

template <typename T, size_t N>
void ParseCsvArray(std::string_view text, std::array<T, N>* out) {
  size_t idx = 0;
  size_t pos = 0;
  while (pos < text.size() && idx < N) {
    size_t comma = text.find(',', pos);
    if (comma == std::string_view::npos) comma = text.size();
    std::string_view tok = TrimSv(text.substr(pos, comma - pos));
    int32_t v = 0;
    if (!tok.empty()) {
      std::from_chars(tok.data(), tok.data() + tok.size(), v);
    }
    (*out)[idx++] = static_cast<T>(v);
    pos = comma + 1;
  }
}

std::shared_ptr<entity::Entity> CreateEntityByName(std::string_view name,
                                                   int32_t mob_lvl) {
  if (name == "Cow") return std::make_shared<entity::Cow>();
  if (name == "Sheep") return std::make_shared<entity::Sheep>();
  if (name == "Pig") return std::make_shared<entity::Pig>();
  if (name == "Zombie") return std::make_shared<entity::Zombie>(mob_lvl);
  if (name == "Slime") return std::make_shared<entity::Slime>(mob_lvl);
  if (name == "Creeper") return std::make_shared<entity::Creeper>(mob_lvl);
  if (name == "Skeleton") return std::make_shared<entity::Skeleton>(mob_lvl);
  if (name == "Knight") return std::make_shared<entity::Knight>(mob_lvl);
  if (name == "Snake") return std::make_shared<entity::Snake>(mob_lvl);
  if (name == "AirWizard") return std::make_shared<entity::AirWizard>();
  if (name == "ObsidianKnight")
    return std::make_shared<entity::ObsidianKnight>();
  if (name == "Spawner")
    return std::make_shared<entity::Spawner>("Zombie", 1);
  if (name == "Workbench")
    return std::make_shared<entity::Crafter>(entity::Crafter::Type::kWorkbench);
  if (name == "Anvil")
    return std::make_shared<entity::Crafter>(entity::Crafter::Type::kAnvil);
  if (name == "Enchanter")
    return std::make_shared<entity::Crafter>(entity::Crafter::Type::kEnchanter);
  if (name == "Loom")
    return std::make_shared<entity::Crafter>(entity::Crafter::Type::kLoom);
  if (name == "Furnace")
    return std::make_shared<entity::Crafter>(entity::Crafter::Type::kFurnace);
  if (name == "Oven")
    return std::make_shared<entity::Crafter>(entity::Crafter::Type::kOven);
  if (name == "DyeVat")
    return std::make_shared<entity::Crafter>(entity::Crafter::Type::kDyeVat);
  if (name == "Composter") return std::make_shared<entity::Composter>();
  if (name == "Chest") return std::make_shared<entity::Chest>();
  if (name == "DeathChest") return std::make_shared<entity::DeathChest>();
  if (name == "DungeonChest")
    return std::make_shared<entity::DungeonChest>(false, true);
  if (name == "Bed") return std::make_shared<entity::Bed>();
  if (name == "Tnt") return std::make_shared<entity::Tnt>();
  if (name == "Lantern")
    return std::make_shared<entity::Lantern>(entity::Lantern::Type::kNorm);
  if (name == "KnightStatue")
    return std::make_shared<entity::KnightStatue>(500);
  if (name == "Boat")
    return std::make_shared<entity::Boat>(entity::Direction::kDown);
  return nullptr;
}

std::string NormalizeBedColor(std::string_view prefix) {
  // Turns "LIGHT_BLUE" or "Light Blue" into "Light Blue"
  std::string out;
  bool cap_next = true;
  for (char c : prefix) {
    if (c == '_') c = ' ';
    if (c == ' ') {
      out.push_back(' ');
      cap_next = true;
    } else if (cap_next) {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
      cap_next = false;
    } else {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

}  // namespace

std::string Load::SubOldName(std::string_view name_sv,
                             const Version& world_ver) {
  std::string name(name_sv);
  if (world_ver < Version(1, 9, 4, 4)) {
    name = ReplaceAll(name, "Hatchet", "Axe");
    name = ReplaceAll(name, "Pick", "Pickaxe");
    name = ReplaceAll(name, "Pickaxeaxe", "Pickaxe");
    name = ReplaceAll(name, "Spade", "Shovel");
    name = ReplaceAll(name, "Pow glove", "Power Glove");
    name = ReplaceAll(name, "II", "");
    name = ReplaceAll(name, "W.Bucket", "Water Bucket");
    name = ReplaceAll(name, "L.Bucket", "Lava Bucket");
    name = ReplaceAll(name, "G.Apple", "Gold Apple");
    name = ReplaceAll(name, "St.", "Stone");
    name = ReplaceAll(name, "Ob.", "Obsidian");
    name = ReplaceAll(name, "I.Lantern", "Iron Lantern");
    name = ReplaceAll(name, "G.Lantern", "Gold Lantern");
    name = ReplaceAll(name, "BrickWall", "Wall");
    name = ReplaceAll(name, "Brick", " Brick");
    name = ReplaceAll(name, "Wall", " Wall");
    name = ReplaceAll(name, "  ", " ");
    if (name == "Bucket") name = "Empty Bucket";
  }
  if (world_ver < Version(1, 9, 4)) {
    name = ReplaceAll(name, "I.Armor", "Iron Armor");
    name = ReplaceAll(name, "S.Armor", "Snake Armor");
    name = ReplaceAll(name, "L.Armor", "Leather Armor");
    name = ReplaceAll(name, "G.Armor", "Gold Armor");
    name = ReplaceAll(name, "BrickWall", "Wall");
  }
  if (world_ver < Version(2, 0, 6, 3)) {
    name = ReplaceAll(name, "Fishing Rod", "Wood Fishing Rod");
  }
  if (world_ver < Version(2, 0, 6)) {
    if (name.rfind("Pork Chop", 0) == 0) {
      name = ReplaceAll(name, "Pork Chop", "Cooked Pork");
    }
  }
  if (world_ver < Version(2, 0, 7, 1)) {
    if (name.rfind("Seeds", 0) == 0) {
      name = ReplaceAll(name, "Seeds", "Wheat Seeds");
    }
  }
  if (world_ver < Version(2, 1, 0, 2)) {
    if (name.rfind("Shear", 0) == 0 && name.rfind("Shears", 0) != 0) {
      name = ReplaceAll(name, "Shear", "Shears");
    }
  }
  if (world_ver < Version(2, 2, 0, 4)) {
    if (name.rfind("Potion", 0) == 0) {
      name = ReplaceAll(name, "Potion", "Awkward Potion");
    }
  }
  if (world_ver < Version(2, 3, 0, 1)) {
    if (name.rfind("Wool", 0) == 0) {
      name = ReplaceAll(name, "Wool", "White Wool");
    }
    if (name.rfind("Flower", 0) == 0) {
      name = ReplaceAll(name, "Flower", "Oxeye Daisy");
    }
  }
  return name;
}

std::vector<WorldMetadata> Load::ListWorlds(const platform::IFileSystem& fs,
                                            std::string_view saves_dir) {
  std::vector<WorldMetadata> worlds;
  if (!fs.Exists(saves_dir)) return worlds;

  for (const auto& entry : fs.ListDirectory(saves_dir)) {
    const std::string world_dir = Save::JoinWorldPath(saves_dir, entry);
    if (!fs.IsDirectory(world_dir)) continue;

    WorldMetadata meta;
    meta.folder_name = entry;
    meta.world_dir = world_dir;

    std::string game_text;
    const std::string game_file =
        Save::JoinWorldPath(world_dir, "Game.miniplussave");
    if (!fs.ReadTextFile(game_file, &game_text)) continue;

    auto lines = SplitLines(game_text);
    if (lines.empty()) continue;

    if (IsCommaSaveFormat(game_text, lines)) {
      auto tokens = SplitUnwrappedCommas(game_text);
      if (tokens.empty()) continue;
      meta.version = Version(tokens[0], false);
      if (!meta.version.IsValid()) continue;

      size_t idx = 1;
      if (meta.version >= Version(2, 2, 0, 1) && idx < tokens.size()) {
        ++idx;  // world seed
      }
      if (meta.version >= Version(2, 0, 4, 8) && idx < tokens.size()) {
        auto mparts = SplitSv(tokens[idx++], ';');
        int32_t m_idx = mparts.empty() ? 0 : ParseInt(mparts[0], 0);
        static constexpr const char* kModeKeys[] = {
            "minicraft.settings.mode.survival",
            "minicraft.settings.mode.creative",
            "minicraft.settings.mode.hardcore",
            "minicraft.settings.mode.score"};
        meta.mode = kModeKeys[std::clamp(m_idx, 0, 3)];
      }
      if (idx < tokens.size()) ++idx;  // time
      if (idx < tokens.size()) {
        meta.game_time = ParseInt(tokens[idx++], 0);
      }
      meta.valid = true;

      std::string player_text;
      if (fs.ReadTextFile(Save::JoinWorldPath(world_dir, "Player.miniplussave"),
                          &player_text)) {
        auto p_tokens = SplitUnwrappedCommas(player_text);
        if (p_tokens.size() > 10) {
          meta.score = ParseInt(p_tokens[10], meta.score);
        }
      }
    } else {
      meta.version = Version(lines[0], false);
      if (!meta.version.IsValid()) continue;

      if (lines.size() > 1) meta.mode = lines[1];
      if (lines.size() > 2) meta.score = ParseInt(lines[2], 0);
      if (lines.size() > 9) {
        meta.game_time = ParseInt(lines[9], 0);
      } else if (lines.size() > 3) {
        meta.game_time = ParseInt(lines[3], 0);
      }
      meta.valid = true;

      // Also pull exact score from Player.miniplussave if present.
      std::string player_text;
      if (fs.ReadTextFile(Save::JoinWorldPath(world_dir, "Player.miniplussave"),
                          &player_text)) {
        auto p_lines = SplitLines(player_text);
        if (p_lines.size() > 13) {
          meta.score = ParseInt(p_lines[13], meta.score);
        }
      }
    }

    worlds.push_back(std::move(meta));
  }

  std::sort(worlds.begin(), worlds.end(),
            [](const WorldMetadata& a, const WorldMetadata& b) {
              return a.folder_name < b.folder_name;
            });
  return worlds;
}

bool Load::LoadPrefs(const platform::IFileSystem& fs,
                     std::string_view game_dir, io::InputHandler* input,
                     std::vector<std::string>* out_resource_packs) {
  bool loaded_any = false;
  auto& gs = game::GameState::Instance();

  std::string prefs_text;
  if (fs.ReadTextFile(Save::JoinWorldPath(game_dir, "Preferences.json"),
                      &prefs_text)) {
    try {
      auto j = nlohmann::json::parse(prefs_text);
      if (j.contains("sound") && j["sound"].is_boolean()) {
        io::Settings::Set("sound", j["sound"].get<bool>());
      }
      if (j.contains("autosave") && j["autosave"].is_boolean()) {
        io::Settings::Set("autosave", j["autosave"].get<bool>());
      }
      if (j.contains("fps") && j["fps"].is_number_integer()) {
        io::Settings::SetMaxFps(j["fps"].get<int32_t>());
      }
      if (j.contains("lang") && j["lang"].is_string()) {
        util::Localization::Instance().ChangeLanguage(
            j["lang"].get<std::string>());
      }
      if (j.contains("diff") && j["diff"].is_string()) {
        io::Settings::Set("diff", j["diff"].get<std::string>());
      }
      if (j.contains("fullscreen") && j["fullscreen"].is_boolean()) {
        io::Settings::Set("fullscreen", j["fullscreen"].get<bool>());
      }
      if (j.contains("tutorials") && j["tutorials"].is_boolean()) {
        io::Settings::Set("tutorials", j["tutorials"].get<bool>());
      }
      if (j.contains("showquests") && j["showquests"].is_boolean()) {
        io::Settings::Set("showquests", j["showquests"].get<bool>());
      }
      if (j.contains("skinIdx") && j["skinIdx"].is_number_integer()) {
        gs.selected_skin_index = j["skinIdx"].get<int32_t>();
      }
      if (j.contains("skin") && j["skin"].is_string()) {
        gs.selected_skin_name = j["skin"].get<std::string>();
      }
      if (input && j.contains("keymap") && j["keymap"].is_array()) {
        for (const auto& kv : j["keymap"]) {
          if (!kv.is_string()) continue;
          std::string s = kv.get<std::string>();
          size_t sep = s.find(';');
          if (sep != std::string::npos) {
            input->SetKey(std::string_view(s).substr(0, sep),
                          std::string_view(s).substr(sep + 1));
          }
        }
      }
      if (out_resource_packs && j.contains("resourcePacks") &&
          j["resourcePacks"].is_array()) {
        out_resource_packs->clear();
        for (const auto& p : j["resourcePacks"]) {
          if (p.is_string()) out_resource_packs->push_back(p.get<std::string>());
        }
      }
      loaded_any = true;
    } catch (const std::exception& e) {
      util::log::kSaveLoad.Error(std::string("Failed parsing Preferences.json: ") +
                                 e.what());
    }
  }

  std::string unlocks_text;
  if (fs.ReadTextFile(Save::JoinWorldPath(game_dir, "Unlocks.json"),
                      &unlocks_text)) {
    try {
      auto j = nlohmann::json::parse(unlocks_text);
      if (j.contains("achievements") && j["achievements"].is_array()) {
        std::vector<std::string> ids;
        for (const auto& a : j["achievements"]) {
          if (a.is_string()) ids.push_back(a.get<std::string>());
        }
        util::AchievementManager::Instance().UnlockFromList(ids);
      }
      if (j.contains("unlockedRecipes") && j["unlockedRecipes"].is_object()) {
        util::AdvancementSystem::Instance().LoadRecipeUnlocksJson(
            j["unlockedRecipes"].dump());
      }
      if (j.contains("visibleScoreTimes") &&
          j["visibleScoreTimes"].is_array()) {
        if (auto* st_entry = io::Settings::GetEntry("scoretime")) {
          for (const auto& v : j["visibleScoreTimes"]) {
            if (!v.is_number_integer()) continue;
            const int32_t score_val = v.get<int32_t>();
            for (size_t i = 0; i < st_entry->int_options.size(); ++i) {
              if (st_entry->int_options[i] == score_val &&
                  i < st_entry->option_visible.size()) {
                st_entry->option_visible[i] = true;
              }
            }
          }
        }
      }
      loaded_any = true;
    } catch (const std::exception& e) {
      util::log::kSaveLoad.Error(std::string("Failed parsing Unlocks.json: ") +
                                 e.what());
    }
  }

  return loaded_any;
}

void Load::LoadInventory(const std::vector<std::string>& lines,
                         item::Inventory& inv, const Version& world_ver,
                         entity::DeathChest* overflow_chest) {
  inv.ClearInv();
  for (const auto& raw_line : lines) {
    if (raw_line.empty()) continue;
    std::string item_str = SubOldName(raw_line, world_ver);
    if (item_str.find("Power Glove") != std::string::npos ||
        item_str.find("Totem of Wind") != std::string::npos) {
      continue;
    }

    auto give_item = [&](std::unique_ptr<item::Item> it) {
      if (!it) return;
      auto leftover = inv.Add(std::move(it));
      if (leftover && overflow_chest && overflow_chest->GetInventory()) {
        overflow_chest->GetInventory()->Add(std::move(leftover));
      }
    };

    if (world_ver <= Version(2, 0, 4) &&
        item_str.find(';') != std::string::npos) {
      size_t semi = item_str.find(';');
      std::string_view item_name = std::string_view(item_str).substr(0, semi);
      int32_t count = ParseInt(std::string_view(item_str).substr(semi + 1), 1);
      auto proto = item::Items::Get(item_name, true);
      if (!proto) continue;
      if (auto* stack = dynamic_cast<item::StackableItem*>(proto.get())) {
        stack->count = count;
        give_item(std::move(proto));
      } else {
        for (int32_t c = 0; c < count; ++c) {
          give_item(proto->Clone());
        }
      }
    } else {
      auto it = item::Items::Get(item_str, true);
      if (it) give_item(std::move(it));
    }
  }
}

std::shared_ptr<entity::Entity> Load::LoadEntity(std::string_view entity_data,
                                                 const Version& world_ver,
                                                 WorldSaveContext& ctx) {
  entity_data = TrimSv(entity_data);
  if (entity_data.empty()) return nullptr;

  size_t lbr = entity_data.find('[');
  size_t rbr = entity_data.rfind(']');
  if (lbr == std::string_view::npos || rbr == std::string_view::npos ||
      rbr <= lbr) {
    return nullptr;
  }

  std::string_view entity_name_raw = entity_data.substr(0, lbr);
  std::string_view inside = entity_data.substr(lbr + 1, rbr - lbr - 1);
  auto info = SplitSv(inside, ':');
  if (info.size() < 3) return nullptr;

  const int32_t x = ParseInt(info[0], 0);
  const int32_t y = ParseInt(info[1], 0);

  // Strip optional package qualifiers from old saves (`minicraft.entity...`)
  std::string_view entity_name = entity_name_raw;
  if (size_t dot = entity_name.rfind('.'); dot != std::string_view::npos) {
    entity_name = entity_name.substr(dot + 1);
  }

  std::shared_ptr<entity::Entity> new_entity;
  if (entity_name.size() > 4 &&
      entity_name.substr(entity_name.size() - 4) == " Bed") {
    std::string color =
        NormalizeBedColor(entity_name.substr(0, entity_name.size() - 4));
    new_entity = std::make_shared<entity::Bed>(std::move(color));
  } else {
    int32_t mob_lvl = 1;
    if (entity_name == "AirWizard" && info.size() >= 4) {
      mob_lvl = ParseInt(info[3], 1);
      if (mob_lvl > 1) return nullptr;
    }
    new_entity = CreateEntityByName(entity_name, mob_lvl);
  }

  if (!new_entity) {
    util::log::kSaveLoad.Warn("Unknown entity in save: " +
                              std::string(entity_name_raw));
    return nullptr;
  }

  const int32_t lvl_idx = ParseInt(info.back(), 4);

  if (auto* mob = dynamic_cast<entity::Mob*>(new_entity.get())) {
    mob->health = ParseInt(info[2], mob->max_health);
    if (auto* enemy = dynamic_cast<entity::EnemyMob*>(new_entity.get())) {
      if (info.size() >= 5) {
        enemy->lvl = std::clamp(ParseInt(info[info.size() - 2], 1), 1, 4);
      }
    } else if (auto* ok =
                   dynamic_cast<entity::ObsidianKnight*>(new_entity.get())) {
      (void)ok;
    } else if (auto* sheep = dynamic_cast<entity::Sheep*>(new_entity.get())) {
      if (world_ver >= Version(2, 0, 7, 1) && info.size() >= 5) {
        sheep->cut = ParseBool(info[3]);
      }
    }
  } else if (auto* chest = dynamic_cast<entity::Chest*>(new_entity.get())) {
    bool is_death = dynamic_cast<entity::DeathChest*>(chest) != nullptr;
    bool is_dungeon = dynamic_cast<entity::DungeonChest*>(chest) != nullptr;
    size_t end_idx = info.size() - 1 - ((is_death || is_dungeon) ? 1 : 0);
    for (size_t idx = 2; idx < end_idx; ++idx) {
      std::string item_str = SubOldName(info[idx], world_ver);
      if (item_str.find("Power Glove") != std::string::npos ||
          item_str.find("Totem of Wind") != std::string::npos) {
        continue;
      }
      auto it = item::Items::Get(item_str, true);
      if (it && chest->GetInventory()) {
        chest->GetInventory()->Add(std::move(it));
      }
    }
    if (is_death && info.size() >= 4) {
      static_cast<entity::DeathChest*>(chest)->SetTime(
          ParseInt(info[info.size() - 2], 60 * 300));
    } else if (is_dungeon && info.size() >= 4) {
      auto* dc = static_cast<entity::DungeonChest*>(chest);
      dc->is_locked = ParseBool(info[info.size() - 2]);
      if (dc->is_locked && ctx.levels && lvl_idx >= 0 && lvl_idx < 6 &&
          (*ctx.levels)[lvl_idx]) {
        (*ctx.levels)[lvl_idx]->chest_count++;
      }
    }
  } else if (dynamic_cast<entity::Spawner*>(new_entity.get()) &&
             info.size() >= 5) {
    std::string_view mob_name = info[2];
    if (size_t dot = mob_name.rfind('.'); dot != std::string_view::npos) {
      mob_name = mob_name.substr(dot + 1);
    }
    new_entity = std::make_shared<entity::Spawner>(
        std::string(mob_name), ParseInt(info[3], 1));
  } else if (dynamic_cast<entity::Lantern*>(new_entity.get()) &&
             world_ver >= Version(1, 9, 4) && info.size() > 3) {
    int32_t t = std::clamp(ParseInt(info[2], 0), 0, 2);
    new_entity =
        std::make_shared<entity::Lantern>(static_cast<entity::Lantern::Type>(t));
  } else if (dynamic_cast<entity::KnightStatue*>(new_entity.get()) &&
             info.size() >= 4) {
    new_entity =
        std::make_shared<entity::KnightStatue>(ParseInt(info[2], 500));
  } else if (dynamic_cast<entity::Boat*>(new_entity.get()) &&
             info.size() >= 4) {
    int32_t d = std::clamp(ParseInt(info[2], 0), 0, 3);
    new_entity =
        std::make_shared<entity::Boat>(static_cast<entity::Direction>(d));
  }

  if (ctx.levels && lvl_idx >= 0 && lvl_idx < 6 && (*ctx.levels)[lvl_idx]) {
    (*ctx.levels)[lvl_idx]->Add(new_entity, x, y, false);
  }
  return new_entity;
}

bool Load::LoadWorld(const platform::IFileSystem& fs, WorldSaveContext& ctx) {
  auto& gs = game::GameState::Instance();
  auto report = [&](std::string_view msg) {
    if (ctx.on_progress) ctx.on_progress(std::string(msg));
  };

  if (ctx.world_dir.empty() || !fs.IsDirectory(ctx.world_dir) || !ctx.levels ||
      !ctx.player) {
    return false;
  }

  gs.ResetForNewWorld();

  // 1. Game.miniplussave
  report("minicraft.displays.loading.message.world");
  std::string game_text;
  if (!fs.ReadTextFile(Save::JoinWorldPath(ctx.world_dir, "Game.miniplussave"),
                       &game_text)) {
    util::log::kSaveLoad.Error("Missing Game.miniplussave in " + ctx.world_dir);
    return false;
  }
  auto game_lines = SplitLines(game_text);
  if (game_lines.empty()) return false;

  const bool is_java_save = IsCommaSaveFormat(game_text, game_lines);
  Version world_ver(game_lines[0], false);
  int32_t saved_lvl_idx = 3;

  if (is_java_save) {
    auto tokens = SplitUnwrappedCommas(game_text);
    if (tokens.empty()) return false;
    world_ver = Version(tokens[0], false);
    if (!world_ver.IsValid()) return false;

    size_t idx = 1;
    if (world_ver >= Version(2, 2, 0, 1) && idx < tokens.size()) {
      ++idx;  // world seed (level files store per-level seeds)
    }
    if (world_ver >= Version(2, 0, 4, 8) && idx < tokens.size()) {
      auto mparts = SplitSv(tokens[idx++], ';');
      const int32_t mode_idx =
          std::clamp(mparts.empty() ? 0 : ParseInt(mparts[0], 0), 0, 3);
      io::Settings::SetIdx("mode", static_cast<size_t>(mode_idx));
      if (mode_idx == 3) {
        gs.score_time = mparts.size() > 1 ? ParseInt(mparts[1], 300) : 300;
      }
    }
    if (idx < tokens.size()) {
      const int32_t saved_tod = ParseInt(tokens[idx++], 0);
      if (saved_tod >= game::GameState::kDayLength) {
        gs.game_time = 0;
        gs.past_day1 = true;
      } else {
        gs.SetGameTime(std::max<int32_t>(0, saved_tod));
      }
    }
    if (idx < tokens.size()) {
      const int32_t total_gt = ParseInt(tokens[idx++], 0);
      if (world_ver >= Version(1, 9, 3, 2)) {
        gs.tick_count = std::max<int32_t>(0, total_gt);
        gs.past_day1 = gs.past_day1 || (total_gt > 65000);
      } else {
        gs.tick_count = 65000;
        gs.past_day1 = true;
      }
    }
    if (idx < tokens.size()) {
      int32_t diff_idx = ParseInt(tokens[idx++], 1);
      if (world_ver < Version(1, 9, 3, 3)) diff_idx--;
      io::Settings::SetIdx("diff",
                           static_cast<size_t>(std::clamp(diff_idx, 0, 2)));
    }
    if (idx < tokens.size()) {
      gs.air_wizard_beaten = ParseBool(tokens[idx++]);
    }
    if (world_ver >= Version(2, 2, 0, 1)) {
      if (idx < tokens.size()) io::Settings::Set("quests", ParseBool(tokens[idx++]));
      if (idx < tokens.size())
        io::Settings::Set("tutorials", ParseBool(tokens[idx++]));
    }
  } else {
    if (game_lines.size() > 1 && !game_lines[1].empty()) {
      io::Settings::Set("mode", game_lines[1]);
    }
    if (game_lines.size() > 2) gs.score_time = ParseInt(game_lines[2], 0);
    if (game_lines.size() > 3) {
      const int32_t saved_tod = ParseInt(game_lines[3], 0);
      if (saved_tod >= game::GameState::kDayLength) {
        gs.game_time = 0;
        gs.past_day1 = true;
      } else {
        gs.SetGameTime(std::max<int32_t>(0, saved_tod));
      }
    }
    if (game_lines.size() > 4 && !game_lines[4].empty()) {
      io::Settings::Set("diff", game_lines[4]);
    }
    if (game_lines.size() > 5) gs.air_wizard_beaten = ParseBool(game_lines[5]);
    if (game_lines.size() > 6)
      gs.obsidian_knight_beaten = ParseBool(game_lines[6]);
    if (game_lines.size() > 7)
      gs.obsidian_knight_summoned = ParseBool(game_lines[7]);

    if (game_lines.size() > 8) {
      saved_lvl_idx = std::clamp(ParseInt(game_lines[8], 3), 0, 5);
    }
    if (game_lines.size() > 9) {
      gs.tick_count = std::max<int32_t>(0, ParseInt(game_lines[9], 0));
      if (gs.tick_count > 65000) gs.past_day1 = true;
    }
    if (game_lines.size() > 10) {
      gs.past_day1 = gs.past_day1 || ParseBool(game_lines[10]);
    }
  }

  // 2. Levels (Level0..Level5) matching World::kIdxToDepth.
  report("minicraft.displays.loading.message.levels");
  static constexpr std::array<int32_t, 6> kDepths = {-3, -2, -1, 0, 1, -4};
  for (int32_t i = 0; i < 6; ++i) {
    const std::string lvl_dir =
        Save::JoinWorldPath(ctx.world_dir, "Level" + std::to_string(i));
    const std::string java_lvl_file =
        Save::JoinWorldPath(ctx.world_dir, "Level" + std::to_string(i) + ".miniplussave");
    int32_t w = 128;
    int32_t h = 128;
    int64_t seed = 0;
    int32_t depth = kDepths[i];
    std::vector<std::tuple<int32_t, int32_t, int32_t>> chunk_specs;

    std::string idx_text;
    std::string java_lvl_text;
    if (!is_java_save &&
        fs.ReadTextFile(Save::JoinWorldPath(lvl_dir, "index.miniplussave"),
                        &idx_text)) {
      auto idx_lines = SplitLines(idx_text);
      if (idx_lines.size() >= 4) {
        w = ParseInt(idx_lines[0], 128);
        h = ParseInt(idx_lines[1], 128);
        seed = ParseInt64(idx_lines[2], 0);
        depth = ParseInt(idx_lines[3], kDepths[i]);
      }
      for (size_t li = 4; li < idx_lines.size(); ++li) {
        auto parts = SplitSv(idx_lines[li], ',');
        if (parts.size() >= 2) {
          int32_t cx = ParseInt(parts[0], 0);
          int32_t cy = ParseInt(parts[1], 0);
          int32_t st = parts.size() >= 3
                           ? ParseInt(parts[2], level::Chunk::kStageFinished)
                           : level::Chunk::kStageFinished;
          chunk_specs.emplace_back(cx, cy, st);
        }
      }
    } else if (fs.ReadTextFile(java_lvl_file, &java_lvl_text)) {
      auto tiles_tokens = SplitUnwrappedCommas(java_lvl_text);
      if (tiles_tokens.size() >= 4) {
        w = std::clamp(ParseInt(tiles_tokens[0], 128), 16, 1024);
        h = std::clamp(ParseInt(tiles_tokens[1], 128), 16, 1024);
        const bool has_seed = (world_ver >= Version(2, 0, 7, 2));
        seed = has_seed ? ParseInt64(tiles_tokens[2], 0) : 0;
        const size_t header_len = has_seed ? 4 : 3;
        if (has_seed) {
          depth = ParseInt(tiles_tokens[3], kDepths[i]);
        }
        io::Settings::Set("size", w);

        std::string data_text;
        std::vector<std::string> data_tokens;
        if (fs.ReadTextFile(Save::JoinWorldPath(ctx.world_dir,
                                                "Level" + std::to_string(i) +
                                                    "data.miniplussave"),
                            &data_text)) {
          data_tokens = SplitUnwrappedCommas(data_text);
        }

        auto lvl = std::make_unique<level::Level>(
            w, h, seed, depth, nullptr, /*skip_gen=*/true);
        auto& cm = lvl->GetChunkManager();
        for (int32_t x = 0; x < w; ++x) {
          for (int32_t y = 0; y < h; ++y) {
            const size_t tidx = static_cast<size_t>(y + x * h);
            if (header_len + tidx >= tiles_tokens.size()) continue;
            int16_t tdata = tidx < data_tokens.size()
                                ? static_cast<int16_t>(ParseInt(data_tokens[tidx], 0))
                                : 0;
            std::string tname =
                MapJavaTileName(tiles_tokens[header_len + tidx], world_ver, &tdata);
            const level::Tile* tile = nullptr;
            if (tname.rfind("TORCH ", 0) == 0 || tname.rfind("Torch ", 0) == 0) {
              const level::Tile* bg = level::Tiles::Get(std::string_view(tname).substr(6));
              tile = level::Tiles::Get("Torch");
              tdata = bg ? static_cast<int16_t>(bg->id) : 0;
            } else {
              tile = level::Tiles::Get(tname);
            }
            if (tile) {
              cm.SetTile(x, y, tile->id, tdata);
            }
          }
        }
        const int32_t max_cx = level::ChunkManager::WorldToChunk(std::max(0, w - 1));
        const int32_t max_cy = level::ChunkManager::WorldToChunk(std::max(0, h - 1));
        for (int32_t cy = 0; cy <= max_cy; ++cy) {
          for (int32_t cx = 0; cx <= max_cx; ++cx) {
            cm.SetChunkStage(cx, cy, level::Chunk::kStageFinished);
          }
        }
        (*ctx.levels)[i] = std::move(lvl);
        continue;
      }
    }

    auto lvl = std::make_unique<level::Level>(
        w, h, seed, depth, nullptr, /*skip_gen=*/!chunk_specs.empty());

    auto& cm = lvl->GetChunkManager();
    for (const auto& [cx, cy, stage] : chunk_specs) {
      const std::string suffix =
          std::to_string(cx) + "." + std::to_string(cy) + ".miniplussave";
      std::string t_text;
      std::string d_text;
      if (!fs.ReadTextFile(Save::JoinWorldPath(lvl_dir, "t." + suffix),
                           &t_text)) {
        continue;
      }
      fs.ReadTextFile(Save::JoinWorldPath(lvl_dir, "d." + suffix), &d_text);

      auto chunk = std::make_unique<level::Chunk>();
      chunk->chunk_x = cx;
      chunk->chunk_y = cy;
      chunk->stage = stage;
      ParseCsvArray(t_text, &chunk->tile_ids);
      if (!d_text.empty()) {
        ParseCsvArray(d_text, &chunk->tile_data);
      }
      chunk->dirty = false;
      cm.InsertLoadedChunk(std::move(chunk));
      cm.SetChunkStage(cx, cy, stage);
    }

    (*ctx.levels)[i] = std::move(lvl);
  }

  // Link parent levels (`parent_level` points to level above, depth + 1)
  // Indices: 0(-3)->1(-2)->2(-1)->3(0)->4(1)->null; 5(-4)->0(-3).
  for (int32_t i = 0; i < 4; ++i) {
    if ((*ctx.levels)[i]) {
      (*ctx.levels)[i]->parent_level = (*ctx.levels)[i + 1].get();
    }
  }
  if ((*ctx.levels)[5]) {
    (*ctx.levels)[5]->parent_level = (*ctx.levels)[0].get();
  }

  // 3. Player.miniplussave
  report("minicraft.displays.loading.message.entities");
  auto player = std::make_shared<entity::Player>(*ctx.player, nullptr);
  std::string active_item_data;
  {
    std::string p_text;
    if (fs.ReadTextFile(Save::JoinWorldPath(ctx.world_dir, "Player.miniplussave"),
                        &p_text)) {
      auto pl = SplitLines(p_text);
      if (is_java_save ||
          (pl.size() == 1 && p_text.find(',') != std::string::npos)) {
        auto pf = SplitUnwrappedCommas(p_text);
        if (pf.size() >= 7) {
          size_t idx = 0;
          auto next_field = [&]() -> std::string {
            return idx < pf.size() ? pf[idx++] : std::string{};
          };
          player->x = ParseInt(next_field(), player->spawnx);
          player->y = ParseInt(next_field(), player->spawny);
          player->spawnx = ParseInt(next_field(), player->x);
          player->spawny = ParseInt(next_field(), player->y);
          player->health = ParseInt(next_field(), entity::Player::kMaxHealth);
          if (world_ver >= Version(2, 2, 0, 3)) {
            int32_t extra_health = ParseInt(next_field(), 0);
            if (extra_health > 0) {
              player->max_health += extra_health;
            }
          }
          if (world_ver >= Version(2, 0, 4, 7)) {
            player->hunger = ParseInt(next_field(), entity::Player::kMaxHunger);
          }
          player->armor = ParseInt(next_field(), 0);
          if (world_ver >= Version(2, 0, 5, 5)) {
            player->armor_damage_buffer = ParseInt(next_field(), 0);
            std::string armor_str = next_field();
            if (!armor_str.empty() && armor_str != "NULL" &&
                armor_str != "None") {
              auto armor_it =
                  item::Items::Get(SubOldName(armor_str, world_ver), true);
              if (auto* arm = dynamic_cast<item::ArmorItem*>(armor_it.get())) {
                armor_it.release();
                player->cur_armor.reset(arm);
              }
            }
          }
          player->SetScore(ParseInt(next_field(), 0));
          if (world_ver < Version(2, 0, 4, 7)) {
            next_field();
          }
          saved_lvl_idx =
              std::clamp(ParseInt(next_field(), saved_lvl_idx), 0, 5);
          if (world_ver < Version(2, 0, 4, 8)) {
            next_field();
          }
          std::string effects_str = next_field();
          if (effects_str.rfind("PotionEffects[", 0) == 0 &&
              effects_str.back() == ']') {
            std::string_view inner(effects_str.data() + 14,
                                   effects_str.size() - 15);
            auto parse_effect_id = [](std::string_view name) -> int32_t {
              if (name == "SPEED") return 0;
              if (name == "LIGHT") return 1;
              if (name == "SWIM") return 2;
              if (name == "ENERGY") return 3;
              if (name == "REGEN") return 4;
              if (name == "HEALTH") return 5;
              if (name == "TIME") return 6;
              if (name == "LAVA") return 7;
              if (name == "SHIELD") return 8;
              if (name == "HASTE") return 9;
              return ParseInt(name, -1);
            };
            for (auto eff_sv : SplitSv(inner, ':')) {
              if (eff_sv.empty()) continue;
              size_t semi = eff_sv.find(';');
              if (semi == std::string_view::npos) continue;
              int32_t eid = parse_effect_id(eff_sv.substr(0, semi));
              int32_t dur = ParseInt(eff_sv.substr(semi + 1), 0);
              if (eid >= 0 && dur > 0) {
                player->potion_effects[eid] = dur;
              }
            }
          }
          if (world_ver >= Version(2, 0, 6, 4)) {
            std::string shirt_str = next_field();
            if (!shirt_str.empty()) {
              player->shirt_color = ParseInt(shirt_str, player->shirt_color);
            }
          }
          if (world_ver < Version(2, 1, 0, 0)) {
            next_field();
          }
          if (world_ver >= Version(2, 2, 0, 3)) {
            std::string rec_str = next_field();
            if (ctx.unlocked_recipes && !rec_str.empty()) {
              try {
                auto obj = nlohmann::json::parse(rec_str);
                if (obj.is_object()) {
                  for (auto& [key, costs_json] : obj.items()) {
                    if (!costs_json.is_array()) continue;
                    std::vector<std::string> costs;
                    for (const auto& c : costs_json) {
                      if (c.is_string()) costs.push_back(c.get<std::string>());
                    }
                    if (world_ver <= Version(2, 2, 0, 6)) {
                      auto ieq = [](std::string_view a, std::string_view b) {
                        return a.size() == b.size() &&
                               std::equal(a.begin(), a.end(), b.begin(),
                                          [](char x, char y) {
                                            return std::tolower(static_cast<unsigned char>(x)) ==
                                                   std::tolower(static_cast<unsigned char>(y));
                                          });
                      };
                      if (ieq(key, "iron_1") && costs.size() == 2 &&
                          ieq(costs[0], "iron Ore_4") && ieq(costs[1], "coal_1")) {
                        continue;
                      }
                      if (ieq(key, "gold_1") && costs.size() == 2 &&
                          ieq(costs[0], "gold Ore_4") && ieq(costs[1], "coal_1")) {
                        continue;
                      }
                    }
                    ctx.unlocked_recipes->emplace_back(key, costs);
                  }
                }
              } catch (...) {
              }
            }
          }
        }
      } else {
        auto pl = SplitLines(p_text);
        if (pl.size() >= 15) {
          player->x = ParseInt(pl[0], player->spawnx);
          player->y = ParseInt(pl[1], player->spawny);
          player->spawnx = ParseInt(pl[2], player->x);
          player->spawny = ParseInt(pl[3], player->y);
          player->spawn_depth = ParseInt(pl[4], 0);
          player->health = ParseInt(pl[5], entity::Player::kMaxHealth);
          player->hunger = ParseInt(pl[6], entity::Player::kMaxHunger);
          player->stamina = ParseInt(pl[7], entity::Player::kMaxStamina);
          player->armor = ParseInt(pl[8], 0);
          player->armor_damage_buffer = ParseInt(pl[9], 0);
          if (pl[10] != "None" && !pl[10].empty()) {
            auto armor_it = item::Items::Get(SubOldName(pl[10], world_ver), true);
            if (auto* arm = dynamic_cast<item::ArmorItem*>(armor_it.get())) {
              armor_it.release();
              player->cur_armor.reset(arm);
            }
          }
          player->shirt_color = ParseInt(pl[11], player->shirt_color);
          player->skinon = ParseBool(pl[12]);
          player->SetScore(ParseInt(pl[13], 0));
          saved_lvl_idx = std::clamp(ParseInt(pl[14], saved_lvl_idx), 0, 5);
          if (pl.size() > 15 && pl[15] != "None" && !pl[15].empty()) {
            for (auto eff_sv : SplitSv(pl[15], ':')) {
              size_t semi = eff_sv.find(';');
              if (semi == std::string_view::npos) continue;
              int32_t eid = ParseInt(eff_sv.substr(0, semi), -1);
              int32_t dur = ParseInt(eff_sv.substr(semi + 1), 0);
              if (eid >= 0 && dur > 0) {
                player->potion_effects[eid] = dur;
              }
            }
          }
          if (pl.size() > 16) {
            player->max_health =
                std::clamp(ParseInt(pl[16], entity::Player::kMaxHealth),
                           entity::Player::kMaxHealth,
                           entity::Player::kMaxTotalHealth);
          }
          if (pl.size() > 17 && ctx.unlocked_recipes && !pl[17].empty()) {
            try {
              auto obj = nlohmann::json::parse(pl[17]);
              if (obj.is_object()) {
                for (auto& [key, costs_json] : obj.items()) {
                  if (!costs_json.is_array()) continue;
                  std::vector<std::string> costs;
                  for (const auto& c : costs_json) {
                    if (c.is_string()) costs.push_back(c.get<std::string>());
                  }
                  ctx.unlocked_recipes->emplace_back(key, costs);
                }
              }
            } catch (...) {
            }
          }
        }
      }
    }
  }

  // 4. Inventory.miniplussave
  report("minicraft.displays.loading.message.entities");
  std::unique_ptr<entity::DeathChest> overflow_chest =
      std::make_unique<entity::DeathChest>();
  {
    std::string inv_text;
    if (fs.ReadTextFile(
            Save::JoinWorldPath(ctx.world_dir, "Inventory.miniplussave"),
            &inv_text)) {
      auto lines = SplitLines(inv_text);
      if (is_java_save ||
          (lines.size() == 1 && inv_text.find(',') != std::string::npos)) {
        lines = SplitUnwrappedCommas(inv_text);
      }
      std::vector<std::string> regular_lines;
      regular_lines.reserve(lines.size());
      for (const auto& l : lines) {
        if (l.empty()) continue;
        if (l.rfind("ACTIVE:", 0) == 0) {
          active_item_data = l.substr(7);
        } else {
          regular_lines.push_back(l);
        }
      }
      LoadInventory(regular_lines, player->GetInventory(), world_ver,
                    overflow_chest.get());
      if (!active_item_data.empty()) {
        auto act = item::Items::Get(SubOldName(active_item_data, world_ver),
                                    true);
        if (act) {
          player->active_item = std::shared_ptr<item::Item>(std::move(act));
        }
      }
    }
  }

  // Place player into saved level
  if (ctx.current_level_idx) *ctx.current_level_idx = saved_lvl_idx;
  if ((*ctx.levels)[saved_lvl_idx]) {
    (*ctx.levels)[saved_lvl_idx]->Add(player, player->x, player->y, false);
  }
  *ctx.player = player;

  // 5. Entities.miniplussave
  report("minicraft.displays.loading.message.entities");
  {
    std::string ent_text;
    if (fs.ReadTextFile(
            Save::JoinWorldPath(ctx.world_dir, "Entities.miniplussave"),
            &ent_text)) {
      auto lines = SplitLines(ent_text);
      if (is_java_save ||
          (lines.size() == 1 && ent_text.find(',') != std::string::npos)) {
        lines = SplitUnwrappedCommas(ent_text);
      }
      for (const auto& line : lines) {
        if (line.empty() || line.rfind("Player", 0) == 0) continue;
        LoadEntity(line, world_ver, ctx);
      }
    }
  }

  for (int32_t i = 0; i < 6; ++i) {
    if ((*ctx.levels)[i]) {
      (*ctx.levels)[i]->FlushPendingEntities();
      (*ctx.levels)[i]->CheckChestCount();
      (*ctx.levels)[i]->CheckAirWizard();
    }
  }

  // 6. advancements.json
  {
    util::AdvancementSystem::Instance().ResetAll();
    std::string adv_text;
    if (fs.ReadTextFile(Save::JoinWorldPath(ctx.world_dir, "advancements.json"),
                        &adv_text)) {
      util::AdvancementSystem::Instance().LoadAdvancementsJson(adv_text);
    }
  }

  // 7. signs.json
  if (ctx.signs) {
    ctx.signs->clear();
    std::string signs_text;
    if (fs.ReadTextFile(Save::JoinWorldPath(ctx.world_dir, "signs.json"),
                        &signs_text)) {
      try {
        auto j = nlohmann::json::parse(signs_text);
        const nlohmann::json* signs_arr = nullptr;
        if (j.is_array()) {
          signs_arr = &j;
        } else if (j.is_object() && j.contains("signs") &&
                   j["signs"].is_array()) {
          signs_arr = &j["signs"];
        }
        if (signs_arr != nullptr) {
          for (const auto& entry : *signs_arr) {
            SignCoord c;
            c.level = entry.value("level", 0);
            c.x = entry.value("x", 0);
            c.y = entry.value("y", 0);
            std::vector<std::string> lines;
            if (entry.contains("lines") && entry["lines"].is_array()) {
              for (const auto& l : entry["lines"]) {
                if (l.is_string()) lines.push_back(l.get<std::string>());
              }
            }
            (*ctx.signs)[c] = std::move(lines);
          }
        }
      } catch (const std::exception& e) {
        util::log::kSaveLoad.Error(std::string("Failed parsing signs.json: ") +
                                   e.what());
      }
    }
  }

  gs.has_world = true;
  return true;
}

}  // namespace minicraft::saveload
