#include "core/screen/displays.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/entity/furniture.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/io/sound.h"
#include "core/item/inventory.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/item/recipes.h"
#include "core/level/level.h"
#include "core/saveload/save.h"
#include "core/screen/entries.h"
#include "core/screen/menu.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/random.h"

namespace minicraft::screen {

namespace {

constexpr int32_t kInvPadding = 10;

float CalcHeaviness(size_t cur, int32_t max_slots) {
  if (max_slots <= 0) return 1.0f;
  const float inner =
      static_cast<float>(cur) / static_cast<float>(max_slots) - 1.0f;
  return std::clamp(-(inner * inner) + 1.0f, 0.0f, 1.0f);
}

int32_t ColorByHeaviness(float val, bool focused) {
  val = std::clamp(val, 0.0f, 1.0f);
  if (focused) {
    if (val < 0.5f) {
      const int32_t r = static_cast<int32_t>(val / 0.5f * 255.0f);
      return gfx::color::Get(1, r, 255, 0);
    } else {
      const int32_t g = static_cast<int32_t>((1.0f - val) / 0.5f * 255.0f);
      return gfx::color::Get(1, 255, g, 0);
    }
  } else {
    if (val < 0.3f) {
      const int32_t b = static_cast<int32_t>(val / 0.3f * 255.0f);
      return gfx::color::Get(1, 255, 255, b);
    } else {
      const int32_t g = static_cast<int32_t>((1.0f - val) / 0.7f * 255.0f);
      return gfx::color::Get(1, 255, g, 0);
    }
  }
}

int32_t FadeColor(int32_t color) {
  constexpr float kOpacity = 0.35f;
  const int32_t r = (color >> 16) & 0xFF;
  const int32_t g = (color >> 8) & 0xFF;
  const int32_t b = color & 0xFF;
  const int32_t nr = 0x2A + static_cast<int32_t>((r - 0x2A) * kOpacity);
  const int32_t ng = 0x32 + static_cast<int32_t>((g - 0x32) * kOpacity);
  const int32_t nb = 0x99 + static_cast<int32_t>((b - 0x99) * kOpacity);
  return gfx::color::Get(1, nr, ng, nb);
}

void RenderCounterNumber(gfx::Screen* screen, const gfx::SheetRect* sheet,
                         int32_t xp, int32_t yp, int32_t ys, int32_t w,
                         int32_t h, int32_t n, int32_t col) {
  if (!screen || !sheet) return;
  const std::string s = std::to_string(n);
  for (size_t i = 0; i < s.size(); ++i) {
    const int32_t digit = s[i] - '0';
    screen->RenderSub(xp + static_cast<int32_t>(i) * w, yp, w * digit, ys, w,
                      h, sheet, 0, col);
  }
}

void RenderCounterExpanded(gfx::Screen* screen, const gfx::SheetRect* sheet,
                           const gfx::Rectangle& bounds, size_t size,
                           int32_t cap, bool right_side) {
  if (!screen || !sheet) return;
  const int32_t sz = static_cast<int32_t>(size);
  const int32_t col =
      ColorByHeaviness(CalcHeaviness(size, cap), true);
  if (!right_side) {
    if (sz < 10) {
      screen->RenderSub(bounds.Right() + 2 - 18, bounds.Top() - 3, 12, 12, 3,
                        13, sheet);
      screen->RenderSub(bounds.Right() + 2 - 15, bounds.Top() - 3, 20, 12, 15,
                        13, sheet);
      RenderCounterNumber(screen, sheet, bounds.Right() + 2 - 16,
                          bounds.Top() - 1, 5, 5, 7, sz, col);
      RenderCounterNumber(screen, sheet, bounds.Right() + 2 - 10,
                          bounds.Top() + 3, 0, 4, 5, cap, gfx::color::kGray);
    } else {
      screen->RenderSub(bounds.Right() + 2 - 23, bounds.Top() - 3, 12, 12, 23,
                        13, sheet);
      RenderCounterNumber(screen, sheet, bounds.Right() + 2 - 21,
                          bounds.Top() - 1, 5, 5, 7, sz, col);
      RenderCounterNumber(screen, sheet, bounds.Right() + 2 - 10,
                          bounds.Top() + 3, 0, 4, 5, cap, gfx::color::kGray);
    }
  } else {
    if (sz < 10) {
      screen->RenderSub(bounds.Left() - 2, bounds.Top() - 3, 20, 12, 15, 13,
                        sheet, 1);
      screen->RenderSub(bounds.Left() - 2 + 15, bounds.Top() - 3, 12, 12, 3,
                        13, sheet, 1);
      RenderCounterNumber(screen, sheet, bounds.Left() - 2 + 11,
                          bounds.Top() - 1, 5, 5, 7, sz, col);
      RenderCounterNumber(screen, sheet, bounds.Left(), bounds.Top() + 3, 0, 4,
                          5, cap, gfx::color::kGray);
    } else {
      screen->RenderSub(bounds.Left() - 2, bounds.Top() - 3, 12, 12, 23, 13,
                        sheet, 1);
      RenderCounterNumber(screen, sheet, bounds.Left() - 2 + 11,
                          bounds.Top() - 1, 5, 5, 7, sz, col);
      RenderCounterNumber(screen, sheet, bounds.Left(), bounds.Top() + 3, 0, 4,
                          5, cap, gfx::color::kGray);
    }
  }
}

void RenderCounterCompact(gfx::Screen* screen, const gfx::SheetRect* sheet,
                          const gfx::Rectangle& bounds, size_t size,
                          int32_t cap, bool on_left_of_bounds) {
  if (!screen || !sheet) return;
  const int32_t sz = static_cast<int32_t>(size);
  const int32_t col =
      FadeColor(ColorByHeaviness(CalcHeaviness(size, cap), false));
  const int32_t base_x =
      on_left_of_bounds ? (bounds.Right() - 4 - (sz < 10 ? 8 : 12))
                        : (bounds.Left() + 4);
  if (sz < 10) {
    screen->RenderSub(base_x, bounds.Top() - 1, 0, 12, 4, 9, sheet);
    screen->RenderSub(base_x + 4, bounds.Top() - 1, 8, 12, 4, 9, sheet);
  } else {
    screen->RenderSub(base_x, bounds.Top() - 1, 0, 12, 12, 9, sheet);
  }
  RenderCounterNumber(screen, sheet, base_x + 2, bounds.Top() + 1, 0, 4, 5, sz,
                      col);
}

std::unique_ptr<Menu> BuildInventoryMenu(entity::Entity* holder,
                                         item::Inventory& inv,
                                         const std::string& title,
                                         gfx::RelPos entry_pos,
                                         int32_t selection) {
  Menu::Builder b(true, 0, entry_pos);
  b.SetPositioning({9, 9}, gfx::RelPos::kBottomRight)
      .SetDisplayLength(9)
      .SetSelectable(true)
      .SetScrollPolicies(1.0f, false)
      .SetSearcherBar(true)
      .SetTitle(title);

  (void)holder;
  for (size_t i = 0; i < inv.InvSize(); ++i) {
    const item::Item* it = inv.Get(i);
    if (it) {
      b.AddEntry(std::make_unique<ItemEntry>(
          std::shared_ptr<item::Item>(it->Clone())));
    }
  }
  if (inv.InvSize() > 0) {
    b.SetSelection(std::clamp<int32_t>(
        selection, 0, static_cast<int32_t>(inv.InvSize()) - 1));
  }
  return b.CreateMenu();
}

std::unique_ptr<OnScreenKeyboardMenu> CreateHiddenOsk() {
  if (!DisplayHost::Instance().input) return nullptr;
  auto osk = OnScreenKeyboardMenu::CheckAndCreateMenu(
      *DisplayHost::Instance().input);
  if (osk) osk->SetVisible(false);
  return osk;
}

// Handles OnScreenKeyboardMenu ticking for searcher-bar menus.
// Returns true if the caller should run its normal input tick body.
bool TickSearcherMenuWithOsk(io::InputHandler& input, Menu* cur_menu,
                             OnScreenKeyboardMenu* osk) {
  if (!osk || !cur_menu) return true;
  if (!cur_menu->IsSearcherBarActive() && !osk->IsVisible()) return true;

  const auto res = osk->TickKeyboard(input);
  const bool acted = (res != OnScreenKeyboardMenu::TickResult::kNone);
  if (!acted) cur_menu->Tick(input);

  if (cur_menu->IsSearcherBarActive()) {
    if (input.ButtonPressed(platform::GamepadButton::kX)) {
      osk->SetVisible(!osk->IsVisible());
    }
  } else {
    osk->SetVisible(false);
  }
  return !osk->IsVisible();
}

bool HandleInventoryDrop(io::InputHandler& input, entity::Entity* holder,
                         item::Inventory* inv, Menu* menu,
                         bool creative_delete_mode) {
  if (!holder || !inv || !menu || inv->InvSize() == 0) return false;
  const int32_t sel = menu->GetSelection();
  if (sel < 0 || static_cast<size_t>(sel) >= inv->InvSize()) return false;

  const bool drop_one = input.InputPressed("drop-one");
  const bool drop_stack = input.InputPressed("drop-stack");
  if (!drop_one && !drop_stack) return false;

  item::Item* inv_item = inv->Get(static_cast<size_t>(sel));
  if (!inv_item) return false;

  bool changed = false;
  std::unique_ptr<item::Item> dropped;
  if (creative_delete_mode) {
    dropped = inv_item->Clone();
  } else {
    changed = true;
    auto* stack = dynamic_cast<item::StackableItem*>(inv_item);
    if (!drop_one || !stack || stack->count <= 1) {
      dropped = inv->Remove(static_cast<size_t>(sel));
    } else {
      dropped = inv_item->Clone();
      if (auto* d_stack = dynamic_cast<item::StackableItem*>(dropped.get())) {
        d_stack->count = 1;
      }
      stack->count--;
    }
  }

  if (dropped && holder->level) {
    holder->level->DropItem(holder->x, holder->y, std::move(dropped));
  }
  return changed;
}

void CheckCraftingAchievements(const item::Item* prod) {
  if (!prod) return;
  const std::string& name = prod->Name();
  const bool creative = game::GameState::Instance().IsCreativeMode();
  auto unlock = [&](std::string_view id) {
    util::AchievementManager::Instance().SetAchievement(id, true, creative);
  };
  if (name == "Workbench")
    unlock("minicraft.achievement.benchmarking");
  else if (name == "Plank")
    unlock("minicraft.achievement.planks");
  else if (name == "Wood Door")
    unlock("minicraft.achievement.doors");
  else if (name == "Rock Sword" || name == "Rock Pickaxe" ||
           name == "Rock Axe" || name == "Rock Shovel" ||
           name == "Rock Hoe" || name == "Rock Bow" ||
           name == "Rock Claymore")
    unlock("minicraft.achievement.upgrade");
  else if (name == "Blue Clothes" || name == "Green Clothes" ||
           name == "Yellow Clothes" || name == "Black Clothes" ||
           name == "Orange Clothes" || name == "Purple Clothes" ||
           name == "Cyan Clothes" || name == "Reg Clothes")
    unlock("minicraft.achievement.clothes");
  else if (name == "Boat")
    unlock("minicraft.achievement.boat");
}

}  // namespace

// ---------------------------------------------------------------------------
// PauseDisplay
// ---------------------------------------------------------------------------
PauseDisplay::PauseDisplay() : Display(false, true) {
  Menu::Builder b(true, 4, gfx::RelPos::kCenter);
  b.SetTitle("minicraft.displays.pause", gfx::color::kYellow);

  b.AddEntry(std::make_unique<BlankEntry>());
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.displays.pause.return",
      [] { DisplayHost::SetDisplay(nullptr); }));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.display.options_display",
      [] { DisplayHost::SetDisplay(std::make_unique<OptionsWorldDisplay>()); }));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.displays.achievements",
      [] { DisplayHost::SetDisplay(std::make_unique<AchievementsDisplay>()); }));
  if (TutorialDisplayHandler::InQuests()) {
    b.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.quests",
        [] { DisplayHost::SetDisplay(std::make_unique<QuestsDisplay>()); }));
  }
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.displays.pause.save", [] {
        DisplayHost::SetDisplay(nullptr);
        if (DisplayHost::Instance().save_world) {
          DisplayHost::Instance().save_world();
        }
      }));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.displays.pause.menu", [] {
        std::vector<std::unique_ptr<ListEntry>> items;
        for (auto& l : StringEntry::UseLines(
                 {"minicraft.displays.pause.display.exit_popup.0"})) {
          items.push_back(std::move(l));
        }
        for (auto& l : StringEntry::UseLines(
                 {"minicraft.displays.pause.display.exit_popup.1"},
                 gfx::color::kRed)) {
          items.push_back(std::move(l));
        }
        items.push_back(std::make_unique<BlankEntry>());
        items.push_back(std::make_unique<SelectEntry>(
            "minicraft.displays.pause.display.exit_popup.cancel",
            [] { DisplayHost::ExitDisplay(); }));
        items.push_back(std::make_unique<SelectEntry>(
            "minicraft.displays.pause.display.exit_popup.quit", [] {
              if (DisplayHost::Instance().exit_world) {
                DisplayHost::Instance().exit_world();
              }
              DisplayHost::SetDisplay(std::make_unique<TitleDisplay>());
            }));
        PopupConfig cfg;
        cfg.entry_spacing = 8;
        DisplayHost::SetDisplay(
            std::make_unique<PopupDisplay>(std::move(cfg), std::move(items)));
      }));
  b.AddEntry(std::make_unique<BlankEntry>());

  if (auto* input = DisplayHost::Instance().input) {
    b.AddEntry(std::make_unique<StringEntry>(
        util::Localization::GetLocalized(
            "minicraft.displays.pause.display.help.scroll",
            input->GetMapping("cursor-up"), input->GetMapping("cursor-down")),
        gfx::color::kGray, false));
    b.AddEntry(std::make_unique<StringEntry>(
        util::Localization::GetLocalized(
            "minicraft.displays.pause.display.help.choose",
            input->GetMapping("select")),
        gfx::color::kGray, false));
  }

  menus_.push_back(b.CreateMenu());
}

