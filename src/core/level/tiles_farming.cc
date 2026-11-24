#include "core/level/tiles_internal.h"

#include <algorithm>

#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/item.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/random.h"

namespace minicraft::level::detail {

namespace {

bool IsCropId(uint16_t id) {
  return id == Tiles::kWheat || id == Tiles::kPotato || id == Tiles::kTomato ||
         id == Tiles::kCarrot || id == Tiles::kHeavenlyBerries ||
         id == Tiles::kHellishBerries;
}

bool IsFarmOrCropId(uint16_t id) {
  return id == Tiles::kFarmland || IsCropId(id);
}

class FarmlandTile : public Tile {
 public:
  FarmlandTile() : FarmlandTile(Tiles::kFarmland, "Farmland", "farmland") {}

  FarmlandTile(uint16_t id, std::string name, std::string sprite_key)
      : Tile(id, std::move(name), std::move(sprite_key)),
        moist_sprite_(gfx::SpriteType::kTile, "farmland_moist") {}

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction /*dir*/) const override {
    if (auto* tool = dynamic_cast<item::ToolItem*>(item)) {
      if (tool->type == item::ToolType::kShovel) {
        if (player.PayStamina(4 - tool->level) && tool->PayDurability()) {
          level.SetTile(xt, yt, Tiles::kDirt, 0);
          io::Sound::Play("monsterhurt");
          return true;
        }
      }
    }
    return false;
  }

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    auto& rng = util::GlobalRandom();
    const uint16_t raw = static_cast<uint16_t>(level.GetData(xt, yt));
    const int32_t moisture = static_cast<int32_t>(raw & 0b111u);
    bool near_water = false;
    for (const gfx::Point& p : level.GetAreaTilePositions(xt, yt, 4)) {
      if (level.GetTile(p.x, p.y)->id == Tiles::kWater) {
        near_water = true;
        break;
      }
    }
    if (near_water) {
      if (moisture < 7 && rng.NextInt(10) == 0) {
        level.SetData(
            xt, yt, static_cast<int16_t>((raw & ~0b111u) | (moisture + 1)));
        return true;
      }
      return false;
    }
    if (moisture > 0 && rng.NextInt(10) == 0) {
      level.SetData(xt, yt,
                    static_cast<int16_t>((raw & ~0b111u) | (moisture - 1)));
      return true;
    }
    if (id == Tiles::kFarmland && moisture == 0 && rng.NextInt(10) == 0) {
      level.SetTile(xt, yt, Tiles::kDirt, 0);
      return true;
    }
    return false;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    if ((static_cast<uint16_t>(level.GetData(x, y)) & 0b111u) > 0) {
      gfx::LinkedSprite copy = moist_sprite_;
      screen->Render(x * 16, y * 16, copy);
    } else {
      Tile::Render(screen, level, x, y);
    }
  }

 private:
  mutable gfx::LinkedSprite moist_sprite_;
};

class CropTile : public FarmlandTile {
 public:
  static constexpr int32_t kMaxStage = 0b111;  // 7

  CropTile(uint16_t id, std::string name, std::string stage_prefix,
           int32_t sprite_max_stage, std::string crop_item,
           std::string seed_item)
      : FarmlandTile(id, std::move(name), stage_prefix + "0"),
        stage_prefix_(std::move(stage_prefix)),
        sprite_max_stage_(sprite_max_stage),
        crop_item_(std::move(crop_item)),
        seed_item_(std::move(seed_item)) {}

