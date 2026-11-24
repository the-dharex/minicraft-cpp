#include "core/gfx/font_style.h"

#include <utility>

#include "core/gfx/font.h"
#include "core/util/logging.h"

namespace minicraft {
namespace gfx {

namespace {

constexpr int32_t kShadowPosMap[16] = {
    0,  1, 1, 1, 0, -1, -1, -1,  // X offsets
    -1, -1, 0, 1, 1,  1,  0, -1   // Y offsets
};

}  // namespace

FontStyle::FontStyle(int32_t main_color)
    : main_color_(main_color),
      shadow_color_(color::Get(-1, -1)),
      anchor_(Screen::w / 2, Screen::h / 2) {}

void FontStyle::Draw(std::string_view msg, Screen* screen) const {
  Dimension size(Font::TextWidth(msg), Font::TextHeight());
  Rectangle text_bounds;
  PositionRect(rel_text_pos_, size, anchor_, &text_bounds);

  if (pad_x_ != 0 || pad_y_ != 0) {
    size.width += pad_x_;
    size.height += pad_y_;
    Rectangle text_box;
    PositionRect(rel_text_pos_, size, anchor_, &text_box);
    PositionRect(rel_line_pos_, text_bounds.Size(), text_box, &text_bounds);
  }

  int32_t x_pos = text_bounds.Left();
  int32_t y_pos = text_bounds.Top();

  for (size_t i = 0; i < 8 && i < shadow_type_.size(); ++i) {
    if (shadow_type_[i] == '1') {
      Font::Draw(msg, screen, x_pos + kShadowPosMap[i],
                 y_pos + kShadowPosMap[i + 8], shadow_color_);
    }
  }

  Font::Draw(msg, screen, x_pos, y_pos, main_color_);
}

void FontStyle::ConfigureForParagraph(const std::vector<std::string>& para,
                                      int32_t spacing) {
  configured_para_ = para;
  has_configured_para_ = true;

  Dimension size(Font::TextWidth(para),
                 static_cast<int32_t>(para.size()) *
                     (Font::TextHeight() + spacing));
  PositionRect(rel_text_pos_, size, anchor_, &para_bounds_);
}

void FontStyle::SetupParagraphLine(const std::vector<std::string>& para,
                                   size_t line, int32_t spacing) {
  if (line >= para.size()) {
    util::log::kGameHandler.Error("FontStyle: Paragraph line index out of bounds.");
    return;
  }

  if (!has_configured_para_ || configured_para_ != para) {
    ConfigureForParagraph(para, spacing);
  }

  Rectangle text_area = para_bounds_;
  text_area.SetSize(text_area.Width(), Font::TextHeight() + spacing,
                    RelPos::kTopLeft);
  text_area.Translate(0, static_cast<int32_t>(line) * text_area.Height());

  anchor_ = text_area.Position(GetOppositeRelPos(rel_text_pos_));
  pad_x_ = para_bounds_.Width() - Font::TextWidth(para[line]);
  pad_y_ = spacing;
}

void FontStyle::DrawParagraphLine(const std::vector<std::string>& para,
                                  size_t line, int32_t spacing,
                                  Screen* screen) {
  SetupParagraphLine(para, line, spacing);
  Draw(para[line], screen);
  pad_x_ = 0;
  pad_y_ = 0;
}

FontStyle& FontStyle::SetColor(int32_t col) {
  main_color_ = col;
  return *this;
}

FontStyle& FontStyle::SetXPos(int32_t pos, bool reset_alignment) {
  anchor_.x = pos;
  if (reset_alignment) {
    rel_text_pos_ =
        GetRelPos(RelPosXIndex(RelPos::kRight), RelPosYIndex(rel_text_pos_));
    rel_line_pos_ =
        GetRelPos(RelPosXIndex(RelPos::kLeft), RelPosYIndex(rel_line_pos_));
  }
  return *this;
}

FontStyle& FontStyle::SetYPos(int32_t pos, bool reset_alignment) {
  anchor_.y = pos;
  if (reset_alignment) {
    rel_text_pos_ =
        GetRelPos(RelPosXIndex(rel_text_pos_), RelPosYIndex(RelPos::kBottom));
    rel_line_pos_ =
        GetRelPos(RelPosXIndex(rel_line_pos_), RelPosYIndex(RelPos::kTop));
  }
  return *this;
}

FontStyle& FontStyle::SetAnchor(int32_t x, int32_t y) {
  anchor_ = Point(x, y);
  return *this;
}

FontStyle& FontStyle::SetRelTextPos(RelPos rel_pos, bool set_both) {
  rel_text_pos_ = rel_pos;
  if (set_both) {
    rel_line_pos_ = GetOppositeRelPos(rel_text_pos_);
  }
  return *this;
}

FontStyle& FontStyle::SetRelLinePos(RelPos rel_pos) {
  rel_line_pos_ = rel_pos;
  return *this;
}

FontStyle& FontStyle::SetShadowType(int32_t color, bool full) {
  return SetShadowType(color, full ? std::string("10101010")
                                   : std::string("00010000"));
}

FontStyle& FontStyle::SetShadowType(int32_t color, std::string type) {
  shadow_color_ = color;
  shadow_type_ = std::move(type);
  return *this;
}

}  // namespace gfx
}  // namespace minicraft