void PauseDisplay::Init(Display* /*parent*/) { Display::Init(nullptr); }

void PauseDisplay::Tick(io::InputHandler& input) {
  if (input.InputPressed("pause")) {
    DisplayHost::ExitDisplay();
    return;
  }
  Display::Tick(input);
}

// ---------------------------------------------------------------------------
// CraftingDisplay
// ---------------------------------------------------------------------------
std::vector<item::Recipe> CraftingDisplay::unlocked_recipes_;
CraftingDisplay* CraftingDisplay::active_instance_ = nullptr;

void CraftingDisplay::ResetRecipeUnlocks() { unlocked_recipes_.clear(); }

void CraftingDisplay::UnlockRecipe(const item::Recipe& r) {
  for (const auto& u : unlocked_recipes_) {
    if (u.Matches(r)) return;
  }
  unlocked_recipes_.push_back(r);
  RefreshInstanceIfNeeded();
}

void CraftingDisplay::RefreshInstanceIfNeeded() {
  if (active_instance_ && DisplayHost::GetDisplay() == active_instance_) {
    active_instance_->RefreshDisplayRecipes();
  }
}

bool CraftingDisplay::IsRecipeUnlocked(const item::Recipe& r) {
  if (game::GameState::Instance().IsCreativeMode()) return true;
  for (const auto& u : unlocked_recipes_) {
    if (u.Matches(r)) return true;
  }
  return false;
}

const std::vector<item::Recipe>& CraftingDisplay::UnlockedRecipes() {
  return unlocked_recipes_;
}

void CraftingDisplay::LoadUnlockedRecipes(std::vector<item::Recipe> recipes) {
  unlocked_recipes_ = std::move(recipes);
}

CraftingDisplay::CraftingDisplay(std::vector<item::Recipe> recipes,
                                 std::string title, entity::Player* player,
                                 bool is_personal)
    : Display(false, true),
      player_(player),
      title_(std::move(title)),
      is_personal_(is_personal),
      available_recipes_(std::move(recipes)) {
  active_instance_ = this;
  RefreshDisplayRecipes();
}

CraftingDisplay::~CraftingDisplay() {
  if (active_instance_ == this) active_instance_ = nullptr;
}

void CraftingDisplay::RefreshDisplayRecipes() {
  int32_t prev_sel = menus_.empty() ? 0 : menus_[0]->GetSelection();
  shown_recipes_.clear();
  for (auto& r : available_recipes_) {
    if (IsRecipeUnlocked(r)) {
      if (player_) r.CheckCanCraft(*player_);
      shown_recipes_.push_back(r);
    }
  }
  std::stable_sort(shown_recipes_.begin(), shown_recipes_.end(),
                   [](const item::Recipe& a, const item::Recipe& b) {
                     return a.GetCanCraft() && !b.GetCanCraft();
                   });

  menus_.clear();

  Menu::Builder rb(true, 0, gfx::RelPos::kLeft);
  rb.SetPositioning({9, 9}, gfx::RelPos::kBottomRight)
      .SetDisplayLength(9)
      .SetSelectable(true)
      .SetScrollPolicies(1.0f, false)
      .SetSearcherBar(true)
      .SetTitle(title_);
  for (const auto& r : shown_recipes_) {
    rb.AddEntry(std::make_unique<RecipeEntry>(&r));
  }
  if (!shown_recipes_.empty()) {
    rb.SetSelection(std::clamp<int32_t>(
        prev_sel, 0, static_cast<int32_t>(shown_recipes_.size()) - 1));
  }
  auto recipe_menu = rb.CreateMenu();
  const gfx::Rectangle r_bounds = recipe_menu->GetBounds();
  menus_.push_back(std::move(recipe_menu));

  menus_.push_back(nullptr);
  menus_.push_back(nullptr);
  RefreshData();
  osk_ = CreateHiddenOsk();
  (void)r_bounds;
}

void CraftingDisplay::RefreshCraftabilityInPlace() {
  if (!player_) return;
  for (auto& r : shown_recipes_) {
    r.CheckCanCraft(*player_);
  }
}

int32_t CraftingDisplay::GetCurItemCount() const {
  if (!player_ || shown_recipes_.empty() || menus_.empty()) return 0;
  const int32_t sel = std::clamp<int32_t>(
      menus_[0]->GetSelection(), 0,
      static_cast<int32_t>(shown_recipes_.size()) - 1);
  auto prod = shown_recipes_[sel].GetProduct();
  return prod ? player_->GetInventory().Count(*prod) : 0;
}

std::vector<std::unique_ptr<ListEntry>> CraftingDisplay::GetCurItemCosts()
    const {
  std::vector<std::unique_ptr<ListEntry>> out;
  if (!player_ || shown_recipes_.empty() || menus_.empty()) return out;
  const int32_t sel = std::clamp<int32_t>(
      menus_[0]->GetSelection(), 0,
      static_cast<int32_t>(shown_recipes_.size()) - 1);
  for (const auto& [name, req_cnt] : shown_recipes_[sel].Costs()) {
    auto cost_item = item::Items::Get(name, true);
    if (!cost_item) continue;
    const int32_t have = player_->GetInventory().Count(*cost_item);
    out.push_back(std::make_unique<ItemListingEntry>(
        std::shared_ptr<item::Item>(std::move(cost_item)),
        std::to_string(have) + "/" + std::to_string(req_cnt)));
  }
  return out;
}

void CraftingDisplay::RefreshData() {
  if (menus_.size() < 3 || !menus_[0]) return;
  const gfx::Rectangle rb = menus_[0]->GetBounds();

  std::unique_ptr<ListEntry> have_entry;
  if (!shown_recipes_.empty()) {
    const int32_t sel = std::clamp<int32_t>(
        menus_[0]->GetSelection(), 0,
        static_cast<int32_t>(shown_recipes_.size()) - 1);
    auto prod = shown_recipes_[sel].GetProduct();
    have_entry = std::make_unique<ItemListingEntry>(
        std::shared_ptr<item::Item>(std::move(prod)),
        std::to_string(GetCurItemCount()));
  }

  Menu::Builder have_b(true, 0, gfx::RelPos::kLeft);
  have_b.SetTitle("minicraft.displays.crafting.container_title.have")
      .SetTitlePos(gfx::RelPos::kTopLeft)
      .SetPositioning({rb.Right() + 8, rb.Top()}, gfx::RelPos::kBottomRight);
  if (have_entry) have_b.AddEntry(std::move(have_entry));
  menus_[1] = have_b.CreateMenu();

  Menu::Builder cost_b(true, 0, gfx::RelPos::kLeft);
  cost_b.SetTitle("minicraft.displays.crafting.container_title.cost")
      .SetTitlePos(gfx::RelPos::kTopLeft)
      .SetPositioning({menus_[1]->GetBounds().Left(), rb.Bottom()},
                      gfx::RelPos::kTopRight)
      .SetEntries(GetCurItemCosts());
  menus_[2] = cost_b.CreateMenu();
}

void CraftingDisplay::Tick(io::InputHandler& input) {
  if (menus_.empty() || !menus_[0]) return;
  const int32_t prev_sel = menus_[0]->GetSelection();
  bool main_method = false;

  if (!osk_ || (!menus_[0]->IsSearcherBarActive() && !osk_->IsVisible())) {
    if (input.InputPressed("menu") ||
        (is_personal_ && input.InputPressed("craft"))) {
      DisplayHost::ExitDisplay();
      return;
    }
    Display::Tick(input);
    main_method = true;
  } else {
    TickSearcherMenuWithOsk(input, menus_[0].get(), osk_.get());
    if (input.InputPressed("menu") ||
        (is_personal_ && input.InputPressed("craft"))) {
      DisplayHost::ExitDisplay();
      return;
    }
  }

  if (menus_[0]->GetSelection() != prev_sel) {
    RefreshData();
  }

  if (main_method || (osk_ && !osk_->IsVisible())) {
    if ((input.InputPressed("select") || input.InputPressed("attack")) &&
        !shown_recipes_.empty()) {
      const int32_t sel = menus_[0]->GetSelection();
      if (sel >= 0 && sel < static_cast<int32_t>(shown_recipes_.size())) {
        item::Recipe& selected = shown_recipes_[sel];
        if (player_ && selected.CheckCanCraft(*player_)) {
          if (selected.Craft(player_)) {
            io::Sound::Play("craft");
            auto prod = selected.GetProduct();
            CheckCraftingAchievements(prod.get());
            RefreshData();
            RefreshCraftabilityInPlace();
          }
        }
      }
    }
  }
}

void CraftingDisplay::Render(gfx::Screen* screen) const {
  Display::Render(screen);
  if (osk_) osk_->Render(screen);
}

// ---------------------------------------------------------------------------
// PlayerInvDisplay
// ---------------------------------------------------------------------------
PlayerInvDisplay::PlayerInvDisplay(entity::Player* player)
    : Display(false, true),
      player_(player),
      creative_mode_(game::GameState::Instance().IsCreativeMode()) {
  if (creative_mode_) {
    creative_inv_ = std::shared_ptr<item::Inventory>(
        item::Items::CreateCreativeModeInventory());
  }
  RebuildMenus();
  if (creative_mode_ && player_ && player_->GetInventory().InvSize() == 0) {
    OnSelectionChange(0, 1);
  }
  osk_ = CreateHiddenOsk();
}

std::string PlayerInvDisplay::CurrentDescription() const {
  if (selection_ == 0 && player_ && !menus_.empty() && menus_[0]) {
    const auto& inv = player_->GetInventory();
    const int32_t sel = menus_[0]->GetSelection();
    if (inv.InvSize() > 0 && sel >= 0 &&
        static_cast<size_t>(sel) < inv.InvSize()) {
      return inv.Get(static_cast<size_t>(sel))->GetDescription();
    }
  } else if (creative_mode_ && creative_inv_ && menus_.size() > 1 && menus_[1]) {
    const int32_t sel = menus_[1]->GetSelection();
    if (creative_inv_->InvSize() > 0 && sel >= 0 &&
        static_cast<size_t>(sel) < creative_inv_->InvSize()) {
      return creative_inv_->Get(static_cast<size_t>(sel))->GetDescription();
    }
  }
  return "";
}

