#include "core/level/tiles_internal.h"

#include <algorithm>
#include <string>

#include "core/entity/boat.h"
#include "core/entity/furniture.h"
#include "core/entity/mob.h"
#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/entity/projectiles.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/item.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/random.h"

namespace minicraft::level::detail {

namespace {

class GrassTile : public Tile {
 public:
  GrassTile()
      : Tile(Tiles::kGrass, "Grass", "grass"),
        border_(gfx::SpriteType::kTile, "grass_border") {
    connects_to_grass = true;
    may_spawn = true;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    auto& rng = util::GlobalRandom();
    if (rng.NextInt(40) != 0) return false;
    int32_t xn = xt;
    int32_t yn = yt;
    if (rng.NextBoolean()) {
      xn += rng.NextInt(2) * 2 - 1;
    } else {
      yn += rng.NextInt(2) * 2 - 1;
    }
    if (level.GetTile(xn, yn)->id == Tiles::kDirt) {
      level.SetTile(xn, yn, Tiles::kGrass, 0);
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    RenderConnected(screen, level, x, y, sprite_, border_, nullptr,
                    [&](const Tile* t, int32_t nx, int32_t ny) {
                      return t->ConnectsToGrass(level, nx, ny);
                    });
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (!tool) return false;
    if (tool->type == item::ToolType::kShovel &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, Tiles::kDirt, 0);
      io::Sound::Play("monsterhurt");
      if (util::GlobalRandom().NextInt(5) == 0) {
        level.DropItem(xt * 16 + 8, yt * 16 + 8, "Grass Seeds");
      }
      return true;
    }
    if (tool->type == item::ToolType::kHoe &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      io::Sound::Play("monsterhurt");
      if (util::GlobalRandom().NextInt(5) != 0) {
        level.DropItem(xt * 16 + 8, yt * 16 + 8, "Wheat Seeds");
      }
      level.SetTile(xt, yt, Tiles::kFarmland, 0);
      return true;
    }
    if (tool->type == item::ToolType::kPickaxe &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, Tiles::kPath, 0);
      io::Sound::Play("monsterhurt");
      return true;
    }
    return false;
  }

 private:
  mutable gfx::LinkedSprite border_;
};

class DirtTile : public Tile {
 public:
  DirtTile() : Tile(Tiles::kDirt, "Dirt", "dirt") { may_spawn = true; }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (!tool) return false;
    if (tool->type == item::ToolType::kShovel &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, Tiles::kHole, 0);
      level.DropItem(xt * 16 + 8, yt * 16 + 8, "Dirt");
      io::Sound::Play("monsterhurt");
      return true;
    }
    if (tool->type == item::ToolType::kHoe &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, Tiles::kFarmland, 0);
      io::Sound::Play("monsterhurt");
      return true;
    }
    return false;
  }
};

class FlowerTile : public Tile {
 public:
  FlowerTile() : Tile(Tiles::kFlower, "Flower", "dandelion") {
    connects_to_grass = true;
    may_spawn = true;
  }

  std::string GetName(int32_t data) const override {
    static constexpr const char* kNames[] = {
        "Oxeye Daisy",  "Rose",        "Sunflower",   "Allium",
        "Blue Orchid",  "Cornflower",  "Dandelion",   "Hydrangea",
        "Iris",         "Orange Tulip","Pink Tulip",  "Red Tulip",
        "White Tulip",  "Peony",       "Periwinkle",  "Pink Lily",
        "White Lily",   "Poppy",       "Violet"};
    int32_t idx = data % 19;
    if (idx < 0) idx += 19;
    return kNames[idx];
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    Tiles::Get(Tiles::kGrass)->Render(screen, level, x, y);
    static constexpr const char* kKeys[] = {
        "oxeye_daisy",  "rose",        "sunflower",   "allium",
        "blue_orchid",  "cornflower",  "dandelion",   "hydrangea",
        "iris",         "orange_tulip","pink_tulip",  "red_tulip",
        "white_tulip",  "peony",       "periwinkle",  "pink_lily",
        "white_lily",   "poppy",       "violet"};
    int32_t idx = level.GetData(x, y) % 19;
    if (idx < 0) idx += 19;
    gfx::LinkedSprite spr(gfx::SpriteType::kTile, kKeys[idx]);
    screen->Render(x * 16, y * 16, spr);
  }

