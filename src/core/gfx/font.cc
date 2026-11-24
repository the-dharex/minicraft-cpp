#include "core/gfx/font.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

#include "core/gfx/font_style.h"

namespace minicraft {
namespace gfx {

namespace {

// glyph ordering in assets/textures/gui/font.png (32 glyphs per row).
constexpr const char kFontCharsUtf8[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"
    "6789.,!?'\"-+=/\\%()<>:;^@\xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A\xC3\x91\xC2\xBF\xC2\xA1"
    "\xC3\x83\xC3\x8A\xC3\x87\xC3\x94\xC3\x95\xC4\x9E\xC3\x87\xC3\x9C\xC4\xB0\xC3\x96\xC5\x9E\xC3\x86\xC3\x98\xC3\x85\xC5\xB0\xC5\x90[]#|{}_\xD0\x90\xD0\x91\xD0\x92\xD0\x93\xD0\x94\xD0\x95\xD0\x81\xD0\x96\xD0\x97"
    "\xD0\x98\xD0\x99\xD0\x9A\xD0\x9B\xD0\x9C\xD0\x9D\xD0\x9E\xD0\x9F\xD0\xA0\xD0\xA1\xD0\xA2\xD0\xA3\xD0\xA4\xD0\xA5\xD0\xA6\xD0\xA7\xD0\xA8\xD0\xA9\xD0\xAA\xD0\xAB\xD0\xAC\xD0\xAD\xD0\xAE\xD0\xAF\xC3\x80\xC3\x82\xC3\x84\xC3\x88\xC3\x8E\xC3\x8C\xC3\x8F\xC3\x92"
    "\xC3\x99\xC3\x9B\xC3\x9D*\xC2\xAB\xC2\xBB\xC2\xA3$&\xE2\x82\xAC\xC2\xA7\xC2\xAA\xC2\xBA"
    "abcdefghijklmnopqrs"
    "tuvwxyz\xC3\xA1\xC3\xA0\xC3\xA3\xC3\xA2\xC3\xA4\xC3\xA9\xC3\xA8\xC3\xAA\xC3\xAB\xC3\xAD\xC3\xAC\xC3\xAE\xC3\xAF\xC3\xB3\xC3\xB2\xC3\xB5\xC3\xB4\xC3\xB6\xC3\xBA\xC3\xB9\xC3\xBB\xC3\xBC\xC3\xA7\xC3\xB1\xC3\xBD"
    "\xC3\xBF\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3\xD0\xB4\xD0\xB5\xD1\x91\xD0\xB6\xD0\xB7\xD0\xB8\xD0\xB9\xD0\xBA\xD0\xBB\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xBF\xD1\x80\xD1\x81\xD1\x82\xD1\x83\xD1\x84\xD1\x85\xD1\x86\xD1\x87\xD1\x88\xD1\x89\xD1\x8A\xD1\x8B\xD1\x8C\xD1\x8D"
    "\xD1\x8E\xD1\x8F\xC4\xB1\xC5\x9F\xC5\x91\xE2\x9C\x93";

struct GlyphTable {
  std::array<int16_t, 128> ascii{};
  std::unordered_map<char32_t, int32_t> extra;

  GlyphTable() {
    ascii.fill(-1);
    std::string_view sv(kFontCharsUtf8);
    size_t off = 0;
    int32_t idx = 0;
    while (off < sv.size()) {
      char32_t cp = Font::NextUtf8Codepoint(sv, &off);
      if (cp < 128) {
        if (ascii[cp] < 0) ascii[cp] = static_cast<int16_t>(idx);
      } else {
        extra.emplace(cp, idx);
      }
      ++idx;
    }
  }
};

const GlyphTable& GetGlyphTable() {
  static const GlyphTable kTable;
  return kTable;
}

bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

bool HasColorCodeAt(std::string_view sv, size_t pos, size_t* code_len) {
  if (pos + 2 <= sv.size() && sv[pos] == '\xC2' && sv[pos + 1] == '\xA7') {
    if (pos + 10 <= sv.size()) {
      bool all_hex = true;
      for (size_t i = 0; i < 8; ++i) {
        if (!IsHexDigit(sv[pos + 2 + i])) {
          all_hex = false;
          break;
        }
      }
      if (all_hex) {
        *code_len = 10;
        return true;
      }
    }
    *code_len = 2;
    return true;
  }
  return false;
}

int32_t TextWidthSingleSegment(std::string_view text) {
  int32_t count = 0;
  size_t off = 0;
  while (off < text.size()) {
    Font::NextUtf8Codepoint(text, &off);
    ++count;
  }
  return count * Font::kCharSize;
}

}  // namespace

char32_t Font::NextUtf8Codepoint(std::string_view sv, size_t* offset) {
  size_t i = *offset;
  if (i >= sv.size()) return 0;
  uint8_t b0 = static_cast<uint8_t>(sv[i]);
  if (b0 < 0x80) {
    *offset = i + 1;
    return b0;
  }
  if ((b0 & 0xE0) == 0xC0 && i + 1 < sv.size()) {
    uint8_t b1 = static_cast<uint8_t>(sv[i + 1]);
    if ((b1 & 0xC0) == 0x80) {
      *offset = i + 2;
      return ((static_cast<char32_t>(b0 & 0x1F) << 6) |
              static_cast<char32_t>(b1 & 0x3F));
    }
  } else if ((b0 & 0xF0) == 0xE0 && i + 2 < sv.size()) {
    uint8_t b1 = static_cast<uint8_t>(sv[i + 1]);
    uint8_t b2 = static_cast<uint8_t>(sv[i + 2]);
    if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
      *offset = i + 3;
      return ((static_cast<char32_t>(b0 & 0x0F) << 12) |
              (static_cast<char32_t>(b1 & 0x3F) << 6) |
              static_cast<char32_t>(b2 & 0x3F));
    }
  } else if ((b0 & 0xF8) == 0xF0 && i + 3 < sv.size()) {
    uint8_t b1 = static_cast<uint8_t>(sv[i + 1]);
    uint8_t b2 = static_cast<uint8_t>(sv[i + 2]);
    uint8_t b3 = static_cast<uint8_t>(sv[i + 3]);
    if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
      *offset = i + 4;
      return ((static_cast<char32_t>(b0 & 0x07) << 18) |
              (static_cast<char32_t>(b1 & 0x3F) << 12) |
              (static_cast<char32_t>(b2 & 0x3F) << 6) |
              static_cast<char32_t>(b3 & 0x3F));
    }
  }
  *offset = i + 1;
  return 0xFFFD;
}

int32_t Font::LookupGlyph(char32_t cp) {
  const auto& table = GetGlyphTable();
  if (cp < 128) return table.ascii[cp];
  auto it = table.extra.find(cp);
  return (it != table.extra.end()) ? it->second : -1;
}

void Font::Draw(std::string_view msg, Screen* screen, int32_t x, int32_t y,
                int32_t white_tint) {
  if (!screen || !screen->Linker()) return;
  const SheetRect* sheet = screen->Linker()->GetSheet(SpriteType::kGui, "font");
  if (!sheet) return;

  size_t off = 0;
  int32_t cursor_x = x;
  while (off < msg.size()) {
    char32_t cp = NextUtf8Codepoint(msg, &off);
    int32_t ix = LookupGlyph(cp);
    if (ix >= 0) {
      screen->Render(cursor_x, y, ix % 32, ix / 32, 0, sheet, white_tint);
    }
    cursor_x += kCharSize;
  }
}

void Font::DrawColor(std::string_view message, Screen* screen, int32_t x,
                     int32_t y) {
  if (message.empty()) return;
  int32_t cur_color = color::kWhite;
  int32_t leading = 0;
  size_t pos = 0;

  while (pos < message.size()) {
    size_t code_len = 0;
    if (HasColorCodeAt(message, pos, &code_len)) {
      if (code_len == 10) {
        cur_color = color::FromStringCode(message.substr(pos, 10));
      } else {
        cur_color = color::kWhite;
      }
      pos += code_len;
      continue;
    }

    size_t next_marker = message.find(color::kColorMarker, pos);
    std::string_view segment = (next_marker == std::string_view::npos)
                                   ? message.substr(pos)
                                   : message.substr(pos, next_marker - pos);
    Draw(segment, screen, x + leading, y, cur_color);
    leading += TextWidthSingleSegment(segment);
    pos += segment.size();
  }
}

void Font::DrawBackground(std::string_view msg, Screen* screen, int32_t x,
                          int32_t y, int32_t white_tint) {
  if (!screen || !screen->Linker()) return;
  const SheetRect* hud = screen->Linker()->GetSheet(SpriteType::kGui, "hud");
  if (hud) {
    size_t off = 0;
    int32_t cursor_x = x;
    while (off < msg.size()) {
      NextUtf8Codepoint(msg, &off);
      screen->Render(cursor_x, y, 5, 2, 0, hud);
      cursor_x += kCharSize;
    }
  }
  Draw(msg, screen, x, y, white_tint);
}

int32_t Font::TextWidth(std::string_view text) {
  int32_t visible_chars = 0;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t code_len = 0;
    if (HasColorCodeAt(text, pos, &code_len)) {
      pos += code_len;
      continue;
    }
    NextUtf8Codepoint(text, &pos);
    ++visible_chars;
  }
  return visible_chars * kCharSize;
}

