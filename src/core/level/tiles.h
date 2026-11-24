#ifndef MINICRAFT_CORE_LEVEL_TILES_H_
#define MINICRAFT_CORE_LEVEL_TILES_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "core/level/tile.h"

namespace minicraft::level {

// Static registry of all game tiles (IDs 0..69 + 255 ConnectTile).
class Tiles {
 public:
  static constexpr uint16_t kGrass = 0;
  static constexpr uint16_t kDirt = 1;
  static constexpr uint16_t kFlower = 2;
  static constexpr uint16_t kHole = 3;
  static constexpr uint16_t kStairsUp = 4;
  static constexpr uint16_t kStairsDown = 5;
  static constexpr uint16_t kWater = 6;
  static constexpr uint16_t kRock = 7;
  static constexpr uint16_t kTree = 8;
  static constexpr uint16_t kTreeSapling = 9;
  static constexpr uint16_t kSand = 10;
  static constexpr uint16_t kCactus = 11;
  static constexpr uint16_t kCactusSapling = 12;
  static constexpr uint16_t kIronOre = 13;
  static constexpr uint16_t kGoldOre = 14;
  static constexpr uint16_t kGemOre = 15;
  static constexpr uint16_t kLapis = 16;
  static constexpr uint16_t kLava = 17;
  static constexpr uint16_t kLavaBrick = 18;
  static constexpr uint16_t kExplode = 19;
  static constexpr uint16_t kFarmland = 20;
  static constexpr uint16_t kWheat = 21;
  static constexpr uint16_t kHardRock = 22;
  static constexpr uint16_t kInfiniteFall = 23;
  static constexpr uint16_t kCloud = 24;
  static constexpr uint16_t kCloudCactus = 25;
  static constexpr uint16_t kWoodDoor = 26;
  static constexpr uint16_t kStoneDoor = 27;
  static constexpr uint16_t kObsidianDoor = 28;
  static constexpr uint16_t kWoodPlanks = 29;
  static constexpr uint16_t kStoneBricks = 30;
  static constexpr uint16_t kObsidian = 31;
  static constexpr uint16_t kWoodWall = 32;
  static constexpr uint16_t kStoneWall = 33;
  static constexpr uint16_t kObsidianWall = 34;
  static constexpr uint16_t kWhiteWool = 35;
  static constexpr uint16_t kPath = 36;
  static constexpr uint16_t kRedWool = 37;
  static constexpr uint16_t kBlueWool = 38;
  static constexpr uint16_t kGreenWool = 39;
  static constexpr uint16_t kYellowWool = 40;
  static constexpr uint16_t kBlackWool = 41;
  static constexpr uint16_t kPotato = 42;
  static constexpr uint16_t kRawStone = 43;
  static constexpr uint16_t kRawObsidian = 44;
  static constexpr uint16_t kOrnateStone = 45;
  static constexpr uint16_t kOrnateObsidian = 46;
  static constexpr uint16_t kBossWall = 47;
  static constexpr uint16_t kBossFloor = 48;
  static constexpr uint16_t kBossDoor = 49;
  static constexpr uint16_t kTomato = 50;
  static constexpr uint16_t kCarrot = 51;
  static constexpr uint16_t kHeavenlyBerries = 52;
  static constexpr uint16_t kHellishBerries = 53;
  static constexpr uint16_t kWoodFence = 54;
  static constexpr uint16_t kStoneFence = 55;
  static constexpr uint16_t kObsidianFence = 56;
  static constexpr uint16_t kTorch = 57;
  static constexpr uint16_t kSign = 58;
  static constexpr uint16_t kOrnateWood = 59;
  static constexpr uint16_t kBrownWool = 60;
  static constexpr uint16_t kPurpleWool = 61;
  static constexpr uint16_t kCyanWool = 62;
  static constexpr uint16_t kLightGrayWool = 63;
  static constexpr uint16_t kGrayWool = 64;
  static constexpr uint16_t kPinkWool = 65;
  static constexpr uint16_t kLimeWool = 66;
  static constexpr uint16_t kLightBlueWool = 67;
  static constexpr uint16_t kMagentaWool = 68;
  static constexpr uint16_t kOrangeWool = 69;
  static constexpr uint16_t kConnectTile = 255;

  static void InitTiles();
  static const Tile* Get(uint16_t id);
  static const Tile* Get(std::string_view name);
  static const Tile* Find(std::string_view name);
  static bool ContainsTile(uint16_t id);
  static bool IsExplosionResistant(uint16_t id);
  static bool IsWool(uint16_t id);

 private:
  static std::array<std::unique_ptr<Tile>, 256> tiles_;
  static bool initialized_;
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_TILES_H_
