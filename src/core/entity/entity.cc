#include "core/entity/entity.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/level/level.h"
#include "core/level/tile.h"

namespace minicraft::entity {

Entity::Entity(int32_t xr, int32_t yr)
    : xr(xr), yr(yr), eid(game::GameState::Instance().GenerateUniqueEid()) {}

void Entity::Remove() { removed = true; }

void Entity::SetLevel(level::Level* lvl, int32_t nx, int32_t ny) {
  level = lvl;
  x = nx;
  y = ny;
  removed = false;
}

bool Entity::Intersects(int32_t x0, int32_t y0, int32_t x1, int32_t y1) const {
  return !(x + xr < x0 || y + yr < y0 || x - xr > x1 || y - yr > y1);
}

gfx::Rectangle Entity::GetBounds() const {
  return gfx::Rectangle(x, y, xr * 2, yr * 2, gfx::Rectangle::kCenterDims);
}

bool Entity::IsWithin(int32_t tile_radius, const Entity& other) const {
  if (level != other.level) return false;
  double dist = std::hypot(static_cast<double>(x - other.x),
                           static_cast<double>(y - other.y));
  return std::floor(dist / 16.0) <= static_cast<double>(tile_radius);
}

void Entity::TryPush(Player& player) {
  if (push_time_ == 0) {
    push_dir_ = player.dir;
    push_time_ = GetPushTimeDelay();
  }
}

void Entity::AdvancePush() {
  // Move at most 1px per push trigger, then cool down for push_time_ ticks.
  Move(DirX(push_dir_), DirY(push_dir_));
  push_dir_ = Direction::kNone;
  if (push_time_ > 0) {
    --push_time_;
  }
}

bool Entity::Move(int32_t xa, int32_t ya) {
  if (xa != 0 || ya != 0) {
    bool stopped = true;
    if (xa != 0 && Move2(xa, 0)) stopped = false;
    if (ya != 0 && Move2(0, ya)) stopped = false;
    if (!stopped && level != nullptr) {
      const int32_t xt = x >> 4;
      const int32_t yt = y >> 4;
      level->GetTile(xt, yt)->SteppedOn(*level, xt, yt, *this);
    }
    return !stopped;
  }
  return true;
}

bool Entity::Move2(int32_t xa, int32_t ya) {
  if (level == nullptr) {
    x += xa;
    y += ya;
    return true;
  }
  if (xa != 0 && ya != 0) {
    bool ok_x = Move2(xa, 0);
    bool ok_y = Move2(0, ya);
    return ok_x || ok_y;
  }
  const int32_t d = (xa != 0) ? xa : ya;
  if (d == 0) return true;

  const bool is_x = (xa != 0);
  const int32_t sgn = (d > 0) ? 1 : -1;
  const int32_t pos = is_x ? x : y;
  const int32_t radius = is_x ? xr : yr;
  const int32_t other_pos = is_x ? y : x;
  const int32_t other_radius = is_x ? yr : xr;

  const int32_t hit_box_left = other_pos - other_radius;
  const int32_t hit_box_right = other_pos + other_radius;
  const int32_t hit_box_front = pos + radius * sgn;

  const int32_t hit_box_left_tile = hit_box_left >> 4;
  const int32_t hit_box_right_tile = hit_box_right >> 4;
  const int32_t hit_box_front_tile = hit_box_front >> 4;
  const int32_t hit_box_front1 = hit_box_front + d;
  const int32_t hit_box_front_tile1 = hit_box_front1 >> 4;

  // Determine the farthest tile boundary reachable along this axis.
  int32_t max_front_tile = hit_box_front_tile1;
  bool hit_blocking_tile = false;
  for (int32_t front = hit_box_front_tile + sgn;
       sgn < 0 ? front >= hit_box_front_tile1 : front <= hit_box_front_tile1;
       front += sgn) {
    for (int32_t hor_tile = hit_box_left_tile; hor_tile <= hit_box_right_tile;
         ++hor_tile) {
      const int32_t tx = is_x ? front : hor_tile;
      const int32_t ty = is_x ? hor_tile : front;
      if (!level->GetTile(tx, ty)->MayPass(*level, tx, ty, *this)) {
        max_front_tile = front - sgn;
        hit_blocking_tile = true;
        break;
      }
    }
    if (hit_blocking_tile) break;
  }

  const int32_t max_front =
      hit_box_front +
      (sgn > 0
           ? std::min(d, (max_front_tile << 4) - hit_box_front + 15)
           : std::max(d, (max_front_tile << 4) - hit_box_front));

  if (max_front == hit_box_front) {
    const int32_t front_tile = (hit_box_front + sgn) >> 4;
    for (int32_t hor_tile = hit_box_left_tile; hor_tile <= hit_box_right_tile;
         ++hor_tile) {
      const int32_t tx = is_x ? front_tile : hor_tile;
      const int32_t ty = is_x ? hor_tile : front_tile;
      const_cast<level::Tile*>(level->GetTile(tx, ty))
          ->BumpedInto(*level, tx, ty, *this);
    }
    return false;
  }

  // Step 1 pixel at a time and test exact pixel hitboxes via GetEntitiesInRect.
  std::vector<Entity*> was_inside = level->GetEntitiesInRect(GetBounds());
  int32_t front_tile = hit_box_front >> 4;
  bool successful = false;

  for (int32_t front = hit_box_front;
       sgn < 0 ? front > max_front : front < max_front; front += sgn) {
    bool handle_stepped_on = false;
    const int32_t new_front_tile = (front + sgn) >> 4;
    if (new_front_tile != front_tile) {
      for (int32_t hor_tile = hit_box_left_tile; hor_tile <= hit_box_right_tile;
           ++hor_tile) {
        const int32_t tx = is_x ? new_front_tile : hor_tile;
        const int32_t ty = is_x ? hor_tile : new_front_tile;
        const_cast<level::Tile*>(level->GetTile(tx, ty))
            ->BumpedInto(*level, tx, ty, *this);
      }
      front_tile = new_front_tile;
      handle_stepped_on = true;
    }

    const int32_t nx = is_x ? (x + sgn) : x;
    const int32_t ny = is_x ? y : (y + sgn);
    const gfx::Rectangle next_bounds(nx, ny, xr * 2, yr * 2,
                                     gfx::Rectangle::kCenterDims);
    bool blocked = false;
    for (Entity* e : level->GetEntitiesInRect(next_bounds)) {
      if (e == this) continue;
      bool already_in = false;
      for (Entity* w : was_inside) {
        if (w == e) {
          already_in = true;
          break;
        }
      }
      if (!already_in) {
        if (!blocked && e->Blocks(*this)) {
          blocked = true;
        }
        if (dynamic_cast<Player*>(e) != nullptr) {
          TouchedBy(*e);
        } else {
          e->TouchedBy(*this);
        }
        was_inside.push_back(e);
      }
    }

    if (blocked) break;

    if (is_x) {
      x += sgn;
    } else {
      y += sgn;
    }
    if (handle_stepped_on) {
      for (int32_t hor_tile = hit_box_left_tile; hor_tile <= hit_box_right_tile;
           ++hor_tile) {
        const int32_t tx = is_x ? front_tile : hor_tile;
        const int32_t ty = is_x ? hor_tile : front_tile;
        const_cast<level::Tile*>(level->GetTile(tx, ty))
            ->SteppedOn(*level, tx, ty, *this);
      }
    }
    successful = true;
  }

  return successful;
}

}  // namespace minicraft::entity
