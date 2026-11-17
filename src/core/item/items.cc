#include "core/item/items.h"

#include <algorithm>
#include <cctype>

#include "core/entity/boat.h"
#include "core/entity/furniture.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/level/level.h"
#include "core/level/tile.h"
#include "core/util/advancements.h"
#include "core/util/book_data.h"
#include "core/util/localization.h"

namespace minicraft::item {

namespace {

std::vector<std::unique_ptr<Item>>& MutableRegistry() {
  static std::vector<std::unique_ptr<Item>> reg;
  return reg;
}

bool& InitializedFlag() {
  static bool flag = false;
  return flag;
}

std::string Normalize(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

void AddStack(std::vector<std::unique_ptr<Item>>& out, std::string name,
              std::string sprite_key) {
  out.push_back(std::make_unique<StackableItem>(
      std::move(name),
      gfx::LinkedSprite(gfx::SpriteType::kItem, std::move(sprite_key)), 1));
}

void AddFood(std::vector<std::unique_ptr<Item>>& out, std::string name,
             std::string sprite_key, int32_t feed) {
  out.push_back(std::make_unique<FoodItem>(
      std::move(name),
      gfx::LinkedSprite(gfx::SpriteType::kItem, std::move(sprite_key)), feed,
      1));
}

void AddTile(std::vector<std::unique_ptr<Item>>& out, std::string name,
             std::string sprite_key, std::string tile_name,
             std::vector<std::string> valid, bool keep_data = false,
             int32_t fixed_data = -1,
             TileItem::TileDataGetter data_getter = nullptr) {
  TileItem::TileModel m{std::move(tile_name), std::move(data_getter), keep_data,
                        fixed_data};
  out.push_back(std::make_unique<TileItem>(
      std::move(name),
      gfx::LinkedSprite(gfx::SpriteType::kItem, std::move(sprite_key)),
      std::move(m), std::move(valid), 1));
}

std::string ResolveItemAlias(const std::string& norm) {
  if (norm == "key") return "dungeon key";
  if (norm == "golden apple") return "gold apple";
  if (norm == "cooked beef") return "steak";
  if (norm == "heart") return "obsidian heart";
  if (norm == "air wizard totem") return "totem of air";
  if (norm == "bow") return "wood bow";
  if (norm == "fishing rod") return "wood fishing rod";
  if (norm == "rock fishing rod") return "iron fishing rod";
  if (norm == "seeds") return "wheat seeds";
  if (norm == "flower") return "oxeye daisy";
  if (norm == "red tunic") return "red clothes";
  if (norm == "blue tunic") return "blue clothes";
  if (norm == "green tunic") return "green clothes";
  if (norm == "yellow tunic") return "yellow clothes";
  if (norm == "wool") return "white wool";
  return norm;
}

}  // namespace

void Items::Init() {
  if (InitializedFlag()) return;
  auto& items = MutableRegistry();
  items.clear();

  items.push_back(std::make_unique<PowerGloveItem>());

  // Basic resources & drops
  AddStack(items, "Wood", "wood");
  AddStack(items, "Coal", "coal");
  AddStack(items, "Iron Ore", "iron_ore");
  AddStack(items, "Gold Ore", "gold_ore");
  AddStack(items, "Iron", "iron_ingot");
  AddStack(items, "Gold", "gold_ingot");
  AddStack(items, "Gem", "gem");
  AddStack(items, "Lapis", "lapis");
  AddStack(items, "Cloth", "cloth");
  AddStack(items, "Leather", "leather");
  AddStack(items, "Bone", "bone");
  AddStack(items, "Slime", "slime");
  AddStack(items, "Gunpowder", "gunpowder");
  AddStack(items, "Scale", "scale");
  AddStack(items, "Shard", "shard");
  AddStack(items, "Arrow", "arrow");
  AddStack(items, "String", "string");
  AddStack(items, "Glass", "glass");
  AddStack(items, "Glass Bottle", "glass_bottle");
  AddStack(items, "Cloud Ore", "cloud_ore");
  AddStack(items, "Dungeon Key", "key");
  AddStack(items, "Obsidian Key", "key");
  AddStack(items, "Fertilizer", "fertilizer");
  AddStack(items, "Wheat", "wheat");
  AddStack(items, "Tomato", "tomato");

  // Foods
  AddFood(items, "Baked Potato", "baked_potato", 1);
  AddFood(items, "Apple", "apple", 1);
  AddFood(items, "Raw Pork", "pork", 1);
  AddFood(items, "Raw Fish", "fish", 1);
  AddFood(items, "Raw Beef", "beef", 1);
  AddFood(items, "Bread", "bread", 2);
  AddFood(items, "Cooked Fish", "cooked_fish", 3);
  AddFood(items, "Cooked Pork", "cooked_pork", 3);
  AddFood(items, "Steak", "cooked_beef", 3);
  AddFood(items, "Gold Apple", "golden_apple", 10);

  // Seeds & placeable terrain tiles
  const std::vector<std::string> solid_tiles = {
      "Dirt",          "Wood Planks",    "Stone Bricks", "Obsidian",
      "White Wool",    "Red Wool",       "Blue Wool",    "Green Wool",
      "Yellow Wool",   "Black Wool",     "Grass",        "Sand",
      "Path",          "Ornate Stone",   "Ornate Obsidian", "Raw Obsidian",
      "Stone"};

  AddTile(items, "Acorn", "acorn", "Tree Sapling", {"Grass"});
  AddTile(items, "Dirt", "dirt", "Dirt", {"Hole", "Water", "Lava"});
  AddTile(items, "Natural Rock", "stone", "Rock",
          {"Hole", "Dirt", "Sand", "Grass", "Path", "Water", "Lava"});

  // Building Planks, Walls, Doors, Fences
  AddTile(items, "Plank", "plank", "Wood Planks", {"Hole", "Water", "Cloud"});
  AddTile(items, "Ornate Wood", "plank", "Ornate Wood",
          {"Hole", "Water", "Cloud"});
  AddTile(items, "Plank Wall", "plank_wall", "Wood Wall", {"Wood Planks"});
  AddTile(items, "Wood Door", "wood_door", "Wood Door", {"Wood Planks"});
  AddTile(items, "Wood Fence", "wood_fence", "Wood Fence", solid_tiles, true);
  AddTile(items, "Stone", "stone", "Stone",
          {"Hole", "Water", "Cloud", "Lava"});
  AddTile(items, "Stone Brick", "stone_brick", "Stone Bricks",
          {"Hole", "Water", "Cloud", "Lava"});
  AddTile(items, "Ornate Stone", "stone_brick", "Ornate Stone",
          {"Hole", "Water", "Cloud", "Lava"});
  AddTile(items, "Stone Wall", "stone_wall", "Stone Wall", {"Stone Bricks"});
  AddTile(items, "Stone Door", "stone_door", "Stone Door", {"Stone Bricks"});
  AddTile(items, "Stone Fence", "stone_fence", "Stone Fence", solid_tiles,
          true);
  AddTile(items, "Raw Obsidian", "obsidian", "Raw Obsidian",
          {"Hole", "Water", "Cloud", "Lava"});
  AddTile(items, "Obsidian Brick", "obsidian_brick", "Obsidian",
          {"Hole", "Water", "Cloud", "Lava"});
  AddTile(items, "Ornate Obsidian", "obsidian_brick", "Ornate Obsidian",
          {"Hole", "Water", "Cloud", "Lava"});
  AddTile(items, "Obsidian Wall", "obsidian_wall", "Obsidian Wall",
          {"Obsidian"});
  AddTile(items, "Obsidian Door", "obsidian_door", "Obsidian Door",
          {"Obsidian"});
  AddTile(items, "Obsidian Fence", "obsidian_fence", "Obsidian Fence",
          solid_tiles, true);

  AddTile(items, "Sand", "sand", "Sand", {"Hole", "Water", "Lava"});
  AddTile(items, "Cactus", "cactus", "Cactus Sapling", {"Sand"});
  AddTile(items, "Cloud", "cloud", "Cloud", {"Infinite Fall"});

  auto seed_planting = [](const level::Tile& /*placed*/,
                          const level::Tile& target, level::Level* /*level*/,
                          int32_t /*xt*/, int32_t /*yt*/,
                          entity::Player* /*player*/,
                          entity::Direction /*dir*/) -> int32_t {
    util::AchievementManager::Instance().SetAchievement(
        "minicraft.achievement.plant_seed", true,
        game::GameState::Instance().IsCreativeMode());
    return target.id;
  };
  AddTile(items, "Wheat Seeds", "seed", "Wheat", {"Farmland"}, false, -1,
          seed_planting);
  AddTile(items, "Potato", "potato", "Potato", {"Farmland"}, true);
  AddTile(items, "Carrot", "carrot", "Carrot", {"Farmland"}, true);
  AddTile(items, "Tomato Seeds", "seed", "Tomato", {"Farmland"}, false, -1,
          seed_planting);
  AddTile(items, "Heavenly Berries", "heavenly_berries", "Heavenly Berries",
          {"Farmland"}, true);
  AddTile(items, "Hellish Berries", "hellish_berries", "Hellish Berries",
          {"Farmland"}, true);
  AddTile(items, "Grass Seeds", "seed", "Grass", {"Dirt"});

  AddTile(items, "Torch", "torch", "Torch", solid_tiles, true);
  AddTile(items, "Sign", "sign", "Sign", solid_tiles, false, -1,
          [](const level::Tile& /*placed*/, const level::Tile& target,
             level::Level* level, int32_t xt, int32_t yt,
             entity::Player* /*player*/,
             entity::Direction /*dir*/) -> int32_t {
            if (level) {
              game::GameState::Instance().OpenSignEditor(level->depth, xt, yt);
            }
            return target.id;
          });

  // Flowers
  AddTile(items, "Oxeye Daisy", "oxeye_daisy", "Flower", {"Grass"}, false, 0);
  AddTile(items, "Rose", "rose", "Flower", {"Grass"}, false, 1);
  AddTile(items, "Sunflower", "sunflower", "Flower", {"Grass"}, false, 2);
  AddTile(items, "Allium", "allium", "Flower", {"Grass"}, false, 3);
  AddTile(items, "Blue Orchid", "blue_orchid", "Flower", {"Grass"}, false, 4);
  AddTile(items, "Cornflower", "cornflower", "Flower", {"Grass"}, false, 5);
  AddTile(items, "Dandelion", "dandelion", "Flower", {"Grass"}, false, 6);
  AddTile(items, "Hydrangea", "hydrangea", "Flower", {"Grass"}, false, 7);
  AddTile(items, "Iris", "iris", "Flower", {"Grass"}, false, 8);
  AddTile(items, "Orange Tulip", "orange_tulip", "Flower", {"Grass"}, false, 9);
  AddTile(items, "Pink Tulip", "pink_tulip", "Flower", {"Grass"}, false, 10);
  AddTile(items, "Red Tulip", "red_tulip", "Flower", {"Grass"}, false, 11);
  AddTile(items, "White Tulip", "white_tulip", "Flower", {"Grass"}, false, 12);
  AddTile(items, "Peony", "peony", "Flower", {"Grass"}, false, 13);
  AddTile(items, "Periwinkle", "periwinkle", "Flower", {"Grass"}, false, 14);
  AddTile(items, "Pink Lily", "pink_lily", "Flower", {"Grass"}, false, 15);
  AddTile(items, "White Lily", "white_lily", "Flower", {"Grass"}, false, 16);
  AddTile(items, "Poppy", "poppy", "Flower", {"Grass"}, false, 17);
  AddTile(items, "Violet", "violet", "Flower", {"Grass"}, false, 18);

  // Wool (all 16 colors) & Dyes
  for (int32_t i = 0; i < static_cast<int32_t>(DyeColor::kCount); ++i) {
    DyeColor col = static_cast<DyeColor>(i);
    std::string prefix = DyeColorSpritePrefix(col);
    std::string wname = std::string(DyeColorDisplayName(col)) + " Wool";
    items.push_back(std::make_unique<WoolItem>(
        wname, gfx::LinkedSprite(gfx::SpriteType::kItem, prefix + "_wool"),
        col));
    items.push_back(std::make_unique<DyeItem>(
        std::string(DyeColorDisplayName(col)) + " Dye",
        gfx::LinkedSprite(gfx::SpriteType::kItem, prefix + "_dye"), col));
  }

  // Tools
  for (int32_t lvl = 0; lvl <= 4; ++lvl) {
    items.push_back(std::make_unique<ToolItem>(ToolType::kShovel, lvl));
    items.push_back(std::make_unique<ToolItem>(ToolType::kHoe, lvl));
    items.push_back(std::make_unique<ToolItem>(ToolType::kSword, lvl));
    items.push_back(std::make_unique<ToolItem>(ToolType::kPickaxe, lvl));
    items.push_back(std::make_unique<ToolItem>(ToolType::kAxe, lvl));
    items.push_back(std::make_unique<ToolItem>(ToolType::kBow, lvl));
    items.push_back(std::make_unique<ToolItem>(ToolType::kClaymore, lvl));
  }
  items.push_back(std::make_unique<ToolItem>(ToolType::kShears, 2));
  for (int32_t lvl = 0; lvl <= 3; ++lvl) {
    items.push_back(std::make_unique<FishingRodItem>(lvl));
  }
  items.push_back(std::make_unique<WateringCanItem>());

  // Armor & Clothing
  items.push_back(std::make_unique<ArmorItem>(
      "Leather Armor",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "leather_armor"), 0.3f, 1));
  items.push_back(std::make_unique<ArmorItem>(
      "Snake Armor", gfx::LinkedSprite(gfx::SpriteType::kItem, "snake_armor"),
      0.4f, 2));
  items.push_back(std::make_unique<ArmorItem>(
      "Iron Armor", gfx::LinkedSprite(gfx::SpriteType::kItem, "iron_armor"),
      0.5f, 3));
  items.push_back(std::make_unique<ArmorItem>(
      "Gold Armor", gfx::LinkedSprite(gfx::SpriteType::kItem, "gold_armor"),
      0.7f, 4));
  items.push_back(std::make_unique<ArmorItem>(
      "Gem Armor", gfx::LinkedSprite(gfx::SpriteType::kItem, "gem_armor"),
      1.0f, 5));

