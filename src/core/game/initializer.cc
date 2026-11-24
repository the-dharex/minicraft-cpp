#include "core/game/initializer.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "core/gfx/sprite_atlas.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/io/sound.h"
#include "core/item/fishing_data.h"
#include "core/item/items.h"
#include "core/item/recipes.h"
#include "core/level/biome.h"
#include "core/level/tiles.h"
#include "core/pak/pak_reader.h"
#include "core/platform/file_system.h"
#include "core/platform/platform_context.h"
#include "core/platform/render_backend.h"
#include "core/platform/window_backend.h"
#include "core/saveload/load.h"
#include "core/saveload/save.h"
#include "core/screen/display.h"
#include "core/util/advancements.h"
#include "core/util/book_data.h"
#include "core/util/localization.h"
#include "core/util/logging.h"
#include "json.hpp"

namespace minicraft::game {

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string StripExtension(std::string_view filename) {
  size_t dot = filename.rfind('.');
  return std::string(dot == std::string_view::npos ? filename
                                                   : filename.substr(0, dot));
}

}  // namespace

LaunchOptions Initializer::ParseArgs(int argc, char* argv[]) {
  LaunchOptions opts;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (EqualsIgnoreCase(arg, "--savedir") && i + 1 < argc) {
      opts.save_dir = argv[++i];
    } else if (EqualsIgnoreCase(arg, "--fullscreen")) {
      opts.fullscreen = true;
    } else if (EqualsIgnoreCase(arg, "--debug-locale")) {
      opts.debug_locale = true;
    } else if (EqualsIgnoreCase(arg, "--debug-unloc-tracing")) {
      opts.unloc_tracing = true;
    }
  }
  return opts;
}

bool Initializer::InitializeCore(const pak::PakReader& pak,
                                 const platform::PlatformContext& platform,
                                 gfx::SpriteLinker& linker,
                                 io::InputHandler& input,
                                 const std::string& game_dir,
                                 const LaunchOptions& opts) {
  io::Settings::Instance().InitDefaults();
  if (platform.window) {
    io::Settings::SetMaxFps(platform.window->GetMonitorRefreshRate());
  }
  if (opts.debug_locale) {
    util::Localization::SetDebugLocaleEnabled(true);
  }
  if (opts.unloc_tracing) {
    util::Localization::SetUnlocTracingEnabled(true);
  }

  level::Tiles::InitTiles();
  level::Biomes::Init();
  item::Items::Init();
  item::Recipes::Init();
  item::FishingData::LoadFromPak(pak);

  util::Localization::Instance().InitFromPak(pak);
  if (opts.debug_locale) {
    util::Localization::Instance().ChangeLanguage(
        util::Localization::kDebugLocale);
  }
  util::BookData::Instance().LoadFromPak(pak);
  util::AchievementManager::Instance().LoadFromPak(pak);
  util::AdvancementSystem::Instance().InitFromPak(pak);

  io::Sound::Init(platform.audio, pak);

  if (!linker.LoadAtlasFromPak(pak, platform.renderer)) {
    util::log::kGameHandler.Error("Failed to load Mega-Atlas from assets.pak");
    return false;
  }

  if (platform.window && !linker.LogoRgba().empty()) {
    platform.window->SetIconRgba(linker.LogoWidth(), linker.LogoHeight(),
                                 linker.LogoRgba().data());
  }

  if (platform.file_system && !game_dir.empty()) {
    platform.file_system->CreateDirectories(game_dir);
    saveload::Load::LoadPrefs(
        *platform.file_system, game_dir, &input,
        &screen::DisplayHost::Instance().active_resource_packs);
  }

  if (opts.fullscreen) {
    io::Settings::Set("fullscreen", true);
  }
  if (platform.window && io::Settings::GetBool("fullscreen")) {
    platform.window->SetFullscreen(true);
  }

  return true;
}

void Initializer::ReloadCustomSkins(
    const platform::PlatformContext& platform, gfx::SpriteLinker& linker,
    const std::string& game_dir,
    std::vector<uint16_t>* custom_skin_texture_ids) {
  if (platform.renderer && custom_skin_texture_ids) {
    for (uint16_t tid : *custom_skin_texture_ids) {
      if (tid > 0) platform.renderer->DestroyTexture(tid);
    }
    custom_skin_texture_ids->clear();
  }
  linker.ClearSkins();
  if (!platform.file_system || game_dir.empty()) {
    linker.BumpGeneration();
    return;
  }

  const std::string skins_dir = saveload::Save::JoinWorldPath(game_dir, "skins");
  if (!platform.file_system->IsDirectory(skins_dir)) {
    linker.BumpGeneration();
    return;
  }

  for (const std::string& fname : platform.file_system->ListDirectory(skins_dir)) {
    if (fname.size() < 4 ||
        !EqualsIgnoreCase(std::string_view(fname).substr(fname.size() - 4),
                          ".png")) {
      continue;
    }
    std::vector<uint8_t> png_bytes;
    std::vector<uint8_t> rgba;
    int32_t w = 0;
    int32_t h = 0;
    if (!platform.file_system->ReadBinaryFile(
            saveload::Save::JoinWorldPath(skins_dir, fname), &png_bytes)) {
      continue;
    }
    if (!platform.file_system->DecodePngFromMemory(
            png_bytes.data(), png_bytes.size(), &w, &h, &rgba)) {
      continue;
    }
    if (platform.renderer) {
      uint16_t tid = platform.renderer->CreateTexture(w, h, rgba.data());
      if (tid > 0) {
        if (custom_skin_texture_ids) custom_skin_texture_ids->push_back(tid);
        linker.SetSkin(
            "skin." + StripExtension(fname),
            gfx::SheetRect{tid, 0, 0, static_cast<uint16_t>(w),
                           static_cast<uint16_t>(h), true});
      }
    }
  }
  linker.BumpGeneration();
}

