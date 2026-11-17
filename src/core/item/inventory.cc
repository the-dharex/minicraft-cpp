#include "core/item/inventory.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "core/item/items.h"

namespace minicraft::item {

namespace {
bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}
}  // namespace

std::unique_ptr<Item> Inventory::Remove(size_t idx) {
  if (idx >= items_.size()) return nullptr;
  auto res = std::move(items_[idx]);
  items_.erase(items_.begin() + static_cast<ptrdiff_t>(idx));
  return res;
}

std::unique_ptr<Item> Inventory::Add(std::unique_ptr<Item> item) {
  if (!item) return nullptr;
  if (dynamic_cast<PowerGloveItem*>(item.get()) != nullptr) {
    return nullptr;
  }

  if (auto* incoming = dynamic_cast<StackableItem*>(item.get())) {
    if (incoming->count <= 0) return nullptr;
    for (auto& slot : items_) {
      if (auto* existing = dynamic_cast<StackableItem*>(slot.get())) {
        if (existing->StacksWith(*incoming)) {
          if (!unlimited_) {
            if (existing->count < existing->max_count) {
              const int32_t r = existing->max_count - existing->count;
              if (r >= incoming->count) {
                existing->count += incoming->count;
                return nullptr;
              } else {
                incoming->count -= r;
                existing->count += r;
              }
            }
          } else {
            existing->count += incoming->count;
            return nullptr;
          }
        }
      }
    }

    if (!unlimited_) {
      if (static_cast<int32_t>(items_.size()) < max_item_) {
        while (incoming->count > 0) {
          if (static_cast<int32_t>(items_.size()) >= max_item_) {
            return item;
          }
          auto adding = incoming->Clone();
          if (auto* adding_st = dynamic_cast<StackableItem*>(adding.get())) {
            const int32_t take = std::min(incoming->count, incoming->max_count);
            adding_st->count = take;
            incoming->count -= take;
          } else {
            incoming->count = 0;
          }
          items_.push_back(std::move(adding));
        }
        return nullptr;
      }
      return item;
    }

    items_.push_back(std::move(item));
    return nullptr;
  }

  if (!unlimited_ && static_cast<int32_t>(items_.size()) >= max_item_) {
    return item;
  }
  items_.push_back(std::move(item));
  return nullptr;
}

std::vector<std::unique_ptr<Item>> Inventory::Add(const Item& item,
                                                  int32_t num) {
  std::vector<std::unique_ptr<Item>> leftovers;
  if (num <= 0) return leftovers;

  for (int32_t i = 0; i < num; ++i) {
    if (auto rem = Add(item.Clone())) {
      leftovers.push_back(std::move(rem));
    }
  }
  return leftovers;
}

void Inventory::RemoveItem(const Item& item) { RemoveItems(item, 1); }

void Inventory::RemoveItems(const Item& given, int32_t count) {
  if (auto* st = dynamic_cast<const StackableItem*>(&given)) {
    RemoveFromStack(*st, count);
    return;
  }
  int32_t remaining = count;
  for (size_t i = 0; i < items_.size() && remaining > 0;) {
    if (items_[i] &&
        (items_[i]->Equals(given) ||
         EqualsIgnoreCase(items_[i]->Name(), given.Name()))) {
      items_.erase(items_.begin() + static_cast<ptrdiff_t>(i));
      --remaining;
    } else {
      ++i;
    }
  }
}

int32_t Inventory::RemoveFromStack(const StackableItem& given, int32_t count) {
  int32_t remaining = count;
  for (size_t i = 0; i < items_.size() && remaining > 0;) {
    if (auto* st = dynamic_cast<StackableItem*>(items_[i].get())) {
      if (st->StacksWith(given) || EqualsIgnoreCase(st->Name(), given.Name())) {
        int32_t take = std::min(remaining, st->count);
        st->count -= take;
        remaining -= take;
        if (st->count <= 0) {
          items_.erase(items_.begin() + static_cast<ptrdiff_t>(i));
          continue;
        }
      }
    }
    ++i;
  }
  return count - remaining;
}

int32_t Inventory::Count(const Item& given) const {
  int32_t total = 0;
  for (const auto& it : items_) {
    if (!it) continue;
    if (auto* st = dynamic_cast<const StackableItem*>(it.get())) {
      if (st->StacksWith(given) || EqualsIgnoreCase(it->Name(), given.Name())) {
        total += st->count;
      }
    } else if (it->Equals(given) || EqualsIgnoreCase(it->Name(), given.Name())) {
      ++total;
    }
  }
  return total;
}

std::string Inventory::GetItemData() const {
  std::string out;
  for (size_t i = 0; i < items_.size(); ++i) {
    if (!items_[i]) continue;
    if (!out.empty()) out.push_back(':');
    out += items_[i]->GetData();
  }
  return out;
}

void Inventory::UpdateInv(std::string_view items_str) {
  ClearInv();
  std::string buf(items_str);
  std::stringstream ss(buf);
  std::string tok;
  while (std::getline(ss, tok, ':')) {
    if (tok.empty()) continue;
    auto it = Items::Get(tok, true);
    if (it) Add(std::move(it));
  }
}

void Inventory::TryAdd(util::Random& rng, int32_t chance, const Item& item,
                       int32_t num, bool all_or_nothing) {
  if (chance <= 0) return;
  if (!all_or_nothing || rng.NextInt(chance) == 0) {
    for (int32_t i = 0; i < num; ++i) {
      if (all_or_nothing || rng.NextInt(chance) == 0) {
        Add(item.Clone());
      }
    }
  }
}

void Inventory::TryAdd(util::Random& rng, int32_t chance,
                       std::unique_ptr<Item> item, int32_t num) {
  if (!item || chance <= 0) return;
  if (auto* st = dynamic_cast<StackableItem*>(item.get())) {
    st->count *= num;
    TryAdd(rng, chance, *item, 1, true);
  } else {
    TryAdd(rng, chance, *item, num, false);
  }
}

util::InventorySnapshot Inventory::ToSnapshot() const {
  util::InventorySnapshot snap;
  snap.items.reserve(items_.size());
  for (const auto& it : items_) {
    if (it) snap.items.push_back(it->ToSnapshot());
  }
  return snap;
}

}  // namespace minicraft::item