void PlayerInvDisplay::UpdateDescriptionMenu() {
  const size_t desc_idx = creative_mode_ ? 2 : 1;
  if (menus_.size() <= desc_idx || menus_.empty() || !menus_[0]) return;
  const std::string desc = CurrentDescription();
  Menu::Builder desc_b(true, 3, gfx::RelPos::kTopLeft);
  if (creative_mode_ && selection_ == 1 && menus_[1]) {
    desc_b.SetPositioning(
        {gfx::Screen::w - kInvPadding, menus_[1]->GetBounds().Bottom() + 8},
        gfx::RelPos::kBottomLeft);
  } else {
    desc_b.SetPositioning(
        {kInvPadding, menus_[0]->GetBounds().Bottom() + 8},
        gfx::RelPos::kBottomRight);
  }
  desc_b.SetSelectable(false)
      .SetShouldRender(!desc.empty())
      .SetEntries(StringEntry::UseLines({desc}, gfx::color::kWhite, false));
  menus_[desc_idx] = desc_b.CreateMenu();
}

void PlayerInvDisplay::RebuildMenus() {
  const int32_t sel0 = (!menus_.empty() && menus_[0]) ? menus_[0]->GetSelection() : 0;
  const int32_t sel1 = (menus_.size() > 1 && menus_[1]) ? menus_[1]->GetSelection() : 0;
  menus_.clear();
  if (!player_) return;

  auto inv_menu = BuildInventoryMenu(player_, player_->GetInventory(),
                                     "minicraft.display.menus.inventory",
                                     gfx::RelPos::kLeft, sel0);
  const gfx::Rectangle inv_bounds = inv_menu->GetBounds();
  menus_.push_back(std::move(inv_menu));

  if (creative_mode_ && creative_inv_) {
    auto c_menu = BuildInventoryMenu(
        player_, *creative_inv_,
        "minicraft.displays.player_inv.container_title.items",
        gfx::RelPos::kRight, sel1);
    c_menu->Translate(inv_bounds.Width() + kInvPadding, 0);
    c_menu->should_render = (selection_ != 0);
    menus_.push_back(std::move(c_menu));
  }

  menus_.push_back(nullptr);
  if (selection_ == 1 && menus_.size() >= 2) {
    const int32_t shift =
        (gfx::Screen::w - kInvPadding) - menus_[1]->GetBounds().Right();
    menus_[0]->Translate(shift, 0);
    menus_[1]->Translate(shift, 0);
  }
  UpdateDescriptionMenu();
}

void PlayerInvDisplay::OnSelectionChange(int32_t old_sel, int32_t new_sel) {
  Display::OnSelectionChange(old_sel, new_sel);
  if (!creative_mode_ || menus_.size() < 2) return;
  if (new_sel == 2) {
    selection_ = old_sel;
    return;
  }
  menus_[1]->should_render = (selection_ != 0);
  if (old_sel != new_sel) {
    int32_t shift = 0;
    if (new_sel == 0) shift = kInvPadding - menus_[0]->GetBounds().Left();
    if (new_sel == 1) {
      shift = (gfx::Screen::w - kInvPadding) - menus_[1]->GetBounds().Right();
    }
    menus_[0]->Translate(shift, 0);
    menus_[1]->Translate(shift, 0);
  }
  UpdateDescriptionMenu();
}

void PlayerInvDisplay::Tick(io::InputHandler& input) {
  if (!player_ || menus_.empty()) return;
  Menu* cur_menu =
      (selection_ >= 0 && static_cast<size_t>(selection_) < menus_.size())
          ? menus_[selection_].get()
          : nullptr;
  if (!cur_menu) return;

  const int32_t prev_item_sel = cur_menu->GetSelection();
  bool main_method = false;

  if (!osk_ || (!cur_menu->IsSearcherBarActive() && !osk_->IsVisible())) {
    if (input.InputPressed("menu")) {
      DisplayHost::ExitDisplay();
      return;
    }
    Display::Tick(input);
    main_method = true;
  } else {
    TickSearcherMenuWithOsk(input, cur_menu, osk_.get());
    if (input.InputPressed("menu")) {
      DisplayHost::ExitDisplay();
      return;
    }
  }

  if (menus_.empty()) return;
  cur_menu =
      (selection_ >= 0 && static_cast<size_t>(selection_) < menus_.size())
          ? menus_[selection_].get()
          : nullptr;
  if (!cur_menu) return;

  if (cur_menu->GetSelection() != prev_item_sel) {
    UpdateDescriptionMenu();
  }

  if (!main_method && osk_ && osk_->IsVisible()) return;

  if (selection_ == 0) {
    if (HandleInventoryDrop(input, player_, &player_->GetInventory(),
                            menus_[0].get(), false)) {
      RebuildMenus();
      return;
    }
    if (input.InputPressed("attack") && menus_[0]->GetNumOptions() > 0) {
      const int32_t sel = menus_[0]->GetSelection();
      if (sel >= 0 &&
          static_cast<size_t>(sel) < player_->GetInventory().InvSize()) {
        auto removed = player_->GetInventory().Remove(static_cast<size_t>(sel));
        player_->active_item = std::shared_ptr<item::Item>(std::move(removed));
        DisplayHost::ExitDisplay();
        return;
      }
    }
    if (creative_mode_) {
      const bool del_all =
          input.InputPressed("SHIFT-D") ||
          input.ButtonPressed(platform::GamepadButton::kY);
      const bool del_one =
          input.InputPressed("D") ||
          input.ButtonPressed(platform::GamepadButton::kX);
      if (del_all || del_one) {
        const int32_t sel = menus_[0]->GetSelection();
        if (sel >= 0 &&
            static_cast<size_t>(sel) < player_->GetInventory().InvSize()) {
          if (del_all) {
            player_->GetInventory().Remove(static_cast<size_t>(sel));
          } else {
            auto* it = player_->GetInventory().Get(static_cast<size_t>(sel));
            if (auto* st = dynamic_cast<item::StackableItem*>(it);
                st && st->count > 1) {
              st->count--;
            } else {
              player_->GetInventory().Remove(static_cast<size_t>(sel));
            }
          }
          RebuildMenus();
        }
      }
    }
  } else if (creative_mode_ && creative_inv_ && selection_ == 1) {
    HandleInventoryDrop(input, player_, creative_inv_.get(), menus_[1].get(),
                        true);
    const bool shift_sel =
        input.InputPressed("SHIFT-SELECT") ||
        (input.InputDown("SHIFT") && input.InputPressed("select"));
    const bool single_sel = input.InputPressed("select");
    if (shift_sel || single_sel) {
      const int32_t sel = menus_[1]->GetSelection();
      if (sel >= 0 && static_cast<size_t>(sel) < creative_inv_->InvSize()) {
        auto copy = creative_inv_->Get(static_cast<size_t>(sel))->Clone();
        if (auto* st = dynamic_cast<item::StackableItem*>(copy.get());
            st && shift_sel) {
          st->count = st->max_count;
        }
        player_->GetInventory().Add(std::move(copy));
        RebuildMenus();
      }
    }
  }
}

void PlayerInvDisplay::Render(gfx::Screen* screen) const {
  if (!screen) return;
  const size_t desc_idx = creative_mode_ ? 2 : 1;
  Display::Render(screen);

  if (player_ && !menus_.empty() && menus_[0] && screen->Linker()) {
    const gfx::SheetRect* cs = counter_sheet_.GetSheet(*screen->Linker());
    if (selection_ == 0) {
      RenderCounterExpanded(screen, cs, menus_[0]->GetBounds(),
                            player_->GetInventory().InvSize(),
                            player_->GetInventory().GetMaxSlots(), false);
    } else {
      RenderCounterCompact(screen, cs, menus_[0]->GetBounds(),
                           player_->GetInventory().InvSize(),
                           player_->GetInventory().GetMaxSlots(), true);
    }
  }

  if (menus_.size() > desc_idx && menus_[desc_idx]) {
    auto* inp = DisplayHost::Instance().input;
    const std::string bar_key =
        inp ? inp->GetMapping("SEARCHER-BAR") : "SHIFT-F";
    const std::string help = util::Localization::GetLocalized(
        "minicraft.displays.player_inv.display.help", bar_key);
    const int32_t hx =
        (selection_ == 0) ? 12 : (gfx::Screen::w - 12 - gfx::Font::TextWidth(help));
    const int32_t hy = menus_[desc_idx]->GetBounds().Bottom() + 8;
    gfx::Font::Draw(help, screen, hx, hy, gfx::color::kWhite);
  }
  if (osk_) osk_->Render(screen);
}

// ---------------------------------------------------------------------------
// ContainerDisplay
// ---------------------------------------------------------------------------
ContainerDisplay::ContainerDisplay(entity::Player* player, entity::Chest* chest)
    : Display(false, true), player_(player), chest_(chest) {
  RebuildMenus();
  if (chest_ && chest_->GetInventory() &&
      chest_->GetInventory()->InvSize() == 0) {
    OnSelectionChange(1, 0);
  }
  osk_ = CreateHiddenOsk();
}

void ContainerDisplay::RebuildMenus() {
  const int32_t sel0 = (!menus_.empty() && menus_[0]) ? menus_[0]->GetSelection() : 0;
  const int32_t sel1 = (menus_.size() > 1 && menus_[1]) ? menus_[1]->GetSelection() : 0;
  menus_.clear();
  if (!player_ || !chest_ || !chest_->GetInventory()) return;

  auto p_menu = BuildInventoryMenu(player_, player_->GetInventory(),
                                   "minicraft.display.menus.inventory",
                                   gfx::RelPos::kRight, sel0);
  auto c_menu = BuildInventoryMenu(chest_, *chest_->GetInventory(),
                                   chest_->name, gfx::RelPos::kLeft, sel1);
  c_menu->Translate(p_menu->GetBounds().Width() + kInvPadding, 0);

  menus_.push_back(std::move(p_menu));
  menus_.push_back(std::move(c_menu));
  OnSelectionChange(selection_, selection_);
}

void ContainerDisplay::OnSelectionChange(int32_t old_sel, int32_t new_sel) {
  Display::OnSelectionChange(old_sel, new_sel);
  if (menus_.size() < 2) return;
  int32_t shift = 0;
  if (new_sel == 0) shift = kInvPadding - menus_[0]->GetBounds().Left();
  if (new_sel == 1) {
    shift = (gfx::Screen::w - kInvPadding) - menus_[1]->GetBounds().Right();
  }
  menus_[0]->Translate(shift, 0);
  menus_[1]->Translate(shift, 0);
}

void ContainerDisplay::Tick(io::InputHandler& input) {
  if (!chest_ || chest_->removed) {
    DisplayHost::SetDisplay(nullptr);
    return;
  }
  if (!player_ || !chest_->GetInventory() || menus_.size() < 2) return;

  Menu* cur_menu =
      (selection_ >= 0 && static_cast<size_t>(selection_) < menus_.size())
          ? menus_[selection_].get()
          : nullptr;
  if (!cur_menu) return;

  bool main_method = false;
  if (!osk_ || (!cur_menu->IsSearcherBarActive() && !osk_->IsVisible())) {
    Display::Tick(input);
    if (input.InputPressed("menu") || !chest_ || chest_->removed) {
      DisplayHost::SetDisplay(nullptr);
      return;
    }
    main_method = true;
  } else {
    TickSearcherMenuWithOsk(input, cur_menu, osk_.get());
    if (input.InputPressed("menu") || !chest_ || chest_->removed) {
      DisplayHost::SetDisplay(nullptr);
      return;
    }
  }

  if (menus_.size() < 2) return;
  cur_menu =
      (selection_ >= 0 && static_cast<size_t>(selection_) < menus_.size())
          ? menus_[selection_].get()
          : nullptr;
  if (!cur_menu) return;

  if (!main_method && osk_ && osk_->IsVisible()) return;

  item::Inventory* from =
      (selection_ == 0) ? &player_->GetInventory()
                        : chest_->GetInventory().get();
  item::Inventory* to =
      (selection_ == 0) ? chest_->GetInventory().get()
                        : &player_->GetInventory();
  entity::Entity* drop_holder =
      (selection_ == 0) ? static_cast<entity::Entity*>(player_)
                        : static_cast<entity::Entity*>(chest_);

  if (HandleInventoryDrop(input, drop_holder, from, cur_menu, false)) {
    RebuildMenus();
    return;
  }

  const bool transfer_pressed =
      input.InputPressed("attack") || input.InputPressed("SHIFT-ATTACK");
  if (transfer_pressed && cur_menu->GetNumOptions() > 0 && from->InvSize() > 0) {
    const int32_t sel = cur_menu->GetSelection();
    if (sel >= 0 && static_cast<size_t>(sel) < from->InvSize()) {
      item::Item* src = from->Get(static_cast<size_t>(sel));
      if (src) {
        auto* st = dynamic_cast<item::StackableItem*>(src);
        const bool transfer_all =
            input.InputDown("SHIFT") || !st || st->count == 1;
        auto to_item = src->Clone();
        if (st) {
          auto* tst = dynamic_cast<item::StackableItem*>(to_item.get());
          const int32_t move_cnt = transfer_all ? st->count : 1;
          if (tst) tst->count = move_cnt;
          auto leftover = to->Add(std::move(to_item));
          int32_t rem_cnt = 0;
          if (leftover) {
            if (auto* lst = dynamic_cast<item::StackableItem*>(leftover.get())) {
              rem_cnt = lst->count;
            }
          }
          const int32_t moved = move_cnt - rem_cnt;
          if (moved > 0) {
            if (moved >= st->count) {
              from->Remove(static_cast<size_t>(sel));
            } else {
              st->count -= moved;
            }
            io::Sound::Play("select");
            RebuildMenus();
          }
        } else {
          if (!to->Add(std::move(to_item))) {
            from->Remove(static_cast<size_t>(sel));
            io::Sound::Play("select");
            RebuildMenus();
          }
        }
      }
    }
  }
}

