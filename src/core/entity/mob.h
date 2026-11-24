#ifndef MINICRAFT_CORE_ENTITY_MOB_H_
#define MINICRAFT_CORE_ENTITY_MOB_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "core/entity/direction.h"
#include "core/entity/entity.h"
#include "core/gfx/sprite_atlas.h"
#include "core/item/item.h"

namespace minicraft::entity {

class Mob : public Entity {
 public:
  Mob(int32_t xr, int32_t yr, int32_t max_health);
  ~Mob() override = default;

  Direction dir = Direction::kDown;
  int32_t walk_dist = 0;
  int32_t hurt_time = 0;
  int32_t x_knockback = 0;
  int32_t y_knockback = 0;
  int32_t health = 10;
  int32_t max_health = 10;
  int32_t swim_timer = 0;
  int32_t tick_time = 0;
  int32_t burning_duration = 0;

  void Tick() override;
  bool Move(int32_t xa, int32_t ya) override;
  bool Blocks(const Entity& e) const override { return e.IsBlockableBy(*this); }
  bool CanSwim() const override { return false; }
  virtual bool CanBurn() const { return true; }
  void Burn(int32_t seconds);
  bool IsSwimming() const;
  bool IsWooling() const;

  void Hurt(Mob& mob, int32_t dmg, Direction attack_dir) override;
  void Hurt(const level::Tile& tile, int32_t x, int32_t y,
            int32_t dmg) override;
  virtual int32_t CalculateEntityDamage(const Entity* attacker,
                                        int32_t dmg) const {
    return dmg;
  }
  virtual void DoHurt(int32_t dmg, Direction attack_dir);
  void Heal(int32_t amount);
  virtual void Die();

  static bool CheckStartPos(const level::Level& lvl, int32_t x, int32_t y);
  virtual bool FindStartPos(const level::Level& lvl, int32_t attempts = 10);

  static std::shared_ptr<Mob> CreateByName(std::string_view name,
                                           int32_t lvl = 1);

  int32_t Speed() const { return speed_; }
  void SetSpeed(int32_t s) { speed_ = s < 1 ? 1 : s; }
  int32_t WalkTime() const { return walk_time_; }

 protected:
  int32_t speed_ = 1;
  int32_t walk_time_ = 1;
  int32_t no_action_time_ = 0;
};

class MobAi : public Mob {
 public:
  MobAi(gfx::LinkedSprite sprite, int32_t max_health, int32_t lifetime_secs,
        int32_t rw_chance, int32_t rw_duration);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  void DoHurt(int32_t dmg, Direction attack_dir) override;
  bool CanWool() const override { return true; }
  virtual void HandleDespawn();
  virtual bool RemoveWhenFarAway() const { return true; }
  static bool CheckStartPos(const level::Level& lvl, int32_t x, int32_t y,
                            int32_t player_dist, int32_t solo_radius);

 protected:
  void RenderMobSprite(gfx::Screen* screen, int32_t xt, int32_t yt,
                       int32_t mirror, int32_t tint = -1) const;
  bool IsWithinLight() const;
  bool SkipTick() const;
  virtual void RandomizeWalkDir(bool by_chance);

  gfx::LinkedSprite sprite_;
  int32_t random_walk_time_ = 0;
  int32_t random_walk_chance_ = 40;
  int32_t random_walk_duration_ = 60;
  int32_t xa_ = 0;
  int32_t ya_ = 0;
  int32_t lifetime_ = 0;
  int32_t age_ = 0;
  bool slow_tick_ = false;
};

class PassiveMob : public MobAi {
 public:
  PassiveMob(gfx::LinkedSprite sprite, int32_t max_health);
  void HandleDespawn() override;
  void RandomizeWalkDir(bool by_chance) override;
  void Die() override;
  static bool CheckStartPos(const level::Level& lvl, int32_t x, int32_t y);
};

class EnemyMob : public MobAi {
 public:
  EnemyMob(int32_t lvl, gfx::LinkedSprite sprite, int32_t max_health,
           int32_t detect_dist, int32_t lifetime_secs = 60,
           int32_t rw_chance = 200, int32_t rw_duration = 60);

  int32_t lvl = 1;
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  void HandleDespawn() override;
  void TouchedBy(Entity& entity) override;
  void Die() override;
  int32_t GetLevelColor() const;
  static bool CheckStartPos(const level::Level& lvl, int32_t x, int32_t y);

