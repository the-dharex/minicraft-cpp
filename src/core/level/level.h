#ifndef MINICRAFT_CORE_LEVEL_LEVEL_H_
#define MINICRAFT_CORE_LEVEL_LEVEL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/gfx/geometry.h"
#include "core/level/biome.h"
#include "core/level/chunk_manager.h"

namespace minicraft {
namespace gfx {
class Screen;
}
namespace entity {
class Entity;
class Player;
}
namespace item {
class Item;
}
}  // namespace minicraft

namespace minicraft::level {

class Tile;

enum class TreeType : uint8_t {
  kOak = 0,
  kSpruce = 1,
  kBirch = 2,
  kAsh = 3,
  kAspen = 4,
  kFir = 5,
  kWillow = 6,
};

class Level {
 public:
  Level(int32_t w, int32_t h, int64_t seed, int32_t depth,
        Level* parent_level = nullptr, bool skip_gen = false);
  ~Level();

  int32_t w = 128;
  int32_t h = 128;
  int64_t seed = 0;
  int32_t depth = 0;
  Level* parent_level = nullptr;

  int32_t mob_count = 0;
  int32_t max_mob_count = 0;
  int32_t monster_density = 16;
  int32_t chest_count = 0;

  // Tile access
  const Tile* GetTile(int32_t x, int32_t y) const;
  void SetTile(int32_t x, int32_t y, uint16_t tile_id, int16_t data = 0);
  void SetTile(int32_t x, int32_t y, const Tile* tile, int16_t data = 0);

  int16_t GetData(int32_t x, int32_t y) const;
  void SetData(int32_t x, int32_t y, int16_t data);

  BiomeId GetBiome(int32_t x, int32_t y) const;

  ChunkManager& GetChunkManager() { return chunk_manager_; }
  const ChunkManager& GetChunkManager() const { return chunk_manager_; }

  // Entity management
  void Add(std::shared_ptr<entity::Entity> entity);
  void Add(std::shared_ptr<entity::Entity> entity, int32_t x, int32_t y,
           bool tile_coords = false);
  void Remove(entity::Entity* entity);

  std::shared_ptr<entity::Entity> GetEntity(int32_t eid) const;
  const std::vector<std::shared_ptr<entity::Entity>>& Entities() const {
    return entities_;
  }
  std::vector<std::shared_ptr<entity::Entity>>& EntitiesMutable() {
    return entities_;
  }
  const std::vector<std::shared_ptr<entity::Player>>& Players() const {
    return players_;
  }

  void GetEntitiesInRect(const gfx::Rectangle& rect,
                         std::vector<entity::Entity*>* out) const;
  std::vector<entity::Entity*> GetEntitiesInRect(
      const gfx::Rectangle& rect) const;
  std::vector<entity::Entity*> GetEntitiesInTiles(int32_t xt, int32_t yt,
                                                  int32_t radius) const;
  std::vector<entity::Entity*> GetEntitiesInTiles(int32_t xt0, int32_t yt0,
                                                  int32_t xt1,
                                                  int32_t yt1) const;
  entity::Player* GetClosestPlayer(int32_t x, int32_t y) const;

  std::vector<gfx::Point> GetMatchingTiles(uint16_t tile_id, int32_t xt,
                                           int32_t yt, int32_t radius) const;
  std::vector<gfx::Point> GetAreaTilePositions(int32_t xt, int32_t yt,
                                               int32_t radius) const;

  // Item drops
  void DropItem(int32_t x, int32_t y, std::unique_ptr<item::Item> item);
  void PurgeRemovedEntities();
  void FlushPendingEntities();
  void DropItem(int32_t x, int32_t y, std::string_view item_name);
  void DropItem(int32_t x, int32_t y, int32_t min_count, int32_t max_count,
                std::string_view item_name);

  // Simulation & rendering
  void Tick(bool full_tick);
  void RenderBackground(gfx::Screen* screen, int32_t x_scroll,
                        int32_t y_scroll) const;
  void RenderSprites(gfx::Screen* screen, int32_t x_scroll,
                     int32_t y_scroll) const;
  void RenderLight(gfx::Screen* screen, int32_t x_scroll, int32_t y_scroll,
                   int32_t brightness = 8) const;

  // Generation / structure hooks
  void SpawnStructureFurniture(int32_t tx, int32_t ty, const std::string& kind);
  void PopulateChunk(int32_t cx, int32_t cy);
  void CheckChestCount(bool check = true);
  void CheckAirWizard(bool check = true);
  void LoadChunksAround(int32_t tile_x, int32_t tile_y);
  void LoadChunk(int32_t cx, int32_t cy);
  void RemoveAllEnemies();
  void RegenerateBossRoom();
  void EnsureBossRoomGenerated();
  void SetBossRoomGenerated(bool generated) {
    boss_room_generated_ = generated;
  }
  bool BossRoomGenerated() const { return boss_room_generated_; }
  bool IsEntityOnTile(int32_t tx, int32_t ty) const;
  TreeType GetTreeType(int32_t x, int32_t y) const;
  void ClearEntities();

  // Sign / tile-entity coordinate helpers
  void RemoveSignIfNeeded(int32_t tx, int32_t ty) const;
  bool IsLight(int32_t tx, int32_t ty) const;
  void UpdateMobCap();

 private:
  void HandleDespawn();
  void TrySpawn();
  void GenerateTreeTypes();

  mutable ChunkManager chunk_manager_;
  std::vector<TreeType> tree_types_;
  std::vector<std::shared_ptr<entity::Entity>> entities_;
  std::vector<std::shared_ptr<entity::Entity>> pending_add_;
  std::vector<std::shared_ptr<entity::Player>> players_;

  // Scratch vectors reused across frames to avoid heap allocations
  mutable std::vector<entity::Entity*> render_scratch_;
  bool boss_room_generated_ = false;
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_LEVEL_H_
