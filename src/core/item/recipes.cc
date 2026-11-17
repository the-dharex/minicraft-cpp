#include "core/item/recipes.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "core/entity/player.h"
#include "core/io/settings.h"
#include "core/item/items.h"
#include "core/level/level.h"

namespace minicraft {
namespace item {

bool CaseInsensitiveLess::operator()(std::string_view a,
                                     std::string_view b) const {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    unsigned char ca = static_cast<unsigned char>(std::tolower(a[i]));
    unsigned char cb = static_cast<unsigned char>(std::tolower(b[i]));
    if (ca != cb) return ca < cb;
  }
  return a.size() < b.size();
}

namespace {

std::string ToUpperAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

std::pair<std::string, int32_t> SplitNameCount(std::string_view spec) {
  size_t pos = spec.find('_');
  if (pos == std::string_view::npos) {
    return {ToUpperAscii(spec), 1};
  }
  std::string item = ToUpperAscii(spec.substr(0, pos));
  int32_t amt = 1;
  try {
    amt = std::stoi(std::string(spec.substr(pos + 1)));
  } catch (...) {
    amt = 1;
  }
  return {std::move(item), amt};
}

}  // namespace

Recipe::Recipe(std::string_view created_item,
               std::initializer_list<std::string_view> req_items) {
  auto [prod, amt] = SplitNameCount(created_item);
  product_ = std::move(prod);
  amount_ = amt;
  for (std::string_view req : req_items) {
    auto [cname, camt] = SplitNameCount(req);
    costs_[cname] += camt;
  }
}

Recipe::Recipe(std::string_view created_item,
               const std::vector<std::string>& req) {
  auto [prod, amt] = SplitNameCount(created_item);
  product_ = std::move(prod);
  amount_ = amt;
  for (const std::string& r : req) {
    auto [cname, camt] = SplitNameCount(r);
    costs_[cname] += camt;
  }
}

std::unique_ptr<Item> Recipe::GetProduct() const {
  return Items::Get(product_);
}

bool Recipe::CheckCanCraft(const entity::Player& player) {
  can_craft_ = CanCraftImpl(player);
  return can_craft_;
}

bool Recipe::CanCraftImpl(const entity::Player& player) const {
  if (io::Settings::GetString("mode") == "minicraft.settings.mode.creative") {
    return true;
  }
  for (const auto& [cost_name, cost_amt] : costs_) {
    auto item = Items::Get(cost_name);
    if (player.GetInventory().Count(*item) < cost_amt) {
      return false;
    }
  }
  return true;
}

bool Recipe::Craft(entity::Player* player) const {
  if (!player || !CanCraftImpl(*player)) return false;

  if (io::Settings::GetString("mode") != "minicraft.settings.mode.creative") {
    for (const auto& [cost_name, cost_amt] : costs_) {
      auto item = Items::Get(cost_name);
      player->GetInventory().RemoveItems(*item, cost_amt);
    }
  }

  for (int32_t i = 0; i < amount_; ++i) {
    auto prod = GetProduct();
    if (auto leftover = player->GetInventory().Add(std::move(prod))) {
      if (player->GetLevel()) {
        player->GetLevel()->DropItem(player->x, player->y, std::move(leftover));
      }
    }
  }
  return true;
}

bool Recipe::Matches(const Recipe& other) const {
  return product_ == other.product_ && amount_ == other.amount_ &&
         costs_ == other.costs_;
}

std::vector<Recipe> Recipes::anvil_recipes;
std::vector<Recipe> Recipes::oven_recipes;
std::vector<Recipe> Recipes::furnace_recipes;
std::vector<Recipe> Recipes::workbench_recipes;
std::vector<Recipe> Recipes::enchant_recipes;
std::vector<Recipe> Recipes::craft_recipes;
std::vector<Recipe> Recipes::loom_recipes;
std::vector<Recipe> Recipes::dye_vat_recipes;

void Recipes::Init() {
  if (!craft_recipes.empty()) return;

  craft_recipes = {
      {"Workbench_1", {"Wood_10"}},
      {"Torch_2", {"Wood_1", "coal_1"}},
      {"plank_2", {"Wood_1"}},
      {"Plank Wall_1", {"plank_3"}},
      {"Wood Door_1", {"plank_5"}},
      {"Wood Fence_1", {"plank_3"}},
  };

  workbench_recipes = {
      {"Workbench_1", {"Wood_10"}},
      {"Torch_2", {"Wood_1", "coal_1"}},
      {"plank_2", {"Wood_1"}},
      {"Ornate Wood_1", {"Wood_1"}},
      {"Plank Wall_1", {"plank_3"}},
      {"Wood Door_1", {"plank_5"}},
      {"Wood Fence_1", {"plank_3"}},
      {"Lantern_1", {"Wood_8", "slime_4", "glass_3"}},
      {"Stone Brick_1", {"Stone_2"}},
      {"Ornate Stone_1", {"Stone_2"}},
      {"Stone Wall_1", {"Stone Brick_3"}},
      {"Stone Door_1", {"Stone Brick_5"}},
      {"Stone Fence_1", {"Stone Brick_3"}},
      {"Obsidian Brick_1", {"Raw Obsidian_2"}},
      {"Ornate Obsidian_1", {"Raw Obsidian_2"}},
      {"Obsidian Wall_1", {"Obsidian Brick_3"}},
      {"Obsidian Door_1", {"Obsidian Brick_5"}},
      {"Obsidian Fence_1", {"Obsidian Brick_3"}},
      {"Oven_1", {"Stone_15"}},
      {"Furnace_1", {"Stone_20"}},
      {"Enchanter_1", {"Wood_5", "String_2", "Lapis_10"}},
      {"Chest_1", {"Wood_20"}},
      {"Anvil_1", {"iron_5"}},
      {"Tnt_1", {"Gunpowder_10", "Sand_8"}},
      {"Loom_1", {"Wood_10", "White Wool_5"}},
      {"Dye Vat_1", {"Stone_10", "iron_5"}},
      {"Boat_1", {"Wood_10", "Wood Shovel_1"}},
      {"Wood Fishing Rod_1", {"Wood_10", "String_3"}},
      {"Iron Fishing Rod_1", {"Iron_10", "String_3"}},
      {"Gold Fishing Rod_1", {"Gold_10", "String_3"}},
      {"Gem Fishing Rod_1", {"Gem_10", "String_3"}},
      {"Wood Sword_1", {"Wood_5"}},
      {"Wood Axe_1", {"Wood_5"}},
      {"Wood Hoe_1", {"Wood_5"}},
      {"Wood Pickaxe_1", {"Wood_5"}},
      {"Wood Shovel_1", {"Wood_5"}},
      {"Wood Bow_1", {"Wood_5", "string_2"}},
      {"Rock Sword_1", {"Wood_5", "Stone_5"}},
      {"Rock Axe_1", {"Wood_5", "Stone_5"}},
      {"Rock Hoe_1", {"Wood_5", "Stone_5"}},
      {"Rock Pickaxe_1", {"Wood_5", "Stone_5"}},
      {"Rock Shovel_1", {"Wood_5", "Stone_5"}},
      {"Rock Bow_1", {"Wood_5", "Stone_5", "string_2"}},
      {"arrow_3", {"Wood_2", "Stone_2"}},
      {"Leather Armor_1", {"leather_10"}},
      {"Snake Armor_1", {"scale_15"}},
  };

  dye_vat_recipes = {
      {"white dye_1", {"White Lily_1"}},
      {"light gray dye_1", {"Oxeye Daisy_1"}},
      {"light gray dye_1", {"Hydrangea_1"}},
      {"light gray dye_1", {"White Tulip_1"}},
      {"blue dye_1", {"Lapis_1"}},
      {"blue dye_1", {"Cornflower_1"}},
      {"blue dye_1", {"Iris_1"}},
      {"green dye_1", {"Cactus_1"}},
      {"yellow dye_1", {"Sunflower_1"}},
      {"yellow dye_1", {"Dandelion_1"}},
      {"light blue dye_1", {"Blue Orchid_1"}},
      {"light blue dye_1", {"Periwinkle_1"}},
      {"black dye_1", {"Coal_1"}},
      {"red dye_1", {"Rose_1"}},
      {"red dye_1", {"Red Tulip_1"}},
      {"red dye_1", {"Poppy_1"}},
      {"magenta dye_1", {"Allium_1"}},
      {"orange dye_1", {"Orange Tulip_1"}},
      {"pink dye_1", {"Pink Tulip_1"}},
      {"pink dye_1", {"Peony_1"}},
      {"pink dye_1", {"Pink Lily_1"}},
      {"purple dye_1", {"Violet_1"}},
      {"orange dye_2", {"red dye_1", "yellow dye_1"}},
      {"purple dye_2", {"blue dye_1", "red dye_1"}},
      {"cyan dye_2", {"blue dye_1", "green dye_1"}},
      {"brown dye_2", {"green dye_1", "red dye_1"}},
      {"pink dye_2", {"white dye_1", "red dye_1"}},
      {"light blue dye_2", {"white dye_1", "blue dye_1"}},
      {"lime dye_2", {"white dye_1", "green dye_1"}},
      {"gray dye_2", {"white dye_1", "black dye_1"}},
      {"light gray dye_2", {"white dye_1", "gray dye_1"}},
      {"light gray dye_3", {"white dye_2", "black dye_1"}},
      {"magenta dye_2", {"purple dye_1", "pink dye_1"}},
      {"magenta dye_4", {"red dye_2", "white dye_1", "blue dye_1"}},
      {"magenta dye_4", {"pink dye_1", "red dye_1", "blue dye_1"}},
  };

  loom_recipes = {
      {"String_2", {"white wool_1"}},
      {"white wool_1", {"String_3"}},
      {"black wool_1", {"white wool_1", "black dye_1"}},
      {"red wool_1", {"white wool_1", "red dye_1"}},
      {"green wool_1", {"white wool_1", "green dye_1"}},
      {"brown wool_1", {"white wool_1", "brown dye_1"}},
      {"blue wool_1", {"white wool_1", "blue dye_1"}},
      {"purple wool_1", {"white wool_1", "purple dye_1"}},
      {"cyan wool_1", {"white wool_1", "cyan dye_1"}},
      {"light gray wool_1", {"white wool_1", "light gray dye_1"}},
      {"gray wool_1", {"white wool_1", "gray dye_1"}},
      {"pink wool_1", {"white wool_1", "pink dye_1"}},
      {"lime wool_1", {"white wool_1", "lime dye_1"}},
      {"yellow wool_1", {"white wool_1", "yellow dye_1"}},
      {"light blue wool_1", {"white wool_1", "light blue dye_1"}},
      {"magenta wool_1", {"white wool_1", "magenta dye_1"}},
      {"orange wool_1", {"white wool_1", "orange dye_1"}},
      {"white Bed_1", {"Wood_5", "white wool_3"}},
      {"black Bed_1", {"Wood_5", "black wool_3"}},
      {"red Bed_1", {"Wood_5", "red wool_3"}},
      {"green Bed_1", {"Wood_5", "green wool_3"}},
      {"brown Bed_1", {"Wood_5", "brown wool_3"}},
      {"blue Bed_1", {"Wood_5", "blue wool_3"}},
      {"purple Bed_1", {"Wood_5", "purple wool_3"}},
      {"cyan Bed_1", {"Wood_5", "cyan wool_3"}},
      {"light gray Bed_1", {"Wood_5", "light gray wool_3"}},
      {"gray Bed_1", {"Wood_5", "gray wool_3"}},
      {"pink Bed_1", {"Wood_5", "pink wool_3"}},
      {"lime Bed_1", {"Wood_5", "lime wool_3"}},
      {"yellow Bed_1", {"Wood_5", "yellow wool_3"}},
      {"light blue Bed_1", {"Wood_5", "light blue wool_3"}},
      {"magenta Bed_1", {"Wood_5", "magenta wool_3"}},
      {"orange Bed_1", {"Wood_5", "orange wool_3"}},
      {"black Bed_1", {"White Bed_1", "black dye_1"}},
      {"red Bed_1", {"White Bed_1", "red dye_1"}},
      {"green Bed_1", {"White Bed_1", "green dye_1"}},
      {"brown Bed_1", {"White Bed_1", "brown dye_1"}},
      {"blue Bed_1", {"White Bed_1", "blue dye_1"}},
      {"purple Bed_1", {"White Bed_1", "purple dye_1"}},
      {"cyan Bed_1", {"White Bed_1", "cyan dye_1"}},
      {"light gray Bed_1", {"White Bed_1", "light gray dye_1"}},
      {"gray Bed_1", {"White Bed_1", "gray dye_1"}},
      {"pink Bed_1", {"White Bed_1", "pink dye_1"}},
      {"lime Bed_1", {"White Bed_1", "lime dye_1"}},
      {"yellow Bed_1", {"White Bed_1", "yellow dye_1"}},
      {"light blue Bed_1", {"White Bed_1", "light blue dye_1"}},
      {"magenta Bed_1", {"White Bed_1", "magenta dye_1"}},
      {"orange Bed_1", {"White Bed_1", "orange dye_1"}},
      {"blue clothes_1", {"cloth_5", "blue dye_1"}},
      {"green clothes_1", {"cloth_5", "green dye_1"}},
      {"yellow clothes_1", {"cloth_5", "yellow dye_1"}},
      {"black clothes_1", {"cloth_5", "black dye_1"}},
      {"orange clothes_1", {"cloth_5", "orange dye_1"}},
      {"purple clothes_1", {"cloth_5", "purple dye_1"}},
      {"cyan clothes_1", {"cloth_5", "cyan dye_1"}},
      {"reg clothes_1", {"cloth_5"}},
      {"Leather Armor_1", {"leather_10"}},
  };

  anvil_recipes = {
      {"Iron Armor_1", {"iron_10"}},
      {"Gold Armor_1", {"gold_10"}},
      {"Gem Armor_1", {"gem_65"}},
      {"Empty Bucket_1", {"iron_5"}},
      {"Iron Lantern_1", {"iron_8", "slime_5", "glass_4"}},
      {"Gold Lantern_1", {"gold_10", "slime_5", "glass_4"}},
      {"Iron Sword_1", {"Wood_5", "iron_5"}},
      {"Iron Claymore_1", {"Iron Sword_1", "shard_15"}},
      {"Iron Axe_1", {"Wood_5", "iron_5"}},
      {"Iron Hoe_1", {"Wood_5", "iron_5"}},
      {"Iron Pickaxe_1", {"Wood_5", "iron_5"}},
      {"Iron Shovel_1", {"Wood_5", "iron_5"}},
      {"Iron Bow_1", {"Wood_5", "iron_5", "string_2"}},
      {"Gold Sword_1", {"Wood_5", "gold_5"}},
      {"Gold Claymore_1", {"Gold Sword_1", "shard_15"}},
      {"Gold Axe_1", {"Wood_5", "gold_5"}},
      {"Gold Hoe_1", {"Wood_5", "gold_5"}},
      {"Gold Pickaxe_1", {"Wood_5", "gold_5"}},
      {"Gold Shovel_1", {"Wood_5", "gold_5"}},
      {"Gold Bow_1", {"Wood_5", "gold_5", "string_2"}},
      {"Gem Sword_1", {"Wood_5", "gem_50"}},
      {"Gem Claymore_1", {"Gem Sword_1", "shard_15"}},
      {"Gem Axe_1", {"Wood_5", "gem_50"}},
      {"Gem Hoe_1", {"Wood_5", "gem_50"}},
      {"Gem Pickaxe_1", {"Wood_5", "gem_50"}},
      {"Gem Shovel_1", {"Wood_5", "gem_50"}},
      {"Gem Bow_1", {"Wood_5", "gem_50", "string_2"}},
      {"Shears_1", {"Iron_4"}},
      {"Watering Can_1", {"Iron_3"}},
  };

  furnace_recipes = {
      {"iron_1", {"iron Ore_3", "coal_1"}},
      {"gold_1", {"gold Ore_3", "coal_1"}},
      {"glass_1", {"sand_4", "coal_1"}},
      {"glass bottle_1", {"glass_3"}},
  };

  oven_recipes = {
      {"cooked pork_1", {"raw pork_1", "coal_1"}},
      {"steak_1", {"raw beef_1", "coal_1"}},
      {"cooked fish_1", {"raw fish_1", "coal_1"}},
      {"bread_1", {"wheat_4"}},
      {"Baked Potato_1", {"Potato_1"}},
  };

  enchant_recipes = {
      {"Gold Apple_1", {"apple_1", "gold_8"}},
      {"awkward potion_1", {"glass bottle_1", "Lapis_3"}},
      {"speed potion_1", {"awkward potion_1", "Cactus_5"}},
      {"light potion_1", {"awkward potion_1", "slime_5"}},
      {"swim potion_1", {"awkward potion_1", "raw fish_5"}},
      {"haste potion_1", {"awkward potion_1", "Wood_5", "Stone_5"}},
      {"lava potion_1", {"awkward potion_1", "Lava Bucket_1"}},
      {"energy potion_1", {"awkward potion_1", "gem_25"}},
      {"regen potion_1", {"awkward potion_1", "Gold Apple_1"}},
      {"Health Potion_1", {"awkward potion_1", "Gunpowder_2", "Leather Armor_1"}},
      {"Escape Potion_1", {"awkward potion_1", "Gunpowder_3", "Lapis_7"}},
      {"Totem of Air_1", {"gold_10", "gem_10", "Lapis_5", "Cloud Ore_5"}},
      {"Obsidian Poppet_1", {"gold_10", "gem_10", "Lapis_5", "Shard_15"}},
  };
}

}  // namespace item
}  // namespace minicraft
