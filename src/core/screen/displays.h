#ifndef MINICRAFT_CORE_SCREEN_DISPLAYS_H_
#define MINICRAFT_CORE_SCREEN_DISPLAYS_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/gfx/ellipsis.h"
#include "core/gfx/sprite_atlas.h"
#include "core/screen/display.h"

namespace minicraft {
namespace entity {
class Chest;
class Entity;
class Player;
}  // namespace entity
namespace item {
class Inventory;
class Recipe;
}  // namespace item
namespace level {
class Level;
}
namespace util {
class Quest;
class QuestSeries;
}  // namespace util
}  // namespace minicraft

namespace minicraft::screen {

// ---------------------------------------------------------------------------
// PopupDisplay
// ---------------------------------------------------------------------------
struct PopupActionCallback {
  std::string key;
  std::function<bool(Menu*)> callback;
};

struct PopupConfig {
  std::string title;
  std::vector<PopupActionCallback> callbacks;
  int32_t entry_spacing = 0;
  bool localize_title = true;
};

class PopupDisplay : public Display {
 public:
  PopupDisplay(std::optional<PopupConfig> config,
               const std::vector<std::string>& messages,
               bool clear_screen = false, bool menu_frame = true);
  PopupDisplay(std::optional<PopupConfig> config,
               std::vector<std::unique_ptr<ListEntry>> entries,
               bool clear_screen = false, bool menu_frame = true);

  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 private:
  bool TickCallbacks(io::InputHandler& input);
  std::vector<PopupActionCallback> callbacks_;
  std::unique_ptr<OnScreenKeyboardMenu> osk_;
};

// ---------------------------------------------------------------------------
// Title, World Select, World Gen, Loading
// ---------------------------------------------------------------------------
class TitleDisplay : public Display {
 public:
  TitleDisplay();
  void Init(Display* parent) override;
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 private:
  int32_t rand_idx_ = 2;
  mutable int32_t count_ = 0;
  mutable bool reverse_ = false;
  gfx::LinkedSprite title_sheet_{gfx::SpriteType::kGui, "title"};
};

class WorldSelectDisplay : public Display {
 public:
  WorldSelectDisplay();
  void Init(Display* parent) override;
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

  static const std::string& WorldName() { return world_name_; }
  static bool HasLoadedWorld() { return loaded_world_; }
  static void SetWorldName(std::string name, bool loaded) {
    world_name_ = std::move(name);
    loaded_world_ = loaded;
  }
  static std::vector<std::string> GetWorldNames();

 private:
  void RefreshWorlds();
  void RebuildEntries();

  std::vector<std::string> world_names_;
  std::vector<std::string> world_versions_;
  std::vector<bool> world_compatible_;
  static std::string world_name_;
  static bool loaded_world_;
};

class WorldGenDisplay : public Display {
 public:
  WorldGenDisplay();
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;
  static std::unique_ptr<InputEntry> MakeWorldNameInput(
      std::string prompt, std::vector<std::string> taken_names,
      std::string init_value, bool is_gen);
  static std::optional<int64_t> ParseSeed(std::string_view seed_str);

 private:
  InputEntry* name_field_ = nullptr;
  InputEntry* seed_field_ = nullptr;
  std::unique_ptr<OnScreenKeyboardMenu> osk_;
};

class LoadingDisplay : public Display {
 public:
  LoadingDisplay(std::string world_name, bool is_new_world,
                 std::optional<int64_t> seed = std::nullopt);
  ~LoadingDisplay() override;

  LoadingDisplay(const LoadingDisplay&) = delete;
  LoadingDisplay& operator=(const LoadingDisplay&) = delete;

  void Init(Display* parent) override;
  void OnExit() override;
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

  static void SetPercentage(float percent);
  static float GetPercentage();
  static void SetMessage(std::string_view progress_type, bool localize = true);
  static void Progress(float amt);

 private:
  void JoinWorker();

  std::string world_name_;
  bool is_new_world_ = true;
  std::optional<int64_t> seed_;
  bool started_ = false;
  int32_t delay_ticks_ = 2;
  std::string msg_;
  mutable gfx::SmoothEllipsis ellipsis_;
  std::thread worker_;
  std::atomic<bool> completed_{false};
  std::atomic<bool> load_ok_{false};

  static std::mutex progress_mutex_;
  static float percentage_;
  static std::string progress_type_;
  static bool localize_progress_;
};

// ---------------------------------------------------------------------------
// Options, Controls, Keybindings, Language, Skins, Resource Packs, Achievements
// ---------------------------------------------------------------------------
class OptionsMainMenuDisplay : public Display {
 public:
  OptionsMainMenuDisplay();
  void OnExit() override;
};

class OptionsWorldDisplay : public Display {
 public:
  OptionsWorldDisplay();
  void OnExit() override;
};

class ControlsDisplay : public Display {
 public:
  ControlsDisplay();
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 private:
  void SwitchMode(int32_t mode);
  int32_t displaying_ = 0;  // 0=keyboard, 1=controller
};

class KeyInputDisplay : public Display {
 public:
  KeyInputDisplay();
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 private:
  void RebuildEntries(int32_t keep_sel = -1, int32_t keep_disp = -1);
  bool waiting_popup_ = false;
};

class LanguageSettingsDisplay : public Display {
 public:
  LanguageSettingsDisplay();
};

class SkinDisplay : public Display {
 public:
  SkinDisplay();
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;
  void OnExit() override;

