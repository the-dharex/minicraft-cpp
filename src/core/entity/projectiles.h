#ifndef MINICRAFT_CORE_ENTITY_PROJECTILES_H_
#define MINICRAFT_CORE_ENTITY_PROJECTILES_H_

#include <cstdint>
#include <memory>
#include <string>

#include "core/entity/direction.h"
#include "core/entity/entity.h"
#include "core/gfx/sprite_atlas.h"

namespace minicraft::item {
class Item;
}

namespace minicraft::entity {

class Mob;
class Player;

class ItemEntity : public Entity {
 public:
  ItemEntity(std::unique_ptr<item::Item> item, int32_t x, int32_t y);
  ItemEntity(std::unique_ptr<item::Item> item, int32_t x, int32_t y,
             double zz, int32_t life_time, double xa, double ya, double za);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool IsSolid() const override { return false; }
  bool IsBlockableBy(const Mob& mob) const override { return false; }
  void TouchedBy(Entity& entity) override;
  void Take(Player& player);
  std::string GetEntityTypeName() const override { return "ItemEntity"; }

  std::unique_ptr<item::Item> item;

 private:
  int32_t life_time_ = 60 * 10;
  int32_t time_ = 0;
  double xa_ = 0.0;
  double ya_ = 0.0;
  double za_ = 0.0;
  double xx_ = 0.0;
  double yy_ = 0.0;
  double zz_ = 0.0;
  bool picked_up_ = false;
};

class Arrow : public Entity {
 public:
  Arrow(Mob* owner, Direction dir, int32_t dmg);
  Arrow(Mob* owner, int32_t x, int32_t y, Direction dir, int32_t dmg);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool IsSolid() const override { return false; }
  bool CanSwim() const override { return true; }
  bool IsBlockableBy(const Mob& mob) const override { return false; }
  std::string GetEntityTypeName() const override { return "Arrow"; }

 private:
  Mob* owner_ = nullptr;
  Direction dir_ = Direction::kDown;
  int32_t damage_ = 2;
  int32_t speed_ = 2;
  gfx::LinkedSprite sprite_;
};

class Spark : public Entity {
 public:
  Spark(Mob* owner, double xa, double ya, int32_t life_time = -1);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool IsSolid() const override { return false; }
  bool IsBlockableBy(const Mob& mob) const override { return false; }
  std::string GetEntityTypeName() const override { return "Spark"; }

  int32_t LifeTime() const { return life_time_; }

 protected:
  Mob* owner_ = nullptr;
  int32_t life_time_ = 360;
  int32_t time_ = 0;
  double xa_ = 0.0;
  double ya_ = 0.0;
  double xx_ = 0.0;
  double yy_ = 0.0;
  gfx::LinkedSprite sprite_;
};

class FireSpark : public Spark {
 public:
  FireSpark(Mob* owner, double xa, double ya);
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  int32_t GetLightRadius() const override { return 3; }
  std::string GetEntityTypeName() const override { return "FireSpark"; }

  bool IsStopped() const { return stopped_; }

 private:
  bool stopped_ = false;
  int32_t stopped_time_ = 0;
};

class ExplosionTileTicker : public Entity {
 public:
  ExplosionTileTicker(int32_t tx, int32_t ty, int32_t max_delay = 5);
  void Tick() override;
  bool IsSolid() const override { return false; }
  bool IsBlockableBy(const Mob& mob) const override { return false; }
  std::string GetEntityTypeName() const override {
    return "ExplosionTileTicker";
  }

 private:
  int32_t tx_ = 0;
  int32_t ty_ = 0;
  int32_t time_ = 0;
  int32_t max_delay_ = 5;
};

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_PROJECTILES_H_