  items.push_back(std::make_unique<ClothingItem>(
      "Red Clothes", gfx::LinkedSprite(gfx::SpriteType::kItem, "red_clothes"),
      gfx::color::Get(1, 204, 0, 0)));
  items.push_back(std::make_unique<ClothingItem>(
      "Blue Clothes",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "blue_clothes"),
      gfx::color::Get(1, 0, 0, 204)));
  items.push_back(std::make_unique<ClothingItem>(
      "Green Clothes",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "green_clothes"),
      gfx::color::Get(1, 0, 204, 0)));
  items.push_back(std::make_unique<ClothingItem>(
      "Yellow Clothes",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "yellow_clothes"),
      gfx::color::Get(1, 204, 204, 0)));
  items.push_back(std::make_unique<ClothingItem>(
      "Black Clothes",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "black_clothes"),
      gfx::color::Get(1, 51)));
  items.push_back(std::make_unique<ClothingItem>(
      "Orange Clothes",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "orange_clothes"),
      gfx::color::Get(1, 255, 102, 0)));
  items.push_back(std::make_unique<ClothingItem>(
      "Purple Clothes",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "purple_clothes"),
      gfx::color::Get(1, 102, 0, 153)));
  items.push_back(std::make_unique<ClothingItem>(
      "Cyan Clothes",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "cyan_clothes"),
      gfx::color::Get(1, 0, 102, 153)));
  items.push_back(std::make_unique<ClothingItem>(
      "Reg Clothes", gfx::LinkedSprite(gfx::SpriteType::kItem, "reg_clothes"),
      gfx::color::Get(1, 51, 51, 0)));

  // Potions, Hearts, Buckets, Books
  for (int32_t p = 0; p < static_cast<int32_t>(PotionType::kCount); ++p) {
    items.push_back(std::make_unique<PotionItem>(static_cast<PotionType>(p)));
  }
  items.push_back(std::make_unique<HeartItem>(
      "Obsidian Heart",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "obsidian_heart"), 5));
  items.push_back(std::make_unique<BucketItem>(BucketFill::kEmpty));
  items.push_back(std::make_unique<BucketItem>(BucketFill::kWater));
  items.push_back(std::make_unique<BucketItem>(BucketFill::kLava));

  items.push_back(std::make_unique<BookItem>(
      "Book", gfx::LinkedSprite(gfx::SpriteType::kItem, "book"),
      []() {
        return util::Localization::GetLocalized(
            "minicraft.displays.book.default_book");
      },
      false));
  items.push_back(std::make_unique<BookItem>(
      "Antidious",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "antidious_book"),
      []() { return util::BookData::Instance().AntVenomBook(); }, true));

  // Furniture
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Crafter>(entity::Crafter::Type::kWorkbench)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Crafter>(entity::Crafter::Type::kFurnace)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Crafter>(entity::Crafter::Type::kOven)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Crafter>(entity::Crafter::Type::kAnvil)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Crafter>(entity::Crafter::Type::kEnchanter)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Crafter>(entity::Crafter::Type::kLoom)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Crafter>(entity::Crafter::Type::kDyeVat)));
  items.push_back(
      std::make_unique<FurnitureItem>(std::make_unique<entity::Chest>()));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::DungeonChest>(false, false)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Lantern>(entity::Lantern::Type::kNorm)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Lantern>(entity::Lantern::Type::kIron)));
  items.push_back(std::make_unique<FurnitureItem>(
      std::make_unique<entity::Lantern>(entity::Lantern::Type::kGold)));
  items.push_back(
      std::make_unique<FurnitureItem>(std::make_unique<entity::Composter>()));
  items.push_back(
      std::make_unique<FurnitureItem>(std::make_unique<entity::Tnt>()));

  for (int32_t i = 0; i < static_cast<int32_t>(DyeColor::kCount); ++i) {
    DyeColor col = static_cast<DyeColor>(i);
    items.push_back(std::make_unique<FurnitureItem>(
        std::make_unique<entity::Bed>(DyeColorDisplayName(col))));
  }

  // Boat & Totems
  items.push_back(std::make_unique<EntitySummonItem>(
      "Boat", gfx::LinkedSprite(gfx::SpriteType::kItem, "boat"),
      [](entity::Direction dir) -> std::unique_ptr<entity::Entity> {
        return std::make_unique<entity::Boat>(dir);
      }));
  items.push_back(std::make_unique<SummonItem>(
      "Totem of Air",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "air_totem"), "Air Wizard"));
  items.push_back(std::make_unique<SummonItem>(
      "Obsidian Poppet",
      gfx::LinkedSprite(gfx::SpriteType::kItem, "knight_statue"),
      "Obsidian Knight"));

  InitializedFlag() = true;
}