  bool Interact(Level& level, int32_t x, int32_t y, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kShovel) {
      if (player.PayStamina(2 - tool->level) && tool->PayDurability()) {
        const int32_t data = level.GetData(x, y);
        level.SetTile(x, y, Tiles::kGrass, 0);
        io::Sound::Play("monsterhurt");
        level.DropItem(x * 16 + 8, y * 16 + 8, GetName(data));
        return true;
      }
    }
    return false;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    if (util::GlobalRandom().NextInt(30) != 0) return false;
    int32_t xn = xt;
    int32_t yn = yt;
    if (util::GlobalRandom().NextBoolean()) {
      xn += util::GlobalRandom().NextInt(2) * 2 - 1;
    } else {
      yn += util::GlobalRandom().NextInt(2) * 2 - 1;
    }
    if (level.GetTile(xn, yn)->id == Tiles::kDirt) {
      level.SetTile(xn, yn, Tiles::kGrass, 0);
      return true;
    }
    return false;
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t /*dmg*/, entity::Direction /*dir*/) const override {
    const int32_t data = level.GetData(x, y);
    level.DropItem(x * 16 + 8, y * 16 + 8, GetName(data));
    level.SetTile(x, y, Tiles::kGrass, 0);
    return true;
  }
};

class HoleTile : public Tile {
 public:
  HoleTile()
      : Tile(Tiles::kHole, "Hole", "hole"),
        border_(gfx::SpriteType::kTile, "hole_border") {
    connects_to_sand = true;
    connects_to_fluid = true;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& e) const override {
    return e.CanSwim();
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    RenderConnected(screen, level, x, y, sprite_, border_, nullptr,
                    [&](const Tile* t, int32_t nx, int32_t ny) {
                      return t->ConnectsToFluid(level, nx, ny);
                    });
  }

 private:
  mutable gfx::LinkedSprite border_;
};

class StairsTile : public Tile {
 public:
  StairsTile(uint16_t id, bool up)
      : Tile(id, up ? "Stairs Up" : "Stairs Down",
             up ? "stairs_up" : "stairs_down") {}

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& e) const override {
    return dynamic_cast<const entity::Furniture*>(&e) == nullptr;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt,
                entity::Player& /*player*/, item::Item* item,
                entity::Direction /*dir*/) const override {
    if (game::GameState::Instance().IsCreativeMode() &&
        dynamic_cast<item::PowerGloveItem*>(item) != nullptr) {
      level.SetTile(xt, yt, Tiles::kGrass, 0);
      io::Sound::Play("monsterhurt");
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    Tile::Render(screen, level, x, y);
  }
};

class WaterTile : public Tile {
 public:
  WaterTile()
      : Tile(Tiles::kWater, "Water", "water"),
        border_(gfx::SpriteType::kTile, "water_border") {
    connects_to_fluid = true;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& e) const override {
    return e.CanSwim();
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    auto& rng = util::GlobalRandom();
    int32_t xn = xt;
    int32_t yn = yt;
    if (rng.NextBoolean()) {
      xn += rng.NextInt(2) * 2 - 1;
    } else {
      yn += rng.NextInt(2) * 2 - 1;
    }
    bool changed = false;
    if (level.GetTile(xn, yn)->id == Tiles::kHole) {
      level.SetTile(xn, yn, Tiles::kWater, 0);
      changed = true;
    }
    // Cool orthogonally adjacent Lava tiles into Raw Obsidian.
    for (int32_t dx = -1; dx <= 1; ++dx) {
      if (level.GetTile(xt + dx, yt)->id == Tiles::kLava) {
        level.SetTile(xt + dx, yt, Tiles::kRawObsidian, 0);
        changed = true;
      }
    }
    for (int32_t dy = -1; dy <= 1; ++dy) {
      if (level.GetTile(xt, yt + dy)->id == Tiles::kLava) {
        level.SetTile(xt, yt + dy, Tiles::kRawObsidian, 0);
        changed = true;
      }
    }
    return changed;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    RenderConnected(screen, level, x, y, sprite_, border_, nullptr,
                    [&](const Tile* t, int32_t nx, int32_t ny) {
                      return t->ConnectsToFluid(level, nx, ny);
                    });
  }

