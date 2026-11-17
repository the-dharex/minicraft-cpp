#ifndef MINICRAFT_CORE_ITEM_ITEMS_H_
#define MINICRAFT_CORE_ITEM_ITEMS_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/item/inventory.h"
#include "core/item/item.h"

namespace minicraft {
namespace item {

class Items {
 public:
  static void Init();
  static const std::vector<std::unique_ptr<Item>>& GetAllTemplates();

  static std::unique_ptr<Item> Get(std::string_view name,
                                   bool allow_null = false);
  static int32_t GetCount(const Item* item);

  static std::unique_ptr<Inventory> CreateCreativeModeInventory();
};

}  // namespace item
}  // namespace minicraft

#endif  // MINICRAFT_CORE_ITEM_ITEMS_H_
