#include "core/entity/projectiles.h"

#include <cmath>
#include <vector>

#include "core/entity/mob.h"
#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/gfx/color.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/item.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/random.h"

namespace minicraft::entity {

ItemEntity::ItemEntity(std::unique_ptr<item::Item> it, int32_t x, int32_t y)
    : Entity(2, 2), item(std::move(it)) {
  this->x = x;
  this->y = y;
  xx_ = static_cast<double>(x);
  yy_ = static_cast<double>(y);
  zz_ = 2.0;
  auto& rng = util::GlobalRandom();
  life_time_ = 60 * 10 + rng.NextInt(70);
  xa_ = rng.NextGaussian() * 0.3;
  ya_ = rng.NextGaussian() * 0.2;
  za_ = rng.NextFloat() * 0.7 + 1.0;
}

ItemEntity::ItemEntity(std::unique_ptr<item::Item> it, int32_t x, int32_t y,
                       double zz, int32_t life_time, double xa, double ya,
                       double za)
    : Entity(2, 2),
      item(std::move(it)),
      life_time_(life_time),
      xa_(xa),
      ya_(ya),
      za_(za),
      xx_(static_cast<double>(x)),
      yy_(static_cast<double>(y)),
      zz_(zz) {
  this->x = x;
  this->y = y;
}

void ItemEntity::Tick() {
  ++time_;
  if (time_ >= life_time_) {
    Remove();
    return;
  }

  xx_ += xa_;
  yy_ += ya_;
  zz_ += za_;
  if (zz_ < 0.0) {
    zz_ = 0.0;
    za_ *= -0.5;
    xa_ *= 0.6;
    ya_ *= 0.6;
  }
  za_ -= 0.15;

  const int32_t ox = x;
  const int32_t oy = y;
  const int32_t nx = static_cast<int32_t>(xx_);
  const int32_t ny = static_cast<int32_t>(yy_);
  const int32_t expectedx = nx - x;
  const int32_t expectedy = ny - y;

  Move(nx - x, ny - y);

  const int32_t gotx = x - ox;
  const int32_t goty = y - oy;
  xx_ += static_cast<double>(gotx - expectedx);
  yy_ += static_cast<double>(goty - expectedy);
}

void ItemEntity::Render(gfx::Screen* screen) const {
  if (!screen || !item) return;
  if (time_ >= life_time_ - 6 * 20) {
    if (time_ / 6 % 2 == 0) return;
  }
  gfx::LinkedSprite shadow = item->sprite;
  shadow.SetColor(gfx::color::Get(0, 31));
  screen->Render(x - 4, y - 4, shadow);
  gfx::LinkedSprite copy = item->sprite;
  screen->Render(x - 4, y - 4 - static_cast<int32_t>(zz_), copy);
}

void ItemEntity::TouchedBy(Entity& entity) {
  if (time_ <= 30 || picked_up_) return;
  if (auto* p = dynamic_cast<Player*>(&entity)) {
    p->PickupItem(*this);
    picked_up_ = picked_up_ || removed;
  }
}

void ItemEntity::Take(Player& player) {
  io::Sound::Play("pickup");
  player.AddScore(1);
  Remove();
}

Arrow::Arrow(Mob* owner, Direction dir, int32_t dmg)
    : Arrow(owner, owner ? owner->x : 0, owner ? owner->y : 0, dir, dmg) {}

Arrow::Arrow(Mob* owner, int32_t x, int32_t y, Direction dir, int32_t dmg)
    : Entity(std::abs(DirX(dir)) + 1, std::abs(DirY(dir)) + 1),
      owner_(owner),
      dir_(dir),
      damage_(dmg),
      sprite_(gfx::SpriteType::kEntity, "arrow") {
  this->x = x;
  this->y = y;
  if (damage_ > 3) {
    speed_ = 8;
  } else if (damage_ >= 0) {
    speed_ = 7;
  } else {
    speed_ = 6;
  }
}

void Arrow::Tick() {
  if (!level || x < 0 || (x >> 4) > level->w || y < 0 || (y >> 4) > level->h) {
    Remove();
    return;
  }
  x += DirX(dir_) * speed_;
  y += DirY(dir_) * speed_;

  std::vector<Entity*> hits =
      level->GetEntitiesInRect(gfx::Rectangle(x, y, 0, 0, gfx::Rectangle::kCenterDims));
  const bool bonus_crit = util::GlobalRandom().NextInt(11) >= 9;
  for (Entity* e : hits) {
    if (e == this || e == owner_) continue;
    const int32_t xt = x >> 4;
    const int32_t yt = y >> 4;
    const level::Tile* tile = level->GetTile(xt, yt);
    if (!tile->MayPass(*level, xt, yt, *this) && !tile->connects_to_fluid &&
        tile->id != level::Tiles::kInfiniteFall) {
      Remove();
      return;
    }
    if (auto* mob = dynamic_cast<Mob*>(e)) {
      if (owner_ != nullptr) {
        const int32_t bonus =
            (dynamic_cast<Player*>(mob) ? 0 : 3) + (bonus_crit ? 1 : 0);
        const int32_t final_dmg =
            mob->CalculateEntityDamage(this, damage_ + bonus);
        if (final_dmg > 0) {
          mob->Hurt(*owner_, final_dmg, dir_);
        }
      }
    }
  }

  const int32_t xt = x >> 4;
  const int32_t yt = y >> 4;
  const level::Tile* tile = level->GetTile(xt, yt);
  if (!tile->MayPass(*level, xt, yt, *this) && !tile->connects_to_fluid &&
      tile->id != level::Tiles::kInfiniteFall) {
    Remove();
    return;
  }
}

void Arrow::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite_;
  int32_t xt = 0;
  int32_t mirror = 0;
  switch (dir_) {
    case Direction::kUp:
      xt = 0;
      mirror = 0;
      break;
    case Direction::kDown:
      xt = 0;
      mirror = 2;
      break;
    case Direction::kRight:
      xt = 1;
      mirror = 0;
      break;
    case Direction::kLeft:
      xt = 1;
      mirror = 1;
      break;
    default:
      break;
  }
  copy.SetSpritePos(xt, 0).SetSpriteSize(1, 1).SetMirror(mirror);
  screen->Render(x - 4, y - 4, copy);
}