void ContainerDisplay::Render(gfx::Screen* screen) const {
  Display::Render(screen);
  if (!screen || !screen->Linker() || !player_ || !chest_ ||
      !chest_->GetInventory() || menus_.size() < 2) {
    return;
  }
  const gfx::SheetRect* cs = counter_sheet_.GetSheet(*screen->Linker());
  const auto& p_inv = player_->GetInventory();
  const auto& c_inv = *chest_->GetInventory();
  if (selection_ == 0) {
    RenderCounterExpanded(screen, cs, menus_[0]->GetBounds(), p_inv.InvSize(),
                          p_inv.GetMaxSlots(), false);
    RenderCounterCompact(screen, cs, menus_[1]->GetBounds(), c_inv.InvSize(),
                         c_inv.GetMaxSlots(), false);
  } else {
    RenderCounterCompact(screen, cs, menus_[0]->GetBounds(), p_inv.InvSize(),
                         p_inv.GetMaxSlots(), true);
    RenderCounterExpanded(screen, cs, menus_[1]->GetBounds(), c_inv.InvSize(),
                          c_inv.GetMaxSlots(), true);
  }
  if (osk_) osk_->Render(screen);
}

// ---------------------------------------------------------------------------
// SignDisplay & SignDisplayMenu
// ---------------------------------------------------------------------------
namespace {
saveload::SignMap g_sign_texts;
uint32_t g_sign_version = 0;

int32_t Utf8CharCount(std::string_view s) {
  int32_t count = 0;
  for (unsigned char b : s) {
    if ((b & 0xC0u) != 0x80u) ++count;
  }
  return count;
}

size_t Utf8ByteOffset(std::string_view s, int32_t cp_idx) {
  if (cp_idx <= 0) return 0;
  int32_t count = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0u) != 0x80u) {
      if (count == cp_idx) return i;
      ++count;
    }
  }
  return s.size();
}
}  // namespace

uint32_t SignDisplay::SignVersion() { return g_sign_version; }

void SignDisplay::ResetSignTexts() {
  g_sign_texts.clear();
  ++g_sign_version;
}

void SignDisplay::UpdateSign(int32_t level_depth, int32_t x, int32_t y,
                             std::vector<std::string> lines) {
  g_sign_texts[{level_depth, x, y}] = std::move(lines);
  ++g_sign_version;
  if (auto* signs = DisplayHost::Instance().get_signs
                        ? DisplayHost::Instance().get_signs()
                        : nullptr) {
    *signs = g_sign_texts;
  }
}

void SignDisplay::RemoveSign(int32_t level_depth, int32_t x, int32_t y) {
  g_sign_texts.erase({level_depth, x, y});
  ++g_sign_version;
  if (auto* signs = DisplayHost::Instance().get_signs
                        ? DisplayHost::Instance().get_signs()
                        : nullptr) {
    *signs = g_sign_texts;
  }
}

const std::vector<std::string>* SignDisplay::GetSign(int32_t level_depth,
                                                     int32_t x, int32_t y) {
  auto it = g_sign_texts.find({level_depth, x, y});
  return (it == g_sign_texts.end()) ? nullptr : &it->second;
}

void SignDisplay::SyncFromMap(const saveload::SignMap& map) {
  g_sign_texts = map;
  ++g_sign_version;
}

void SignDisplay::ExportToMap(saveload::SignMap* out_map) {
  if (out_map) *out_map = g_sign_texts;
}

SignDisplay::SignDisplay(int32_t level_depth, int32_t x, int32_t y)
    : Display(false, false), level_depth_(level_depth), x_(x), y_(y) {
  Menu::Builder b(true, 3, gfx::RelPos::kCenter);
  b.SetPositioning({gfx::Screen::w / 2, 6}, gfx::RelPos::kBottom)
      .SetMenuSize({8 * (kMaxTextLength + 2), 8 * (kMaxRowCount + 2)})
      .SetSelectable(false);
  menus_.push_back(b.CreateMenu());

  if (const auto* existing = GetSign(level_depth_, x_, y_)) {
    for (size_t i = 0; i < existing->size() && i < kMaxRowCount; ++i) {
      rows_[i] = (*existing)[i];
    }
  }
  cursor_x_ = 0;
  osk_ = CreateHiddenOsk();
}

void SignDisplay::UpdateCaretAnimation() {
  caret_shown_ = true;
  caret_timer_ = 120;
}

bool SignDisplay::InsertChar(char c) {
  std::string& row = rows_[cursor_y_];
  if (c == '\b') {
    if (cursor_x_ > 0) {
      const size_t start = Utf8ByteOffset(row, cursor_x_ - 1);
      const size_t end = Utf8ByteOffset(row, cursor_x_);
      row.erase(start, end - start);
      --cursor_x_;
    }
    return true;
  }
  if (c == '\n') return true;
  if (Utf8CharCount(row) < kMaxTextLength) {
    const size_t pos = Utf8ByteOffset(row, cursor_x_);
    row.insert(pos, 1, c);
    ++cursor_x_;
    return true;
  }
  return false;
}

void SignDisplay::InsertChars(std::string_view chars) {
  std::string cleaned = io::InputHandler::HandleBackspaceChars(chars, true);
  if (cleaned.empty()) return;
  UpdateCaretAnimation();
  for (size_t i = 0; i < cleaned.size();) {
    const unsigned char b = static_cast<unsigned char>(cleaned[i]);
    if (b < 0x80) {
      if (!InsertChar(cleaned[i])) break;
      ++i;
      continue;
    }
    size_t len = 1;
    if ((b & 0xE0u) == 0xC0u) len = 2;
    else if ((b & 0xF0u) == 0xE0u) len = 3;
    else if ((b & 0xF8u) == 0xF0u) len = 4;
    len = std::min(len, cleaned.size() - i);
    std::string& row = rows_[cursor_y_];
    if (Utf8CharCount(row) >= kMaxTextLength) break;
    const size_t pos = Utf8ByteOffset(row, cursor_x_);
    row.insert(pos, cleaned.substr(i, len));
    ++cursor_x_;
    i += len;
  }
}

void SignDisplay::TickEditor(io::InputHandler& input) {
  if (--caret_timer_ <= 0) {
    caret_timer_ = 30;
    caret_shown_ = !caret_shown_;
  }

  InsertChars(input.GetKeysTyped(""));

  if (input.InputPressed("PAGE-UP")) {
    cursor_y_ = 0;
    cursor_x_ = Utf8CharCount(rows_[cursor_y_]);
    UpdateCaretAnimation();
  } else if (input.InputPressed("PAGE-DOWN")) {
    cursor_y_ = kMaxRowCount - 1;
    cursor_x_ = Utf8CharCount(rows_[cursor_y_]);
    UpdateCaretAnimation();
  } else if (input.InputPressed("HOME")) {
    cursor_x_ = 0;
    UpdateCaretAnimation();
  } else if (input.InputPressed("END")) {
    cursor_x_ = Utf8CharCount(rows_[cursor_y_]);
    UpdateCaretAnimation();
  } else if (input.InputPressed("cursor-up")) {
    cursor_y_ = (cursor_y_ == 0) ? (kMaxRowCount - 1) : (cursor_y_ - 1);
    cursor_x_ = Utf8CharCount(rows_[cursor_y_]);
    UpdateCaretAnimation();
  } else if (input.InputPressed("cursor-down") ||
             input.InputPressed("ENTER")) {
    cursor_y_ = (cursor_y_ == kMaxRowCount - 1) ? 0 : (cursor_y_ + 1);
    cursor_x_ = Utf8CharCount(rows_[cursor_y_]);
    UpdateCaretAnimation();
  } else if (input.InputPressed("cursor-left")) {
    if (cursor_x_ > 0) --cursor_x_;
    UpdateCaretAnimation();
  } else if (input.InputPressed("cursor-right")) {
    if (cursor_x_ < Utf8CharCount(rows_[cursor_y_])) ++cursor_x_;
    UpdateCaretAnimation();
  } else if (input.InputPressed("CTRL-X")) {
    DisplayHost::SetClipboardText(rows_[cursor_y_]);
    rows_[cursor_y_].clear();
    cursor_x_ = 0;
    UpdateCaretAnimation();
  } else if (input.InputPressed("CTRL-C")) {
    DisplayHost::SetClipboardText(rows_[cursor_y_]);
    UpdateCaretAnimation();
  } else if (input.InputPressed("CTRL-V")) {
    InsertChars(DisplayHost::ClipboardText());
  }
}

void SignDisplay::Tick(io::InputHandler& input) {
  auto save_and_exit = [&]() {
    std::vector<std::string> out(rows_.begin(), rows_.end());
    UpdateSign(level_depth_, x_, y_, std::move(out));
    DisplayHost::ExitDisplay();
  };

  bool main_method = false;
  if (!osk_ || !osk_->IsVisible()) {
    if (input.InputPressed("exit") || input.InputPressed("SHIFT-ENTER")) {
      save_and_exit();
      return;
    }
    main_method = true;
  } else {
    const auto res = osk_->TickKeyboard(input);
    if (res == OnScreenKeyboardMenu::TickResult::kBackspaceActed ||
        (res == OnScreenKeyboardMenu::TickResult::kNone &&
         input.ButtonPressed(platform::GamepadButton::kA))) {
      TickEditor(input);
    }
    if (input.InputPressed("exit") || input.InputPressed("SHIFT-ENTER")) {
      save_and_exit();
      return;
    }
    if (input.ButtonPressed(platform::GamepadButton::kX)) {
      osk_->SetVisible(!osk_->IsVisible());
    }
  }

  if (osk_ && main_method && input.ButtonPressed(platform::GamepadButton::kX)) {
    osk_->SetVisible(!osk_->IsVisible());
  }
  if (main_method || (osk_ && !osk_->IsVisible())) {
    TickEditor(input);
  }
}

void SignDisplay::Render(gfx::Screen* screen) const {
  Display::Render(screen);
  if (!screen || menus_.empty()) return;
  const gfx::Rectangle& b = menus_[0]->GetBounds();
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized("Use SHIFT-ENTER to confirm input."),
      screen, b.Bottom() + 8, gfx::color::kGray);

  int32_t ry = b.Top() + 8;
  for (int32_t r = 0; r < kMaxRowCount; ++r) {
    gfx::Font::DrawCentered(rows_[r], screen, ry, gfx::color::kWhite);
    ry += 8;
  }

  if (caret_shown_) {
    const int32_t centered_x = b.Left() + b.Width() / 2;
    const int32_t char_count = Utf8CharCount(rows_[cursor_y_]);
    const int32_t line_w = char_count * 8;
    const int32_t line_begin = centered_x - line_w / 2;
    const int32_t cx = line_begin + cursor_x_ * 8;
    const int32_t cy = b.Top() + 8 + cursor_y_ * 8;
    if (cursor_x_ == char_count) {
      screen->DrawLineSpecial(cx, cy + 7, 0, 8);
    } else {
      screen->DrawLineSpecial(cx, cy, 1, 8);
    }
  }
  if (osk_) osk_->Render(screen);
}

