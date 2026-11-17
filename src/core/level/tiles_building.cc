#include "core/level/tiles_internal.h"

#include <string>

#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/item.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/localization.h"

namespace minicraft::level::detail {

namespace {

std::string MaterialName(TileMaterial mat) {
  switch (mat) {
    case TileMaterial::kWood:
      return "Wood";
    case TileMaterial::kStone:
      return "Stone";
    case TileMaterial::kObsidian:
      return "Obsidian";
  }
  return "Wood";
}

bool MatchesMaterialTool(TileMaterial mat, const item::ToolItem* tool) {
  if (!tool) return false;
  if (mat == TileMaterial::kWood) {
    return tool->type == item::ToolType::kAxe;
  }
  return tool->type == item::ToolType::kPickaxe;
}

bool IsBossLocked() {
  const auto& gs = game::GameState::Instance();
  return (!gs.obsidian_knight_beaten || gs.obsidian_knight_summoned) &&
         !gs.IsCreativeMode();
}

void RenderUnderlyingTile(gfx::Screen* screen, const Level& level, int32_t x,
                          int32_t y, uint16_t self_id) {
  const int32_t prev = level.GetData(x, y);
  if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(self_id) &&
      Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
    Tiles::Get(static_cast<uint16_t>(prev))->Render(screen, level, x, y);
  } else {
    Tiles::Get(Tiles::kDirt)->Render(screen, level, x, y);
  }
}

class DoorTile : public Tile {
 public:
  DoorTile(uint16_t id, std::string name, std::string closed_key,
           std::string open_key, TileMaterial mat, bool is_boss = false)
      : Tile(id, std::move(name), std::move(closed_key)),
        open_sprite_(gfx::SpriteType::kTile, std::move(open_key)),
        mat_(mat),
        is_boss_(is_boss) {}

  bool MayPass(const Level& level, int32_t x, int32_t y,
               const entity::Entity& /*e*/) const override {
    return level.GetData(x, y) != 0;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (is_boss_ && IsBossLocked()) {
      if (MatchesMaterialTool(mat_, tool) && player.PayStamina(1)) {
        game::GameState::Instance().AddNotification(
            util::Localization::GetLocalized(
                "minicraft.notification.defeat_obsidian_knight_first"));
        io::Sound::Play("monsterhurt");
        return true;
      }
      return false;
    }

    if (MatchesMaterialTool(mat_, tool) &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      const uint16_t floor_id =
          is_boss_ ? Tiles::kBossFloor : static_cast<uint16_t>(id + 3);
      level.SetTile(xt, yt, floor_id, 0);
      io::Sound::Play("monsterhurt");
      level.DropItem(xt * 16 + 8, yt * 16 + 8, MaterialName(mat_) + " Door");
      return true;
    }
    return false;
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& source,
            int32_t /*dmg*/, entity::Direction /*dir*/) const override {
    if (dynamic_cast<entity::Player*>(&source) != nullptr) {
      if (is_boss_ &&
          game::GameState::Instance().obsidian_knight_summoned &&
          !game::GameState::Instance().IsCreativeMode()) {
        game::GameState::Instance().AddNotification(
            util::Localization::GetLocalized(
                "minicraft.notification.defeat_obsidian_knight_first"));
        return true;
      }
      const int16_t cur = level.GetData(x, y);
      level.SetData(x, y, cur == 0 ? 1 : 0);
      io::Sound::Play("craft");
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    if (level.GetData(x, y) != 0) {
      gfx::LinkedSprite copy = open_sprite_;
      screen->Render(x * 16, y * 16, copy);
    } else {
      Tile::Render(screen, level, x, y);
    }
  }

 protected:
  mutable gfx::LinkedSprite open_sprite_;
  TileMaterial mat_;
  bool is_boss_ = false;
};

class FloorTile : public Tile {
 public:
  FloorTile(uint16_t id, std::string name, std::string key, TileMaterial mat,
            std::string drop_item = "", bool is_path = false,
            bool is_boss = false)
      : Tile(id, std::move(name), std::move(key)),
        mat_(mat),
        drop_item_(drop_item.empty() ? this->name : std::move(drop_item)),
        is_path_(is_path),
        is_boss_(is_boss) {
    may_spawn = !is_boss_;
    if (is_path_) {
      connects_to_grass = true;
    }
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (is_boss_ && IsBossLocked()) {
      if (MatchesMaterialTool(mat_, tool) && player.PayStamina(1)) {
        game::GameState::Instance().AddNotification(
            util::Localization::GetLocalized(
                "minicraft.notification.defeat_obsidian_knight_first"));
        io::Sound::Play("monsterhurt");
        return true;
      }
      return false;
    }

    const bool right_tool =
        is_path_ ? (tool && tool->type == item::ToolType::kShovel)
                 : MatchesMaterialTool(mat_, tool);
    if (right_tool && player.PayStamina(4 - tool->level) &&
        tool->PayDurability()) {
      const uint16_t under =
          is_path_ ? Tiles::kHole
                   : (level.depth == 1 ? Tiles::kCloud : Tiles::kHole);
      level.SetTile(xt, yt, under, 0);
      level.DropItem(xt * 16 + 8, yt * 16 + 8, drop_item_);
      io::Sound::Play("monsterhurt");
      return true;
    }
    return false;
  }

