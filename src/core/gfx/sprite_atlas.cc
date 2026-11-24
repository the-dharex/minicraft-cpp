#include "core/gfx/sprite_atlas.h"

#include <cstring>

#include "core/pak/pak_format.h"
#include "json.hpp"

namespace minicraft {
namespace gfx {

bool SpriteLinker::LoadAtlasFromPak(const pak::PakReader& pak,
                                    platform::IRenderBackend* backend) {
  std::vector<uint8_t> bin;
  if (!pak.ReadEntry("__atlas__/atlas.bin", &bin)) {
    return false;
  }
  if (bin.size() < sizeof(pak::AtlasHeader)) {
    return false;
  }

  pak::AtlasHeader header{};
  std::memcpy(&header, bin.data(), sizeof(header));
  if (std::memcmp(header.magic, "MATL", 4) != 0) {
    return false;
  }

  size_t offset = sizeof(header);
  for (auto& m : sheets_) {
    m.clear();
  }

  SheetRect logo_rect{};
  for (uint32_t i = 0; i < header.sheet_count; ++i) {
    if (offset + sizeof(pak::AtlasSheetEntry) > bin.size()) return false;
    pak::AtlasSheetEntry entry{};
    std::memcpy(&entry, bin.data() + offset, sizeof(entry));
    offset += sizeof(entry);
    if (offset + entry.name_length > bin.size()) return false;
    std::string key(reinterpret_cast<const char*>(bin.data() + offset),
                    entry.name_length);
    offset += entry.name_length;

    if (entry.category < 5) {
      SheetRect rect{0, entry.x, entry.y, entry.w, entry.h, true};
      sheets_[entry.category][key] = rect;
      if (entry.category == static_cast<uint8_t>(SpriteType::kMisc) &&
          key == "logo") {
        logo_rect = rect;
      }
    }
  }

  size_t pixel_bytes =
      static_cast<size_t>(header.width) * header.height * 4U;
  if (offset + pixel_bytes > bin.size()) {
    return false;
  }
  const uint8_t* pixels = bin.data() + offset;

  if (logo_rect.valid) {
    logo_w_ = logo_rect.width;
    logo_h_ = logo_rect.height;
    logo_rgba_.resize(static_cast<size_t>(logo_w_) * logo_h_ * 4U);
    for (int32_t row = 0; row < logo_h_; ++row) {
      size_t src_off =
          (static_cast<size_t>(logo_rect.atlas_y + row) * header.width +
           logo_rect.atlas_x) *
          4U;
      size_t dst_off = static_cast<size_t>(row) * logo_w_ * 4U;
      std::memcpy(logo_rgba_.data() + dst_off, pixels + src_off,
                  static_cast<size_t>(logo_w_) * 4U);
    }
  }

  if (backend != nullptr) {
    if (!backend->UploadAtlas(header.width, header.height, pixels)) {
      return false;
    }
  }

  LoadTileMetadataFromPak(pak);
  BumpGeneration();
  return true;
}

void SpriteLinker::LoadTileMetadataFromPak(const pak::PakReader& pak) {
  metas_.clear();
  for (const std::string& path : pak.ListEntries()) {
    constexpr std::string_view kPrefix = "assets/textures/tile/";
    constexpr std::string_view kSuffix = ".png.json";
    if (path.size() > kPrefix.size() + kSuffix.size() &&
        path.compare(0, kPrefix.size(), kPrefix) == 0 &&
        path.compare(path.size() - kSuffix.size(), kSuffix.size(), kSuffix) ==
            0) {
      std::string key = path.substr(
          kPrefix.size(), path.size() - kPrefix.size() - kSuffix.size());
      std::string text;
      if (!pak.ReadTextEntry(path, &text)) continue;

      nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
      if (j.is_discarded()) continue;

      SpriteMeta meta;
      if (j.contains("border") && j["border"].is_object()) {
        const auto& b = j["border"];
        if (b.contains("key") && b["key"].is_string()) {
          meta.border = b["key"].get<std::string>();
        }
        if (b.contains("corner") && b["corner"].is_string()) {
          meta.corner = b["corner"].get<std::string>();
        }
      }
      if (j.contains("animation") && j["animation"].is_object()) {
        const auto& a = j["animation"];
        if (a.contains("frametime") && a["frametime"].is_number_integer()) {
          meta.frametime = a["frametime"].get<int>();
        }
        if (a.contains("frames") && a["frames"].is_number_integer()) {
          meta.frames = a["frames"].get<int>();
        }
      }
      if (const SheetRect* sheet = GetSheet(SpriteType::kTile, key)) {
        if (meta.frametime > 0 && meta.frames <= 1 && sheet->width > 0) {
          meta.frames = std::max(1, sheet->height / sheet->width);
        }
      }
      metas_[key] = std::move(meta);
    }
  }
}

void SpriteLinker::ResetOverrides() {
  for (auto& m : sheets_) m.clear();
  metas_.clear();
  BumpGeneration();
}

void SpriteLinker::SetSheet(SpriteType type, std::string_view key,
                            const SheetRect& rect) {
  sheets_[static_cast<size_t>(type)][std::string(key)] = rect;
  BumpGeneration();
}

const SheetRect* SpriteLinker::GetSheet(SpriteType type,
                                        std::string_view key) const {
  const auto& m = sheets_[static_cast<size_t>(type)];
  auto it = m.find(std::string(key));
  return it != m.end() ? &it->second : nullptr;
}

const SheetRect* SpriteLinker::MissingSheet(SpriteType type) const {
  switch (type) {
    case SpriteType::kEntity:
      return GetSheet(SpriteType::kEntity, "missing_entity");
    case SpriteType::kItem:
      return GetSheet(SpriteType::kItem, "missing_item");
    case SpriteType::kTile:
      return GetSheet(SpriteType::kTile, "missing_tile");
    default:
      return GetSheet(SpriteType::kItem, "missing_item");
  }
}

void SpriteLinker::ClearSkins() {
  auto& m = sheets_[static_cast<size_t>(SpriteType::kEntity)];
  for (auto it = m.begin(); it != m.end();) {
    if (it->first.rfind("skin.", 0) == 0) {
      it = m.erase(it);
    } else {
      ++it;
    }
  }
  BumpGeneration();
}

void SpriteLinker::SetSkin(std::string_view key, const SheetRect& rect) {
  SetSheet(SpriteType::kEntity, key, rect);
}

void SpriteLinker::SetMetadata(std::string_view key, const SpriteMeta& meta) {
  metas_[std::string(key)] = meta;
}

const SpriteMeta* SpriteLinker::GetMetadata(std::string_view key) const {
  auto it = metas_.find(std::string(key));
  return it != metas_.end() ? &it->second : nullptr;
}

Sprite Sprite::FromGrid(int32_t sx, int32_t sy, int32_t w_tiles,
                        int32_t h_tiles, SpriteType type, std::string_view key,
                        int32_t mirror, int32_t color) {
  Sprite sp(w_tiles, h_tiles, color);
  for (int32_t r = 0; r < h_tiles; ++r) {
    for (int32_t c = 0; c < w_tiles; ++c) {
      SpritePx& px = sp.pixels[r * w_tiles + c];
      px.tile_x = sx + c;
      px.tile_y = sy + r;
      px.mirror = static_cast<uint8_t>(mirror);
      px.type = type;
      px.sheet_key = std::string(key);
    }
  }
  return sp;
}

LinkedSprite::LinkedSprite(SpriteType type, std::string key)
    : type_(type), key_(std::move(key)) {}

LinkedSprite& LinkedSprite::SetSpriteSize(int32_t w, int32_t h) {
  w_ = w;
  h_ = h;
  cached_gen_ = 0;
  return *this;
}

LinkedSprite& LinkedSprite::SetSpritePos(int32_t x, int32_t y) {
  x_ = x;
  y_ = y;
  cached_gen_ = 0;
  return *this;
}

LinkedSprite& LinkedSprite::SetSpriteDim(int32_t x, int32_t y, int32_t w,
                                         int32_t h) {
  x_ = x;
  y_ = y;
  w_ = w;
  h_ = h;
  cached_gen_ = 0;
  return *this;
}

LinkedSprite& LinkedSprite::SetColor(int32_t color) {
  color_ = color;
  cached_gen_ = 0;
  return *this;
}

LinkedSprite& LinkedSprite::SetMirror(int32_t mirror) {
  mirror_ = mirror;
  cached_gen_ = 0;
  return *this;
}

LinkedSprite& LinkedSprite::SetFlip(int32_t flip) {
  flip_ = flip;
  cached_gen_ = 0;
  return *this;
}

const SheetRect* LinkedSprite::GetSheet(const SpriteLinker& linker) const {
  return linker.GetSheet(type_, key_);
}

const Sprite& LinkedSprite::GetSprite(const SpriteLinker& linker) {
  if (cached_gen_ == linker.Generation()) {
    return cached_sprite_;
  }
  const SheetRect* sheet = linker.GetSheet(type_, key_);
  if (sheet == nullptr) {
    const char* missing_key = "missing_item";
    if (type_ == SpriteType::kEntity) missing_key = "missing_entity";
    else if (type_ == SpriteType::kTile) missing_key = "missing_tile";
    sheet = linker.GetSheet(type_, missing_key);
    int32_t w = sheet ? std::max(1, sheet->width / 8) : 2;
    int32_t h = sheet ? std::max(1, sheet->height / 8) : 2;
    cached_sprite_ = Sprite::FromGrid(0, 0, w, h, type_, missing_key, 0, -1);
    cached_gen_ = linker.Generation();
    return cached_sprite_;
  }

  int32_t w = w_ > 0 ? w_ : std::max(1, sheet->width / 8);
  int32_t h = h_ > 0 ? h_ : std::max(1, sheet->height / 8);
  bool flip_x = (flip_ & 0x01) != 0;
  bool flip_y = (flip_ & 0x02) != 0;

  cached_sprite_ = Sprite(w, h, color_);
  for (int32_t r = 0; r < h; ++r) {
    for (int32_t c = 0; c < w; ++c) {
      int32_t xo = flip_x ? (w - 1 - c) : c;
      int32_t yo = flip_y ? (h - 1 - r) : r;
      SpritePx& px = cached_sprite_.pixels[r * w + c];
      px.tile_x = x_ + xo;
      px.tile_y = y_ + yo;
      px.mirror = static_cast<uint8_t>(mirror_);
      px.type = type_;
      px.sheet_key = key_;
    }
  }
  cached_gen_ = linker.Generation();
  return cached_sprite_;
}

}  // namespace gfx
}  // namespace minicraft
