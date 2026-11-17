#include "core/gfx/color.h"

#include <algorithm>

namespace minicraft {
namespace gfx {
namespace color {

namespace {

int Limit(int v, int lo, int hi) { return std::clamp(v, lo, hi); }

int TintByte(int rgb_byte, int amount) {
  if (rgb_byte == 255) return 255;
  int r = Limit((rgb_byte / 36) % 6 + amount, 0, 5);
  int g = Limit((rgb_byte / 6) % 6 + amount, 0, 5);
  int b = Limit(rgb_byte % 6 + amount, 0, 5);
  return r * 36 + g * 6 + b;
}

}  // namespace

std::string ToStringCode(int32_t col) {
  // Store as § + 4 hex-safe or raw chars
  static constexpr char kHex[] = "0123456789ABCDEF";
  uint32_t u = static_cast<uint32_t>(col);
  std::string out = kColorMarker;
  for (int shift = 28; shift >= 0; shift -= 4) {
    out.push_back(kHex[(u >> shift) & 0xFU]);
  }
  return out;
}

int32_t FromStringCode(std::string_view code) {
  // Supports 8 hex digits after optional §.
  size_t pos = 0;
  if (code.size() >= 2 && code[0] == '\xC2' && code[1] == '\xA7') {
    pos = 2;
  }
  uint32_t val = 0;
  for (size_t i = pos; i < code.size() && i < pos + 8; ++i) {
    char c = code[i];
    uint32_t nibble = 0;
    if (c >= '0' && c <= '9') nibble = static_cast<uint32_t>(c - '0');
    else if (c >= 'A' && c <= 'F') nibble = static_cast<uint32_t>(c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') nibble = static_cast<uint32_t>(c - 'a' + 10);
    val = (val << 4) | nibble;
  }
  return static_cast<int32_t>(val);
}

int32_t RgbReadable(int red, int green, int blue) {
  red = Limit(red, 0, 250);
  green = Limit(green, 0, 250);
  blue = Limit(blue, 0, 250);
  return (red / 50) * 100 + (green / 50) * 10 + (blue / 50);
}

int32_t TintColor(int32_t rgb_int, int amount) {
  if (rgb_int < 0) return rgb_int;
  int r = Limit(((rgb_int >> 16) & 0xFF) + amount, 0, 255);
  int g = Limit(((rgb_int >> 8) & 0xFF) + amount, 0, 255);
  int b = Limit((rgb_int & 0xFF) + amount, 0, 255);
  return (r << 16) | (g << 8) | b;
}

int32_t TintEncoded(int32_t col, int amount, bool is_sprite_col) {
  if (!is_sprite_col) return TintByte(col, amount);
  int a = TintByte((col >> 24) & 0xFF, amount);
  int b = TintByte((col >> 16) & 0xFF, amount);
  int c = TintByte((col >> 8) & 0xFF, amount);
  int d = TintByte(col & 0xFF, amount);
  return (a << 24) | (b << 16) | (c << 8) | d;
}

}  // namespace color
}  // namespace gfx
}  // namespace minicraft