  bool Tick(Level& level, int32_t xt, int32_t yt) const override {
    auto& rng = util::GlobalRandom();
    uint16_t data = static_cast<uint16_t>(level.GetData(xt, yt));
    int32_t moisture = static_cast<int32_t>(data & 0b111u);
    bool successful = false;

    bool near_water = false;
    for (const gfx::Point& p : level.GetAreaTilePositions(xt, yt, 4)) {
      if (level.GetTile(p.x, p.y)->id == Tiles::kWater) {
        near_water = true;
        break;
      }
    }
    if (near_water) {
      if (moisture < 7 && rng.NextInt(10) == 0) {
        moisture += 1;
        data = static_cast<uint16_t>((data & ~0b111u) | moisture);
        level.SetData(xt, yt, static_cast<int16_t>(data));
        successful = true;
      }
    } else if (moisture > 0 && rng.NextInt(10) == 0) {
      moisture -= 1;
      data = static_cast<uint16_t>((data & ~0b111u) | moisture);
      level.SetData(xt, yt, static_cast<int16_t>(data));
      successful = true;
    }

    int32_t fertilization = static_cast<int32_t>(data >> 7);
    const int32_t stage = static_cast<int32_t>((data >> 3) & kMaxStage);
    if (stage < kMaxStage) {
      double points = moisture > 0 ? 4.0 : 2.0;
      for (int32_t i = -1; i < 2; ++i) {
        for (int32_t j = -1; j < 2; ++j) {
          const Tile* t = level.GetTile(xt + i, yt + j);
          if ((i != 0 || j != 0) && t && IsFarmOrCropId(t->id)) {
            points += (static_cast<uint16_t>(level.GetData(xt + i, yt + j)) &
                       0b111u) > 0
                          ? 0.75
                          : 0.25;
          }
        }
      }

      const bool up = level.GetTile(xt, yt - 1)->id == this->id;
      const bool down = level.GetTile(xt, yt + 1)->id == this->id;
      const bool left = level.GetTile(xt - 1, yt)->id == this->id;
      const bool right = level.GetTile(xt + 1, yt)->id == this->id;
      const bool up_left = level.GetTile(xt - 1, yt - 1)->id == this->id;
      const bool down_left = level.GetTile(xt - 1, yt + 1)->id == this->id;
      const bool up_right = level.GetTile(xt + 1, yt - 1)->id == this->id;
      const bool down_right = level.GetTile(xt + 1, yt + 1)->id == this->id;
      if (up && down && left && right && up_left && down_left && up_right &&
          down_right) {
        points /= 2.0;
      } else {
        if (up && down && left && right) points *= 0.75;
        if ((up && ((down && (left || right)) || (left && right))) ||
            (down && left && right)) {
          points *= 0.85;
        }
        if ((up_left && (down_right || down_left || up_right)) ||
            (down_left && (up_right || down_right)) ||
            (up_right && down_right)) {
          points *= 0.9;
        }
        if (up_left) points *= 0.98125;
        if (down_left) points *= 0.98125;
        if (up_right) points *= 0.98125;
        if (down_right) points *= 0.98125;
      }

      const int32_t denom = std::max(1, static_cast<int32_t>(100.0 / points) + 1);
      if (rng.NextInt(denom) < (fertilization / 30 + 1)) {
        data = static_cast<uint16_t>(
            (data & ~static_cast<uint32_t>(kMaxStage << 3)) |
            static_cast<uint32_t>((stage + 1) << 3));
        level.SetData(xt, yt, static_cast<int16_t>(data));
      }
      successful = true;
    }

    if (fertilization > 0) {
      data = static_cast<uint16_t>(
          (data & 0x7Fu) | static_cast<uint32_t>((fertilization - 1) << 7));
      level.SetData(xt, yt, static_cast<int16_t>(data));
      successful = true;
    }

    return successful;
  }

  bool Interact(Level& level, int32_t xt, int32_t yt, entity::Player& player,
                item::Item* item, entity::Direction dir) const override {
    if (auto* st = dynamic_cast<item::StackableItem*>(item)) {
      if (item->Name() == "Fertilizer") {
        --st->count;
        auto& rng = util::GlobalRandom();
        for (int32_t i = 0; i < 2; ++i) {
          const double px = xt * 16.0 + 8.0 + (rng.NextGaussian() * 0.5) * 8.0;
          const double py = yt * 16.0 + 8.0 + (rng.NextGaussian() * 0.5) * 8.0;
          level.Add(std::make_shared<entity::Particle>(
              static_cast<int32_t>(px), static_cast<int32_t>(py),
              120 + rng.NextInt(21) - 40,
              gfx::LinkedSprite(gfx::SpriteType::kEntity, "glint")));
        }
        const int32_t fert =
            static_cast<int32_t>(static_cast<uint16_t>(level.GetData(xt, yt)) >> 7);
        int32_t add = 10;
        if (fert < 100) add = 40;
        else if (fert < 200) add = 30;
        else if (fert < 300) add = 25;
        else if (fert < 400) add = 20;
        Fertilize(level, xt, yt, add);
        return true;
      }
    }
    return FarmlandTile::Interact(level, xt, yt, player, item, dir);
  }