int32_t Font::TextWidth(const std::vector<std::string>& para) {
  int32_t max_w = 0;
  for (const auto& line : para) {
    max_w = std::max(max_w, TextWidth(line));
  }
  return max_w;
}

int32_t Font::TextWidth(std::span<const std::string_view> para) {
  int32_t max_w = 0;
  for (std::string_view line : para) {
    max_w = std::max(max_w, TextWidth(line));
  }
  return max_w;
}

void Font::DrawCentered(std::string_view msg, Screen* screen, int32_t y,
                        int32_t col) {
  FontStyle(col).SetYPos(y).Draw(msg, screen);
}

void Font::DrawParagraph(std::string_view para, Screen* screen, FontStyle style,
                         int32_t line_spacing) {
  DrawParagraph(para, screen, Screen::w, Screen::h, style, line_spacing);
}

void Font::DrawParagraph(std::string_view para, Screen* screen, int32_t w,
                         int32_t h, FontStyle style, int32_t line_spacing) {
  auto lines = GetLines(para, w, h, line_spacing);
  DrawParagraph(lines, screen, style, line_spacing);
}

void Font::DrawParagraph(const std::vector<std::string>& lines, Screen* screen,
                         FontStyle style, int32_t line_spacing) {
  for (size_t i = 0; i < lines.size(); ++i) {
    style.DrawParagraphLine(lines, i, line_spacing, screen);
  }
}

