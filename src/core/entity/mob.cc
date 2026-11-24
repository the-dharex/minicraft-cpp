#include "core/entity/mob.h"

#include <algorithm>
#include <cmath>

#include "core/entity/furniture.h"
#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/entity/projectiles.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/advancements.h"
#include "core/util/random.h"

namespace minicraft::entity {

Mob::Mob(int32_t xr, int32_t yr, int32_t max_hp)
    : Entity(xr, yr), health(max_hp), max_health(max_hp) {}

void Mob::Tick() {
  ++tick_time;
  if (removed) return;
  ++no_action_time_;

  if (level) {
    const level::Tile* under = level->GetTile(x >> 4, y >> 4);
    if (under && under->id == level::Tiles::kLava) {
      Hurt(*under, x, y, 4);
    }
  }

  if (CanBurn() && burning_duration > 0) {
    if (level) {
      if (level->GetTile(x >> 4, y >> 4)->id == level::Tiles::kWater) {
        burning_duration = 0;
      } else {
        if (burning_duration % 10 == 0) {
          auto& rng = util::GlobalRandom();
          level->Add(std::make_shared<BurnParticle>(
              x - 8 + (rng.NextInt(8) - 4), y - 8 + (rng.NextInt(8) - 4)));
        }
        --burning_duration;
        if (auto* p = dynamic_cast<Player*>(this)) {
          if (burning_duration % 70 == 0 && !p->HasLavaEffect()) {
            Hurt(*this, 1, Direction::kNone);
          }
        } else if (burning_duration % 70 == 0) {
          Hurt(*this, 2, Direction::kNone);
        }
      }
    } else {
      --burning_duration;
      if (burning_duration % 70 == 0) {
        const bool is_player = dynamic_cast<Player*>(this) != nullptr;
        Hurt(*this, is_player ? 1 : 2, Direction::kNone);
      }
    }
  }
  if (health <= 0) {
    Die();
    return;
  }
  if (hurt_time > 0) --hurt_time;

  // Knockback movement in Tick
  int32_t xd = 0;
  int32_t yd = 0;
  if (x_knockback != 0) {
    xd = (x_knockback > 0) ? ((x_knockback + 1) / 2) : -((-x_knockback + 1) / 2);
    x_knockback -= (x_knockback > 0 ? 1 : -1);
  }
  if (y_knockback != 0) {
    yd = (y_knockback > 0) ? ((y_knockback + 1) / 2) : -((-y_knockback + 1) / 2);
    y_knockback -= (y_knockback > 0 ? 1 : -1);
  }
  if (xd != 0 || yd != 0) {
    const Direction saved_dir = dir;
    Move(xd, yd);
    dir = saved_dir;
  }
}

void Mob::Burn(int32_t seconds) {
  if (!CanBurn()) return;
  burning_duration = seconds * 60;
}

bool Mob::IsSwimming() const {
  if (!level) return false;
  const uint16_t tid = level->GetTile(x >> 4, y >> 4)->id;
  return tid == level::Tiles::kWater || tid == level::Tiles::kLava;
}

bool Mob::IsWooling() const {
  if (!level) return false;
  return level::Tiles::IsWool(level->GetTile(x >> 4, y >> 4)->id);
}

bool Mob::Move(int32_t xa, int32_t ya) {
  const bool is_player = dynamic_cast<const Player*>(this) != nullptr;
  if (IsSwimming()) {
    if (swim_timer++ % 2 == 0) {
      if (is_player) {
        if (!static_cast<const Player*>(this)->HasSwimEffect()) return true;
      } else {
        return true;
      }
    }
  } else if (!is_player && CanWool() && IsWooling()) {
    if (tick_time % 2 == 0) return true;
  }

  if (walk_time_ > 1 && tick_time % walk_time_ == 0) return true;
  if (!is_player && hurt_time > 0) return true;

  // Prevent walking in the opposite direction against active knockback.
  if (x_knockback != 0 && ((xa < 0 && x_knockback > 0) || (xa > 0 && x_knockback < 0))) {
    xa = 0;
  }
  if (y_knockback != 0 && ((ya < 0 && y_knockback > 0) || (ya > 0 && y_knockback < 0))) {
    ya = 0;
  }

  if (xa != 0 || ya != 0) {
    ++walk_dist;
    Direction new_dir = GetDirection(xa, ya);
    if (new_dir != Direction::kNone) dir = new_dir;
  }
  return Entity::Move(xa, ya);
}

void Mob::Hurt(Mob& mob, int32_t dmg, Direction attack_dir) {
  if (dynamic_cast<const Player*>(&mob) != nullptr &&
      game::GameState::Instance().IsCreativeMode() && &mob != this) {
    DoHurt(health, attack_dir);
  } else {
    DoHurt(dmg, attack_dir);
  }
}

void Mob::Heal(int32_t amount) {
  if (hurt_time > 0 || amount <= 0) return;
  if (level) {
    level->Add(std::make_shared<TextParticle>(std::to_string(amount), x, y,
                                              gfx::color::kGreen));
  }
  health = std::min(max_health, health + amount);
}

void Mob::Hurt(const level::Tile& /*tile*/, int32_t /*x*/, int32_t /*y*/,
               int32_t dmg) {
  Direction adir = Direction::kDown;
  switch (dir) {
    case Direction::kDown:
      adir = Direction::kUp;
      break;
    case Direction::kUp:
      adir = Direction::kDown;
      break;
    case Direction::kLeft:
      adir = Direction::kRight;
      break;
    case Direction::kRight:
      adir = Direction::kLeft;
      break;
    default:
      break;
  }
  DoHurt(dmg, adir);
}

void Mob::DoHurt(int32_t dmg, Direction attack_dir) {
  if (hurt_time > 0) return;

  if (level) {
    level->Add(std::make_shared<TextParticle>(std::to_string(dmg), x, y,
                                              gfx::color::kRed));
  }
  health -= dmg;
  if (attack_dir == Direction::kLeft) x_knockback = -6;
  if (attack_dir == Direction::kRight) x_knockback = +6;
  if (attack_dir == Direction::kUp) y_knockback = -6;
  if (attack_dir == Direction::kDown) y_knockback = +6;
  hurt_time = 10;
}

void Mob::Die() { Remove(); }

bool Mob::CheckStartPos(const level::Level& lvl, int32_t x, int32_t y) {
  const uint16_t tid = lvl.GetTile(x >> 4, y >> 4)->id;
  return tid != level::Tiles::kWater && tid != level::Tiles::kLava &&
         tid != level::Tiles::kHole && tid != level::Tiles::kInfiniteFall &&
         tid != level::Tiles::kRock && tid != level::Tiles::kHardRock &&
         tid != level::Tiles::kBossWall;
}

bool Mob::FindStartPos(const level::Level& lvl, int32_t attempts) {
  auto& rng = util::GlobalRandom();
  for (int32_t i = 0; i < attempts; ++i) {
    int32_t rx = (rng.NextInt(lvl.w - 8) + 4) * 16 + 8;
    int32_t ry = (rng.NextInt(lvl.h - 8) + 4) * 16 + 8;
    if (CheckStartPos(lvl, rx, ry)) {
      x = rx;
      y = ry;
      return true;
    }
  }
  return false;
}

std::shared_ptr<Mob> Mob::CreateByName(std::string_view name, int32_t lvl) {
  if (name == "Cow") return std::make_shared<Cow>();
  if (name == "Pig") return std::make_shared<Pig>();
  if (name == "Sheep") return std::make_shared<Sheep>();
  if (name == "Slime") return std::make_shared<Slime>(lvl);
  if (name == "Zombie") return std::make_shared<Zombie>(lvl);
  if (name == "Skeleton") return std::make_shared<Skeleton>(lvl);
  if (name == "Creeper") return std::make_shared<Creeper>(lvl);
  if (name == "Knight") return std::make_shared<Knight>(lvl);
  if (name == "Snake") return std::make_shared<Snake>(lvl);
  if (name == "AirWizard") return std::make_shared<AirWizard>();
  if (name == "ObsidianKnight") return std::make_shared<ObsidianKnight>();
  return nullptr;
}

MobAi::MobAi(gfx::LinkedSprite sprite, int32_t max_hp, int32_t lifetime_secs,
             int32_t rw_chance, int32_t rw_duration)
    : Mob(4, 3, max_hp),
      sprite_(std::move(sprite)),
      random_walk_chance_(rw_chance),
      random_walk_duration_(rw_duration),
      lifetime_(lifetime_secs * 60) {
  walk_time_ = 2;
  auto& rng = util::GlobalRandom();
  dir = DirectionFromIndex(rng.NextInt(4) + 1);
}

bool MobAi::SkipTick() const {
  return slow_tick_ && (tick_time + 1) % 4 == 0;
}

bool MobAi::IsWithinLight() const {
  if (!level) return false;
  for (Entity* e : level->GetEntitiesInRect(
           gfx::Rectangle(x, y, 16, 16, gfx::Rectangle::kCenterDims))) {
    if (auto* lantern = dynamic_cast<Lantern*>(e)) {
      if (IsWithin(lantern->GetLightRadius(), *lantern)) return true;
    }
  }
  const int32_t tx = x >> 4;
  const int32_t ty = y >> 4;
  for (const gfx::Point& p : level->GetAreaTilePositions(tx, ty, 5)) {
    const int32_t lr = level->GetTile(p.x, p.y)->GetLightRadius(*level, p.x, p.y);
    if (lr <= 0) continue;
    const int32_t xx = std::abs(tx - p.x);
    const int32_t yy = std::abs(ty - p.y);
    if (xx * xx + yy * yy <= lr * lr) return true;
  }
  return false;
}

void MobAi::RandomizeWalkDir(bool by_chance) {
  auto& rng = util::GlobalRandom();
  if (!by_chance && rng.NextInt(std::max<int32_t>(1, random_walk_chance_)) != 0) {
    return;
  }
  random_walk_time_ = random_walk_duration_;
  xa_ = rng.NextInt(3) - 1;
  ya_ = rng.NextInt(3) - 1;
}

void MobAi::HandleDespawn() {
  if (removed || !level) return;
  const Player* p = level->GetClosestPlayer(x, y);
  if (!p) return;
  const double dist = std::hypot(static_cast<double>(p->x - x),
                                 static_cast<double>(p->y - y));
  constexpr double kDespawnDist = 1280.0;
  constexpr double kNoDespawnDist = 640.0;
  if (dist > kDespawnDist && RemoveWhenFarAway()) {
    Remove();
    return;
  }
  if (no_action_time_ > 1800 && util::GlobalRandom().NextInt(800) == 0 &&
      dist > kNoDespawnDist && RemoveWhenFarAway()) {
    Remove();
  } else if (dist < kNoDespawnDist) {
    no_action_time_ = 0;
  }
}

void MobAi::Tick() {
  Mob::Tick();
  if (removed) return;
  ++age_;
  if (lifetime_ > 0 && age_ > lifetime_) {
    if (level && level->GetClosestPlayer(x, y)) {
      const Player* p = level->GetClosestPlayer(x, y);
      const int64_t dx = p->x - x;
      const int64_t dy = p->y - y;
      if (dx * dx + dy * dy > 640LL * 640LL) {
        Remove();
        return;
      }
    }
  }

  if (level) {
    bool found_time_player = false;
    for (const auto& p : level->Players()) {
      if (p && !p->removed && p->IsWithin(8, *this) &&
          p->potion_effects.count(static_cast<int32_t>(Player::PotionEffect::kTime)) > 0) {
        found_time_player = true;
        break;
      }
    }
    slow_tick_ = found_time_player;
  }

  if (SkipTick()) return;

  if (!Move(xa_ * speed_, ya_ * speed_)) {
    xa_ = 0;
    ya_ = 0;
  }

  auto& rng = util::GlobalRandom();
  if (rng.NextInt(std::max<int32_t>(1, random_walk_chance_)) == 0) {
    RandomizeWalkDir(true);
  }

  if (random_walk_time_ > 0) {
    --random_walk_time_;
  }
}

namespace {

void MobDirectionSprite(Direction dir, int32_t walk_dist, int32_t* out_xt,
                        int32_t* out_mirror) {
  const int32_t walk_frame = (walk_dist >> 3) & 1;
  int32_t xt = 0;
  int32_t mirror = 0;
  switch (dir) {
    case Direction::kDown:
      xt = 0;
      mirror = walk_frame;
      break;
    case Direction::kUp:
      xt = 2;
      mirror = walk_frame;
      break;
    case Direction::kRight:
      xt = 4 + 2 * walk_frame;
      mirror = 0;
      break;
    case Direction::kLeft:
      xt = 4 + 2 * walk_frame;
      mirror = 1;
      break;
    default:
      break;
  }
  *out_xt = xt;
  *out_mirror = mirror;
}

}  // namespace

void MobAi::RenderMobSprite(gfx::Screen* screen, int32_t xt, int32_t yt,
                            int32_t mirror, int32_t tint) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite_;
  copy.SetSpritePos(xt, yt).SetSpriteSize(2, 2).SetMirror(mirror).SetFlip(mirror);
  if (hurt_time > 0) {
    copy.SetColor(gfx::color::kWhite);
  } else if (tint != -1) {
    copy.SetColor(tint);
  }
  screen->Render(x - 8, y - 11, copy);
}

