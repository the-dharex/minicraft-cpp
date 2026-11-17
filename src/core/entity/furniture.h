#ifndef MINICRAFT_CORE_ENTITY_FURNITURE_H_
#define MINICRAFT_CORE_ENTITY_FURNITURE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "core/entity/direction.h"
#include "core/entity/entity.h"
#include "core/gfx/sprite_atlas.h"

namespace minicraft::item {
class Inventory;
}

namespace minicraft::entity {

class Furniture : public Entity {
 public:
  Furniture(std::string name, gfx::LinkedSprite sprite,
            gfx::LinkedSprite item_sprite, int32_t xr = 3, int32_t yr = 3);
  ~Furniture() override = default;

  std::string name;
  gfx::LinkedSprite sprite;
  gfx::LinkedSprite item_sprite;

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool Blocks(const Entity& e) const override { return e.IsSolid(); }
  void TouchedBy(Entity& entity) override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  bool CanTake(const Player& player) const;
  std::string GetEntityTypeName() const override { return name; }

  virtual std::unique_ptr<Furniture> Clone() const;
  virtual bool CanPickup() const { return true; }

 protected:
  bool should_take_ = false;
  Player* taker_ = nullptr;
};

class Bed : public Furniture {
 public:
  explicit Bed(std::string color_name = "Red");
  bool Use(Player& player) override;
  std::string GetEntityTypeName() const override { return "Bed"; }
  std::unique_ptr<Furniture> Clone() const override;

  const std::string& ColorName() const { return color_name_; }

 private:
  std::string color_name_;
};

class Chest : public Furniture {
 public:
  explicit Chest(std::string name = "Chest",
                 std::string sprite_key = "chest",
                 std::string item_sprite_key = "chest");
  ~Chest() override;

  bool Use(Player& player) override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  void Remove() override;
  std::string GetEntityTypeName() const override { return "Chest"; }
  std::unique_ptr<Furniture> Clone() const override;

  std::shared_ptr<item::Inventory> GetInventory() const { return inventory_; }
  void PopulateInvRandom(const std::string& loot_file, int32_t depth_bonus);

 protected:
  std::shared_ptr<item::Inventory> inventory_;
  std::string sprite_key_;
};

class DeathChest : public Chest {
 public:
  DeathChest();
  explicit DeathChest(Player& player);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool Use(Player& player) override { return false; }
  void TouchedBy(Entity& entity) override;
  std::string GetEntityTypeName() const override { return "DeathChest"; }
  bool CanPickup() const override { return false; }

  int32_t Time() const { return time_; }
  void SetTime(int32_t t) { time_ = t; }

 private:
  int32_t time_ = 60 * 300;
  int32_t red_tick_ = 0;
  bool reverse_ = false;
};

class DungeonChest : public Chest {
 public:
  explicit DungeonChest(bool populate = true, bool locked = true);

  bool Use(Player& player) override;
  void TouchedBy(Entity& entity) override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  std::string GetEntityTypeName() const override { return "DungeonChest"; }
  std::unique_ptr<Furniture> Clone() const override;

  bool is_locked = true;
};

class Crafter : public Furniture {
 public:
  enum class Type : int32_t {
    kWorkbench = 0,
    kFurnace = 1,
    kOven = 2,
    kAnvil = 3,
    kEnchanter = 4,
    kLoom = 5,
    kDyeVat = 6,
  };

  explicit Crafter(Type type);
  bool Use(Player& player) override;
  std::string GetEntityTypeName() const override;
  std::unique_ptr<Furniture> Clone() const override;

  Type GetType() const { return type_; }

 private:
  Type type_;
};

class Lantern : public Furniture {
 public:
  enum class Type : int32_t {
    kNorm = 0,
    kIron = 1,
    kGold = 2,
  };

  explicit Lantern(Type type = Type::kNorm);
  int32_t GetLightRadius() const override;
  std::string GetEntityTypeName() const override;
  std::unique_ptr<Furniture> Clone() const override;

  Type GetType() const { return type_; }

 private:
  Type type_;
};

class Composter : public Furniture {
 public:
  Composter();
  bool Use(Player& player) override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  std::string GetEntityTypeName() const override { return "Composter"; }
  std::unique_ptr<Furniture> Clone() const override;

  int32_t compost = 0;
  static constexpr int32_t kMaxCompost = 7;

 private:
  void UpdateVisual();
};

class KnightStatue : public Furniture {
 public:
  explicit KnightStatue(int32_t boss_hp = 5000);
  void Tick() override;
  bool Use(Player& player) override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  void TryPush(Player& player) override {}
  void Hurt(Mob& mob, int32_t dmg, Direction attack_dir) override;
  std::string GetEntityTypeName() const override { return "KnightStatue"; }
  bool CanPickup() const override { return false; }

  int32_t BossHp() const { return boss_hp_; }
  void SetBossHp(int32_t hp) { boss_hp_ = hp; }

 private:
  int32_t boss_hp_ = 5000;
  int32_t hp_ = 100;
  int32_t touches_ = 0;
};

class Spawner : public Furniture {
 public:
  Spawner(std::string mob_type = "Zombie", int32_t mob_lvl = 1);
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  void Hurt(Mob& mob, int32_t dmg, Direction attack_dir) override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  bool Use(Player& player) override;
  std::string GetEntityTypeName() const override { return "Spawner"; }
  std::unique_ptr<Furniture> Clone() const override;
  bool CanPickup() const override { return false; }

  const std::string& MobType() const { return mob_type_; }
  int32_t MobLvl() const { return mob_lvl_; }
  int32_t SpawnTick() const { return spawn_tick_; }
  void SetSpawnTick(int32_t t) { spawn_tick_ = t; }

 private:
  void ResetSpawnInterval();
  void TrySpawn();

  std::string mob_type_;
  int32_t mob_lvl_ = 1;
  int32_t health_ = 100;
  int32_t spawn_tick_ = 0;
};

class Tnt : public Furniture {
 public:
  Tnt();
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool Interact(Player& player, item::Item* item,
                Direction attack_dir) override;
  void Hurt(Mob& mob, int32_t dmg, Direction attack_dir) override;
  bool Use(Player& player) override;
  std::string GetEntityTypeName() const override { return "Tnt"; }
  std::unique_ptr<Furniture> Clone() const override;

  bool fuse_lit = false;
  int32_t Ftik() const { return ftik_; }

 private:
  void Explode();
  int32_t ftik_ = 0;
};

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_FURNITURE_H_
