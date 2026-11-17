#ifndef MINICRAFT_CORE_LEVEL_TILES_INTERNAL_H_
#define MINICRAFT_CORE_LEVEL_TILES_INTERNAL_H_

#include <memory>
#include <vector>

#include "core/level/tile.h"

namespace minicraft::level::detail {

void RegisterNatureTiles(std::vector<std::unique_ptr<Tile>>& out);
void RegisterBuildingTiles(std::vector<std::unique_ptr<Tile>>& out);
void RegisterFarmingTiles(std::vector<std::unique_ptr<Tile>>& out);

}  // namespace minicraft::level::detail

#endif  // MINICRAFT_CORE_LEVEL_TILES_INTERNAL_H_
