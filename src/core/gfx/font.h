#ifndef MINICRAFT_CORE_GFX_FONT_H_
#define MINICRAFT_CORE_GFX_FONT_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/gfx/color.h"
#include "core/gfx/screen.h"

namespace minicraft {
namespace gfx {

class FontStyle;

class Font {
 public:
  static constexpr int32_t kCharSize = 8;

  static void Draw(std::string_view msg, Screen* screen, int32_t x, int32_t y,
                   int32_t white_tint = -1);

  static void DrawColor(std::string_view message, Screen* screen, int32_t x,
                        int32_t y);

  static void DrawBackground(std::string_view msg, Screen* screen, int32_t x,
                             int32_t y, int32_t white_tint = -1);

  static int32_t TextWidth(std::string_view text);
  static int32_t TextWidth(const std::vector<std::string>& para);
  static int32_t TextWidth(std::span<const std::string_view> para);

  static constexpr int32_t TextHeight() { return kCharSize; }

  static void DrawCentered(std::string_view msg, Screen* screen, int32_t y,
                           int32_t col);

  static void DrawParagraph(std::string_view para, Screen* screen,
                            FontStyle style, int32_t line_spacing);
  static void DrawParagraph(std::string_view para, Screen* screen, int32_t w,
                            int32_t h, FontStyle style, int32_t line_spacing);
  static void DrawParagraph(const std::vector<std::string>& lines,
                            Screen* screen, FontStyle style,
                            int32_t line_spacing);

  static std::vector<std::string> GetLines(std::string_view para, int32_t w,
                                           int32_t h, int32_t line_spacing,
                                           bool keep_empty_remainder = false);

  // Decodes the next UTF-8 codepoint from sv and advances offset.
  static char32_t NextUtf8Codepoint(std::string_view sv, size_t* offset);

 private:
  static int32_t LookupGlyph(char32_t cp);
  static size_t GetLineSplitByteOffset(std::string_view text,
                                       int32_t max_width);
};

}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_FONT_H_
