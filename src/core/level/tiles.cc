#include "core/level/tiles.h"

#include <cctype>
#include <vector>

#include "core/level/tiles_internal.h"

namespace minicraft::level {

std::array<std::unique_ptr<Tile>, 256> Tiles::tiles_;
bool Tiles::initialized_ = false;

void Tiles::InitTiles() {
  if (initialized_) return;
  std::vector<std::unique_ptr<Tile>> temp(256);
  detail::RegisterNatureTiles(temp);
  detail::RegisterBuildingTiles(temp);
  detail::RegisterFarmingTiles(temp);
  for (size_t i = 0; i < 256; ++i) {
    tiles_[i] = std::move(temp[i]);
  }
  initialized_ = true;
}

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

const Tile* Tiles::Get(uint16_t id) {
  if (!initialized_) InitTiles();
  if (id < 256 && tiles_[id]) {
    return tiles_[id].get();
  }
  return tiles_[kConnectTile].get();
}

const Tile* Tiles::Find(std::string_view name) {
  if (!initialized_) InitTiles();
  // Strip any legacy metadata suffix from the first '_' (matches Java Tiles.get).
  const size_t underscore = name.find('_');
  if (underscore != std::string_view::npos) {
    name = name.substr(0, underscore);
  }
  if (EqualsIgnoreCase(name, "lapis ore")) {
    name = "lapis";
  } else if (EqualsIgnoreCase(name, "wool")) {
    name = "white wool";
  }
  for (const auto& t : tiles_) {
    if (!t) continue;
    if (EqualsIgnoreCase(t->name, name)) {
      return t.get();
    }
  }
  return nullptr;
}

const Tile* Tiles::Get(std::string_view name) {
  const Tile* found = Find(name);
  return found ? found : tiles_[kGrass].get();
}

bool Tiles::ContainsTile(uint16_t id) {
  if (!initialized_) InitTiles();
  return id < 256 && tiles_[id] != nullptr;
}

bool Tiles::IsExplosionResistant(uint16_t id) {
  switch (id) {
    case kStairsUp:
    case kStairsDown:
    case kHardRock:
    case kObsidianDoor:
    case kObsidian:
    case kObsidianWall:
    case kRawObsidian:
    case kOrnateObsidian:
    case kBossWall:
    case kBossFloor:
    case kBossDoor:
      return true;
    default:
      return false;
  }
}

bool Tiles::IsWool(uint16_t id) {
  return id == kWhiteWool || (id >= kRedWool && id <= kBlackWool) ||
         (id >= kBrownWool && id <= kOrangeWool);
}

}  // namespace minicraft::level
