#include "core/util/advancements.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "core/util/localization.h"
#include "core/util/logging.h"
#include "json.hpp"

namespace minicraft {
namespace util {

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

IntRange ParseIntRange(const nlohmann::json& val) {
  IntRange r;
  if (val.is_number_integer()) {
    int32_t v = val.get<int32_t>();
    r.min = v;
    r.max = v;
  } else if (val.is_object()) {
    if (val.contains("min") && val["min"].is_number_integer())
      r.min = val["min"].get<int32_t>();
    if (val.contains("max") && val["max"].is_number_integer())
      r.max = val["max"].get<int32_t>();
  }
  return r;
}

DoubleRange ParseDoubleRange(const nlohmann::json& val) {
  DoubleRange r;
  if (val.is_number()) {
    double v = val.get<double>();
    r.min = v;
    r.max = v;
  } else if (val.is_object()) {
    if (val.contains("min") && val["min"].is_number())
      r.min = val["min"].get<double>();
    if (val.contains("max") && val["max"].is_number())
      r.max = val["max"].get<double>();
  }
  return r;
}

ItemConditions ParseItemConditions(const nlohmann::json& j) {
  ItemConditions cond;
  if (j.contains("items") && j["items"].is_array()) {
    for (const auto& s : j["items"]) {
      if (s.is_string()) cond.items.push_back(s.get<std::string>());
    }
  }
  if (j.contains("count")) cond.count = ParseIntRange(j["count"]);
  if (j.contains("durability"))
    cond.durability = ParseIntRange(j["durability"]);
  return cond;
}

LocationConditions ParseLocationConditions(const nlohmann::json& j) {
  LocationConditions cond;
  if (j.contains("tile") && j["tile"].is_object()) {
    const auto& tj = j["tile"];
    if (tj.contains("tiles") && tj["tiles"].is_array()) {
      for (const auto& s : tj["tiles"]) {
        if (s.is_string()) cond.tiles.push_back(s.get<std::string>());
      }
    }
    if (tj.contains("data") && tj["data"].is_number_integer()) {
      cond.data = tj["data"].get<int32_t>();
    }
  }
  if (j.contains("level") && j["level"].is_number_integer()) {
    cond.level = j["level"].get<int32_t>();
  }
  if (j.contains("position") && j["position"].is_object()) {
    const auto& pj = j["position"];
    if (pj.contains("x")) cond.x = ParseDoubleRange(pj["x"]);
    if (pj.contains("y")) cond.y = ParseDoubleRange(pj["y"]);
  }
  return cond;
}

ElementCriterion ParseCriterion(const nlohmann::json& j) {
  ElementCriterion c;
  std::string trig = j.value("trigger", "impossible");
  const auto cond =
      j.contains("conditions") && j["conditions"].is_object()
          ? j["conditions"]
          : nlohmann::json::object();

  if (trig == "inventory_changed") {
    c.trigger = TriggerType::kInventoryChanged;
    if (cond.contains("items") && cond["items"].is_array()) {
      for (const auto& ij : cond["items"]) {
        if (ij.is_object()) c.inv_items.push_back(ParseItemConditions(ij));
      }
    }
    if (cond.contains("slots") && cond["slots"].is_object()) {
      const auto& sj = cond["slots"];
      if (sj.contains("empty")) c.slots_empty = ParseIntRange(sj["empty"]);
      if (sj.contains("full")) c.slots_full = ParseIntRange(sj["full"]);
      if (sj.contains("occupied"))
        c.slots_occupied = ParseIntRange(sj["occupied"]);
    }
  } else if (trig == "placed_tile") {
    c.trigger = TriggerType::kPlacedTile;
    if (cond.contains("tile") && cond["tile"].is_string()) {
      c.tile = cond["tile"].get<std::string>();
    }
    if (cond.contains("item") && cond["item"].is_object()) {
      c.item_cond = ParseItemConditions(cond["item"]);
    }
    if (cond.contains("location") && cond["location"].is_object()) {
      c.location_cond = ParseLocationConditions(cond["location"]);
    }
    if (cond.contains("data") && cond["data"].is_number_integer()) {
      c.data = cond["data"].get<int32_t>();
    }
  } else if (trig == "item_used_on_tile") {
    c.trigger = TriggerType::kItemUsedOnTile;
    if (cond.contains("location") && cond["location"].is_object()) {
      c.location_cond = ParseLocationConditions(cond["location"]);
    }
    if (cond.contains("item") && cond["item"].is_object()) {
      c.item_cond = ParseItemConditions(cond["item"]);
    }
  } else {
    c.trigger = TriggerType::kImpossible;
  }
  return c;
}

void PopulateElementFields(AdvancementElement* el, const std::string& key,
                           const nlohmann::json& j, bool displayable) {
  el->key = key;
  el->index = j.value("index", 0);
  if (displayable) {
    el->description = j.value("description", "");
  }
  if (j.contains("criteria") && j["criteria"].is_object()) {
    for (auto& [ck, cv] : j["criteria"].items()) {
      el->criteria[ck] = ParseCriterion(cv);
    }
  }
  if (j.contains("requirements") && j["requirements"].is_array()) {
    for (const auto& arr : j["requirements"]) {
      if (!arr.is_array()) continue;
      std::vector<std::string> group;
      for (const auto& req : arr) {
        if (req.is_string()) group.push_back(req.get<std::string>());
      }
      el->requirements.push_back(std::move(group));
    }
  }
  if (j.contains("unlocking_criteria") && j["unlocking_criteria"].is_object()) {
    for (auto& [ck, cv] : j["unlocking_criteria"].items()) {
      el->unlocking_criteria[ck] = ParseCriterion(cv);
    }
  }
  if (j.contains("unlocking_requirements") &&
      j["unlocking_requirements"].is_array()) {
    for (const auto& arr : j["unlocking_requirements"]) {
      if (!arr.is_array()) continue;
      std::vector<std::string> group;
      for (const auto& req : arr) {
        if (req.is_string()) group.push_back(req.get<std::string>());
      }
      el->unlocking_requirements.push_back(std::move(group));
    }
  }
  if (j.contains("rewards") && j["rewards"].is_object()) {
    const auto& rj = j["rewards"];
    if (rj.contains("items") && rj["items"].is_array()) {
      for (const auto& it : rj["items"]) {
        if (it.is_string()) el->rewards.items.push_back(it.get<std::string>());
      }
    }
    if (rj.contains("recipes") && rj["recipes"].is_object()) {
      for (auto& [prod, costs_j] : rj["recipes"].items()) {
        RecipeReward rr;
        rr.product = prod;
        if (costs_j.is_array()) {
          for (const auto& cj : costs_j) {
            if (cj.is_string()) rr.costs.push_back(cj.get<std::string>());
          }
        }
        el->rewards.recipes.push_back(std::move(rr));
      }
    }
  }
}

bool CheckInventoryItemsMatch(const std::vector<ItemSnapshot>& inv_items,
                              const std::vector<ItemConditions>& conds) {
  if (conds.empty()) return true;
  std::vector<bool> matched(conds.size(), false);
  size_t remaining = conds.size();
  for (const auto& item : inv_items) {
    for (size_t i = 0; i < conds.size(); ++i) {
      if (!matched[i] && conds[i].Matches(item)) {
        matched[i] = true;
        --remaining;
        if (remaining == 0) return true;
      }
    }
  }
  return remaining == 0;
}

nlohmann::json SerializeElement(const AdvancementElement& el) {
  nlohmann::json obj = nlohmann::json::object();
  nlohmann::json crit_json = nlohmann::json::object();
  for (const auto& [k, c] : el.criteria) {
    if (c.IsCompleted()) crit_json[k] = c.completion_time;
  }
  if (!crit_json.empty()) {
    obj["criteria"] = crit_json;
    obj["done"] = el.completed;
  }
  nlohmann::json unl_json = nlohmann::json::object();
  for (const auto& [k, c] : el.unlocking_criteria) {
    if (c.IsCompleted()) unl_json[k] = c.completion_time;
  }
  if (!unl_json.empty()) {
    obj["unlockingCriteria"] = unl_json;
    obj["unlocked"] = el.unlocked;
  }
  return obj;
}

void DeserializeElement(AdvancementElement* el, const nlohmann::json& obj) {
  el->Reset(false);
  el->completed = obj.value("done", false);
  el->unlocked = obj.value("unlocked", false);
  if (obj.contains("criteria") && obj["criteria"].is_object()) {
    for (auto& [k, v] : obj["criteria"].items()) {
      auto it = el->criteria.find(k);
      if (it != el->criteria.end() && v.is_string()) {
        it->second.completion_time = v.get<std::string>();
      }
    }
  }
  if (obj.contains("unlockingCriteria") &&
      obj["unlockingCriteria"].is_object()) {
    for (auto& [k, v] : obj["unlockingCriteria"].items()) {
      auto it = el->unlocking_criteria.find(k);
      if (it != el->unlocking_criteria.end() && v.is_string()) {
        it->second.completion_time = v.get<std::string>();
      }
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// AchievementManager
// ---------------------------------------------------------------------------
AchievementManager& AchievementManager::Instance() {
  static AchievementManager instance;
  return instance;
}

void AchievementManager::LoadFromPak(const pak::PakReader& pak) {
  achievements_.clear();
  ordered_ids_.clear();
  total_score_ = 0;

  std::string text;
  if (!pak.ReadTextEntry("resources/achievements.json", &text)) {
    log::kGameHandler.Error("Could not read resources/achievements.json");
    return;
  }

  try {
    auto arr = nlohmann::json::parse(text);
    if (!arr.is_array()) return;
    for (const auto& obj : arr) {
      Achievement a;
      a.id = obj.value("id", "");
      a.name = Localization::GetLocalized(a.id);
      a.description = obj.value("desc", "");
      a.score = obj.value("score", 0);
      if (!a.id.empty()) {
        ordered_ids_.push_back(a.id);
        achievements_.emplace(a.id, std::move(a));
      }
    }
  } catch (const std::exception& e) {
    log::kGameHandler.Error(
        std::string("Invalid achievements.json: ") + e.what());
  }
}

void AchievementManager::Reset() {
  total_score_ = 0;
  for (auto& [id, ach] : achievements_) {
    ach.unlocked = false;
  }
}

bool AchievementManager::SetAchievement(std::string_view id, bool unlocked,
                                        bool is_creative, bool allow_creative) {
  if (!allow_creative && is_creative) return false;
  auto it = achievements_.find(std::string(id));
  if (it == achievements_.end()) return false;
  if (it->second.unlocked == unlocked) return false;

  it->second.unlocked = unlocked;
  if (unlocked) {
    total_score_ += it->second.score;
    if (on_unlocked_) on_unlocked_(it->first);
  } else {
    total_score_ -= it->second.score;
  }
  return true;
}

std::vector<std::string> AchievementManager::GetUnlockedIds() const {
  std::vector<std::string> out;
  for (const auto& id : ordered_ids_) {
    auto it = achievements_.find(id);
    if (it != achievements_.end() && it->second.unlocked) {
      out.push_back(id);
    }
  }
  return out;
}

void AchievementManager::UnlockFromList(const std::vector<std::string>& ids) {
  for (const auto& id : ids) {
    SetAchievement(id, true, false, false);
  }
}

const Achievement* AchievementManager::Find(std::string_view id) const {
  auto it = achievements_.find(std::string(id));
  return it != achievements_.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Conditions & AdvancementElement
// ---------------------------------------------------------------------------
bool IntRange::InRange(int32_t v) const {
  if (min.has_value() && v < *min) return false;
  if (max.has_value() && v > *max) return false;
  return true;
}

bool DoubleRange::InRange(double v) const {
  if (min.has_value() && v < *min) return false;
  if (max.has_value() && v > *max) return false;
  return true;
}

bool ItemConditions::Matches(const ItemSnapshot& item) const {
  if (!count.IsAbsent()) {
    int32_t c = item.is_stackable ? item.count : 1;
    if (!count.InRange(c)) return false;
  }
  if (!durability.IsAbsent() && item.is_tool) {
    if (!durability.InRange(item.durability)) return false;
  }
  if (items.empty()) return true;
  for (const auto& allowed : items) {
    if (EqualsIgnoreCase(allowed, item.name)) return true;
  }
  return false;
}

bool LocationConditions::Matches(const TileEvent& ev) const {
  if (level.has_value() && *level != ev.level) return false;
  if (data.has_value() && *data != ev.data) return false;
  if (!x.IsAbsent() && !x.InRange(static_cast<double>(ev.x))) return false;
  if (!y.IsAbsent() && !y.InRange(static_cast<double>(ev.y))) return false;
  if (tiles.empty()) return true;
  for (const auto& t : tiles) {
    if (EqualsIgnoreCase(t, ev.tile_name)) return true;
  }
  return false;
}

int32_t AdvancementElement::NumCriteriaCompleted() const {
  int32_t n = 0;
  for (const auto& [k, c] : criteria) {
    if (c.IsCompleted()) ++n;
  }
  return n;
}

bool AdvancementElement::IsUnlockable() const {
  if (unlocked) return true;
  if (unlocking_requirements.empty()) {
    for (const auto& [k, c] : unlocking_criteria) {
      if (!c.IsCompleted()) return false;
    }
    return true;
  }
  for (const auto& group : unlocking_requirements) {
    bool any = false;
    for (const auto& k : group) {
      auto u_it = unlocking_criteria.find(k);
      if (u_it != unlocking_criteria.end() && u_it->second.IsCompleted()) {
        any = true;
        break;
      }
      auto c_it = criteria.find(k);
      if (c_it != criteria.end() && c_it->second.IsCompleted()) {
        any = true;
        break;
      }
    }
    if (!any) return false;
  }
  return true;
}

bool AdvancementElement::CheckIsCompleted() const {
  if (requirements.empty()) {
    for (const auto& [k, c] : criteria) {
      if (!c.IsCompleted()) return false;
    }
    return true;
  }
  for (const auto& group : requirements) {
    bool any = false;
    for (const auto& k : group) {
      auto it = criteria.find(k);
      if (it != criteria.end() && it->second.IsCompleted()) {
        any = true;
        break;
      }
    }
    if (!any) return false;
  }
  return true;
}

void AdvancementElement::Update() {
  if (!unlocked) {
    unlocked = IsUnlockable();
  }
  if (!completed && unlocked) {
    if (CheckIsCompleted()) {
      completed = true;
      if (on_rewards) on_rewards(rewards);
    }
  }
  if (on_updated) on_updated(this);
}

void AdvancementElement::Reset(bool do_update) {
  completed = false;
  unlocked = false;
  for (auto& [k, c] : criteria) c.completion_time.clear();
  for (auto& [k, c] : unlocking_criteria) c.completion_time.clear();
  if (do_update) Update();
}

const Quest* Quest::Parent() const {
  if (parent_key.empty() || !series) return nullptr;
  auto it = series->quests.find(parent_key);
  return it != series->quests.end() ? it->second.get() : nullptr;
}

bool Quest::IsUnlockable() const {
  if (unlocked) return true;
  if (!series || !series->unlocked) return false;
  const Quest* p = Parent();
  return AdvancementElement::IsUnlockable() && (!p || p->completed);
}

void Quest::Update() {
  AdvancementElement::Update();
  if (series) series->Update();
}

void Quest::UpdateShallow() { AdvancementElement::Update(); }

bool QuestSeries::CheckIsCompleted() const {
  for (const auto& [k, q] : quests) {
    if (!q->completed) return false;
  }
  return AdvancementElement::CheckIsCompleted();
}

void QuestSeries::Update() {
  AdvancementElement::Update();
  for (const auto& [k, q] : quests) {
    q->UpdateShallow();
  }
}

// ---------------------------------------------------------------------------
// AdvancementSystem
// ---------------------------------------------------------------------------
AdvancementSystem& AdvancementSystem::Instance() {
  static AdvancementSystem instance;
  return instance;
}

void AdvancementSystem::SetCallbacks(
    std::function<void(const ElementRewards&)> on_rewards,
    std::function<void()> on_quests_changed,
    std::function<void(const std::string&)> on_tutorial_completed) {
  on_rewards_ = std::move(on_rewards);
  on_quests_changed_ = std::move(on_quests_changed);
  on_tutorial_completed_ = std::move(on_tutorial_completed);

  auto wire = [&](AdvancementElement* el, bool is_tut) {
    el->on_rewards = on_rewards_;
    el->on_updated = [this, is_tut](AdvancementElement* updated) {
      if (is_tut && on_tutorial_completed_ && updated->completed) {
        on_tutorial_completed_(updated->key);
      } else if (!is_tut && on_quests_changed_) {
        on_quests_changed_();
      }
    };
  };

  for (auto& r : recipe_unlocks_) wire(r.get(), false);
  for (auto& s : quest_series_) {
    wire(s.get(), false);
    for (auto& [qk, q] : s->quests) wire(q.get(), false);
  }
  for (auto& t : tutorials_) wire(t.get(), true);
}

void AdvancementSystem::InitFromPak(const pak::PakReader& pak) {
  recipe_unlocks_.clear();
  quest_series_.clear();
  series_order_.clear();
  tutorials_.clear();

  // 1. Recipe unlocks
  std::string text;
  if (pak.ReadTextEntry("resources/recipes.json", &text)) {
    try {
      auto root = nlohmann::json::parse(text);
      for (auto& [k, v] : root.items()) {
        auto el = std::make_unique<AdvancementElement>();
        PopulateElementFields(el.get(), k, v, false);
        el->Update();
        recipe_unlocks_.push_back(std::move(el));
      }
    } catch (const std::exception& e) {
      log::kGameHandler.Error(std::string("recipes.json error: ") + e.what());
    }
  }

  // 2. Quests
  if (pak.ReadTextEntry("resources/quests.json", &text)) {
    try {
      auto root = nlohmann::json::parse(text);
      for (auto& [sk, sv] : root.items()) {
        auto series = std::make_unique<QuestSeries>();
        PopulateElementFields(series.get(), sk, sv, true);
        if (sv.contains("quests") && sv["quests"].is_object()) {
          for (auto& [qk, qv] : sv["quests"].items()) {
            auto q = std::make_unique<Quest>();
            PopulateElementFields(q.get(), qk, qv, true);
            q->series = series.get();
            if (qv.contains("parent") && qv["parent"].is_string()) {
              q->parent_key = qv["parent"].get<std::string>();
            }
            series->quest_order.push_back(qk);
            series->quests.emplace(qk, std::move(q));
          }
        }
        series->Update();
        series_order_.push_back(sk);
        quest_series_.push_back(std::move(series));
      }
    } catch (const std::exception& e) {
      log::kGameHandler.Error(std::string("quests.json error: ") + e.what());
    }
  }

  // 3. Tutorials
  if (pak.ReadTextEntry("resources/tutorials.json", &text)) {
    try {
      auto root = nlohmann::json::parse(text);
      for (auto& [k, v] : root.items()) {
        auto el = std::make_unique<AdvancementElement>();
        PopulateElementFields(el.get(), k, v, true);
        el->Update();
        tutorials_.push_back(std::move(el));
      }
      std::stable_sort(
          tutorials_.begin(), tutorials_.end(),
          [](const std::unique_ptr<AdvancementElement>& a,
             const std::unique_ptr<AdvancementElement>& b) {
            return a->index < b->index;
          });
    } catch (const std::exception& e) {
      log::kGameHandler.Error(std::string("tutorials.json error: ") + e.what());
    }
  }
}

void AdvancementSystem::ResetAll() {
  current_ongoing_tutorial_.clear();
  for (auto& r : recipe_unlocks_) r->Reset(true);
  for (auto& s : quest_series_) {
    s->Reset(false);
    for (auto& [qk, q] : s->quests) q->Reset(false);
    s->Update();
  }
  for (auto& t : tutorials_) t->Reset(true);
}

void AdvancementSystem::LoadAdvancementsJson(std::string_view json_text) {
  ResetAll();
  if (json_text.empty()) return;
  try {
    auto root = nlohmann::json::parse(json_text);
    if (root.contains("CurrentOngoingTutorial") &&
        root["CurrentOngoingTutorial"].is_string()) {
      current_ongoing_tutorial_ =
          root["CurrentOngoingTutorial"].get<std::string>();
    }
    for (auto& r : recipe_unlocks_) {
      if (root.contains(r->key) && root[r->key].is_object()) {
        DeserializeElement(r.get(), root[r->key]);
        r->Update();
      }
    }
    for (auto& s : quest_series_) {
      if (root.contains(s->key) && root[s->key].is_object()) {
        DeserializeElement(s.get(), root[s->key]);
      }
      for (auto& [qk, q] : s->quests) {
        if (root.contains(qk) && root[qk].is_object()) {
          DeserializeElement(q.get(), root[qk]);
        }
      }
      s->Update();
    }
    for (auto& t : tutorials_) {
      if (root.contains(t->key) && root[t->key].is_object()) {
        DeserializeElement(t.get(), root[t->key]);
        t->Update();
      }
    }
  } catch (const std::exception& e) {
    log::kGameHandler.Error(
        std::string("advancements load error: ") + e.what());
  }
}

std::string AdvancementSystem::SaveAdvancementsJson() const {
  nlohmann::json root = nlohmann::json::object();
  if (!current_ongoing_tutorial_.empty()) {
    root["CurrentOngoingTutorial"] = current_ongoing_tutorial_;
  }
  for (const auto& r : recipe_unlocks_) {
    auto j = SerializeElement(*r);
    if (!j.empty()) root[r->key] = j;
  }
  for (const auto& s : quest_series_) {
    auto j = SerializeElement(*s);
    if (!j.empty()) root[s->key] = j;
    for (const auto& qk : s->quest_order) {
      auto qit = s->quests.find(qk);
      if (qit != s->quests.end()) {
        auto qj = SerializeElement(*qit->second);
        if (!qj.empty()) root[qk] = qj;
      }
    }
  }
  for (const auto& t : tutorials_) {
    auto j = SerializeElement(*t);
    if (!j.empty()) root[t->key] = j;
  }
  return root.dump(2);
}

void AdvancementSystem::LoadRecipeUnlocksJson(std::string_view json_text) {
  for (auto& r : recipe_unlocks_) r->Reset(true);
  if (json_text.empty()) return;
  try {
    auto root = nlohmann::json::parse(json_text);
    for (auto& r : recipe_unlocks_) {
      if (root.contains(r->key) && root[r->key].is_object()) {
        DeserializeElement(r.get(), root[r->key]);
        r->Update();
      }
    }
  } catch (const std::exception& e) {
    log::kGameHandler.Error(
        std::string("recipe unlocks load error: ") + e.what());
  }
}

std::string AdvancementSystem::SaveRecipeUnlocksJson() const {
  nlohmann::json root = nlohmann::json::object();
  for (const auto& r : recipe_unlocks_) {
    auto j = SerializeElement(*r);
    if (!j.empty()) root[r->key] = j;
  }
  return root.dump(2);
}

void AdvancementSystem::TriggerInventoryChanged(const InventorySnapshot& inv,
                                                std::string_view timestamp) {
  int32_t occupied = static_cast<int32_t>(inv.items.size());
  int32_t empty_slots = inv.max_slots - occupied;
  int32_t full_slots = 0;
  for (const auto& it : inv.items) {
    if (!it.is_stackable || it.count >= it.max_count) ++full_slots;
  }

  auto check_map = [&](AdvancementElement* owner,
                       std::unordered_map<std::string, ElementCriterion>& map,
                       bool unlock_check) {
    if (unlock_check && owner->unlocked) return;
    if (!unlock_check && !owner->unlocked) return;
    bool changed = false;
    for (auto& [k, c] : map) {
      if (c.IsCompleted() || c.trigger != TriggerType::kInventoryChanged)
        continue;
      if (!c.slots_empty.IsAbsent() && !c.slots_empty.InRange(empty_slots))
        continue;
      if (!c.slots_full.IsAbsent() && !c.slots_full.InRange(full_slots))
        continue;
      if (!c.slots_occupied.IsAbsent() && !c.slots_occupied.InRange(occupied))
        continue;
      if (!CheckInventoryItemsMatch(inv.items, c.inv_items)) continue;
      c.completion_time = std::string(timestamp);
      changed = true;
    }
    if (changed) owner->Update();
  };

  auto check_el = [&](AdvancementElement* el) {
    check_map(el, el->unlocking_criteria, true);
    check_map(el, el->criteria, false);
  };

  for (auto& r : recipe_unlocks_) check_el(r.get());
  for (auto& s : quest_series_) {
    check_el(s.get());
    for (auto& [qk, q] : s->quests) check_el(q.get());
  }
  for (auto& t : tutorials_) check_el(t.get());
}

void AdvancementSystem::TriggerPlacedTile(const TileEvent& ev,
                                          std::string_view timestamp) {
  auto check_map = [&](AdvancementElement* owner,
                       std::unordered_map<std::string, ElementCriterion>& map,
                       bool unlock_check) {
    if (unlock_check && owner->unlocked) return;
    if (!unlock_check && !owner->unlocked) return;
    bool changed = false;
    for (auto& [k, c] : map) {
      if (c.IsCompleted() || c.trigger != TriggerType::kPlacedTile) continue;
      if (c.tile.has_value() && !EqualsIgnoreCase(*c.tile, ev.tile_name))
        continue;
      if (c.item_cond.has_value() && !c.item_cond->Matches(ev.item)) continue;
      if (c.location_cond.has_value() && !c.location_cond->Matches(ev))
        continue;
      if (c.data.has_value() && *c.data != ev.data) continue;
      c.completion_time = std::string(timestamp);
      changed = true;
    }
    if (changed) owner->Update();
  };

  auto check_el = [&](AdvancementElement* el) {
    check_map(el, el->unlocking_criteria, true);
    check_map(el, el->criteria, false);
  };

  for (auto& r : recipe_unlocks_) check_el(r.get());
  for (auto& s : quest_series_) {
    check_el(s.get());
    for (auto& [qk, q] : s->quests) check_el(q.get());
  }
  for (auto& t : tutorials_) check_el(t.get());
}

void AdvancementSystem::TriggerItemUsedOnTile(const TileEvent& ev,
                                              std::string_view timestamp) {
  auto check_map = [&](AdvancementElement* owner,
                       std::unordered_map<std::string, ElementCriterion>& map,
                       bool unlock_check) {
    if (unlock_check && owner->unlocked) return;
    if (!unlock_check && !owner->unlocked) return;
    bool changed = false;
    for (auto& [k, c] : map) {
      if (c.IsCompleted() || c.trigger != TriggerType::kItemUsedOnTile)
        continue;
      if (c.location_cond.has_value() && !c.location_cond->Matches(ev))
        continue;
      if (c.item_cond.has_value() && !c.item_cond->Matches(ev.item)) continue;
      c.completion_time = std::string(timestamp);
      changed = true;
    }
    if (changed) owner->Update();
  };

  auto check_el = [&](AdvancementElement* el) {
    check_map(el, el->unlocking_criteria, true);
    check_map(el, el->criteria, false);
  };

  for (auto& r : recipe_unlocks_) check_el(r.get());
  for (auto& s : quest_series_) {
    check_el(s.get());
    for (auto& [qk, q] : s->quests) check_el(q.get());
  }
  for (auto& t : tutorials_) check_el(t.get());
}

}  // namespace util
}  // namespace minicraft
