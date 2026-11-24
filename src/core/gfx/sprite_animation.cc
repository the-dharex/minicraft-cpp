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
    : type_(type), key_(std::move(key)) {
  RefreshAnimation(linker ? linker->GetMetadata(key_) : nullptr, linker);
  Registry().push_back(this);
}

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

SpriteAnimation::SpriteAnimation(SpriteAnimation&& other) noexcept
    : animations_(std::move(other.animations_)),
      metadata_(std::move(other.metadata_)),
      has_metadata_(other.has_metadata_),
      frame_(other.frame_),
      frametick_(other.frametick_),
      last_tick_(other.last_tick_),
      last_millis_(other.last_millis_),
      border_(std::move(other.border_)),
      corner_(std::move(other.corner_)),
      connection_checker_(std::move(other.connection_checker_)),
      singleton_with_connective_(other.singleton_with_connective_),
      type_(other.type_),
      key_(std::move(other.key_)) {
  other.has_metadata_ = false;
  other.frame_ = 0;
  other.frametick_ = 0;
  other.last_tick_ = 0;
  other.last_millis_ = 0;
  other.singleton_with_connective_ = false;

  auto& reg = Registry();
  auto it = std::find(reg.begin(), reg.end(), &other);
  if (it != reg.end()) {
    *it = this;
  }
}

SpriteAnimation& SpriteAnimation::operator=(SpriteAnimation&& other) noexcept {
  if (this == &other) return *this;

  animations_ = std::move(other.animations_);
  metadata_ = std::move(other.metadata_);
  has_metadata_ = other.has_metadata_;
  frame_ = other.frame_;
  frametick_ = other.frametick_;
  last_tick_ = other.last_tick_;
  last_millis_ = other.last_millis_;
  border_ = std::move(other.border_);
  corner_ = std::move(other.corner_);
  connection_checker_ = std::move(other.connection_checker_);
  singleton_with_connective_ = other.singleton_with_connective_;
  type_ = other.type_;
  key_ = std::move(other.key_);

  other.has_metadata_ = false;
  other.frame_ = 0;
  other.frametick_ = 0;
  other.last_tick_ = 0;
  other.last_millis_ = 0;
  other.singleton_with_connective_ = false;

  auto& reg = Registry();
  const size_t before = reg.size();
  reg.erase(std::remove(reg.begin(), reg.end(), &other), reg.end());
  const bool other_was_registered = (reg.size() != before);

  auto self_it = std::find(reg.begin(), reg.end(), this);
  if (other_was_registered && self_it == reg.end()) {
    reg.push_back(this);
  } else if (!other_was_registered && self_it != reg.end()) {
    reg.erase(self_it);
  }
  return *this;
}

