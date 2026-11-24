#ifndef MINICRAFT_CORE_GFX_COLOR_H_
#define MINICRAFT_CORE_GFX_COLOR_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace minicraft {
namespace gfx {

namespace color {

// Minicraft 25-bit color helpers (a in {0, 1}, r,g,b in 0..255).
inline constexpr int32_t Get(int a, int r, int g, int b) {
  return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) |
         (b & 0xFF);
}

inline constexpr int32_t Get(int a, int copy) {
  return Get(a, copy, copy, copy);
}

inline constexpr int32_t kTransparent = Get(0, 0);
inline constexpr int32_t kWhite = Get(1, 255);
inline constexpr int32_t kGray = Get(1, 153);
inline constexpr int32_t kDarkGray = Get(1, 51);
inline constexpr int32_t kBlack = Get(1, 0);
inline constexpr int32_t kRed = Get(1, 198, 44, 44);
inline constexpr int32_t kGreen = Get(1, 77, 212, 77);
inline constexpr int32_t kBlue = Get(1, 32, 32, 136);
inline constexpr int32_t kYellow = Get(1, 255, 255, 0);
inline constexpr int32_t kMagenta = Get(1, 255, 0, 255);
inline constexpr int32_t kCyan = Get(1, 90, 204, 204);

// UTF-8 encoding of '\u00A7' (section sign) used in color-coded strings.
inline constexpr char kColorMarker[] = "\xC2\xA7";

// Converts 25-bit Minicraft color to 0xRRGGBBAA for platform render backend.
inline constexpr uint32_t ToRgba(int32_t minicraft_col) {
  uint32_t r = static_cast<uint32_t>((minicraft_col >> 16) & 0xFF);
  uint32_t g = static_cast<uint32_t>((minicraft_col >> 8) & 0xFF);
  uint32_t b = static_cast<uint32_t>(minicraft_col & 0xFF);
  return (r << 24) | (g << 16) | (b << 8) | 0xFFU;
}

// Converts a 24-bit Java RGB int (e.g., from Font/Color) into 0xRRGGBBAA.
inline constexpr uint32_t RgbIntToRgba(int32_t rgb) {
  return ToRgba(rgb);
}

inline constexpr float GetLightnessFromRgb(int32_t rgb) {
  int r = (rgb >> 16) & 0xFF;
  int g = (rgb >> 8) & 0xFF;
  int b = rgb & 0xFF;
  int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  return static_cast<float>(mx + mn) / 510.0f;
}

// Encodes a color as a 4-byte ASCII/UTF-8 marker sequence matching Color.toStringCode.
std::string ToStringCode(int32_t col);
int32_t FromStringCode(std::string_view code);

int32_t RgbReadable(int red, int green, int blue);
int32_t TintColor(int32_t rgb_int, int amount);
int32_t TintEncoded(int32_t col, int amount, bool is_sprite_col);

}  // namespace color
}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_COLOR_H_
