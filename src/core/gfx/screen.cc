#include "core/gfx/screen.h"

#include "core/gfx/color.h"

namespace minicraft {
namespace gfx {

Screen::Screen(SpriteLinker *linker) : linker_(linker) {
  commands_.reserve(4096);
  lights_.reserve(128);
}

void Screen::Clear(int32_t color) {
  commands_.clear();
  lights_.clear();
  if (color == 0) {
    clear_rgba_ = 0x000000FF;
  } else {
    clear_rgba_ = color::RgbIntToRgba(color);
  }
}

void Screen::SetOffset(int32_t x_offset, int32_t y_offset) {
  x_offset_ = x_offset;
  y_offset_ = y_offset;
}

void Screen::Render(int32_t xp, int32_t yp, int32_t xt, int32_t yt,
                    int32_t bits, const SheetRect *sheet, int32_t white_tint,
                    bool fullbright, int32_t solid_color) {
  if (!sheet || !sheet->valid)
    return;
  RenderSub(xp - x_offset_, yp - y_offset_, xt * 8, yt * 8, 8, 8, sheet, bits,
            white_tint, fullbright, solid_color);
}

void Screen::Render(int32_t xp, int32_t yp, LinkedSprite &sprite) {
  if (!linker_)
    return;
  Render(xp, yp, sprite.GetSprite(*linker_), false);
}

void Screen::Render(int32_t xp, int32_t yp, const Sprite &sprite,
                    bool fullbright) {
  Render(xp, yp, sprite, 0, fullbright, 0);
}

void Screen::Render(int32_t xp, int32_t yp, const Sprite &sprite,
                    int32_t mirror, bool fullbright, int32_t solid_color) {
  bool mirror_x = (mirror & kBitMirrorX) != 0;
  bool mirror_y = (mirror & kBitMirrorY) != 0;
  for (int32_t r = 0; r < sprite.rows; ++r) {
    int32_t lr = mirror_y ? (sprite.rows - 1 - r) : r;
    for (int32_t c = 0; c < sprite.cols; ++c) {
      int32_t lc = mirror_x ? (sprite.cols - 1 - c) : c;
      const SpritePx &px = sprite.pixels[lr * sprite.cols + lc];
      Render(xp + c * 8, yp + r * 8, px, mirror, sprite.color, fullbright,
             solid_color);
    }
  }
}

void Screen::Render(int32_t xp, int32_t yp, const SpritePx &pixel,
                    int32_t mirror, int32_t white_tint, bool fullbright,
                    int32_t solid_color) {
  if (!linker_)
    return;
  const SheetRect *sheet = linker_->GetSheet(pixel.type, pixel.sheet_key);
  if (!sheet) {
    sheet = linker_->MissingSheet(pixel.type);
  }
  Render(xp, yp, pixel.tile_x, pixel.tile_y, pixel.mirror ^ mirror, sheet,
         white_tint, fullbright, solid_color);
}

void Screen::RenderSub(int32_t xp, int32_t yp, int32_t sx, int32_t sy,
                       int32_t tw, int32_t th, const SheetRect *sheet,
                       int32_t mirrors, int32_t white_tint, bool fullbright,
                       int32_t solid_color) {
  if (!sheet || !sheet->valid)
    return;
  if (xp + tw <= 0 || yp + th <= 0 || xp >= w || yp >= h) {
    return; // Frustum cull off-screen 8x8 tiles & sprites on CPU
  }
  if (sx + tw > sheet->width || sy + th > sheet->height) {
    if (linker_) {
      const SheetRect *miss = linker_->MissingSheet(SpriteType::kItem);
      if (miss && miss != sheet) {
        RenderSub(xp, yp, 0, 0, 8, 8, miss, mirrors, white_tint, fullbright,
                  solid_color);
      }
    }
    return;
  }

  platform::DrawCmd cmd{};
  cmd.type = platform::DrawCmdType::kSprite;
  cmd.texture_id = sheet->texture_id;
  cmd.sprite.x = xp;
  cmd.sprite.y = yp;
  cmd.sprite.w = tw;
  cmd.sprite.h = th;
  cmd.sprite.src_x = static_cast<uint16_t>(sheet->atlas_x + sx);
  cmd.sprite.src_y = static_cast<uint16_t>(sheet->atlas_y + sy);
  cmd.sprite.src_w = static_cast<uint16_t>(tw);
  cmd.sprite.src_h = static_cast<uint16_t>(th);

  uint8_t flags = platform::kFlagNone;
  if (mirrors & kBitMirrorX)
    flags |= platform::kFlagMirrorX;
  if (mirrors & kBitMirrorY)
    flags |= platform::kFlagMirrorY;

  if (white_tint != -1) {
    flags |= platform::kFlagUseWhiteTint;
    cmd.sprite.tint_rgba = color::RgbIntToRgba(white_tint);
  }
  if (fullbright) {
    flags |= platform::kFlagFullBright;
    cmd.sprite.color_rgba = 0xFFFFFFFF;
  } else if (solid_color != 0) {
    flags |= platform::kFlagUseSolidColor;
    cmd.sprite.color_rgba = color::RgbIntToRgba(solid_color);
  } else {
    cmd.sprite.color_rgba = 0xFFFFFFFF;
  }
  cmd.flags = flags;
  commands_.push_back(cmd);
}

void Screen::FillRect(int32_t xp, int32_t yp, int32_t w_rect, int32_t h_rect,
                      int32_t color) {
  if (w_rect <= 0 || h_rect <= 0)
    return;
  platform::DrawCmd cmd{};
  cmd.type = platform::DrawCmdType::kFillRect;
  cmd.rect.x = xp;
  cmd.rect.y = yp;
  cmd.rect.w = w_rect;
  cmd.rect.h = h_rect;
  cmd.rect.color_rgba = color::RgbIntToRgba(color);
  commands_.push_back(cmd);
}

void Screen::DrawRect(int32_t xp, int32_t yp, int32_t w_rect, int32_t h_rect,
                      int32_t color) {
  if (w_rect < 0 || h_rect < 0)
    return;
  platform::DrawCmd cmd{};
  cmd.type = platform::DrawCmdType::kDrawRect;
  cmd.rect.x = xp;
  cmd.rect.y = yp;
  cmd.rect.w = w_rect;
  cmd.rect.h = h_rect;
  cmd.rect.color_rgba = color::RgbIntToRgba(color);
  commands_.push_back(cmd);
}

void Screen::DrawAxisLine(int32_t xp, int32_t yp, Axis axis, int32_t len,
                          int32_t color) {
  switch (axis) {
    case Axis::kHorizontal:
      DrawLine(xp, yp, xp + len, yp, color);
      break;
    case Axis::kVertical:
      DrawLine(xp, yp, xp, yp + len, color);
      break;
  }
}

void Screen::DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t color) {
  platform::DrawCmd cmd{};
  cmd.type = platform::DrawCmdType::kDrawLine;
  cmd.line.x0 = x0;
  cmd.line.y0 = y0;
  cmd.line.x1 = x1;
  cmd.line.y1 = y1;
  cmd.line.color_rgba = color::RgbIntToRgba(color);
  commands_.push_back(cmd);
}

void Screen::DrawLineSpecial(int32_t x0, int32_t y0, Axis axis, int32_t len) {
  platform::DrawCmd cmd{};
  cmd.type = platform::DrawCmdType::kInvertLine;
  cmd.rect.x = x0;
  cmd.rect.y = y0;
  switch (axis) {
    case Axis::kHorizontal:
      cmd.rect.w = len;
      cmd.rect.h = 1;
      break;
    case Axis::kVertical:
      cmd.rect.w = 1;
      cmd.rect.h = len;
      break;
  }
  commands_.push_back(cmd);
}

void Screen::RenderLight(int32_t x, int32_t y, int32_t r) {
  int32_t sx = x - x_offset_;
  int32_t sy = y - y_offset_;
  if (sx + r < 0 || sy + r < 0 || sx - r >= w || sy - r >= h) {
    return;
  }
  lights_.push_back({sx, sy, r});
}

void Screen::Overlay(int current_level, int32_t xa, int32_t ya,
                     double dark_factor) {
  float alpha =
      (current_level < 3) ? 1.0f : static_cast<float>(dark_factor / 160.0);
  platform::DrawCmd cmd{};
  cmd.type = platform::DrawCmdType::kApplyLightOverlay;
  cmd.overlay.offset_x = xa;
  cmd.overlay.offset_y = ya;
  cmd.overlay.dark_alpha = alpha;
  commands_.push_back(cmd);
}

void Screen::Flush(platform::IRenderBackend *backend, bool capture_screenshot) {
  if (!backend)
    return;
  platform::FrameRenderData frame{};
  frame.clear_rgba = clear_rgba_;
  frame.commands = commands_.data();
  frame.command_count = commands_.size();
  frame.lights = lights_.data();
  frame.light_count = lights_.size();
  frame.capture_screenshot = capture_screenshot;
  backend->RenderFrame(frame);
}

} // namespace gfx
} // namespace minicraft