Spark::Spark(Mob* owner, double xa, double ya, int32_t life_time)
    : Entity(0, 0),
      owner_(owner),
      life_time_(life_time >= 0 ? life_time
                                : 60 * 6 + util::GlobalRandom().NextInt(30)),
      xa_(xa),
      ya_(ya),
      sprite_(gfx::SpriteType::kEntity, "spark") {
  if (owner) {
    x = owner->x;
    y = owner->y;
  }
  xx_ = static_cast<double>(x);
  yy_ = static_cast<double>(y);
}

void Spark::Tick() {
  if (time_ == 0) {
    xx_ = static_cast<double>(x);
    yy_ = static_cast<double>(y);
  }
  ++time_;
  if (time_ >= life_time_) {
    Remove();
    return;
  }
  xx_ += xa_;
  yy_ += ya_;
  x = static_cast<int32_t>(xx_);
  y = static_cast<int32_t>(yy_);

  if (!level || !owner_) return;
  if (Player* player = level->GetClosestPlayer(x, y)) {
    if (player->IsWithin(0, *this)) {
      player->Hurt(*owner_, 1, Direction::kNone);
    }
  }
}

void Spark::Render(gfx::Screen* screen) const {
  if (!screen) return;
  if (time_ >= life_time_ - 6 * 20 && (time_ / 6) % 2 == 0) return;
  const int32_t rand_mirror = util::GlobalRandom().NextInt(4);
  gfx::LinkedSprite shadow = sprite_;
  shadow.SetMirror(rand_mirror).SetColor(gfx::color::kBlack);
  screen->Render(x - 4, y - 4 + 2, shadow);

  gfx::LinkedSprite copy = sprite_;
  copy.SetMirror(rand_mirror);
  screen->Render(x - 4, y - 4 - 2, copy);
}

FireSpark::FireSpark(Mob* owner, double xa, double ya)
    : Spark(owner, xa, ya, 60 * 3 + util::GlobalRandom().NextInt(20)) {}

void FireSpark::Tick() {
  if (time_ == 0) {
    xx_ = static_cast<double>(x);
    yy_ = static_cast<double>(y);
  }
  ++time_;
  if (time_ >= life_time_) {
    Remove();
    return;
  }
  if (stopped_) {
    ++stopped_time_;
    if (stopped_time_ >= 50) {
      Remove();
      return;
    }
  } else {
    xx_ += xa_;
    yy_ += ya_;
    const int32_t dx = static_cast<int32_t>(xx_) - x;
    const int32_t dy = static_cast<int32_t>(yy_) - y;
    if (!Move(dx, dy)) {
      stopped_ = true;
    }
  }

  if (!level) return;
  if (Player* p = level->GetClosestPlayer(x, y)) {
    if (p->IsWithin(0, *this)) {
      p->Burn(5);
    }
  }
}

void FireSpark::Render(gfx::Screen* screen) const {
  if (!screen) return;
  if (time_ >= life_time_ - 6 * 20 && (time_ / 6) % 2 == 0) return;
  const int32_t rand_mirror = util::GlobalRandom().NextInt(4);
  gfx::LinkedSprite shadow = sprite_;
  shadow.SetMirror(rand_mirror).SetColor(gfx::color::kBlack);
  screen->Render(x - 4, y - 4 + 2, shadow);

  gfx::LinkedSprite copy = sprite_;
  copy.SetMirror(rand_mirror).SetColor(gfx::color::Get(255, 255, 110, 20));
  screen->Render(x - 4, y - 4 - 2, copy);
}

ExplosionTileTicker::ExplosionTileTicker(int32_t tx, int32_t ty,
                                         int32_t max_delay)
    : Entity(1, 1), tx_(tx), ty_(ty), max_delay_(max_delay) {
  x = tx * 16 + 8;
  y = ty * 16 + 8;
}

void ExplosionTileTicker::Tick() {
  ++time_;
  if (time_ >= max_delay_) {
    if (level) {
      if (level->GetTile(tx_, ty_)->id == level::Tiles::kExplode) {
        uint16_t fallback = (level->depth > 0)
                                ? level::Tiles::kInfiniteFall
                                : level::Tiles::kHole;
        level->SetTile(tx_, ty_, fallback, 0);
      }
    }
    Remove();
  }
}

}  // namespace minicraft::entity