 private:
  mutable gfx::LinkedSprite border_;
};

class LavaTile : public Tile {
 public:
  LavaTile()
      : Tile(Tiles::kLava, "Lava", "lava"),
        border_(gfx::SpriteType::kTile, "lava_border") {
    connects_to_sand = true;
    connects_to_fluid = true;
  }

  int32_t GetLightRadius(const Level& /*level*/, int32_t /*x*/,
                         int32_t /*y*/) const override {
    return 6;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& e) const override {
    return e.CanSwim();
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    auto& rng = util::GlobalRandom();
    int32_t xn = xt;
    int32_t yn = yt;
    if (rng.NextBoolean()) {
      xn += rng.NextInt(2) * 2 - 1;
    } else {
      yn += rng.NextInt(2) * 2 - 1;
    }
    if (level.GetTile(xn, yn)->id == Tiles::kHole) {
      level.SetTile(xn, yn, Tiles::kLava, 0);
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderConnected(screen, level, x, y, sprite_, border_, nullptr,
                    [&](const Tile* t, int32_t nx, int32_t ny) {
                      return t->ConnectsToFluid(level, nx, ny);
                    });
  }

 private:
  mutable gfx::LinkedSprite border_;
};

class RockTile : public Tile {
 public:
  RockTile()
      : Tile(Tiles::kRock, "Rock", "rock"),
        border_(gfx::SpriteType::kTile, "rock_border"),
        corner_(gfx::SpriteType::kTile, "rock_corner") {
    connects_to_rock = true;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return false;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    const int16_t damage = level.GetData(xt, yt);
    if (damage > 0) {
      level.SetData(xt, yt, static_cast<int16_t>(damage - 1));
      return true;
    }
    return false;
  }

  void Hurt(Level& level, int32_t x, int32_t y, int32_t dmg) const override {
    ApplyDamage(level, x, y, dmg, false);
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t dmg, entity::Direction /*dir*/) const override {
    ApplyDamage(level, x, y, dmg, false);
    return true;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kPickaxe &&
        player.PayStamina(5 - tool->level) && tool->PayDurability()) {
      ApplyDamage(level, xt, yt, tool->GetDamage(), true);
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    RenderConnected(screen, level, x, y, sprite_, border_, &corner_,
                    [](const Tile* t, int32_t, int32_t) {
                      return t->id == Tiles::kRock;
                    });
  }

 private:
  static void ApplyDamage(Level& level, int32_t x, int32_t y, int32_t dmg,
                          bool drop_coal) {
    constexpr int32_t kMaxHealth = 50;
    const bool creative = game::GameState::Instance().IsCreativeMode();
    int32_t damage = level.GetData(x, y) + dmg;
    if (creative) {
      dmg = kMaxHealth;
      damage = kMaxHealth;
      drop_coal = true;
    }

    level.Add(std::make_shared<entity::SmashParticle>(x * 16, y * 16));
    io::Sound::Play("monsterhurt");
    level.Add(std::make_shared<entity::TextParticle>(
        std::to_string(dmg), x * 16 + 8, y * 16 + 8, gfx::color::kRed));

    if (damage >= kMaxHealth) {
      auto& rng = util::GlobalRandom();
      int32_t stone = 1;
      if (drop_coal) {
        stone += rng.NextInt(3) + 1;
        int32_t coal = 1;
        if (game::GameState::Instance().GetDifficultyIdx() != 2) {
          coal += 1;
        }
        level.DropItem(x * 16 + 8, y * 16 + 8, 0, coal, "Coal");
      }
      level.DropItem(x * 16 + 8, y * 16 + 8, stone, stone, "Stone");
      level.SetTile(x, y, Tiles::kDirt, 0);
    } else {
      level.SetData(x, y, static_cast<int16_t>(damage));
    }
  }