 private:
  TileMaterial mat_;
  std::string drop_item_;
  bool is_path_;
  bool is_boss_;
};

class LavaBrickTile : public Tile {
 public:
  LavaBrickTile() : Tile(Tiles::kLavaBrick, "Lava Brick", "lava") {
    may_spawn = false;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& e) const override {
    return e.CanWool();
  }

  void BumpedInto(Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
                  entity::Entity& entity) const override {
    if (auto* mob = dynamic_cast<entity::Mob*>(&entity)) {
      mob->Hurt(*mob, 3, mob->dir);
    }
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kPickaxe &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, Tiles::kLava, 0);
      io::Sound::Play("monsterhurt");
      return true;
    }
    return false;
  }
};

class ExplodedTile : public Tile {
 public:
  ExplodedTile()
      : Tile(Tiles::kExplode, "Explode", "exploded"),
        border_(gfx::SpriteType::kTile, "exploded_border") {
    connects_to_sand = true;
    connects_to_fluid = true;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderConnected(
        screen, level, x, y, sprite_, border_, nullptr,
        [](const Tile* t, int32_t, int32_t) { return t->id == Tiles::kExplode; },
        /*singleton_with_connective=*/false);
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return true;
  }

 private:
  mutable gfx::LinkedSprite border_;
};

class WallTile : public Tile {
 public:
  WallTile(uint16_t id, std::string name, std::string key, std::string bkey,
           TileMaterial mat, bool is_boss = false)
      : Tile(id, std::move(name), std::move(key)),
        has_border_(!bkey.empty()),
        border_(gfx::SpriteType::kTile, std::move(bkey)),
        mat_(mat),
        is_boss_(is_boss) {}

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
    constexpr int32_t kWallHealth = 100;
    const bool creative = game::GameState::Instance().IsCreativeMode();
    int32_t damage = level.GetData(x, y) + dmg;
    if (creative) {
      dmg = kWallHealth;
      damage = kWallHealth;
    }

    level.Add(std::make_shared<entity::SmashParticle>(x * 16 + 8, y * 16 + 8));
    io::Sound::Play("monsterhurt");
    level.Add(std::make_shared<entity::TextParticle>(
        std::to_string(dmg), x * 16 + 8, y * 16 + 8, gfx::color::kRed));