  static const std::vector<std::string>& DefaultSkins();
  static int32_t SelectedSkinIndex();
  static void SetSelectedSkinIndex(int32_t idx);

 private:
  int32_t step_ = 0;
  std::vector<std::string> skin_names_;
};

class ResourcePackDisplay : public Display {
 public:
  struct PackInfo {
    std::string folder;       // Directory name under resourcepacks/ (empty for Default).
    std::string name;         // Display name (from pack.json or folder).
    std::string description;  // Description (from pack.json).
    int32_t pack_format = 1;
    uint16_t logo_tex = 0;    // Uploaded custom pack.png texture ID (0 = fallback).
    uint16_t logo_w = 0;
    uint16_t logo_h = 0;
    bool enabled = false;
    bool locked = false;      // True only for the built-in Default pack.
  };

  ResourcePackDisplay();
  ~ResourcePackDisplay() override;
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;
  void OnExit() override;

  const std::vector<PackInfo>& Packs() const { return packs_; }

 private:
  void LoadPacks();
  void FreeLogos();
  void RebuildMenus(int32_t keep_selection = 0, int32_t keep_disp = 0);
  void SyncActiveResourcePacks();

  std::vector<PackInfo> packs_;
  bool changed_ = false;
};

class AchievementsDisplay : public Display {
 public:
  AchievementsDisplay();
  void Init(Display* parent) override;
  void OnExit() override;
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 private:
  void UpdateDescription(bool force = false);
  std::string selected_id_;
};

// ---------------------------------------------------------------------------
// In-game Displays & HUD Helpers
// ---------------------------------------------------------------------------
class PauseDisplay : public Display {
 public:
  PauseDisplay();
  void Init(Display* parent) override;
  void Tick(io::InputHandler& input) override;
};

class CraftingDisplay : public Display {
 public:
  CraftingDisplay(std::vector<item::Recipe> recipes, std::string title,
                  entity::Player* player, bool is_personal = false);
  ~CraftingDisplay() override;

  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

  static void ResetRecipeUnlocks();
  static void UnlockRecipe(const item::Recipe& r);
  static bool IsRecipeUnlocked(const item::Recipe& r);
  static const std::vector<item::Recipe>& UnlockedRecipes();
  static void LoadUnlockedRecipes(std::vector<item::Recipe> recipes);
  static void RefreshInstanceIfNeeded();

 private:
  void RefreshDisplayRecipes();
  void RefreshData();
  void RefreshCraftabilityInPlace();
  int32_t GetCurItemCount() const;
  std::vector<std::unique_ptr<ListEntry>> GetCurItemCosts() const;

  entity::Player* player_ = nullptr;
  std::string title_;
  bool is_personal_ = false;
  std::vector<item::Recipe> available_recipes_;
  std::vector<item::Recipe> shown_recipes_;
  std::unique_ptr<OnScreenKeyboardMenu> osk_;

  static std::vector<item::Recipe> unlocked_recipes_;
  static CraftingDisplay* active_instance_;
};

class PlayerInvDisplay : public Display {
 public:
  explicit PlayerInvDisplay(entity::Player* player);
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 protected:
  void OnSelectionChange(int32_t old_sel, int32_t new_sel) override;

 private:
  void RebuildMenus();
  void UpdateDescriptionMenu();
  std::string CurrentDescription() const;

  entity::Player* player_ = nullptr;
  bool creative_mode_ = false;
  std::shared_ptr<item::Inventory> creative_inv_;
  std::unique_ptr<OnScreenKeyboardMenu> osk_;
  gfx::LinkedSprite counter_sheet_{gfx::SpriteType::kGui, "inventory_counter"};
};

class ContainerDisplay : public Display {
 public:
  ContainerDisplay(entity::Player* player, entity::Chest* chest);
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 protected:
  void OnSelectionChange(int32_t old_sel, int32_t new_sel) override;

 private:
  void RebuildMenus();

  entity::Player* player_ = nullptr;
  entity::Chest* chest_ = nullptr;
  std::unique_ptr<OnScreenKeyboardMenu> osk_;
  gfx::LinkedSprite counter_sheet_{gfx::SpriteType::kGui, "inventory_counter"};
};

class SignDisplay : public Display {
 public:
  static constexpr int32_t kMaxTextLength = 20;
  static constexpr int32_t kMaxRowCount = 4;

  SignDisplay(int32_t level_depth, int32_t x, int32_t y);
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

  static void ResetSignTexts();
  static void UpdateSign(int32_t level_depth, int32_t x, int32_t y,
                         std::vector<std::string> lines);
  static void RemoveSign(int32_t level_depth, int32_t x, int32_t y);
  static const std::vector<std::string>* GetSign(int32_t level_depth, int32_t x,
                                                 int32_t y);
  static void SyncFromMap(const saveload::SignMap& map);
  static void ExportToMap(saveload::SignMap* out_map);
  static uint32_t SignVersion();