void MobAi::Render(gfx::Screen* screen) const {
  int32_t xt = 0;
  int32_t mirror = 0;
  MobDirectionSprite(dir, walk_dist, &xt, &mirror);
  RenderMobSprite(screen, xt, 0, mirror);
}

void MobAi::DoHurt(int32_t dmg, Direction attack_dir) {
  if (removed || hurt_time > 0) return;

  if (level) {
    if (Player* player = level->GetClosestPlayer(x, y)) {
      // Only play hurt audio when the player is within 5 tiles (80 px).
      const int32_t xd = player->x - x;
      const int32_t yd = player->y - y;
      if (xd * xd + yd * yd < 80 * 80) {
        io::Sound::Play("monsterhurt");
      }
    }
  }
  Mob::DoHurt(dmg, attack_dir);
}

bool MobAi::CheckStartPos(const level::Level& lvl, int32_t x, int32_t y,
                          int32_t player_dist, int32_t solo_radius) {
  if (const Player* p = lvl.GetClosestPlayer(x, y)) {
    const int64_t xd = p->x - x;
    const int64_t yd = p->y - y;
    if (xd * xd + yd * yd < static_cast<int64_t>(player_dist) * player_dist) {
      return false;
    }
  }

  const int32_t r = lvl.monster_density * solo_radius;
  if (!lvl.GetEntitiesInRect(
             gfx::Rectangle(x, y, r * 2, r * 2, gfx::Rectangle::kCenterDims))
           .empty()) {
    return false;
  }

  return lvl.GetTile(x >> 4, y >> 4)->MaySpawn();
}