    if (damage >= kWallHealth) {
      std::string item_name = "Plank";
      uint16_t floor_id = Tiles::kWoodPlanks;
      if (mat_ == TileMaterial::kStone) {
        item_name = "Stone Brick";
        floor_id = Tiles::kStoneBricks;
      } else if (mat_ == TileMaterial::kObsidian) {
        item_name = "Obsidian Brick";
        floor_id = Tiles::kObsidian;
      }
      const int32_t max_drop = 3 - static_cast<int32_t>(mat_);
      level.DropItem(x * 16 + 8, y * 16 + 8, 1, std::max(1, max_drop),
                     item_name);
      level.SetTile(x, y, floor_id, 0);
    } else {
      level.SetData(x, y, static_cast<int16_t>(damage));
    }
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t /*dmg*/, entity::Direction /*dir*/) const override {
    if (level.depth != -3 || mat_ != TileMaterial::kObsidian ||
        game::GameState::Instance().air_wizard_beaten ||
        game::GameState::Instance().IsCreativeMode()) {
      Hurt(level, x, y, 0);
      return true;
    }
    game::GameState::Instance().AddNotification(
        util::Localization::GetLocalized(
            "minicraft.notification.defeat_air_wizard_first"));
    return false;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    const auto& gs = game::GameState::Instance();
    if (gs.IsCreativeMode()) return false;
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (is_boss_ && IsBossLocked()) {
      if (MatchesMaterialTool(mat_, tool) && player.PayStamina(1)) {
        game::GameState::Instance().AddNotification(
            util::Localization::GetLocalized(
                "minicraft.notification.defeat_obsidian_knight_first"));
        io::Sound::Play("monsterhurt");
        return true;
      }
      return false;
    }
    if (!MatchesMaterialTool(mat_, tool)) return false;
    if (level.depth != -3 || mat_ != TileMaterial::kObsidian ||
        gs.air_wizard_beaten) {
      if (player.PayStamina(4 - tool->level) && tool->PayDurability()) {
        Hurt(level, xt, yt, tool->GetDamage());
        return true;
      }
    } else {
      game::GameState::Instance().AddNotification(
          util::Localization::GetLocalized(
              "minicraft.notification.defeat_air_wizard_first"));
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderDirtBackground(screen, level, x, y);
    if (!has_border_) {
      Tile::Render(screen, level, x, y);
      return;
    }
    RenderConnected(screen, level, x, y, sprite_, border_, nullptr,
                    [this](const Tile* t, int32_t, int32_t) {
                      return t->id == this->id ||
                             t->id == Tiles::kWoodWall ||
                             t->id == Tiles::kStoneWall ||
                             t->id == Tiles::kObsidianWall ||
                             t->id == Tiles::kBossWall ||
                             t->id == Tiles::kWoodDoor ||
                             t->id == Tiles::kStoneDoor ||
                             t->id == Tiles::kObsidianDoor ||
                             t->id == Tiles::kBossDoor;
                    },
                    /*singleton_with_connective=*/false);
  }

 private:
  bool has_border_;
  mutable gfx::LinkedSprite border_;
  TileMaterial mat_;
  bool is_boss_;
};

class MaterialTile : public Tile {
 public:
  MaterialTile(uint16_t id, std::string name, std::string sprite_key,
               TileMaterial mat, std::string drop_name)
      : Tile(id, std::move(name), std::move(sprite_key)),
        mat_(mat),
        drop_name_(std::move(drop_name)) {
    may_spawn = true;
  }

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return true;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (MatchesMaterialTool(mat_, tool) &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, level.depth == 1 ? Tiles::kCloud : Tiles::kHole, 0);
      io::Sound::Play("monsterhurt");
      level.DropItem(xt * 16 + 8, yt * 16 + 8, drop_name_);
      return true;
    }
    return false;
  }

 private:
  TileMaterial mat_;
  std::string drop_name_;
};

class WoolTile : public Tile {
 public:
  WoolTile(uint16_t id, std::string name, std::string key)
      : Tile(id, std::move(name), std::move(key)) {}

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& e) const override {
    return e.CanWool();
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (tool && tool->type == item::ToolType::kShears &&
        player.PayStamina(3 - tool->level) && tool->PayDurability()) {
      level.SetTile(xt, yt, Tiles::kHole, 0);
      io::Sound::Play("monsterhurt");
      level.DropItem(xt * 16 + 8, yt * 16 + 8, name);
      return true;
    }
    return false;
  }
};

