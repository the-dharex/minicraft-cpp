#ifndef MINICRAFT_CORE_ITEM_INVENTORY_H_
#define MINICRAFT_CORE_ITEM_INVENTORY_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/item/item.h"
#include "core/util/advancements.h"
#include "core/util/random.h"

namespace minicraft {
namespace item {

class Inventory {
 public:
  explicit Inventory(int32_t max_slots = 27, bool unlimited = false)
      : max_item_(max_slots), unlimited_(unlimited) {}

  int32_t GetMaxSlots() const { return max_item_; }
  void SetUnlimited(bool u) { unlimited_ = u; }
  bool IsUnlimited() const { return unlimited_; }

  size_t InvSize() const { return items_.size(); }
  void ClearInv() { items_.clear(); }

  Item* Get(size_t idx) { return items_[idx].get(); }
  const Item* Get(size_t idx) const { return items_[idx].get(); }
  const std::vector<std::unique_ptr<Item>>& ItemsList() const { return items_; }

  std::unique_ptr<Item> Remove(size_t idx);
  std::unique_ptr<Item> Add(std::unique_ptr<Item> item);
  std::vector<std::unique_ptr<Item>> Add(const Item& item, int32_t num);

  void RemoveItem(const Item& item);
  void RemoveItems(const Item& given, int32_t count);
  int32_t Count(const Item& given) const;

  std::string GetItemData() const;
  void UpdateInv(std::string_view items_str);

  void TryAdd(util::Random& rng, int32_t chance, const Item& item, int32_t num,
              bool all_or_nothing);
  void TryAdd(util::Random& rng, int32_t chance,
              std::unique_ptr<Item> item, int32_t num);

  util::InventorySnapshot ToSnapshot() const;

 private:
  int32_t RemoveFromStack(const StackableItem& given, int32_t count);

  std::vector<std::unique_ptr<Item>> items_;
  int32_t max_item_ = 27;
  bool unlimited_ = false;
};

}  // namespace item
}  // namespace minicraft

#endif  // MINICRAFT_CORE_ITEM_INVENTORY_H_
