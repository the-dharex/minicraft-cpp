#ifndef MINICRAFT_CORE_GFX_SPRITE_ANIMATION_H_
#define MINICRAFT_CORE_GFX_SPRITE_ANIMATION_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/gfx/screen.h"
#include "core/gfx/sprite_atlas.h"

namespace minicraft {
namespace level {
class Level;
class Tile;
}  // namespace level

namespace gfx {

class SpriteAnimation {
 public:
  using TileConnectionChecker = std::function<bool(
      const level::Level&, int32_t, int32_t, const level::Tile&, bool)>;

  SpriteAnimation(SpriteType type, std::string key, const SpriteLinker* linker);
  SpriteAnimation(const SpriteMeta* meta, SpriteType type, std::string key,
                  const SpriteLinker* linker);
  ~SpriteAnimation();

  SpriteAnimation(const SpriteAnimation&) = delete;
  SpriteAnimation& operator=(const SpriteAnimation&) = delete;

  SpriteAnimation& SetConnectionChecker(TileConnectionChecker checker);
  SpriteAnimation& SetSingletonWithConnective(bool connective);
  SpriteAnimation& SetColor(int32_t color);
  SpriteAnimation& SetColor(int32_t frame, int32_t color);
  SpriteAnimation& SetMirror(int32_t mirror);
  SpriteAnimation& SetMirror(int32_t frame, int32_t mirror);
  SpriteAnimation& SetSpriteMirror(int32_t mirror);

  LinkedSprite& CurrentFrame() { return animations_[frame_]; }
  const LinkedSprite& CurrentFrame() const { return animations_[frame_]; }
  LinkedSprite& FrameAt(int32_t frame) { return animations_[frame]; }

  void Render(Screen* screen, const level::Level& level, int32_t x, int32_t y,
              int32_t game_time, uint64_t last_world_enter_ms,
              uint64_t now_ms);

  void RefreshAnimation(const SpriteMeta* metadata,
                        const SpriteLinker* linker);

  static void RefreshAll(const SpriteLinker* linker);

 private:
  static std::vector<SpriteAnimation*>& Registry();

  std::vector<LinkedSprite> animations_;
  SpriteMeta metadata_;
  bool has_metadata_ = false;
  int32_t frame_ = 0;
  int32_t frametick_ = 0;
  int32_t last_tick_ = 0;
  uint64_t last_millis_ = 0;

  std::unique_ptr<LinkedSprite> border_;
  std::unique_ptr<LinkedSprite> corner_;
  TileConnectionChecker connection_checker_;
  bool singleton_with_connective_ = false;

  SpriteType type_;
  std::string key_;
};

}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_SPRITE_ANIMATION_H_
