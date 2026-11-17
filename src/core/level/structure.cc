#include "core/level/structure.h"

#include <sstream>

#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/random.h"

namespace minicraft::level {

void Structure::AddTile(int32_t dx, int32_t dy, uint16_t tile_id,
                        int16_t data) {
  tiles_.push_back({dx, dy, tile_id, data});
}

void Structure::AddFurniture(int32_t dx, int32_t dy, std::string kind) {
  furniture_.push_back({dx, dy, std::move(kind)});
}

void Structure::SetData(const std::string& keys, const std::string& pattern) {
  std::vector<std::string> rows;
  std::istringstream iss(pattern);
  std::string line;
  while (std::getline(iss, line, '\n')) {
    if (!line.empty()) rows.push_back(line);
  }
  if (rows.empty()) return;

  // Format for keys: semicolon-separated "CHAR:TILE_NAME[:DATA]" entries.
  struct Entry {
    char ch = 0;
    uint16_t id = 0;
    int16_t data = 0;
  };
  std::vector<Entry> legend;
  std::istringstream kss(keys);
  std::string item;
  while (std::getline(kss, item, ';')) {
    if (item.size() < 3 || item[1] != ':') continue;
    char ch = item[0];
    std::string rest = item.substr(2);
    size_t colon = rest.find(':');
    std::string tname = (colon == std::string::npos) ? rest : rest.substr(0, colon);
    int16_t data = 0;
    if (colon != std::string::npos) {
      data = static_cast<int16_t>(std::stoi(rest.substr(colon + 1)));
    }
    const Tile* t = Tiles::Find(tname);
    if (t) {
      legend.push_back({ch, t->id, data});
    }
  }

  const int32_t h = static_cast<int32_t>(rows.size());
  const int32_t w = static_cast<int32_t>(rows[0].size());
  for (int32_t i = 0; i < h; ++i) {
    for (int32_t c = 0; c < static_cast<int32_t>(rows[i].size()); ++c) {
      char ch = rows[i][c];
      for (const auto& e : legend) {
        if (e.ch == ch) {
          // Matches Java Structure.setData centering: (-w/2 + c, -h/2 + i)
          AddTile(-w / 2 + c, -h / 2 + i, e.id, e.data);
          break;
        }
      }
    }
  }
}

void Structure::Draw(Level& level, int32_t cx, int32_t cy) const {
  for (const auto& t : tiles_) {
    level.SetTile(cx + t.dx, cy + t.dy, t.tile_id, t.data);
  }
  for (const auto& f : furniture_) {
    level.SpawnStructureFurniture(cx + f.dx, cy + f.dy, f.kind);
  }
}

void Structure::DrawWithFloor(Level& level, int32_t cx, int32_t cy,
                              uint16_t floor_tile_id) const {
  for (const auto& t : tiles_) {
    if (level.GetTile(cx + t.dx, cy + t.dy)->id != Tiles::kStairsDown &&
        level.GetTile(cx + t.dx, cy + t.dy)->id != Tiles::kStairsUp) {
      level.SetTile(cx + t.dx, cy + t.dy, floor_tile_id, 0);
    }
  }
  Draw(level, cx, cy);
}

const Structure& Structure::DungeonGate() {
  static Structure s = [] {
    Structure st;
    st.SetData("O:Obsidian;D:Obsidian Door;W:Obsidian Wall",
               "WWDWW\n"
               "WOOOW\n"
               "DOOOD\n"
               "WOOOW\n"
               "WWDWW");
    st.AddFurniture(-1, -1, "Lantern:Iron");
    return st;
  }();
  return s;
}

const Structure& Structure::DungeonLock() {
  static Structure s = [] {
    Structure st;
    st.SetData("O:Obsidian;W:Obsidian Wall",
               "WWWWW\n"
               "WOOOW\n"
               "WOOOW\n"
               "WOOOW\n"
               "WWWWW");
    return st;
  }();
  return s;
}

const Structure& Structure::DungeonBossRoom() {
  static Structure s = [] {
    Structure st;
    st.SetData("O:Obsidian Boss Floor;D:Obsidian Boss Door;W:Obsidian Boss Wall",
               "WWWWDWWWW\n"
               "WOOOOOOOW\n"
               "WOOOOOOOW\n"
               "WOOOOOOOW\n"
               "DOOOOOOOD\n"
               "WOOOOOOOW\n"
               "WOOOOOOOW\n"
               "WOOOOOOOW\n"
               "WWWWDWWWW");
    st.AddFurniture(0, 0, "KnightStatue:5000");
    return st;
  }();
  return s;
}

const Structure& Structure::DungeonSpawner() {
  static Structure s = [] {
    Structure st;
    st.SetData("F:Grass;W:Obsidian Wall;O:Ornate Obsidian;D:Obsidian Door",
               "WWWDWWW\n"
               "WOOOOOW\n"
               "WOFFFOW\n"
               "DOFFFOD\n"
               "WOFFFOW\n"
               "WOOOOOW\n"
               "WWWDWWW");
    return st;
  }();
  return s;
}

const Structure& Structure::LavaPool() {
  static Structure s = [] {
    Structure st;
    st.SetData("L:Lava",
               "LL\n"
               "LL");
    return st;
  }();
  return s;
}

const Structure& Structure::OrnateLavaPool() {
  static Structure s = [] {
    Structure st;
    st.SetData("L:Lava;W:Obsidian Wall;O:Ornate Obsidian;D:Obsidian Door",
               "WWWDWWW\n"
               "WOOOOOW\n"
               "WOLLLOW\n"
               "DOLLLOD\n"
               "WOLLLOW\n"
               "WOOOOOW\n"
               "WWWDWWW");
    return st;
  }();
  return s;
}

const Structure& Structure::DungeonGarden() {
  static Structure s = [] {
    Structure st;
    st.SetData("F:Flower;W:Obsidian Wall;O:Ornate Obsidian;D:Obsidian Door",
               "WWWDWWW\n"
               "WOOOOOW\n"
               "WOFFFOW\n"
               "DOFFFOD\n"
               "WOFFFOW\n"
               "WOOOOOW\n"
               "WWWDWWW");
    return st;
  }();
  return s;
}

const Structure& Structure::DungeonChestRoom() {
  static Structure s = [] {
    Structure st;
    st.SetData("F:Grass;W:Obsidian Wall;O:Ornate Obsidian;D:Obsidian Door",
               "WWWDWWW\n"
               "WOOOOOW\n"
               "WOFFFOW\n"
               "DOFFFOD\n"
               "WOFFFOW\n"
               "WOOOOOW\n"
               "WWWDWWW");
    st.AddFurniture(0, 0, "DungeonChest");
    return st;
  }();
  return s;
}

const Structure& Structure::MobDungeonCenter() {
  static Structure s = [] {
    Structure st;
    st.SetData("B:Stone Bricks;W:Stone Wall",
               "WWBWW\n"
               "WBBBW\n"
               "BBBBB\n"
               "WBBBW\n"
               "WWBWW");
    return st;
  }();
  return s;
}

const Structure& Structure::MobDungeonNorth() {
  static Structure s = [] {
    Structure st;
    // Open on South (+Y) toward center, B doorway on North (-Y)
    st.SetData("B:Stone Bricks;W:Stone Wall",
               "WWBWW\n"
               "WBBBW\n"
               "WBBBW\n"
               "WBBBW\n"
               "WBBBW");
    return st;
  }();
  return s;
}

const Structure& Structure::MobDungeonSouth() {
  static Structure s = [] {
    Structure st;
    // Open on North (-Y) toward center, B doorway on South (+Y)
    st.SetData("B:Stone Bricks;W:Stone Wall",
               "WBBBW\n"
               "WBBBW\n"
               "WBBBW\n"
               "WBBBW\n"
               "WWBWW");
    return st;
  }();
  return s;
}

const Structure& Structure::MobDungeonEast() {
  static Structure s = [] {
    Structure st;
    // Open on West (-X) toward center, B doorway on East (+X)
    st.SetData("B:Stone Bricks;W:Stone Wall",
               "WWWWW\n"
               "BBBBW\n"
               "BBBBB\n"
               "BBBBW\n"
               "WWWWW");
    return st;
  }();
  return s;
}

const Structure& Structure::MobDungeonWest() {
  static Structure s = [] {
    Structure st;
    // Open on East (+X) toward center, B doorway on West (-X)
    st.SetData("B:Stone Bricks;W:Stone Wall",
               "WWWWW\n"
               "WBBBB\n"
               "BBBBB\n"
               "WBBBB\n"
               "WWWWW");
    return st;
  }();
  return s;
}

const Structure& Structure::AirWizardHouse() {
  static Structure s = [] {
    Structure st;
    st.SetData("F:Wood Planks;W:Wood Wall;D:Wood Door",
               "WWWWWWW\n"
               "WFFFFFW\n"
               "DFFFFFW\n"
               "WFFFFFW\n"
               "WWWWWWW");
    st.AddFurniture(-2, 0, "Lantern:Gold");
    st.AddFurniture(0, 0, "Crafter:Enchanter");
    return st;
  }();
  return s;
}

const Structure& Structure::VillageHouseNormal() {
  static Structure s = [] {
    Structure st;
    st.SetData("F:Wood Planks;W:Wood Wall;D:Wood Door;G:Grass",
               "WWWWW\n"
               "WFFFW\n"
               "WFFFW\n"
               "WFFFW\n"
               "WWDGW");
    return st;
  }();
  return s;
}

const Structure& Structure::VillageHouseTwoDoor() {
  static Structure s = [] {
    Structure st;
    st.SetData("F:Wood Planks;W:Wood Wall;D:Wood Door",
               "WWDWW\n"
               "WFFFW\n"
               "WFFFD\n"
               "WFFFW\n"
               "WWWWW");
    return st;
  }();
  return s;
}

const Structure& Structure::VillageRuinedOverlay1() {
  static Structure s = [] {
    Structure st;
    st.SetData("G:Grass;F:Wood Planks",
               "*F*G*\n"
               "F*G**\n"
               "GG**G\n"
               "*G*G*\n"
               "**F**");
    return st;
  }();
  return s;
}

const Structure& Structure::VillageRuinedOverlay2() {
  static Structure s = [] {
    Structure st;
    st.SetData("G:Grass;F:Wood Planks",
               "F**F*\n"
               "**G*F\n"
               "**G**\n"
               "G**G*\n"
               "***FG");
    return st;
  }();
  return s;
}

const Structure& Structure::MiniDungeon() { return MobDungeonCenter(); }
const Structure& Structure::VillageHouse() { return VillageHouseNormal(); }
const Structure& Structure::ObsidianDungeonRoom() { return DungeonBossRoom(); }
const Structure& Structure::SkyTower() { return AirWizardHouse(); }

}  // namespace minicraft::level