size_t Font::GetLineSplitByteOffset(std::string_view text, int32_t max_width) {
  if (max_width <= 0 || text.empty()) return 0;

  // Normalize " ?\n ?" equivalent cleanly while tracking byte offsets in the
  // original string. First locate earliest '\n' or fit words separated by ' '.
  size_t pos = 0;
  int32_t cur_width = 0;
  size_t last_good_end = 0;
  bool first_word = true;

  while (pos <= text.size()) {
    // Check for explicit newline at current boundary.
    if (pos < text.size() && text[pos] == '\n') {
      return first_word ? pos : last_good_end;
    }

    size_t word_start = pos;
    size_t word_end = word_start;
    while (word_end < text.size() && text[word_end] != ' ' &&
           text[word_end] != '\n') {
      ++word_end;
    }

    std::string_view word = text.substr(word_start, word_end - word_start);
    int32_t word_w = TextWidth(word);

    if (first_word) {
      if (word_w > max_width) {
        // Word itself exceeds max_width: split by codepoint inside the word.
        size_t cp_off = 0;
        size_t fit_end = 0;
        while (cp_off < word.size()) {
          size_t prev = cp_off;
          NextUtf8Codepoint(word, &cp_off);
          if (TextWidth(word.substr(0, cp_off)) > max_width) {
            return prev > 0 ? prev : cp_off;
          }
          fit_end = cp_off;
        }
        return fit_end;
      }
      cur_width = word_w;
      last_good_end = word_end;
      first_word = false;
    } else {
      int32_t add_w = kCharSize + word_w;  // " " + word
      if (cur_width + add_w > max_width) {
        return last_good_end;
      }
      cur_width += add_w;
      last_good_end = word_end;
    }

    if (word_end >= text.size()) {
      return text.size();
    }
    if (text[word_end] == '\n') {
      // Java's replaceAll(" ?\n ?", " \n ") drops an adjacent space before '\n'
      return word_end;
    }
    pos = word_end + 1;  // Skip single space.
  }

  return last_good_end;
}

std::vector<std::string> Font::GetLines(std::string_view para, int32_t w,
                                        int32_t h, int32_t line_spacing,
                                        bool keep_empty_remainder) {
  std::vector<std::string> lines;
  int32_t height = TextHeight();
  std::string_view rem = para;

  while (!rem.empty()) {
    size_t split = GetLineSplitByteOffset(rem, w);
    lines.emplace_back(rem.substr(0, split));

    if (split < rem.size() && rem[split] == ' ') {
      ++split;
      if (split < rem.size() && rem[split] == '\n') {
        ++split;
      }
    } else if (split < rem.size() && rem[split] == '\n') {
      ++split;
    }
    rem = rem.substr(split);

    height += line_spacing + TextHeight();
    if (height > h) break;
  }

  if (!rem.empty() || keep_empty_remainder) {
    lines.emplace_back(rem);
  }

  return lines;
}

}  // namespace gfx
}  // namespace minicraft
