#include "core/level/chunk_manager.h"

#include "core/level/tiles.h"
#include "core/util/random.h"

namespace minicraft::level {

ChunkManager::ChunkManager(int64_t seed, int32_t depth)
    : seed_(seed), depth_(depth) {
  Biomes::Init();
  Tiles::InitTiles();
}

void ChunkManager::ClearCache() {
  for (auto& slot : cache_) {
    slot.cx = 0x7FFFFFFF;
    slot.cy = 0x7FFFFFFF;
    slot.ptr = nullptr;
  }
}

const Chunk* ChunkManager::FindChunk(int32_t cx, int32_t cy) const {
  for (const auto& slot : cache_) {
    if (slot.ptr != nullptr && slot.cx == cx && slot.cy == cy) {
      return slot.ptr;
    }
  }
  auto it = chunks_.find(PackKey(cx, cy));
  if (it == chunks_.end()) return nullptr;
  Chunk* ptr = it->second.get();
  cache_[cache_cursor_ & 3] = {cx, cy, ptr};
  ++cache_cursor_;
  return ptr;
}

Chunk* ChunkManager::FindChunkMutable(int32_t cx, int32_t cy) {
  for (const auto& slot : cache_) {
    if (slot.ptr != nullptr && slot.cx == cx && slot.cy == cy) {
      return slot.ptr;
    }
  }
  auto it = chunks_.find(PackKey(cx, cy));
  if (it == chunks_.end()) return nullptr;
  Chunk* ptr = it->second.get();
  cache_[cache_cursor_ & 3] = {cx, cy, ptr};
  ++cache_cursor_;
  return ptr;
}

Chunk* ChunkManager::GetChunk(int32_t cx, int32_t cy) {
  if (Chunk* existing = FindChunkMutable(cx, cy)) {
    return existing;
  }
  return GenerateChunk(cx, cy);
}

Chunk* ChunkManager::InsertLoadedChunk(std::unique_ptr<Chunk> chunk) {
  if (!chunk) return nullptr;
  int32_t cx = chunk->chunk_x;
  int32_t cy = chunk->chunk_y;
  Chunk* raw = chunk.get();
  chunks_[PackKey(cx, cy)] = std::move(chunk);
  cache_[cache_cursor_ & 3] = {cx, cy, raw};
  ++cache_cursor_;
  return raw;
}

bool ChunkManager::HasChunk(int32_t cx, int32_t cy) const {
  return chunks_.find(PackKey(cx, cy)) != chunks_.end();
}

int32_t ChunkManager::GetChunkStage(int32_t cx, int32_t cy) const {
  const Chunk* c = FindChunk(cx, cy);
  return c ? c->stage : -1;
}

void ChunkManager::SetChunkStage(int32_t cx, int32_t cy, int32_t stage) {
  Chunk* c = GetChunk(cx, cy);
  if (!c) return;
  c->stage = stage;
  if (stage >= Chunk::kStageUnfinishedStairs) {
    // Drop multi-layer noise array to free ~320 KB per finalized chunk
    c->noise.reset();
  }
}

Chunk* ChunkManager::GenerateChunk(int32_t cx, int32_t cy) {
  auto chunk = std::make_unique<Chunk>();
  chunk->chunk_x = cx;
  chunk->chunk_y = cy;
  chunk->stage = Chunk::kStageTerrain;

  const int32_t x_off = cx * Chunk::kSize;
  const int32_t y_off = cy * Chunk::kSize;
  chunk->noise = std::make_unique<LevelNoise>(seed_, x_off, y_off, 0,
                                              Chunk::kSize, Chunk::kSize);

  uint64_t chunk_seed = static_cast<uint64_t>(seed_) ^
                        (static_cast<uint64_t>(cx) * 341873128712ULL) ^
                        (static_cast<uint64_t>(cy) * 132897987541ULL) ^
                        (static_cast<uint64_t>(depth_) * 0x9E3779B9ULL);
  util::Random rng(static_cast<int64_t>(chunk_seed));

  const LevelNoise& noise = *chunk->noise;
  for (int32_t ly = 0; ly < Chunk::kSize; ++ly) {
    for (int32_t lx = 0; lx < Chunk::kSize; ++lx) {
      const Biome& biome = Biomes::GetBiomeForDepth(depth_, noise, lx, ly);
      const GeneratedTile gt = biome.GenerateTile(noise, lx, ly, rng);
      const int32_t idx = ly * Chunk::kSize + lx;
      chunk->tile_ids[idx] = gt.tile_id;
      chunk->tile_data[idx] = gt.data;
      chunk->biome_ids[idx] = static_cast<uint8_t>(biome.Id());
      if (gt.generate_ornate_lava_pool) {
        chunk->pending_ornate_pools.emplace_back(x_off + lx, y_off + ly);
      }
    }
  }

  chunk->dirty = true;
  return InsertLoadedChunk(std::move(chunk));
}

const Tile* ChunkManager::GetTile(int32_t wx, int32_t wy) {
  Chunk* c = GetChunk(WorldToChunk(wx), WorldToChunk(wy));
  return Tiles::Get(c->GetTileId(wx, wy));
}

const Tile* ChunkManager::GetTileConst(int32_t wx, int32_t wy) const {
  const Chunk* c = FindChunk(WorldToChunk(wx), WorldToChunk(wy));
  if (!c) return Tiles::Get(Tiles::kConnectTile);
  return Tiles::Get(c->GetTileId(wx, wy));
}

void ChunkManager::SetTile(int32_t wx, int32_t wy, uint16_t tile_id,
                           int16_t data) {
  Chunk* c = GetChunk(WorldToChunk(wx), WorldToChunk(wy));
  const int32_t idx = WorldToLocal(wy) * Chunk::kSize + WorldToLocal(wx);
  c->tile_ids[idx] = tile_id;
  c->tile_data[idx] = data;
  c->dirty = true;
}

int16_t ChunkManager::GetData(int32_t wx, int32_t wy) {
  Chunk* c = GetChunk(WorldToChunk(wx), WorldToChunk(wy));
  return c->GetData(wx, wy);
}

int16_t ChunkManager::GetDataConst(int32_t wx, int32_t wy) const {
  const Chunk* c = FindChunk(WorldToChunk(wx), WorldToChunk(wy));
  return c ? c->GetData(wx, wy) : 0;
}

void ChunkManager::SetData(int32_t wx, int32_t wy, int16_t data) {
  Chunk* c = GetChunk(WorldToChunk(wx), WorldToChunk(wy));
  c->SetData(wx, wy, data);
}

BiomeId ChunkManager::GetBiome(int32_t wx, int32_t wy) {
  Chunk* c = GetChunk(WorldToChunk(wx), WorldToChunk(wy));
  return static_cast<BiomeId>(c->GetBiomeId(wx, wy));
}

}  // namespace minicraft::level
