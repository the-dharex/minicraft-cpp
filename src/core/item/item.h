#ifndef MINICRAFT_CORE_ITEM_ITEM_H_
#define MINICRAFT_CORE_ITEM_ITEM_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/gfx/screen.h"
#include "core/gfx/sprite_atlas.h"
#include "core/util/advancements.h"

namespace minicraft {
namespace level {
class Level;
class Tile;
}  // namespace level

namespace entity {
class Entity;
class Player;
class Furniture;
enum class Direction : uint8_t;
}  // namespace entity

namespace item {

enum class ItemKind : uint8_t {
  kPowerGlove = 0,
  kFurniture,
  kBucket,
  kBook,
  kTile,
  kTool,
  kFood,
  kStackable,
  kClothing,
  kArmor,
  kPotion,
  kFishingRod,
  kSummon,
  kHeart,
  kWateringCan,
  kDye,
  kWool,
  kEntitySummon,
  kUnknown,
};

enum class DyeColor : uint8_t {
  kBlack = 0,
  kRed,
  kGreen,
  kBrown,
  kBlue,
  kPurple,
  kCyan,
  kLightGray,
  kGray,
  kPink,
  kLime,
  kYellow,
  kLightBlue,
  kMagenta,
  kOrange,
  kWhite,
  kCount
};

int32_t DyeColorRgb(DyeColor color);
const char* DyeColorDisplayName(DyeColor color);
const char* DyeColorSpritePrefix(DyeColor color);

enum class ToolType : uint8_t {
  kShovel = 0,
  kHoe,
  kSword,
  kPickaxe,
  kAxe,
  kBow,
  kClaymore,
  kShears,
  kCount
};

struct ToolSpec {
  int32_t x_pos = 0;
  int32_t y_pos = 13;
  int32_t durability = 30;
  bool no_level = false;
  const char* name = "";
};

const ToolSpec& GetToolSpec(ToolType type);

enum class PotionType : uint8_t {
  kAwkward = 0,
  kSpeed,
  kLight,
  kSwim,
  kEnergy,
  kRegen,
  kHealth,
  kTime,
  kLava,
  kShield,
  kHaste,
  kEscape,
  kCount
};

struct PotionSpec {
  int32_t disp_color = 0;
  int32_t duration = 0;
  const char* name = "";
};

const PotionSpec& GetPotionSpec(PotionType type);
int32_t PotionTypeToEffectIdx(PotionType type);
bool ApplyPotionEffect(entity::Player* player, PotionType type, bool add);
bool ApplyPotionEffectWithTime(entity::Player* player, PotionType type,
                               int32_t time_ticks);

enum class BucketFill : uint8_t {
  kEmpty = 0,
  kWater = 1,
  kLava = 2,
};

class Item {
 public:
  explicit Item(std::string name);
  Item(std::string name, gfx::LinkedSprite sprite);
  virtual ~Item() = default;

  virtual ItemKind Kind() const = 0;

  virtual void RenderHud(gfx::Screen* screen, int32_t x, int32_t y,
                         int32_t font_color) const;

  virtual bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                          int32_t yt, entity::Player* player,
                          entity::Direction attack_dir);

  virtual bool InteractsWithWorld() const { return true; }
  virtual bool IsDepleted() const { return false; }
  virtual bool CanAttack() const { return false; }
  virtual int32_t GetAttackDamageBonus(const entity::Entity* e) { return 0; }

  virtual bool Equals(const Item& other) const;
  virtual std::unique_ptr<Item> Clone() const = 0;

  const std::string& Name() const { return name_; }
  virtual std::string GetData() const { return name_; }
  virtual std::string GetDisplayName() const;
  virtual std::string GetDescription() const { return name_; }

  virtual bool IsStackable() const { return false; }
  virtual bool IsTool() const { return false; }

  virtual util::ItemSnapshot ToSnapshot() const;

  gfx::LinkedSprite sprite;
  bool used_pending = false;

