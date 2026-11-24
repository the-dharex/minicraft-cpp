#ifndef MINICRAFT_CORE_SCREEN_MENU_H_
#define MINICRAFT_CORE_SCREEN_MENU_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/gfx/color.h"
#include "core/gfx/geometry.h"
#include "core/gfx/sprite_atlas.h"
#include "core/screen/entries.h"

namespace minicraft {
namespace gfx {
class Screen;
}
namespace io {
class InputHandler;
}
}  // namespace minicraft

namespace minicraft::screen {

class Menu {
 public:
  class Builder {
   public:
    Builder(bool has_frame, int32_t entry_spacing, gfx::RelPos entry_pos);
    Builder(bool has_frame, int32_t entry_spacing, gfx::RelPos entry_pos,
            std::vector<std::unique_ptr<ListEntry>> entries);

    Builder& SetEntries(std::vector<std::unique_ptr<ListEntry>> entries);
    Builder& AddEntry(std::unique_ptr<ListEntry> entry);
    Builder& SetPositioning(gfx::Point anchor, gfx::RelPos menu_pos);
    Builder& SetSize(int32_t width, int32_t height);
    Builder& SetMenuSize(gfx::Dimension d);
    Builder& SetBounds(const gfx::Rectangle& rect);
    Builder& SetDisplayLength(int32_t num_entries);
    Builder& SetTitlePos(gfx::RelPos rp);
    Builder& SetTitle(std::string title);
    Builder& SetTitle(std::string title, int32_t color,
                      bool full_color = false);
    Builder& SetFrame(bool has_frame);
    Builder& SetScrollPolicies(float padding, bool wrap);
    Builder& SetShouldRender(bool render);
    Builder& SetSelectable(bool selectable);
    Builder& SetSelection(int32_t sel);
    Builder& SetSelection(int32_t sel, int32_t disp_sel);
    Builder& SetSearcherBar(bool searcher_bar);

    std::unique_ptr<Menu> CreateMenu();

   private:
    bool has_frame_ = false;
    int32_t spacing_ = 0;
    gfx::RelPos entry_pos_ = gfx::RelPos::kCenter;
    std::vector<std::unique_ptr<ListEntry>> entries_;

    gfx::Point anchor_;
    gfx::RelPos menu_pos_ = gfx::RelPos::kCenter;
    bool has_menu_size_ = false;
    gfx::Dimension menu_size_;

    int32_t display_length_ = 0;
    float padding_ = 1.0f;
    bool wrap_ = false;
    bool should_render_ = true;
    bool set_selectable_ = false;
    bool selectable_ = false;
    int32_t selection_ = 0;
    int32_t disp_selection_ = 0;

    std::string title_;
    gfx::RelPos title_pos_ = gfx::RelPos::kTop;
    bool full_title_color_ = false;
    bool set_title_color_ = false;
    int32_t title_col_ = gfx::color::kYellow;
    bool searcher_bar_ = false;
  };

  Menu() = default;
  Menu(Menu&&) = default;
  Menu& operator=(Menu&&) = default;
  virtual ~Menu() = default;

  void Init();

  virtual void Tick(io::InputHandler& input);
  virtual void Render(gfx::Screen* screen) const;

  void SetSelection(int32_t idx);
  int32_t GetSelection() const { return selection_; }
  int32_t GetDispSelection() const { return disp_selection_; }
  int32_t GetNumOptions() const {
    return static_cast<int32_t>(entries_.size());
  }

  ListEntry* GetCurEntry() const;
  ListEntry* GetEntry(int32_t idx) const;
  void SetEntries(std::vector<std::unique_ptr<ListEntry>> entries);
  void UpdateEntry(int32_t idx, std::unique_ptr<ListEntry> new_entry);
  void UpdateSelectedEntry(std::unique_ptr<ListEntry> new_entry);
  void RemoveSelectedEntry();

  const gfx::Rectangle& GetBounds() const { return bounds_; }
  const std::string& GetTitle() const { return title_; }
  bool IsSelectable() const { return selectable_; }
  bool ShouldRender() const { return should_render; }
  bool IsSearcherBarActive() const { return searcher_bar_active_; }
  void SetColors(const Menu& model) { title_color_ = model.title_color_; }
  void Translate(int32_t xoff, int32_t yoff);

  bool should_render = true;

 private:
  void DoScroll();
  void RenderFrame(gfx::Screen* screen) const;

  std::vector<std::unique_ptr<ListEntry>> entries_;
  int32_t spacing_ = 0;
  gfx::Rectangle bounds_;
  gfx::Rectangle entry_bounds_;
  gfx::RelPos entry_pos_ = gfx::RelPos::kCenter;

  std::string title_;
  int32_t title_color_ = gfx::color::kYellow;
  gfx::Point title_loc_;
  bool draw_vertically_ = false;

  bool has_frame_ = false;
  bool selectable_ = false;

  int32_t display_length_ = 0;
  int32_t padding_ = 0;
  bool wrap_ = false;

  int32_t selection_ = 0;
  int32_t disp_selection_ = 0;
  int32_t offset_ = 0;

  bool use_searcher_bar_ = false;
  bool searcher_bar_active_ = false;
  std::vector<int32_t> list_searcher_;
  int32_t list_position_searcher_ = 0;
  int32_t selection_searcher_ = 0;
  std::string typing_searcher_;

  gfx::LinkedSprite hud_sheet_{gfx::SpriteType::kGui, "hud"};
};

class OnScreenKeyboardMenu : public Menu {
 public:
  enum class TickResult {
    kNone = 0,
    kActionCompleted = 1,
    kBackspaceActed = 2,
  };

  OnScreenKeyboardMenu();

  static std::unique_ptr<OnScreenKeyboardMenu> CheckAndCreateMenu(
      const io::InputHandler& input);

  TickResult TickKeyboard(io::InputHandler& input);
  void Render(gfx::Screen* screen) const override;

  void SetVisible(bool visible);
  bool IsVisible() const { return visible_; }

 private:
  struct VirtualKey {
    char output = '\0';
    bool is_shift = false;
    bool is_space = false;
    bool is_backspace = false;
  };

  void InitKeyboard();
  void SetShiftPressed(bool pressed);

  std::vector<std::vector<VirtualKey>> keys_f_;
  std::vector<std::vector<VirtualKey>> keys_b_;
  bool shift_pressed_ = false;
  bool visible_ = true;
  int32_t x_ = 0;
  int32_t y_ = 0;
  int32_t key_pressed_ = 0;
  gfx::LinkedSprite osk_sheet_{gfx::SpriteType::kGui, "osk"};
};

}  // namespace minicraft::screen

#endif  // MINICRAFT_CORE_SCREEN_MENU_H_