  mutable gfx::LinkedSprite border_;
  mutable gfx::LinkedSprite corner_;
};

class HardRockTile : public Tile {
 public:
  HardRockTile()
      : Tile(Tiles::kHardRock, "Hard Rock", "hardrock"),
        border_(gfx::SpriteType::kTile, "hardrock_border"),
        corner_(gfx::SpriteType::kTile, "hardrock_corner") {
    connects_to_rock = true;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return false;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    const int16_t damage = level.GetData(xt, yt);
    if (damage > 0) {
      level.SetData(xt, yt, static_cast<int16_t>(damage - 1));
      return true;
    }
    return false;
  }

  void Hurt(Level& level, int32_t x, int32_t y, int32_t dmg) const override {
    constexpr int32_t kMaxHealth = 200;
    const bool creative = game::GameState::Instance().IsCreativeMode();
    int32_t damage = level.GetData(x, y) + dmg;
    if (creative) {
      dmg = kMaxHealth;
      damage = kMaxHealth;
    }

    level.Add(std::make_shared<entity::SmashParticle>(x * 16, y * 16));
    io::Sound::Play("monsterhurt");
    level.Add(std::make_shared<entity::TextParticle>(
        std::to_string(dmg), x * 16 + 8, y * 16 + 8, gfx::color::kRed));

    if (damage >= kMaxHealth) {
      level.SetTile(x, y, Tiles::kDirt, 0);
      level.DropItem(x * 16 + 8, y * 16 + 8, 1, 3, "Stone");
      level.DropItem(x * 16 + 8, y * 16 + 8, 0, 1, "Coal");
    } else {
      level.SetData(x, y, static_cast<int16_t>(damage));
    }
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t /*dmg*/, entity::Direction /*dir*/) const override {
    Hurt(level, x, y, 0);
    return true;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    if (game::GameState::Instance().IsCreativeMode()) return false;
    if (auto* tool = dynamic_cast<item::ToolItem*>(item)) {
      if (tool->type == item::ToolType::kPickaxe && tool->level == 4) {
        if (player.PayStamina(2) && tool->PayDurability()) {
          Hurt(level, xt, yt, tool->GetDamage());
          return true;
        }
      } else {
        game::GameState::Instance().AddNotification(
            util::Localization::GetLocalized(
                "minicraft.notification.gem_pickaxe_required"));
      }
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    RenderConnected(screen, level, x, y, sprite_, border_, &corner_,
                    [](const Tile* t, int32_t, int32_t) {
                      return t->id == Tiles::kRock;
                    });
  }

 private:
  mutable gfx::LinkedSprite border_;
  mutable gfx::LinkedSprite corner_;
};

class TreeTile : public Tile {
 public:
  TreeTile() : Tile(Tiles::kTree, "Tree", "oak") { connects_to_grass = true; }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return false;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    const int16_t damage = level.GetData(xt, yt);
    if (damage > 0) {
      level.SetData(xt, yt, static_cast<int16_t>(damage - 1));
      return true;
    }
    return false;
  }