 protected:
  int32_t detect_dist_ = 50;
};

class Cow : public PassiveMob {
 public:
  Cow();
  void Tick() override;
  void Die() override;
  std::string GetEntityTypeName() const override { return "Cow"; }
};

class Pig : public PassiveMob {
 public:
  Pig();
  void Die() override;
  std::string GetEntityTypeName() const override { return "Pig"; }
};

class Sheep : public PassiveMob {
 public:
  explicit Sheep(item::DyeColor color = item::DyeColor::kWhite);
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  void Die() override;
  std::string GetEntityTypeName() const override { return "Sheep"; }

  bool cut = false;
  item::DyeColor color = item::DyeColor::kWhite;

 private:
  int32_t regrow_time_ = 0;
};

class Slime : public EnemyMob {
 public:
  explicit Slime(int32_t lvl = 1);
  void Tick() override;
  bool Move(int32_t xa, int32_t ya) override;
  void RandomizeWalkDir(bool by_chance) override;
  void Render(gfx::Screen* screen) const override;
  void Die() override;
  std::string GetEntityTypeName() const override { return "Slime"; }

 private:
  int32_t jump_time_ = 0;
};

class Zombie : public EnemyMob {
 public:
  explicit Zombie(int32_t lvl = 1);
  void Die() override;
  std::string GetEntityTypeName() const override { return "Zombie"; }
};

class Skeleton : public EnemyMob {
 public:
  explicit Skeleton(int32_t lvl = 1);
  void Tick() override;
  void Die() override;
  std::string GetEntityTypeName() const override { return "Skeleton"; }

 private:
  int32_t arrow_interval_ = 80;
  int32_t arrow_time_ = 80;
};

class Creeper : public EnemyMob {
 public:
  explicit Creeper(int32_t lvl = 1);
  void Tick() override;
  bool Move(int32_t xa, int32_t ya) override;
  void TouchedBy(Entity& entity) override;
  void Render(gfx::Screen* screen) const override;
  bool CanWool() const override { return false; }
  bool CanBlock() const { return fuse_time_ == 0; }
  bool IsFuseLit() const { return fuse_lit_; }
  int32_t FuseTime() const { return fuse_time_; }
  void Die() override;
  std::string GetEntityTypeName() const override { return "Creeper"; }

 private:
  int32_t fuse_time_ = 0;
  bool fuse_lit_ = false;
};

class Knight : public EnemyMob {
 public:
  explicit Knight(int32_t lvl = 1);
  void Die() override;
  std::string GetEntityTypeName() const override { return "Knight"; }
};

class Snake : public EnemyMob {
 public:
  explicit Snake(int32_t lvl = 1);
  void Die() override;
  std::string GetEntityTypeName() const override { return "Snake"; }
};

class AirWizard : public MobAi {
 public:
  AirWizard();
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool RemoveWhenFarAway() const override { return false; }
  void DoHurt(int32_t dmg, Direction attack_dir) override;
  void TouchedBy(Entity& entity) override;
  void Die() override;
  std::string GetEntityTypeName() const override { return "AirWizard"; }

  int32_t AttackDelay() const { return attack_delay_; }
  int32_t AttackTime() const { return attack_time_; }
  int32_t AttackType() const { return attack_type_; }

 private:
  int32_t attack_delay_ = 0;
  int32_t attack_time_ = 0;
  int32_t attack_type_ = 0;
};

class ObsidianKnight : public MobAi {
 public:
  enum class AttackPhase : int32_t {
    kAttacking = 0,
    kDashing = 1,
    kWalking = 2,
  };

  explicit ObsidianKnight(int32_t max_hp = 5000);
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool RemoveWhenFarAway() const override { return false; }
  bool CanBurn() const override { return false; }
  int32_t CalculateEntityDamage(const Entity* attacker,
                                int32_t dmg) const override;
  void DoHurt(int32_t dmg, Direction attack_dir) override;
  void TouchedBy(Entity& entity) override;
  void Die() override;
  std::string GetEntityTypeName() const override { return "ObsidianKnight"; }

  int32_t Phase() const { return phase_; }
  void SetPhase(int32_t phase);
  AttackPhase CurrentAttackPhase() const { return attack_phase_; }

 private:
  int32_t phase_ = 0;
  AttackPhase attack_phase_ = AttackPhase::kAttacking;
  int32_t attack_phase_cooldown_ = 500;
  int32_t dash_time_ = 0;
  int32_t dash_cooldown_ = 1000;
  int32_t attack_delay_ = 0;
  int32_t attack_time_ = 0;
  int32_t attack_level_ = 0;
};

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_MOB_H_
