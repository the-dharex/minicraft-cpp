#include "core/screen/menu.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/input_handler.h"
#include "core/io/sound.h"
#include "core/platform/input_types.h"
#include "core/util/localization.h"

namespace minicraft::screen {

namespace {

constexpr int32_t kMaxSearcherChars = 22;

int32_t Utf8Length(std::string_view sv) {
  int32_t count = 0;
  size_t off = 0;
  while (off < sv.size()) {
    gfx::Font::NextUtf8Codepoint(sv, &off);
    ++count;
  }
  return count;
}

gfx::Dimension AddInsets(gfx::Dimension d, const gfx::Insets& in) {
  return {d.width + in.left + in.right, d.height + in.top + in.bottom};
}

gfx::Dimension SubtractInsets(gfx::Dimension d, const gfx::Insets& in) {
  return {std::max<int32_t>(0, d.width - in.left - in.right),
          std::max<int32_t>(0, d.height - in.top - in.bottom)};
}

gfx::Rectangle SubtractInsets(const gfx::Rectangle& r, const gfx::Insets& in) {
  return gfx::Rectangle(r.Left() + in.left, r.Top() + in.top,
                        std::max<int32_t>(0, r.Width() - in.left - in.right),
                        std::max<int32_t>(0, r.Height() - in.top - in.bottom));
}

std::string ToLowerAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Menu::Builder
// ---------------------------------------------------------------------------
Menu::Builder::Builder(bool has_frame, int32_t entry_spacing,
                       gfx::RelPos entry_pos)
    : has_frame_(has_frame),
      spacing_(entry_spacing),
      entry_pos_(entry_pos),
      anchor_(gfx::Screen::w / 2, gfx::Screen::h / 2) {}

Menu::Builder::Builder(bool has_frame, int32_t entry_spacing,
                       gfx::RelPos entry_pos,
                       std::vector<std::unique_ptr<ListEntry>> entries)
    : has_frame_(has_frame),
      spacing_(entry_spacing),
      entry_pos_(entry_pos),
      entries_(std::move(entries)),
      anchor_(gfx::Screen::w / 2, gfx::Screen::h / 2) {}

Menu::Builder& Menu::Builder::SetEntries(
    std::vector<std::unique_ptr<ListEntry>> entries) {
  entries_ = std::move(entries);
  return *this;
}

Menu::Builder& Menu::Builder::AddEntry(std::unique_ptr<ListEntry> entry) {
  if (entry) entries_.push_back(std::move(entry));
  return *this;
}

Menu::Builder& Menu::Builder::SetPositioning(gfx::Point anchor,
                                             gfx::RelPos menu_pos) {
  anchor_ = anchor;
  menu_pos_ = menu_pos;
  return *this;
}

Menu::Builder& Menu::Builder::SetSize(int32_t width, int32_t height) {
  has_menu_size_ = true;
  menu_size_ = {width, height};
  return *this;
}

Menu::Builder& Menu::Builder::SetMenuSize(gfx::Dimension d) {
  has_menu_size_ = true;
  menu_size_ = d;
  return *this;
}

Menu::Builder& Menu::Builder::SetBounds(const gfx::Rectangle& rect) {
  has_menu_size_ = true;
  menu_size_ = rect.Size();
  SetPositioning(rect.Center(), gfx::RelPos::kCenter);
  return *this;
}

Menu::Builder& Menu::Builder::SetDisplayLength(int32_t num_entries) {
  display_length_ = num_entries;
  return *this;
}

Menu::Builder& Menu::Builder::SetTitlePos(gfx::RelPos rp) {
  title_pos_ = rp;
  return *this;
}

Menu::Builder& Menu::Builder::SetTitle(std::string title) {
  title_ = std::move(title);
  return *this;
}

Menu::Builder& Menu::Builder::SetTitle(std::string title, int32_t color,
                                       bool full_color) {
  title_ = std::move(title);
  full_title_color_ = full_color;
  set_title_color_ = true;
  title_col_ = color;
  return *this;
}

Menu::Builder& Menu::Builder::SetFrame(bool has_frame) {
  has_frame_ = has_frame;
  return *this;
}

Menu::Builder& Menu::Builder::SetScrollPolicies(float padding, bool wrap) {
  padding_ = padding;
  wrap_ = wrap;
  return *this;
}

Menu::Builder& Menu::Builder::SetShouldRender(bool render) {
  should_render_ = render;
  return *this;
}

Menu::Builder& Menu::Builder::SetSelectable(bool selectable) {
  set_selectable_ = true;
  selectable_ = selectable;
  return *this;
}

Menu::Builder& Menu::Builder::SetSelection(int32_t sel) {
  selection_ = sel;
  return *this;
}

Menu::Builder& Menu::Builder::SetSelection(int32_t sel, int32_t disp_sel) {
  selection_ = sel;
  disp_selection_ = disp_sel;
  return *this;
}

Menu::Builder& Menu::Builder::SetSearcherBar(bool searcher_bar) {
  searcher_bar_ = searcher_bar;
  return *this;
}

std::unique_ptr<Menu> Menu::Builder::CreateMenu() {
  auto m = std::make_unique<Menu>();
  m->has_frame_ = has_frame_;
  m->spacing_ = spacing_;
  m->entry_pos_ = entry_pos_;
  m->entries_ = std::move(entries_);
  m->display_length_ = display_length_;
  m->wrap_ = wrap_;
  m->should_render = should_render_;
  m->selection_ = selection_;
  m->disp_selection_ = disp_selection_;
  m->use_searcher_bar_ = searcher_bar_;
  m->title_ = util::Localization::GetLocalized(title_);

  if (!set_selectable_) {
    m->selectable_ = false;
    for (const auto& entry : m->entries_) {
      if (entry && entry->IsSelectable()) {
        m->selectable_ = true;
        break;
      }
    }
  } else {
    m->selectable_ = selectable_;
  }

  m->draw_vertically_ = (title_pos_ == gfx::RelPos::kLeft ||
                         title_pos_ == gfx::RelPos::kRight);
  const gfx::Dimension title_dim =
      m->draw_vertically_
          ? gfx::Dimension(gfx::Font::TextHeight() * 2,
                           gfx::Font::TextWidth(m->title_))
          : gfx::Dimension(gfx::Font::TextWidth(m->title_),
                           gfx::Font::TextHeight() * 2);

  constexpr int32_t kBoxW = 8;
  gfx::Insets border;
  if (m->has_frame_) {
    border = gfx::Insets(kBoxW);
  } else if (!m->title_.empty() && title_pos_ != gfx::RelPos::kCenter) {
    const int32_t space = kBoxW * 2;
    const int x_idx = gfx::RelPosXIndex(title_pos_);
    const int y_idx = gfx::RelPosYIndex(title_pos_);
    if (y_idx == 0)
      border.top = space;
    else if (y_idx == 2)
      border.bottom = space;
    else if (x_idx == 0)
      border.left = space;
    else if (x_idx == 2)
      border.right = space;
  }

  if (m->selectable_) {
    border.left += kBoxW * 2;
    border.right += kBoxW * 2;
  }

  if (m->wrap_ && m->display_length_ > 0) {
    m->display_length_ =
        std::min<int32_t>(m->display_length_,
                          static_cast<int32_t>(m->entries_.size()));
  }

  gfx::Dimension entry_size;
  gfx::Dimension final_menu_size;
  if (!has_menu_size_) {
    int32_t width = title_dim.width;
    for (const auto& entry : m->entries_) {
      if (!entry) continue;
      int32_t ew = entry->GetWidth();
      if (m->selectable_ && !entry->IsSelectable()) {
        ew = std::max<int32_t>(0, ew - kBoxW * 4);
      }
      width = std::max(width, ew);
    }

    if (m->display_length_ > 0) {
      const int32_t height =
          (ListEntry::GetHeight() + m->spacing_) * m->display_length_ -
          m->spacing_;
      entry_size = {width, height};
    } else {
      const int y_idx = gfx::RelPosYIndex(menu_pos_);
      int32_t max_h = 0;
      if (y_idx == 0)
        max_h = anchor_.y;
      else if (y_idx == 2)
        max_h = gfx::Screen::h - anchor_.y;
      else
        max_h = std::min(anchor_.y, gfx::Screen::h - anchor_.y) * 2;

      max_h -= border.top + border.bottom;
      const int32_t entry_h = std::max<int32_t>(1, m->spacing_ + ListEntry::GetHeight());
      const int32_t total_h =
          entry_h * static_cast<int32_t>(m->entries_.size()) - m->spacing_;
      max_h = ((max_h + m->spacing_) / entry_h) * entry_h - m->spacing_;
      entry_size = {width, std::max<int32_t>(0, std::min(max_h, total_h))};
    }
    final_menu_size = AddInsets(entry_size, border);
    if (final_menu_size.width > gfx::Screen::w) {
      final_menu_size.width = gfx::Screen::w;
      entry_size.width =
          std::max<int32_t>(0, final_menu_size.width - border.left - border.right);
    }
    if (final_menu_size.height > gfx::Screen::h) {
      final_menu_size.height = gfx::Screen::h;
      entry_size.height =
          std::max<int32_t>(0, final_menu_size.height - border.top - border.bottom);
    }
  } else {
    final_menu_size = menu_size_;
    entry_size = SubtractInsets(final_menu_size, border);
  }

  if (!m->entries_.empty()) {
    const int32_t denom =
        std::max<int32_t>(1, ListEntry::GetHeight() + m->spacing_);
    const int32_t max_vis =
        std::max<int32_t>(1, (entry_size.height + m->spacing_) / denom);
    if (m->display_length_ <= 0) {
      m->display_length_ = max_vis;
    } else if (!has_menu_size_) {
      m->display_length_ = std::min(m->display_length_, max_vis);
    }
  }

  m->bounds_ = gfx::PositionRect(menu_pos_, final_menu_size, anchor_, nullptr);
  if (!has_menu_size_) {
    const int32_t bw = m->bounds_.Width();
    const int32_t bh = m->bounds_.Height();
    const int32_t nx = (bw <= gfx::Screen::w)
                           ? std::clamp(m->bounds_.Left(), 0, gfx::Screen::w - bw)
                           : 0;
    const int32_t ny = (bh <= gfx::Screen::h)
                           ? std::clamp(m->bounds_.Top(), 0, gfx::Screen::h - bh)
                           : 0;
    m->bounds_ = gfx::Rectangle(nx, ny, bw, bh);
  }
  m->entry_bounds_ = SubtractInsets(m->bounds_, border);
  m->title_loc_ = gfx::PositionRect(title_pos_, title_dim, m->bounds_);

  if (gfx::RelPosXIndex(title_pos_) == 0 && gfx::RelPosYIndex(title_pos_) != 1)
    m->title_loc_.x += kBoxW;
  if (gfx::RelPosXIndex(title_pos_) == 2 && gfx::RelPosYIndex(title_pos_) != 1)
    m->title_loc_.x -= kBoxW;

  if (!m->title_.empty()) {
    if (full_title_color_) {
      m->title_color_ = title_col_;
    } else {
      m->title_color_ =
          set_title_color_
              ? title_col_
              : (m->has_frame_ ? gfx::color::kYellow : gfx::color::kWhite);
    }
  }

  float clamped_pad = std::clamp(padding_, 0.0f, 1.0f);
  m->padding_ = static_cast<int32_t>(
      std::floor(clamped_pad * static_cast<float>(m->display_length_) / 2.0f));

  m->Init();
  return m;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
void Menu::Init() {
  if (entries_.empty()) {
    selection_ = 0;
    disp_selection_ = 0;
    offset_ = 0;
    return;
  }

  const int32_t n = static_cast<int32_t>(entries_.size());
  selection_ = std::clamp(selection_, 0, n - 1);

  if (!entries_[selection_]->IsSelectable()) {
    int32_t prev_sel = selection_;
    do {
      selection_ = (selection_ + 1) % n;
    } while (!entries_[selection_]->IsSelectable() && selection_ != prev_sel);
  }

  disp_selection_ =
      std::clamp(selection_, 0, std::max<int32_t>(0, display_length_ - 1));
  DoScroll();
}

void Menu::SetSelection(int32_t idx) {
  if (entries_.empty()) {
    selection_ = 0;
    return;
  }
  selection_ = std::clamp(idx, 0, static_cast<int32_t>(entries_.size()) - 1);
  DoScroll();
}

ListEntry* Menu::GetCurEntry() const {
  if (entries_.empty() || selection_ < 0 ||
      selection_ >= static_cast<int32_t>(entries_.size())) {
    return nullptr;
  }
  return entries_[selection_].get();
}

ListEntry* Menu::GetEntry(int32_t idx) const {
  if (idx < 0 || idx >= static_cast<int32_t>(entries_.size())) return nullptr;
  return entries_[idx].get();
}

void Menu::SetEntries(std::vector<std::unique_ptr<ListEntry>> entries) {
  entries_ = std::move(entries);
  if (entry_bounds_.Height() > 0 && !entries_.empty()) {
    const int32_t denom = std::max<int32_t>(1, ListEntry::GetHeight() + spacing_);
    display_length_ =
        std::max<int32_t>(1, (entry_bounds_.Height() + spacing_) / denom);
  }
  if (wrap_ && display_length_ > 0) {
    display_length_ =
        std::min<int32_t>(display_length_,
                          static_cast<int32_t>(entries_.size()));
  }
  Init();
}

void Menu::UpdateEntry(int32_t idx, std::unique_ptr<ListEntry> new_entry) {
  if (idx >= 0 && idx < static_cast<int32_t>(entries_.size()) && new_entry) {
    entries_[idx] = std::move(new_entry);
  }
}

void Menu::UpdateSelectedEntry(std::unique_ptr<ListEntry> new_entry) {
  UpdateEntry(selection_, std::move(new_entry));
}

void Menu::RemoveSelectedEntry() {
  if (entries_.empty()) return;
  if (selection_ >= 0 && selection_ < static_cast<int32_t>(entries_.size())) {
    entries_.erase(entries_.begin() + selection_);
  }
  if (entries_.empty()) {
    selection_ = 0;
    offset_ = 0;
    disp_selection_ = 0;
    return;
  }
  if (selection_ >= static_cast<int32_t>(entries_.size())) {
    selection_ = static_cast<int32_t>(entries_.size()) - 1;
  }
  DoScroll();
}

void Menu::Translate(int32_t xoff, int32_t yoff) {
  bounds_.Translate(xoff, yoff);
  entry_bounds_.Translate(xoff, yoff);
  title_loc_.Translate(xoff, yoff);
}

void Menu::Tick(io::InputHandler& input) {
  if (!selectable_ || entries_.empty()) return;

  const int32_t n = static_cast<int32_t>(entries_.size());
  const int32_t prev_sel = selection_;

  if (input.InputPressed("cursor-up")) selection_--;
  if (input.InputPressed("cursor-down")) selection_++;
  if (input.InputPressed("shift+cursor-up") && selection_searcher_ == 0) {
    selection_searcher_ -= 2;
  }
  if (input.InputPressed("shift+cursor-down") && selection_searcher_ == 0) {
    selection_searcher_ += 2;
  }
  if (prev_sel != selection_ && selection_searcher_ != 0) {
    selection_ = prev_sel;
  }

  if (use_searcher_bar_) {
    if (input.InputPressed("searcher-bar")) {
      searcher_bar_active_ = !searcher_bar_active_;
      input.AddKeyTyped("");
    }

    if (!list_searcher_.empty() && selection_searcher_ == 0) {
      int32_t speed = input.InputPressed("PAGE-UP")     ? -1
                      : input.InputPressed("PAGE-DOWN") ? 1
                                                        : 0;
      if (speed != 0) {
        const int32_t sn = static_cast<int32_t>(list_searcher_.size());
        list_position_searcher_ = (list_position_searcher_ + speed + sn) % sn;
        const int32_t pos = list_searcher_[list_position_searcher_];
        selection_searcher_ = pos - selection_;
      }
    }

    if (searcher_bar_active_) {
      std::string next_search = input.AddKeyTyped(typing_searcher_);
      input.MaskInput([](std::string_view k) { return k != "ENTER"; });

      if (Utf8Length(next_search) <= kMaxSearcherChars &&
          next_search.size() != typing_searcher_.size()) {
        typing_searcher_ = next_search;
        list_searcher_.clear();
        list_position_searcher_ = 0;

        const std::string needle = ToLowerAscii(typing_searcher_);
        bool first = true;
        for (int32_t i = 0; i < n; ++i) {
          if (!entries_[i]) continue;
          const std::string hay = ToLowerAscii(entries_[i]->ToString());
          if (hay.find(needle) != std::string::npos) {
            if (first) {
              selection_searcher_ = i - selection_;
              first = false;
            }
            list_searcher_.push_back(i);
          }
        }
      }
    }

    if (selection_searcher_ != 0) {
      const bool down = selection_searcher_ > 0;
      selection_searcher_ += down ? -1 : 1;
      selection_ += down ? 1 : -1;
    }
  }

  const int32_t delta = selection_ - prev_sel;
  selection_ = prev_sel;
  if (delta == 0) {
    if (entries_[selection_]) entries_[selection_]->Tick(input);
    return;
  }

  io::Sound::Play("select");
  do {
    selection_ = (selection_ + (delta > 0 ? 1 : -1) + n) % n;
  } while (!entries_[selection_]->IsSelectable() && selection_ != prev_sel);

  disp_selection_ += selection_ - prev_sel;
  if (display_length_ > 0) {
    disp_selection_ = std::clamp(disp_selection_, 0, display_length_ - 1);
  }
  DoScroll();
}

void Menu::DoScroll() {
  if (entries_.empty() || display_length_ <= 0) return;

  const int32_t n = static_cast<int32_t>(entries_.size());
  disp_selection_ = selection_ - offset_;
  int32_t off = offset_;

  while ((disp_selection_ < padding_ ||
          (!wrap_ && off + display_length_ > n)) &&
         (wrap_ || off > 0)) {
    off--;
    disp_selection_++;
  }

  while ((display_length_ - disp_selection_ <= padding_ ||
          (!wrap_ && off < 0)) &&
         (wrap_ || off + display_length_ < n)) {
    off++;
    disp_selection_--;
  }

  if (off < 0) off = ((off % n) + n) % n;
  if (off >= n) off %= n;
  offset_ = off;
}

void Menu::Render(gfx::Screen* screen) const {
  if (!screen || !should_render) return;
  RenderFrame(screen);

  if (!title_.empty()) {
    const gfx::SheetRect* sheet =
        screen->Linker() ? hud_sheet_.GetSheet(*screen->Linker()) : nullptr;
    if (draw_vertically_) {
      size_t off = 0;
      int32_t row = 0;
      while (off < title_.size()) {
        const size_t start = off;
        gfx::Font::NextUtf8Codepoint(title_, &off);
        const int32_t ty = title_loc_.y + row * gfx::Font::TextHeight();
        if (has_frame_ && sheet) {
          screen->Render(title_loc_.x, ty, 3, 6, 0, sheet);
        }
        gfx::Font::Draw(std::string_view(title_).substr(start, off - start),
                        screen, title_loc_.x, ty, title_color_);
        ++row;
      }
    } else {
      if (has_frame_ && sheet) {
        const int32_t sp_w = std::max<int32_t>(1, gfx::Font::TextWidth(" "));
        const int32_t cols =
            std::max<int32_t>(1, (gfx::Font::TextWidth(title_) + sp_w - 1) / sp_w);
        for (int32_t i = 0; i < cols; ++i) {
          screen->Render(title_loc_.x + i * sp_w, title_loc_.y, 3, 6, 0, sheet);
        }
      }
      gfx::Font::Draw(title_, screen, title_loc_.x, title_loc_.y, title_color_);
    }
  }

  if (searcher_bar_active_ && use_searcher_bar_) {
    const int32_t space_w = std::max<int32_t>(1, gfx::Font::TextWidth(" "));
    const int32_t search_glyphs = Utf8Length(typing_searcher_);
    const int32_t title_glyphs = Utf8Length(title_);
    int32_t leading = search_glyphs * space_w / 2;
    const int32_t x_bar = title_loc_.x + title_glyphs * 8 / 2 - 16;
    if (x_bar - leading < 0) leading += x_bar - leading;

    const gfx::SheetRect* sheet =
        screen->Linker() ? hud_sheet_.GetSheet(*screen->Linker()) : nullptr;
    if (has_frame_ && sheet) {
      for (int32_t i = 0; i < search_glyphs + 4; ++i) {
        screen->Render(x_bar + space_w * i - leading,
                       title_loc_.y - 8, 3, 6, 0, sheet);
      }
    }
    const int32_t bar_col =
        search_glyphs < kMaxSearcherChars
            ? gfx::color::kYellow
            : gfx::color::kRed;
    gfx::Font::Draw("> " + typing_searcher_ + " <", screen, x_bar - leading,
                    title_loc_.y - 8, bar_col);
  }

  if (entries_.empty()) return;

  const int32_t n = static_cast<int32_t>(entries_.size());
  int32_t y = entry_bounds_.Top();
  const bool special = wrap_ && n < display_length_;
  if (special) {
    const int32_t diff = display_length_ - n;
    y += diff * (ListEntry::GetHeight() + spacing_) / 2;
  }

  const int32_t end_i =
      wrap_ ? (offset_ + display_length_) : std::min(offset_ + display_length_, n);
  for (int32_t i = offset_; i < end_i; ++i) {
    if (special && i - offset_ >= n) break;
    const int32_t idx = ((i % n) + n) % n;
    const ListEntry* entry = entries_[idx].get();
    if (entry && dynamic_cast<const BlankEntry*>(entry) == nullptr) {
      const gfx::Rectangle row_box(entry_bounds_.Left(), y,
                                   entry_bounds_.Width(),
                                   ListEntry::GetHeight());
      gfx::Point pos = gfx::PositionRect(
          entry_pos_, gfx::Dimension(entry->GetWidth(), ListEntry::GetHeight()),
          row_box);
      const bool selected = (idx == selection_);
      if (searcher_bar_active_ && use_searcher_bar_) {
        entry->Render(screen, pos.x, pos.y, selected, typing_searcher_,
                      gfx::color::kYellow);
      } else {
        entry->Render(screen, pos.x, pos.y, selected);
      }
      if (selected && entry->IsSelectable()) {
        gfx::Font::Draw("> ", screen, pos.x - gfx::Font::TextWidth("> "), y,
                        ListEntry::kColSelected);
        gfx::Font::Draw(" <", screen, pos.x + entry->GetWidth(), y,
                        ListEntry::kColSelected);
      }
    }
    y += ListEntry::GetHeight() + spacing_;
  }
}

void Menu::RenderFrame(gfx::Screen* screen) const {
  if (!has_frame_ || !screen || !screen->Linker()) return;
  const gfx::SheetRect* sheet = hud_sheet_.GetSheet(*screen->Linker());
  if (!sheet) return;

  constexpr int32_t kBoxW = 8;
  const int32_t bottom = bounds_.Bottom() - kBoxW;
  const int32_t right = bounds_.Right() - kBoxW;

  for (int32_t y = bounds_.Top(); y <= bottom; y += kBoxW) {
    for (int32_t x = bounds_.Left(); x <= right; x += kBoxW) {
      const bool x_end = (x == bounds_.Left() || x == right);
      const bool y_end = (y == bounds_.Top() || y == bottom);
      const int32_t sprite_off =
          (x_end && y_end) ? 0 : (y_end ? 1 : (x_end ? 2 : 3));
      const int32_t mirrors = (x == right ? 1 : 0) + (y == bottom ? 2 : 0);
      screen->Render(x, y, sprite_off, 6, mirrors, sheet);
      if (x < right && x + kBoxW > right) x = right - kBoxW;
    }
    if (y < bottom && y + kBoxW > bottom) y = bottom - kBoxW;
  }
}

// ---------------------------------------------------------------------------
// OnScreenKeyboardMenu
// ---------------------------------------------------------------------------
OnScreenKeyboardMenu::OnScreenKeyboardMenu()
    : Menu(std::move(*Builder(false, 0, gfx::RelPos::kCenter)
                          .SetPositioning({0, gfx::Screen::h},
                                          gfx::RelPos::kTopRight)
                          .SetMenuSize({gfx::Screen::w, 59})
                          .CreateMenu())) {
  InitKeyboard();
}

std::unique_ptr<OnScreenKeyboardMenu> OnScreenKeyboardMenu::CheckAndCreateMenu(
    const io::InputHandler& input) {
  if (input.AnyControllerConnected()) {
    return std::make_unique<OnScreenKeyboardMenu>();
  }
  return nullptr;
}

void OnScreenKeyboardMenu::InitKeyboard() {
  const VirtualKey space{' ', false, true, false};
  const VirtualKey bs{'\b', false, false, true};
  const VirtualKey sh{'\0', true, false, false};

  keys_f_ = {
      {{'Q'}, {'W'}, {'E'}, {'R'}, {'T'}, {'Y'}, {'U'}, {'I'}, {'O'}, {'P'}},
      {{'A'}, {'S'}, {'D'}, {'F'}, {'G'}, {'H'}, {'J'}, {'K'}, {'L'}, bs},
      {sh, {'Z'}, {'X'}, {'C'}, {'V'}, {'B'}, {'N'}, {'M'}, sh},
      {{'/'}, {'?'}, space},
  };
  keys_b_ = {
      {{'1'}, {'2'}, {'3'}, {'4'}, {'5'}, {'6'}, {'7'}, {'8'}, {'9'}, {'0'}},
      {{'!'}, {'@'}, {'#'}, {'$'}, {'%'}, {'^'}, {'&'}, {'*'}, {'('}, {')'}},
      {{'-'}, {'='}, {'_'}, {'+'}, {'['}, {']'}, {'{'}, {'}'}, {'\\'}, {'|'}},
      {sh, {';'}, {':'}, {'\''}, {'\"'}, {','}, {'<'}, {'.'}, {'>'}, space},
  };
}

void OnScreenKeyboardMenu::SetShiftPressed(bool pressed) {
  shift_pressed_ = pressed;
  x_ = 0;
  y_ = pressed ? 3 : 2;
}

OnScreenKeyboardMenu::TickResult OnScreenKeyboardMenu::TickKeyboard(
    io::InputHandler& input) {
  if (key_pressed_ > 0) --key_pressed_;
  if (!visible_) return TickResult::kNone;

  auto& keys = shift_pressed_ ? keys_b_ : keys_f_;
  using B = platform::GamepadButton;
  if (input.ButtonPressed(B::kA)) {
    const VirtualKey& k = keys[y_][x_];
    if (k.is_shift) {
      SetShiftPressed(!shift_pressed_);
    } else {
      input.PushVirtualChar(static_cast<char32_t>(k.output));
    }
    io::Sound::Play("select");
    key_pressed_ = 5;
  } else if (input.ButtonPressed(B::kB)) {
    input.PushVirtualChar(static_cast<char32_t>('\b'));
    io::Sound::Play("confirm");
    return TickResult::kBackspaceActed;
  } else if (input.ButtonPressed(B::kDpadUp)) {
    if (y_ > 0) {
      --y_;
      if (x_ >= static_cast<int32_t>(keys[y_].size())) {
        x_ = static_cast<int32_t>(keys[y_].size()) - 1;
      }
      io::Sound::Play("select");
    }
    return TickResult::kActionCompleted;
  } else if (input.ButtonPressed(B::kDpadDown)) {
    if (y_ < static_cast<int32_t>(keys.size()) - 1) {
      ++y_;
      if (x_ >= static_cast<int32_t>(keys[y_].size())) {
        x_ = static_cast<int32_t>(keys[y_].size()) - 1;
      }
      io::Sound::Play("select");
    }
    return TickResult::kActionCompleted;
  } else if (input.ButtonPressed(B::kDpadLeft)) {
    if (x_ > 0) {
      --x_;
      io::Sound::Play("select");
    }
    return TickResult::kActionCompleted;
  } else if (input.ButtonPressed(B::kDpadRight)) {
    if (x_ < static_cast<int32_t>(keys[y_].size()) - 1) {
      ++x_;
      io::Sound::Play("select");
    }
    return TickResult::kActionCompleted;
  }
  return TickResult::kNone;
}

void OnScreenKeyboardMenu::SetVisible(bool visible) {
  if (visible_ != visible) {
    const int32_t h = GetBounds().Height();
    Translate(0, visible ? -h : h);
    visible_ = visible;
  }
  key_pressed_ = 0;
}

void OnScreenKeyboardMenu::Render(gfx::Screen* screen) const {
  if (!screen) return;
  Menu::Render(screen);

  const gfx::Rectangle& b = GetBounds();
  const int32_t width = b.Width();
  const int32_t height = b.Height();
  const int32_t top = b.Top();
  screen->FillRect(0, top, width, height, gfx::color::Get(1, 0xCF, 0xCF, 0xCF));
  screen->FillRect(0, top, width, 2, gfx::color::Get(1, 0xEF, 0xEF, 0xEF));

  constexpr int32_t kKeyH = 14;
  constexpr int32_t kKeyW = 16;
  const auto& keys = shift_pressed_ ? keys_b_ : keys_f_;
  const gfx::SheetRect* sheet =
      screen->Linker() ? osk_sheet_.GetSheet(*screen->Linker()) : nullptr;

  for (int32_t r = 0; r < static_cast<int32_t>(keys.size()); ++r) {
    int32_t total_len = static_cast<int32_t>(keys[r].size()) * kKeyW;
    for (const auto& k : keys[r]) {
      if (k.is_space) total_len += kKeyW * 2;
      if (k.is_shift) total_len += kKeyW;
    }
    int32_t x = (gfx::Screen::w - total_len) / 2;
    const int32_t y = top + 2 + r * kKeyH;
    for (int32_t c = 0; c < static_cast<int32_t>(keys[r].size()); ++c) {
      const auto& k = keys[r][c];
      int32_t color = (key_pressed_ > 0 && r == y_ && c == x_)
                          ? gfx::color::Get(1, 0xEF, 0xEF, 0xF0)
                          : gfx::color::Get(1, 0xFD, 0xFD, 0xFD);
      if (k.is_backspace && sheet) {
        screen->RenderSub(x, y, 0, 0, kKeyW, kKeyH, sheet, 0, -1, false, color);
      } else if (k.is_shift && sheet) {
        screen->RenderSub(x, y, kKeyW, 0, kKeyW, kKeyH, sheet, 0, -1, false,
                          color);
      } else if (k.is_space && sheet) {
        screen->RenderSub(x, y, 0, kKeyH, kKeyW, kKeyH, sheet, 0, -1, false,
                          color);
      } else {
        std::string label =
            k.is_backspace ? "<"
                           : (k.is_shift ? "^" : (k.is_space ? "_" : std::string(1, k.output)));
        gfx::Font::Draw(label, screen, x + kKeyW / 2 - 3, y + kKeyH / 2 - 3,
                        color);
      }

      screen->DrawRect(x, y, kKeyW, kKeyH, gfx::color::Get(1, 0xBC, 0xBC, 0xBC));
      if (x_ == c && y_ == r) {
        const int32_t hi = (key_pressed_ > 0)
                               ? gfx::color::Get(1, 0xEF, 0xEF, 0xF0)
                               : gfx::color::Get(1, 0xDF, 0xDF, 0xE0);
        screen->DrawRect(x + 1, y + 1, kKeyW - 2, kKeyH - 2, hi);
      }
      x += kKeyW;
    }
  }
}

}  // namespace minicraft::screen
