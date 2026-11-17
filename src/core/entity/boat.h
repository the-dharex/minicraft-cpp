#ifndef MINICRAFT_CORE_ENTITY_BOAT_H_
#define MINICRAFT_CORE_ENTITY_BOAT_H_

#include <cstdint>
#include <string>

#include "core/entity/direction.h"
#include "core/entity/entity.h"
#include "core/gfx/sprite_atlas.h"

namespace minicraft::entity {

class Player;

class Boat : public Entity {
 public:
  explicit Boat(Direction dir = Direction::kDown);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool Blocks(const Entity& e) const override;
  bool CanSwim() const override { return true; }
  void TouchedBy(Entity& entity) override;
  int32_t GetPushTimeDelay() const override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  bool Use(Player& player) override;
  using Entity::Hurt;
  void Hurt(Mob& mob, int32_t dmg, Direction attack_dir) override;
  void Hurt() { BreakBoat(); }
  std::string GetEntityTypeName() const override { return "Boat"; }

  void SetRider(Player* rider);
  Player* Rider() const { return rider_; }
  Direction Dir() const { return dir_; }
  void Drive(int32_t xa, int32_t ya, Direction d);
  bool IsInWater() const;
  bool IsInLava() const;

 private:
  void BreakBoat();

  Direction dir_ = Direction::kDown;
  Player* rider_ = nullptr;
  int32_t exit_timer_ = 0;
  int32_t walk_dist_ = 0;
  int32_t tick_count_ = 0;
  int32_t unit_move_counter_ = 0;
  gfx::LinkedSprite sprite_;
};

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_BOAT_H_