SignDisplayMenu::SignDisplayMenu(int32_t level_depth, int32_t x, int32_t y)
    : level_depth_(level_depth),
      x_(x),
      y_(y),
      version_(SignDisplay::SignVersion()) {
  Menu::Builder b(true, 0, gfx::RelPos::kCenter);
  b.SetPositioning({gfx::Screen::w / 2, 6}, gfx::RelPos::kBottom)
      .SetMenuSize({8 * (SignDisplay::kMaxTextLength + 2),
                    8 * (SignDisplay::kMaxRowCount + 2)})
      .SetDisplayLength(4)
      .SetSelectable(false);
  if (const auto* lines = SignDisplay::GetSign(level_depth, x, y)) {
    for (const auto& l : *lines) {
      b.AddEntry(std::make_unique<StringEntry>(l, gfx::color::kWhite, false));
    }
  }
  menu_ = b.CreateMenu();
}

bool SignDisplayMenu::Matches(int32_t level_depth, int32_t x,
                              int32_t y) const {
  return level_depth_ == level_depth && x_ == x && y_ == y &&
         version_ == SignDisplay::SignVersion();
}

void SignDisplayMenu::Render(gfx::Screen* screen) const {
  if (menu_) menu_->Render(screen);
}

// ---------------------------------------------------------------------------
// BookDisplay
// ---------------------------------------------------------------------------
BookDisplay::BookDisplay(std::string book, bool has_title)
    : Display(false, true), has_title_(has_title) {
  constexpr int32_t kSpacing = 3;
  constexpr int32_t kMinX = 15, kMaxX = 15 + 8 * 32;
  constexpr int32_t kMinY = 8 * 5, kMaxY = 8 * 5 + (8 << 4);

  if (book.empty()) {
    book = "This book has no text.";
    has_title_ = false;
  }

  size_t pos = 0;
  while (pos <= book.size()) {
    size_t nul = book.find('\0', pos);
    std::string section = (nul == std::string::npos)
                              ? book.substr(pos)
                              : book.substr(pos, nul - pos);
    std::string rem = section;
    while (!rem.empty()) {
      auto lines =
          gfx::Font::GetLines(rem, kMaxX - kMinX, kMaxY - kMinY, kSpacing, true);
      if (lines.empty()) break;
      rem = lines.back();
      lines.pop_back();
      pages_.push_back(std::move(lines));
      if (rem.empty()) break;
    }
    if (nul == std::string::npos) break;
    pos = nul + 1;
  }
  if (pages_.empty()) pages_.push_back({"This book has no text."});

  show_page_count_ = has_title_ || pages_.size() != 1;
  page_offset_ = show_page_count_ ? 1 : 0;

  Menu::Builder page_b(true, kSpacing, gfx::RelPos::kCenter);
  page_b.SetPositioning({gfx::Screen::w / 2, 0}, gfx::RelPos::kBottom)
      .SetSelection(1)
      .AddEntry(
          std::make_unique<StringEntry>("Page", gfx::color::kBlack, false))
      .AddEntry(std::make_unique<StringEntry>(
          has_title_ ? "Title" : ("1/" + std::to_string(pages_.size())),
          gfx::color::kBlack, false));
  auto page_menu = page_b.CreateMenu();
  const int32_t header_bottom = page_menu->GetBounds().Bottom();
  if (show_page_count_) menus_.push_back(std::move(page_menu));

  for (size_t i = 0; i < pages_.size(); ++i) {
    Menu::Builder pb(true, kSpacing, gfx::RelPos::kCenter);
    pb.SetPositioning({gfx::Screen::w / 2, header_bottom + kSpacing},
                      gfx::RelPos::kBottom)
        .SetSize(kMaxX - kMinX + 16, kMaxY - kMinY + 16)
        .SetShouldRender(i == 0)
        .SetEntries(StringEntry::UseLines(pages_[i], gfx::color::kWhite,
                                          false, kMaxX - kMinX));
    menus_.push_back(pb.CreateMenu());
  }
}

void BookDisplay::TurnPage(int32_t dir) {
  const int32_t next = page_ + dir;
  if (next < 0 || next >= static_cast<int32_t>(pages_.size())) return;
  menus_[page_ + page_offset_]->should_render = false;
  page_ = next;
  if (show_page_count_ && menus_[0]) {
    menus_[0]->UpdateEntry(
        1, std::make_unique<StringEntry>(
               (page_ == 0 && has_title_)
                   ? "Title"
                   : (std::to_string(page_ + 1) + "/" +
                      std::to_string(pages_.size())),
               gfx::color::kBlack, false));
  }
  menus_[page_ + page_offset_]->should_render = true;
}

void BookDisplay::Tick(io::InputHandler& input) {
  if (input.InputPressed("menu") || input.InputPressed("exit")) {
    DisplayHost::ExitDisplay();
    return;
  }
  if (input.InputPressed("cursor-left")) TurnPage(-1);
  if (input.InputPressed("cursor-right")) TurnPage(1);
}

// ---------------------------------------------------------------------------
// InfoDisplay
// ---------------------------------------------------------------------------
std::string InfoDisplay::GetTimeString() {
  int32_t secs =
      game::GameState::Instance().tick_count / game::GameState::kNormSpeed;
  int32_t mins = secs / 60;
  const int32_t hrs = mins / 60;
  mins %= 60;
  secs %= 60;
  if (hrs > 0) {
    return std::to_string(hrs) + "h" + (mins < 10 ? "0" : "") +
           std::to_string(mins) + "m";
  }
  return std::to_string(mins) + "m " + (secs < 10 ? "0" : "") +
         std::to_string(secs) + "s";
}

InfoDisplay::InfoDisplay() : Display(false, true) {
  auto* p =
      DisplayHost::Instance().get_player ? DisplayHost::Instance().get_player()
                                         : nullptr;
  const int32_t score = p ? p->GetScore() : 0;
  auto* inp = DisplayHost::Instance().input;
  const std::string sel_key = inp ? inp->GetMapping("select") : "ENTER";
  const std::string exit_key = inp ? inp->GetMapping("exit") : "ESCAPE";

  Menu::Builder b(true, 4, gfx::RelPos::kLeft);
  b.SetTitle("minicraft.displays.info.title")
      .SetTitlePos(gfx::RelPos::kTopLeft)
      .SetPositioning({8, 8}, gfx::RelPos::kBottomRight)
      .SetEntries(StringEntry::UseLines(
          {"----------------------------",
           util::Localization::GetLocalized("minicraft.displays.info.display.time",
                                            GetTimeString()),
           util::Localization::GetLocalized(
               "minicraft.displays.info.display.score", score),
           "----------------------------",
           util::Localization::GetLocalized(
               "minicraft.displays.info.display.exit_help", sel_key, exit_key)},
          gfx::color::kWhite, false));
  menus_.push_back(b.CreateMenu());
}

void InfoDisplay::Tick(io::InputHandler& input) {
  if (input.InputPressed("select") || input.InputPressed("exit")) {
    DisplayHost::ExitDisplay();
  }
}

// ---------------------------------------------------------------------------
// PlayerDeathDisplay
// ---------------------------------------------------------------------------
PlayerDeathDisplay::PlayerDeathDisplay() : Display(false, false) {
  auto* p =
      DisplayHost::Instance().get_player ? DisplayHost::Instance().get_player()
                                         : nullptr;
  const int32_t score = p ? p->GetScore() : 0;

  Menu::Builder b(true, 0, gfx::RelPos::kLeft);
  b.SetPositioning({8, 24}, gfx::RelPos::kBottomRight)
      .SetTitle("minicraft.displays.player_death.title")
      .SetTitlePos(gfx::RelPos::kTopLeft);
  b.AddEntry(std::make_unique<StringEntry>(
      util::Localization::GetLocalized(
          "minicraft.displays.player_death.display.time",
          InfoDisplay::GetTimeString()),
      gfx::color::kWhite, false));
  b.AddEntry(std::make_unique<StringEntry>(
      util::Localization::GetLocalized(
          "minicraft.displays.player_death.display.score", score),
      gfx::color::kWhite, false));
  b.AddEntry(std::make_unique<BlankEntry>());

  if (!game::GameState::Instance().IsHardcoreMode()) {
    b.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.player_death.respawn", [] {
          if (DisplayHost::Instance().respawn_player) {
            DisplayHost::Instance().respawn_player();
          }
          DisplayHost::SetDisplay(nullptr);
        }));
  }
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.displays.player_death.save_quit", [] {
        if (DisplayHost::Instance().save_world) {
          DisplayHost::Instance().save_world();
        }
        if (DisplayHost::Instance().exit_world) {
          DisplayHost::Instance().exit_world();
        }
        DisplayHost::SetDisplay(std::make_unique<TitleDisplay>());
      }));
  if (!game::GameState::Instance().IsHardcoreMode()) {
    b.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.player_death.quit", [] {
          if (DisplayHost::Instance().exit_world) {
            DisplayHost::Instance().exit_world();
          }
          DisplayHost::SetDisplay(std::make_unique<TitleDisplay>());
        }));
  }
  menus_.push_back(b.CreateMenu());
}

// ---------------------------------------------------------------------------
// EndGameDisplay
// ---------------------------------------------------------------------------
EndGameDisplay::EndGameDisplay() : Display(false, false) {
  auto* p =
      DisplayHost::Instance().get_player ? DisplayHost::Instance().get_player()
                                         : nullptr;
  const int32_t base_score = p ? p->GetScore() : 0;
  final_score_ = base_score;
  if (p) {
    static constexpr const char* kScoredItems[] = {
        "Cloth", "Slime", "Bone", "Arrow", "Gunpowder", "Antidious"};
    for (const char* name : kScoredItems) {
      auto it = item::Items::Get(name, true);
      if (it) {
        const int32_t cnt = p->GetInventory().Count(*it);
        final_score_ += cnt * (util::GlobalRandom().NextInt(2) + 1) * 10;
      }
    }
  }

  Menu::Builder b(true, 0, gfx::RelPos::kLeft);
  b.AddEntry(std::make_unique<StringEntry>(
      util::Localization::GetLocalized(
          "minicraft.displays.end_game.display.player_score", base_score),
      gfx::color::kWhite, false));
  b.AddEntry(std::make_unique<StringEntry>(
      "minicraft.displays.end_game.display.bonuses", gfx::color::kYellow,
      true));
  b.AddEntry(std::make_unique<StringEntry>(
      util::Localization::GetLocalized(
          "minicraft.displays.end_game.display.final_score", final_score_),
      gfx::color::kWhite, false));

  // Add any scoretime unlocks and persist preferences/unlocks.
  if (auto* st = io::Settings::GetEntry("scoretime")) {
    const int32_t score_time = io::Settings::GetInt("scoretime");
    auto try_unlock = [&](int32_t min_val) {
      for (size_t i = 0; i < st->int_options.size(); ++i) {
        if (st->int_options[i] == min_val) {
          if (i < st->option_visible.size() && !st->option_visible[i]) {
            st->option_visible[i] = true;
            b.AddEntry(std::make_unique<StringEntry>(
                util::Localization::GetLocalized(
                    "minicraft.displays.end_game.display.unlocked", min_val),
                gfx::color::kWhite, false));
          }
          break;
        }
      }
    };
    if (score_time == 20 && final_score_ > 1000) try_unlock(10);
    if (score_time == 60 && final_score_ > 100000) try_unlock(120);
  }
  if (DisplayHost::Instance().save_prefs) {
    DisplayHost::Instance().save_prefs();
  }

  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.displays.end_game.exit", [] {
        if (DisplayHost::Instance().exit_world) {
          DisplayHost::Instance().exit_world();
        }
        DisplayHost::SetDisplay(std::make_unique<TitleDisplay>());
      }));
  menus_.push_back(b.CreateMenu());
}

