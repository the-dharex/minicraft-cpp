#ifndef MINICRAFT_CORE_SAVELOAD_SAVE_H_
#define MINICRAFT_CORE_SAVELOAD_SAVE_H_

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace minicraft {
namespace entity {
class Entity;
class Player;
}  // namespace entity
namespace io {
class InputHandler;
}  // namespace io
namespace item {
class Inventory;
class Recipe;
}  // namespace item
namespace level {
class Level;
}  // namespace level
namespace platform {
class IFileSystem;
}  // namespace platform
}  // namespace minicraft

namespace minicraft::saveload {

struct SignCoord {
  int32_t level = 0;
  int32_t x = 0;
  int32_t y = 0;

  bool operator<(const SignCoord& other) const {
    return std::tie(level, x, y) < std::tie(other.level, other.x, other.y);
  }
};

using SignMap = std::map<SignCoord, std::vector<std::string>>;

// Decoupled snapshot/target of world state needed by Save and Load without
// circular dependency on Game/World/Screen headers.
struct WorldSaveContext {
  std::string world_name;
  std::string world_dir;
  std::array<std::unique_ptr<level::Level>, 6>* levels = nullptr;
  int32_t* current_level_idx = nullptr;
  std::shared_ptr<entity::Player>* player = nullptr;
  SignMap* signs = nullptr;
  std::vector<item::Recipe>* unlocked_recipes = nullptr;
  std::function<void(const std::string&)> on_progress;
  std::function<std::string(std::string_view)> get_sign_text;
};

struct WorldFileEntry {
  std::string path;
  std::string content;
  std::vector<uint8_t> binary_content;
  bool is_binary = false;
  bool critical = false;
};

// Self-contained immutable snapshot of serialized world save data so disk I/O
// can execute safely on a background thread while gameplay resumes.
struct WorldSaveSnapshot {
  std::string world_dir;
  std::vector<uint8_t> binary_world;
  std::vector<std::string> directories;
  std::vector<WorldFileEntry> files;
};

class Save {
 public:
  // Serializes a full world folder into an in-memory snapshot.
  static bool CaptureWorldSnapshot(WorldSaveContext& ctx,
                                   WorldSaveSnapshot* out);

  // Writes a captured world snapshot to `fs` and optionally notifies listeners.
  static bool WriteWorldSnapshot(
      const platform::IFileSystem& fs, const WorldSaveSnapshot& snapshot,
      bool notify_on_complete = true,
      const std::function<void(float)>& on_percent = {});

  // Saves a full world folder (Game, Level0..5 chunks, Player, Inventory,
  // Entities, advancements.json, signs.json).
  static bool SaveWorld(const platform::IFileSystem& fs,
                        WorldSaveContext& ctx);

  // Saves global user preferences (`Preferences.json`) and unlocks
  // (`Unlocks.json`) in `game_dir`.
  static bool SavePrefs(const platform::IFileSystem& fs,
                        std::string_view game_dir,
                        const io::InputHandler* input = nullptr,
                        const std::vector<std::string>* resource_packs = nullptr);

  // Serializes a single entity to the Minicraft text format (`Name[...]`).
  static std::string WriteEntity(const entity::Entity& e, int32_t level_idx,
                                 bool is_local_save = true);

  // Serializes an inventory to comma/newline line items.
  static std::vector<std::string> WriteInventory(const item::Inventory& inv,
                                                 const entity::Player* player = nullptr);

  static std::string JoinWorldPath(std::string_view base, std::string_view sub);
};

}  // namespace minicraft::saveload

#endif  // MINICRAFT_CORE_SAVELOAD_SAVE_H_