  void Hurt(Level& level, int32_t x, int32_t y, int32_t dmg) const override {
    auto& rng = util::GlobalRandom();
    if (rng.NextInt(100) == 0) {
      level.DropItem(x * 16 + 8, y * 16 + 8, "Apple");
    }

    constexpr int32_t kTreeHealth = 20;
    const bool creative = game::GameState::Instance().IsCreativeMode();
    int32_t damage = level.GetData(x, y) + dmg;
    if (creative) {
      dmg = kTreeHealth;
      damage = kTreeHealth;
    }

    level.Add(std::make_shared<entity::SmashParticle>(x * 16, y * 16));
    io::Sound::Play("monsterhurt");
    level.Add(std::make_shared<entity::TextParticle>(
        std::to_string(dmg), x * 16 + 8, y * 16 + 8, gfx::color::kRed));

    if (damage >= kTreeHealth) {
      level.DropItem(x * 16 + 8, y * 16 + 8, 1, 3, "Wood");
      level.DropItem(x * 16 + 8, y * 16 + 8, 0, 2, "Acorn");
      level.SetTile(x, y, Tiles::kGrass, 0);
      util::AchievementManager::Instance().SetAchievement(
          "minicraft.achievement.woodcutter", true, creative);
    } else {
      level.SetData(x, y, static_cast<int16_t>(damage));
    }
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t dmg, entity::Direction /*dir*/) const override {
    Hurt(level, x, y, dmg);
    return true;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    if (game::GameState::Instance().IsCreativeMode()) return false;
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kAxe) {
      if (player.PayStamina(4 - tool->level) && tool->PayDurability()) {
        Hurt(level, xt, yt, tool->GetDamage());
        return true;
      }
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    Tiles::Get(Tiles::kGrass)->Render(screen, level, x, y);
    if (!screen) return;

    static constexpr const char* kTreeKeys[7] = {
        "oak", "spruce", "birch", "ash", "aspen", "fir", "willow"};
    Level& mutable_level = const_cast<Level&>(level);
    const TreeType this_type = mutable_level.GetTreeType(x, y);
    const int32_t idx = std::clamp(static_cast<int32_t>(this_type), 0, 6);
    gfx::LinkedSprite spr_norm(gfx::SpriteType::kTile, kTreeKeys[idx]);
    gfx::LinkedSprite spr_full(gfx::SpriteType::kTile,
                               std::string(kTreeKeys[idx]) + "_full");

    auto& gs = game::GameState::Instance();
    const gfx::SheetRect* norm_rect =
        gs.sprite_linker ? spr_norm.GetSheet(*gs.sprite_linker) : nullptr;
    const gfx::SheetRect* full_rect =
        gs.sprite_linker ? spr_full.GetSheet(*gs.sprite_linker) : nullptr;
    if (!norm_rect || !full_rect) {
      screen->Render(x * 16, y * 16, spr_norm);
      return;
    }

    auto same = [&](int32_t nx, int32_t ny) {
      return level.GetTile(nx, ny) == this &&
             this_type == mutable_level.GetTreeType(nx, ny);
    };
    const bool u = same(x, y - 1);
    const bool l = same(x - 1, y);
    const bool r = same(x + 1, y);
    const bool d = same(x, y + 1);
    const bool ul = same(x - 1, y - 1);
    const bool ur = same(x + 1, y - 1);
    const bool dl = same(x - 1, y + 1);
    const bool dr = same(x + 1, y + 1);

    const int32_t px = x * 16;
    const int32_t py = y * 16;
    if (u && ul && l) screen->Render(px, py, 1, 0, 0, full_rect);
    else screen->Render(px, py, 0, 0, 0, norm_rect);

    if (u && ur && r) screen->Render(px + 8, py, 0, 0, 0, full_rect);
    else screen->Render(px + 8, py, 1, 0, 0, norm_rect);

    if (d && dl && l) screen->Render(px, py + 8, 1, 1, 0, full_rect);
    else screen->Render(px, py + 8, 0, 1, 0, norm_rect);

    if (d && dr && r) screen->Render(px + 8, py + 8, 0, 1, 0, full_rect);
    else screen->Render(px + 8, py + 8, 1, 1, 0, norm_rect);
  }
};

class SaplingTile : public Tile {
 public:
  SaplingTile(uint16_t id, std::string name, uint16_t base_tile,
              uint16_t grown_tile)
      : Tile(id, std::move(name), "sapling"),
        base_tile_(base_tile),
        grown_tile_(grown_tile) {
    connects_to_grass = (base_tile == Tiles::kGrass);
    connects_to_sand = (base_tile == Tiles::kSand);
    may_spawn = true;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    const int16_t age = static_cast<int16_t>(level.GetData(xt, yt) + 1);
    if (age > 110) {
      if (!level.IsEntityOnTile(xt, yt)) {
        level.SetTile(xt, yt, grown_tile_, 0);
        return true;
      }
      return false;
    }
    level.SetData(xt, yt, age);
    return false;
  }

