#include "core/entity/furniture.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include "core/entity/mob.h"
#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/entity/projectiles.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/inventory.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/random.h"

namespace minicraft::entity {

namespace {

std::string ToSnakeLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == ' ') {
      out.push_back('_');
    } else {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

}  // namespace

Furniture::Furniture(std::string n, gfx::LinkedSprite spr,
                     gfx::LinkedSprite item_spr, int32_t xr, int32_t yr)
    : Entity(xr, yr),
      name(std::move(n)),
      sprite(std::move(spr)),
      item_sprite(std::move(item_spr)) {}

void Furniture::Tick() {
  if (should_take_ && taker_ != nullptr && !removed) {
    auto fitem = std::make_shared<item::FurnitureItem>(Clone());
    if (taker_->AddItemToInventory(fitem->Clone())) {
      io::Sound::Play("pickup");
      Remove();
    }
    should_take_ = false;
    taker_ = nullptr;
    return;
  }

  AdvancePush();
}

void Furniture::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite;
  screen->Render(x - 8, y - 8, copy);
}

void Furniture::TouchedBy(Entity& entity) {
  if (auto* p = dynamic_cast<Player*>(&entity)) {
    TryPush(*p);
  }
}

bool Furniture::Interact(Player& player, item::Item* item,
                         Direction /*attack_dir*/) {
  if (CanPickup() && item != nullptr &&
      dynamic_cast<item::PowerGloveItem*>(item) != nullptr) {
    io::Sound::Play("monsterhurt");
    Remove();
    if (player.active_item != nullptr &&
        dynamic_cast<item::PowerGloveItem*>(player.active_item.get()) ==
            nullptr) {
      if (level) {
        level->DropItem(player.x, player.y, player.active_item->Clone());
      } else {
        player.TryAddToInvOrDrop(player.active_item->Clone());
      }
    }
    player.active_item = std::make_shared<item::FurnitureItem>(Clone());
    return true;
  }
  return false;
}

bool Furniture::CanTake(const Player& /*player*/) const { return CanPickup(); }

std::unique_ptr<Furniture> Furniture::Clone() const {
  return std::make_unique<Furniture>(name, sprite, item_sprite, xr, yr);
}

Bed::Bed(std::string color_name)
    : Furniture(
          color_name + " Bed",
          gfx::LinkedSprite(gfx::SpriteType::kEntity,
                            ToSnakeLower(color_name) + "_bed"),
          gfx::LinkedSprite(gfx::SpriteType::kItem,
                            ToSnakeLower(color_name) + "_bed"),
          3, 2),
      color_name_(std::move(color_name)) {}

bool Bed::Use(Player& player) {
  auto& gs = game::GameState::Instance();
  if (!level || gs.bed_sleeping) return false;
  if (level->depth != 0) {
    gs.AddNotification("You can only sleep on the surface!");
    return false;
  }
  const int32_t t =
      ((gs.game_time % game::GameState::kDayLength) + game::GameState::kDayLength) %
      game::GameState::kDayLength;
  const bool can_sleep =
      (t >= game::GameState::kSleepStartTime) ||
      (t < game::GameState::kSleepEndTime && gs.past_day1);
  if (!can_sleep && !gs.IsCreativeMode()) {
    const int32_t ticks_left = std::max(0, game::GameState::kSleepStartTime - t);
    const int32_t sec = static_cast<int32_t>(
        std::ceil(static_cast<double>(ticks_left) / game::GameState::kNormSpeed));
    gs.AddNotification(util::Localization::GetLocalized(
        "minicraft.notification.cannot_sleep", sec / 60, sec % 60));
    return false;
  }
  player.SetSpawnPosition(x, y, level->depth);
  gs.bed_sleeping = true;
  gs.bed_sleep_players = 1;
  gs.game_speed = 20.0f;
  return true;
}

std::unique_ptr<Furniture> Bed::Clone() const {
  return std::make_unique<Bed>(color_name_);
}

