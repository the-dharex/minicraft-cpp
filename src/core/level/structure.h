#ifndef MINICRAFT_CORE_LEVEL_STRUCTURE_H_
#define MINICRAFT_CORE_LEVEL_STRUCTURE_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace minicraft::level {

class Level;

// Reusable blueprint for multi-tile world structures (dungeon lock, mini
// dungeon, village house, obsidian boss arena, lava pool, etc.).
class Structure {
 public:
  struct TileOffset {
    int32_t dx = 0;
    int32_t dy = 0;
    uint16_t tile_id = 0;
    int16_t data = 0;
  };

  struct FurnitureSpawn {
    int32_t dx = 0;
    int32_t dy = 0;
    std::string kind;  // e.g. "Spawner:Zombie", "DungeonChest", "Chest:villagehouse"
  };

  Structure() = default;

  void SetData(const std::string& keys, const std::string& tiles);
  void AddTile(int32_t dx, int32_t dy, uint16_t tile_id, int16_t data = 0);
  void AddFurniture(int32_t dx, int32_t dy, std::string kind);

  void Draw(Level& level, int32_t cx, int32_t cy) const;
  void DrawTilesOnly(Level& level, int32_t cx, int32_t cy) const;
  void DrawWithFloor(Level& level, int32_t cx, int32_t cy,
                     uint16_t floor_tile_id) const;

  static const Structure& DungeonGate();
  static const Structure& DungeonLock();
  static const Structure& DungeonBossRoom();
  static const Structure& DungeonSpawner();
  static const Structure& LavaPool();
  static const Structure& OrnateLavaPool();
  static const Structure& DungeonGarden();
  static const Structure& DungeonChestRoom();
  static const Structure& MobDungeonCenter();
  static const Structure& MobDungeonNorth();
  static const Structure& MobDungeonSouth();
  static const Structure& MobDungeonEast();
  static const Structure& MobDungeonWest();
  static const Structure& AirWizardHouse();
  static const Structure& VillageHouseNormal();
  static const Structure& VillageHouseTwoDoor();
  static const Structure& VillageRuinedOverlay1();
  static const Structure& VillageRuinedOverlay2();

  // Compatibility aliases
  static const Structure& MiniDungeon();
  static const Structure& VillageHouse();
  static const Structure& ObsidianDungeonRoom();
  static const Structure& SkyTower();

 private:
  std::vector<TileOffset> tiles_;
  std::vector<FurnitureSpawn> furniture_;
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_STRUCTURE_H_