  bool Hurt(Level& level, int32_t x, int32_t y, entity::Mob& source,
            int32_t /*dmg*/, entity::Direction /*dir*/) const override {
    Harvest(level, x, y, &source);
    return true;
  }

  void Render(gfx::Screen* screen, const Level& level, int32_t x,
              int32_t y) const override {
    Tiles::Get(Tiles::kFarmland)->Render(screen, level, x, y);
    const int32_t age =
        (static_cast<uint16_t>(level.GetData(x, y)) >> 3) & kMaxStage;
    const int32_t sprite_stage = std::clamp(
        static_cast<int32_t>(static_cast<float>(age) / kMaxStage *
                             static_cast<float>(sprite_max_stage_)),
        0, sprite_max_stage_);
    gfx::LinkedSprite spr(gfx::SpriteType::kTile,
                          stage_prefix_ + std::to_string(sprite_stage));
    screen->Render(x * 16, y * 16, spr);
  }

 private:
  static void Fertilize(Level& level, int32_t x, int32_t y, int32_t amount) {
    const uint16_t data = static_cast<uint16_t>(level.GetData(x, y));
    int32_t fert = static_cast<int32_t>(data >> 7) + amount;
    fert = std::clamp(fert, 0, 511);
    level.SetData(x, y,
                  static_cast<int16_t>((data & 0x7Fu) |
                                       static_cast<uint32_t>(fert << 7)));
  }

  void Harvest(Level& level, int32_t x, int32_t y, entity::Mob* source) const {
    const uint16_t data = static_cast<uint16_t>(level.GetData(x, y));
    const int32_t age = static_cast<int32_t>((data >> 3) & kMaxStage);
    auto& rng = util::GlobalRandom();

    if (!seed_item_.empty()) {
      level.DropItem(x * 16 + 8, y * 16 + 8, seed_item_);
    }
    if (age == kMaxStage) {
      level.DropItem(x * 16 + 8, y * 16 + 8, 2, 4, crop_item_);
      if (auto* p = dynamic_cast<entity::Player*>(source)) {
        p->AddScore(rng.NextInt(5) + 1);
      }
    } else if (seed_item_.empty()) {
      level.DropItem(x * 16 + 8, y * 16 + 8, crop_item_);
    }

    io::Sound::Play("monsterhurt");
    level.SetTile(x, y, Tiles::kFarmland, static_cast<int16_t>(data & 0b111u));
  }

  std::string stage_prefix_;
  int32_t sprite_max_stage_;
  std::string crop_item_;
  std::string seed_item_;
};

}  // namespace

void RegisterFarmingTiles(std::vector<std::unique_ptr<Tile>>& out) {
  out[Tiles::kFarmland] = std::make_unique<FarmlandTile>();
  out[Tiles::kWheat] = std::make_unique<CropTile>(
      Tiles::kWheat, "Wheat", "wheat_stage", 5, "Wheat", "Wheat Seeds");
  out[Tiles::kPotato] = std::make_unique<CropTile>(
      Tiles::kPotato, "Potato", "potato_stage", 5, "Potato", "");
  out[Tiles::kTomato] = std::make_unique<CropTile>(
      Tiles::kTomato, "Tomato", "tomato_stage", 3, "Tomato", "Tomato Seeds");
  out[Tiles::kCarrot] = std::make_unique<CropTile>(
      Tiles::kCarrot, "Carrot", "carrot_stage", 3, "Carrot", "");
  out[Tiles::kHeavenlyBerries] = std::make_unique<CropTile>(
      Tiles::kHeavenlyBerries, "Heavenly Berries", "heavenly_berries_stage", 3,
      "Heavenly Berries", "");
  out[Tiles::kHellishBerries] = std::make_unique<CropTile>(
      Tiles::kHellishBerries, "Hellish Berries", "hellish_berries_stage", 3,
      "Hellish Berries", "");
}

}  // namespace minicraft::level::detail