Chest::Chest(std::string name, std::string sprite_key,
             std::string item_sprite_key)
    : Furniture(std::move(name),
                gfx::LinkedSprite(gfx::SpriteType::kEntity, sprite_key),
                gfx::LinkedSprite(gfx::SpriteType::kItem,
                                  std::move(item_sprite_key)),
                3, 3),
      inventory_(std::make_shared<item::Inventory>()),
      sprite_key_(std::move(sprite_key)) {}

Chest::~Chest() = default;

bool Chest::Use(Player& /*player*/) {
  game::GameState::Instance().OpenContainer(this);
  return true;
}

bool Chest::Interact(Player& player, item::Item* item, Direction attack_dir) {
  if (!inventory_ || inventory_->InvSize() == 0) {
    return Furniture::Interact(player, item, attack_dir);
  }
  return false;
}

void Chest::Remove() {
  if (!removed && level != nullptr && inventory_) {
    for (size_t i = 0; i < inventory_->InvSize(); ++i) {
      auto* it = inventory_->Get(i);
      if (it) {
        level->DropItem(x, y, it->Clone());
      }
    }
    inventory_->ClearInv();
  }
  Furniture::Remove();
}

std::unique_ptr<Furniture> Chest::Clone() const {
  return std::make_unique<Chest>(name, sprite_key_, item_sprite.Key());
}

void Chest::PopulateInvRandom(const std::string& loot_file,
                              int32_t /*depth_bonus*/) {
  std::string text;
  if (!game::GameState::Instance().ReadAssetText(
          "resources/data/chestloot/" + loot_file + ".txt", &text)) {
    // Fallback starter loot
    if (auto a = item::Items::Get("Apple")) inventory_->Add(*a, 2);
    if (auto c = item::Items::Get("Coal")) inventory_->Add(*c, 3);
    return;
  }

  auto trim = [](std::string s) -> std::string {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
      s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
      s.pop_back();
    }
    return s;
  };
  auto parse_int = [](const std::string& s, int32_t fallback) -> int32_t {
    if (s.empty()) return fallback;
    int32_t val = 0;
    for (unsigned char ch : s) {
      if (ch < '0' || ch > '9') return fallback;
      val = val * 10 + (ch - '0');
    }
    return val > 0 ? val : fallback;
  };
  auto split_char = [&trim](const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream lss(s);
    std::string tok;
    while (std::getline(lss, tok, delim)) {
      parts.push_back(trim(tok));
    }
    return parts;
  };

  auto& rng = util::GlobalRandom();
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;

    if (line[0] != ':') {
      // Java chestloot format: "<chance>,<item_name>[,<count>]"
      auto parts = split_char(line, ',');
      if (parts.size() < 2) continue;
      const int32_t chance = std::max<int32_t>(1, parse_int(parts[0], 1));
      const std::string& item_name = parts[1];
      const int32_t count =
          parts.size() >= 3 ? std::max<int32_t>(1, parse_int(parts[2], 1)) : 1;
      auto it = item::Items::Get(item_name);
      if (!it) continue;

      if (it->IsStackable()) {
        if (rng.NextInt(chance) == 0) {
          inventory_->Add(*it, count);
        }
      } else {
        for (int32_t i = 0; i < count; ++i) {
          if (rng.NextInt(chance) == 0) {
            inventory_->Add(it->Clone());
          }
        }
      }
    } else if (inventory_->InvSize() == 0) {
      // Java fallback line: ":item1,count1:item2,count2..."
      auto fallbacks = split_char(line.substr(1), ':');
      for (const std::string& fb : fallbacks) {
        if (fb.empty()) continue;
        auto pair = split_char(fb, ',');
        if (pair.empty()) continue;
        auto it = item::Items::Get(pair[0]);
        const int32_t count =
            pair.size() >= 2 ? std::max<int32_t>(1, parse_int(pair[1], 1)) : 1;
        if (it) {
          inventory_->Add(*it, count);
        }
      }
    }
  }
}

