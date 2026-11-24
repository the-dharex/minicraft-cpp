#ifndef MINICRAFT_CORE_GFX_SCREEN_H_
#define MINICRAFT_CORE_GFX_SCREEN_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/gfx/geometry.h"
#include "core/gfx/sprite_atlas.h"
#include "core/platform/render_backend.h"

namespace minicraft {
namespace gfx {

class Screen {
 public:
  static constexpr int32_t w = platform::kScreenWidth;
  static constexpr int32_t h = platform::kScreenHeight;
  static constexpr Point center{w / 2, h / 2};
  static constexpr int32_t kMaxDark = 128;

  static constexpr int32_t kBitMirrorX = 0x01;
  static constexpr int32_t kBitMirrorY = 0x02;

  explicit Screen(SpriteLinker* linker);

  void Clear(int32_t color);
  void SetOffset(int32_t x_offset, int32_t y_offset);
  int32_t XOffset() const { return x_offset_; }
  int32_t YOffset() const { return y_offset_; }

  // Tile-aligned (8x8) sprite rendering with level offset subtracted.
  void Render(int32_t xp, int32_t yp, int32_t xt, int32_t yt, int32_t bits,
              const SheetRect* sheet, int32_t white_tint = -1,
              bool fullbright = false, int32_t solid_color = 0);

  void Render(int32_t xp, int32_t yp, LinkedSprite& sprite);
  void Render(int32_t xp, int32_t yp, const Sprite& sprite,
              bool fullbright = false);
  void Render(int32_t xp, int32_t yp, const Sprite& sprite, int32_t mirror,
              bool fullbright, int32_t solid_color = 0);
  void Render(int32_t xp, int32_t yp, const SpritePx& pixel,
              int32_t mirror = 0, int32_t white_tint = -1,
              bool fullbright = false, int32_t solid_color = 0);

  // Pixel-aligned sub-region rendering (no offset subtracted).
  void RenderSub(int32_t xp, int32_t yp, int32_t sx, int32_t sy, int32_t tw,
                 int32_t th, const SheetRect* sheet, int32_t mirrors = 0,
                 int32_t white_tint = -1, bool fullbright = false,
                 int32_t solid_color = 0);

  void FillRect(int32_t xp, int32_t yp, int32_t w_rect, int32_t h_rect,
                int32_t color);
  void DrawRect(int32_t xp, int32_t yp, int32_t w_rect, int32_t h_rect,
                int32_t color);
  void DrawAxisLine(int32_t xp, int32_t yp, Axis axis, int32_t len,
                    int32_t color);
  void DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color);
  void DrawLineSpecial(int32_t x0, int32_t y0, Axis axis, int32_t len);

  void RenderLight(int32_t x, int32_t y, int32_t r);
  void Overlay(int current_level, int32_t xa, int32_t ya, double dark_factor);

  void Flush(platform::IRenderBackend* backend, bool capture_screenshot = false);

  SpriteLinker* Linker() const { return linker_; }

 private:
  SpriteLinker* linker_ = nullptr;
  int32_t x_offset_ = 0;
  int32_t y_offset_ = 0;
  uint32_t clear_rgba_ = 0x000000FF;
  std::vector<platform::DrawCmd> commands_;
  std::vector<platform::LightCmd> lights_;
};

}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_SCREEN_H_