PassiveMob::PassiveMob(gfx::LinkedSprite sprite, int32_t max_hp)
    : MobAi(std::move(sprite), max_hp, 5 * 60, 40, 45) {}

void PassiveMob::HandleDespawn() {
  if (IsWithinLight()) return;
  MobAi::HandleDespawn();
}

void PassiveMob::RandomizeWalkDir(bool by_chance) {
  auto& rng = util::GlobalRandom();
  if ((xa_ == 0 && ya_ == 0 && rng.NextInt(5) == 0) || by_chance ||
      rng.NextInt(std::max<int32_t>(1, random_walk_chance_)) == 0) {
    random_walk_time_ = random_walk_duration_;
    xa_ = (rng.NextInt(3) - 1) * rng.NextInt(2);
    ya_ = (rng.NextInt(3) - 1) * rng.NextInt(2);
  }
}

void PassiveMob::Die() { MobAi::Die(); }

bool PassiveMob::CheckStartPos(const level::Level& lvl, int32_t x,
                               int32_t y) {
  const auto& gs = game::GameState::Instance();
  const int32_t r =
      (gs.IsScoreMode() ? 22 : 15) +
      (gs.GetTimeOfDay() == game::TimeOfDay::kNight ? 0 : 5);
  if (!MobAi::CheckStartPos(lvl, x, y, 80, r)) return false;

  const uint16_t tid = lvl.GetTile(x >> 4, y >> 4)->id;
  return tid == level::Tiles::kGrass || tid == level::Tiles::kFlower;
}

EnemyMob::EnemyMob(int32_t lvl, gfx::LinkedSprite sprite, int32_t max_hp,
                   int32_t detect_dist, int32_t lifetime_secs,
                   int32_t rw_chance, int32_t rw_duration)
    : MobAi(std::move(sprite), max_hp, lifetime_secs, rw_chance, rw_duration),
      lvl(std::clamp(lvl, 1, 4)),
      detect_dist_(detect_dist) {
  const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
  const int32_t diff_mult = 1 << diff;
  health = max_health = max_hp * this->lvl * this->lvl * diff_mult;
}

void EnemyMob::HandleDespawn() {
  if (!level || level->depth != 0) return;
  const int32_t gt = game::GameState::Instance().game_time;
  if (gt >= game::GameState::kDayLength / 4 &&
      gt <= game::GameState::kDayLength / 2) {
    MobAi::HandleDespawn();
  }
}

bool EnemyMob::CheckStartPos(const level::Level& lvl, int32_t x, int32_t y) {
  const auto& gs = game::GameState::Instance();
  const int32_t r = (lvl.depth == -4 ? (gs.IsScoreMode() ? 22 : 15) : 13);
  if (!MobAi::CheckStartPos(lvl, x, y, 60, r)) return false;

  const int32_t tx = x >> 4;
  const int32_t ty = y >> 4;
  const uint16_t tid = lvl.GetTile(tx, ty)->id;
  if (lvl.depth == -4) {
    return (tid == level::Tiles::kObsidian) && !lvl.IsLight(tx, ty);
  }
  if (tid == level::Tiles::kStoneDoor || tid == level::Tiles::kWoodDoor ||
      tid == level::Tiles::kObsidianDoor || tid == level::Tiles::kWheat ||
      tid == level::Tiles::kFarmland) {
    return false;
  }
  return !lvl.IsLight(tx, ty);
}

int32_t EnemyMob::GetLevelColor() const {
  switch (lvl) {
    case 1:
      return -1;
    case 2:
      return gfx::color::Get(255, 210, 120, 120);
    case 3:
      return gfx::color::Get(255, 160, 180, 255);
    case 4:
      return gfx::color::Get(255, 255, 220, 110);
    default:
      return -1;
  }
}

