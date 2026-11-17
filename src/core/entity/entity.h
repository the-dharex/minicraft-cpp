#ifndef MINICRAFT_CORE_ENTITY_ENTITY_H_
#define MINICRAFT_CORE_ENTITY_ENTITY_H_

#include <cstdint>
#include <memory>
#include <string>

#include "core/entity/direction.h"
#include "core/gfx/geometry.h"

namespace minicraft {
namespace gfx {
class Screen;
}
namespace item {
class Item;
}
namespace level {
class Level;
class Tile;
}
}  // namespace minicraft

namespace minicraft::entity {

class Mob;
class Player;

// Base class for all dynamic world entities (mobs, player, furniture,
// projectiles, particles, dropped items, boats).
class Entity : public std::enable_shared_from_this<Entity> {
 public:
  Entity(int32_t xr = 6, int32_t yr = 6);
  virtual ~Entity() = default;

  int32_t x = 0;
  int32_t y = 0;
  int32_t xr = 6;
  int32_t yr = 6;

  bool removed = false;
  level::Level* level = nullptr;
  int32_t eid = 0;

  virtual void Render(gfx::Screen* screen) const {}
  virtual void Tick() {}

  virtual void Remove();
  virtual void SetLevel(level::Level* lvl, int32_t nx, int32_t ny);

  virtual bool Intersects(int32_t x0, int32_t y0, int32_t x1,
                          int32_t y1) const;
  gfx::Rectangle GetBounds() const;

  virtual bool Blocks(const Entity& e) const { return false; }
  virtual void Hurt(Mob& mob, int32_t dmg, Direction attack_dir) {}
  virtual void Hurt(const level::Tile& tile, int32_t x, int32_t y,
                    int32_t dmg) {}
  virtual bool Move(int32_t xa, int32_t ya);

  virtual bool Interact(Player& player, item::Item* item,
                        Direction attack_dir) {
    return false;
  }
  virtual bool Use(Player& player) { return false; }

  virtual int32_t GetLightRadius() const { return 0; }
  virtual bool IsSolid() const { return true; }
  virtual bool CanSwim() const { return false; }
  virtual bool CanWool() const { return false; }
  virtual bool CanLight() const { return false; }
  virtual bool IsBlockableBy(const Mob& mob) const { return true; }

  virtual void TouchedBy(Entity& entity) {}
  virtual void TryPush(Player& player);
  virtual int32_t GetPushTimeDelay() const { return 10; }

  virtual std::string GetEntityTypeName() const = 0;

  bool IsWithin(int32_t tile_radius, const Entity& other) const;

 protected:
  virtual bool Move2(int32_t xa, int32_t ya);
  void AdvancePush();
  bool IsStopped() const { return false; }

  int32_t push_time_ = 0;
  Direction push_dir_ = Direction::kNone;
};

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_ENTITY_H_
