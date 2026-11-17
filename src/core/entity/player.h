#ifndef MINICRAFT_CORE_ENTITY_PLAYER_H_
#define MINICRAFT_CORE_ENTITY_PLAYER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/entity/direction.h"
#include "core/entity/mob.h"

namespace minicraft {
namespace io {
class InputHandler;
}
namespace item {
class ArmorItem;
class Inventory;
class Item;
}  // namespace item
}  // namespace minicraft

namespace minicraft::entity {

class Boat;
class ItemEntity;

class Player : public Mob {
 public:
  Player(std::shared_ptr<Player> previous_instance = nullptr,
         io::InputHandler* input = nullptr);
  ~Player() override;

  static constexpr int32_t kMaxHealth = 10;
  static constexpr int32_t kMaxTotalHealth = 30;
  static constexpr int32_t kMaxStamina = 10;
  static constexpr int32_t kMaxHunger = 10;
  static constexpr int32_t kMaxArmor = 100;

  int32_t stamina = kMaxStamina;
  int32_t max_stamina = kMaxStamina;
  int32_t hunger = kMaxHunger;
  int32_t max_hunger = kMaxHunger;
  int32_t armor = 0;
  int32_t armor_damage_buffer = 0;
  int32_t shirt_color = 0;
  bool skinon = false;

  int32_t spawnx = 64 * 16 + 8;
  int32_t spawny = 64 * 16 + 8;
  int32_t spawn_depth = 0;

  std::shared_ptr<item::Item> active_item;
  std::shared_ptr<item::ArmorItem> cur_armor;

  bool is_fishing = false;
  int32_t max_fishing_ticks = 120;
  int32_t fishing_ticks = max_fishing_ticks;
  int32_t fishing_level = 0;

  // Potion effect IDs matching PotionItem types
  enum class PotionEffect : int32_t {
    kSpeed = 0,
    kLight = 1,
    kSwim = 2,
    kEnergy = 3,
    kRegen = 4,
    kHealth = 5,
    kTime = 6,
    kLava = 7,
    kShield = 8,
    kHaste = 9,
  };
  std::unordered_map<int32_t, int32_t> potion_effects;

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool Move(int32_t xa, int32_t ya) override;
  bool CanSwim() const override { return true; }
  bool CanWool() const override { return true; }
  bool CanLight() const override { return true; }
  int32_t GetLightRadius() const override;
  bool Blocks(const Entity& e) const override { return e.IsBlockableBy(*this); }
  bool IsBlockableBy(const Mob& mob) const override { return true; }

  void DoHurt(int32_t dmg, Direction attack_dir) override;
  void Die() override;
  std::string GetEntityTypeName() const override { return "Player"; }

  void SetInput(io::InputHandler* input) { input_ = input; }
  io::InputHandler* GetInput() const { return input_; }

  item::Inventory& GetInventory() { return *inventory_; }
  const item::Inventory& GetInventory() const { return *inventory_; }
  std::shared_ptr<item::Inventory> GetInventoryPtr() const {
    return inventory_;
  }
  level::Level* GetLevel() const { return level; }
  bool AddItemToInventory(std::unique_ptr<item::Item> item);
  void TryAddToInvOrDrop(std::unique_ptr<item::Item> item);
  void PickupItem(ItemEntity& item_entity);

  int32_t GetScore() const { return score_; }
  void AddScore(int32_t points);
  void SetScore(int32_t s) { score_ = s; }

  bool PayStamina(int32_t cost);
  bool HasSwimEffect() const;
  bool HasLavaEffect() const;
  void AddPotionEffect(PotionEffect type, int32_t duration_ticks);

  void SetSpawnPosition(int32_t x, int32_t y, int32_t depth);
  void ChangeLevel(int32_t dir);

  void EnterBoat(Boat* boat);
  void ExitBoat();
  Boat* GetBoat() const { return boat_; }

  bool IsAttacking() const { return attack_time_ > 0; }
  int32_t AttackTime() const { return attack_time_; }
  Direction AttackDir() const { return attack_dir_; }

  void ResetForRespawn();
  void SetInventory(std::shared_ptr<item::Inventory> inv) {
    inventory_ = std::move(inv);
  }
  bool UseNearby();
  void ResetFishing();

 private:
  void Attack();
  bool InteractWithTiles(int32_t xt, int32_t yt);
  bool UseEntity(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
  bool InteractWithEntities(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
  void HurtEntities(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
  int32_t GetAttackDamage(Entity* target) const;
  void GoFishing();

  io::InputHandler* input_ = nullptr;
  std::shared_ptr<item::Inventory> inventory_;
  int32_t score_ = 0;

  int32_t stamina_recharge_ = 0;
 public:
  int32_t StaminaRechargeDelay() const { return stamina_recharge_delay_; }

 private:
  int32_t stamina_recharge_delay_ = 0;
  int32_t hunger_stam_cnt_ = 0;
  int32_t hunger_tick_cnt_ = 0;

  int32_t attack_time_ = 0;
  Direction attack_dir_ = Direction::kDown;
  std::shared_ptr<item::Item> attack_item_;

  int32_t on_stair_delay_ = 0;
  int32_t step_sound_timer_ = 0;
  Boat* boat_ = nullptr;
};

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_PLAYER_H_