 protected:
  std::string name_;
};

class PowerGloveItem : public Item {
 public:
  PowerGloveItem() : Item("Power Glove") {}
  ItemKind Kind() const override { return ItemKind::kPowerGlove; }
  void RenderHud(gfx::Screen* screen, int32_t x, int32_t y,
                 int32_t font_color) const override;
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<PowerGloveItem>(*this);
  }
};

class StackableItem : public Item {
 public:
  StackableItem(std::string name, gfx::LinkedSprite sprite, int32_t count = 1);

  ItemKind Kind() const override { return ItemKind::kStackable; }
  bool IsStackable() const override { return true; }
  virtual bool StacksWith(const Item& other) const;
  bool IsDepleted() const override { return count <= 0; }
  std::string GetData() const override;
  std::string GetDisplayName() const override;
  util::ItemSnapshot ToSnapshot() const override;
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<StackableItem>(*this);
  }

  int32_t count = 1;
  int32_t max_count = 100;

 protected:
  bool ConsumeOnSuccess(bool success);
};

class UnknownItem : public StackableItem {
 public:
  explicit UnknownItem(std::string req_name)
      : StackableItem(std::move(req_name),
                      gfx::LinkedSprite(gfx::SpriteType::kItem, "missing_item"),
                      1) {}
  ItemKind Kind() const override { return ItemKind::kUnknown; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<UnknownItem>(*this);
  }
};

class FoodItem : public StackableItem {
 public:
  FoodItem(std::string name, gfx::LinkedSprite sprite, int32_t feed,
           int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kFood; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<FoodItem>(*this);
  }

 private:
  int32_t feed_ = 1;
};

class ArmorItem : public StackableItem {
 public:
  ArmorItem(std::string name, gfx::LinkedSprite sprite, float armor_ratio,
            int32_t level, int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kArmor; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<ArmorItem>(*this);
  }

  int32_t level = 1;

 private:
  float armor_ = 0.3f;
};

class ClothingItem : public StackableItem {
 public:
  ClothingItem(std::string name, gfx::LinkedSprite sprite, int32_t player_col,
               int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kClothing; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  int32_t PlayerColor() const { return player_col_; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<ClothingItem>(*this);
  }

 private:
  int32_t player_col_ = 0;
};

class PotionItem : public StackableItem {
 public:
  explicit PotionItem(PotionType type, int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kPotion; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  bool Equals(const Item& other) const override;
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<PotionItem>(*this);
  }

  PotionType type = PotionType::kAwkward;
};

class HeartItem : public StackableItem {
 public:
  HeartItem(std::string name, gfx::LinkedSprite sprite, int32_t health_add,
            int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kHeart; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<HeartItem>(*this);
  }

 private:
  int32_t health_ = 5;
};

class DyeItem : public StackableItem {
 public:
  DyeItem(std::string name, gfx::LinkedSprite sprite, DyeColor col,
          int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kDye; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<DyeItem>(*this);
  }

  DyeColor color = DyeColor::kWhite;
};

class BookItem : public Item {
 public:
  using ContentProvider = std::function<std::string()>;
  BookItem(std::string title, gfx::LinkedSprite sprite, ContentProvider book,
           bool has_title_page = false);
  ItemKind Kind() const override { return ItemKind::kBook; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<BookItem>(*this);
  }

 private:
  ContentProvider book_;
  bool has_title_page_ = false;
};

class BucketItem : public StackableItem {
 public:
  explicit BucketItem(BucketFill fill, int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kBucket; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool Equals(const Item& other) const override;
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<BucketItem>(*this);
  }

  BucketFill filling = BucketFill::kEmpty;

 private:
  std::unique_ptr<BucketItem> EditBucket(entity::Player* player,
                                         BucketFill new_fill);
};

class TileItem : public StackableItem {
 public:
  using TileDataGetter = std::function<int32_t(
      const level::Tile& model, const level::Tile& target, level::Level* level,
      int32_t xt, int32_t yt, entity::Player* player, entity::Direction dir)>;

