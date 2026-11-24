#ifndef MINICRAFT_CORE_LEVEL_LEVEL_GEN_H_
#define MINICRAFT_CORE_LEVEL_LEVEL_GEN_H_

#include <cstdint>

namespace minicraft::level {

class Level;

class LevelGen {
 public:
  static void GenerateInitialChunks(Level& level);
  static void LinkStairsAndPopulate(Level& level, Level* above_level);
  static void GenerateSpawnerStructures(Level& level);
  static void GenerateVillages(Level& level);
  static void GenerateDungeonStructures(Level& level);
  static void PlaceInitialDungeonStructures(Level& level);
  static void PlaceSkyBossArena(Level& level);
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_LEVEL_GEN_H_
