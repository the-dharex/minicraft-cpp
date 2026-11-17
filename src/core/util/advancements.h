#ifndef MINICRAFT_CORE_UTIL_ADVANCEMENTS_H_
#define MINICRAFT_CORE_UTIL_ADVANCEMENTS_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/pak/pak_reader.h"

namespace minicraft {
namespace util {

// ---------------------------------------------------------------------------
// Achievements
// ---------------------------------------------------------------------------
struct Achievement {
  std::string id;
  std::string name;
  std::string description;
  int32_t score = 0;
  bool unlocked = false;
};

class AchievementManager {
 public:
  using AchievementCallback = std::function<void(const std::string& id)>;

  static AchievementManager& Instance();

  void LoadFromPak(const pak::PakReader& pak);
  void Reset();

  bool SetAchievement(std::string_view id, bool unlocked,
                      bool is_creative = false, bool allow_creative = false);

  std::vector<std::string> GetUnlockedIds() const;
  void UnlockFromList(const std::vector<std::string>& ids);

  const std::unordered_map<std::string, Achievement>& All() const {
    return achievements_;
  }
  const std::vector<std::string>& OrderedIds() const { return ordered_ids_; }
  const Achievement* Find(std::string_view id) const;

  int32_t TotalScore() const { return total_score_; }
  void SetOnUnlocked(AchievementCallback cb) { on_unlocked_ = std::move(cb); }

 private:
  std::unordered_map<std::string, Achievement> achievements_;
  std::vector<std::string> ordered_ids_;
  int32_t total_score_ = 0;
  AchievementCallback on_unlocked_;
};

// ---------------------------------------------------------------------------
// Advancement Triggers, Quests, Tutorials, Recipe Unlocks
// ---------------------------------------------------------------------------
struct ItemSnapshot {
  std::string name;
  int32_t count = 1;
  bool is_stackable = false;
  int32_t max_count = 1;
  bool is_tool = false;
  int32_t durability = 0;
};

struct InventorySnapshot {
  std::vector<ItemSnapshot> items;
  int32_t max_slots = 27;
};

struct TileEvent {
  ItemSnapshot item;
  std::string tile_name;
  int32_t data = 0;
  int32_t x = 0;
  int32_t y = 0;
  int32_t level = 0;
};

struct IntRange {
  std::optional<int32_t> min;
  std::optional<int32_t> max;
  bool IsAbsent() const { return !min.has_value() && !max.has_value(); }
  bool InRange(int32_t v) const;
};

struct DoubleRange {
  std::optional<double> min;
  std::optional<double> max;
  bool IsAbsent() const { return !min.has_value() && !max.has_value(); }
  bool InRange(double v) const;
};

struct ItemConditions {
  std::vector<std::string> items;
  IntRange count;
  IntRange durability;
  bool Matches(const ItemSnapshot& item) const;
};

struct LocationConditions {
  std::vector<std::string> tiles;
  std::optional<int32_t> level;
  std::optional<int32_t> data;
  DoubleRange x;
  DoubleRange y;
  bool Matches(const TileEvent& ev) const;
};

enum class TriggerType : uint8_t {
  kImpossible = 0,
  kInventoryChanged = 1,
  kPlacedTile = 2,
  kItemUsedOnTile = 3,
};

struct ElementCriterion {
  TriggerType trigger = TriggerType::kImpossible;
  std::vector<ItemConditions> inv_items;
  IntRange slots_empty;
  IntRange slots_full;
  IntRange slots_occupied;

  std::optional<std::string> tile;
  std::optional<ItemConditions> item_cond;
  std::optional<LocationConditions> location_cond;
  std::optional<int32_t> data;

  std::string completion_time;  // Empty if not completed.
  bool IsCompleted() const { return !completion_time.empty(); }
};

struct RecipeReward {
  std::string product;
  std::vector<std::string> costs;
};

struct ElementRewards {
  std::vector<std::string> items;
  std::vector<RecipeReward> recipes;
};

class AdvancementElement {
 public:
  std::string key;
  std::string description;
  int32_t index = 0;
  std::unordered_map<std::string, ElementCriterion> criteria;
  std::vector<std::vector<std::string>> requirements;
  std::unordered_map<std::string, ElementCriterion> unlocking_criteria;
  std::vector<std::vector<std::string>> unlocking_requirements;
  ElementRewards rewards;