void EnemyMob::Tick() {
  MobAi::Tick();
  if (removed || SkipTick()) return;

  const auto& gs = game::GameState::Instance();
  if (level && !gs.bed_sleeping && random_walk_time_ <= 0 &&
      !gs.IsCreativeMode()) {
    Player* p = level->GetClosestPlayer(x, y);
    if (p != nullptr) {
      const int32_t xd = p->x - x;
      const int32_t yd = p->y - y;
      const int32_t dist_sq = xd * xd + yd * yd;
      if (dist_sq < detect_dist_ * detect_dist_) {
        constexpr int32_t kSig0 = 1;
        xa_ = (xd < -kSig0) ? -1 : ((xd > kSig0) ? 1 : 0);
        ya_ = (yd < -kSig0) ? -1 : ((yd > kSig0) ? 1 : 0);
      } else {
        RandomizeWalkDir(false);
      }
    }
  }
}

void EnemyMob::Render(gfx::Screen* screen) const {
  int32_t xt = 0;
  int32_t mirror = 0;
  MobDirectionSprite(dir, walk_dist, &xt, &mirror);
  const int32_t yt = (std::clamp(lvl, 1, 4) - 1) * 2;
  RenderMobSprite(screen, xt, yt, mirror, -1);
}

void EnemyMob::TouchedBy(Entity& entity) {
  if (auto* p = dynamic_cast<Player*>(&entity)) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    p->Hurt(*this, lvl * (diff == 2 ? 2 : 1), dir);
  }
}

void EnemyMob::Die() {
  if (level) {
    const auto& players = level->Players();
    if (!players.empty()) {
      for (const auto& p : players) {
        if (p && !p->removed) {
          p->AddScore(50 * lvl);
          p->AddMultiplier(1);
        }
      }
    } else if (auto* p = level->GetClosestPlayer(x, y)) {
      p->AddScore(50 * lvl);
      p->AddMultiplier(1);
    }
  }
  MobAi::Die();
}

Cow::Cow()
    : PassiveMob(gfx::LinkedSprite(gfx::SpriteType::kEntity, "cow"),
                 5 + 5 * game::GameState::Instance().GetDifficultyIdx()) {}

void Cow::Tick() {
  PassiveMob::Tick();
  if (removed || !level) return;
  const int32_t tx = x >> 4;
  const int32_t ty = y >> 4;
  if (level->GetTile(tx, ty)->id == level::Tiles::kGrass &&
      util::GlobalRandom().NextInt(1000) == 0) {
    level->SetTile(tx, ty, level::Tiles::kDirt, 0);
  }
}

void Cow::Die() {
  if (level) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    const int32_t min_d = (diff <= 1) ? 1 : 0;
    const int32_t max_d = (diff == 0) ? 3 : ((diff == 1) ? 2 : 1);
    level->DropItem(x, y, min_d, max_d, "Leather");
    level->DropItem(x, y, min_d, max_d, "Raw Beef");
  }
  PassiveMob::Die();
}

Pig::Pig()
    : PassiveMob(gfx::LinkedSprite(gfx::SpriteType::kEntity, "pig"),
                 5 + 3 * game::GameState::Instance().GetDifficultyIdx()) {}

void Pig::Die() {
  if (level) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    const int32_t min_d = (diff <= 1) ? 1 : 0;
    const int32_t max_d = (diff == 0) ? 3 : 2;
    level->DropItem(x, y, min_d, max_d, "Raw Pork");
  }
  PassiveMob::Die();
}

Sheep::Sheep(item::DyeColor col)
    : PassiveMob(gfx::LinkedSprite(gfx::SpriteType::kEntity, "sheep"),
                 5 + 3 * game::GameState::Instance().GetDifficultyIdx()),
      color(col) {
  sprite_.SetColor(item::DyeColorRgb(color));
}

void Sheep::Tick() {
  PassiveMob::Tick();
  if (removed || !level) return;
  const int32_t tx = x >> 4;
  const int32_t ty = y >> 4;
  if (level->GetTile(tx, ty)->id == level::Tiles::kGrass &&
      util::GlobalRandom().NextInt(1000) == 0) {
    level->SetTile(tx, ty, level::Tiles::kDirt, 0);
    cut = false;
    regrow_time_ = 0;
  }
}

void Sheep::Render(gfx::Screen* screen) const {
  int32_t xt = 0;
  int32_t mirror = 0;
  MobDirectionSprite(dir, walk_dist, &xt, &mirror);
  RenderMobSprite(screen, xt, cut ? 2 : 0, mirror, item::DyeColorRgb(color));
}

bool Sheep::Interact(Player& player, item::Item* item,
                     Direction /*attack_dir*/) {
  if (item == nullptr) return false;
  if (auto* tool = dynamic_cast<item::ToolItem*>(item)) {
    if (!cut && tool->type == item::ToolType::kShears) {
      cut = true;
      regrow_time_ = 0;
      if (level) {
        std::string wool_name =
            std::string(item::DyeColorDisplayName(color)) + " Wool";
        level->DropItem(x, y, 1, 3, wool_name);
      }
      tool->PayDurability();
      io::Sound::Play("craft");
      return true;
    }
  } else if (auto* dye = dynamic_cast<item::DyeItem*>(item)) {
    color = dye->color;
    sprite_.SetColor(item::DyeColorRgb(color));
    if (!game::GameState::Instance().IsCreativeMode()) {
      --dye->count;
    }
    return true;
  }
  return false;
}

void Sheep::Die() {
  if (level) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    const int32_t min_d = (diff <= 1) ? 1 : 0;
    const int32_t max_d = (diff == 0) ? 3 : 2;
    if (!cut) {
      std::string wool_name =
          std::string(item::DyeColorDisplayName(color)) + " Wool";
      level->DropItem(x, y, min_d, max_d, wool_name);
    }
    level->DropItem(x, y, min_d, max_d, "Raw Beef");
  }
  PassiveMob::Die();
}

Slime::Slime(int32_t lvl)
    : EnemyMob(lvl, gfx::LinkedSprite(gfx::SpriteType::kEntity, "slime"), 1,
               50, 60, 40, 60) {}

