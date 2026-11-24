#ifndef MINICRAFT_CORE_SAVELOAD_LEGACY_LOAD_H_
#define MINICRAFT_CORE_SAVELOAD_LEGACY_LOAD_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/saveload/load.h"

namespace minicraft::saveload {

// Isolated loader for legacy Java Minicraft+ (`.miniplussave` comma-separated
// and older text formats). Invoked by `Load` only when a world folder lacks a
// native binary `world.mcbin` save and contains `Game.miniplussave`.
class LegacyLoad {
 public:
  static bool IsLegacyWorldDir(const platform::IFileSystem& fs,
                               std::string_view world_dir);

  static bool ReadWorldMetadata(const platform::IFileSystem& fs,
                                std::string_view world_dir,
                                WorldMetadata* out);

  static bool LoadWorld(const platform::IFileSystem& fs,
                        WorldSaveContext& ctx);

  static std::shared_ptr<entity::Entity> LoadEntity(
      std::string_view entity_data, const Version& world_ver,
      WorldSaveContext& ctx, bool is_java_save = false);

  static void LoadInventory(const std::vector<std::string>& lines,
                            item::Inventory& inv, const Version& world_ver,
                            entity::DeathChest* overflow_chest = nullptr);

  static std::string SubOldName(std::string_view name,
                                const Version& world_ver);

  static std::string NormalizeBedColor(std::string_view prefix);
};

}  // namespace minicraft::saveload

#endif  // MINICRAFT_CORE_SAVELOAD_LEGACY_LOAD_H_