DeathChest::DeathChest() : Chest("Death Chest", "chest", "dungeon_chest") {
  if (inventory_) {
    inventory_->SetUnlimited(true);
  }
  const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
  if (diff == 0) {
    time_ = 450 * game::GameState::kNormSpeed;
  } else if (diff == 2) {
    time_ = 150 * game::GameState::kNormSpeed;
  } else {
    time_ = 300 * game::GameState::kNormSpeed;
  }
}

DeathChest::DeathChest(Player& player) : DeathChest() {
  x = player.x;
  y = player.y;
  if (player.GetInventoryPtr()) {
    for (size_t i = 0; i < player.GetInventory().InvSize(); ++i) {
      auto* it = player.GetInventory().Get(i);
      if (it) {
        inventory_->Add(it->Clone());
      }
    }
    player.GetInventory().ClearInv();
  }
}

void DeathChest::Tick() {
  Chest::Tick();
  if (inventory_ && inventory_->InvSize() == 0) {
    Remove();
    return;
  }

  if (time_ < 30 * game::GameState::kNormSpeed) {
    red_tick_ += reverse_ ? -1 : 1;
    if (red_tick_ > 13) {
      reverse_ = true;
      sprite = gfx::LinkedSprite(gfx::SpriteType::kEntity, "chest");
    }
    if (red_tick_ < 0) {
      reverse_ = false;
      sprite = gfx::LinkedSprite(gfx::SpriteType::kEntity, "red_chest");
    }
  }

  if (time_ > 0) {
    --time_;
  }
  if (time_ == 0) {
    if (inventory_) {
      while (inventory_->InvSize() > 0) {
        auto removed_item = inventory_->Remove(0);
        if (removed_item && level) {
          level->DropItem(x, y, std::move(removed_item));
        }
      }
    }
    Remove();
  }
}

void DeathChest::TouchedBy(Entity& entity) {
  auto* player = dynamic_cast<Player*>(&entity);
  if (!player || !inventory_) return;
  auto& player_inv = player->GetInventory();
  while (inventory_->InvSize() > 0) {
    auto* it = inventory_->Get(0);
    if (!it) break;
    auto leftover = player_inv.Add(it->Clone());
    if (leftover) {
      if (auto* st = dynamic_cast<item::StackableItem*>(it)) {
        if (auto* rem = dynamic_cast<item::StackableItem*>(leftover.get())) {
          st->count = rem->count;
        }
      }
      game::GameState::Instance().AddNotification("Your inventory is full!");
      return;
    }
    inventory_->Remove(0);
  }
  Remove();
  game::GameState::Instance().AddNotification(
      util::Localization::GetLocalized(
          "minicraft.notification.death_chest_retrieved"));
}

void DeathChest::Render(gfx::Screen* screen) const {
  if (!screen) return;
  Chest::Render(screen);
  const std::string time_str =
      std::to_string(time_ / game::GameState::kNormSpeed) + "S";
  gfx::Font::Draw(time_str, screen, x - gfx::Font::TextWidth(time_str) / 2,
                  y - gfx::Font::TextHeight() - yr, gfx::color::kWhite);
}

DungeonChest::DungeonChest(bool populate, bool locked)
    : Chest("Dungeon Chest", locked ? "white_chest" : "dungeon_chest",
            "dungeon_chest"),
      is_locked(locked) {
  if (populate) {
    PopulateInvRandom("dungeonchest", 1);
  }
}

bool DungeonChest::Use(Player& player) {
  if (is_locked) {
    auto& gs = game::GameState::Instance();
    auto key_item = item::Items::Get("Dungeon Key");
    if (key_item && player.GetInventory().Count(*key_item) > 0) {
      if (!gs.IsCreativeMode()) {
        player.GetInventory().RemoveItems(*key_item, 1);
      }
      is_locked = false;
      io::Sound::Play("craft");
      gs.AddNotification("Dungeon Chest unlocked!");
      if (level) {
        level->Add(std::make_shared<TextParticle>(
            util::Localization::GetLocalized(
                "minicraft.text_particales.key_consumed"),
            x, y, gfx::color::kRed));
        level->chest_count = std::max(0, level->chest_count - 1);
        if (level->chest_count == 0) {
          level->DropItem(x, y, 5, 5, "Gold Apple");
        }
      }
      return Chest::Use(player);
    }
    gs.AddNotification("Locked! Requires a Dungeon Key.");
    return false;
  }
  return Chest::Use(player);
}