bool Slime::Move(int32_t xa, int32_t ya) {
  const bool result = EnemyMob::Move(xa, ya);
  dir = Direction::kDown;
  return result;
}

void Slime::RandomizeWalkDir(bool by_chance) {
  if (jump_time_ > 0) return;
  EnemyMob::RandomizeWalkDir(by_chance);
}

void Slime::Tick() {
  EnemyMob::Tick();
  if (removed) return;

  if (jump_time_ <= -10 && (xa_ != 0 || ya_ != 0)) {
    jump_time_ = 10;
  }
  --jump_time_;
  if (jump_time_ == 0) {
    xa_ = 0;
    ya_ = 0;
  }
}

void Slime::Render(gfx::Screen* screen) const {
  if (!screen) return;
  const int32_t xt = (jump_time_ > 0) ? 2 : 0;
  const int32_t yt = (std::clamp(lvl, 1, 4) - 1) * 2;
  gfx::LinkedSprite copy = sprite_;
  copy.SetSpritePos(xt, yt).SetSpriteSize(2, 2);
  if (hurt_time > 0) {
    copy.SetColor(gfx::color::kWhite);
  }
  screen->Render(x - 8, y - 11 - (jump_time_ > 0 ? 4 : 0), copy);
}

void Slime::Die() {
  if (level) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    const int32_t max_d =
        game::GameState::Instance().IsScoreMode() ? 2 : std::max(1, 4 - diff);
    level->DropItem(x, y, 1, max_d, "Slime");
  }
  EnemyMob::Die();
}

Zombie::Zombie(int32_t lvl)
    : EnemyMob(lvl, gfx::LinkedSprite(gfx::SpriteType::kEntity, "zombie"), 5,
               100) {}

void Zombie::Die() {
  if (level) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    auto& rng = util::GlobalRandom();
    if (diff == 0) {
      level->DropItem(x, y, 2, 4, "Cloth");
    } else if (diff == 1) {
      level->DropItem(x, y, 1, 3, "Cloth");
    } else {
      level->DropItem(x, y, 1, 2, "Cloth");
    }
    if (rng.NextInt(60) == 2) {
      level->DropItem(x, y, "Iron");
    }
    if (rng.NextInt(100) < 4) {
      level->DropItem(x, y, "Potato");
    }
    if (rng.NextInt(40) == 19) {
      static constexpr const char* kClothes[3] = {
          "Green Clothes", "Red Clothes", "Blue Clothes"};
      level->DropItem(x, y, kClothes[rng.NextInt(3)]);
    }
  }
  EnemyMob::Die();
}

Skeleton::Skeleton(int32_t lvl)
    : EnemyMob(lvl, gfx::LinkedSprite(gfx::SpriteType::kEntity, "skeleton"), 6,
               100, 60, 200, 45),
      arrow_interval_(500 / (std::clamp(lvl, 1, 4) + 5)),
      arrow_time_(arrow_interval_) {}

void Skeleton::Tick() {
  EnemyMob::Tick();
  if (removed || !level || SkipTick()) return;

  const auto& gs = game::GameState::Instance();
  Player* p = level->GetClosestPlayer(x, y);
  if (p && random_walk_time_ == 0 && !gs.IsCreativeMode() &&
      !gs.bed_sleeping) {
    --arrow_time_;
    const int32_t xd = p->x - x;
    const int32_t yd = p->y - y;
    if (xd * xd + yd * yd < 100 * 100 && arrow_time_ < 1) {
      level->Add(std::make_shared<Arrow>(this, dir, lvl));
      arrow_time_ = arrow_interval_;
    }
  }
}

void Skeleton::Die() {
  if (level) {
    static constexpr int32_t kDiffRands[3] = {20, 20, 30};
    static constexpr int32_t kDiffVals[3] = {13, 18, 28};
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    auto& rng = util::GlobalRandom();
    const int32_t count = rng.NextInt(std::max(1, 3 - diff)) + 1;
    const int32_t rand = rng.NextInt(kDiffRands[diff]);
    if (rand <= kDiffVals[diff]) {
      level->DropItem(x, y, count, count, "Bone");
      level->DropItem(x, y, count, count, "Arrow");
    } else if (diff == 0 && rand >= 19) {
      level->DropItem(x, y, 10, 10, "Arrow");
    } else {
      level->DropItem(x, y, 1, 1, "Antidious");
      level->DropItem(x, y, 1, 1, "Arrow");
    }
  }
  EnemyMob::Die();
}

Creeper::Creeper(int32_t lvl)
    : EnemyMob(lvl, gfx::LinkedSprite(gfx::SpriteType::kEntity, "creeper"), 10,
               50) {}

bool Creeper::Move(int32_t xa, int32_t ya) {
  const bool result = EnemyMob::Move(xa, ya);
  dir = Direction::kDown;
  if (xa == 0 && ya == 0) walk_dist = 0;
  return result;
}

void Creeper::TouchedBy(Entity& entity) {
  if (game::GameState::Instance().IsCreativeMode()) return;
  if (auto* p = dynamic_cast<Player*>(&entity)) {
    if (fuse_time_ == 0 && !fuse_lit_) {
      io::Sound::Play("fuse");
      fuse_time_ = 60;
      fuse_lit_ = true;
    }
    p->Hurt(*this, 1, dir);
  }
}

