#include "core/screen/entries.h"

#include <algorithm>
#include <cctype>

#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/io/sound.h"
#include "core/item/item.h"
#include "core/item/recipes.h"
#include "core/screen/display.h"
#include "core/util/localization.h"

namespace minicraft::screen {

namespace {

std::string ReplaceAllStr(std::string str, std::string_view from,
                          std::string_view to) {
  if (from.empty())
    return str;
  size_t pos = 0;
  while ((pos = str.find(from, pos)) != std::string::npos) {
    str.replace(pos, from.size(), to);
    pos += to.size();
  }
  return str;
}

bool IsHexChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

size_t ColorCodeLenAt(std::string_view sv, size_t pos) {
  if (pos + 2 <= sv.size() && sv[pos] == '\xC2' && sv[pos + 1] == '\xA7') {
    if (pos + 10 <= sv.size()) {
      bool all_hex = true;
      for (size_t i = 0; i < 8; ++i) {
        if (!IsHexChar(sv[pos + 2 + i])) {
          all_hex = false;
          break;
        }
      }
      if (all_hex) return 10;
    }
    return 2;
  }
  return 0;
}

std::string ToLowerAscii(std::string_view sv) {
  std::string out(sv);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// Highlights matches of `contain` in `str` without corrupting `§AARRGGBB` color escapes.
std::string HighlightOutsideColorCodes(std::string_view str,
                                       std::string_view contain,
                                       int32_t contain_color,
                                       int32_t restore_color) {
  if (contain.empty()) return std::string(str);
  std::string plain;
  std::vector<size_t> plain_to_orig;
  plain.reserve(str.size());
  plain_to_orig.reserve(str.size());
  for (size_t i = 0; i < str.size();) {
    size_t clen = ColorCodeLenAt(str, i);
    if (clen > 0) {
      i += clen;
      continue;
    }
    plain.push_back(str[i]);
    plain_to_orig.push_back(i);
    ++i;
  }
  const std::string lower_plain = ToLowerAscii(plain);
  const std::string lower_needle = ToLowerAscii(contain);
  size_t match_plain = lower_plain.find(lower_needle);
  if (match_plain == std::string::npos) return std::string(str);

  std::string out;
  out.reserve(str.size() + 32);
  size_t orig_cursor = 0;
  const std::string hl_code = gfx::color::ToStringCode(contain_color);
  const std::string rst_code = gfx::color::ToStringCode(restore_color);
  while (match_plain != std::string::npos) {
    const size_t start_orig = plain_to_orig[match_plain];
    const size_t end_plain = match_plain + lower_needle.size();
    const size_t end_orig =
        (end_plain < plain_to_orig.size()) ? plain_to_orig[end_plain] : str.size();
    out.append(str.substr(orig_cursor, start_orig - orig_cursor));
    out.append(hl_code);
    for (size_t k = start_orig; k < end_orig;) {
      size_t clen = ColorCodeLenAt(str, k);
      if (clen > 0) {
        k += clen;
        continue;
      }
      out.push_back(str[k]);
      ++k;
    }
    out.append(rst_code);
    orig_cursor = end_orig;
    match_plain = lower_plain.find(lower_needle, end_plain);
  }
  out.append(str.substr(orig_cursor));
  return out;
}

} // namespace

void ListEntry::Render(gfx::Screen *screen, int32_t x, int32_t y,
                       bool is_selected, std::string_view contain,
                       int32_t contain_color) const {
  if (!IsVisible() || !screen)
    return;
  Render(screen, x, y, is_selected);
  if (contain.empty())
    return;

  const int32_t col = GetColor(is_selected);
  const std::string white_gray = gfx::color::ToStringCode(gfx::color::kWhite) +
                                 gfx::color::ToStringCode(gfx::color::kGray);
  std::string text =
      ReplaceAllStr(ToString(), white_gray, gfx::color::ToStringCode(col));
  std::string highlighted =
      HighlightOutsideColorCodes(text, contain, contain_color, col);
  if (highlighted != text) {
    gfx::Font::DrawColor(gfx::color::ToStringCode(col) + highlighted, screen,
                         x, y);
  }
}

void ListEntry::Render(gfx::Screen *screen, int32_t x, int32_t y,
                       bool is_selected) const {
  if (!IsVisible() || !screen)
    return;
  const int32_t col = GetColor(is_selected);
  const std::string white_gray = gfx::color::ToStringCode(gfx::color::kWhite) +
                                 gfx::color::ToStringCode(gfx::color::kGray);
  std::string text =
      ReplaceAllStr(ToString(), white_gray, gfx::color::ToStringCode(col));
  if (text.find(gfx::color::kColorMarker) != std::string::npos) {
    gfx::Font::DrawColor(gfx::color::ToStringCode(col) + text, screen, x, y);
  } else {
    gfx::Font::Draw(text, screen, x, y, col);
  }
}

int32_t ListEntry::GetWidth() const { return gfx::Font::TextWidth(ToString()); }

int32_t ListEntry::GetHeight() { return gfx::Font::TextHeight(); }

int32_t BlankEntry::GetWidth() const { return 8; }

// ---------------------------------------------------------------------------
// StringEntry
// ---------------------------------------------------------------------------
std::vector<std::unique_ptr<ListEntry>>
StringEntry::UseLines(const std::vector<std::string> &lines, int32_t color,
                      bool localize, int32_t max_width) {
  const int32_t wrap_w = max_width > 0 ? max_width : (gfx::Screen::w - 20);
  std::vector<std::string> wrapped;
  for (const auto &l : lines) {
    std::string s = localize ? util::Localization::GetLocalized(l) : l;
    auto split = gfx::Font::GetLines(s, wrap_w, gfx::Screen::h * 2, 0);
    wrapped.insert(wrapped.end(), split.begin(), split.end());
  }
  std::vector<std::unique_ptr<ListEntry>> out;
  out.reserve(wrapped.size());
  for (auto &line : wrapped) {
    out.push_back(std::make_unique<StringEntry>(std::move(line), color, false));
  }
  return out;
}

StringEntry::StringEntry(std::string text, int32_t color, bool localize)
    : text_(std::move(text)), color_(color), localize_(localize) {
  SetSelectable(false);
}

std::string StringEntry::ToString() const {
  return localize_ ? util::Localization::GetLocalized(text_) : text_;
}

// ---------------------------------------------------------------------------
// SelectEntry
// ---------------------------------------------------------------------------
SelectEntry::SelectEntry(std::string text, Action on_select, bool localize,
                         ColorFn custom_color)
    : on_select_(std::move(on_select)), text_(std::move(text)),
      localize_(localize), custom_color_(std::move(custom_color)) {}

void SelectEntry::Tick(io::InputHandler &input) {
  if (input.InputPressed("select") && on_select_) {
    io::Sound::Play("confirm");
    on_select_();
  }
}

int32_t SelectEntry::GetColor(bool is_selected) const {
  if (custom_color_)
    return custom_color_(is_selected);
  return ListEntry::GetColor(is_selected);
}

std::string SelectEntry::ToString() const {
  return localize_ ? util::Localization::GetLocalized(text_) : text_;
}

// ---------------------------------------------------------------------------
// SettingListEntry
// ---------------------------------------------------------------------------
SettingListEntry::SettingListEntry(io::SettingEntry *setting,
                                   ChangeCallback on_change)
    : setting_(setting), on_change_(std::move(on_change)) {}

void SettingListEntry::MoveSelection(int32_t delta, int32_t step) {
  if (!setting_)
    return;
  auto next_visible_idx = [&](size_t count) -> size_t {
    if (count == 0) return 0;
    const int32_t dir = (delta >= 0) ? 1 : -1;
    const int32_t n = static_cast<int32_t>(count);
    int32_t cur = static_cast<int32_t>(setting_->selected_idx % count);
    for (size_t attempt = 0; attempt < count; ++attempt) {
      int32_t next = cur + dir;
      if (setting_->wrap) {
        next = ((next % n) + n) % n;
      } else {
        next = std::clamp(next, 0, n - 1);
      }
      cur = next;
      if (setting_->IsOptionVisible(static_cast<size_t>(cur))) {
        return static_cast<size_t>(cur);
      }
      if (!setting_->wrap && (cur == 0 || cur == n - 1)) {
        return setting_->selected_idx;
      }
    }
    return setting_->selected_idx;
  };

  bool changed = false;
  switch (setting_->kind) {
  case io::SettingKind::kBool: {
    setting_->bool_val = !setting_->bool_val;
    changed = true;
    break;
  }
  case io::SettingKind::kIntRange: {
    const int32_t span = setting_->int_max - setting_->int_min + 1;
    if (span <= 0)
      break;
    int32_t next = setting_->int_val + delta * step;
    if (setting_->wrap) {
      if (next > setting_->int_max) next = setting_->int_min;
      if (next < setting_->int_min) next = setting_->int_max;
    } else {
      next = std::clamp(next, setting_->int_min, setting_->int_max);
    }
    changed = (next != setting_->int_val);
    setting_->int_val = next;
    break;
  }
  case io::SettingKind::kArrayString: {
    const size_t next = next_visible_idx(setting_->str_options.size());
    changed = (next != setting_->selected_idx);
    setting_->selected_idx = next;
    break;
  }
  case io::SettingKind::kArrayInt: {
    const size_t next = next_visible_idx(setting_->int_options.size());
    changed = (next != setting_->selected_idx);
    setting_->selected_idx = next;
    break;
  }
  }
  if (changed) {
    io::Sound::Play("select");
    if (setting_->on_change)
      setting_->on_change(*setting_);
    if (on_change_)
      on_change_(*setting_);
  }
}

void SettingListEntry::Tick(io::InputHandler &input) {
  if (!setting_)
    return;
  const int32_t step = (setting_->kind == io::SettingKind::kIntRange &&
                        input.PhysicalKeyDown("ALT"))
                           ? 10
                           : 1;
  if (input.InputPressed("cursor-left"))
    MoveSelection(-1, step);
  if (input.InputPressed("cursor-right")) {
    MoveSelection(1, step);
  }
}

int32_t SettingListEntry::GetColor(bool is_selected) const {
  return ListEntry::GetColor(is_selected);
}

bool SettingListEntry::IsVisible() const {
  return setting_ ? (ListEntry::IsVisible() && setting_->visible)
                  : ListEntry::IsVisible();
}

std::string SettingListEntry::ToString() const {
  if (!setting_)
    return "";
  std::string val = setting_->DisplayValue();
  if (setting_->kind != io::SettingKind::kIntRange &&
      setting_->localize_options) {
    val = util::Localization::GetLocalized(val);
  }
  return util::Localization::GetLocalized(setting_->loc_name) + ": " + val;
}

// ---------------------------------------------------------------------------
// InputEntry
// ---------------------------------------------------------------------------
InputEntry::InputEntry(std::string prompt, int32_t max_length,
                       std::string init_value, Validator validator,
                       bool lowercase_output, bool right_scroll_align,
                       CharFilter char_filter)
    : prompt_(std::move(prompt)), max_length_(max_length),
      user_input_(std::move(init_value)), validator_(std::move(validator)),
      char_filter_(std::move(char_filter)),
      lowercase_output_(lowercase_output),
      right_scroll_align_(right_scroll_align) {}

void InputEntry::Tick(io::InputHandler &input) {
  std::string prev = user_input_;
  user_input_ = input.AddKeyTyped(user_input_, char_filter_);
  if (input.InputPressed("CTRL-V")) {
    std::string clip = DisplayHost::ClipboardText();
    size_t off = 0;
    while (off < clip.size()) {
      size_t start = off;
      char32_t cp = gfx::Font::NextUtf8Codepoint(clip, &off);
      if (cp >= 0x20 && cp != 0x7F && (!char_filter_ || char_filter_(cp))) {
        user_input_.append(clip.substr(start, off - start));
      }
    }
  }
  if (max_length_ > 0 &&
      static_cast<int32_t>(user_input_.size()) > max_length_) {
    user_input_.resize(static_cast<size_t>(max_length_));
  }
  if (!user_input_.empty()) {
    if (input.InputPressed("CTRL-C")) {
      DisplayHost::SetClipboardText(user_input_);
    }
    if (input.InputPressed("CTRL-X")) {
      DisplayHost::SetClipboardText(user_input_);
      user_input_.clear();
    }
  }
  if (prev != user_input_ && listener_) {
    listener_(user_input_);
  }
}

void InputEntry::Render(gfx::Screen *screen, int32_t x, int32_t y,
                        bool is_selected) const {
  if (!IsVisible() || !screen)
    return;
  int32_t rx = x;
  const int32_t glyphs = gfx::Font::TextWidth(user_input_) / 8;
  if (right_scroll_align_ && glyphs > 11) {
    rx -= (glyphs - 11) * 8;
  }
  const int32_t col = IsValid()
                          ? (is_selected ? gfx::color::kGreen : kColUnselected)
                          : gfx::color::kRed;
  gfx::Font::Draw(ToString(), screen, rx, y, col);
}

std::string InputEntry::GetUserInput() const {
  if (!lowercase_output_)
    return user_input_;
  std::string lower = user_input_;
  for (char &c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return lower;
}

bool InputEntry::IsValid() const {
  return validator_ ? validator_(user_input_) : true;
}

std::string InputEntry::ToString() const {
  std::string label =
      prompt_.empty() ? "" : util::Localization::GetLocalized(prompt_);
  return label + (prompt_.empty() ? "" : ": ") + GetUserInput();
}

// ---------------------------------------------------------------------------
// KeyInputEntry
// ---------------------------------------------------------------------------
KeyInputEntry::KeyInputEntry(
    std::string_view key_pref,
    const std::unordered_set<std::string> &duplicated) {
  size_t semi = key_pref.find(';');
  if (semi == std::string_view::npos) {
    action_ = std::string(key_pref);
    return;
  }
  action_ = std::string(key_pref.substr(0, semi));
  std::string raw_map(key_pref.substr(semi + 1));

  const int32_t space_w = std::max<int32_t>(1, gfx::Font::TextWidth(" "));
  const int32_t total_cols = (gfx::Screen::w - 32) / space_w;
  const int32_t action_len =
      gfx::Font::TextWidth(util::Localization::GetLocalized(action_)) / space_w;
  const int32_t raw_len = gfx::Font::TextWidth(raw_map) / space_w;
  const int32_t spaces =
      std::max<int32_t>(1, total_cols - action_len - raw_len);
  buffer_.assign(static_cast<size_t>(spaces), ' ');

  std::string formatted;
  size_t pos = 0;
  while (pos <= raw_map.size()) {
    size_t bar = raw_map.find('|', pos);
    std::string tok = (bar == std::string::npos)
                          ? raw_map.substr(pos)
                          : raw_map.substr(pos, bar - pos);
    if (!formatted.empty())
      formatted.push_back('|');
    if (duplicated.count(tok)) {
      formatted += gfx::color::ToStringCode(gfx::color::kRed) + tok;
    } else {
      formatted += gfx::color::ToStringCode(gfx::color::kGray) + tok;
    }
    formatted += gfx::color::ToStringCode(gfx::color::kWhite);
    if (bar == std::string::npos)
      break;
    pos = bar + 1;
  }
  mapping_ = std::move(formatted);
}

void KeyInputEntry::Tick(io::InputHandler &input) {
  if (input.InputPressed("select") || input.PhysicalKeyPressed("C")) {
    input.ChangeKeyBinding(action_);
  } else if (input.PhysicalKeyPressed("A")) {
    input.AddKeyBinding(action_);
  }
}

int32_t KeyInputEntry::GetWidth() const { return gfx::Screen::w - 32; }

std::string KeyInputEntry::ToString() const {
  return util::Localization::GetLocalized(action_) + buffer_ + mapping_;
}

// ---------------------------------------------------------------------------
// ItemEntry & ItemListingEntry & RecipeEntry
// ---------------------------------------------------------------------------
ItemEntry::ItemEntry(std::shared_ptr<item::Item> item)
    : item_(std::move(item)) {}

void ItemEntry::Render(gfx::Screen *screen, int32_t x, int32_t y,
                       bool /*is_selected*/) const {
  if (!IsVisible() || !screen || !item_)
    return;
  ListEntry::Render(screen, x, y, true);
  gfx::LinkedSprite sp = item_->sprite;
  screen->Render(x, y, sp);
}

std::string ItemEntry::ToString() const {
  return item_ ? item_->GetDisplayName() : "";
}

ItemListingEntry::ItemListingEntry(std::shared_ptr<item::Item> item,
                                   std::string right_text)
    : ItemEntry(std::move(item)), right_text_(std::move(right_text)) {
  SetSelectable(false);
}

void ItemListingEntry::Render(gfx::Screen *screen, int32_t x, int32_t y,
                              bool /*is_selected*/) const {
  if (!IsVisible() || !screen || !item_)
    return;
  ListEntry::Render(screen, x, y, true);
  gfx::LinkedSprite sp = item_->sprite;
  screen->Render(x, y, sp);
}

std::string ItemListingEntry::ToString() const { return " " + right_text_; }

RecipeEntry::RecipeEntry(const item::Recipe *recipe)
    : ItemEntry(recipe ? std::shared_ptr<item::Item>(recipe->GetProduct())
                       : nullptr),
      recipe_(recipe) {}

void RecipeEntry::Render(gfx::Screen *screen, int32_t x, int32_t y,
                         bool /*is_selected*/) const {
  if (!IsVisible() || !screen || !item_)
    return;
  const bool craftable = recipe_ && recipe_->GetCanCraft();
  gfx::Font::Draw(ToString(), screen, x, y,
                  craftable ? kColSelected : kColUnselected);
  gfx::LinkedSprite sp = item_->sprite;
  screen->Render(x, y, sp);
}

std::string RecipeEntry::ToString() const {
  std::string s = ItemEntry::ToString();
  if (recipe_ && recipe_->Amount() > 1) {
    s += " x" + std::to_string(recipe_->Amount());
  }
  return s;
}

} // namespace minicraft::screen