 private:
  void UpdateCaretAnimation();
  void InsertChars(std::string_view chars);
  bool InsertChar(char c);
  void TickEditor(io::InputHandler& input);

  int32_t level_depth_ = 0;
  int32_t x_ = 0;
  int32_t y_ = 0;
  std::array<std::string, kMaxRowCount> rows_{};
  int32_t cursor_x_ = 0;
  int32_t cursor_y_ = 0;
  int32_t caret_timer_ = 60;
  bool caret_shown_ = true;
  std::unique_ptr<OnScreenKeyboardMenu> osk_;
};

class SignDisplayMenu {
 public:
  SignDisplayMenu(int32_t level_depth, int32_t x, int32_t y);
  bool Matches(int32_t level_depth, int32_t x, int32_t y) const;
  void Render(gfx::Screen* screen) const;

 private:
  int32_t level_depth_ = 0;
  int32_t x_ = 0;
  int32_t y_ = 0;
  uint32_t version_ = 0;
  std::unique_ptr<Menu> menu_;
};

class BookDisplay : public Display {
 public:
  explicit BookDisplay(std::string book, bool has_title = false);
  void Tick(io::InputHandler& input) override;

 private:
  void TurnPage(int32_t dir);

  std::vector<std::vector<std::string>> pages_;
  int32_t page_ = 0;
  bool has_title_ = false;
  bool show_page_count_ = true;
  int32_t page_offset_ = 1;
};

class InfoDisplay : public Display {
 public:
  InfoDisplay();
  void Tick(io::InputHandler& input) override;
  static std::string GetTimeString();
};

class PlayerDeathDisplay : public Display {
 public:
  PlayerDeathDisplay();
};

class EndGameDisplay : public Display {
 public:
  EndGameDisplay();
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 private:
  int32_t display_timer_ = 60;
  int32_t input_delay_ = 30;
  int32_t final_score_ = 0;
};

class LevelTransitionDisplay : public Display {
 public:
  explicit LevelTransitionDisplay(int32_t dir);
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

 private:
  static constexpr int32_t kDuration = 30;
  int32_t dir_ = 1;
  int32_t time_ = 0;
  gfx::LinkedSprite hud_sheet_{gfx::SpriteType::kGui, "hud"};
};

class QuestsDisplay : public Display {
 public:
  QuestsDisplay();
  ~QuestsDisplay() override;
  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen) const override;

  static void RefreshIfOpen();

  class SeriesInformationDisplay : public Display {
   public:
    explicit SeriesInformationDisplay(const util::QuestSeries* series);
  };

  class SeriesQuestViewerDisplay : public Display {
   public:
    explicit SeriesQuestViewerDisplay(const util::QuestSeries* series);
    void Tick(io::InputHandler& input) override;
    void Render(gfx::Screen* screen) const override;

   private:
    struct QuestNode {
      const util::Quest* quest = nullptr;
      const util::Quest* parent = nullptr;
      gfx::Rectangle bounds;
    };

    void ScrollIfNeeded();

    std::vector<std::vector<QuestNode>> rows_;
    int32_t cursor_x_ = 0;
    int32_t cursor_y_ = 0;
    int32_t x_scroll_ = 0;
    int32_t y_scroll_ = 0;
    bool in_browsing_ = false;
    int32_t raster_x_ = 0;
    int32_t raster_y_ = 0;
    int32_t raster_w_ = 0;
    int32_t raster_h_ = 0;
  };

  class QuestInformationDisplay : public Display {
   public:
    explicit QuestInformationDisplay(const util::Quest* quest);
  };

 private:
  void ReloadEntries();
  void UpdateEntries();

  int32_t selected_tab_ = 0;  // 0 = unlocked, 1 = completed
  std::array<int32_t, 2> prev_selections_{0, 0};
  std::vector<const util::QuestSeries*> unlocked_series_;
  std::vector<const util::QuestSeries*> completed_series_;

  static QuestsDisplay* active_instance_;
};

class TutorialDisplayHandler {
 public:
  static void Reset(bool initial);
  static void Tick(io::InputHandler& input);
  static void Render(gfx::Screen* screen);
  static bool InTutorial();
  static bool InGuide();
  static bool InQuests();
  static void UpdateCompletedElement(const std::string& key);
  static void SkipCurrent();
  static void TurnOffTutorials();

 private:
  static void RefreshAll();
  static void GetNextTutorial();
  static void TurnOffGuides();

  struct ControlGuide {
    int32_t duration = 1;
    std::string keys;
    std::string loc_key;
    int32_t interacted_ticks = 0;
  };
  static std::vector<ControlGuide> guides_;
  static int32_t current_guide_idx_;
  static int32_t guide_banner_anim_;
  static int32_t current_ongoing_idx_;
};

}  // namespace minicraft::screen

#endif  // MINICRAFT_CORE_SCREEN_DISPLAYS_H_