void Creeper::Tick() {
  EnemyMob::Tick();
  if (removed || !level) return;
  if (game::GameState::Instance().IsCreativeMode()) return;

  if (fuse_time_ > 0) {
    --fuse_time_;
    xa_ = 0;
    ya_ = 0;
  } else if (fuse_lit_) {
    xa_ = 0;
    ya_ = 0;
    constexpr int32_t kTriggerRadius = 64;
    bool player_in_range = false;
    for (const auto& p : level->Players()) {
      if (!p || p->removed) continue;
      if (std::abs(p->x - x) < kTriggerRadius &&
          std::abs(p->y - y) < kTriggerRadius) {
        player_in_range = true;
        break;
      }
    }

    if (player_in_range) {
      io::Sound::Play("explode");
      const int32_t xt = x >> 4;
      const int32_t yt = (y - 2) >> 4;
      const int32_t radius = lvl;
      const int32_t lvl_damage = 50 * lvl;
      const int32_t diff = game::GameState::Instance().GetDifficultyIdx();

      auto entities_in_range = level->GetEntitiesInTiles(xt, yt, radius);
      std::vector<Entity*> spawners;
      auto tile_positions = level->GetAreaTilePositions(xt, yt, radius);

      for (Entity* entity : entities_in_range) {
        if (!entity) continue;
        if (auto* mob = dynamic_cast<Mob*>(entity)) {
          const double dist_diag =
              std::hypot(static_cast<double>(mob->x - x),
                         static_cast<double>(mob->y - y));
          const int32_t dmg =
              static_cast<int32_t>(lvl_damage * (1.0 / (dist_diag + 1.0)) + diff);
          mob->Hurt(*this, dmg, dir);
        } else if (dynamic_cast<Spawner*>(entity)) {
          spawners.push_back(entity);
        }
        if (entity == this) continue;
        const int32_t ex = entity->x >> 4;
        const int32_t ey = entity->y >> 4;
        for (const gfx::Point& tp : tile_positions) {
          if (tp.x != ex || tp.y != ey) continue;
          if (!level->GetTile(tp.x, tp.y)->MayPass(*level, tp.x, tp.y, *entity)) {
            if (auto* m = dynamic_cast<Mob*>(entity)) {
              m->Die();
            } else {
              entity->Remove();
            }
          }
        }
      }

      for (const gfx::Point& tp : tile_positions) {
        bool has_spawner = false;
        for (const Entity* sp : spawners) {
          if ((sp->x >> 4) == tp.x && (sp->y >> 4) == tp.y) {
            has_spawner = true;
            break;
          }
        }
        if (has_spawner) continue;
        const level::Tile* t = level->GetTile(tp.x, tp.y);
        if (!level::Tiles::IsExplosionResistant(t->id)) {
          level->SetTile(tp.x, tp.y, level::Tiles::kExplode, 0);
          level->Add(std::make_shared<ExplosionTileTicker>(tp.x, tp.y, 18));
        }
      }

      Die();
    } else {
      fuse_time_ = 0;
      fuse_lit_ = false;
    }
  }
}

void Creeper::Render(gfx::Screen* screen) const {
  const int32_t xt = ((walk_dist >> 3) & 1) * 2;
  const int32_t yt = (std::clamp(lvl, 1, 4) - 1) * 2;
  if (fuse_lit_ && (fuse_time_ / 4) % 2 == 0) {
    RenderMobSprite(screen, xt, yt, 0, gfx::color::kWhite);
    return;
  }
  RenderMobSprite(screen, xt, yt, 0, -1);
}

void Creeper::Die() {
  if (level && !fuse_lit_) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    level->DropItem(x, y, 1, std::max(1, 4 - diff), "Gunpowder");
  }
  EnemyMob::Die();
}

Knight::Knight(int32_t lvl)
    : EnemyMob(lvl, gfx::LinkedSprite(gfx::SpriteType::kEntity, "knight"), 9,
               100) {}

void Knight::Die() {
  if (level) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    if (diff == 0) {
      level->DropItem(x, y, 1, 3, "Shard");
    } else {
      level->DropItem(x, y, 0, 2, "Shard");
    }
    const int32_t key_denom = std::max(1, 24 / lvl / (diff + 1));
    if (util::GlobalRandom().NextInt(key_denom) == 0) {
      level->DropItem(x, y, "Dungeon Key");
    }
  }
  EnemyMob::Die();
}

Snake::Snake(int32_t lvl)
    : EnemyMob(lvl, gfx::LinkedSprite(gfx::SpriteType::kEntity, "snake"),
               lvl > 1 ? 8 : 7, 100) {}

void Snake::Die() {
  if (level) {
    const int32_t diff = game::GameState::Instance().GetDifficultyIdx();
    if (diff == 2) {
      level->DropItem(x, y, 1, 2, "Scale");
    } else {
      level->DropItem(x, y, 0, 1, "Scale");
    }
    const int32_t key_denom = std::max(1, 24 / lvl / (diff + 1));
    if (util::GlobalRandom().NextInt(key_denom) == 0) {
      level->DropItem(x, y, "Dungeon Key");
    }
  }
  EnemyMob::Die();
}

AirWizard::AirWizard()
    : MobAi(gfx::LinkedSprite(gfx::SpriteType::kEntity, "air_wizard"), 2000, 0,
            50, 10) {
  xr = 6;
  yr = 6;
  speed_ = 2;
  walk_time_ = 2;
}

void AirWizard::Tick() {
  MobAi::Tick();
  if (removed || !level || SkipTick()) return;
  if (game::GameState::Instance().IsCreativeMode()) return;

  if (attack_delay_ > 0) {
    xa_ = 0;
    ya_ = 0;
    int32_t d = (attack_delay_ - 45) / 4 % 4;
    d = (d * 2 % 4) + (d / 2);
    if (attack_delay_ < 45) d = 0;
    dir = DirectionFromIndex(d + 1);

    --attack_delay_;
    if (attack_delay_ == 0) {
      attack_type_ = 0;
      if (health < max_health / 2) attack_type_ = 1;
      if (health < max_health / 10) attack_type_ = 2;
      attack_time_ = 120;
    }
    return;
  }

  if (attack_time_ > 0) {
    xa_ = 0;
    ya_ = 0;
    attack_time_ = static_cast<int32_t>(attack_time_ * 0.92);
    const double dir_angle = attack_time_ * 0.25 * (attack_time_ % 2 * 2 - 1);
    const double sp = 0.7 + attack_type_ * 0.2;
    level->Add(std::make_shared<Spark>(this, std::cos(dir_angle) * sp,
                                       std::sin(dir_angle) * sp));
    return;
  }

  Player* p = level->GetClosestPlayer(x, y);
  if (p && random_walk_time_ == 0) {
    int32_t xd = p->x - x;
    int32_t yd = p->y - y;
    const int32_t dsq = xd * xd + yd * yd;
    if (dsq < 16 * 16 * 2 * 2) {
      xa_ = (xd < 0) ? 1 : (xd > 0 ? -1 : 0);
      ya_ = (yd < 0) ? 1 : (yd > 0 ? -1 : 0);
    } else if (dsq > 16 * 16 * 15 * 15) {
      const double hypot = std::sqrt(static_cast<double>(dsq));
      const int32_t new_xd = static_cast<int32_t>(xd * (16.0 * 15.0) / hypot);
      const int32_t new_yd = static_cast<int32_t>(yd * (16.0 * 15.0) / hypot);
      x = p->x - new_xd;
      y = p->y - new_yd;
    }

    xd = p->x - x;
    yd = p->y - y;
    if (util::GlobalRandom().NextInt(4) == 0 && xd * xd + yd * yd < 50 * 50 &&
        attack_delay_ == 0 && attack_time_ == 0) {
      attack_delay_ = 60 * 2;
    }
  }
}