void DungeonChest::TouchedBy(Entity& entity) {
  if (!is_locked) {
    Chest::TouchedBy(entity);
  }
}

bool DungeonChest::Interact(Player& player, item::Item* item,
                            Direction attack_dir) {
  if (is_locked && item != nullptr && item->Name() == "Dungeon Key") {
    return Use(player);
  }
  if (is_locked) {
    return false;
  }
  return Chest::Interact(player, item, attack_dir);
}

std::unique_ptr<Furniture> DungeonChest::Clone() const {
  return std::make_unique<DungeonChest>(false, is_locked);
}

namespace {
std::string CrafterName(Crafter::Type t) {
  switch (t) {
    case Crafter::Type::kWorkbench:
      return "Workbench";
    case Crafter::Type::kFurnace:
      return "Furnace";
    case Crafter::Type::kOven:
      return "Oven";
    case Crafter::Type::kAnvil:
      return "Anvil";
    case Crafter::Type::kEnchanter:
      return "Enchanter";
    case Crafter::Type::kLoom:
      return "Loom";
    case Crafter::Type::kDyeVat:
      return "Dye Vat";
  }
  return "Workbench";
}

std::string CrafterSpriteKey(Crafter::Type t) {
  switch (t) {
    case Crafter::Type::kWorkbench:
      return "workbench";
    case Crafter::Type::kFurnace:
      return "furnace";
    case Crafter::Type::kOven:
      return "oven";
    case Crafter::Type::kAnvil:
      return "anvil";
    case Crafter::Type::kEnchanter:
      return "enchanter";
    case Crafter::Type::kLoom:
      return "loom";
    case Crafter::Type::kDyeVat:
      return "dyevat";
  }
  return "workbench";
}
int32_t CrafterXr(Crafter::Type type) {
  switch (type) {
    case Crafter::Type::kEnchanter:
    case Crafter::Type::kLoom:
      return 7;
    case Crafter::Type::kDyeVat:
      return 0;
    default:
      return 3;
  }
}

int32_t CrafterYr(Crafter::Type type) {
  return (type == Crafter::Type::kDyeVat) ? 0 : 2;
}
}  // namespace

Crafter::Crafter(Type type)
    : Furniture(CrafterName(type),
                gfx::LinkedSprite(gfx::SpriteType::kEntity,
                                  CrafterSpriteKey(type)),
                gfx::LinkedSprite(gfx::SpriteType::kItem,
                                  CrafterSpriteKey(type)),
                CrafterXr(type), CrafterYr(type)),
      type_(type) {}

bool Crafter::Use(Player& /*player*/) {
  game::GameState::Instance().OpenCrafting(static_cast<int32_t>(type_));
  return true;
}

std::string Crafter::GetEntityTypeName() const { return CrafterName(type_); }

std::unique_ptr<Furniture> Crafter::Clone() const {
  return std::make_unique<Crafter>(type_);
}

namespace {
std::string LanternName(Lantern::Type t) {
  switch (t) {
    case Lantern::Type::kNorm:
      return "Lantern";
    case Lantern::Type::kIron:
      return "Iron Lantern";
    case Lantern::Type::kGold:
      return "Gold Lantern";
  }
  return "Lantern";
}
std::string LanternKey(Lantern::Type t) {
  switch (t) {
    case Lantern::Type::kNorm:
      return "lantern";
    case Lantern::Type::kIron:
      return "iron_lantern";
    case Lantern::Type::kGold:
      return "gold_lantern";
  }
  return "lantern";
}
}  // namespace