class FenceTile : public Tile {
 public:
  FenceTile(uint16_t id, std::string name, std::string base_key, TileMaterial mat)
      : Tile(id, std::move(name), base_key),
        base_key_(std::move(base_key)),
        mat_(mat) {}

  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return false;
  }

  bool ConnectsToSand(const Level& level, int32_t x, int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))->ConnectsToSand(level, x, y);
    }
    return false;
  }

  bool ConnectsToFluid(const Level& level, int32_t x,
                       int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))
          ->ConnectsToFluid(level, x, y);
    }
    return false;
  }

  bool ConnectsToGrass(const Level& level, int32_t x,
                       int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))
          ->ConnectsToGrass(level, x, y);
    }
    return false;
  }

  static bool ConnectsTo(const Tile* t) {
    return t->id == Tiles::kWoodFence || t->id == Tiles::kStoneFence ||
           t->id == Tiles::kObsidianFence;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderUnderlyingTile(screen, level, x, y, id);
    Tile::Render(screen, level, x, y);
    if (ConnectsTo(level.GetTile(x, y - 1))) {
      gfx::LinkedSprite s(gfx::SpriteType::kTile, base_key_ + "_top");
      screen->Render(x * 16, y * 16, s);
    }
    if (ConnectsTo(level.GetTile(x, y + 1))) {
      gfx::LinkedSprite s(gfx::SpriteType::kTile, base_key_ + "_bottom");
      screen->Render(x * 16, y * 16, s);
    }
    if (ConnectsTo(level.GetTile(x - 1, y))) {
      gfx::LinkedSprite s(gfx::SpriteType::kTile, base_key_ + "_left");
      screen->Render(x * 16, y * 16, s);
    }
    if (ConnectsTo(level.GetTile(x + 1, y))) {
      gfx::LinkedSprite s(gfx::SpriteType::kTile, base_key_ + "_right");
      screen->Render(x * 16, y * 16, s);
    }
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    if (game::GameState::Instance().IsCreativeMode()) return false;
    auto* tool = dynamic_cast<item::ToolItem*>(item);
    if (MatchesMaterialTool(mat_, tool) &&
        player.PayStamina(4 - tool->level) && tool->PayDurability()) {
      const int32_t prev = level.GetData(xt, yt);
      const uint16_t restore = (prev >= 0 && prev < 255 && prev != id)
                                   ? static_cast<uint16_t>(prev)
                                   : Tiles::kDirt;
      io::Sound::Play("monsterhurt");
      level.DropItem(xt * 16 + 8, yt * 16 + 8, name);
      level.SetTile(xt, yt, restore, 0);
      return true;
    }
    return false;
  }

  void Hurt(Level& level, int32_t x, int32_t y, int32_t /*dmg*/) const override {
    if (game::GameState::Instance().IsCreativeMode()) {
      const int32_t prev = level.GetData(x, y);
      const uint16_t restore = (prev >= 0 && prev < 255 && prev != id)
                                   ? static_cast<uint16_t>(prev)
                                   : Tiles::kDirt;
      level.Add(std::make_shared<entity::SmashParticle>(x * 16 + 8, y * 16 + 8));
      io::Sound::Play("monsterhurt");
      level.DropItem(x * 16 + 8, y * 16 + 8, name);
      level.SetTile(x, y, restore, 0);
    }
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& /*source*/,
            int32_t dmg, entity::Direction /*dir*/) const override {
    Hurt(level, x, y, dmg);
    return true;
  }

 private:
  std::string base_key_;
  TileMaterial mat_;
};

class TorchTile : public Tile {
 public:
  TorchTile() : Tile(Tiles::kTorch, "Torch", "torch") {}

  int32_t GetLightRadius(const Level& /*level*/, int32_t /*x*/,
                         int32_t /*y*/) const override {
    return 5;
  }

  bool ConnectsToSand(const Level& level, int32_t x, int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))->ConnectsToSand(level, x, y);
    }
    return false;
  }

  bool ConnectsToFluid(const Level& level, int32_t x,
                       int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))
          ->ConnectsToFluid(level, x, y);
    }
    return false;
  }

  bool ConnectsToGrass(const Level& level, int32_t x,
                       int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))
          ->ConnectsToGrass(level, x, y);
    }
    return false;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& /*p*/,
                item::Item* item, entity::Direction /*dir*/) const override {
    if (dynamic_cast<item::PowerGloveItem*>(item) != nullptr) {
      const int32_t prev = level.GetData(xt, yt);
      const uint16_t restore = (prev >= 0 && prev < 255 && prev != id)
                                   ? static_cast<uint16_t>(prev)
                                   : Tiles::kDirt;
      level.SetTile(xt, yt, restore, 0);
      io::Sound::Play("monsterhurt");
      level.DropItem(xt * 16 + 8, yt * 16 + 8, "Torch");
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderUnderlyingTile(screen, level, x, y, id);
    Tile::Render(screen, level, x, y);
  }
};

class SignTile : public Tile {
 public:
  SignTile() : Tile(Tiles::kSign, "Sign", "sign") {}