  struct TileModel {
    std::string tile_name;
    TileDataGetter data_getter;
    bool keep_data = false;
    int32_t fixed_data = -1;
  };

  TileItem(std::string name, gfx::LinkedSprite sprite, TileModel model,
           std::vector<std::string> valid_tiles, int32_t count = 1);

  ItemKind Kind() const override { return ItemKind::kTile; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool Equals(const Item& other) const override;
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<TileItem>(*this);
  }

  TileModel model;
  std::vector<std::string> valid_tiles;
};

class WoolItem : public TileItem {
 public:
  WoolItem(std::string name, gfx::LinkedSprite sprite, DyeColor col,
           int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kWool; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<WoolItem>(*this);
  }

  DyeColor color = DyeColor::kWhite;
};

class ToolItem : public Item {
 public:
  ToolItem(ToolType type, int32_t level);
  explicit ToolItem(ToolType type);

  ItemKind Kind() const override { return ItemKind::kTool; }
  bool IsTool() const override { return true; }
  std::string GetDisplayName() const override;
  bool IsDepleted() const override;
  bool CanAttack() const override { return type != ToolType::kShears; }
  bool PayDurability();
  int32_t GetDamage() const;
  int32_t GetAttackDamageBonus(const entity::Entity* e) override;
  std::string GetData() const override;
  bool Equals(const Item& other) const override;
  util::ItemSnapshot ToSnapshot() const override;
  std::unique_ptr<Item> Clone() const override;

  ToolType type = ToolType::kSword;
  int32_t level = 0;
  int32_t dur = 0;
  int32_t damage = 10;
};

class FishingRodItem : public Item {
 public:
  explicit FishingRodItem(int32_t level);
  ItemKind Kind() const override { return ItemKind::kFishingRod; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool CanAttack() const override { return false; }
  bool IsDepleted() const override;
  std::unique_ptr<Item> Clone() const override;

  static int32_t GetChance(int32_t idx, int32_t level);

  int32_t level = 0;
  int32_t uses = 0;
};

class WateringCanItem : public Item {
 public:
  explicit WateringCanItem(std::string name = "Watering Can");
  ItemKind Kind() const override { return ItemKind::kWateringCan; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  void UpdateSprite();
  std::string GetData() const override;
  std::unique_ptr<Item> Clone() const override;

  static constexpr int32_t kCapacity = 1800;
  int32_t content = 0;

 private:
  int32_t rendering_tick_ = 0;
};

class SummonItem : public StackableItem {
 public:
  SummonItem(std::string name, gfx::LinkedSprite sprite, std::string mob_name,
             int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kSummon; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<SummonItem>(*this);
  }

 private:
  std::string mob_;
};

class EntitySummonItem : public StackableItem {
 public:
  using EntityFactory = std::function<std::unique_ptr<entity::Entity>(
      entity::Direction dir)>;

  EntitySummonItem(std::string name, gfx::LinkedSprite sprite,
                   EntityFactory factory, int32_t count = 1);
  ItemKind Kind() const override { return ItemKind::kEntitySummon; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool InteractsWithWorld() const override { return false; }
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<EntitySummonItem>(*this);
  }

 private:
  EntityFactory factory_;
};

class FurnitureItem : public Item {
 public:
  explicit FurnitureItem(std::unique_ptr<entity::Furniture> furn);
  FurnitureItem(const FurnitureItem& other);
  FurnitureItem& operator=(const FurnitureItem& other);
  ~FurnitureItem() override;

  ItemKind Kind() const override { return ItemKind::kFurniture; }
  bool InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                  int32_t yt, entity::Player* player,
                  entity::Direction attack_dir) override;
  bool IsDepleted() const override { return placed; }
  bool Equals(const Item& other) const override;
  std::unique_ptr<Item> Clone() const override {
    return std::make_unique<FurnitureItem>(*this);
  }

  std::unique_ptr<entity::Furniture> furniture;
  bool placed = false;
};

}  // namespace item
}  // namespace minicraft

#endif  // MINICRAFT_CORE_ITEM_ITEM_H_
