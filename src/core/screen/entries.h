#ifndef MINICRAFT_CORE_SCREEN_ENTRIES_H_
#define MINICRAFT_CORE_SCREEN_ENTRIES_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/gfx/color.h"

namespace minicraft {
namespace gfx {
class Screen;
}
namespace io {
class InputHandler;
struct SettingEntry;
}  // namespace io
namespace item {
class Item;
class Recipe;
}  // namespace item
}  // namespace minicraft

namespace minicraft::screen {

class ListEntry {
 public:
  static constexpr int32_t kColUnselected = gfx::color::kGray;
  static constexpr int32_t kColSelected = gfx::color::kWhite;

  virtual ~ListEntry() = default;

  virtual void Tick(io::InputHandler& input) = 0;

  virtual void Render(gfx::Screen* screen, int32_t x, int32_t y,
                      bool is_selected, std::string_view contain,
                      int32_t contain_color) const;
  virtual void Render(gfx::Screen* screen, int32_t x, int32_t y,
                      bool is_selected) const;

  virtual int32_t GetColor(bool is_selected) const {
    return is_selected ? kColSelected : kColUnselected;
  }

  virtual int32_t GetWidth() const;
  static int32_t GetHeight();

  bool IsSelectable() const { return selectable_ && IsVisible(); }
  virtual bool IsVisible() const { return visible_; }
  void SetSelectable(bool selectable) { selectable_ = selectable; }
  void SetVisible(bool visible) { visible_ = visible; }

  virtual std::string ToString() const = 0;

 private:
  bool selectable_ = true;
  bool visible_ = true;
};

class BlankEntry : public ListEntry {
 public:
  BlankEntry() { SetSelectable(false); }
  void Tick(io::InputHandler& /*input*/) override {}
  void Render(gfx::Screen* /*screen*/, int32_t /*x*/, int32_t /*y*/,
              bool /*is_selected*/) const override {}
  int32_t GetWidth() const override;
  std::string ToString() const override { return " "; }
};

class StringEntry : public ListEntry {
 public:
  static constexpr int32_t kDefaultColor = gfx::color::kWhite;

  static std::vector<std::unique_ptr<ListEntry>> UseLines(
      const std::vector<std::string>& lines,
      int32_t color = kDefaultColor, bool localize = true,
      int32_t max_width = 0);

  explicit StringEntry(std::string text, int32_t color = kDefaultColor,
                       bool localize = true);
  StringEntry(std::string text, bool localize)
      : StringEntry(std::move(text), kDefaultColor, localize) {}

  void SetText(std::string text) { text_ = std::move(text); }
  void Tick(io::InputHandler& /*input*/) override {}
  int32_t GetColor(bool /*is_selected*/) const override { return color_; }
  std::string ToString() const override;

 private:
  std::string text_;
  int32_t color_ = kDefaultColor;
  bool localize_ = true;
};

class SelectEntry : public ListEntry {
 public:
  using Action = std::function<void()>;
  using ColorFn = std::function<int32_t(bool)>;

  SelectEntry(std::string text, Action on_select, bool localize = true,
              ColorFn custom_color = nullptr);

  void SetText(std::string text) { text_ = std::move(text); }
  const std::string& RawText() const { return text_; }

  void Tick(io::InputHandler& input) override;
  int32_t GetColor(bool is_selected) const override;
  std::string ToString() const override;

 private:
  Action on_select_;
  std::string text_;
  bool localize_ = true;
  ColorFn custom_color_;
};

// Wraps a mutable io::SettingEntry for interactive Left/Right option selection.
class SettingListEntry : public ListEntry {
 public:
  using ChangeCallback = std::function<void(const io::SettingEntry&)>;

  explicit SettingListEntry(io::SettingEntry* setting,
                            ChangeCallback on_change = nullptr);

  void Tick(io::InputHandler& input) override;
  int32_t GetColor(bool is_selected) const override;
  bool IsVisible() const override;
  std::string ToString() const override;

  io::SettingEntry* Setting() const { return setting_; }

 private:
  void MoveSelection(int32_t delta, int32_t step = 1);

  io::SettingEntry* setting_ = nullptr;
  ChangeCallback on_change_;
};

class InputEntry : public ListEntry {
 public:
  using Validator = std::function<bool(std::string_view)>;
  using ChangeListener = std::function<void(const std::string&)>;
  using CharFilter = std::function<bool(char32_t)>;

  InputEntry(std::string prompt, int32_t max_length = 36,
             std::string init_value = "", Validator validator = nullptr,
             bool lowercase_output = false, bool right_scroll_align = false,
             CharFilter char_filter = nullptr);

  void Tick(io::InputHandler& input) override;
  void Render(gfx::Screen* screen, int32_t x, int32_t y,
              bool is_selected) const override;
  std::string ToString() const override;

  std::string GetUserInput() const;
  bool IsValid() const;
  void SetChangeListener(ChangeListener l) { listener_ = std::move(l); }
  void SetCharFilter(CharFilter f) { char_filter_ = std::move(f); }

 private:
  std::string prompt_;
  int32_t max_length_ = 36;
  std::string user_input_;
  Validator validator_;
  ChangeListener listener_;
  CharFilter char_filter_;
  bool lowercase_output_ = false;
  bool right_scroll_align_ = false;
};

class KeyInputEntry : public ListEntry {
 public:
  KeyInputEntry(std::string_view key_pref,
                const std::unordered_set<std::string>& duplicated);

  void Tick(io::InputHandler& input) override;
  int32_t GetWidth() const override;
  std::string ToString() const override;

 private:
  std::string action_;
  std::string mapping_;
  std::string buffer_;
};

class ItemEntry : public ListEntry {
 public:
  explicit ItemEntry(std::shared_ptr<item::Item> item);

  item::Item* GetItem() const { return item_.get(); }
  std::shared_ptr<item::Item> GetItemPtr() const { return item_; }

  void Tick(io::InputHandler& /*input*/) override {}
  void Render(gfx::Screen* screen, int32_t x, int32_t y,
              bool is_selected) const override;
  std::string ToString() const override;

 protected:
  std::shared_ptr<item::Item> item_;
};

class ItemListingEntry : public ItemEntry {
 public:
  ItemListingEntry(std::shared_ptr<item::Item> item, std::string right_text);

  void SetSelectableOverride() { SetSelectable(false); }
  void Render(gfx::Screen* screen, int32_t x, int32_t y,
              bool is_selected) const override;
  std::string ToString() const override;

 private:
  std::string right_text_;
};

class RecipeEntry : public ItemEntry {
 public:
  explicit RecipeEntry(const item::Recipe* recipe);

  const item::Recipe* GetRecipe() const { return recipe_; }

  void Render(gfx::Screen* screen, int32_t x, int32_t y,
              bool is_selected) const override;
  std::string ToString() const override;

 private:
  const item::Recipe* recipe_ = nullptr;
};

}  // namespace minicraft::screen

#endif  // MINICRAFT_CORE_SCREEN_ENTRIES_H_
