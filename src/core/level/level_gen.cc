#include "core/level/level_gen.h"

#include <algorithm>
#include <cmath>

#include "core/entity/furniture.h"
#include "core/level/chunk_manager.h"
#include "core/level/level.h"
#include "core/level/structure.h"
#include "core/level/tiles.h"
#include "core/util/random.h"

namespace minicraft::level {

void LevelGen::GenerateInitialChunks(Level& level) {
  ChunkManager& cm = level.GetChunkManager();
  const int32_t cx_count = std::max(1, level.w / Chunk::kSize);
  const int32_t cy_count = std::max(1, level.h / Chunk::kSize);
  for (int32_t cy = 0; cy < cy_count; ++cy) {
    for (int32_t cx = 0; cx < cx_count; ++cx) {
      cm.GetChunk(cx, cy);
      level.PopulateChunk(cx, cy);
    }
  }

  if (level.depth == 0) {
    GenerateVillages(level);

    // Guarantee dry spawn pad near world center (64, 64)
    const int32_t sx = level.w / 2;
    const int32_t sy = level.h / 2;
    for (int32_t dy = -3; dy <= 3; ++dy) {
      for (int32_t dx = -3; dx <= 3; ++dx) {
        const uint16_t tid = level.GetTile(sx + dx, sy + dy)->id;
        if (tid != Tiles::kGrass && tid != Tiles::kSand &&
            tid != Tiles::kFlower) {
          level.SetTile(sx + dx, sy + dy, Tiles::kGrass, 0);
        }
      }
    }
  } else if (level.depth <= -1 && level.depth >= -3) {
    GenerateSpawnerStructures(level);
  } else if (level.depth == 1) {
    util::Random rng(level.seed ^ 0x51A7ULL);
    bool placed_house = false;
    for (int32_t attempt = 0; attempt < 200 && !placed_house; ++attempt) {
      const int32_t x = 8 + rng.NextInt(std::max(1, level.w - 16));
      const int32_t y = 8 + rng.NextInt(std::max(1, level.h - 16));
      if (std::abs(x - level.w / 2) <= 6 && std::abs(y - level.h / 2) <= 6) {
        continue;
      }
      if (level.GetTile(x - 3, y - 2)->id == Tiles::kCloud &&
          level.GetTile(x + 3, y - 2)->id == Tiles::kCloud &&
          level.GetTile(x - 3, y + 2)->id == Tiles::kCloud &&
          level.GetTile(x + 3, y + 2)->id == Tiles::kCloud) {
        Structure::AirWizardHouse().Draw(level, x, y);
        placed_house = true;
      }
    }
    if (!placed_house) {
      Structure::AirWizardHouse().Draw(level, level.w / 2, level.h / 2 - 12);
    }
    PlaceSkyBossArena(level);
  }
}

void LevelGen::GenerateSpawnerStructures(Level& level) {
  if (level.depth >= 0 || level.depth <= -4) return;
  util::Random rng(level.seed ^ (static_cast<int64_t>(level.depth) * 13379LL));
  const int32_t count =
      std::max(2, (18 / -level.depth) * std::max(1, level.w / 128));
  for (int32_t i = 0; i < count; ++i) {
    const int32_t x = 10 + rng.NextInt(std::max(1, level.w - 20));
    const int32_t y = 10 + rng.NextInt(std::max(1, level.h - 20));

    Structure::MobDungeonCenter().Draw(level, x, y);
    if (rng.NextInt(2) == 0) {
      Structure::MobDungeonNorth().Draw(level, x, y - 5);
    }
    if (rng.NextInt(2) == 0) {
      Structure::MobDungeonSouth().Draw(level, x, y + 5);
    }
    if (rng.NextInt(2) == 0) {
      Structure::MobDungeonEast().Draw(level, x + 5, y);
    }
    if (rng.NextInt(2) == 0) {
      Structure::MobDungeonWest().Draw(level, x - 5, y);
    }

    static const char* const kCaveMobs[] = {"Zombie", "Skeleton", "Slime",
                                            "Creeper"};
    const char* mob_type = kCaveMobs[rng.NextInt(4)];
    const int32_t sp_x = x * 16 + 8;
    const int32_t sp_y = y * 16 + 8;
    if (level.GetTile(x, y)->id == Tiles::kObsidianWall) {
      level.SetTile(x, y, Tiles::kDirt, 0);
    }
    level.Add(
        std::make_shared<entity::Spawner>(
            mob_type, std::clamp(-level.depth, 1, 4)),
        sp_x, sp_y);

    for (int32_t rpt = 0; rpt < 2; ++rpt) {
      if (rng.NextInt(2) != 0) continue;
      auto chest = std::make_shared<entity::DungeonChest>(true, true);
      level.Add(std::move(chest), sp_x - 16 + rpt * 32, sp_y - 16);
    }
  }
}

void LevelGen::GenerateVillages(Level& level) {
  if (level.depth != 0) return;
  util::Random rng(level.seed ^ 0x7111A9ULL);
  const int32_t max_villages = std::max(1, level.w / 64);
  int32_t placed = 0;

  for (int32_t attempt = 0; attempt < 64 && placed < max_villages; ++attempt) {
    const int32_t vx = 12 + rng.NextInt(std::max(1, level.w - 24));
    const int32_t vy = 12 + rng.NextInt(std::max(1, level.h - 24));
    if (std::abs(vx - level.w / 2) < 12 && std::abs(vy - level.h / 2) < 12) {
      continue;
    }

    bool flat_area = true;
    for (int32_t dy = -5; dy <= 5 && flat_area; ++dy) {
      for (int32_t dx = -5; dx <= 5; ++dx) {
        const uint16_t tid = level.GetTile(vx + dx, vy + dy)->id;
        if (tid != Tiles::kGrass && tid != Tiles::kSand &&
            tid != Tiles::kFlower) {
          flat_area = false;
          break;
        }
      }
    }
    if (!flat_area) continue;

    // Crossbar paths
    for (int32_t d = -6; d <= 6; ++d) {
      level.SetTile(vx + d, vy, Tiles::kPath, 0);
      level.SetTile(vx, vy + d, Tiles::kPath, 0);
    }

    const int32_t houses = 2 + rng.NextInt(3);
    for (int32_t h_idx = 0; h_idx < houses; ++h_idx) {
      const int32_t hx = vx + ((h_idx % 2 == 0) ? -5 : 5);
      const int32_t hy = vy + ((h_idx < 2) ? -5 : 5);
      if (rng.NextInt(2) == 0) {
        Structure::VillageHouseTwoDoor().Draw(level, hx, hy);
      } else {
        Structure::VillageHouseNormal().Draw(level, hx, hy);
      }
      if (rng.NextInt(3) == 0) {
        Structure::VillageRuinedOverlay1().Draw(level, hx, hy);
      } else if (rng.NextInt(3) == 0) {
        Structure::VillageRuinedOverlay2().Draw(level, hx, hy);
      }
      if (rng.NextInt(2) == 0) {
        auto chest = std::make_shared<entity::Chest>();
        chest->PopulateInvRandom("villagehouse", 1);
        level.Add(std::move(chest), (hx + rng.NextInt(2)) * 16 + 8,
                  (hy + rng.NextInt(2)) * 16 + 8);
      }
    }
    ++placed;
  }
}

void LevelGen::GenerateDungeonStructures(Level& level) {
  if (level.depth != -4) return;
  util::Random rng(level.seed ^ 0xD09E0ULL);
  const int32_t mid_x = level.w / 2;
  const int32_t mid_y = level.h / 2;
  const int32_t count = 6;
  for (int32_t i = 0; i < count; ++i) {
    const int32_t rx = 10 + rng.NextInt(std::max(1, level.w - 20));
    const int32_t ry = 10 + rng.NextInt(std::max(1, level.h - 20));
    if (std::abs(rx - mid_x) <= 10 && std::abs(ry - mid_y) <= 10) {
      continue;
    }
    if (rng.NextInt(2) == 0) {
      Structure::DungeonGarden().Draw(level, rx, ry);
    } else {
      Structure::DungeonChestRoom().Draw(level, rx, ry);
    }
  }
}

void LevelGen::LinkStairsAndPopulate(Level& level, Level* above_level) {
  if (above_level == nullptr) return;

  if (level.depth == -4) {
    GenerateDungeonStructures(level);
    Structure::DungeonBossRoom().Draw(level, level.w / 2, level.h / 2);
  }

  int32_t placed = 0;
  for (int32_t y = 0; y < level.h; ++y) {
    for (int32_t x = 0; x < level.w; ++x) {
      if (above_level->GetTile(x, y)->id == Tiles::kStairsDown) {
        if (level.depth == -4) {
          Structure::DungeonGate().Draw(level, x, y);
        } else if (level.depth == 0) {
          for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
              level.SetTile(x + dx, y + dy, Tiles::kHardRock, 0);
            }
          }
        } else {
          for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
              level.SetTile(x + dx, y + dy, Tiles::kDirt, 0);
            }
          }
        }
        level.SetTile(x, y, Tiles::kStairsUp, 0);
        ++placed;
      }
    }
  }

  // Fallback if above_level generated zero Stairs Down in initial bounds
  if (placed == 0) {
    const int32_t sx = level.w / 2;
    const int32_t sy =
        (level.depth == -4) ? std::min(level.h - 8, level.h / 2 + 24)
                            : level.h / 2;
    above_level->SetTile(sx, sy, Tiles::kStairsDown, 0);
    if (level.depth == -4) {
      Structure::DungeonGate().Draw(level, sx, sy);
    } else if (level.depth == 0) {
      for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
          level.SetTile(sx + dx, sy + dy, Tiles::kHardRock, 0);
        }
      }
    } else {
      for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
          level.SetTile(sx + dx, sy + dy, Tiles::kDirt, 0);
        }
      }
    }
    level.SetTile(sx, sy, Tiles::kStairsUp, 0);
  }
}

void LevelGen::PlaceInitialDungeonStructures(Level& level) {
  GenerateDungeonStructures(level);
  Structure::DungeonBossRoom().Draw(level, level.w / 2, level.h / 2);
}

void LevelGen::PlaceSkyBossArena(Level& level) {
  const int32_t cx = level.w / 2;
  const int32_t cy = level.h / 2;
  for (int32_t dy = -8; dy <= 8; ++dy) {
    for (int32_t dx = -8; dx <= 8; ++dx) {
      if (dx * dx + dy * dy <= 64) {
        const uint16_t existing = level.GetTile(cx + dx, cy + dy)->id;
        if (existing != Tiles::kStairsUp && existing != Tiles::kStairsDown) {
          level.SetTile(cx + dx, cy + dy, Tiles::kCloud, 0);
        }
      }
    }
  }
  if (level.GetTile(cx, cy)->id != Tiles::kStairsDown) {
    level.SetTile(cx, cy, Tiles::kStairsDown, 0);
  }
}

}  // namespace minicraft::level
