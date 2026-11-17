#include "core/entity/player.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/entity/boat.h"
#include "core/entity/furniture.h"
#include "core/entity/particles.h"
#include "core/entity/projectiles.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/screen.h"
#include "core/gfx/sprite_atlas.h"
#include "core/io/input_handler.h"
#include "core/io/sound.h"
#include "core/item/fishing_data.h"
#include "core/item/inventory.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/advancements.h"
#include "core/util/random.h"

namespace minicraft::entity {

Player::Player(std::shared_ptr<Player> previous_instance,
               io::InputHandler* input)
    : Mob(4, 3, kMaxHealth),
      shirt_color(gfx::color::Get(1, 51, 51, 0)),
      input_(input),
      inventory_(std::make_shared<item::Inventory>()) {
  if (previous_instance) {
    x = previous_instance->x;
    y = previous_instance->y;
    health = previous_instance->health;
    max_health = previous_instance->max_health;
    stamina = previous_instance->stamina;
    hunger = previous_instance->hunger;
    armor = previous_instance->armor;
    armor_damage_buffer = previous_instance->armor_damage_buffer;
    cur_armor = previous_instance->cur_armor;
    active_item = previous_instance->active_item;
    inventory_ = previous_instance->inventory_;
    score_ = previous_instance->score_;
    shirt_color = previous_instance->shirt_color;
    skinon = previous_instance->skinon;
    spawnx = previous_instance->spawnx;
    spawny = previous_instance->spawny;
    spawn_depth = previous_instance->spawn_depth;
    potion_effects = previous_instance->potion_effects;
  } else {
    // Starter gear matching Java Minicraft+
    inventory_->Add(item::Items::Get("Workbench"));
    active_item.reset();
  }
}

Player::~Player() = default;

void Player::ResetForRespawn() {
  health = max_health;
  stamina = max_stamina;
  hunger = max_hunger;
  hurt_time = 20;
  removed = false;
  potion_effects.clear();
  if (!inventory_ || inventory_->InvSize() == 0) {
    inventory_ = std::make_shared<item::Inventory>();
    inventory_->Add(item::Items::Get("Workbench"));
    // Do not grant legacy Power Glove in inventory; pickup uses V/P directly.
  }
}

void Player::AddScore(int32_t points) {
  score_ += points;
  game::GameState::Instance().score = score_;
}

bool Player::AddItemToInventory(std::unique_ptr<item::Item> it) {
  if (!it || !inventory_) return false;
  auto leftover = inventory_->Add(std::move(it));
  util::AdvancementSystem::Instance().TriggerInventoryChanged(
      inventory_->ToSnapshot());
  return leftover == nullptr;
}

void Player::TryAddToInvOrDrop(std::unique_ptr<item::Item> it) {
  if (!it) return;
  std::unique_ptr<item::Item> leftover =
      inventory_ ? inventory_->Add(std::move(it)) : std::move(it);
  if (leftover && level) {
    level->DropItem(x, y, std::move(leftover));
  }
  if (inventory_) {
    util::AdvancementSystem::Instance().TriggerInventoryChanged(
        inventory_->ToSnapshot());
  }
}

void Player::PickupItem(ItemEntity& item_entity) {
  if (!item_entity.item || !inventory_) return;
  if (game::GameState::Instance().IsCreativeMode() &&
      inventory_->Count(*item_entity.item) >= 1) {
    item_entity.Take(*this);
    return;
  }
  auto leftover = inventory_->Add(std::move(item_entity.item));
  if (!leftover) {
    util::AdvancementSystem::Instance().TriggerInventoryChanged(
        inventory_->ToSnapshot());
    item_entity.Take(*this);
  } else {
    item_entity.item = std::move(leftover);
  }
}

bool Player::PayStamina(int32_t cost) {
  if (game::GameState::Instance().IsCreativeMode()) return true;
  if (potion_effects.count(static_cast<int32_t>(PotionEffect::kEnergy)) > 0) {
    return true;
  }
  if (stamina <= 0) return false;
  if (cost < 0) cost = 0;
  stamina -= std::min(stamina, cost);
  stamina_recharge_ = 0;
  return true;
}

bool Player::HasSwimEffect() const {
  auto it = potion_effects.find(static_cast<int32_t>(PotionEffect::kSwim));
  return it != potion_effects.end() && it->second > 0;
}

bool Player::HasLavaEffect() const {
  auto it = potion_effects.find(static_cast<int32_t>(PotionEffect::kLava));
  return it != potion_effects.end() && it->second > 0;
}

void Player::AddPotionEffect(PotionEffect type, int32_t duration_ticks) {
  potion_effects[static_cast<int32_t>(type)] = duration_ticks;
}

int32_t Player::GetLightRadius() const {
  int32_t r = 2;
  if (auto* furn = dynamic_cast<item::FurnitureItem*>(active_item.get())) {
    if (furn->furniture) {
      int32_t rr = furn->furniture->GetLightRadius();
      if (rr > r) r = rr;
    }
  }
  if (active_item && active_item->Name() == "Torch") {
    r = std::max(r, 6);
  }
  auto it = potion_effects.find(static_cast<int32_t>(PotionEffect::kLight));
  if (it != potion_effects.end() && it->second > 0) {
    r = std::max(r, 8);
  }
  return r;
}

void Player::SetSpawnPosition(int32_t nx, int32_t ny, int32_t depth) {
  spawnx = nx;
  spawny = ny;
  spawn_depth = depth;
}

void Player::ChangeLevel(int32_t dir) {
  game::GameState::Instance().ScheduleLevelChange(dir);
}

void Player::EnterBoat(Boat* boat) {
  boat_ = boat;
  if (boat_) {
    x = boat_->x;
    y = boat_->y;
  }
}

void Player::ExitBoat() { boat_ = nullptr; }

void Player::Tick() {
  Mob::Tick();
  if (removed) return;

  auto& gs = game::GameState::Instance();
  if (on_stair_delay_ > 0) --on_stair_delay_;

  // Update active potion timers (paused while in bed, like Java)
  if (!gs.bed_sleeping) {
    for (auto it = potion_effects.begin(); it != potion_effects.end();) {
      --it->second;
      if (it->second <= 0) {
        it = potion_effects.erase(it);
      } else {
        if (it->first == static_cast<int32_t>(PotionEffect::kRegen) &&
            tick_time % 60 == 0 && health < max_health) {
          ++health;
        }
        ++it;
      }
    }
  }

  // Stamina recharge (40-tick exhaustion delay only when stamina runs out)
  if (stamina <= 0 && stamina_recharge_delay_ == 0 && stamina_recharge_ == 0) {
    stamina_recharge_delay_ = 40;
  }
  if (stamina_recharge_delay_ > 0 && stamina < max_stamina) {
    --stamina_recharge_delay_;
  }
  if (stamina_recharge_delay_ == 0) {
    int32_t rate = 1;
    if (potion_effects.count(static_cast<int32_t>(PotionEffect::kEnergy))) {
      rate = 2;
    }
    stamina_recharge_ += rate;
    if (IsSwimming() && !HasSwimEffect() && boat_ == nullptr) {
      stamina_recharge_ = 0;
    }
    while (stamina_recharge_ > 10) {
      stamina_recharge_ -= 10;
      if (stamina < max_stamina) {
        ++stamina;
      }
    }
  }

  // Hunger decay (disabled in Creative or while sleeping in bed)
  if (!gs.bed_sleeping && !gs.IsCreativeMode() && gs.GetDifficultyIdx() > 0) {
    if (++hunger_tick_cnt_ >= 60 * 45) {
      hunger_tick_cnt_ = 0;
      if (hunger > 0) {
        --hunger;
      } else if (health > 1) {
        DoHurt(1, Direction::kNone);
      }
    }
  }

  // Tile hazards under player (lava damage)
  if (level && !gs.bed_sleeping) {
    const uint16_t under = level->GetTile(x >> 4, y >> 4)->id;
    if (under == level::Tiles::kLava && !HasLavaEffect() &&
        !gs.IsCreativeMode() && tick_time % 30 == 0) {
      DoHurt(2, Direction::kNone);
    }
  }

  if (attack_time_ > 0) {
    --attack_time_;
    if (attack_time_ == 0) {
      attack_item_.reset();
    }
  }

  if (is_fishing) {
    if (!gs.bed_sleeping && !IsSwimming() &&
        dynamic_cast<item::FishingRodItem*>(active_item.get()) != nullptr) {
      --fishing_ticks;
      if (fishing_ticks <= 0) {
        GoFishing();
      }
    } else {
      ResetFishing();
    }
  }

  if (gs.bed_sleeping || !input_) return;

  // Movement
  int32_t xa = 0;
  int32_t ya = 0;
  if (input_->InputDown("move-up")) --ya;
  if (input_->InputDown("move-down")) ++ya;
  if (input_->InputDown("move-left")) --xa;
  if (input_->InputDown("move-right")) ++xa;

  const int32_t speed_mult =
      potion_effects.count(static_cast<int32_t>(PotionEffect::kSpeed)) ? 2 : 1;

  if (boat_ != nullptr) {
    if (xa != 0 || ya != 0) {
      Direction bdir = GetDirection(xa, ya);
      if (bdir != Direction::kNone) dir = bdir;
    }
    boat_->Drive(xa, ya, dir);
    x = boat_->x;
    y = boat_->y;
    if (input_->InputPressed("attack")) {
      boat_->Use(*this);
    }
    return;
  }

  if (xa != 0 || ya != 0) {
    Move(xa * speed_mult, ya * speed_mult);
    if (level && ++step_sound_timer_ >= 16) {
      step_sound_timer_ = 0;
      const uint16_t tid = level->GetTile(x >> 4, y >> 4)->id;
      if (tid == level::Tiles::kSand && util::GlobalRandom().NextInt(2) == 0) {
        level->Add(std::make_shared<SandParticle>(x, y + 3));
      } else if (tid == level::Tiles::kWater) {
        level->Add(std::make_shared<WaterParticle>(x, y));
      }
    }
  }

  if (input_->InputPressed("attack")) {
    Attack();
  }
  if (input_->InputPressed("pickup") &&
      (active_item == nullptr || !active_item->used_pending)) {
    ResetFishing();
    std::shared_ptr<item::Item> prev_item;
    if (std::dynamic_pointer_cast<item::PowerGloveItem>(active_item) ==
        nullptr) {
      prev_item = active_item;
      active_item = std::make_shared<item::PowerGloveItem>();
    }
    Attack();
    if (std::dynamic_pointer_cast<item::PowerGloveItem>(active_item) ==
        nullptr) {
      if (prev_item) {
        TryAddToInvOrDrop(prev_item->Clone());
      }
    } else {
      active_item = prev_item;
    }
    if (std::dynamic_pointer_cast<item::PowerGloveItem>(active_item) !=
        nullptr) {
      active_item.reset();
    }
  }
}

bool Player::Move(int32_t xa, int32_t ya) {
  const bool moved = Mob::Move(xa, ya);
  if (level && on_stair_delay_ == 0) {
    const uint16_t tid = level->GetTile(x >> 4, y >> 4)->id;
    if (tid == level::Tiles::kStairsUp) {
      on_stair_delay_ = 25;
      ChangeLevel(+1);
    } else if (tid == level::Tiles::kStairsDown) {
      on_stair_delay_ = 25;
      ChangeLevel(-1);
    }
  }
  return moved;
}

void Player::Attack() {
  if (stamina <= 0 && !game::GameState::Instance().IsCreativeMode()) return;
  if (active_item != nullptr && active_item->used_pending) return;

  PayStamina(1);
  walk_dist += 8;

  if (is_fishing) {
    ResetFishing();
  }

  attack_dir_ = dir;
  attack_item_ = active_item;

  if (!level) return;

  if (active_item != nullptr && !active_item->InteractsWithWorld()) {
    active_item->InteractOn(
        const_cast<level::Tile*>(level::Tiles::Get(level::Tiles::kRock)),
        level, 0, 0, this, dir);
    if (active_item->IsDepleted()) {
      if (inventory_) inventory_->RemoveItem(*active_item);
      active_item.reset();
      ResetFishing();
    }
    return;
  }

  const int32_t range = 12;
  const int32_t attack_range = 20;
  const int32_t yo = -2;
  const int32_t dx = DirX(dir);
  const int32_t dy = DirY(dir);

  if (active_item != nullptr) {
    attack_time_ = 10;
    // Shoot bow if bow is active & player has arrows
    if (auto* tool = dynamic_cast<item::ToolItem*>(active_item.get())) {
      if (tool->type == item::ToolType::kBow && tool->dur > 0 && inventory_) {
        auto arrow = item::Items::Get("Arrow");
        if (game::GameState::Instance().IsCreativeMode() ||
            (arrow && inventory_->Count(*arrow) > 0)) {
          if (!game::GameState::Instance().IsCreativeMode() && arrow) {
            inventory_->RemoveItems(*arrow, 1);
          }
          io::Sound::Play("playerhurt");
          level->Add(std::make_shared<Arrow>(this, dir, 3 + tool->level));
          tool->PayDurability();
          if (tool->IsDepleted()) {
            inventory_->RemoveItem(*active_item);
            active_item.reset();
          }
          util::AchievementManager::Instance().SetAchievement(
              "minicraft.achievement.bow", true,
              game::GameState::Instance().IsCreativeMode());
          return;
        }
      }
    }

    // First check entity interact (furniture pickup, sheep shear, unlocked chest key, etc.)
    const int32_t x0 = x + dx * range - 8;
    const int32_t y0 = y + yo + dy * range - 8;
    const int32_t x1 = x + dx * range + 8;
    const int32_t y1 = y + yo + dy * range + 8;

    if (InteractWithEntities(x0, y0, x1, y1)) {
      if (active_item && active_item->IsDepleted()) {
        if (inventory_) inventory_->RemoveItem(*active_item);
        active_item.reset();
        ResetFishing();
      }
      return;
    }

    const int32_t xt = (x + dx * range) >> 4;
    const int32_t yt = (y + yo + dy * range) >> 4;
    bool tile_blocked_by_entity = false;
    for (Entity* e : level->GetEntitiesInTiles(xt, yt, xt, yt)) {
      if (e != this && dynamic_cast<ItemEntity*>(e) == nullptr &&
          dynamic_cast<Particle*>(e) == nullptr) {
        tile_blocked_by_entity = true;
        break;
      }
    }
    if (!tile_blocked_by_entity && InteractWithTiles(xt, yt)) {
      return;
    }
    if (active_item && active_item->IsDepleted()) {
      if (inventory_) inventory_->RemoveItem(*active_item);
      active_item.reset();
      ResetFishing();
    }
  }

  if (active_item == nullptr || active_item->CanAttack()) {
    attack_time_ = 5;
    const int32_t hx0 = (dx == 0) ? (x - 8) : (x + std::min(dx * 4, dx * attack_range));
    const int32_t hy0 =
        (dy == 0) ? (y + yo - 8) : (y + yo + std::min(dy * 4, dy * attack_range));
    const int32_t hx1 = (dx == 0) ? (x + 8) : (x + std::max(dx * 4, dx * attack_range));
    const int32_t hy1 =
        (dy == 0) ? (y + yo + 8) : (y + yo + std::max(dy * 4, dy * attack_range));
    HurtEntities(hx0, hy0, hx1, hy1);

    const int32_t xt = (x + dx * range) >> 4;
    const int32_t yt = (y + yo + dy * range) >> 4;
    const level::Tile* tile = level->GetTile(xt, yt);
    bool tile_used = false;
    if (active_item == nullptr &&
        tile->Interact(*level, xt, yt, *this, nullptr, dir)) {
      tile_used = true;
    }
    const int32_t dmg = 1 + util::GlobalRandom().NextInt(3);
    const bool tile_hurt =
        !tile_used && tile->Hurt(*level, xt, yt, *this, dmg, dir);
    if (tile_hurt) {
      if (auto* tool = dynamic_cast<item::ToolItem*>(active_item.get())) {
        tool->PayDurability();
      }
    }
    if (active_item && active_item->IsDepleted()) {
      if (inventory_) inventory_->RemoveItem(*active_item);
      active_item.reset();
    }
  }
}

void Player::ResetFishing() {
  is_fishing = false;
  fishing_ticks = max_fishing_ticks;
}

void Player::GoFishing() {
  if (!level) {
    ResetFishing();
    return;
  }
  auto& rng = util::GlobalRandom();
  const int32_t fcatch = rng.NextInt(100);
  bool caught = false;

  const std::vector<std::string>* data = nullptr;
  if (fcatch > item::FishingRodItem::GetChance(0, fishing_level)) {
    data = &item::FishingData::FishData();
  } else if (fcatch > item::FishingRodItem::GetChance(1, fishing_level)) {
    data = &item::FishingData::JunkData();
  } else if (fcatch > item::FishingRodItem::GetChance(2, fishing_level)) {
    data = &item::FishingData::ToolData();
  } else if (fcatch >= item::FishingRodItem::GetChance(3, fishing_level)) {
    data = &item::FishingData::RareData();
  }

  if (data != nullptr) {
    for (const std::string& line : *data) {
      const size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      int32_t chance = 0;
      try {
        chance = std::stoi(line.substr(0, colon));
      } catch (...) {
        continue;
      }
      std::string item_data = line.substr(colon + 1);
      if (rng.NextInt(100) + 1 <= chance) {
        if (item_data.find(',') != std::string::npos) {
          std::vector<std::string> opts;
          size_t start = 0;
          while (start <= item_data.size()) {
            size_t comma = item_data.find(',', start);
            if (comma == std::string::npos) comma = item_data.size();
            opts.push_back(item_data.substr(start, comma - start));
            start = comma + 1;
          }
          if (!opts.empty()) {
            item_data = opts[rng.NextInt(static_cast<int32_t>(opts.size()))];
          }
        }
        if (!item_data.empty() && item_data[0] == ';') {
          game::GameState::Instance().AddNotification(item_data.substr(1));
        } else {
          auto dropped = item::Items::Get(item_data);
          if (dropped && dropped->Name() == "Raw Fish") {
            util::AchievementManager::Instance().SetAchievement(
                "minicraft.achievement.fish", true,
                game::GameState::Instance().IsCreativeMode());
          }
          if (dropped) {
            level->DropItem(x, y, std::move(dropped));
          }
          caught = true;
          break;
        }
      }
    }
  } else {
    caught = true;
  }

  if (caught) {
    is_fishing = false;
  }
  fishing_ticks = max_fishing_ticks;
}

bool Player::UseNearby() {
  if (!level) return false;
  const int32_t dx = DirX(dir);
  const int32_t dy = DirY(dir);
  const int32_t yo = -2;

  if (UseEntity(x + dx * 12 - 8, y + yo + dy * 12 - 8, x + dx * 12 + 8,
                y + yo + dy * 12 + 8)) {
    return true;
  }
  return false;
}

bool Player::UseEntity(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  std::vector<Entity*> entities = level->GetEntitiesInRect(
      gfx::Rectangle(x0, y0, x1, y1, gfx::Rectangle::kCorners));
  for (Entity* e : entities) {
    if (e != this && e != boat_ && e->Use(*this)) {
      return true;
    }
  }
  return false;
}

bool Player::InteractWithEntities(int32_t x0, int32_t y0, int32_t x1,
                                  int32_t y1) {
  std::vector<Entity*> entities = level->GetEntitiesInRect(
      gfx::Rectangle(x0, y0, x1, y1, gfx::Rectangle::kCorners));
  for (Entity* e : entities) {
    if (e != this && e->Interact(*this, active_item.get(), dir)) {
      return true;
    }
  }
  return false;
}

bool Player::InteractWithTiles(int32_t xt, int32_t yt) {
  const level::Tile* tile = level->GetTile(xt, yt);
  auto check_deplete = [&]() {
    if (active_item && active_item->IsDepleted()) {
      if (inventory_) {
        inventory_->RemoveItem(*active_item);
      }
      active_item.reset();
    }
  };
  if (active_item &&
      active_item->InteractOn(const_cast<level::Tile*>(tile), level, xt, yt,
                              this, dir)) {
    check_deplete();
    return true;
  }
  if (tile->Interact(*level, xt, yt, *this, active_item.get(), dir)) {
    check_deplete();
    return true;
  }
  return false;
}

int32_t Player::GetAttackDamage(Entity* target) const {
  auto& rng = util::GlobalRandom();
  int32_t dmg = rng.NextInt(3) + 1;
  if (active_item) {
    dmg += active_item->GetAttackDamageBonus(target);
  }
  return dmg;
}

void Player::HurtEntities(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  std::vector<Entity*> entities = level->GetEntitiesInRect(
      gfx::Rectangle(x0, y0, x1, y1, gfx::Rectangle::kCorners));
  for (Entity* e : entities) {
    if (e == this) continue;
    if (dynamic_cast<Mob*>(e) != nullptr || dynamic_cast<Boat*>(e) != nullptr) {
      const int32_t dmg = GetAttackDamage(e);
      e->Hurt(*this, dmg, dir);
    } else if (dynamic_cast<Furniture*>(e) != nullptr) {
      e->Interact(*this, nullptr, dir);
    }
  }
}

void Player::DoHurt(int32_t dmg, Direction attack_dir) {
  const auto& gs = game::GameState::Instance();
  if (hurt_time > 0 || gs.IsCreativeMode() || gs.bed_sleeping) return;

  io::Sound::Play("playerhurt");

  // Apply equipped armor absorption
  int32_t armor_absorb = 0;
  if (cur_armor != nullptr && armor > 0) {
    int32_t take = std::min(dmg, armor);
    armor_damage_buffer += take * 2;
    armor -= take;
    armor_absorb = take / 2;
    if (armor <= 0) {
      cur_armor.reset();
    }
  }
  int32_t final_dmg = std::max(1, dmg - armor_absorb);
  Mob::DoHurt(final_dmg, attack_dir);
  hurt_time = 30;
}

void Player::Die() {
  auto& gs = game::GameState::Instance();
  if (level && !gs.IsHardcoreMode() && !gs.IsCreativeMode() && inventory_ &&
      inventory_->InvSize() > 0) {
    level->Add(std::make_shared<DeathChest>(*this));
  }
  gs.player_dead = true;
  io::Sound::Play("death");
  gs.TriggerPlayerDeath();
  Remove();
}

void Player::Render(gfx::Screen* screen) const {
  if (!screen || game::GameState::Instance().bed_sleeping) return;

  const int32_t xo = x - 8;
  int32_t yo = y - 11;

  const bool swimming = IsSwimming() && boat_ == nullptr;
  if (swimming) {
    yo += 4;
  }

  // Player sprite is loaded from default_skins (256x256 sheet; top-left 64x128
  // holds 4 default skins stacked vertically, 4 tiles tall each: normal + carry).
  gfx::LinkedSprite spr(gfx::SpriteType::kMisc, "default_skins");
  int32_t xt = 0;
  int32_t mirror = 0;
  const int32_t walk_frame = (walk_dist >> 3) & 1;
  switch (dir) {
    case Direction::kDown:
      xt = 0;
      mirror = walk_frame;
      break;
    case Direction::kUp:
      xt = 2;
      mirror = walk_frame;
      break;
    case Direction::kRight:
      xt = 4 + 2 * walk_frame;
      mirror = 0;
      break;
    case Direction::kLeft:
      xt = 4 + 2 * walk_frame;
      mirror = 1;
      break;
    default:
      break;
  }

  const bool carrying =
      dynamic_cast<const item::FurnitureItem*>(active_item.get()) != nullptr;
  const auto& gs = game::GameState::Instance();
  const int32_t skin_idx = std::max(0, gs.selected_skin_index);
  int32_t yt = (skin_idx % 4) * 4 + (carrying ? 2 : 0);
  if (skin_idx >= 4 && !gs.selected_skin_name.empty() && screen->Linker() &&
      screen->Linker()->GetSheet(gfx::SpriteType::kEntity,
                                 "skin." + gs.selected_skin_name)) {
    spr = gfx::LinkedSprite(gfx::SpriteType::kEntity,
                            "skin." + gs.selected_skin_name);
    yt = carrying ? 2 : 0;
  }

  if (swimming && level) {
    const uint16_t tid = level->GetTile(x >> 4, y >> 4)->id;
    const int32_t splash_row = ((tick_time / 8) & 1);
    const int32_t splash_col = (tid == level::Tiles::kLava) ? 6 : 5;
    gfx::LinkedSprite hud_l(gfx::SpriteType::kGui, "hud");
    gfx::LinkedSprite hud_r(gfx::SpriteType::kGui, "hud");
    hud_l.SetSpritePos(splash_col, splash_row).SetSpriteSize(1, 1).SetMirror(0);
    hud_r.SetSpritePos(splash_col, splash_row).SetSpriteSize(1, 1).SetMirror(1);
    screen->Render(xo, yo + 3, hud_l);
    screen->Render(xo + 8, yo + 3, hud_r);
  }

  // Render placement guide indicator when holding a TileItem and not swimming.
  if (dynamic_cast<const item::TileItem*>(active_item.get()) != nullptr &&
      !swimming) {
    const gfx::SheetRect* hud_sheet =
        screen->Linker()
            ? screen->Linker()->GetSheet(gfx::SpriteType::kGui, "hud")
            : nullptr;
    if (hud_sheet) {
      const int32_t itx = (x + DirX(dir) * 12) >> 4;
      const int32_t ity = (y - 2 + DirY(dir) * 12) >> 4;
      screen->Render(itx * 16, ity * 16, 3, 2, 0, hud_sheet);
      screen->Render(itx * 16 + 8, ity * 16, 3, 2, 1, hud_sheet);
      screen->Render(itx * 16, ity * 16 + 8, 3, 2, 2, hud_sheet);
      screen->Render(itx * 16 + 8, ity * 16 + 8, 3, 2, 3, hud_sheet);
    }
  }

  if (swimming) {
    // Only draw the upper half of the player when swimming.
    spr.SetSpritePos(xt, yt).SetSpriteSize(2, 1).SetMirror(mirror).SetFlip(mirror);
  } else {
    spr.SetSpritePos(xt, yt).SetSpriteSize(2, 2).SetMirror(mirror).SetFlip(mirror);
  }
  if (hurt_time > 20) {
    spr.SetColor(gfx::color::kWhite);
  }
  screen->Render(xo, yo, spr);

  // Render attack slash and held attack item
  if (attack_time_ > 0) {
    const gfx::SheetRect* hud_sheet =
        screen->Linker()
            ? screen->Linker()->GetSheet(gfx::SpriteType::kGui, "hud")
            : nullptr;
    const bool show_item =
        attack_item_ != nullptr &&
        dynamic_cast<const item::PowerGloveItem*>(attack_item_.get()) ==
            nullptr;
    gfx::LinkedSprite as;
    if (show_item) {
      as = attack_item_->sprite;
    }
    switch (attack_dir_) {
      case Direction::kUp:
        if (hud_sheet) {
          screen->Render(xo + 0, yo - 4, 3, 0, 0, hud_sheet);
          screen->Render(xo + 8, yo - 4, 3, 0, 1, hud_sheet);
        }
        if (show_item && screen->Linker()) {
          screen->Render(xo + 4, yo - 4, as.GetSprite(*screen->Linker()), 1,
                         false, 0);
        }
        break;
      case Direction::kLeft:
        if (hud_sheet) {
          screen->Render(xo - 4, yo, 4, 0, 1, hud_sheet);
          screen->Render(xo - 4, yo + 8, 4, 0, 3, hud_sheet);
        }
        if (show_item && screen->Linker()) {
          screen->Render(xo - 4, yo + 4, as.GetSprite(*screen->Linker()), 1,
                         false, 0);
        }
        break;
      case Direction::kRight:
        if (hud_sheet) {
          screen->Render(xo + 8 + 4, yo, 4, 0, 0, hud_sheet);
          screen->Render(xo + 8 + 4, yo + 8, 4, 0, 2, hud_sheet);
        }
        if (show_item && screen->Linker()) {
          screen->Render(xo + 8 + 4, yo + 4, as.GetSprite(*screen->Linker()),
                         0, false, 0);
        }
        break;
      case Direction::kDown:
        if (hud_sheet) {
          screen->Render(xo + 0, yo + 8 + 4, 3, 0, 2, hud_sheet);
          screen->Render(xo + 8, yo + 8 + 4, 3, 0, 3, hud_sheet);
        }
        if (show_item && screen->Linker()) {
          screen->Render(xo + 4, yo + 8 + 4, as.GetSprite(*screen->Linker()), 0,
                         false, 0);
        }
        break;
      default:
        break;
    }
  }

  // Render active fishing rod when fishing
  if (is_fishing && active_item) {
    gfx::LinkedSprite rs = active_item->sprite;
    switch (dir) {
      case Direction::kUp:
        rs.SetMirror(1);
        screen->Render(xo + 4, yo - 4, rs);
        break;
      case Direction::kLeft:
        rs.SetMirror(1);
        screen->Render(xo - 4, yo + 4, rs);
        break;
      case Direction::kRight:
        rs.SetMirror(0);
        screen->Render(xo + 8 + 4, yo + 4, rs);
        break;
      case Direction::kDown:
        rs.SetMirror(0);
        screen->Render(xo + 4, yo + 8 + 4, rs);
        break;
      default:
        break;
    }
  }

  // Render carried Furniture overhead
  if (auto* furn = dynamic_cast<item::FurnitureItem*>(active_item.get())) {
    if (furn->furniture) {
      gfx::LinkedSprite fs = furn->furniture->sprite;
      screen->Render(x - 8, y - 22, fs);
    }
  }
}

}  // namespace minicraft::entity
