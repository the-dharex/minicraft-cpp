#include "core/level/tile.h"

#include "core/entity/boat.h"
#include "core/game/game_state.h"
#include "core/gfx/screen.h"
#include "core/level/level.h"
#include "core/level/tiles.h"

namespace minicraft::level {

int32_t Tile::tick_count = 0;

bool Tile::MayPass(const Level& /*level*/, int32_t /*x*/, int32_t /*y*/,
                   const entity::Entity& e) const {
  const auto* boat = dynamic_cast<const entity::Boat*>(&e);
  if (!boat) return true;
  return (id == Tiles::kWater && boat->IsInWater()) ||
         (id == Tiles::kLava && boat->IsInLava()) ||
         !(id == Tiles::kWater || id == Tiles::kLava || boat->IsInWater() ||
           boat->IsInLava());
}

Tile::Tile(uint16_t id, std::string name, std::string sprite_key)
    : id(id),
      name(std::move(name)),
      sprite_(gfx::SpriteType::kTile,
              sprite_key.empty() ? "missing_tile" : std::move(sprite_key)) {}

void Tile::Render(gfx::Screen* screen, const Level& level, int32_t x,
                  int32_t y) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite_;
  screen->Render(x * 16, y * 16, copy);
}

void Tile::RenderDirtBackground(gfx::Screen* screen, const Level& level,
                                int32_t x, int32_t y) {
  if (!screen) return;
  const char* key = "dirt";
  if (level.depth <= -4) {
    key = "purple_dirt";
  } else if (level.depth < 0) {
    key = "gray_dirt";
  } else if (level.depth > 0) {
    key = "cloud_background";
  }
  gfx::LinkedSprite bg(gfx::SpriteType::kTile, key);
  screen->Render(x * 16, y * 16, bg);
}

void Tile::RenderConnected(
    gfx::Screen* screen, const Level& level, int32_t x, int32_t y,
    gfx::LinkedSprite& base, gfx::LinkedSprite& border,
    gfx::LinkedSprite* corner,
    const std::function<bool(const Tile*, int32_t, int32_t)>& pred,
    bool singleton_with_connective) {
  if (!screen) return;
  const auto& gs = game::GameState::Instance();
  if (!gs.sprite_linker) {
    screen->Render(x * 16, y * 16, base);
    return;
  }

  const gfx::SheetRect* base_rect = base.GetSheet(*gs.sprite_linker);
  const gfx::SheetRect* border_rect = border.GetSheet(*gs.sprite_linker);
  if (!base_rect || !border_rect) {
    screen->Render(x * 16, y * 16, base);
    return;
  }

  const bool u = pred(level.GetTile(x, y - 1), x, y - 1);
  const bool d = pred(level.GetTile(x, y + 1), x, y + 1);
  const bool l = pred(level.GetTile(x - 1, y), x - 1, y);
  const bool r = pred(level.GetTile(x + 1, y), x + 1, y);

  const gfx::SheetRect* corner_rect =
      corner ? corner->GetSheet(*gs.sprite_linker) : nullptr;
  const bool ul =
      (u && l) ? pred(level.GetTile(x - 1, y - 1), x - 1, y - 1) : true;
  const bool ur =
      (u && r) ? pred(level.GetTile(x + 1, y - 1), x + 1, y - 1) : true;
  const bool dl =
      (d && l) ? pred(level.GetTile(x - 1, y + 1), x - 1, y + 1) : true;
  const bool dr =
      (d && r) ? pred(level.GetTile(x + 1, y + 1), x + 1, y + 1) : true;

  const int32_t px = x * 16;
  const int32_t py = y * 16;
  constexpr int32_t kMirrorBoth =
      gfx::Screen::kBitMirrorX | gfx::Screen::kBitMirrorY;

  auto render_quad = [&](int32_t qx, int32_t qy, bool horiz, bool vert,
                         bool diag, int32_t border_x, int32_t border_y,
                         int32_t corner_x, int32_t corner_y, int32_t base_x,
                         int32_t base_y) {
    if (horiz && vert) {
      if (!diag && corner_rect) {
        // Java SpriteAnimation mirrors 8x8 inner-corner tiles on both axes.
        screen->Render(qx, qy, corner_x, corner_y, kMirrorBoth, corner_rect);
      } else if (singleton_with_connective) {
        const int32_t bx = (base_rect->width >= 16) ? base_x : 0;
        const int32_t by = (base_rect->height >= 16) ? base_y : 0;
        screen->Render(qx, qy, bx, by, 0, base_rect);
      } else {
        screen->Render(qx, qy, 1, 1, 0, border_rect);
      }
    } else {
      screen->Render(qx, qy, border_x, border_y, 0, border_rect);
    }
  };

  // Render each 8x8 quadrant exclusively (never base under border/corner).
  render_quad(px, py, l, u, ul, !l ? 0 : 1, !u ? 0 : 1, 0, 0, 1, 1);
  render_quad(px + 8, py, r, u, ur, !r ? 2 : 1, !u ? 0 : 1, 1, 0, 0, 1);
  render_quad(px, py + 8, l, d, dl, !l ? 0 : 1, !d ? 2 : 1, 0, 1, 1, 0);
  render_quad(px + 8, py + 8, r, d, dr, !r ? 2 : 1, !d ? 2 : 1, 1, 1, 0, 0);
}

}  // namespace minicraft::level