Lantern::Lantern(Type type)
    : Furniture(LanternName(type),
                gfx::LinkedSprite(gfx::SpriteType::kEntity, LanternKey(type)),
                gfx::LinkedSprite(gfx::SpriteType::kItem, LanternKey(type)),
                3, 2),
      type_(type) {}

int32_t Lantern::GetLightRadius() const {
  switch (type_) {
    case Type::kNorm:
      return 8;
    case Type::kIron:
      return 10;
    case Type::kGold:
      return 12;
  }
  return 8;
}

std::string Lantern::GetEntityTypeName() const { return LanternName(type_); }

std::unique_ptr<Furniture> Lantern::Clone() const {
  return std::make_unique<Lantern>(type_);
}

Composter::Composter()
    : Furniture("Composter",
                gfx::LinkedSprite(gfx::SpriteType::kEntity, "composter"),
                gfx::LinkedSprite(gfx::SpriteType::kItem, "composter"), 3,
                2) {}

void Composter::UpdateVisual() {
  if (compost >= kMaxCompost) {
    sprite = gfx::LinkedSprite(gfx::SpriteType::kEntity, "compostor_full");
  } else if (compost > 0) {
    sprite = gfx::LinkedSprite(gfx::SpriteType::kEntity, "compostor_filled");
  } else {
    sprite = gfx::LinkedSprite(gfx::SpriteType::kEntity, "composter");
  }
}

bool Composter::Use(Player& player) {
  return Interact(player, nullptr, player.dir);
}

bool Composter::Interact(Player& player, item::Item* item,
                         Direction attack_dir) {
  if (compost >= kMaxCompost) {
    compost = 0;
    auto fert = item::Items::Get("Fertilizer");
    if (auto* st = dynamic_cast<item::StackableItem*>(fert.get())) {
      st->count = 1;
    }
    if (fert && level) {
      level->DropItem(x, y, std::move(fert));
    }
    UpdateVisual();
    return true;
  }
  if (auto* st = dynamic_cast<item::StackableItem*>(item)) {
    const std::string& n = item->Name();
    auto& rng = util::GlobalRandom();
    bool accepted = false;
    if (n == "Baked Potato" || n == "Bread") {
      accepted = true;
    } else if (n == "Wheat" || n == "Rose" || n == "Flower" || n == "Apple" ||
               n == "Potato" || n == "Carrot" || n == "Tomato") {
      if (rng.NextInt(4) != 0) {
        accepted = true;
      } else {
        return false;
      }
    } else if (n == "Acorn" || n == "Cactus" || n == "Wheat Seeds" ||
               n == "Grass Seeds" || n == "Seeds" || n == "Tomato Seeds" ||
               n == "Sapling") {
      if (rng.NextInt(3) != 0) {
        accepted = true;
      } else {
        return false;
      }
    }
    if (accepted) {
      ++compost;
      st->count -= 1;
      UpdateVisual();
      io::Sound::Play("craft");
      return true;
    }
  }
  return Furniture::Interact(player, item, attack_dir);
}

std::unique_ptr<Furniture> Composter::Clone() const {
  auto c = std::make_unique<Composter>();
  c->compost = compost;
  c->UpdateVisual();
  return c;
}

KnightStatue::KnightStatue(int32_t boss_hp)
    : Furniture("Knight Statue",
                gfx::LinkedSprite(gfx::SpriteType::kEntity, "knight_statue"),
                gfx::LinkedSprite(gfx::SpriteType::kItem, "knight_statue"),
                3, 2),
      boss_hp_(boss_hp) {}

void KnightStatue::Tick() { Furniture::Tick(); }

