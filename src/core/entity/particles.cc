#include "core/entity/particles.h"

#include <cmath>

#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/level/level.h"
#include "core/util/random.h"

namespace minicraft::entity {

Particle::Particle(int32_t x, int32_t y, int32_t time, gfx::LinkedSprite sprite)
    : Entity(1, 1),
      life_time_(time),
      sprite_(std::move(sprite)) {
  this->x = x;
  this->y = y;
}

Particle::Particle(int32_t x, int32_t y, int32_t time, gfx::LinkedSprite sprite,
                   bool has_light)
    : Entity(1, 1),
      life_time_(time),
      light_radius_(has_light ? 2 : 0),
      sprite_(std::move(sprite)) {
  this->x = x;
  this->y = y;
}

void Particle::Tick() {
  ++time_;
  if (time_ > life_time_) {
    Remove();
  }
}

void Particle::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite_;
  screen->Render(x - 8, y - 8, copy);
}

TextParticle::TextParticle(std::string msg, int32_t x, int32_t y, int32_t col)
    : Particle(x, y, 60, gfx::LinkedSprite()),
      msg_(std::move(msg)),
      col_(col) {
  xx_ = static_cast<double>(x);
  yy_ = static_cast<double>(y);
  zz_ = 2.0;
  auto& rng = util::GlobalRandom();
  xa_ = rng.NextGaussian() * 0.3;
  ya_ = rng.NextGaussian() * 0.2;
  za_ = rng.NextFloat() * 0.7 + 2.0;
}

void TextParticle::Tick() {
  ++time_;
  if (time_ > 60) {
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
  x = static_cast<int32_t>(xx_);
  y = static_cast<int32_t>(yy_);
}

void TextParticle::Render(gfx::Screen* screen) const {
  if (!screen) return;
  const int32_t dx = x - static_cast<int32_t>(msg_.size()) * 4;
  const int32_t dy = y - static_cast<int32_t>(zz_);
  gfx::Font::Draw(msg_, screen, dx + 1, dy + 1, gfx::color::kBlack);
  gfx::Font::Draw(msg_, screen, dx, dy, col_);
}

bool TextParticle::HasTextParticle(const level::Level& lvl) {
  for (const auto& e : lvl.Entities()) {
    if (e && e->GetEntityTypeName() == "TextParticle") return true;
  }
  return false;
}

FireParticle::FireParticle(int32_t x, int32_t y)
    : Particle(x, y, 25 + util::GlobalRandom().NextInt(20),
               gfx::LinkedSprite(gfx::SpriteType::kEntity, "spark")) {
  xx_ = static_cast<double>(x);
  yy_ = static_cast<double>(y);
  auto& rng = util::GlobalRandom();
  xa_ = rng.NextGaussian() * 0.4;
  ya_ = -0.35 - rng.NextFloat() * 0.6;
  max_time_ = life_time_;
  light_radius_ = 2;
}

void FireParticle::Tick() {
  Particle::Tick();
  xx_ += xa_;
  yy_ += ya_;
  xa_ *= 0.94;
  x = static_cast<int32_t>(xx_);
  y = static_cast<int32_t>(yy_);
}

void FireParticle::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite_;
  copy.SetSpritePos(0, 0).SetSpriteDim(0, 0, 1, 1).SetColor(
      gfx::color::Get(255, 255, std::max(60, 220 - time_ * 4), 40));
  screen->Render(x - 4, y - 4, copy);
}

BurnParticle::BurnParticle(int32_t x, int32_t y) : FireParticle(x, y) {
  light_radius_ = 3;
}

SmashParticle::SmashParticle(int32_t x, int32_t y)
    : Particle(x, y, 10,
               gfx::LinkedSprite(gfx::SpriteType::kEntity, "smash")) {
  io::Sound::Play("monsterhurt");
}

SandParticle::SandParticle(int32_t x, int32_t y)
    : Particle(x, y, 45,
               gfx::LinkedSprite(gfx::SpriteType::kEntity, "sand_footsteps")),
      dir_(util::GlobalRandom().NextInt(2)) {}

void SandParticle::Tick() { Particle::Tick(); }

void SandParticle::Render(gfx::Screen* screen) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite_;
  copy.SetMirror(dir_);
  screen->Render(x - 4, y - 4, copy);
}

WaterParticle::WaterParticle(int32_t x, int32_t y)
    : Particle(x, y, 20,
               gfx::LinkedSprite(gfx::SpriteType::kEntity, "splash_0")) {}

WaterParticle::WaterParticle(int32_t x, int32_t y, int32_t life_time,
                             gfx::LinkedSprite sprite, int32_t dest_x,
                             int32_t dest_y)
    : Particle(x, y, life_time, std::move(sprite)),
      has_dest_(true),
      start_x_(x),
      start_y_(y),
      dest_x_(dest_x),
      dest_y_(dest_y) {}

void WaterParticle::Tick() {
  Particle::Tick();
  if (has_dest_) {
    const double t =
        life_time_ > 0 ? static_cast<double>(time_) / life_time_ : 1.0;
    x = start_x_ + static_cast<int32_t>((dest_x_ - start_x_) * t);
    y = start_y_ + static_cast<int32_t>((dest_y_ - start_y_) * t);
  } else {
    int32_t frame = (time_ / 5) % 4;
    sprite_ = gfx::LinkedSprite(gfx::SpriteType::kEntity,
                                "splash_" + std::to_string(frame));
  }
}

}  // namespace minicraft::entity