  void Hurt(Level& level, int32_t x, int32_t y, int32_t /*dmg*/) const override {
    level.SetTile(x, y, base_tile_, 0);
    io::Sound::Play("monsterhurt");
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t dmg, entity::Direction /*dir*/) const override {
    Hurt(level, x, y, dmg);
    return true;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    Tiles::Get(base_tile_)->Render(screen, level, x, y);
    Tile::Render(screen, level, x, y);
  }

 private:
  uint16_t base_tile_;
  uint16_t grown_tile_;
};

class SandTile : public Tile {
 public:
  SandTile()
      : Tile(Tiles::kSand, "Sand", "sand"),
        border_(gfx::SpriteType::kTile, "sand_border") {
    connects_to_sand = true;
    may_spawn = true;
  }

  void SteppedOn(Level& level, int32_t /*xt*/, int32_t /*yt*/,
                 entity::Entity& entity) const override {
    if (auto* mob = dynamic_cast<entity::Mob*>(&entity)) {
      if (mob->walk_dist % 8 == 0) {
        int32_t spawn_x = entity.x - 4;
        int32_t spawn_y = entity.y - 4;
        const int32_t side = ((mob->walk_dist >> 3) % 2 == 0) ? 1 : -1;
        const int32_t offset = (util::GlobalRandom().NextInt(2) + 2) * side;
        switch (mob->dir) {
          case entity::Direction::kNone:
            return;
          case entity::Direction::kUp:
          case entity::Direction::kDown:
            spawn_x += offset;
            break;
          case entity::Direction::kLeft:
          case entity::Direction::kRight:
            spawn_y += offset;
            break;
        }
        level.Add(std::make_shared<entity::SandParticle>(spawn_x, spawn_y));
      }
    }
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kShovel &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, Tiles::kHole, 0);
      level.DropItem(xt * 16 + 8, yt * 16 + 8, "Sand");
      io::Sound::Play("monsterhurt");
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    RenderConnected(screen, level, x, y, sprite_, border_, nullptr,
                    [&](const Tile* t, int32_t nx, int32_t ny) {
                      return t->ConnectsToSand(level, nx, ny);
                    });
  }

 private:
  mutable gfx::LinkedSprite border_;
};

class CactusTile : public Tile {
 public:
  CactusTile() : Tile(Tiles::kCactus, "Cactus", "cactus") {
    connects_to_sand = true;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return false;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    const int16_t damage = level.GetData(xt, yt);
    if (damage > 0) {
      level.SetData(xt, yt, static_cast<int16_t>(damage - 1));
      return true;
    }
    return false;
  }

  void BumpedInto(Level& /*level*/, int32_t x, int32_t y,
                  entity::Entity& entity) const override {
    if (auto* boat = dynamic_cast<entity::Boat*>(&entity)) {
      boat->Hurt();
    }
    if (auto* mob = dynamic_cast<entity::Mob*>(&entity)) {
      const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
      mob->Hurt(*this, x, y, diff == 2 ? 2 : 1);
    }
  }