const std::vector<std::unique_ptr<Item>>& Items::GetAllTemplates() {
  if (!InitializedFlag()) Init();
  return MutableRegistry();
}

std::unique_ptr<Item> Items::Get(std::string_view name, bool allow_null) {
  if (!InitializedFlag()) Init();
  if (name.empty()) return allow_null ? nullptr : std::make_unique<UnknownItem>("NULL");

  std::string raw(name);
  int32_t data = 1;
  bool had_underscore = false;
  const size_t us = raw.find('_');
  const size_t semi = raw.find(';');
  const size_t sep = (us != std::string::npos && (semi == std::string::npos || us < semi))
                         ? us
                         : semi;
  if (sep != std::string::npos) {
    had_underscore = true;
    try {
      data = std::stoi(raw.substr(sep + 1));
    } catch (...) {
    }
    raw = raw.substr(0, sep);
  }

  const std::string target_raw = Normalize(raw);
  if (target_raw == "null") {
    return allow_null ? nullptr : std::make_unique<UnknownItem>("NULL");
  }
  if (target_raw == "unknown") {
    return std::make_unique<UnknownItem>("BLANK");
  }

  const std::string target = ResolveItemAlias(target_raw);
  for (const auto& it : MutableRegistry()) {
    if (it && Normalize(it->Name()) == target) {
      auto copy = it->Clone();
      if (auto* st = dynamic_cast<StackableItem*>(copy.get())) {
        st->count = std::max(1, data);
      } else if (auto* tool = dynamic_cast<ToolItem*>(copy.get())) {
        if (had_underscore) {
          tool->dur = data;
        }
      } else if (auto* wc = dynamic_cast<WateringCanItem*>(copy.get())) {
        if (had_underscore) {
          wc->content = std::clamp(data, 0, WateringCanItem::kCapacity);
          wc->UpdateSprite();
        }
      }
      return copy;
    }
  }

  if (allow_null) return nullptr;
  auto unk = std::make_unique<UnknownItem>(raw);
  unk->count = std::max(1, data);
  return unk;
}

int32_t Items::GetCount(const Item* item) {
  if (!item) return 0;
  if (auto* st = dynamic_cast<const StackableItem*>(item)) return st->count;
  return 1;
}

std::unique_ptr<Inventory> Items::CreateCreativeModeInventory() {
  if (!InitializedFlag()) Init();
  auto inv = std::make_unique<Inventory>(200, true);
  for (const auto& tmpl : MutableRegistry()) {
    if (tmpl && tmpl->Kind() != ItemKind::kPowerGlove) {
      inv->Add(tmpl->Clone());
    }
  }
  return inv;
}

}  // namespace minicraft::item
