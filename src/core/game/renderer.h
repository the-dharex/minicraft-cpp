#ifndef MINICRAFT_CORE_GAME_RENDERER_H_
#define MINICRAFT_CORE_GAME_RENDERER_H_

#include <cstdint>
#include <string>

#include "core/gfx/sprite_atlas.h"

namespace minicraft {
namespace gfx {
class Screen;
}
namespace platform {
struct PlatformContext;
}
}  // namespace minicraft

namespace minicraft::game {

class Updater;
class World;

// Renders the active level, lighting overlay, gameplay HUD (hearts, stamina,
// hunger, armor, durability, potion timers, bossbar, quests, F3 debug),
// active Display stack, focus nagger, and screenshot captures.
class Renderer {
 public:
  Renderer() = default;
  ~Renderer() = default;

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void Render(const World& world, Updater& updater, gfx::Screen& screen,
              const platform::PlatformContext& platform,
              const std::string& game_dir, int32_t fps, int32_t tps);

 private:
  void RenderLevel(const World& world, gfx::Screen& screen) const;
  void RenderGui(const World& world, Updater& updater, gfx::Screen& screen,
                 int32_t fps, int32_t tps) const;
  void RenderBossbar(gfx::Screen& screen, int32_t bar_length,
                     const std::string& title) const;
  void RenderQuestsDisplay(const World& world, gfx::Screen& screen) const;
  void RenderDebugInfo(const World& world, const Updater& updater,
                       gfx::Screen& screen, int32_t fps, int32_t tps) const;
  void RenderFocusNagger(gfx::Screen& screen) const;
  void CaptureScreenshotIfRequested(Updater& updater,
                                    const platform::PlatformContext& platform,
                                    const std::string& game_dir) const;

  mutable gfx::LinkedSprite hud_sheet_{gfx::SpriteType::kGui, "hud"};
  mutable int32_t potion_render_offset_ = 0;
};

}  // namespace minicraft::game

#endif  // MINICRAFT_CORE_GAME_RENDERER_H_