  bool completed = false;
  bool unlocked = false;

  virtual ~AdvancementElement() = default;

  bool IsDisplayableAtStatus() const { return unlocked && !completed; }
  int32_t NumCriteriaCompleted() const;
  int32_t TotalNumCriteria() const {
    return static_cast<int32_t>(criteria.size());
  }
  bool ShouldAllCriteriaBeCompleted() const { return requirements.empty(); }

  virtual bool IsUnlockable() const;
  virtual bool CheckIsCompleted() const;
  virtual void Update();
  void Reset(bool do_update = true);

  std::string LoadFromJsonStr(std::string_view json_text);  // Returns err if any.
  std::string SaveToJsonStr() const;

  std::function<void(const ElementRewards&)> on_rewards;
  std::function<void(AdvancementElement*)> on_updated;
};

class QuestSeries;

class Quest : public AdvancementElement {
 public:
  QuestSeries* series = nullptr;
  std::string parent_key;

  const Quest* Parent() const;
  bool IsUnlockable() const override;
  void Update() override;
  void UpdateShallow();
};

class QuestSeries : public AdvancementElement {
 public:
  std::unordered_map<std::string, std::unique_ptr<Quest>> quests;
  std::vector<std::string> quest_order;

  bool CheckIsCompleted() const override;
  void Update() override;
};

class AdvancementSystem {
 public:
  static AdvancementSystem& Instance();

  void InitFromPak(const pak::PakReader& pak);

  // Trigger dispatches from gameplay systems.
  void TriggerInventoryChanged(const InventorySnapshot& inv,
                               std::string_view timestamp = "completed");
  void TriggerPlacedTile(const TileEvent& ev,
                         std::string_view timestamp = "completed");
  void TriggerItemUsedOnTile(const TileEvent& ev,
                             std::string_view timestamp = "completed");

  // Save/load JSON blocks (matches advancements.json & Unlocks.json keys).
  void ResetAll();
  void LoadAdvancementsJson(std::string_view json_text);
  std::string SaveAdvancementsJson() const;
  void LoadRecipeUnlocksJson(std::string_view json_text);
  std::string SaveRecipeUnlocksJson() const;

  const std::vector<std::unique_ptr<AdvancementElement>>& RecipeUnlocks()
      const {
    return recipe_unlocks_;
  }
  const std::vector<std::unique_ptr<QuestSeries>>& QuestSeriesList() const {
    return quest_series_;
  }
  const std::vector<std::unique_ptr<AdvancementElement>>& Tutorials() const {
    return tutorials_;
  }
  const std::string& CurrentOngoingTutorial() const {
    return current_ongoing_tutorial_;
  }
  void SetCurrentOngoingTutorial(std::string key) {
    current_ongoing_tutorial_ = std::move(key);
  }

  void SetCallbacks(
      std::function<void(const ElementRewards&)> on_rewards,
      std::function<void()> on_quests_changed,
      std::function<void(const std::string& key)> on_tutorial_completed);

 private:
  void EvaluateCriterion(AdvancementElement* owner, ElementCriterion& crit,
                         std::string_view timestamp);

  std::vector<std::unique_ptr<AdvancementElement>> recipe_unlocks_;
  std::vector<std::unique_ptr<QuestSeries>> quest_series_;
  std::vector<std::string> series_order_;
  std::vector<std::unique_ptr<AdvancementElement>> tutorials_;
  std::string current_ongoing_tutorial_;

  std::function<void(const ElementRewards&)> on_rewards_;
  std::function<void()> on_quests_changed_;
  std::function<void(const std::string&)> on_tutorial_completed_;
};

}  // namespace util
}  // namespace minicraft

#endif  // MINICRAFT_CORE_UTIL_ADVANCEMENTS_H_