void SpriteAnimation::RefreshAll(const SpriteLinker* linker) {
  if (!linker) return;
  for (SpriteAnimation* anim : Registry()) {
    if (anim) {
      anim->RefreshAnimation(linker->GetMetadata(anim->key_), linker);
    }
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

LinkedSprite& SpriteAnimation::CurrentFrame() { return FrameAt(frame_); }

const LinkedSprite& SpriteAnimation::CurrentFrame() const {
  return FrameAt(frame_);
}

LinkedSprite& SpriteAnimation::FrameAt(int32_t frame) {
  if (animations_.empty()) {
    animations_.emplace_back(type_, "missing_tile");
  }
  const int32_t idx =
      std::clamp(frame, 0, static_cast<int32_t>(animations_.size()) - 1);
  return animations_[idx];
}

const LinkedSprite& SpriteAnimation::FrameAt(int32_t frame) const {
  static const LinkedSprite kFallback(SpriteType::kTile, "missing_tile");
  if (animations_.empty()) return kFallback;
  const int32_t idx =
      std::clamp(frame, 0, static_cast<int32_t>(animations_.size()) - 1);
  return animations_[idx];
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

  const int32_t width = std::max(1, sheet->width / 8);
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
  frame_ = std::clamp(frame_, 0, static_cast<int32_t>(animations_.size()) - 1);

  auto has_grid = [](const Sprite* sp, int32_t min_rows,
                     int32_t min_cols) -> bool {
    if (!sp || sp->rows < min_rows || sp->cols < min_cols) return false;
    const size_t needed =
        static_cast<size_t>(sp->rows) * static_cast<size_t>(sp->cols);
    return sp->pixels.size() >= needed;
  };

  auto px_at = [](const Sprite& sp, int32_t row,
                  int32_t col) -> const SpritePx* {
    if (sp.rows <= 0 || sp.cols <= 0 || sp.pixels.empty()) return nullptr;
    const int32_t r = std::clamp(row, 0, sp.rows - 1);
    const int32_t c = std::clamp(col, 0, sp.cols - 1);
    const size_t idx =
        static_cast<size_t>(r) * static_cast<size_t>(sp.cols) +
        static_cast<size_t>(c);
    return idx < sp.pixels.size() ? &sp.pixels[idx] : nullptr;
  };

  bool rendered_connected = false;
  if (connection_checker_ && (border_ || corner_)) {
    const Sprite& full = animations_[frame_].GetSprite(linker);
    const Sprite* sparse = border_ ? &border_->GetSprite(linker) : nullptr;
    const Sprite* raw_sides = corner_ ? &corner_->GetSprite(linker) : nullptr;
    const Sprite* sides = has_grid(raw_sides, 2, 2) ? raw_sides : nullptr;

    if (has_grid(&full, 2, 2) && has_grid(sparse, 3, 3)) {
      const auto check_tile = [&](int32_t tx, int32_t ty, bool side) -> bool {
        const level::Tile* neighbor = level.GetTile(tx, ty);
        return neighbor ? connection_checker_(level, tx, ty, *neighbor, side)
                        : false;
      };

      const bool u = check_tile(x, y - 1, true);
      const bool d = check_tile(x, y + 1, true);
      const bool l = check_tile(x - 1, y, true);
      const bool r = check_tile(x + 1, y, true);

      const bool ul = check_tile(x - 1, y - 1, false);
      const bool dl = check_tile(x - 1, y + 1, false);
      const bool ur = check_tile(x + 1, y - 1, false);
      const bool dr = check_tile(x + 1, y + 1, false);

      const int32_t px = x << 4;
      const int32_t py = y << 4;

      auto render_px = [&](int32_t dx, int32_t dy, const SpritePx* cell,
                           int32_t mirror, int32_t col) {
        if (cell) {
          screen->Render(px + dx, py + dy, *cell, mirror, col);
        }
      };

      auto render_corner = [&](int32_t dx, int32_t dy, bool v_conn, bool h_conn,
                               bool diag_conn, int32_t full_r, int32_t full_c,
                               int32_t sparse_r, int32_t sparse_c,
                               int32_t side_r, int32_t side_c) {
        if (v_conn && h_conn) {
          const int32_t conn_col =
              singleton_with_connective_ ? full.color : sparse->color;
          const SpritePx* conn_px = singleton_with_connective_
                                        ? px_at(full, full_r, full_c)
                                        : px_at(*sparse, 1, 1);
          if (diag_conn) {
            render_px(dx, dy, conn_px, 0, conn_col);
          } else if (!sides) {
            render_px(dx, dy, px_at(full, full_r, full_c), 0, full.color);
          } else {
            render_px(dx, dy, px_at(*sides, side_r, side_c), 3, sides->color);
          }
        } else {
          render_px(dx, dy, px_at(*sparse, sparse_r, sparse_c), 0,
                    sparse->color);
        }
      };

      render_corner(0, 0, u, l, ul, 1, 1, u ? 1 : 0, l ? 1 : 0, 0, 0);
      render_corner(8, 0, u, r, ur, 1, 0, u ? 1 : 0, r ? 1 : 2, 0, 1);
      render_corner(0, 8, d, l, dl, 0, 1, d ? 1 : 2, l ? 1 : 0, 1, 0);
      render_corner(8, 8, d, r, dr, 0, 0, d ? 1 : 2, r ? 1 : 2, 1, 1);
      rendered_connected = true;
    }
  }

  if (!rendered_connected) {
    screen->Render(x << 4, y << 4, animations_[frame_]);
  }

  if (animations_.size() > 1 && has_metadata_ && metadata_.frametime > 0) {
    if (last_millis_ < last_world_enter_ms || game_time < last_tick_) {
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
