#ifndef MINICRAFT_CORE_LEVEL_CHUNK_MANAGER_H_
#define MINICRAFT_CORE_LEVEL_CHUNK_MANAGER_H_

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/level/biome.h"
#include "core/level/level_noise.h"

namespace minicraft::level {

class Tile;

struct Chunk {
  static constexpr int32_t kSize = 64;
  static constexpr int32_t kArea = kSize * kSize;
  static constexpr int32_t kStageTerrain = 0;
  static constexpr int32_t kStageUnfinishedStairs = 1;
  static constexpr int32_t kStageFinished = 2;

  int32_t chunk_x = 0;
  int32_t chunk_y = 0;
  int32_t stage = kStageTerrain;
  bool dirty = false;

  std::array<uint16_t, kArea> tile_ids{};
  std::array<int16_t, kArea> tile_data{};
  std::array<uint8_t, kArea> biome_ids{};

  // Retained only while stage < kStageFinished to save RAM.
  std::unique_ptr<LevelNoise> noise;
  std::vector<std::pair<int32_t, int32_t>> pending_ornate_pools;

  uint16_t GetTileId(int32_t lx, int32_t ly) const {
    return tile_ids[(ly & 63) * kSize + (lx & 63)];
  }
  void SetTileId(int32_t lx, int32_t ly, uint16_t id) {
    tile_ids[(ly & 63) * kSize + (lx & 63)] = id;
    dirty = true;
  }
  int16_t GetData(int32_t lx, int32_t ly) const {
    return tile_data[(ly & 63) * kSize + (lx & 63)];
  }
  void SetData(int32_t lx, int32_t ly, int16_t data) {
    tile_data[(ly & 63) * kSize + (lx & 63)] = data;
    dirty = true;
  }
  uint8_t GetBiomeId(int32_t lx, int32_t ly) const {
    return biome_ids[(ly & 63) * kSize + (lx & 63)];
  }
  void SetBiomeId(int32_t lx, int32_t ly, uint8_t bid) {
    biome_ids[(ly & 63) * kSize + (lx & 63)] = bid;
  }
};

// Manages chunk storage, procedural terrain population, and an MRU fast-path
// cache so hot tile queries avoid hash lookups.
class ChunkManager {
 public:
  ChunkManager(int64_t seed, int32_t depth);

  static uint64_t PackKey(int32_t cx, int32_t cy) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint32_t>(cy);
  }
  static int32_t WorldToChunk(int32_t w) { return w >> 6; }
  static int32_t WorldToLocal(int32_t w) { return w & 63; }

  Chunk* GetChunk(int32_t cx, int32_t cy);
  const Chunk* FindChunk(int32_t cx, int32_t cy) const;
  Chunk* FindChunkMutable(int32_t cx, int32_t cy);
  Chunk* InsertLoadedChunk(std::unique_ptr<Chunk> chunk);

  bool HasChunk(int32_t cx, int32_t cy) const;
  int32_t GetChunkStage(int32_t cx, int32_t cy) const;
  void SetChunkStage(int32_t cx, int32_t cy, int32_t stage);

  const Tile* GetTile(int32_t wx, int32_t wy);
  const Tile* GetTileConst(int32_t wx, int32_t wy) const;
  void SetTile(int32_t wx, int32_t wy, uint16_t tile_id, int16_t data = 0);

  int16_t GetData(int32_t wx, int32_t wy);
  int16_t GetDataConst(int32_t wx, int32_t wy) const;
  void SetData(int32_t wx, int32_t wy, int16_t data);

  BiomeId GetBiome(int32_t wx, int32_t wy);

  const std::unordered_map<uint64_t, std::unique_ptr<Chunk>>& Chunks() const {
    return chunks_;
  }
  std::unordered_map<uint64_t, std::unique_ptr<Chunk>>& ChunksMutable() {
    return chunks_;
  }

  void ClearCache();

 private:
  Chunk* GenerateChunk(int32_t cx, int32_t cy);

  int64_t seed_;
  int32_t depth_;
  std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;

  // Small 4-entry direct-mapped cache for recent chunk lookups
  struct CacheSlot {
    int32_t cx = 0x7FFFFFFF;
    int32_t cy = 0x7FFFFFFF;
    Chunk* ptr = nullptr;
  };
  mutable std::array<CacheSlot, 4> cache_{};
  mutable uint32_t cache_cursor_ = 0;
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_CHUNK_MANAGER_H_
