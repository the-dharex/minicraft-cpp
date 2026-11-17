#ifndef MINICRAFT_CORE_SAVELOAD_LOAD_H_
#define MINICRAFT_CORE_SAVELOAD_LOAD_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/saveload/save.h"
#include "core/saveload/version.h"

namespace minicraft {
namespace entity {
class DeathChest;
class Entity;
class Player;
}  // namespace entity
namespace io {
class InputHandler;
}  // namespace io
namespace item {
class Inventory;
}  // namespace item
namespace platform {
class IFileSystem;
}  // namespace platform
}  // namespace minicraft

namespace minicraft::saveload {

struct WorldMetadata {
  std::string folder_name;
  std::string world_dir;
  Version version{Version::kCurrentVersion, false};
  std::string mode;
  int32_t score = 0;
  int32_t game_time = 0;
  bool valid = false;
};

class Load {
 public:
  // Scans `<saves_dir>` for saved world folders and reads summary metadata.
  static std::vector<WorldMetadata> ListWorlds(const platform::IFileSystem& fs,
                                               std::string_view saves_dir);

  // Loads global `Preferences.json` and `Unlocks.json` from `game_dir`.
  static bool LoadPrefs(const platform::IFileSystem& fs,
                        std::string_view game_dir,
                        io::InputHandler* input = nullptr,
                        std::vector<std::string>* out_resource_packs = nullptr);

  // Loads a complete world into `ctx`.
  static bool LoadWorld(const platform::IFileSystem& fs,
                        WorldSaveContext& ctx);

  // Parses and spawns a single entity string (e.g. `Zombie[100:200:10:1:4]`).
  static std::shared_ptr<entity::Entity> LoadEntity(
      std::string_view entity_data, const Version& world_ver,
      WorldSaveContext& ctx);

  // Parses inventory lines into `inv`.
  static void LoadInventory(const std::vector<std::string>& lines,
                            item::Inventory& inv, const Version& world_ver,
                            entity::DeathChest* overflow_chest = nullptr);

  // Upgrades legacy Minicraft item/entity names from older save versions.
  static std::string SubOldName(std::string_view name, const Version& world_ver);
};

}  // namespace minicraft::saveload

#endif  // MINICRAFT_CORE_SAVELOAD_LOAD_H_