bool KnightStatue::Use(Player& /*player*/) {
  auto& gs = game::GameState::Instance();
  if (gs.obsidian_knight_summoned) {
    gs.AddNotification(
        util::Localization::GetLocalized("minicraft.notification.boss_limit"));
    return false;
  }
  if (!level) return false;

  if (touches_ == 0) {
    gs.AddNotification(util::Localization::GetLocalized(
        "minicraft.notifications.statue_tapped"));
    ++touches_;
    return true;
  }
  if (touches_ == 1) {
    gs.AddNotification(util::Localization::GetLocalized(
        "minicraft.notifications.statue_touched"));
    ++touches_;
    return true;
  }

  gs.obsidian_knight_summoned = true;
  if (level->depth == -4) {
    level->RegenerateBossRoom();
  }
  auto boss = std::make_shared<ObsidianKnight>(boss_hp_);
  level->Add(boss, x, y);
  io::Sound::Play("bossdeath");
  gs.NotifyAll("minicraft.notification.obsidian_knight_awoken");
  Remove();
  return true;
}

bool KnightStatue::Interact(Player& player, item::Item* /*item*/,
                            Direction /*attack_dir*/) {
  return Use(player);
}

void KnightStatue::Hurt(Mob& mob, int32_t dmg, Direction /*attack_dir*/) {
  hp_ -= dmg;
  if (level) {
    level->Add(std::make_shared<SmashParticle>(x, y));
    level->Add(std::make_shared<TextParticle>(std::to_string(dmg), x, y,
                                              gfx::color::kRed));
  }
  if (hp_ <= 0) {
    if (auto* p = dynamic_cast<Player*>(&mob)) {
      touches_ = 2;
      Use(*p);
    } else if (level && !game::GameState::Instance().obsidian_knight_summoned) {
      game::GameState::Instance().obsidian_knight_summoned = true;
      level->Add(std::make_shared<ObsidianKnight>(boss_hp_), x, y);
      Remove();
    }
  }
}

namespace {
constexpr int32_t kSpawnerActiveRadius = 8 << 4;
constexpr int32_t kSpawnerMinInterval = 200;
constexpr int32_t kSpawnerMaxInterval = 500;
constexpr int32_t kSpawnerMinMobChance = 10;
}  // namespace

Spawner::Spawner(std::string mob_type, int32_t mob_lvl)
    : Furniture(
          "Spawner",
          gfx::LinkedSprite(gfx::SpriteType::kEntity, "spawner"),
          gfx::LinkedSprite(gfx::SpriteType::kItem,
                            ToSnakeLower(mob_type) + "_spawner"),
          7, 2),
      mob_type_(std::move(mob_type)),
      mob_lvl_(std::clamp(mob_lvl, 1, 4)) {
  ResetSpawnInterval();
}

void Spawner::ResetSpawnInterval() {
  spawn_tick_ = util::GlobalRandom().NextInt(kSpawnerMaxInterval -
                                             kSpawnerMinInterval + 1) +
                kSpawnerMinInterval;
}

void Spawner::Tick() {
  Furniture::Tick();
  if (!level) return;
  --spawn_tick_;
  if (spawn_tick_ <= 0) {
    const int32_t chance =
        level->max_mob_count > 0
            ? static_cast<int32_t>(
                  kSpawnerMinMobChance *
                  std::pow(static_cast<double>(level->mob_count), 2) /
                  std::pow(static_cast<double>(level->max_mob_count), 2))
            : 0;
    if (chance <= 0 || util::GlobalRandom().NextInt(chance) == 0) {
      TrySpawn();
    }
    ResetSpawnInterval();
  }
}