  bool ConnectsToSand(const Level& level, int32_t x, int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))->ConnectsToSand(level, x, y);
    }
    return false;
  }

  bool ConnectsToFluid(const Level& level, int32_t x,
                       int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))
          ->ConnectsToFluid(level, x, y);
    }
    return false;
  }

  bool ConnectsToGrass(const Level& level, int32_t x,
                       int32_t y) const override {
    const int32_t prev = level.GetData(x, y);
    if (prev >= 0 && prev < 255 && prev != static_cast<int32_t>(id) &&
        Tiles::ContainsTile(static_cast<uint16_t>(prev))) {
      return Tiles::Get(static_cast<uint16_t>(prev))
          ->ConnectsToGrass(level, x, y);
    }
    return false;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& /*p*/,
                item::Item* item, entity::Direction /*dir*/) const override {
    if (item != nullptr) {
      auto* tool = dynamic_cast<item::ToolItem*>(item);
      if (tool && tool->type == item::ToolType::kAxe) {
        const int32_t prev = level.GetData(xt, yt);
        const uint16_t restore = (prev >= 0 && prev < 255 && prev != id)
                                     ? static_cast<uint16_t>(prev)
                                     : Tiles::kDirt;
        level.SetTile(xt, yt, restore, 0);
        game::GameState::Instance().RemoveSign(level.depth, xt, yt);
        io::Sound::Play("monsterhurt");
        level.DropItem(xt * 16 + 8, yt * 16 + 8, "Sign");
        return true;
      }
      return false;
    }
    game::GameState::Instance().OpenSignEditor(level.depth, xt, yt);
    return true;
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& source,
            int32_t /*dmg*/, entity::Direction /*dir*/) const override {
    if (dynamic_cast<entity::Player*>(&source) != nullptr) {
      game::GameState::Instance().OpenSignEditor(level.depth, x, y);
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    RenderUnderlyingTile(screen, level, x, y, id);
    Tile::Render(screen, level, x, y);
  }
};

class ConnectTile : public Tile {
 public:
  ConnectTile() : Tile(Tiles::kConnectTile, "Connect", "missing_tile") {}
  bool MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
               const entity::Entity& /*e*/) const override {
    return false;
  }
};

}  // namespace

