#include "core/level/level.h"

#include <algorithm>
#include <cmath>

#include "core/entity/entity.h"
#include "core/entity/furniture.h"
#include "core/entity/mob.h"
#include "core/entity/player.h"
#include "core/entity/projectiles.h"
#include "core/game/game_state.h"
#include "core/gfx/screen.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/level/level_gen.h"
#include "core/level/structure.h"
#include "core/level/tile.h"
#include "core/level/tiles.h"
#include "core/util/random.h"

namespace minicraft::level {

Level::Level(int32_t w, int32_t h, int64_t seed, int32_t depth,
             Level* parent_level, bool skip_gen)
    : w(w),
      h(h),
      seed(seed),
      depth(depth),
      parent_level(parent_level),
      chunk_manager_(seed, depth) {
  if (depth != -4 && depth != 0) {
    monster_density = 8;
  }
  UpdateMobCap();
  GenerateTreeTypes();

  if (!skip_gen) {
    LevelGen::GenerateInitialChunks(*this);
    if (parent_level != nullptr) {
      LevelGen::LinkStairsAndPopulate(*this, parent_level);
    }
    CheckChestCount(false);
    CheckAirWizard(false);
  }
}

Level::~Level() = default;

const Tile* Level::GetTile(int32_t x, int32_t y) const {
  return chunk_manager_.GetTile(x, y);
}

void Level::SetTile(int32_t x, int32_t y, uint16_t tile_id, int16_t data) {
  RemoveSignIfNeeded(x, y);
  chunk_manager_.SetTile(x, y, tile_id, data);
  const Tile* t = Tiles::Get(tile_id);
  if (t) t->OnPlaced(*this, x, y);
}

void Level::SetTile(int32_t x, int32_t y, const Tile* tile, int16_t data) {
  if (!tile) return;
  SetTile(x, y, tile->id, data);
}

int16_t Level::GetData(int32_t x, int32_t y) const {
  return chunk_manager_.GetData(x, y);
}

void Level::SetData(int32_t x, int32_t y, int16_t data) {
  chunk_manager_.SetData(x, y, data);
}

BiomeId Level::GetBiome(int32_t x, int32_t y) const {
  return chunk_manager_.GetBiome(x, y);
}

void Level::RemoveSignIfNeeded(int32_t tx, int32_t ty) const {
  const Tile* cur = chunk_manager_.GetTileConst(tx, ty);
  if (cur && cur->id == Tiles::kSign) {
    game::GameState::Instance().RemoveSign(depth, tx, ty);
  }
}

void Level::Add(std::shared_ptr<entity::Entity> entity) {
  if (!entity) return;
  entity->level = this;
  entity->removed = false;
  pending_add_.push_back(std::move(entity));
}

void Level::Add(std::shared_ptr<entity::Entity> entity, int32_t x, int32_t y,
                bool tile_coords) {
  if (!entity) return;
  if (tile_coords) {
    entity->x = x * 16 + 8;
    entity->y = y * 16 + 8;
  } else {
    entity->x = x;
    entity->y = y;
  }
  Add(std::move(entity));
}

void Level::Remove(entity::Entity* entity) {
  if (entity) entity->Remove();
}

void Level::PurgeRemovedEntities() {
  for (size_t i = 0; i < entities_.size();) {
    entity::Entity* e = entities_[i].get();
    if (!e || e->removed) {
      if (e) {
        if (dynamic_cast<entity::Mob*>(e) &&
            dynamic_cast<entity::Player*>(e) == nullptr) {
          mob_count = std::max(0, mob_count - 1);
        } else if (depth == -4) {
          if (auto* dc = dynamic_cast<entity::DungeonChest*>(e)) {
            if (dc->is_locked) chest_count = std::max(0, chest_count - 1);
          }
        } else if (dynamic_cast<entity::Chest*>(e)) {
          chest_count = std::max(0, chest_count - 1);
        }
      }
      entities_[i] = std::move(entities_.back());
      entities_.pop_back();
    } else {
      ++i;
    }
  }
  players_.erase(
      std::remove_if(players_.begin(), players_.end(),
                     [](const std::shared_ptr<entity::Player>& p) {
                       return !p || p->removed;
                     }),
      players_.end());
}

void Level::FlushPendingEntities() {
  if (pending_add_.empty()) return;
  for (auto& e : pending_add_) {
    if (!e || e->removed) continue;
    if (auto p = std::dynamic_pointer_cast<entity::Player>(e)) {
      if (std::find(players_.begin(), players_.end(), p) == players_.end()) {
        players_.push_back(p);
      }
    } else if (dynamic_cast<entity::Mob*>(e.get())) {
      ++mob_count;
    } else if (depth == -4) {
      if (auto* dc = dynamic_cast<entity::DungeonChest*>(e.get())) {
        if (dc->is_locked) ++chest_count;
      }
    } else if (dynamic_cast<entity::Chest*>(e.get())) {
      ++chest_count;
    }
    entities_.push_back(std::move(e));
  }
  pending_add_.clear();
}

std::shared_ptr<entity::Entity> Level::GetEntity(int32_t eid) const {
  for (const auto& e : entities_) {
    if (e && !e->removed && e->eid == eid) return e;
  }
  return nullptr;
}

void Level::GetEntitiesInRect(const gfx::Rectangle& rect,
                              std::vector<entity::Entity*>* out) const {
  out->clear();
  const int32_t x0 = rect.Left();
  const int32_t y0 = rect.Top();
  const int32_t x1 = rect.Right();
  const int32_t y1 = rect.Bottom();
  for (const auto& e : entities_) {
    if (e && !e->removed && e->Intersects(x0, y0, x1, y1)) {
      out->push_back(e.get());
    }
  }
}

std::vector<entity::Entity*> Level::GetEntitiesInRect(
    const gfx::Rectangle& rect) const {
  std::vector<entity::Entity*> result;
  GetEntitiesInRect(rect, &result);
  return result;
}

std::vector<entity::Entity*> Level::GetEntitiesInTiles(int32_t xt, int32_t yt,
                                                       int32_t radius) const {
  return GetEntitiesInTiles(xt - radius, yt - radius, xt + radius, yt + radius);
}

std::vector<entity::Entity*> Level::GetEntitiesInTiles(int32_t xt0, int32_t yt0,
                                                       int32_t xt1,
                                                       int32_t yt1) const {
  std::vector<entity::Entity*> result;
  for (const auto& e : entities_) {
    if (!e || e->removed) continue;
    const int32_t ex = e->x >> 4;
    const int32_t ey = e->y >> 4;
    if (ex >= xt0 && ex <= xt1 && ey >= yt0 && ey <= yt1) {
      result.push_back(e.get());
    }
  }
  return result;
}

entity::Player* Level::GetClosestPlayer(int32_t x, int32_t y) const {
  entity::Player* best = nullptr;
  int64_t best_dist = 0x7FFFFFFFFFFFFFFFLL;
  for (const auto& p : players_) {
    if (!p || p->removed) continue;
    int64_t dx = p->x - x;
    int64_t dy = p->y - y;
    int64_t d = dx * dx + dy * dy;
    if (d < best_dist) {
      best_dist = d;
      best = p.get();
    }
  }
  return best;
}

std::vector<gfx::Point> Level::GetMatchingTiles(uint16_t tile_id, int32_t xt,
                                                int32_t yt,
                                                int32_t radius) const {
  std::vector<gfx::Point> matches;
  for (int32_t y = yt - radius; y <= yt + radius; ++y) {
    for (int32_t x = xt - radius; x <= xt + radius; ++x) {
      if (GetTile(x, y)->id == tile_id) {
        matches.push_back({x, y});
      }
    }
  }
  return matches;
}

std::vector<gfx::Point> Level::GetAreaTilePositions(int32_t xt, int32_t yt,
                                                    int32_t radius) const {
  std::vector<gfx::Point> pos;
  pos.reserve(static_cast<size_t>((radius * 2 + 1) * (radius * 2 + 1)));
  for (int32_t y = yt - radius; y <= yt + radius; ++y) {
    for (int32_t x = xt - radius; x <= xt + radius; ++x) {
      if (x >= 0 && x < w && y >= 0 && y < h) {
        pos.push_back({x, y});
      }
    }
  }
  return pos;
}

void Level::DropItem(int32_t x, int32_t y, std::unique_ptr<item::Item> item) {
  if (!item) return;
  auto& rng = util::GlobalRandom();
  int32_t ranx = 0;
  int32_t rany = 0;
  do {
    ranx = x + rng.NextInt(11) - 5;
    rany = y + rng.NextInt(11) - 5;
  } while ((ranx >> 4) != (x >> 4) || (rany >> 4) != (y >> 4));
  Add(std::make_shared<entity::ItemEntity>(std::move(item), ranx, rany));
}

void Level::DropItem(int32_t x, int32_t y, std::string_view item_name) {
  auto it = item::Items::Get(item_name);
  if (it) DropItem(x, y, std::move(it));
}

void Level::DropItem(int32_t x, int32_t y, int32_t min_count, int32_t max_count,
                     std::string_view item_name) {
  int32_t count = min_count;
  if (max_count > min_count) {
    count += util::GlobalRandom().NextInt(max_count - min_count + 1);
  }
  for (int32_t i = 0; i < count; ++i) {
    DropItem(x, y, item_name);
  }
}

void Level::Tick(bool full_tick) {
  FlushPendingEntities();

  if (!full_tick) return;

  for (const auto& p : players_) {
    if (p && !p->removed) {
      LoadChunksAround(p->x >> 4, p->y >> 4);
    }
  }

  TrySpawn();

  // Tick 10 random tiles per player in [-192..+191] px (24x24 tiles, ~1.04 Hz
  // per tile matching classic w*h/50 tick rate so tiles don't out-heal hits).
  auto& rng = util::GlobalRandom();
  for (const auto& p : players_) {
    if (!p || p->removed) continue;
    for (int32_t i = 0; i < 10; ++i) {
      const int32_t xt = (rng.NextInt(384) - 192 + p->x) >> 4;
      const int32_t yt = (rng.NextInt(384) - 192 + p->y) >> 4;
      GetTile(xt, yt)->Tick(*this, xt, yt);
    }
  }

  HandleDespawn();

  // Tick all entities (iterate by index since Tick may append to pending_add_)
  const size_t count = entities_.size();
  for (size_t i = 0; i < count; ++i) {
    entity::Entity* e = entities_[i].get();
    if (!e || e->removed) continue;
    e->Tick();
    if (!e->removed) {
      if (auto* ai = dynamic_cast<entity::MobAi*>(e)) {
        ai->HandleDespawn();
      }
    }
  }

  FlushPendingEntities();

  // Compact removed entities cleanly in-place
  int32_t counted_mobs = 0;
  int32_t counted_chests = 0;
  for (size_t i = 0; i < entities_.size();) {
    entity::Entity* e = entities_[i].get();
    if (!e || e->removed) {
      entities_[i] = std::move(entities_.back());
      entities_.pop_back();
    } else {
      if (dynamic_cast<entity::Mob*>(e) &&
          dynamic_cast<entity::Player*>(e) == nullptr) {
        ++counted_mobs;
      } else if (depth == -4) {
        if (auto* dc = dynamic_cast<entity::DungeonChest*>(e)) {
          if (dc->is_locked) ++counted_chests;
        }
      } else if (dynamic_cast<entity::Chest*>(e)) {
        ++counted_chests;
      }
      ++i;
    }
  }
  mob_count = counted_mobs;
  chest_count = counted_chests;

  players_.erase(
      std::remove_if(players_.begin(), players_.end(),
                     [](const std::shared_ptr<entity::Player>& p) {
                       return !p || p->removed;
                     }),
      players_.end());
}

void Level::RenderBackground(gfx::Screen* screen, int32_t x_scroll,
                             int32_t y_scroll) const {
  if (!screen) return;
  const int32_t xo = x_scroll >> 4;
  const int32_t yo = y_scroll >> 4;
  const int32_t x1 = ((x_scroll + screen->w) >> 4) + 1;
  const int32_t y1 = ((y_scroll + screen->h) >> 4) + 1;

  screen->SetOffset(x_scroll, y_scroll);
  for (int32_t y = yo; y <= y1; ++y) {
    for (int32_t x = xo; x <= x1; ++x) {
      GetTile(x, y)->Render(screen, *this, x, y);
    }
  }
  screen->SetOffset(0, 0);
}

void Level::RenderSprites(gfx::Screen* screen, int32_t x_scroll,
                          int32_t y_scroll) const {
  if (!screen) return;
  const int32_t x0 = x_scroll - 24;
  const int32_t y0 = y_scroll - 24;
  const int32_t x1 = x_scroll + screen->w + 24;
  const int32_t y1 = y_scroll + screen->h + 24;

  render_scratch_.clear();
  for (const auto& e : entities_) {
    if (!e || e->removed) continue;
    if (e->x >= x0 && e->x <= x1 && e->y >= y0 && e->y <= y1) {
      render_scratch_.push_back(e.get());
    }
  }

  std::sort(render_scratch_.begin(), render_scratch_.end(),
            [](const entity::Entity* a, const entity::Entity* b) {
              if (a->y != b->y) return a->y < b->y;
              return a->eid < b->eid;
            });

  screen->SetOffset(x_scroll, y_scroll);
  for (const entity::Entity* e : render_scratch_) {
    e->Render(screen);
  }
  screen->SetOffset(0, 0);
}

void Level::RenderLight(gfx::Screen* screen, int32_t x_scroll,
                        int32_t y_scroll, int32_t brightness) const {
  if (!screen) return;
  const int32_t xo = x_scroll >> 4;
  const int32_t yo = y_scroll >> 4;
  const int32_t tw = (gfx::Screen::w + 15) >> 4;
  const int32_t th = (gfx::Screen::h + 15) >> 4;
  const int32_t r = 8;

  screen->SetOffset(x_scroll, y_scroll);
  for (const entity::Entity* e :
       GetEntitiesInTiles(xo - r, yo - r, tw + xo + r, th + yo + r)) {
    const int32_t lr = e->GetLightRadius();
    if (lr > 0) {
      screen->RenderLight(e->x - 1, e->y - 4, lr * brightness);
    }
  }

  for (int32_t y = yo - r; y <= th + yo + r; ++y) {
    for (int32_t x = xo - r; x <= tw + xo + r; ++x) {
      if (x < 0 || y < 0 || x >= w || y >= h) continue;
      const int32_t lr = GetTile(x, y)->GetLightRadius(*this, x, y);
      if (lr > 0) {
        screen->RenderLight(x * 16 + 8, y * 16 + 8, lr * brightness);
      }
    }
  }
  screen->SetOffset(0, 0);
}

void Level::UpdateMobCap() {
  max_mob_count = 150 + 150 * game::GameState::Instance().GetDifficultyIdx();
  if (depth == 1) max_mob_count /= 2;
  if (depth == 0 || depth == -4 || depth == -5) {
    max_mob_count = max_mob_count * 2 / 3;
  }
}

bool Level::IsLight(int32_t tx, int32_t ty) const {
  for (const gfx::Point& p : GetAreaTilePositions(tx, ty, 3)) {
    if (GetTile(p.x, p.y)->id == Tiles::kTorch) {
      return true;
    }
  }
  for (const auto& e : entities_) {
    if (!e || e->removed) continue;
    if (dynamic_cast<const entity::Lantern*>(e.get()) != nullptr) {
      const int32_t rr = e->GetLightRadius() - 1;
      if (rr <= 0) continue;
      const int32_t ex = (e->x >> 4) - tx;
      const int32_t ey = (e->y >> 4) - ty;
      if (ex * ex + ey * ey < rr * rr) {
        return true;
      }
    }
  }
  return false;
}

namespace {

std::vector<double> GenerateDiamondSquare(int32_t w, int32_t h,
                                          int32_t feature_size,
                                          int64_t seed) {
  std::vector<double> values(static_cast<size_t>(w * h), 0.0);
  if (w <= 0 || h <= 0) return values;
  util::Random rng(seed);

  auto sample = [&](int32_t x, int32_t y) -> double {
    const int32_t wx = ((x % w) + w) % w;
    const int32_t wy = ((y % h) + h) % h;
    return values[static_cast<size_t>(wx + wy * w)];
  };
  auto set_sample = [&](int32_t x, int32_t y, double v) {
    const int32_t wx = ((x % w) + w) % w;
    const int32_t wy = ((y % h) + h) % h;
    values[static_cast<size_t>(wx + wy * w)] = v;
  };

  for (int32_t y = 0; y < h; y += feature_size) {
    for (int32_t x = 0; x < w; x += feature_size) {
      set_sample(x, y, rng.NextDouble() * 2.0 - 1.0);
    }
  }

  int32_t step_size = feature_size;
  double scale = 2.0 / static_cast<double>(std::max(1, w));
  double scale_mod = 1.0;
  do {
    const int32_t half_step = std::max(1, step_size / 2);
    for (int32_t y = 0; y < h; y += step_size) {
      for (int32_t x = 0; x < w; x += step_size) {
        const double a = sample(x, y);
        const double b = sample(x + step_size, y);
        const double c = sample(x, y + step_size);
        const double d = sample(x + step_size, y + step_size);
        const double e =
            (a + b + c + d) / 4.0 +
            (rng.NextDouble() * 2.0 - 1.0) * step_size * scale;
        set_sample(x + half_step, y + half_step, e);
      }
    }
    for (int32_t y = 0; y < h; y += step_size) {
      for (int32_t x = 0; x < w; x += step_size) {
        const double a = sample(x, y);
        const double b = sample(x + step_size, y);
        const double c = sample(x, y + step_size);
        const double d = sample(x + half_step, y + half_step);
        const double e = sample(x + half_step, y - half_step);
        const double f = sample(x - half_step, y + half_step);

        const double H =
            (a + b + d + e) / 4.0 +
            (rng.NextDouble() * 2.0 - 1.0) * step_size * scale * 0.5;
        const double g =
            (a + c + d + f) / 4.0 +
            (rng.NextDouble() * 2.0 - 1.0) * step_size * scale * 0.5;
        set_sample(x + half_step, y, H);
        set_sample(x, y + half_step, g);
      }
    }
    step_size /= 2;
    scale *= (scale_mod + 0.8);
    scale_mod *= 0.3;
  } while (step_size > 1);

  return values;
}

}  // namespace

void Level::GenerateTreeTypes() {
  if (w <= 0 || h <= 0) return;
  tree_types_.assign(static_cast<size_t>(w * h), TreeType::kOak);
  const auto n1 = GenerateDiamondSquare(w, h, 32, seed ^ 0x1A2B3C4DULL);
  const auto n2 = GenerateDiamondSquare(w, h, 32, seed ^ 0x5E6F7A8BULL);
  for (int32_t i = 0; i < w * h; ++i) {
    const double val =
        std::abs(n1[static_cast<size_t>(i)] - n2[static_cast<size_t>(i)]) *
            3.0 -
        1.0;
    const int64_t idx = std::llround(val - 1.0);
    if (idx >= 0 && idx <= 6) {
      tree_types_[static_cast<size_t>(i)] = static_cast<TreeType>(idx);
    } else {
      tree_types_[static_cast<size_t>(i)] = TreeType::kOak;
    }
  }
}

TreeType Level::GetTreeType(int32_t x, int32_t y) const {
  if (x >= 0 && x < w && y >= 0 && y < h &&
      static_cast<size_t>(x + y * w) < tree_types_.size()) {
    return tree_types_[static_cast<size_t>(x + y * w)];
  }
  return TreeType::kOak;
}

void Level::HandleDespawn() {}

void Level::LoadChunk(int32_t cx, int32_t cy) {
  if (chunk_manager_.GetChunkStage(cx, cy) == Chunk::kStageUnfinishedStairs &&
      parent_level != nullptr) {
    if (parent_level->chunk_manager_.GetChunkStage(cx, cy) <
        Chunk::kStageUnfinishedStairs) {
      parent_level->LoadChunk(cx, cy);
    }
  }
  chunk_manager_.GetChunk(cx, cy);
  PopulateChunk(cx, cy);
}

void Level::LoadChunksAround(int32_t tile_x, int32_t tile_y) {
  auto floor_div = [](int32_t a, int32_t b) -> int32_t {
    int32_t q = a / b;
    int32_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
  };
  const int32_t cx = floor_div(tile_x, Chunk::kSize);
  const int32_t cy = floor_div(tile_y, Chunk::kSize);
  for (int32_t y = cy - 3; y <= cy + 3; ++y) {
    for (int32_t x = cx - 3; x <= cx + 3; ++x) {
      if (chunk_manager_.GetChunkStage(x, y) != Chunk::kStageFinished) {
        LoadChunk(x, y);
      }
    }
  }
}

bool Level::IsEntityOnTile(int32_t tx, int32_t ty) const {
  for (const auto& e : entities_) {
    if (!e || e->removed) continue;
    if ((e->x >> 4) == tx && (e->y >> 4) == ty) return true;
  }
  return false;
}

void Level::RemoveAllEnemies() {
  for (const auto& e : entities_) {
    if (e && dynamic_cast<entity::EnemyMob*>(e.get()) != nullptr) {
      e->Remove();
    }
  }
}

void Level::RegenerateBossRoom() {
  if (depth != -4) return;
  Structure::DungeonBossRoom().Draw(*this, w / 2, h / 2);
  for (int32_t y = h / 2 - 4; y < h / 2 + 5; ++y) {
    for (int32_t x = w / 2 - 4; x < w / 2 + 5; ++x) {
      SetData(x, y, 0);
    }
  }
}

void Level::CheckAirWizard(bool check) {
  if (depth != 1 || game::GameState::Instance().air_wizard_beaten) return;
  if (check) {
    for (const auto& e : pending_add_) {
      if (e && dynamic_cast<entity::AirWizard*>(e.get()) != nullptr) return;
    }
    for (const auto& e : entities_) {
      if (e && !e->removed &&
          dynamic_cast<entity::AirWizard*>(e.get()) != nullptr) {
        return;
      }
    }
  }
  Add(std::make_shared<entity::AirWizard>(), (w / 2) * 16 + 8,
      (h / 2) * 16 + 8);
}

void Level::TrySpawn() {
  UpdateMobCap();
  if (mob_count >= max_mob_count || players_.empty()) return;

  auto& rng = util::GlobalRandom();
  constexpr int32_t kMobSpawnFactor = 100;
  const int32_t spawn_skip_chance = static_cast<int32_t>(
      kMobSpawnFactor *
      (static_cast<double>(mob_count) * static_cast<double>(mob_count)) /
      (static_cast<double>(max_mob_count) * static_cast<double>(max_mob_count)));
  if (spawn_skip_chance > 0 && rng.NextInt(spawn_skip_chance) != 0) {
    return;
  }

  const auto& gs = game::GameState::Instance();
  const auto tod = gs.GetTimeOfDay();
  bool spawned = false;

  for (const auto& anchor : players_) {
    if (!anchor) continue;
    const int32_t lvl = -std::clamp(depth, -4, 0);

    for (int32_t i = 0; i < 30 && !spawned; ++i) {
      const int32_t rnd = rng.NextInt(100);
      const int32_t sx =
          (rng.NextInt(Chunk::kSize * 2) - Chunk::kSize) * 16 + anchor->x;
      const int32_t sy =
          (rng.NextInt(Chunk::kSize * 2) - Chunk::kSize) * 16 + anchor->y;

      if (((tod == game::TimeOfDay::kNight && gs.past_day1) || depth != 0) &&
          entity::EnemyMob::CheckStartPos(*this, sx, sy) &&
          !IsLight(sx >> 4, sy >> 4)) {
        std::shared_ptr<entity::Mob> mob;
        if (depth != -4) {
          if (rnd <= 40) mob = std::make_shared<entity::Slime>(lvl);
          else if (rnd <= 75) mob = std::make_shared<entity::Zombie>(lvl);
          else if (rnd >= 85) mob = std::make_shared<entity::Skeleton>(lvl);
          else mob = std::make_shared<entity::Creeper>(lvl);
        } else {
          if (rnd <= 40) mob = std::make_shared<entity::Snake>(lvl);
          else if (rnd <= 75) mob = std::make_shared<entity::Knight>(lvl);
          else if (rnd >= 85) mob = std::make_shared<entity::Snake>(lvl);
          else mob = std::make_shared<entity::Knight>(lvl);
        }
        if (mob) Add(std::move(mob), sx, sy);
        spawned = true;
        continue;
      }

      if (depth == 0 && entity::PassiveMob::CheckStartPos(*this, sx, sy)) {
        std::shared_ptr<entity::Mob> mob;
        if (rnd <= (tod == game::TimeOfDay::kNight ? 22 : 33)) {
          mob = std::make_shared<entity::Cow>();
        } else if (rnd >= 68) {
          mob = std::make_shared<entity::Pig>();
        } else {
          const int32_t col_rnd = rng.NextInt(100);
          item::DyeColor sheep_col = item::DyeColor::kWhite;
          if (col_rnd < 80) {
            sheep_col = item::DyeColor::kWhite;
          } else if (col_rnd < 85) {
            sheep_col = item::DyeColor::kBlack;
          } else if (col_rnd < 90) {
            sheep_col = item::DyeColor::kGray;
          } else if (col_rnd < 95) {
            sheep_col = item::DyeColor::kLightGray;
          } else if (col_rnd < 98) {
            sheep_col = item::DyeColor::kBrown;
          } else {
            sheep_col = item::DyeColor::kPink;
          }
          mob = std::make_shared<entity::Sheep>(sheep_col);
        }
        if (mob) Add(std::move(mob), sx, sy);
        spawned = true;
      }
    }
  }
}

void Level::SpawnStructureFurniture(int32_t tx, int32_t ty,
                                    const std::string& kind) {
  const int32_t px = tx * 16 + 8;
  const int32_t py = ty * 16 + 8;
  if (kind == "DungeonChest") {
    Add(std::make_shared<entity::DungeonChest>(true, true), px, py);
  } else if (kind == "KnightStatue") {
    Add(std::make_shared<entity::KnightStatue>(), px, py);
  } else if (kind.rfind("KnightStatue:", 0) == 0) {
    const int32_t hp = std::stoi(kind.substr(13));
    Add(std::make_shared<entity::KnightStatue>(hp), px, py);
  } else if (kind == "Lantern:Iron") {
    Add(std::make_shared<entity::Lantern>(entity::Lantern::Type::kIron), px,
        py);
  } else if (kind == "Lantern:Gold") {
    Add(std::make_shared<entity::Lantern>(entity::Lantern::Type::kGold), px,
        py);
  } else if (kind == "Lantern:Norm") {
    Add(std::make_shared<entity::Lantern>(entity::Lantern::Type::kNorm), px,
        py);
  } else if (kind == "Crafter:Enchanter") {
    Add(std::make_shared<entity::Crafter>(entity::Crafter::Type::kEnchanter),
        px, py);
  } else if (kind == "Crafter:Workbench") {
    Add(std::make_shared<entity::Crafter>(entity::Crafter::Type::kWorkbench),
        px, py);
  } else if (kind == "Crafter:Furnace") {
    Add(std::make_shared<entity::Crafter>(entity::Crafter::Type::kFurnace), px,
        py);
  } else if (kind == "Crafter:Oven") {
    Add(std::make_shared<entity::Crafter>(entity::Crafter::Type::kOven), px,
        py);
  } else if (kind == "Crafter:Anvil") {
    Add(std::make_shared<entity::Crafter>(entity::Crafter::Type::kAnvil), px,
        py);
  } else if (kind == "Crafter:Loom") {
    Add(std::make_shared<entity::Crafter>(entity::Crafter::Type::kLoom), px,
        py);
  } else if (kind == "Crafter:DyeVat") {
    Add(std::make_shared<entity::Crafter>(entity::Crafter::Type::kDyeVat), px,
        py);
  } else if (kind.rfind("Chest:", 0) == 0) {
    auto chest = std::make_shared<entity::Chest>();
    chest->PopulateInvRandom(kind.substr(6), -depth);
    Add(std::move(chest), px, py);
  } else if (kind.rfind("Spawner:", 0) == 0) {
    Add(std::make_shared<entity::Spawner>(kind.substr(8),
                                          std::clamp(-depth, 1, 4)),
        px, py);
  }
}

void Level::PopulateChunk(int32_t cx, int32_t cy) {
  if (chunk_manager_.GetChunkStage(cx, cy) >= Chunk::kStageFinished) return;

  Chunk* chunk = chunk_manager_.GetChunk(cx, cy);
  chunk_manager_.SetChunkStage(cx, cy, Chunk::kStageFinished);

  if (chunk != nullptr && !chunk->pending_ornate_pools.empty()) {
    auto pools = std::move(chunk->pending_ornate_pools);
    chunk->pending_ornate_pools.clear();
    for (const auto& [px, py] : pools) {
      Structure::OrnateLavaPool().Draw(*this, px, py);
    }
  }
}

void Level::CheckChestCount(bool check) {
  if (depth != -4) {
    int32_t count = 0;
    for (const auto& e : entities_) {
      if (e && !e->removed && dynamic_cast<entity::Chest*>(e.get())) {
        ++count;
      }
    }
    chest_count = count;
    return;
  }

  int32_t num_chests = 0;
  if (check) {
    for (const auto& e : pending_add_) {
      if (auto* dc = dynamic_cast<entity::DungeonChest*>(e.get())) {
        if (dc->is_locked) ++num_chests;
      }
    }
    for (const auto& e : entities_) {
      if (e && !e->removed) {
        if (auto* dc = dynamic_cast<entity::DungeonChest*>(e.get())) {
          if (dc->is_locked) ++num_chests;
        }
      }
    }
  }

  const int32_t target = 10 * std::max(1, w / 128);
  auto& rng = util::GlobalRandom();
  for (int32_t i = num_chests; i < target; ++i) {
    bool added_chest = false;
    for (int32_t attempt = 0; attempt < 5000 && !added_chest; ++attempt) {
      const int32_t x2 = rng.NextInt(std::max(1, w));
      const int32_t y2 = rng.NextInt(std::max(1, h));
      if (GetTile(x2, y2)->id == Tiles::kGrass) {
        auto chest = std::make_shared<entity::DungeonChest>(true, true);
        Add(std::move(chest), x2 * 16 + 8, y2 * 16 + 8);
        ++num_chests;
        added_chest = true;
      }
    }
    if (!added_chest) break;
  }
  chest_count = num_chests;
}

void Level::ClearEntities() {
  entities_.clear();
  pending_add_.clear();
  players_.clear();
  mob_count = 0;
  chest_count = 0;
}

}  // namespace minicraft::level