  void Hurt(Level& level, int32_t x, int32_t y, int32_t dmg) const override {
    constexpr int32_t kCactusHealth = 10;
    const bool creative = game::GameState::Instance().IsCreativeMode();
    int32_t damage = level.GetData(x, y) + dmg;
    if (creative) {
      dmg = kCactusHealth;
      damage = kCactusHealth;
    }
    level.Add(std::make_shared<entity::SmashParticle>(x * 16, y * 16));
    level.Add(std::make_shared<entity::TextParticle>(
        std::to_string(dmg), x * 16 + 8, y * 16 + 8, gfx::color::kRed));
    if (damage >= kCactusHealth) {
      level.SetTile(x, y, Tiles::kSand, 0);
      io::Sound::Play("monsterhurt");
      level.DropItem(x * 16 + 8, y * 16 + 8, 2, 4, "Cactus");
    } else {
      level.SetData(x, y, static_cast<int16_t>(damage));
    }
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t dmg, entity::Direction /*dir*/) const override {
    Hurt(level, x, y, dmg);
    return true;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    Tiles::Get(Tiles::kSand)->Render(screen, level, x, y);
    Tile::Render(screen, level, x, y);
  }
};

class OreTile : public Tile {
 public:
  OreTile(uint16_t id, std::string name, std::string key, std::string drop_name,
          int32_t light = 0, uint16_t base_tile = Tiles::kDirt)
      : Tile(id, std::move(name), std::move(key)),
        drop_name_(std::move(drop_name)),
        light_(light),
        base_tile_(base_tile) {}

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return false;
  }

  int32_t GetLightRadius(const Level& /*level*/, int32_t /*x*/,
                         int32_t /*y*/) const override {
    return light_;
  }

  void Hurt(Level& level, int32_t x, int32_t y, int32_t dmg) const override {
    auto& rng = util::GlobalRandom();
    const bool creative = game::GameState::Instance().IsCreativeMode();
    const int32_t ore_h = rng.NextInt(10) * 4 + 20;
    int32_t damage = level.GetData(x, y) + dmg;
    if (creative) {
      dmg = ore_h;
      damage = ore_h;
    }

    level.Add(std::make_shared<entity::SmashParticle>(x * 16, y * 16));
    io::Sound::Play("monsterhurt");
    level.Add(std::make_shared<entity::TextParticle>(
        std::to_string(dmg), x * 16 + 8, y * 16 + 8, gfx::color::kRed));

    if (dmg > 0) {
      int32_t count = rng.NextInt(2);
      if (damage >= ore_h) {
        level.SetTile(x, y, base_tile_, 0);
        count += 2;
      } else {
        level.SetData(x, y, static_cast<int16_t>(damage));
      }
      if (drop_name_ == "Gem") {
        util::AchievementManager::Instance().SetAchievement(
            "minicraft.achievement.find_gem", true, creative);
      }
      if (count > 0) {
        level.DropItem(x * 16 + 8, y * 16 + 8, count, count, drop_name_);
      }
    }
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t /*dmg*/, entity::Direction /*dir*/) const override {
    Hurt(level, x, y, 0);
    return true;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    if (game::GameState::Instance().IsCreativeMode()) return false;
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kPickaxe) {
      if (player.PayStamina(6 - tool->level) && tool->PayDurability()) {
        Hurt(level, xt, yt, tool->GetDamage());
        return true;
      }
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    Tiles::Get(base_tile_)->Render(screen, level, x, y);
    Tile::Render(screen, level, x, y);
  }

 private:
  std::string drop_name_;
  int32_t light_ = 0;
  uint16_t base_tile_;
};

class InfiniteFallTile : public Tile {
 public:
  InfiniteFallTile()
      : Tile(Tiles::kInfiniteFall, "Infinite Fall", "cloud_background") {}
  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& e) const override {
    if (dynamic_cast<const entity::AirWizard*>(&e) != nullptr ||
        dynamic_cast<const entity::Arrow*>(&e) != nullptr) {
      return true;
    }
    if (game::GameState::Instance().IsCreativeMode() &&
        dynamic_cast<const entity::Player*>(&e) != nullptr) {
      return true;
    }
    return false;
  }
};