void Initializer::ReloadResourcePacks(
    const pak::PakReader& pak, const platform::PlatformContext& platform,
    gfx::SpriteLinker& linker, const std::string& game_dir,
    const std::vector<std::string>& active_packs,
    std::vector<uint16_t>* custom_texture_ids) {
  if (platform.renderer && custom_texture_ids) {
    for (uint16_t tid : *custom_texture_ids) {
      if (tid > 0) platform.renderer->DestroyTexture(tid);
    }
    custom_texture_ids->clear();
  }

  linker.LoadAtlasFromPak(pak, platform.renderer);
  linker.LoadTileMetadataFromPak(pak);
  util::Localization::Instance().InitFromPak(pak);
  util::BookData::Instance().LoadFromPak(pak);

  if (!platform.file_system || game_dir.empty()) {
    util::Localization::Instance().LoadLanguage();
    return;
  }

  const std::string packs_root =
      saveload::Save::JoinWorldPath(game_dir, "resourcepacks");

  static constexpr struct {
    const char* dir_name;
    gfx::SpriteType type;
  } kTexDirs[] = {
      {"entity", gfx::SpriteType::kEntity},
      {"gui", gfx::SpriteType::kGui},
      {"item", gfx::SpriteType::kItem},
      {"tile", gfx::SpriteType::kTile},
  };

  auto upload_png = [&](const std::string& path, gfx::SpriteType type,
                        const std::string& key) -> bool {
    if (!platform.renderer) return false;
    std::vector<uint8_t> png_bytes;
    std::vector<uint8_t> rgba;
    int32_t w = 0;
    int32_t h = 0;
    if (!platform.file_system->ReadBinaryFile(path, &png_bytes)) return false;
    if (!platform.file_system->DecodePngFromMemory(
            png_bytes.data(), png_bytes.size(), &w, &h, &rgba)) {
      return false;
    }
    uint16_t tid = platform.renderer->CreateTexture(w, h, rgba.data());
    if (tid == 0) return false;
    if (custom_texture_ids) custom_texture_ids->push_back(tid);
    linker.SetSheet(type, key,
                    gfx::SheetRect{tid, 0, 0, static_cast<uint16_t>(w),
                                   static_cast<uint16_t>(h), true});
    return true;
  };

  // Load packs from lowest priority (end of list) to highest (front of list).
  for (auto it = active_packs.rbegin(); it != active_packs.rend(); ++it) {
    const std::string pack_dir = saveload::Save::JoinWorldPath(packs_root, *it);
    if (!platform.file_system->IsDirectory(pack_dir)) continue;

    // Optional pack.json language metadata
    std::string pack_json_text;
    if (platform.file_system->ReadTextFile(
            saveload::Save::JoinWorldPath(pack_dir, "pack.json"),
            &pack_json_text)) {
      try {
        auto pj = nlohmann::json::parse(pack_json_text);
        if (pj.contains("language") && pj["language"].is_object()) {
          for (auto& [raw_tag, info_json] : pj["language"].items()) {
            util::LocaleInformation info;
            info.tag = util::Localization::NormalizeTag(raw_tag);
            info.name = info_json.value("name", info.tag);
            info.region = info_json.value("region", "");
            util::Localization::Instance().AddLocale(info);
          }
        }
      } catch (...) {
      }
    }

    const std::string assets_dir =
        saveload::Save::JoinWorldPath(pack_dir, "assets");

    // Localization overrides (assets/localization/*.json)
    const std::string loc_dir =
        saveload::Save::JoinWorldPath(assets_dir, "localization");
    if (platform.file_system->IsDirectory(loc_dir)) {
      for (const std::string& fname :
           platform.file_system->ListDirectory(loc_dir)) {
        if (fname.size() > 5 &&
            EqualsIgnoreCase(std::string_view(fname).substr(fname.size() - 5),
                             ".json")) {
          std::string json_text;
          if (platform.file_system->ReadTextFile(
                  saveload::Save::JoinWorldPath(loc_dir, fname), &json_text)) {
            std::string tag =
                util::Localization::NormalizeTag(StripExtension(fname));
            util::Localization::Instance().AddLocalization(
                tag, std::move(json_text));
          }
        }
      }
    }

    // Book overrides (assets/books/*.txt)
    const std::string books_dir =
        saveload::Save::JoinWorldPath(assets_dir, "books");
    if (platform.file_system->IsDirectory(books_dir)) {
      auto bind_book_override = [&](const char* fname, auto setter) {
        std::string raw;
        if (platform.file_system->ReadTextFile(
                saveload::Save::JoinWorldPath(books_dir, fname), &raw)) {
          std::string loaded = util::BookData::LoadBook(raw);
          (util::BookData::Instance().*setter)(
              [loaded = std::move(loaded)]() { return loaded; });
        }
      };
      bind_book_override("about.txt", &util::BookData::SetAbout);
      bind_book_override("credits.txt", &util::BookData::SetCredits);
      bind_book_override("instructions.txt", &util::BookData::SetInstructions);
      bind_book_override("antidous.txt", &util::BookData::SetAntVenomBook);
      bind_book_override("game_guide.txt", &util::BookData::SetStorylineGuide);
    }

    // Texture overrides (support both Java assets/textures/<cat> and legacy textures/<cat>)
    const std::string tex_roots[2] = {
        saveload::Save::JoinWorldPath(pack_dir, "textures"),
        saveload::Save::JoinWorldPath(assets_dir, "textures"),
    };
    for (const std::string& tex_root : tex_roots) {
      if (!platform.renderer || !platform.file_system->IsDirectory(tex_root)) {
        continue;
      }
      for (const auto& td : kTexDirs) {
        const std::string cat_dir =
            saveload::Save::JoinWorldPath(tex_root, td.dir_name);
        if (!platform.file_system->IsDirectory(cat_dir)) continue;
        std::vector<std::string> files =
            platform.file_system->ListDirectory(cat_dir);

        if (td.type == gfx::SpriteType::kTile) {
          for (const std::string& fname : files) {
            if (fname.size() <= 9 ||
                !EqualsIgnoreCase(
                    std::string_view(fname).substr(fname.size() - 9),
                    ".png.json")) {
              continue;
            }
            std::string meta_text;
            if (!platform.file_system->ReadTextFile(
                    saveload::Save::JoinWorldPath(cat_dir, fname),
                    &meta_text)) {
              continue;
            }
            try {
              auto obj = nlohmann::json::parse(meta_text);
              std::string base_key = fname.substr(0, fname.size() - 9);
              std::string img_name = base_key + ".png";
              gfx::SpriteMeta meta;
              if (obj.contains("animation") && obj["animation"].is_object()) {
                meta.frametime = obj["animation"].value("frametime", 0);
              }
              std::vector<uint8_t> png_bytes, rgba;
              int32_t w = 0, h = 0;
              if (platform.file_system->ReadBinaryFile(
                      saveload::Save::JoinWorldPath(cat_dir, img_name),
                      &png_bytes) &&
                  platform.file_system->DecodePngFromMemory(
                      png_bytes.data(), png_bytes.size(), &w, &h, &rgba)) {
                if (meta.frametime > 0 && h >= 16) meta.frames = h / 16;
                uint16_t tid = platform.renderer->CreateTexture(w, h, rgba.data());
                if (tid > 0) {
                  if (custom_texture_ids) custom_texture_ids->push_back(tid);
                  linker.SetSheet(
                      gfx::SpriteType::kTile, base_key,
                      gfx::SheetRect{tid, 0, 0, static_cast<uint16_t>(w),
                                     static_cast<uint16_t>(h), true});
                }
              }
              if (obj.contains("border") && obj["border"].is_object()) {
                const auto& b = obj["border"];
                if (b.contains("key") && b["key"].is_string()) {
                  std::string bk = b["key"].get<std::string>();
                  if (!bk.empty() &&
                      upload_png(
                          saveload::Save::JoinWorldPath(cat_dir, bk + ".png"),
                          gfx::SpriteType::kTile, bk)) {
                    meta.border = bk;
                  }
                }
                if (b.contains("corner") && b["corner"].is_string()) {
                  std::string ck = b["corner"].get<std::string>();
                  if (!ck.empty() &&
                      upload_png(
                          saveload::Save::JoinWorldPath(cat_dir, ck + ".png"),
                          gfx::SpriteType::kTile, ck)) {
                    meta.corner = ck;
                  }
                }
              }
              linker.SetMetadata(base_key, meta);
            } catch (...) {
            }
          }
        }

        for (const std::string& fname : files) {
          if (fname.size() < 4 ||
              !EqualsIgnoreCase(
                  std::string_view(fname).substr(fname.size() - 4), ".png")) {
            continue;
          }
          upload_png(saveload::Save::JoinWorldPath(cat_dir, fname), td.type,
                     StripExtension(fname));
        }
      }
    }
  }

  util::Localization::Instance().LoadLanguage();
  if (screen::DisplayHost::Instance().reload_skins) {
    screen::DisplayHost::Instance().reload_skins("");
  }
}

}  // namespace minicraft::game
