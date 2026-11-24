#ifndef MINICRAFT_CORE_GFX_SPRITE_ATLAS_H_
#define MINICRAFT_CORE_GFX_SPRITE_ATLAS_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/pak/pak_reader.h"
#include "core/platform/render_backend.h"

namespace minicraft {
namespace gfx {

enum class SpriteType : uint8_t {
  kEntity = 0,
  kGui = 1,
  kItem = 2,
  kTile = 3,
  kMisc = 4,
};

struct SheetRect {
  uint16_t texture_id = 0;  // 0 = main Mega-Atlas
  uint16_t atlas_x = 0;
  uint16_t atlas_y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  bool valid = false;
};

struct SpriteMeta {
  int frames = 1;
  int frametime = 0;
  std::string border;
  std::string corner;
};

class SpriteLinker {
 public:
  bool LoadAtlasFromPak(const pak::PakReader& pak,
                        platform::IRenderBackend* backend);
  void ResetOverrides();
  void SetSheet(SpriteType type, std::string_view key, const SheetRect& rect);
  const SheetRect* GetSheet(SpriteType type, std::string_view key) const;
  const SheetRect* MissingSheet(SpriteType type) const;

  void ClearSkins();
  void SetSkin(std::string_view key, const SheetRect& rect);

  void SetMetadata(std::string_view key, const SpriteMeta& meta);
  const SpriteMeta* GetMetadata(std::string_view key) const;
  void LoadTileMetadataFromPak(const pak::PakReader& pak);

  // Raw RGBA bytes of special misc images extracted from atlas.bin (e.g. "logo").
  const std::vector<uint8_t>& LogoRgba() const { return logo_rgba_; }
  int32_t LogoWidth() const { return logo_w_; }
  int32_t LogoHeight() const { return logo_h_; }

  uint32_t Generation() const { return generation_; }
  void BumpGeneration() { ++generation_; }

 private:
  std::unordered_map<std::string, SheetRect> sheets_[5];
  std::unordered_map<std::string, SpriteMeta> metas_;
  std::vector<uint8_t> logo_rgba_;
  int32_t logo_w_ = 0;
  int32_t logo_h_ = 0;
  uint32_t generation_ = 1;
};

struct SpritePx {
  int32_t tile_x = 0;
  int32_t tile_y = 0;
  uint8_t mirror = 0;
  SpriteType type = SpriteType::kItem;
  std::string sheet_key;
};

struct Sprite {
  int32_t cols = 0;
  int32_t rows = 0;
  int32_t color = -1;
  std::vector<SpritePx> pixels;  // Row-major: r * cols + c.

  Sprite() = default;
  Sprite(int32_t w_tiles, int32_t h_tiles, int32_t col = -1)
      : cols(w_tiles), rows(h_tiles), color(col), pixels(w_tiles * h_tiles) {}
  static Sprite FromGrid(int32_t sx, int32_t sy, int32_t w_tiles,
                         int32_t h_tiles, SpriteType type,
                         std::string_view key, int32_t mirror = 0,
                         int32_t color = -1);
};

class LinkedSprite {
 public:
  LinkedSprite() = default;
  LinkedSprite(SpriteType type, std::string key);

  LinkedSprite& SetSpriteSize(int32_t w, int32_t h);
  LinkedSprite& SetSpritePos(int32_t x, int32_t y);
  LinkedSprite& SetSpriteDim(int32_t x, int32_t y, int32_t w, int32_t h);
  LinkedSprite& SetColor(int32_t color);
  LinkedSprite& SetMirror(int32_t mirror);
  LinkedSprite& SetFlip(int32_t flip);

  const SheetRect* GetSheet(const SpriteLinker& linker) const;
  const Sprite& GetSprite(const SpriteLinker& linker);
  void Invalidate() { cached_gen_ = 0; }

  SpriteType Type() const { return type_; }
  const std::string& Key() const { return key_; }

 private:
  SpriteType type_ = SpriteType::kItem;
  std::string key_;
  int32_t x_ = 0;
  int32_t y_ = 0;
  int32_t w_ = 0;
  int32_t h_ = 0;
  int32_t color_ = -1;
  int32_t mirror_ = 0;
  int32_t flip_ = 0;
  uint32_t cached_gen_ = 0;
  Sprite cached_sprite_;
};

}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_SPRITE_ATLAS_H_
