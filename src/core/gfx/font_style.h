#ifndef MINICRAFT_CORE_GFX_FONT_STYLE_H_
#define MINICRAFT_CORE_GFX_FONT_STYLE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/gfx/color.h"
#include "core/gfx/geometry.h"
#include "core/gfx/screen.h"

namespace minicraft {
namespace gfx {

class FontStyle {
 public:
  FontStyle() : FontStyle(color::kWhite) {}
  explicit FontStyle(int32_t main_color);

  void Draw(std::string_view msg, Screen* screen) const;

  void ConfigureForParagraph(const std::vector<std::string>& para,
                             int32_t spacing);
  void SetupParagraphLine(const std::vector<std::string>& para, size_t line,
                          int32_t spacing);
  void DrawParagraphLine(const std::vector<std::string>& para, size_t line,
                         int32_t spacing, Screen* screen);

  FontStyle& SetColor(int32_t col);
  FontStyle& SetXPos(int32_t pos, bool reset_alignment = true);
  FontStyle& SetYPos(int32_t pos, bool reset_alignment = true);
  FontStyle& SetAnchor(int32_t x, int32_t y);
  FontStyle& SetRelTextPos(RelPos rel_pos, bool set_both = true);
  FontStyle& SetRelLinePos(RelPos rel_pos);
  FontStyle& SetShadowType(int32_t color, bool full);
  FontStyle& SetShadowType(int32_t color, std::string type);

  int32_t GetColor() const { return main_color_; }

 private:
  int32_t main_color_;
  int32_t shadow_color_;
  std::string shadow_type_;
  Point anchor_;
  RelPos rel_text_pos_ = RelPos::kCenter;
  RelPos rel_line_pos_ = RelPos::kCenter;

  std::vector<std::string> configured_para_;
  bool has_configured_para_ = false;
  Rectangle para_bounds_;
  int32_t pad_x_ = 0;
  int32_t pad_y_ = 0;
};

}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_FONT_STYLE_H_