namespace {
void RenderBossHealthPercent(gfx::Screen* screen, int32_t x, int32_t y,
                           int32_t health, int32_t max_health) {
  if (!screen || max_health <= 0) return;
  const int32_t pct = std::max<int32_t>(
      1, static_cast<int32_t>(static_cast<int64_t>(health) * 100 / max_health));
  const std::string h = std::to_string(pct) + "%";
  int32_t col = gfx::color::Get(1, 0, 204, 0);
  int32_t shadow = gfx::color::Get(1, 0, 51, 0);
  if (pct < 16) {
    col = gfx::color::Get(1, 204, 0, 0);
    shadow = gfx::color::Get(1, 51, 0, 0);
  } else if (pct < 51) {
    col = gfx::color::Get(1, 204, 204, 9);
    shadow = gfx::color::Get(1, 51, 51, 0);
  }
  const int32_t tw = gfx::Font::TextWidth(h);
  gfx::Font::Draw(h, screen, (x - tw / 2) + 1, y - 17, shadow);
  gfx::Font::Draw(h, screen, (x - tw / 2), y - 18, col);
}
}  // namespace

void AirWizard::Render(gfx::Screen* screen) const {
  int32_t xt = 0;
  int32_t mirror = 0;
  MobDirectionSprite(dir, walk_dist, &xt, &mirror);
  RenderMobSprite(screen, xt, 0, mirror, -1);
  RenderBossHealthPercent(screen, x, y, health, max_health);
}

void AirWizard::DoHurt(int32_t dmg, Direction attack_dir) {
  MobAi::DoHurt(dmg, attack_dir);
  if (attack_delay_ == 0 && attack_time_ == 0) {
    attack_delay_ = 60 * 2;
  }
}

void AirWizard::TouchedBy(Entity& entity) {
  if (auto* p = dynamic_cast<Player*>(&entity)) {
    p->Hurt(*this, 1, dir);
  }
}

void AirWizard::Die() {
  auto& gs = game::GameState::Instance();
  const bool first_time = !gs.air_wizard_beaten;
  gs.air_wizard_beaten = true;
  if (level) {
    const auto& players = level->Players();
    if (!players.empty()) {
      for (const auto& p : players) {
        if (p && !p->removed) {
          p->AddScore(100000);
          level->DropItem(x, y, 5, 10, "Cloud Ore");
        }
      }
    } else {
      if (auto* p = level->GetClosestPlayer(x, y)) {
        p->AddScore(100000);
      }
      level->DropItem(x, y, 5, 10, "Cloud Ore");
    }
  }
  io::Sound::Play("bossdeath");
  util::AchievementManager::Instance().SetAchievement(
      "minicraft.achievement.airwizard", true, gs.IsCreativeMode(), true);
  gs.NotifyAll("minicraft.notification.air_wizard_defeated");
  if (first_time) {
    gs.NotifyAll("minicraft.notification.dungeon_opened", -400);
  }
  MobAi::Die();
}

ObsidianKnight::ObsidianKnight(int32_t max_hp)
    : MobAi(gfx::LinkedSprite(gfx::SpriteType::kEntity,
                              "obsidian_knight_armored"),
            max_hp, 0, 50, 10) {
  xr = 6;
  yr = 6;
  speed_ = 1;
  walk_time_ = 3;
}

