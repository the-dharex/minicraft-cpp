#ifndef MINICRAFT_CORE_ENTITY_PARTICLES_H_
#define MINICRAFT_CORE_ENTITY_PARTICLES_H_

#include <cstdint>
#include <string>

#include "core/entity/entity.h"
#include "core/gfx/sprite_atlas.h"

namespace minicraft::entity {

class Particle : public Entity {
 public:
  Particle(int32_t x, int32_t y, int32_t time, gfx::LinkedSprite sprite);
  Particle(int32_t x, int32_t y, int32_t time, gfx::LinkedSprite sprite,
           bool has_light);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  bool IsSolid() const override { return false; }
  bool Blocks(const Entity& e) const override { return false; }
  int32_t GetLightRadius() const override { return light_radius_; }
  std::string GetEntityTypeName() const override { return "Particle"; }

 protected:
  int32_t time_ = 0;
  int32_t life_time_ = 60;
  int32_t light_radius_ = 0;
  gfx::LinkedSprite sprite_;
};

class TextParticle : public Particle {
 public:
  TextParticle(std::string msg, int32_t x, int32_t y, int32_t col);

  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  std::string GetEntityTypeName() const override { return "TextParticle"; }

  static bool HasTextParticle(const level::Level& lvl);

 private:
  std::string msg_;
  int32_t col_ = 0;
  double xa_ = 0.0;
  double ya_ = 0.0;
  double za_ = 0.0;
  double xx_ = 0.0;
  double yy_ = 0.0;
  double zz_ = 0.0;
};

class FireParticle : public Particle {
 public:
  FireParticle(int32_t x, int32_t y);
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  std::string GetEntityTypeName() const override { return "FireParticle"; }

 private:
  double xa_ = 0.0;
  double ya_ = 0.0;
  double xx_ = 0.0;
  double yy_ = 0.0;
  int32_t max_time_ = 40;
};

class BurnParticle : public FireParticle {
 public:
  BurnParticle(int32_t x, int32_t y);
  std::string GetEntityTypeName() const override { return "BurnParticle"; }
};

class SmashParticle : public Particle {
 public:
  SmashParticle(int32_t x, int32_t y);
  std::string GetEntityTypeName() const override { return "SmashParticle"; }
};

class SandParticle : public Particle {
 public:
  SandParticle(int32_t x, int32_t y);
  void Tick() override;
  void Render(gfx::Screen* screen) const override;
  std::string GetEntityTypeName() const override { return "SandParticle"; }

 private:
  int32_t dir_ = 0;
};

class WaterParticle : public Particle {
 public:
  WaterParticle(int32_t x, int32_t y);
  WaterParticle(int32_t x, int32_t y, int32_t life_time,
                gfx::LinkedSprite sprite, int32_t dest_x, int32_t dest_y);
  void Tick() override;
  std::string GetEntityTypeName() const override { return "WaterParticle"; }

 private:
  bool has_dest_ = false;
  int32_t start_x_ = 0;
  int32_t start_y_ = 0;
  int32_t dest_x_ = 0;
  int32_t dest_y_ = 0;
};

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_PARTICLES_H_
