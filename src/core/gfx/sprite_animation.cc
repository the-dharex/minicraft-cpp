#include "core/gfx/sprite_animation.h"

#include <algorithm>
#include <utility>

#include "core/level/level.h"
#include "core/level/tile.h"

namespace minicraft {
namespace gfx {

std::vector<SpriteAnimation*>& SpriteAnimation::Registry() {
  static std::vector<SpriteAnimation*> instances;
  return instances;
}

SpriteAnimation::SpriteAnimation(SpriteType type, std::string key,
                                 const SpriteLinker* linker)
    : SpriteAnimation(linker ? linker->GetMetadata(key) : nullptr, type,
                      std::move(key), linker) {}

SpriteAnimation::SpriteAnimation(const SpriteMeta* meta, SpriteType type,
                                 std::string key, const SpriteLinker* linker)
    : type_(type), key_(std::move(key)) {
  RefreshAnimation(meta, linker);
  Registry().push_back(this);
}

SpriteAnimation::~SpriteAnimation() {
  auto& reg = Registry();
  reg.erase(std::remove(reg.begin(), reg.end(), this), reg.end());
}

void SpriteAnimation::RefreshAll(const SpriteLinker* linker) {
  if (!linker) return;
  for (SpriteAnimation* anim : Registry()) {
    anim->RefreshAnimation(linker->GetMetadata(anim->key_), linker);
  }
}

SpriteAnimation& SpriteAnimation::SetConnectionChecker(
    TileConnectionChecker checker) {
  connection_checker_ = std::move(checker);
  return *this;
}

SpriteAnimation& SpriteAnimation::SetSingletonWithConnective(bool connective) {
  singleton_with_connective_ = connective;
  return *this;
}

SpriteAnimation& SpriteAnimation::SetColor(int32_t color) {
  for (auto& sprite : animations_) {
    sprite.SetColor(color);
  }
  return *this;
}

SpriteAnimation& SpriteAnimation::SetColor(int32_t frame, int32_t color) {
  if (animations_.empty()) return *this;
  int32_t idx =
      std::clamp(frame, 0, static_cast<int32_t>(animations_.size()) - 1);
  animations_[idx].SetColor(color);
  return *this;
}

SpriteAnimation& SpriteAnimation::SetMirror(int32_t mirror) {
  for (auto& sprite : animations_) {
    sprite.SetMirror(mirror);
  }
  return *this;
}

SpriteAnimation& SpriteAnimation::SetMirror(int32_t frame, int32_t mirror) {
  if (animations_.empty()) return *this;
  int32_t idx =
      std::clamp(frame, 0, static_cast<int32_t>(animations_.size()) - 1);
  animations_[idx].SetMirror(mirror);
  return *this;
}

SpriteAnimation& SpriteAnimation::SetSpriteMirror(int32_t mirror) {
  for (auto& sprite : animations_) {
    sprite.SetFlip(mirror);
  }
  return *this;
}

void SpriteAnimation::RefreshAnimation(const SpriteMeta* metadata,
                                       const SpriteLinker* linker) {
  frame_ = 0;
  frametick_ = 0;
  has_metadata_ = (metadata != nullptr);
  if (metadata) {
    metadata_ = *metadata;
    if (metadata_.frames < 1) metadata_.frames = 1;
  }

  animations_.clear();
  border_.reset();
  corner_.reset();

  const SheetRect* sheet = linker ? linker->GetSheet(type_, key_) : nullptr;
  if (!sheet) {
    animations_.emplace_back(type_, "missing_tile");
    return;
  }

  int32_t width = sheet->width / 8;
  if (has_metadata_) {
    animations_.reserve(static_cast<size_t>(metadata_.frames));
    for (int32_t f = 0; f < metadata_.frames; ++f) {
      LinkedSprite ls(type_, key_);
      ls.SetSpriteDim(0, f * width, width, width);
      animations_.push_back(std::move(ls));
    }
    if (!metadata_.border.empty()) {
      border_ = std::make_unique<LinkedSprite>(type_, metadata_.border);
    }
    if (!metadata_.corner.empty()) {
      corner_ = std::make_unique<LinkedSprite>(type_, metadata_.corner);
    }
  } else {
    LinkedSprite ls(type_, key_);
    ls.SetSpriteSize(width, width);
    animations_.push_back(std::move(ls));
  }
}

void SpriteAnimation::Render(Screen* screen, const level::Level& level,
                             int32_t x, int32_t y, int32_t game_time,
                             uint64_t last_world_enter_ms, uint64_t now_ms) {
  if (!screen || !screen->Linker() || animations_.empty()) return;
  const SpriteLinker& linker = *screen->Linker();

  if (connection_checker_ && (border_ || corner_)) {
    bool u = connection_checker_(level, x, y - 1, *level.GetTile(x, y - 1), true);
    bool d = connection_checker_(level, x, y + 1, *level.GetTile(x, y + 1), true);
    bool l = connection_checker_(level, x - 1, y, *level.GetTile(x - 1, y), true);
    bool r = connection_checker_(level, x + 1, y, *level.GetTile(x + 1, y), true);

    bool ul =
        connection_checker_(level, x - 1, y - 1, *level.GetTile(x - 1, y - 1), false);
    bool dl =
        connection_checker_(level, x - 1, y + 1, *level.GetTile(x - 1, y + 1), false);
    bool ur =
        connection_checker_(level, x + 1, y - 1, *level.GetTile(x + 1, y - 1), false);
    bool dr =
        connection_checker_(level, x + 1, y + 1, *level.GetTile(x + 1, y + 1), false);

    int32_t px = x << 4;
    int32_t py = y << 4;

    const Sprite& full = animations_[frame_].GetSprite(linker);
    const Sprite* sparse = border_ ? &border_->GetSprite(linker) : nullptr;
    const Sprite* sides = corner_ ? &corner_->GetSprite(linker) : nullptr;

    auto px_at = [](const Sprite& sp, int32_t row, int32_t col) -> const SpritePx& {
      return sp.pixels[row * sp.cols + col];
    };

    auto render_corner = [&](int32_t dx, int32_t dy, bool v_conn, bool h_conn,
                             bool diag_conn, int32_t full_r, int32_t full_c,
                             int32_t sparse_r, int32_t sparse_c,
                             int32_t side_r, int32_t side_c) {
      if (v_conn && h_conn && sparse) {
        int32_t conn_col =
            singleton_with_connective_ ? full.color : sparse->color;
        const SpritePx& conn_px = singleton_with_connective_
                                      ? px_at(full, full_r, full_c)
                                      : px_at(*sparse, 1, 1);
        if (diag_conn) {
          screen->Render(px + dx, py + dy, conn_px, 0, conn_col);
        } else if (!sides) {
          screen->Render(px + dx, py + dy, px_at(full, full_r, full_c), 0,
                         full.color);
        } else {
          screen->Render(px + dx, py + dy, px_at(*sides, side_r, side_c), 3,
                         sides->color);
        }
      } else if (sparse) {
        screen->Render(px + dx, py + dy, px_at(*sparse, sparse_r, sparse_c), 0,
                       sparse->color);
      }
    };

    render_corner(0, 0, u, l, ul, 1, 1, u ? 1 : 0, l ? 1 : 0, 0, 0);
    render_corner(8, 0, u, r, ur, 1, 0, u ? 1 : 0, r ? 1 : 2, 0, 1);
    render_corner(0, 8, d, l, dl, 0, 1, d ? 1 : 2, l ? 1 : 0, 1, 0);
    render_corner(8, 8, d, r, dr, 0, 0, d ? 1 : 2, r ? 1 : 2, 1, 1);
  } else {
    screen->Render(x << 4, y << 4, animations_[frame_]);
  }

  if (animations_.size() > 1 && has_metadata_ && metadata_.frametime > 0) {
    if (last_millis_ < last_world_enter_ms) {
      last_tick_ = game_time;
      frame_ = 0;
      frametick_ = 0;
    }
    frametick_ += game_time - last_tick_;
    last_tick_ = game_time;
    last_millis_ = now_ms;

    if (frametick_ >= metadata_.frametime) {
      frame_ += frametick_ / metadata_.frametime;
      frametick_ %= metadata_.frametime;
      if (frame_ >= static_cast<int32_t>(animations_.size()) - 1) {
        frame_ %= static_cast<int32_t>(animations_.size());
      }
    }
  }
}

}  // namespace gfx
}  // namespace minicraft