void Spawner::TrySpawn() {
  if (!level) return;
  if (level->mob_count >= level->max_mob_count) return;

  auto new_mob = Mob::CreateByName(mob_type_, mob_lvl_);
  if (!new_mob) return;
  const bool is_enemy = dynamic_cast<EnemyMob*>(new_mob.get()) != nullptr;

  if (is_enemy) {
    const auto& gs = game::GameState::Instance();
    constexpr int32_t kSleepEnd = game::GameState::kDayLength / 8;
    constexpr int32_t kSleepStart =
        game::GameState::kDayLength / 2 + game::GameState::kDayLength / 8;
    if (level->depth >= 0 && gs.game_time > kSleepEnd &&
        gs.game_time < kSleepStart) {
      return;
    }
    if (level->IsLight(x >> 4, y >> 4)) return;
  }

  Player* player = level->GetClosestPlayer(x, y);
  if (!player) return;
  const int32_t xd = player->x - x;
  const int32_t yd = player->y - y;
  if (xd * xd + yd * yd > kSpawnerActiveRadius * kSpawnerActiveRadius) return;

  const auto area_positions = level->GetAreaTilePositions(x >> 4, y >> 4, 1);
  std::vector<gfx::Point> valid_positions;
  for (const auto& p : area_positions) {
    const auto* tile = level->GetTile(p.x, p.y);
    if (!tile->MayPass(*level, p.x, p.y, *new_mob)) continue;
    if (is_enemy && tile->GetLightRadius(*level, p.x, p.y) > 0) continue;
    valid_positions.push_back(p);
  }
  if (valid_positions.empty()) return;

  auto& rng = util::GlobalRandom();
  const gfx::Point spawn_pos = valid_positions[rng.NextInt(
      static_cast<int32_t>(valid_positions.size()))];
  new_mob->x = spawn_pos.x << 4;
  new_mob->y = spawn_pos.y << 4;
  level->Add(new_mob);
  io::Sound::Play("monsterhurt");
  for (int32_t i = 0; i < 6; ++i) {
    const int32_t rand_x = rng.NextInt(16);
    const int32_t rand_y = rng.NextInt(12);
    level->Add(
        std::make_shared<FireParticle>(x - 8 + rand_x, y - 6 + rand_y));
  }
}

void Spawner::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite;
  screen->Render(x - 8, y - 8, copy);
}

void Spawner::Hurt(Mob& mob, int32_t dmg, Direction /*attack_dir*/) {
  health_ -= dmg;
  if (level) {
    level->Add(std::make_shared<TextParticle>(
        std::to_string(dmg), x, y, gfx::color::Get(-1, 200, 300, 400)));
  }
  if (health_ <= 0) {
    if (auto* p = dynamic_cast<Player*>(&mob)) {
      p->AddScore(500);
    }
    io::Sound::Play("death");
    Remove();
  }
}

bool Spawner::Interact(Player& player, item::Item* item,
                       Direction attack_dir) {
  auto& gs = game::GameState::Instance();
  auto& rng = util::GlobalRandom();
  if (auto* tool = dynamic_cast<item::ToolItem*>(item)) {
    io::Sound::Play("monsterhurt");
    int32_t dmg;
    if (gs.IsCreativeMode()) {
      dmg = health_;
    } else {
      dmg = tool->level + rng.NextInt(2);
      if (tool->type == item::ToolType::kPickaxe) {
        dmg += rng.NextInt(5) + 2;
      }
      if (player.potion_effects.count(
              static_cast<int32_t>(Player::PotionEffect::kHaste))) {
        dmg *= 2;
      }
    }
    Hurt(player, dmg, attack_dir);
    return true;
  }
  if (dynamic_cast<item::PowerGloveItem*>(item) != nullptr &&
      gs.IsCreativeMode()) {
    io::Sound::Play("monsterhurt");
    Remove();
    player.active_item = std::make_shared<item::FurnitureItem>(Clone());
    return true;
  }
  if (item == nullptr) {
    return Use(player);
  }
  return false;
}

std::unique_ptr<Furniture> Spawner::Clone() const {
  return std::make_unique<Spawner>(mob_type_, mob_lvl_);
}

bool Spawner::Use(Player& /*player*/) {
  if (!game::GameState::Instance().IsCreativeMode()) return false;
  auto sample = Mob::CreateByName(mob_type_, mob_lvl_);
  if (dynamic_cast<EnemyMob*>(sample.get()) != nullptr) {
    ++mob_lvl_;
    if (mob_lvl_ > 4) mob_lvl_ = 1;
    return true;
  }
  return false;
}