void RegisterBuildingTiles(std::vector<std::unique_ptr<Tile>>& out) {
  out[Tiles::kLavaBrick] = std::make_unique<LavaBrickTile>();
  out[Tiles::kExplode] = std::make_unique<ExplodedTile>();

  out[Tiles::kWoodDoor] = std::make_unique<DoorTile>(
      Tiles::kWoodDoor, "Wood Door", "wood_door", "wood_door_opened",
      TileMaterial::kWood);
  out[Tiles::kStoneDoor] = std::make_unique<DoorTile>(
      Tiles::kStoneDoor, "Stone Door", "stone_door", "stone_door_opened",
      TileMaterial::kStone);
  out[Tiles::kObsidianDoor] = std::make_unique<DoorTile>(
      Tiles::kObsidianDoor, "Obsidian Door", "obsidian_door",
      "obsidian_door_opened", TileMaterial::kObsidian);
  out[Tiles::kBossDoor] = std::make_unique<DoorTile>(
      Tiles::kBossDoor, "Obsidian Boss Door", "obsidian_door",
      "obsidian_door_opened", TileMaterial::kObsidian, true);

  out[Tiles::kWoodPlanks] = std::make_unique<FloorTile>(
      Tiles::kWoodPlanks, "Wood Planks", "wood_floor", TileMaterial::kWood,
      "Plank");
  out[Tiles::kStoneBricks] = std::make_unique<FloorTile>(
      Tiles::kStoneBricks, "Stone Bricks", "stone_floor", TileMaterial::kStone,
      "Stone Brick");
  out[Tiles::kObsidian] = std::make_unique<FloorTile>(
      Tiles::kObsidian, "Obsidian", "obsidian_floor", TileMaterial::kObsidian,
      "Obsidian Brick");
  out[Tiles::kOrnateStone] = std::make_unique<FloorTile>(
      Tiles::kOrnateStone, "Ornate Stone", "ornate_stone", TileMaterial::kStone);
  out[Tiles::kOrnateObsidian] = std::make_unique<FloorTile>(
      Tiles::kOrnateObsidian, "Ornate Obsidian", "ornate_obsidian",
      TileMaterial::kObsidian);
  out[Tiles::kOrnateWood] = std::make_unique<FloorTile>(
      Tiles::kOrnateWood, "Ornate Wood", "ornate_wood", TileMaterial::kWood);
  out[Tiles::kBossFloor] = std::make_unique<FloorTile>(
      Tiles::kBossFloor, "Obsidian Boss Floor", "obsidian_floor",
      TileMaterial::kObsidian, "Obsidian Brick", false, true);

  out[Tiles::kPath] = std::make_unique<FloorTile>(
      Tiles::kPath, "Path", "path", TileMaterial::kStone, "Dirt", true);

  out[Tiles::kWoodWall] = std::make_unique<WallTile>(
      Tiles::kWoodWall, "Wood Wall", "wood_wall", "wood_wall_border",
      TileMaterial::kWood);
  out[Tiles::kStoneWall] = std::make_unique<WallTile>(
      Tiles::kStoneWall, "Stone Wall", "stone_wall", "stone_wall_border",
      TileMaterial::kStone);
  out[Tiles::kObsidianWall] = std::make_unique<WallTile>(
      Tiles::kObsidianWall, "Obsidian Wall", "obsidian_wall",
      "obsidian_wall_border", TileMaterial::kObsidian);
  out[Tiles::kRawStone] = std::make_unique<MaterialTile>(
      Tiles::kRawStone, "Stone", "stone", TileMaterial::kStone, "Stone");
  out[Tiles::kRawObsidian] = std::make_unique<MaterialTile>(
      Tiles::kRawObsidian, "Raw Obsidian", "obsidian", TileMaterial::kObsidian,
      "Raw Obsidian");
  out[Tiles::kBossWall] = std::make_unique<WallTile>(
      Tiles::kBossWall, "Obsidian Boss Wall", "obsidian_wall",
      "obsidian_wall_border", TileMaterial::kObsidian, true);

  out[Tiles::kWhiteWool] =
      std::make_unique<WoolTile>(Tiles::kWhiteWool, "White Wool", "white_wool");
  out[Tiles::kRedWool] =
      std::make_unique<WoolTile>(Tiles::kRedWool, "Red Wool", "red_wool");
  out[Tiles::kBlueWool] =
      std::make_unique<WoolTile>(Tiles::kBlueWool, "Blue Wool", "blue_wool");
  out[Tiles::kGreenWool] =
      std::make_unique<WoolTile>(Tiles::kGreenWool, "Green Wool", "green_wool");
  out[Tiles::kYellowWool] = std::make_unique<WoolTile>(
      Tiles::kYellowWool, "Yellow Wool", "yellow_wool");
  out[Tiles::kBlackWool] =
      std::make_unique<WoolTile>(Tiles::kBlackWool, "Black Wool", "black_wool");
  out[Tiles::kBrownWool] =
      std::make_unique<WoolTile>(Tiles::kBrownWool, "Brown Wool", "brown_wool");
  out[Tiles::kPurpleWool] = std::make_unique<WoolTile>(
      Tiles::kPurpleWool, "Purple Wool", "purple_wool");
  out[Tiles::kCyanWool] =
      std::make_unique<WoolTile>(Tiles::kCyanWool, "Cyan Wool", "cyan_wool");
  out[Tiles::kLightGrayWool] = std::make_unique<WoolTile>(
      Tiles::kLightGrayWool, "Light Gray Wool", "light_gray_wool");
  out[Tiles::kGrayWool] =
      std::make_unique<WoolTile>(Tiles::kGrayWool, "Gray Wool", "gray_wool");
  out[Tiles::kPinkWool] =
      std::make_unique<WoolTile>(Tiles::kPinkWool, "Pink Wool", "pink_wool");
  out[Tiles::kLimeWool] =
      std::make_unique<WoolTile>(Tiles::kLimeWool, "Lime Wool", "lime_wool");
  out[Tiles::kLightBlueWool] = std::make_unique<WoolTile>(
      Tiles::kLightBlueWool, "Light Blue Wool", "light_blue_wool");
  out[Tiles::kMagentaWool] = std::make_unique<WoolTile>(
      Tiles::kMagentaWool, "Magenta Wool", "magenta_wool");
  out[Tiles::kOrangeWool] = std::make_unique<WoolTile>(
      Tiles::kOrangeWool, "Orange Wool", "orange_wool");

  out[Tiles::kWoodFence] = std::make_unique<FenceTile>(
      Tiles::kWoodFence, "Wood Fence", "wood_fence", TileMaterial::kWood);
  out[Tiles::kStoneFence] = std::make_unique<FenceTile>(
      Tiles::kStoneFence, "Stone Fence", "stone_fence", TileMaterial::kStone);
  out[Tiles::kObsidianFence] = std::make_unique<FenceTile>(
      Tiles::kObsidianFence, "Obsidian Fence", "obsidian_fence",
      TileMaterial::kObsidian);

  out[Tiles::kTorch] = std::make_unique<TorchTile>();
  out[Tiles::kSign] = std::make_unique<SignTile>();
  out[Tiles::kConnectTile] = std::make_unique<ConnectTile>();
}

}  // namespace minicraft::level::detail
