#ifndef MINICRAFT_CORE_PLATFORM_RENDER_BACKEND_H_
#define MINICRAFT_CORE_PLATFORM_RENDER_BACKEND_H_

#include <cstddef>
#include <cstdint>

namespace minicraft {
namespace platform {

#ifdef __SWITCH__
inline constexpr int32_t kScreenWidth = 320;
inline constexpr int32_t kScreenHeight = 180;
#else
inline constexpr int32_t kScreenWidth = 288;
inline constexpr int32_t kScreenHeight = 192;
#endif

enum DrawCmdFlags : uint8_t {
  kFlagNone = 0,
  kFlagMirrorX = 1 << 0,
  kFlagMirrorY = 1 << 1,
  kFlagFullBright = 1 << 2,
  kFlagUseWhiteTint = 1 << 3,
  kFlagUseSolidColor = 1 << 4,
};

enum class DrawCmdType : uint8_t {
  kSprite = 0,
  kFillRect = 1,
  kDrawRect = 2,
  kDrawLine = 3,
  kInvertLine = 4,
  kApplyLightOverlay = 5,
};

struct DrawCmd {
  DrawCmdType type = DrawCmdType::kSprite;
  uint8_t flags = kFlagNone;
  uint16_t texture_id = 0;  // 0 = main Mega-Atlas; >0 = custom uploaded sheet.
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;  // Also used as x1 for kDrawLine, or light offset x for overlay.
  int32_t h = 0;  // Also used as y1 for kDrawLine, or light offset y for overlay.
  uint16_t src_x = 0;
  uint16_t src_y = 0;
  uint16_t src_w = 0;
  uint16_t src_h = 0;
  uint32_t color_rgba = 0;  // Packed 0xRRGGBBAA (solid_color or rect/line color).
  uint32_t tint_rgba = 0;   // Packed 0xRRGGBBAA (white_tint when kFlagUseWhiteTint is set).
  float extra_float = 0.0f;  // Used by kApplyLightOverlay for darkness alpha.
};

struct LightCmd {
  int32_t x = 0;
  int32_t y = 0;
  int32_t radius = 0;
};

struct FrameRenderData {
  uint32_t clear_rgba = 0x000000FF;
  const DrawCmd* commands = nullptr;
  size_t command_count = 0;
  const LightCmd* lights = nullptr;
  size_t light_count = 0;
  bool capture_screenshot = false;
  int32_t screenshot_w = 0;
  int32_t screenshot_h = 0;
};

// platform-agnostic rendering backend interface.
class IRenderBackend {
 public:
  virtual ~IRenderBackend() = default;

  // Uploads or replaces the main RGBA8 Mega-Atlas (texture_id == 0).
  virtual bool UploadAtlas(int32_t width, int32_t height,
                           const uint8_t* rgba) = 0;

  // Creates or updates a custom RGBA8 texture (returns texture_id > 0).
  virtual uint16_t CreateTexture(int32_t width, int32_t height,
                                 const uint8_t* rgba) = 0;
  virtual void DestroyTexture(uint16_t texture_id) = 0;

  // Executes a complete 288x192 frame and presents it to the window.
  virtual void RenderFrame(const FrameRenderData& frame) = 0;

  // Reads back the last rendered screenshot pixels (RGBA8, top-to-bottom).
  virtual bool ReadBackScreenshot(int32_t* out_w, int32_t* out_h,
                                  uint8_t* out_rgba,
                                  size_t buffer_bytes) = 0;
};

}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PLATFORM_RENDER_BACKEND_H_
