#include "core/entity/boat.h"

#include <algorithm>

#include "core/entity/player.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/items.h"
#include "core/level/level.h"
#include "core/level/tiles.h"

namespace minicraft::entity {

Boat::Boat(Direction dir)
    : Entity(6, 6),
      dir_(dir),
      sprite_(gfx::SpriteType::kEntity, "boat") {}

bool Boat::IsInWater() const {
  if (!level) return false;
  return level->GetTile(x >> 4, y >> 4)->id == level::Tiles::kWater;
}

bool Boat::IsInLava() const {
  if (!level) return false;
  return level->GetTile(x >> 4, y >> 4)->id == level::Tiles::kLava;
}

int32_t Boat::GetPushTimeDelay() const {
  return IsInWater() ? 2 : IsInLava() ? 4 : 6;
}

void Boat::TouchedBy(Entity& entity) {
  if (auto* p = dynamic_cast<Player*>(&entity)) {
    TryPush(*p);
  } else if (auto* b = dynamic_cast<Boat*>(&entity)) {
    if (b->Rider() != nullptr) {
      TryPush(*b->Rider());
    }
  }
}

void Boat::Tick() {
  if (removed) return;
  ++tick_count_;
  if (exit_timer_ > 0) --exit_timer_;
  if (level && level->GetTile(x >> 4, y >> 4)->id == level::Tiles::kLava) {
    BreakBoat();
    return;
  }
  if (rider_ != nullptr) {
    if (rider_->removed || rider_->GetBoat() != this) {
      rider_ = nullptr;
    } else {
      rider_->x = x;
      rider_->y = y;
    }
    push_dir_ = Direction::kNone;
    push_time_ = 0;
  } else {
    AdvancePush();
  }
}

void Boat::Drive(int32_t xa, int32_t ya, Direction d) {
  static constexpr int32_t kMoveSpeed = 1;
  static constexpr int32_t kStaminaCostTime = 45;

  if (rider_ && unit_move_counter_ >= kStaminaCostTime) {
    rider_->PayStamina(1);
    unit_move_counter_ -= kStaminaCostTime;
  }

  const bool in_lava = IsInLava();
  const bool in_water = IsInWater();
  if (in_lava) {
    if (tick_count_ % 2 != 0) return;
  } else if (!in_water && tick_count_ % 4 != 0) {
    return;
  }

  if (d != Direction::kNone) dir_ = d;
  const int32_t xd = xa * kMoveSpeed;
  const int32_t yd = ya * kMoveSpeed;
  if (rider_ && rider_->stamina > 0 && (xd != 0 || yd != 0) && Move(xd, yd)) {
    if (!(in_water || in_lava) || (in_lava && tick_count_ % 4 == 0) ||
        (in_water && tick_count_ % 2 != 0)) {
      ++walk_dist_;
    }
    rider_->dir = dir_;
    rider_->walk_dist = walk_dist_;
    rider_->x = x;
    rider_->y = y;
    ++unit_move_counter_;
  } else {
    if (rider_ && rider_->dir != dir_) {
      rider_->dir = dir_;
    }
    if (unit_move_counter_ > 0) --unit_move_counter_;
  }
}

void Boat::SetRider(Player* rider) { rider_ = rider; }

void Boat::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite_;
  const int32_t wf = (walk_dist_ >> 3) & 1;
  int32_t xt = 0;
  int32_t yt = 0;
  switch (dir_) {
    case Direction::kDown:
      xt = wf * 3;
      yt = 0;
      break;
    case Direction::kUp:
      xt = (wf + 2) * 3;
      yt = 0;
      break;
    case Direction::kLeft:
      xt = wf * 3;
      yt = 3;
      break;
    case Direction::kRight:
      xt = (wf + 2) * 3;
      yt = 3;
      break;
    default:
      break;
  }
  copy.SetSpritePos(xt, yt).SetSpriteSize(3, 3).SetMirror(0);
  screen->Render(x - 12, y - 12, copy);
}

bool Boat::Blocks(const Entity& e) const {
  if (&e == rider_) return false;
  return e.IsSolid();
}

bool Boat::Interact(Player& player, item::Item* /*item*/,
                    Direction /*attack_dir*/) {
  return Use(player);
}

bool Boat::Use(Player& player) {
  if (exit_timer_ > 0) return false;
  if (rider_ == &player) {
    player.ExitBoat();
    rider_ = nullptr;
    exit_timer_ = 10;
    return true;
  }
  if (rider_ == nullptr && player.GetBoat() == nullptr) {
    rider_ = &player;
    player.EnterBoat(this);
    exit_timer_ = 10;
    return true;
  }
  return false;
}

void Boat::BreakBoat() {
  if (removed) return;
  if (rider_ != nullptr) {
    rider_->ExitBoat();
    rider_ = nullptr;
  }
  if (level) {
    level->DropItem(x, y, item::Items::Get("Boat"));
    io::Sound::Play("monsterhurt");
  }
  Remove();
}

void Boat::Hurt(Mob& /*mob*/, int32_t /*dmg*/, Direction /*attack_dir*/) {
  BreakBoat();
}

}  // namespace minicraft::entity
