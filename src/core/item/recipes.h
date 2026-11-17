#ifndef MINICRAFT_CORE_ITEM_RECIPES_H_
#define MINICRAFT_CORE_ITEM_RECIPES_H_

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/item/item.h"

namespace minicraft {
namespace entity {
class Player;
}  // namespace entity

namespace item {

struct CaseInsensitiveLess {
  bool operator()(std::string_view a, std::string_view b) const;
};

class Recipe {
 public:
  Recipe(std::string_view created_item,
         std::initializer_list<std::string_view> req_items);
  Recipe(std::string_view created_item, const std::vector<std::string>& req);

  std::unique_ptr<Item> GetProduct() const;
  const std::string& Product() const { return product_; }
  const std::map<std::string, int32_t, CaseInsensitiveLess>& Costs() const {
    return costs_;
  }
  int32_t Amount() const { return amount_; }
  bool GetCanCraft() const { return can_craft_; }

  bool CheckCanCraft(const entity::Player& player);
  bool Craft(entity::Player* player) const;
  bool Matches(const Recipe& other) const;

 private:
  bool CanCraftImpl(const entity::Player& player) const;

  std::map<std::string, int32_t, CaseInsensitiveLess> costs_;
  std::string product_;
  int32_t amount_ = 1;
  bool can_craft_ = false;
};

class Recipes {
 public:
  static void Init();

  static std::vector<Recipe> anvil_recipes;
  static std::vector<Recipe> oven_recipes;
  static std::vector<Recipe> furnace_recipes;
  static std::vector<Recipe> workbench_recipes;
  static std::vector<Recipe> enchant_recipes;
  static std::vector<Recipe> craft_recipes;
  static std::vector<Recipe> loom_recipes;
  static std::vector<Recipe> dye_vat_recipes;
};

}  // namespace item
}  // namespace minicraft

#endif  // MINICRAFT_CORE_ITEM_RECIPES_H_