void EndGameDisplay::Tick(io::InputHandler& input) {
  if (display_timer_ > 0) {
    --display_timer_;
    return;
  }
  if (input_delay_ > 0) {
    --input_delay_;
    return;
  }
  Display::Tick(input);
}

void EndGameDisplay::Render(gfx::Screen* screen) const {
  if (display_timer_ <= 0) Display::Render(screen);
}

// ---------------------------------------------------------------------------
// LevelTransitionDisplay
// ---------------------------------------------------------------------------
LevelTransitionDisplay::LevelTransitionDisplay(int32_t dir)
    : Display(false, false), dir_(dir) {}

void LevelTransitionDisplay::Tick(io::InputHandler& /*input*/) {
  ++time_;
  if (time_ == kDuration / 2 && DisplayHost::Instance().change_level) {
    DisplayHost::Instance().change_level(dir_);
  }
  if (time_ >= kDuration) {
    DisplayHost::SetDisplay(nullptr);
  }
}

void LevelTransitionDisplay::Render(gfx::Screen* screen) const {
  if (!screen || !screen->Linker()) return;
  const gfx::SheetRect* sheet = hud_sheet_.GetSheet(*screen->Linker());
  if (!sheet) return;
  const int32_t cols = gfx::Screen::w / 8 + 1;
  const int32_t rows = gfx::Screen::h / 8 + 1;
  for (int32_t x = 0; x < cols; ++x) {
    for (int32_t y = 0; y < rows; ++y) {
      const int32_t dd = (y + (x % 2) * 2 + x / 3) - time_ * 2;
      if (dd < 0 && dd > -30) {
        if (dir_ > 0) {
          screen->Render(x * 8, y * 8, 5, 2, 0, sheet);
        } else {
          screen->Render(x * 8, gfx::Screen::h - y * 8 - 8, 5, 2, 0, sheet);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// QuestsDisplay & TutorialDisplayHandler
// ---------------------------------------------------------------------------
QuestsDisplay* QuestsDisplay::active_instance_ = nullptr;

QuestsDisplay::QuestsDisplay() : Display(true, true) {
  active_instance_ = this;
  ReloadEntries();

  menus_.clear();
  Menu::Builder list_b(false, 1, gfx::RelPos::kCenter);
  list_b.SetPositioning({gfx::Screen::w / 2, gfx::Screen::h / 2 - 20},
                        gfx::RelPos::kCenter)
      .SetDisplayLength(5)
      .SetSelectable(true);
  menus_.push_back(list_b.CreateMenu());

  const int32_t unlocked_len = static_cast<int32_t>(
      util::Localization::GetLocalized(
          "minicraft.displays.quests.display.header.unlocked")
          .size());
  Menu::Builder unl_hdr(false, 0, gfx::RelPos::kLeft);
  unl_hdr.SetPositioning({gfx::Screen::w / 2 - 8 * (2 + unlocked_len), 30},
                         gfx::RelPos::kRight)
      .SetSelectable(false)
      .AddEntry(std::make_unique<StringEntry>(
          "minicraft.displays.quests.display.header.unlocked", gfx::color::kGray,
          true));
  menus_.push_back(unl_hdr.CreateMenu());

  Menu::Builder cmp_hdr(false, 0, gfx::RelPos::kLeft);
  cmp_hdr.SetPositioning({gfx::Screen::w / 2 + 8 * 2, 30}, gfx::RelPos::kRight)
      .SetSelectable(false)
      .AddEntry(std::make_unique<StringEntry>(
          "minicraft.displays.quests.display.header.completed",
          gfx::color::kGray, true));
  menus_.push_back(cmp_hdr.CreateMenu());

  Menu::Builder desc_b(false, 0, gfx::RelPos::kCenter);
  desc_b.SetPositioning({gfx::Screen::w / 2, gfx::Screen::h / 2 + 35},
                        gfx::RelPos::kCenter)
      .SetSelectable(false)
      .SetEntries(StringEntry::UseLines(
          {"minicraft.displays.quests.display.no_quest_desc"},
          gfx::color::kWhite, true));
  menus_.push_back(desc_b.CreateMenu());

  std::string quests_str;
  if (const auto* qe = io::Settings::GetEntry("quests")) {
    quests_str = util::Localization::GetLocalized(qe->loc_name) + ": " +
                 util::Localization::GetLocalized(qe->DisplayValue());
  }
  Menu::Builder opt_b(false, 0, gfx::RelPos::kCenter);
  opt_b.SetPositioning({gfx::Screen::w / 2, 10}, gfx::RelPos::kCenter)
      .SetSelectable(false)
      .AddEntry(
          std::make_unique<StringEntry>(quests_str, gfx::color::kWhite, false));
  menus_.push_back(opt_b.CreateMenu());

  UpdateEntries();
}

QuestsDisplay::~QuestsDisplay() {
  if (active_instance_ == this) active_instance_ = nullptr;
}

void QuestsDisplay::RefreshIfOpen() {
  if (active_instance_ && DisplayHost::GetDisplay() == active_instance_) {
    active_instance_->ReloadEntries();
    active_instance_->UpdateEntries();
  }
}

void QuestsDisplay::ReloadEntries() {
  unlocked_series_.clear();
  completed_series_.clear();
  const auto& series_list =
      util::AdvancementSystem::Instance().QuestSeriesList();
  for (const auto& qs : series_list) {
    if (!qs) continue;
    if (qs->completed) completed_series_.push_back(qs.get());
    if (qs->unlocked) unlocked_series_.push_back(qs.get());
  }
}

void QuestsDisplay::UpdateEntries() {
  if (menus_.size() < 4 || !menus_[0]) return;
  const auto& active_vec =
      (selected_tab_ == 0) ? unlocked_series_ : completed_series_;

  std::vector<std::unique_ptr<ListEntry>> entries;
  entries.reserve(active_vec.size());
  for (const util::QuestSeries* qs : active_vec) {
    const int32_t col = qs->completed
                            ? gfx::color::kGreen
                            : (qs->unlocked ? gfx::color::kWhite
                                            : gfx::color::kGray);
    entries.push_back(std::make_unique<SelectEntry>(
        qs->key,
        [qs]() {
          DisplayHost::SetDisplay(
              std::make_unique<SeriesInformationDisplay>(qs));
        },
        true, [col](bool) { return col; }));
  }

  prev_selections_[selected_tab_] = menus_[0]->GetSelection();
  menus_[0]->SetEntries(std::move(entries));
  const int32_t other_tab = 1 - selected_tab_;
  menus_[0]->SetSelection(prev_selections_[other_tab]);
  prev_selections_[other_tab] = menus_[0]->GetSelection();

  static constexpr const char* kHdrKeys[2] = {
      "minicraft.displays.quests.display.header.unlocked",
      "minicraft.displays.quests.display.header.completed",
  };
  for (int32_t i = 0; i < 2; ++i) {
    if (menus_[i + 1]) {
      menus_[i + 1]->UpdateEntry(
          0, std::make_unique<StringEntry>(
                 kHdrKeys[i],
                 (i == selected_tab_) ? gfx::color::kWhite : gfx::color::kGray,
                 true));
    }
  }

  if (!active_vec.empty()) {
    const int32_t sel = std::clamp<int32_t>(
        menus_[0]->GetSelection(), 0,
        static_cast<int32_t>(active_vec.size()) - 1);
    menus_[3]->SetEntries(StringEntry::UseLines({active_vec[sel]->description},
                                                gfx::color::kWhite, true));
  } else {
    menus_[3]->SetEntries(StringEntry::UseLines(
        {"minicraft.displays.quests.display.no_quest_desc"}, gfx::color::kWhite,
        true));
  }
}

void QuestsDisplay::Tick(io::InputHandler& input) {
  Display::Tick(input);
  if (input.InputPressed("cursor-left") && selected_tab_ > 0) {
    --selected_tab_;
    UpdateEntries();
  }
  if (input.InputPressed("cursor-right") && selected_tab_ < 1) {
    ++selected_tab_;
    UpdateEntries();
  }
  if (menus_.size() < 4 || !menus_[0] || !menus_[3]) return;
  const auto& active_vec =
      (selected_tab_ == 0) ? unlocked_series_ : completed_series_;
  if (!active_vec.empty() && menus_[0]->GetCurEntry() != nullptr) {
    const int32_t sel = std::clamp<int32_t>(
        menus_[0]->GetSelection(), 0,
        static_cast<int32_t>(active_vec.size()) - 1);
    menus_[3]->SetEntries(StringEntry::UseLines({active_vec[sel]->description},
                                                gfx::color::kWhite, true));
  } else {
    menus_[3]->SetEntries(StringEntry::UseLines(
        {"minicraft.displays.quests.display.no_quest_desc"}, gfx::color::kWhite,
        true));
  }
}

void QuestsDisplay::Render(gfx::Screen* screen) const { Display::Render(screen); }

QuestsDisplay::SeriesInformationDisplay::SeriesInformationDisplay(
    const util::QuestSeries* series)
    : Display(false, true) {
  const std::string title =
      series ? util::Localization::GetLocalized(series->key) : "";
  Menu::Builder title_b(true, 0, gfx::RelPos::kCenter);
  title_b.SetPositioning({gfx::Screen::w / 2, 5}, gfx::RelPos::kBottom)
      .SetSelectable(false)
      .AddEntry(std::make_unique<StringEntry>(title, gfx::color::kWhite, false));
  menus_.push_back(title_b.CreateMenu());

  std::vector<std::unique_ptr<ListEntry>> entries;
  if (series) {
    const char* state_key =
        series->completed
            ? "minicraft.displays.quests.quest_info.display.status.completed"
            : (series->unlocked
                   ? "minicraft.displays.quests.quest_info.display.status.unlocked"
                   : "minicraft.displays.quests.quest_info.display.status.locked");
    const int32_t state_col =
        series->completed
            ? gfx::color::kGreen
            : (series->unlocked ? gfx::color::kWhite : gfx::color::kGray);
    entries.push_back(std::make_unique<StringEntry>(
        util::Localization::GetLocalized(
            "minicraft.displays.quests.quest_info.display.status",
            util::Localization::GetLocalized(state_key)),
        state_col, false));

    int32_t done_cnt = 0;
    int32_t ongoing_cnt = 0;
    for (const auto& [qk, q] : series->quests) {
      if (!q) continue;
      if (q->completed) ++done_cnt;
      if (q->IsDisplayableAtStatus()) ++ongoing_cnt;
    }
    entries.push_back(std::make_unique<StringEntry>(
        util::Localization::GetLocalized(
            "minicraft.displays.quests.quest_info.display.quests_completed_count",
            done_cnt),
        gfx::color::kWhite, false));

    auto desc_lines = StringEntry::UseLines(
        {util::Localization::GetLocalized(
            "minicraft.displays.quests.quest_info.display.description",
            util::Localization::GetLocalized(series->description))},
        gfx::color::kWhite, false);
    for (auto& e : desc_lines) entries.push_back(std::move(e));

    entries.push_back(std::make_unique<StringEntry>(
        util::Localization::GetLocalized(
            "minicraft.displays.quests.quest_info.display.ongoing_quests",
            ongoing_cnt),
        gfx::color::kWhite, false));
    entries.push_back(std::make_unique<BlankEntry>());
    entries.push_back(std::make_unique<SelectEntry>(
        "minicraft.displays.quests.quest_info.view_quests", [series]() {
          DisplayHost::SetDisplay(
              std::make_unique<SeriesQuestViewerDisplay>(series));
        }));
  }

  Menu::Builder body_b(true, 2, gfx::RelPos::kCenter);
  body_b.SetPositioning({gfx::Screen::w / 2, 30}, gfx::RelPos::kBottom)
      .SetSelectable(true)
      .SetEntries(std::move(entries));
  menus_.push_back(body_b.CreateMenu());
  selection_ = 1;
}

QuestsDisplay::SeriesQuestViewerDisplay::SeriesQuestViewerDisplay(
    const util::QuestSeries* series)
    : Display(false, true) {
  constexpr int32_t kEntryPadding = 3;
  constexpr int32_t kEntryGap = 5;

  Menu::Builder hdr_b(true, 0, gfx::RelPos::kCenter);
  hdr_b.SetPositioning({gfx::Screen::w / 2, 6}, gfx::RelPos::kBottom)
      .SetEntries(StringEntry::UseLines(
          {"minicraft.displays.quests", series ? series->key : ""},
          gfx::color::kWhite, true));
  menus_.push_back(hdr_b.CreateMenu());

  Menu::Builder box_b(true, 0, gfx::RelPos::kCenter);
  box_b.SetPositioning({gfx::Screen::w / 2, 40}, gfx::RelPos::kBottom)
      .SetSize(gfx::Screen::w - 16, gfx::Screen::h - 60);
  auto box = box_b.CreateMenu();
  const gfx::Rectangle mb = box->GetBounds();
  raster_x_ = mb.Left() + 8;
  raster_y_ = mb.Top() + 8;
  raster_w_ = std::max<int32_t>(1, mb.Width() - 16);
  raster_h_ = std::max<int32_t>(1, mb.Height() - 16);
  menus_.push_back(std::move(box));

  if (series) {
    std::vector<const util::Quest*> unlocked;
    for (const auto& qk : series->quest_order) {
      auto it = series->quests.find(qk);
      if (it != series->quests.end() && it->second && it->second->unlocked) {
        unlocked.push_back(it->second.get());
      }
    }
    if (!unlocked.empty()) {
      std::vector<QuestNode> top_row;
      std::vector<QuestNode> pending;
      for (const util::Quest* q : unlocked) {
        const util::Quest* p = q->Parent();
        if (!p || !p->unlocked) {
          top_row.push_back({q, nullptr, {}});
        } else {
          pending.push_back({q, p, {}});
        }
      }
      rows_.push_back(std::move(top_row));

      while (!pending.empty()) {
        std::vector<QuestNode> next_row;
        std::vector<QuestNode> still_pending;
        for (const auto& node : pending) {
          bool parent_found = false;
          for (size_t r = 0; r < rows_.size(); ++r) {
            for (const auto& candidate : rows_[r]) {
              if (candidate.quest == node.parent) {
                parent_found = true;
                if (r + 1 < rows_.size()) {
                  rows_[r + 1].push_back(node);
                } else {
                  next_row.push_back(node);
                }
                break;
              }
            }
            if (parent_found) break;
          }
          if (!parent_found) still_pending.push_back(node);
        }
        if (!next_row.empty()) {
          rows_.push_back(std::move(next_row));
        } else if (still_pending.size() == pending.size()) {
          // Break cycle or orphan edge gracefully.
          rows_.push_back(std::move(still_pending));
          break;
        }
        pending = std::move(still_pending);
      }

      const int32_t h = gfx::Font::TextHeight();
      for (size_t i = 0; i < rows_.size(); ++i) {
        for (size_t j = 0; j < rows_[i].size(); ++j) {
          const int32_t w = gfx::Font::TextWidth(
              util::Localization::GetLocalized(rows_[i][j].quest->key));
          gfx::Rectangle rec(kEntryGap, kEntryGap, kEntryPadding * 2 + w,
                             kEntryPadding * 2 + h, 0);
          if (j > 0) rec.Translate(rows_[i][j - 1].bounds.Right() + kEntryGap, 0);
          if (i > 0 && !rows_[i - 1].empty()) {
            rec.Translate(0, rows_[i - 1][0].bounds.Bottom() + kEntryGap * 2);
          }
          rows_[i][j].bounds = rec;
        }
      }
    }
  }
}

void QuestsDisplay::SeriesQuestViewerDisplay::ScrollIfNeeded() {
  constexpr int32_t kEntryGap = 5;
  if (rows_.empty() || cursor_y_ < 0 ||
      cursor_y_ >= static_cast<int32_t>(rows_.size()) ||
      rows_[cursor_y_].empty()) {
    return;
  }
  const gfx::Rectangle& r = rows_[cursor_y_][cursor_x_].bounds;
  if (x_scroll_ > r.Left() - kEntryGap) x_scroll_ = r.Left() - kEntryGap;
  else if (x_scroll_ + raster_w_ < r.Right() + kEntryGap)
    x_scroll_ = r.Right() + kEntryGap - raster_w_;

  if (y_scroll_ > r.Top() - kEntryGap) y_scroll_ = r.Top() - kEntryGap;
  else if (y_scroll_ + raster_h_ < r.Bottom() + kEntryGap)
    y_scroll_ = r.Bottom() + kEntryGap - raster_h_;
}

void QuestsDisplay::SeriesQuestViewerDisplay::Tick(io::InputHandler& input) {
  Display::Tick(input);
  if (rows_.empty()) return;

  if (input.InputDown("SHIFT")) {
    in_browsing_ = true;
    if (input.InputPressed("shift+cursor-down")) y_scroll_ += 3;
    else if (input.InputPressed("shift+cursor-up")) y_scroll_ -= 3;
    else if (input.InputPressed("shift+cursor-right")) x_scroll_ += 3;
    else if (input.InputPressed("shift+cursor-left")) x_scroll_ -= 3;
    return;
  }

  if (in_browsing_) {
    ScrollIfNeeded();
    in_browsing_ = false;
  }

  if (input.InputPressed("cursor-down") &&
      cursor_y_ < static_cast<int32_t>(rows_.size()) - 1) {
    ++cursor_y_;
    if (cursor_x_ >= static_cast<int32_t>(rows_[cursor_y_].size())) {
      cursor_x_ = static_cast<int32_t>(rows_[cursor_y_].size()) - 1;
    }
    io::Sound::Play("select");
    ScrollIfNeeded();
  } else if (input.InputPressed("cursor-up") && cursor_y_ > 0) {
    --cursor_y_;
    if (cursor_x_ >= static_cast<int32_t>(rows_[cursor_y_].size())) {
      cursor_x_ = static_cast<int32_t>(rows_[cursor_y_].size()) - 1;
    }
    io::Sound::Play("select");
    ScrollIfNeeded();
  } else if (input.InputPressed("cursor-right") &&
             cursor_x_ < static_cast<int32_t>(rows_[cursor_y_].size()) - 1) {
    ++cursor_x_;
    io::Sound::Play("select");
    ScrollIfNeeded();
  } else if (input.InputPressed("cursor-left") && cursor_x_ > 0) {
    --cursor_x_;
    io::Sound::Play("select");
    ScrollIfNeeded();
  } else if (input.InputPressed("select")) {
    io::Sound::Play("confirm");
    DisplayHost::SetDisplay(std::make_unique<QuestInformationDisplay>(
        rows_[cursor_y_][cursor_x_].quest));
  }
}

void QuestsDisplay::SeriesQuestViewerDisplay::Render(
    gfx::Screen* screen) const {
  Display::Render(screen);
  if (!screen) return;

  screen->FillRect(raster_x_, raster_y_, raster_w_, raster_h_,
                   gfx::color::kBlack);
  screen->DrawRect(raster_x_ - 1, raster_y_ - 1, raster_w_ + 2, raster_h_ + 2,
                   gfx::color::kWhite);

  auto plot_pixel = [&](int32_t x, int32_t y, int32_t col) {
    const int32_t sx = raster_x_ + x - x_scroll_;
    const int32_t sy = raster_y_ + y - y_scroll_;
    if (sx < raster_x_ || sx >= raster_x_ + raster_w_ || sy < raster_y_ ||
        sy >= raster_y_ + raster_h_) {
      return;
    }
    screen->FillRect(sx, sy, 1, 1, col);
  };

  auto plot_line = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       int32_t min_y, int32_t max_y, int32_t col) {
    const int32_t steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    if (steps == 0) {
      if (y0 >= min_y && y0 <= max_y) plot_pixel(x0, y0, col);
      return;
    }
    for (int32_t s = 0; s <= steps; ++s) {
      const int32_t px = x0 + (x1 - x0) * s / steps;
      const int32_t py = y0 + (y1 - y0) * s / steps;
      if (py >= min_y && py <= max_y) plot_pixel(px, py, col);
    }
  };

  if (rows_.empty()) {
    const std::string txt = util::Localization::GetLocalized(
        "minicraft.displays.quests.display.no_quest");
    const int32_t tw = gfx::Font::TextWidth(txt);
    const int32_t th = gfx::Font::TextHeight();
    gfx::Font::Draw(txt, screen, raster_x_ + raster_w_ / 2 - tw / 2,
                    raster_y_ + raster_h_ / 2 - th / 2, gfx::color::kGray);
    return;
  }

  for (size_t r = 0; r < rows_.size(); ++r) {
    for (size_t c = 0; c < rows_[r].size(); ++c) {
      const QuestNode& node = rows_[r][c];
      if (!node.parent) continue;
      for (size_t pr = 0; pr < rows_.size(); ++pr) {
        for (size_t pc = 0; pc < rows_[pr].size(); ++pc) {
          if (rows_[pr][pc].quest == node.parent) {
            const gfx::Rectangle& prec = rows_[pr][pc].bounds;
            const bool selected = (static_cast<int32_t>(c) == cursor_x_ &&
                                   static_cast<int32_t>(r) == cursor_y_);
            const bool parent_sel = (static_cast<int32_t>(pc) == cursor_x_ &&
                                     static_cast<int32_t>(pr) == cursor_y_);
            const int32_t line_col =
                selected
                    ? gfx::color::kCyan
                    : (parent_sel ? gfx::color::kGreen
                                  : gfx::color::TintEncoded(gfx::color::kGray,
                                                            -1, true));
            const gfx::Point p0 = prec.Center();
            const gfx::Point p1 = node.bounds.Center();
            plot_line(p0.x, p0.y, p1.x, p1.y, prec.Bottom(), node.bounds.Top(),
                      line_col);
          }
        }
      }
    }
  }

  constexpr int32_t kEntryPadding = 3;
  for (size_t r = 0; r < rows_.size(); ++r) {
    for (size_t c = 0; c < rows_[r].size(); ++c) {
      const QuestNode& node = rows_[r][c];
      const gfx::Rectangle& rec = node.bounds;
      const bool selected = (static_cast<int32_t>(c) == cursor_x_ &&
                             static_cast<int32_t>(r) == cursor_y_);
      const int32_t text_col =
          selected
              ? (node.quest->completed
                     ? gfx::color::TintEncoded(gfx::color::kGreen, 1, true)
                     : gfx::color::kWhite)
              : gfx::color::TintEncoded(gfx::color::kGray, 1, true);
      const int32_t border_col =
          selected
              ? (node.quest->completed
                     ? gfx::color::TintEncoded(gfx::color::kGreen, -1, true)
                     : gfx::color::TintEncoded(gfx::color::kGray, 2, true))
              : gfx::color::kGray;

      const int32_t tx = raster_x_ + rec.Left() + kEntryPadding - x_scroll_;
      const int32_t ty = raster_y_ + rec.Top() + kEntryPadding - y_scroll_;
      if (tx >= raster_x_ - rec.Width() && tx < raster_x_ + raster_w_ &&
          ty >= raster_y_ - rec.Height() && ty < raster_y_ + raster_h_) {
        gfx::Font::Draw(util::Localization::GetLocalized(node.quest->key),
                        screen, tx, ty, text_col);
      }

      for (int32_t i = 0; i < rec.Width(); ++i) {
        plot_pixel(rec.Left() + i, rec.Top(), border_col);
        plot_pixel(rec.Left() + i, rec.Bottom() - 1, border_col);
      }
      for (int32_t j = 0; j < rec.Height(); ++j) {
        plot_pixel(rec.Left(), rec.Top() + j, border_col);
        plot_pixel(rec.Right() - 1, rec.Top() + j, border_col);
      }
    }
  }
}

QuestsDisplay::QuestInformationDisplay::QuestInformationDisplay(
    const util::Quest* quest)
    : Display(false, true) {
  if (!quest) return;
  const char* state_key =
      quest->completed
          ? "minicraft.displays.quests.quest_info.display.status.completed"
          : (quest->unlocked
                 ? "minicraft.displays.quests.quest_info.display.status.unlocked"
                 : "minicraft.displays.quests.quest_info.display.status.locked");
  const int32_t state_col =
      quest->completed
          ? gfx::color::kGreen
          : (quest->unlocked ? gfx::color::kWhite : gfx::color::kGray);

  Menu::Builder top_b(true, 1, gfx::RelPos::kCenter);
  top_b.SetPositioning({gfx::Screen::w / 2, 5}, gfx::RelPos::kBottom)
      .SetSelectable(false)
      .AddEntry(std::make_unique<StringEntry>(
          quest->series ? quest->series->key : "", gfx::color::kWhite, true))
      .AddEntry(std::make_unique<StringEntry>(
          util::Localization::GetLocalized(quest->key) + ": " +
              util::Localization::GetLocalized(state_key),
          state_col, false))
      .AddEntry(std::make_unique<StringEntry>(
          quest->ShouldAllCriteriaBeCompleted()
              ? util::Localization::GetLocalized(
                    "minicraft.displays.quests.quest_info.display.progress",
                    quest->NumCriteriaCompleted(), quest->TotalNumCriteria())
              : util::Localization::GetLocalized(
                    "minicraft.displays.quests.quest_info.display.progress_uncompleted"),
          gfx::color::kWhite, false));
  menus_.push_back(top_b.CreateMenu());

  Menu::Builder desc_b(true, 2, gfx::RelPos::kCenter);
  desc_b.SetPositioning({gfx::Screen::w / 2, 52}, gfx::RelPos::kBottom)
      .SetSelectable(false)
      .SetEntries(StringEntry::UseLines({quest->description}, gfx::color::kWhite,
                                        true));
  menus_.push_back(desc_b.CreateMenu());
}

std::vector<TutorialDisplayHandler::ControlGuide>
    TutorialDisplayHandler::guides_;
int32_t TutorialDisplayHandler::current_guide_idx_ = -1;
int32_t TutorialDisplayHandler::guide_banner_anim_ = 60;
int32_t TutorialDisplayHandler::current_ongoing_idx_ = -1;

void TutorialDisplayHandler::Reset(bool initial) {
  if (guides_.empty()) {
    guides_.push_back({300, "move-up|move-down|move-left|move-right",
                       "minicraft.control_guide.move", 0});
    guides_.push_back({1, "attack", "minicraft.control_guide.attack", 0});
    guides_.push_back({1, "menu", "minicraft.control_guide.menu", 0});
    guides_.push_back({1, "craft", "minicraft.control_guide.craft", 0});
  }
  for (auto& g : guides_) g.interacted_ticks = 0;

  if (initial) {
    current_ongoing_idx_ = -1;
    util::AdvancementSystem::Instance().SetCurrentOngoingTutorial("");
    if (io::Settings::GetBool("tutorials")) {
      current_guide_idx_ = 0;
      guide_banner_anim_ = 60;
    } else {
      current_guide_idx_ = -1;
    }
  } else {
    current_guide_idx_ = -1;
    current_ongoing_idx_ = -1;
    const auto& cur_key =
        util::AdvancementSystem::Instance().CurrentOngoingTutorial();
    const auto& tuts = util::AdvancementSystem::Instance().Tutorials();
    if (!cur_key.empty()) {
      for (size_t i = 0; i < tuts.size(); ++i) {
        if (tuts[i] && tuts[i]->key == cur_key) {
          current_ongoing_idx_ = static_cast<int32_t>(i);
          tuts[i]->Update();
          break;
        }
      }
    }
  }
}

bool TutorialDisplayHandler::InGuide() {
  return current_guide_idx_ >= 0 &&
         current_guide_idx_ < static_cast<int32_t>(guides_.size());
}

bool TutorialDisplayHandler::InTutorial() {
  const auto& tuts = util::AdvancementSystem::Instance().Tutorials();
  return current_ongoing_idx_ >= 0 &&
         current_ongoing_idx_ < static_cast<int32_t>(tuts.size());
}

bool TutorialDisplayHandler::InQuests() {
  return io::Settings::GetBool("quests") && !InGuide() && !InTutorial();
}

void TutorialDisplayHandler::TurnOffGuides() {
  current_guide_idx_ = -1;
  const auto& tuts = util::AdvancementSystem::Instance().Tutorials();
  if (io::Settings::GetBool("tutorials") && !tuts.empty()) {
    current_ongoing_idx_ = 0;
    util::AdvancementSystem::Instance().SetCurrentOngoingTutorial(
        tuts[0]->key);
    tuts[0]->Update();
    RefreshAll();
  }
}

void TutorialDisplayHandler::GetNextTutorial() {
  const auto& tuts = util::AdvancementSystem::Instance().Tutorials();
  if (current_ongoing_idx_ >= 0 &&
      current_ongoing_idx_ + 1 < static_cast<int32_t>(tuts.size())) {
    ++current_ongoing_idx_;
    util::AdvancementSystem::Instance().SetCurrentOngoingTutorial(
        tuts[current_ongoing_idx_]->key);
    tuts[current_ongoing_idx_]->Update();
    RefreshAll();
  } else {
    TurnOffTutorials();
  }
}

void TutorialDisplayHandler::RefreshAll() {
  const auto& tuts = util::AdvancementSystem::Instance().Tutorials();
  if (current_ongoing_idx_ < 0 ||
      current_ongoing_idx_ >= static_cast<int32_t>(tuts.size())) {
    io::Settings::Set("tutorials", false);
    return;
  }
  int32_t highest_completed = -1;
  for (int32_t i = static_cast<int32_t>(tuts.size()) - 1; i >= 0; --i) {
    if (tuts[i] && tuts[i]->completed) {
      highest_completed = i;
      break;
    }
  }
  if (highest_completed > current_ongoing_idx_) {
    current_ongoing_idx_ = highest_completed;
    util::AdvancementSystem::Instance().SetCurrentOngoingTutorial(
        tuts[current_ongoing_idx_]->key);
  }
  if (current_ongoing_idx_ >= 0 &&
      current_ongoing_idx_ < static_cast<int32_t>(tuts.size()) &&
      tuts[current_ongoing_idx_]->completed) {
    GetNextTutorial();
  }
}

void TutorialDisplayHandler::UpdateCompletedElement(const std::string& key) {
  if (!io::Settings::GetBool("tutorials")) return;
  const auto& tuts = util::AdvancementSystem::Instance().Tutorials();
  bool is_completed = false;
  for (const auto& t : tuts) {
    if (t && t->key == key && t->completed) {
      is_completed = true;
      break;
    }
  }
  if (is_completed) RefreshAll();
}

void TutorialDisplayHandler::SkipCurrent() {
  if (InGuide()) {
    ++current_guide_idx_;
    guide_banner_anim_ = 60;
    if (!InGuide()) TurnOffGuides();
    return;
  }
  if (InTutorial()) {
    GetNextTutorial();
  }
}

void TutorialDisplayHandler::TurnOffTutorials() {
  current_guide_idx_ = -1;
  current_ongoing_idx_ = -1;
  util::AdvancementSystem::Instance().SetCurrentOngoingTutorial("");
  io::Settings::Set("tutorials", false);
  DisplayHost::AddNotification(util::Localization::GetLocalized(
      "minicraft.notification.tutorials_completed"));
}

void TutorialDisplayHandler::Tick(io::InputHandler& input) {
  if (InGuide()) {
    if (guide_banner_anim_ > 0) --guide_banner_anim_;
    if (input.InputPressed("expandQuestDisplay")) {
      TurnOffGuides();
      return;
    }
    auto& g = guides_[current_guide_idx_];
    if (g.interacted_ticks >= g.duration) {
      if (current_guide_idx_ + 1 < static_cast<int32_t>(guides_.size())) {
        ++current_guide_idx_;
        guide_banner_anim_ = 60;
      } else {
        TurnOffGuides();
      }
      return;
    }
    if (DisplayHost::GetDisplay() == nullptr) {
      size_t pos = 0;
      while (pos <= g.keys.size()) {
        size_t bar = g.keys.find('|', pos);
        std::string_view k(g.keys.data() + pos,
                           bar == std::string::npos ? g.keys.size() - pos
                                                    : bar - pos);
        if (input.InputDown(k)) g.interacted_ticks++;
        if (bar == std::string::npos) break;
        pos = bar + 1;
      }
    }
    return;
  }

  if (InTutorial()) {
    if (input.InputPressed("expandQuestDisplay") &&
        DisplayHost::GetDisplay() == nullptr) {
      const auto& tut =
          util::AdvancementSystem::Instance().Tutorials()[current_ongoing_idx_];
      if (tut) {
        PopupConfig cfg;
        cfg.title = tut->key;
        cfg.entry_spacing = 4;
        cfg.localize_title = true;
        DisplayHost::SetDisplay(std::make_unique<PopupDisplay>(
            std::move(cfg), std::vector<std::string>{tut->description}));
      }
    }
  }
}

void TutorialDisplayHandler::Render(gfx::Screen* screen) {
  if (!screen) return;
  if (InGuide()) {
    const auto& g = guides_[current_guide_idx_];
    auto* input = DisplayHost::Instance().input;
    std::string msg;
    if (g.loc_key == "minicraft.control_guide.move" && input) {
      msg = util::Localization::GetLocalized(
          g.loc_key,
          input->GetMapping("move-up") + "|" + input->GetMapping("move-left") +
              "|" + input->GetMapping("move-down") + "|" +
              input->GetMapping("move-right"));
    } else if (input) {
      msg = util::Localization::GetLocalized(g.loc_key,
                                             input->GetMapping(g.keys));
    }
    auto lines =
        gfx::Font::GetLines(msg, gfx::Screen::w - 20, gfx::Screen::h, 0);
    if (guide_banner_anim_ > 0) {
      const int32_t tw = gfx::Font::TextWidth(lines);
      const int32_t xp = gfx::Screen::w / 2 - (tw + 8) / 2;
      const int32_t yp = gfx::Screen::h / 2 -
                         (static_cast<int32_t>(lines.size()) * 8 + 8) / 2;
      screen->FillRect(xp + 1, yp + 1, tw + 6,
                       static_cast<int32_t>(lines.size()) * 8 + 6,
                       gfx::color::kBlue);
      screen->DrawRect(xp, yp, tw + 8,
                       static_cast<int32_t>(lines.size()) * 8 + 8,
                       gfx::color::kWhite);
      const int32_t y_pad =
          gfx::Screen::h / 2 - static_cast<int32_t>(lines.size()) * 4;
      for (size_t i = 0; i < lines.size(); ++i) {
        gfx::Font::DrawCentered(lines[i], screen,
                                y_pad + 8 * static_cast<int32_t>(i),
                                gfx::color::kWhite);
      }
    } else {
      Menu::Builder b(true, 0, gfx::RelPos::kRight);
      b.SetPositioning({gfx::Screen::w - 9, 9}, gfx::RelPos::kBottomLeft)
          .SetSelectable(false)
          .SetEntries(StringEntry::UseLines(lines, gfx::color::kWhite, false));
      auto m = b.CreateMenu();
      m->Render(screen);
      const gfx::Rectangle& r = m->GetBounds();
      const int32_t len = std::max<int32_t>(0, r.Width() - 4);
      const int32_t fill =
          g.duration > 0 ? len * g.interacted_ticks / g.duration : len;
      screen->DrawAxisLine(r.Left() + 2, r.Bottom() - 2, 0, fill,
                           gfx::color::kWhite);
    }
  } else if (InTutorial()) {
    const auto& tut =
        util::AdvancementSystem::Instance().Tutorials()[current_ongoing_idx_];
    if (!tut) return;
    Menu::Builder b(true, 0, gfx::RelPos::kRight);
    b.SetPositioning({gfx::Screen::w - 9, 9}, gfx::RelPos::kBottomLeft)
        .SetSelectable(false)
        .SetEntries(
            StringEntry::UseLines({tut->key}, gfx::color::kWhite, true));
    auto m = b.CreateMenu();
    m->Render(screen);
    const gfx::Rectangle& r = m->GetBounds();
    auto* inp = DisplayHost::Instance().input;
    const std::string help = util::Localization::GetLocalized(
        "minicraft.displays.tutorial_display_handler.display.element_examine_help",
        inp ? inp->GetMapping("expandQuestDisplay") : "L");
    auto lines =
        gfx::Font::GetLines(help, gfx::Screen::w * 2 / 3, gfx::Screen::h, 0);
    for (size_t i = 0; i < lines.size(); ++i) {
      gfx::Font::Draw(lines[i], screen,
                      r.Right() - gfx::Font::TextWidth(lines[i]),
                      r.Bottom() + 8 * static_cast<int32_t>(1 + i),
                      gfx::color::kGray);
    }
  }
}

}  // namespace minicraft::screen