void ObsidianKnight::Tick() {
  MobAi::Tick();
  if (removed || !level) return;

  Player* p = level->GetClosestPlayer(x, y);
  if (p == nullptr || p->removed) {
    game::GameState::Instance().obsidian_knight_summoned = false;
    level->Add(std::make_shared<KnightStatue>(health), x, y, false);
    Remove();
    return;
  }

  if (phase_ == 0 && health <= max_health / 2) {
    phase_ = 1;
    sprite_ = gfx::LinkedSprite(gfx::SpriteType::kEntity,
                                  "obsidian_knight_broken");
    io::Sound::Play("bossdeath");
  }

  if (SkipTick() || game::GameState::Instance().IsCreativeMode()) return;

  // Chase player like EnemyMob when in Walking/Dashing or fallback
  auto update_chase = [&]() {
    if (random_walk_time_ <= 0 && !game::GameState::Instance().bed_sleeping) {
      const int32_t xd = p->x - x;
      const int32_t yd = p->y - y;
      constexpr int32_t kDetectDist = 16 * 8;
      if (xd * xd + yd * yd < kDetectDist * kDetectDist) {
        xa_ = (xd < -1) ? -1 : ((xd > 1) ? 1 : 0);
        ya_ = (yd < -1) ? -1 : ((yd > 1) ? 1 : 0);
      } else {
        RandomizeWalkDir(false);
      }
    }
  };

  if (attack_phase_cooldown_ <= 0) {
    static constexpr AttackPhase kPhases[] = {
        AttackPhase::kAttacking,
        AttackPhase::kDashing,
        AttackPhase::kWalking,
    };
    AttackPhase next_phase;
    do {
      next_phase = kPhases[util::GlobalRandom().NextInt(3)];
    } while (next_phase == attack_phase_);
    attack_phase_ = next_phase;
    attack_phase_cooldown_ = 500;
    speed_ = 1;
  } else {
    --attack_phase_cooldown_;
  }

  if (attack_phase_ == AttackPhase::kAttacking) {
    if (attack_delay_ > 0) {
      xa_ = 0;
      ya_ = 0;
      int32_t d = (attack_delay_ - 35) / 4 % 4;
      d = (d * 2 % 4) + (d / 2);
      if (attack_delay_ < 35) d = 0;
      dir = DirectionFromIndex(d + 1);
      --attack_delay_;
      if (attack_delay_ == 0) {
        attack_level_ = 0;
        if (health < max_health / 2) attack_level_ = 1;
        if (health < max_health / 10) attack_level_ = 2;
        attack_time_ = 120;
      }
      return;
    }

    if (attack_time_ > 0) {
      xa_ = 0;
      ya_ = 0;
      --attack_time_;
      constexpr double kRadToDeg = 57.29577951308232;
      constexpr double kDegToRad = 0.017453292519943295;
      const double deg = std::atan2(p->y - y, p->x - x) * kRadToDeg;
      int32_t attack_dir_deg = 0;
      if (deg > 157.5 || deg < -157.5)
        attack_dir_deg = 270;
      else if (deg > 112.5)
        attack_dir_deg = 135;
      else if (deg > 67.5)
        attack_dir_deg = 90;
      else if (deg > 22.5)
        attack_dir_deg = 45;
      else if (deg < -112.5)
        attack_dir_deg = -135;
      else if (deg < -67.5)
        attack_dir_deg = -90;
      else if (deg < -22.5)
        attack_dir_deg = -45;
      const double sp =
          1.0 + attack_level_ * 0.2 + (attack_time_ / 10.0) * 0.01;
      const int32_t phi = attack_dir_deg - 36 + (attack_time_ % 5) * 18 +
                          util::GlobalRandom().NextInt(7) - 3;
      level->Add(std::make_shared<FireSpark>(
          this, std::cos(phi * kDegToRad) * sp,
          std::sin(phi * kDegToRad) * sp));
      return;
    }

    if (random_walk_time_ == 0) {
      int32_t xd = p->x - x;
      int32_t yd = p->y - y;
      const int32_t dsq = xd * xd + yd * yd;
      if (dsq < 16 * 16 * 2 * 2) {
        xa_ = (xd < 0) ? 1 : (xd > 0 ? -1 : 0);
        ya_ = (yd < 0) ? 1 : (yd > 0 ? -1 : 0);
      } else if (dsq > 16 * 16 * 15 * 15) {
        const double hypot = std::sqrt(static_cast<double>(dsq));
        const int32_t new_xd = static_cast<int32_t>(xd * (16.0 * 15.0) / hypot);
        const int32_t new_yd = static_cast<int32_t>(yd * (16.0 * 15.0) / hypot);
        x = p->x - new_xd;
        y = p->y - new_yd;
      } else {
        update_chase();
      }
      xd = p->x - x;
      yd = p->y - y;
      if (util::GlobalRandom().NextInt(4) == 0 && xd * xd + yd * yd < 50 * 50 &&
          attack_delay_ == 0 && attack_time_ == 0) {
        attack_delay_ = 60 * 2;
      }
    }
  } else if (phase_ == 1 && attack_phase_ == AttackPhase::kDashing) {
    update_chase();
    if (dash_cooldown_ < 1) {
      dash_time_ = 40;
      dash_cooldown_ = 250;
    } else {
      --dash_cooldown_;
    }
    if (dash_time_ == 0) {
      speed_ = 1;
      --dash_cooldown_;
    }
    if (dash_time_ > 0) {
      --dash_time_;
      speed_ = 2;
      level->Add(std::make_shared<FireSpark>(this, 0.0, 0.0));
    }
  } else {
    speed_ = 1;
    update_chase();
  }
}

int32_t ObsidianKnight::CalculateEntityDamage(const Entity* attacker,
                                                int32_t dmg) const {
  if (phase_ == 0 && dynamic_cast<const Arrow*>(attacker) != nullptr) {
    const_cast<Entity*>(attacker)->Remove();
    return 0;
  }
  return dmg;
}

void ObsidianKnight::SetPhase(int32_t phase) {
  phase_ = (phase > 0) ? 1 : 0;
  sprite_ = gfx::LinkedSprite(gfx::SpriteType::kEntity,
                                phase_ > 0 ? "obsidian_knight_broken"
                                           : "obsidian_knight_armored");
}

void ObsidianKnight::Render(gfx::Screen* screen) const {
  int32_t xt = 0;
  int32_t mirror = 0;
  MobDirectionSprite(dir, walk_dist, &xt, &mirror);
  RenderMobSprite(screen, xt, 0, mirror, -1);
  RenderBossHealthPercent(screen, x, y, health, max_health);
}

void ObsidianKnight::DoHurt(int32_t dmg, Direction attack_dir) {
  MobAi::DoHurt(dmg, attack_dir);
  if (attack_delay_ == 0 && attack_time_ == 0) {
    attack_delay_ = 60 * 2;
  }
}

void ObsidianKnight::TouchedBy(Entity& entity) {
  if (auto* p = dynamic_cast<Player*>(&entity)) {
    p->Hurt(*this, 2, dir);
    if (attack_phase_ == AttackPhase::kDashing) {
      dash_time_ = std::max<int32_t>(dash_time_ - 10, 0);
    }
  }
}

void ObsidianKnight::Die() {
  auto& gs = game::GameState::Instance();
  gs.obsidian_knight_beaten = true;
  gs.obsidian_knight_summoned = false;
  if (level) {
    const auto& players = level->Players();
    if (!players.empty()) {
      for (const auto& p : players) {
        if (p && !p->removed) {
          p->AddScore(300000);
          level->DropItem(x, y, 15, 25, "Shard");
          level->DropItem(x, y, 1, 1, "Obsidian Heart");
        }
      }
    } else {
      if (auto* p = level->GetClosestPlayer(x, y)) {
        p->AddScore(300000);
      }
      level->DropItem(x, y, 15, 25, "Shard");
      level->DropItem(x, y, "Obsidian Heart");
    }
  }
  io::Sound::Play("bossdeath");
  util::AchievementManager::Instance().SetAchievement(
      "minicraft.achievement.obsidianknight", true, gs.IsCreativeMode(), true);
  gs.NotifyAll("minicraft.notification.obsidian_knight_defeated");
  MobAi::Die();
}

}  // namespace minicraft::entity