class CloudTile : public Tile {
 public:
  CloudTile()
      : Tile(Tiles::kCloud, "Cloud", "cloud"),
        border_(gfx::SpriteType::kTile, "cloud_border"),
        corner_(gfx::SpriteType::kTile, "cloud_corner") {
    may_spawn = true;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return true;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kShovel) {
      if (player.PayStamina(5)) {
        level.SetTile(xt, yt, Tiles::kInfiniteFall, 0);
        io::Sound::Play("monsterhurt");
        level.DropItem(xt * 16 + 8, yt * 16 + 8, 1, 3, "Cloud");
        return true;
      }
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    Tiles::Get(Tiles::kInfiniteFall)->Render(screen, level, x, y);
    RenderConnected(screen, level, x, y, sprite_, border_, &corner_,
                    [](const Tile* t, int32_t, int32_t) {
                      return t->id != Tiles::kInfiniteFall;
                    });
  }

 private:
  mutable gfx::LinkedSprite border_;
  mutable gfx::LinkedSprite corner_;
};

}  // namespace

void RegisterNatureTiles(std::vector<std::unique_ptr<Tile>>& out) {
  out[Tiles::kGrass] = std::make_unique<GrassTile>();
  out[Tiles::kDirt] = std::make_unique<DirtTile>();
  out[Tiles::kFlower] = std::make_unique<FlowerTile>();
  out[Tiles::kHole] = std::make_unique<HoleTile>();
  out[Tiles::kStairsUp] = std::make_unique<StairsTile>(Tiles::kStairsUp, true);
  out[Tiles::kStairsDown] =
      std::make_unique<StairsTile>(Tiles::kStairsDown, false);
  out[Tiles::kWater] = std::make_unique<WaterTile>();
  out[Tiles::kRock] = std::make_unique<RockTile>();
  out[Tiles::kTree] = std::make_unique<TreeTile>();
  out[Tiles::kTreeSapling] = std::make_unique<SaplingTile>(
      Tiles::kTreeSapling, "Tree Sapling", Tiles::kGrass, Tiles::kTree);
  out[Tiles::kSand] = std::make_unique<SandTile>();
  out[Tiles::kCactus] = std::make_unique<CactusTile>();
  out[Tiles::kCactusSapling] = std::make_unique<SaplingTile>(
      Tiles::kCactusSapling, "Cactus Sapling", Tiles::kSand, Tiles::kCactus);
  out[Tiles::kIronOre] = std::make_unique<OreTile>(
      Tiles::kIronOre, "Iron Ore", "iron_ore", "Iron Ore", 0);
  out[Tiles::kGoldOre] = std::make_unique<OreTile>(
      Tiles::kGoldOre, "Gold Ore", "gold_ore", "Gold Ore", 0);
  out[Tiles::kGemOre] = std::make_unique<OreTile>(
      Tiles::kGemOre, "Gem Ore", "gem_ore", "Gem", 0);
  out[Tiles::kLapis] =
      std::make_unique<OreTile>(Tiles::kLapis, "Lapis", "lapis_ore", "Lapis", 0);
  out[Tiles::kLava] = std::make_unique<LavaTile>();
  out[Tiles::kHardRock] = std::make_unique<HardRockTile>();
  out[Tiles::kInfiniteFall] = std::make_unique<InfiniteFallTile>();
  out[Tiles::kCloud] = std::make_unique<CloudTile>();
  out[Tiles::kCloudCactus] = std::make_unique<OreTile>(
      Tiles::kCloudCactus, "Cloud Cactus", "cloud_ore", "Cloud Ore", 0,
      Tiles::kCloud);
}

}  // namespace minicraft::level::detail
