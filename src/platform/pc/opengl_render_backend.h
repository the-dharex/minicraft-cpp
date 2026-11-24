#ifndef MINICRAFT_PLATFORM_PC_OPENGL_RENDER_BACKEND_H_
#define MINICRAFT_PLATFORM_PC_OPENGL_RENDER_BACKEND_H_

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/platform/render_backend.h"

namespace minicraft {
namespace platform {
namespace pc {

class OpenGLRenderBackend final : public IRenderBackend {
 public:
  explicit OpenGLRenderBackend(const class PcWindow* window);
  ~OpenGLRenderBackend() override;

  OpenGLRenderBackend(const OpenGLRenderBackend&) = delete;
  OpenGLRenderBackend& operator=(const OpenGLRenderBackend&) = delete;

  bool Init();
  void Shutdown();

  bool UploadAtlas(int32_t width, int32_t height,
                   const uint8_t* rgba) override;
  uint16_t CreateTexture(int32_t width, int32_t height,
                         const uint8_t* rgba) override;
  void DestroyTexture(uint16_t texture_id) override;

  void RenderFrame(const FrameRenderData& frame) override;
  bool ReadBackScreenshot(int32_t* out_w, int32_t* out_h, uint8_t* out_rgba,
                          size_t buffer_bytes) override;

 private:
  struct Vertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
    float mode;      // 0=sprite, 1=solid, 2=fullbright, 3=solid_sprite
    float has_tint;  // 1.0 if whiteTint active
    float tr;
    float tg;
    float tb;
  };

  struct TextureInfo {
    uint32_t gl_id = 0;
    int32_t width = 1;
    int32_t height = 1;
  };

  void FlushBatch(uint16_t texture_id);
  void PushQuad(float x0, float y0, float x1, float y1, float u0, float v0,
                float u1, float v1, float r, float g, float b, float a,
                float mode, float has_tint, float tr = 1.0f, float tg = 1.0f,
                float tb = 1.0f);
  void ApplyLightOverlay(const FrameRenderData& frame, int32_t offset_x,
                         int32_t offset_y, float darkness_alpha);

  const PcWindow* window_ = nullptr;
  bool initialized_ = false;

  bool using_spirv_ = false;
  uint32_t sprite_prog_ = 0;
  uint32_t light_prog_ = 0;
  uint32_t dither_prog_ = 0;
  uint32_t blit_prog_ = 0;
  int32_t u_sprite_screen_size_ = -1;
  int32_t u_sprite_tex_ = -1;
  int32_t u_light_screen_size_ = -1;
  int32_t u_dither_light_tex_ = -1;
  int32_t u_dither_offset_ = -1;
  int32_t u_dither_darkness_alpha_ = -1;
  int32_t u_dither_screen_size_ = -1;
  int32_t u_blit_scene_tex_ = -1;

  uint32_t batch_vao_ = 0;
  uint32_t batch_vbo_ = 0;
  uint32_t quad_vao_ = 0;
  uint32_t quad_vbo_ = 0;

  uint32_t scene_fbo_ = 0;
  uint32_t scene_tex_ = 0;
  uint32_t light_fbo_ = 0;
  uint32_t light_tex_ = 0;

  TextureInfo atlas_tex_{};
  uint16_t next_custom_tex_id_ = 1;
  std::unordered_map<uint16_t, TextureInfo> custom_textures_;

  std::vector<Vertex> vertices_;
  std::vector<uint8_t> screenshot_rgba_;
  int32_t screenshot_w_ = 0;
  int32_t screenshot_h_ = 0;
};

}  // namespace pc
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_PC_OPENGL_RENDER_BACKEND_H_
