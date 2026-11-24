#ifndef MINICRAFT_CORE_LEVEL_TILE_H_
#define MINICRAFT_CORE_LEVEL_TILE_H_

#include <cstdint>
#include <functional>
#include <string>

#include "core/entity/direction.h"
#include "core/gfx/sprite_atlas.h"

namespace minicraft {
namespace gfx {
class Screen;
}
namespace entity {
class Entity;
class Mob;
class Player;
}
namespace item {
class Item;
}
}  // namespace minicraft

namespace minicraft::level {

class Level;

enum class TileMaterial : uint8_t {
  kWood = 0,
  kStone = 1,
  kObsidian = 2,
};

// Base class for all static world tiles. Tile instances are immutable
// flyweight singletons owned by `Tiles`; per-cell state lives in Level's
// chunk arrays (`data`).
class Tile {
 public:
  static int32_t tick_count;

  Tile(uint16_t id, std::string name, std::string sprite_key = "");
  virtual ~Tile() = default;

  uint16_t id = 0;
  std::string name;

  bool connects_to_grass = false;
  bool connects_to_sand = false;
  bool connects_to_fluid = false;
  bool connects_to_rock = false;
  bool may_spawn = false;

  virtual bool MaySpawn() const { return may_spawn; }

  virtual bool MayPass(const Level& level, int32_t x, int32_t y,
                       const entity::Entity& e) const;

  virtual int32_t GetLightRadius(const Level& level, int32_t x,
                                 int32_t y) const {
    return 0;
  }

  // Returns true if tile took damage / reacted
  virtual bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& source,
                    int32_t dmg, entity::Direction attack_dir) const {
    return false;
  }

  virtual void Hurt(Level& level, int32_t x, int32_t y, int32_t dmg) const {}

  virtual bool Interact(Level& level, int32_t xt, int32_t yt,
                        entity::Player& player, item::Item* item,
                        entity::Direction attack_dir) const {
    return false;
  }

  virtual void SteppedOn(Level& level, int32_t xt, int32_t yt,
                         entity::Entity& entity) const {}

  virtual bool Tick(Level& level, int32_t xt, int32_t yt) const {
    return false;
  }

  virtual bool OnExplode(Level& /*level*/, int32_t /*xt*/,
                         int32_t /*yt*/) const {
    return false;
  }

  virtual void Render(gfx::Screen* screen, const Level& level, int32_t x,
                      int32_t y) const;

  virtual void BumpedInto(Level& level, int32_t x, int32_t y,
                          entity::Entity& entity) const {}

  virtual std::string GetName(int32_t /*data*/) const { return name; }

  virtual bool ConnectsToGrass(const Level& /*level*/, int32_t /*x*/,
                               int32_t /*y*/) const {
    return connects_to_grass;
  }
  virtual bool ConnectsToSand(const Level& /*level*/, int32_t /*x*/,
                              int32_t /*y*/) const {
    return connects_to_sand;
  }
  virtual bool ConnectsToFluid(const Level& /*level*/, int32_t /*x*/,
                               int32_t /*y*/) const {
    return connects_to_fluid;
  }
  virtual bool ConnectsToRock(const Level& /*level*/, int32_t /*x*/,
                              int32_t /*y*/) const {
    return connects_to_rock;
  }

  virtual void OnPlaced(Level& /*level*/, int32_t /*x*/, int32_t /*y*/) const {}
  virtual void OnRemoved(Level& /*level*/, int32_t /*x*/,
                         int32_t /*y*/) const {}

 protected:
  mutable gfx::LinkedSprite sprite_;

  static void RenderDirtBackground(gfx::Screen* screen, const Level& level,
                                   int32_t x, int32_t y);
  static void RenderConnected(
      gfx::Screen* screen, const Level& level, int32_t x, int32_t y,
      gfx::LinkedSprite& base, gfx::LinkedSprite& border,
      gfx::LinkedSprite* corner,
      const std::function<bool(const Tile*, int32_t, int32_t)>& pred,
      bool singleton_with_connective = true);
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_TILE_H_