Tnt::Tnt()
    : Furniture("Tnt", gfx::LinkedSprite(gfx::SpriteType::kEntity, "tnt"),
                gfx::LinkedSprite(gfx::SpriteType::kItem, "tnt"), 3, 2) {}

void Tnt::Tick() {
  Furniture::Tick();
  if (fuse_lit) {
    ++ftik_;
    if (ftik_ >= 90) {
      Explode();
    }
  }
}

void Tnt::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite;
  if (fuse_lit && (ftik_ / 5) % 2 == 0) {
    copy.SetColor(gfx::color::kWhite);
  }
  screen->Render(x - 8, y - 8, copy);
}

bool Tnt::Interact(Player& player, item::Item* item, Direction attack_dir) {
  if (dynamic_cast<item::PowerGloveItem*>(item) != nullptr) {
    if (!fuse_lit) {
      return Furniture::Interact(player, item, attack_dir);
    }
    return false;
  }
  if (!fuse_lit) {
    fuse_lit = true;
    io::Sound::Play("fuse");
    return true;
  }
  return false;
}

void Tnt::Hurt(Mob& /*mob*/, int32_t /*dmg*/, Direction /*attack_dir*/) {
  // TNT does not ignite from generic Mob::Hurt in Java.
}

bool Tnt::Use(Player& player) {
  return Interact(player, nullptr, player.dir);
}

void Tnt::Explode() {
  static constexpr int32_t kFuseTime = 90;
  static constexpr int32_t kBlastRadius = 32;
  static constexpr int32_t kBlastDamage = 75;

  if (!level) {
    Remove();
    return;
  }

  std::vector<Entity*> hits = level->GetEntitiesInRect(gfx::Rectangle(
      x, y, kBlastRadius * 2, kBlastRadius * 2, gfx::Rectangle::kCenterDims));
  for (Entity* e : hits) {
    if (e == this) continue;
    const double dist =
        std::hypot(static_cast<double>(e->x - x), static_cast<double>(e->y - y));
    const int32_t dmg =
        static_cast<int32_t>(kBlastDamage * (1.0 - (dist / kBlastRadius))) + 1;
    if (auto* mob = dynamic_cast<Mob*>(e)) {
      if (dmg > 0) {
        Direction adir = Direction::kNone;
        const int32_t xdiff = mob->x - x;
        const int32_t ydiff = mob->y - y;
        if (std::abs(xdiff) > std::abs(ydiff)) {
          adir = (xdiff < 0) ? Direction::kLeft : Direction::kRight;
        } else {
          adir = (ydiff < 0) ? Direction::kUp : Direction::kDown;
        }
        mob->DoHurt(dmg, adir);
      }
    } else if (auto* other_tnt = dynamic_cast<Tnt*>(e)) {
      if (!other_tnt->fuse_lit) {
        other_tnt->fuse_lit = true;
        io::Sound::Play("fuse");
        other_tnt->ftik_ = kFuseTime * 2 / 3;
      }
    }
  }

  const int32_t xt = x >> 4;
  const int32_t yt = (y - 2) >> 4;
  for (int32_t dy = -1; dy <= 1; ++dy) {
    for (int32_t dx = -1; dx <= 1; ++dx) {
      const int32_t nx = xt + dx;
      const int32_t ny = yt + dy;
      const level::Tile* t = level->GetTile(nx, ny);
      const_cast<level::Tile*>(t)->OnExplode(*level, nx, ny);
      if (!level::Tiles::IsExplosionResistant(t->id)) {
        level->SetTile(nx, ny, level::Tiles::kExplode, 0);
        level->Add(std::make_shared<ExplosionTileTicker>(nx, ny, 18));
      }
    }
  }

  util::AchievementManager::Instance().SetAchievement(
      "minicraft.achievement.demolition", true,
      game::GameState::Instance().IsCreativeMode(), true);
  io::Sound::Play("explode");
  Remove();
}

std::unique_ptr<Furniture> Tnt::Clone() const {
  return std::make_unique<Tnt>();
}

}  // namespace minicraft::entity
