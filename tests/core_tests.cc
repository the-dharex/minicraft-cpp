#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/entity/boat.h"
#include "core/entity/furniture.h"
#include "core/entity/mob.h"
#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/entity/projectiles.h"
#include "core/game/game_state.h"
#include "core/game/updater.h"
#include "core/game/world.h"
#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/gfx/sprite_animation.h"
#include "core/gfx/sprite_atlas.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/io/sound.h"
#include "core/item/fishing_data.h"
#include "core/item/inventory.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/item/recipes.h"
#include "core/level/biome.h"
#include "core/level/chunk_manager.h"
#include "core/level/level.h"
#include "core/level/level_noise.h"
#include "core/level/structure.h"
#include "core/level/tiles.h"
#include "core/pak/pak_reader.h"
#include "core/platform/audio_backend.h"
#include "core/platform/platform_context.h"
#include "core/platform/render_backend.h"
#include "core/saveload/binary_codec.h"
#include "core/saveload/load.h"
#include "core/saveload/save.h"
#include "core/saveload/version.h"
#include "core/screen/displays.h"
#include "core/screen/entries.h"
#include "core/screen/menu.h"
#include "core/util/advancements.h"
#include "core/util/book_data.h"
#include "core/util/localization.h"
#include "platform/pc/pc_clock.h"
#include "platform/pc/pc_file_system.h"
#include "platform/pc/precompiled_shaders.h"

namespace {

int g_failures = 0;

void Check(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "[FAIL] " << msg << "\n";
    ++g_failures;
  }
}

class DummyRenderBackend final : public minicraft::platform::IRenderBackend {
 public:
  size_t last_command_count = 0;
  std::vector<minicraft::platform::DrawCmd> last_commands;

  bool UploadAtlas(int32_t, int32_t, const uint8_t*) override { return true; }
  uint16_t CreateTexture(int32_t, int32_t, const uint8_t*) override { return 1; }
  void DestroyTexture(uint16_t) override {}
  void RenderFrame(const minicraft::platform::FrameRenderData& frame) override {
    last_command_count = frame.command_count;
    last_commands.assign(frame.commands, frame.commands + frame.command_count);
  }
  bool ReadBackScreenshot(int32_t*, int32_t*, uint8_t*, size_t) override {
    return false;
  }
};

std::string FindAssetsPak(const minicraft::platform::pc::PcFileSystem& fs) {
  const char* paths[] = {
      "assets/assets.pak",
      "minicraft-cpp/assets/assets.pak",
      "../assets/assets.pak",
      "../../assets/assets.pak",
  };
  for (const char* p : paths) {
    if (fs.Exists(p)) return p;
  }
  return "";
}

}  // namespace

int main() {
  using namespace minicraft;

  platform::pc::PcFileSystem fs;
  platform::pc::PcClock clock;

  const std::string pak_path = FindAssetsPak(fs);
  Check(!pak_path.empty(), "Find assets.pak");
  if (pak_path.empty()) return 1;

  std::vector<uint8_t> pak_bytes;
  Check(fs.ReadBinaryFile(pak_path, &pak_bytes), "Read assets.pak bytes");

  pak::PakReader pak;
  Check(pak.LoadFromMemory(std::move(pak_bytes)), "PakReader LoadFromMemory");
  Check(pak.EntryCount() >= 50, "Pak entry count >= 50");
  Check(pak.HasEntry("__atlas__/atlas.bin"), "Has mega-atlas binary");
  Check(pak.HasEntry("resources/achievements.json"), "Has achievements.json");
  Check(pak.HasEntry("resources/recipes.json"), "Has recipes.json");
  Check(pak.HasEntry("resources/quests.json"), "Has quests.json");
  Check(pak.HasEntry("resources/tutorials.json"), "Has tutorials.json");
  Check(pak.HasEntry("assets/sound/craft.wav"), "Has craft.wav");

  // 2. Settings, Registries, Atlas, Localization, Books, Advancements
  io::Settings::Instance().InitDefaults();
  level::Tiles::InitTiles();
  level::Biomes::Init();
  item::Items::Init();
  item::Recipes::Init();
  item::FishingData::LoadFromPak(pak);
  util::Localization::Instance().InitFromPak(pak);
  util::BookData::Instance().LoadFromPak(pak);
  util::AchievementManager::Instance().LoadFromPak(pak);
  util::AdvancementSystem::Instance().InitFromPak(pak);

  gfx::SpriteLinker linker;
  Check(linker.LoadAtlasFromPak(pak, nullptr), "LoadAtlasFromPak headless");
  Check(linker.GetSheet(gfx::SpriteType::kGui, "font") != nullptr,
        "Atlas contains gui/font");
  Check(linker.GetSheet(gfx::SpriteType::kGui, "hud") != nullptr,
        "Atlas contains gui/hud");
  Check(linker.GetSheet(gfx::SpriteType::kGui, "title") != nullptr,
        "Atlas contains gui/title");
  Check(linker.GetSheet(gfx::SpriteType::kTile, "grass") != nullptr,
        "Atlas contains tile/grass");
  Check(linker.GetSheet(gfx::SpriteType::kEntity, "cow") != nullptr,
        "Atlas contains entity/cow");
  Check(linker.GetSheet(gfx::SpriteType::kMisc, "default_skins") != nullptr,
        "Atlas contains misc/default_skins");
  Check(!linker.LogoRgba().empty(), "Extracted window logo RGBA");
  {
    std::vector<uint8_t> splash_png;
    Check(pak.ReadEntry("Minicraft_Splash_Screen_3.png", &splash_png),
          "Pak contains Minicraft_Splash_Screen_3.png");
    int32_t sw = 0, sh = 0;
    std::vector<uint8_t> splash_rgba;
    Check(fs.DecodePngFromMemory(splash_png.data(), splash_png.size(), &sw, &sh,
                                 &splash_rgba) &&
              sw == 381 && sh == 248,
          "Splash screen PNG decodes as 381x248 RGBA");
  }

  Check(!util::BookData::Instance().About().empty(), "About book loaded");
  Check(!util::BookData::Instance().Instructions().empty(),
        "Instructions book loaded");
  Check(!util::AchievementManager::Instance().OrderedIds().empty(),
        "Achievements loaded");
  Check(!util::AdvancementSystem::Instance().QuestSeriesList().empty(),
        "Quests loaded");
  Check(!item::FishingData::FishData().empty(), "Fishing data loaded");
  Check(gfx::Font::TextWidth("Hello") == 40, "Font TextWidth exact 8px glyphs");
  Check(gfx::Font::GetLines("One two three four five six", 40, 100, 1).size() > 1,
        "Font wrapping splits long strings");

  // 3. Version semver ordering
  Check(saveload::Version("2.0.7-dev1") < saveload::Version("2.0.7"),
        "Dev version < release");
  Check(saveload::Version("2.3.0-infdev3").IsValid(), "Current version valid");

  // 4. Full 6-depth World Generation & Infinite Chunk Generation
  const std::string temp_dir =
      (std::filesystem::temp_directory_path() / "minicraft_cpp_test_root")
          .string();
  fs.RemoveDirectoryAll(temp_dir);
  fs.CreateDirectories(temp_dir);

  io::InputHandler input;
  platform::PlatformContext plat{};
  plat.file_system = &fs;
  plat.clock = &clock;

  // Test real World instance on stack and clean exit.
  {
    game::World world;
    game::Updater updater;
    updater.WireHostBridges(world, input, plat, pak, linker, temp_dir, []() {},
                            []() {});

    Check(world.InitWorld(&fs, temp_dir, "TestWorld", true, int64_t{424242},
                          &input, &updater),
          "Generate 6-depth TestWorld");

    Check(world.CurrentLevelIdx() == game::World::kSurfaceLevelIdx,
          "Starts on Surface (idx 3)");
    for (int32_t i = 0; i < 6; ++i) {
      Check(world.Levels()[i] != nullptr, "Level slot populated");
      Check(world.Levels()[i]->depth == game::World::kIdxToDepth[i],
            "Level depth matches kIdxToDepth");
    }
    Check(world.GetPlayer() != nullptr, "Player spawned");
    Check(world.CurrentLevel()->GetTile(world.GetPlayer()->x >> 4,
                                        world.GetPlayer()->y >> 4) != nullptr,
          "Player spawn tile valid");

    // Infinite chunk exploration way beyond initial 128x128 bounds
    const level::Tile* far_tile = world.CurrentLevel()->GetTile(350, -220);
    Check(far_tile != nullptr, "Infinite chunk tile generated at (350, -220)");

    // Add chest + sign + custom item and save
    auto chest = std::make_shared<entity::Chest>();
    chest->GetInventory()->Add(item::Items::Get("Gold Sword"));
    auto apple = item::Items::Get("Apple");
    Check(apple != nullptr, "Apple item lookup non-null");
    if (apple) {
      chest->GetInventory()->Add(*apple, 5);
    }
    world.CurrentLevel()->Add(chest, 64 * 16 + 8, 65 * 16 + 8);
    world.CurrentLevel()->Tick(false);

    world.GetPlayer()->AddItemToInventory(item::Items::Get("Gem Pickaxe"));
    world.GetPlayer()->SetScore(1234);
    screen::SignDisplay::UpdateSign(0, 66, 66, {"Line1", "Line2", "", "End"});

    Check(world.SaveCurrentWorld(fs, temp_dir, "TestWorld"),
          "Save TestWorld");

    auto worlds = saveload::Load::ListWorlds(fs, temp_dir + "/saves");
    Check(worlds.size() == 1 && worlds[0].folder_name == "TestWorld",
          "ListWorlds finds TestWorld");

    // Unload and reload into a fresh World
    world.UnloadWorld(&clock);
    Check(world.GetPlayer() == nullptr, "World unloaded cleanly");

    Check(world.InitWorld(&fs, temp_dir, "TestWorld", false, std::nullopt,
                          &input, &updater),
          "Reload TestWorld from disk");
    Check(world.GetPlayer() != nullptr, "Reloaded player non-null");
    Check(world.GetPlayer()->GetScore() == 1234, "Reloaded score == 1234");
    auto gem_pick = item::Items::Get("Gem Pickaxe");
    Check(world.GetPlayer()->GetInventory().Count(*gem_pick) >= 1,
          "Reloaded inventory contains Gem Pickaxe");
    const auto* sign_lines = screen::SignDisplay::GetSign(0, 66, 66);
    Check(sign_lines != nullptr && sign_lines->size() >= 2 &&
              (*sign_lines)[0] == "Line1",
          "Reloaded sign text matches");

    // Headless render tick smoke test
    gfx::Screen screen(&linker);
    world.CurrentLevel()->RenderBackground(&screen, 0, 0);
    world.CurrentLevel()->RenderSprites(&screen, 0, 0);
    DummyRenderBackend backend;
    screen.Flush(&backend, false);
    Check(backend.last_command_count > 10,
          "Software/Command render produced commands");
  }

  // 4b. Java 2.2.4 comma-separated .miniplussave compatibility test
  {
    const std::string java_world_dir = temp_dir + "/saves/JavaWorld";
    fs.CreateDirectories(java_world_dir);

    std::string grass_tiles;
    grass_tiles.reserve(128 * 128 * 6);
    std::string level3_tiles;
    level3_tiles.reserve(128 * 128 * 6);
    std::string zero_data;
    zero_data.reserve(128 * 128 * 2);
    std::string level3_data;
    level3_data.reserve(128 * 128 * 2);
    for (int32_t col = 0; col < 128; ++col) {
      for (int32_t row = 0; row < 128; ++row) {
        grass_tiles += "GRASS,";
        level3_tiles += (col == 5 && row == 7) ? "SAND," : "GRASS,";
        zero_data += "0,";
        level3_data += (col == 5 && row == 7) ? "3," : "0,";
      }
    }

    fs.WriteTextFile(java_world_dir + "/Game.miniplussave",
                     "2.2.4,123456,0,1806,2472,1,false,false,false,false,");
    fs.WriteTextFile(
        java_world_dir + "/Player.miniplussave",
        "585,1036,33,63,10,0,10,0,0,NULL,42,3,PotionEffects[SPEED;120],20132608,{},");
    fs.WriteTextFile(java_world_dir + "/Inventory.miniplussave",
                     "Acorn_3,Wood_6,");
    fs.WriteTextFile(java_world_dir + "/Entities.miniplussave",
                     "Chest[128:128:Wood_2],");
    fs.WriteTextFile(
        java_world_dir + "/signs.json",
        R"({"Version":"2.2.4","signs":[{"level":3,"x":10,"y":12,"lines":["Hola","Java","","!"]}]})");
    static constexpr int32_t kTestDepths[6] = {-3, -2, -1, 0, 1, -4};
    for (int32_t lvl = 0; lvl < 6; ++lvl) {
      const std::string ls = std::to_string(lvl);
      const std::string header =
          "128,128,123456," + std::to_string(kTestDepths[lvl]) + ",";
      fs.WriteTextFile(java_world_dir + "/Level" + ls + ".miniplussave",
                       header + (lvl == 3 ? level3_tiles : grass_tiles));
      fs.WriteTextFile(java_world_dir + "/Level" + ls + "data.miniplussave",
                       lvl == 3 ? level3_data : zero_data);
    }

    auto worlds = saveload::Load::ListWorlds(fs, temp_dir + "/saves");
    bool found_java = false;
    for (const auto& w : worlds) {
      if (w.folder_name == "JavaWorld") {
        found_java = (w.score == 42);
      }
    }
    Check(found_java, "ListWorlds detects Java 2.2.4 world and score");

    game::World world;
    game::Updater updater;
    updater.WireHostBridges(world, input, plat, pak, linker, temp_dir, []() {},
                            []() {});
    Check(world.InitWorld(&fs, temp_dir, "JavaWorld", false, std::nullopt,
                          &input, &updater),
          "Load Java 2.2.4 world");
    Check(world.CurrentLevelIdx() == 3, "Java world restores level idx 3");
    {
      const auto& gs_ref = game::GameState::Instance();
      Check(gs_ref.game_time == 1806,
            "Java world restores tickCount (1806) without overwriting by gameTime");
      Check(gs_ref.tick_count == 2472,
            "Java world restores cumulative gameTime (2472) into tick_count");
      Check(!gs_ref.past_day1,
            "Java world sets past_day1=false when gameTime <= 65000");
    }
    Check(world.CurrentLevel()->GetTile(5, 7) == level::Tiles::Get("Sand"),
          "Java column-major tile mapped to (5, 7)");
    Check(world.CurrentLevel()->GetData(5, 7) == 3,
          "Java column-major tile data mapped to (5, 7)");
    Check(world.GetPlayer() != nullptr && world.GetPlayer()->x == 585 &&
              world.GetPlayer()->y == 1036,
          "Java player position restored");
    Check(world.GetPlayer()->GetScore() == 42, "Java player score == 42");
    Check(world.GetPlayer()->potion_effects.count(0) == 1 &&
              world.GetPlayer()->potion_effects[0] == 120,
          "Java potion effect SPEED restored");
    auto acorn = item::Items::Get("Acorn");
    auto wood = item::Items::Get("Wood");
    Check(acorn && world.GetPlayer()->GetInventory().Count(*acorn) == 3,
          "Java inventory Acorn x3 restored");
    Check(wood && world.GetPlayer()->GetInventory().Count(*wood) == 6,
          "Java inventory Wood x6 restored");
    const auto* j_sign = screen::SignDisplay::GetSign(3, 10, 12);
    Check(j_sign != nullptr && !j_sign->empty() && (*j_sign)[0] == "Hola",
          "Java signs.json loaded");
    world.UnloadWorld(&clock);
  }

  // 5. Main Menu & Submenu Localization + Window Fit Tests (en-us & es-es)
  {
    screen::DisplayHost::Instance().input = &input;
    screen::DisplayHost::Instance().fs = &fs;
    screen::DisplayHost::Instance().pak = &pak;
    screen::DisplayHost::Instance().game_dir = temp_dir;

    auto verify_display = [&](screen::Display& disp, const char* name,
                              gfx::Screen& scr) {
      disp.Init(nullptr);
      disp.Render(&scr);
      for (const auto& menu : disp.Menus()) {
        if (!menu) continue;
        const gfx::Rectangle& b = menu->GetBounds();
        if (b.Width() > 0 && b.Height() > 0) {
          Check(b.Left() >= 0 && b.Right() <= gfx::Screen::w,
                (std::string(name) + " fits horizontally").c_str());
          Check(b.Top() >= 0 && b.Bottom() <= gfx::Screen::h,
                (std::string(name) + " fits vertically").c_str());
        }
        const std::string& title = menu->GetTitle();
        Check(title.rfind("minicraft.", 0) != 0,
              (std::string(name) + " title localized").c_str());
        Check(title.find("%s") == std::string::npos,
              (std::string(name) + " title formatted").c_str());
        for (int32_t i = 0; i < menu->GetNumOptions(); ++i) {
          const screen::ListEntry* entry = menu->GetEntry(i);
          if (!entry) continue;
          const std::string text = entry->ToString();
          Check(text.rfind("minicraft.", 0) != 0,
                (std::string(name) + " entry localized").c_str());
          Check(text.find(": minicraft.") == std::string::npos,
                (std::string(name) + " setting value localized").c_str());
          Check(text.find("%s") == std::string::npos,
                (std::string(name) + " entry formatted").c_str());
          Check(entry->GetWidth() <= gfx::Screen::w - 16,
                (std::string(name) + " entry fits width").c_str());
        }
      }
    };

    gfx::Screen ui_screen(&linker);
    for (const char* lang : {"en-us", "es-es"}) {
      util::Localization::Instance().ChangeLanguage(lang);
      Check(util::Localization::Instance().SelectedLocaleTag() == lang,
            (std::string("Switch locale to ") + lang).c_str());

      screen::TitleDisplay title;
      verify_display(title, "TitleDisplay", ui_screen);

      screen::WorldGenDisplay world_gen;
      verify_display(world_gen, "WorldGenDisplay", ui_screen);

      screen::WorldSelectDisplay world_sel;
      verify_display(world_sel, "WorldSelectDisplay", ui_screen);

      screen::OptionsMainMenuDisplay opt_main;
      verify_display(opt_main, "OptionsMainMenuDisplay", ui_screen);

      screen::OptionsWorldDisplay opt_world;
      verify_display(opt_world, "OptionsWorldDisplay", ui_screen);

      screen::ControlsDisplay controls;
      verify_display(controls, "ControlsDisplay", ui_screen);

      screen::KeyInputDisplay key_input;
      verify_display(key_input, "KeyInputDisplay", ui_screen);

      screen::LanguageSettingsDisplay lang_disp;
      verify_display(lang_disp, "LanguageSettingsDisplay", ui_screen);

      screen::SkinDisplay skin_disp;
      verify_display(skin_disp, "SkinDisplay", ui_screen);

      screen::ResourcePackDisplay rp_disp;
      verify_display(rp_disp, "ResourcePackDisplay", ui_screen);

      screen::AchievementsDisplay ach_disp;
      verify_display(ach_disp, "AchievementsDisplay", ui_screen);
    }
    util::Localization::Instance().ChangeLanguage("en-us");
  }

  // 6. Exhaustive Sprite Atlas & Mob/Player/Item/Tile Bounds Verification
  {
    auto cmd_in_rect = [](const platform::DrawCmd& cmd,
                          const gfx::SheetRect* r) -> bool {
      if (!r || !r->valid) return false;
      return cmd.sprite.src_x >= r->atlas_x && cmd.sprite.src_y >= r->atlas_y &&
             cmd.sprite.src_x + cmd.sprite.src_w <= r->atlas_x + r->width &&
             cmd.sprite.src_y + cmd.sprite.src_h <= r->atlas_y + r->height;
    };

    const gfx::SheetRect* miss_item = linker.MissingSheet(gfx::SpriteType::kItem);
    const gfx::SheetRect* miss_ent =
        linker.MissingSheet(gfx::SpriteType::kEntity);
    const gfx::SheetRect* miss_tile = linker.MissingSheet(gfx::SpriteType::kTile);
    Check(miss_item && miss_ent && miss_tile, "Missing fallback sheets exist");

    DummyRenderBackend sprite_backend;
    gfx::Screen s_scr(&linker);

    // 6a. Every item template must resolve to a valid non-missing sprite sheet.
    const auto& all_items = item::Items::GetAllTemplates();
    Check(!all_items.empty(), "Items registry non-empty");
    for (const auto& it : all_items) {
      if (!it) continue;
      s_scr.Clear(0);
      it->RenderHud(&s_scr, 80, gfx::Screen::h - 8, gfx::color::kWhite);
      s_scr.Flush(&sprite_backend, false);
      if (it->Kind() == item::ItemKind::kPowerGlove) {
        for (const auto& c : sprite_backend.last_commands) {
          if (c.type == platform::DrawCmdType::kSprite) {
            Check(!cmd_in_rect(c, miss_item),
                  "PowerGlove does not render missing_item");
          }
        }
        continue;
      }
      const gfx::SheetRect* r = it->sprite.GetSheet(linker);
      Check(r != nullptr && r->valid,
            ("Item sheet exists: " + it->Name()).c_str());
      for (const auto& c : sprite_backend.last_commands) {
        if (c.type == platform::DrawCmdType::kSprite) {
          Check(!cmd_in_rect(c, miss_item),
                ("Item does not use missing_item: " + it->Name()).c_str());
        }
      }
    }

    // 6b. Every registered tile renders without hitting missing_tile.
    level::Level tile_lvl(8, 8, 1, 0, nullptr, false);
    for (uint16_t id = 0; id < 256; ++id) {
      if (id == level::Tiles::kConnectTile || !level::Tiles::ContainsTile(id)) {
        continue;
      }
      tile_lvl.SetTile(4, 4, id, 0);
      s_scr.Clear(0);
      level::Tiles::Get(id)->Render(&s_scr, tile_lvl, 4, 4);
      s_scr.Flush(&sprite_backend, false);
      for (const auto& c : sprite_backend.last_commands) {
        if (c.type == platform::DrawCmdType::kSprite) {
          Check(!cmd_in_rect(c, miss_tile),
                ("Tile does not use missing_tile: id=" + std::to_string(id))
                    .c_str());
        }
      }
    }

    // 6c. Player starts without PowerGlove and renders inside default_skins.
    {
      entity::Player p(nullptr, &input);
      p.x = 64;
      p.y = 64;
      Check(p.active_item == nullptr,
            "Player active_item starts null (no PowerGlove checkerboard)");
      const gfx::SheetRect* skins_rect =
          linker.GetSheet(gfx::SpriteType::kMisc, "default_skins");
      Check(skins_rect && skins_rect->valid && skins_rect->width >= 64 &&
                skins_rect->height >= 128,
            "default_skins sheet present and valid");
      const auto dirs = {entity::Direction::kDown, entity::Direction::kUp,
                         entity::Direction::kLeft, entity::Direction::kRight};
      for (int32_t skin = 0; skin < 4; ++skin) {
        game::GameState::Instance().selected_skin_index = skin;
        for (bool carry : {false, true}) {
          p.active_item =
              carry ? std::shared_ptr<item::Item>(
                          item::Items::Get("Workbench").release())
                    : nullptr;
          for (auto d : dirs) {
            for (int32_t wd : {0, 8}) {
              p.dir = d;
              p.walk_dist = wd;
              s_scr.Clear(0);
              p.Render(&s_scr);
              s_scr.Flush(&sprite_backend, false);
              Check(!sprite_backend.last_commands.empty(),
                    "Player Render emitted sprite");
              const gfx::SheetRect* wb_rect =
                  linker.GetSheet(gfx::SpriteType::kEntity, "workbench");
              for (const auto& c : sprite_backend.last_commands) {
                if (c.type == platform::DrawCmdType::kSprite) {
                  Check(!cmd_in_rect(c, miss_ent),
                        "Player does not render missing_entity");
                  Check(cmd_in_rect(c, skins_rect) ||
                            (carry && cmd_in_rect(c, wb_rect)),
                        "Player frame within default_skins bounds");
                }
              }
            }
          }
        }
      }
      // Verify horizontal mirror swaps left/right 8x8 tiles (no inside-out split).
      p.active_item.reset();
      p.dir = entity::Direction::kDown;
      p.walk_dist = 8;  // mirror = 1
      s_scr.Clear(0);
      p.Render(&s_scr);
      s_scr.Flush(&sprite_backend, false);
      int32_t top_left_src_x = -1;
      int32_t top_right_src_x = -1;
      for (const auto& c : sprite_backend.last_commands) {
        if (c.type == platform::DrawCmdType::kSprite &&
            c.sprite.y == p.y - 11) {
          if (c.sprite.x == p.x - 8) top_left_src_x = c.sprite.src_x;
          if (c.sprite.x == p.x) top_right_src_x = c.sprite.src_x;
          Check((c.flags & platform::kFlagMirrorX) != 0,
                "Player mirrored frame sets kFlagMirrorX");
        }
      }
      Check(top_left_src_x == skins_rect->atlas_x + 8 &&
                top_right_src_x == skins_rect->atlas_x,
            "Player mirrored 2x2 swaps left and right tile columns");

      // Partial stamina spend must not trigger full-bar exhaustion delay.
      p.stamina = p.max_stamina;
      Check(p.PayStamina(1), "PayStamina(1) succeeds");
      Check(p.stamina == p.max_stamina - 1,
            "PayStamina(1) reduces stamina by 1 bolt");
      Check(p.StaminaRechargeDelay() == 0,
            "Partial stamina spend keeps StaminaRechargeDelay == 0");
      Check(p.PayStamina(p.max_stamina),
            "Exhausting remaining stamina succeeds");
      Check(p.stamina == 0, "Stamina reaches 0 after exhaustion");
      p.Tick();
      Check(p.StaminaRechargeDelay() == 39,
            "Depleted stamina enters 40-tick exhaustion delay (decremented to 39)");

      game::GameState::Instance().selected_skin_index = 0;
    }

    // 6d. Every Mob, Boat, Furniture, Particle & Projectile renders in bounds.
    auto check_entity_sheet = [&](const entity::Entity& e,
                                  const char* sheet_key,
                                  const std::string& label) {
      const gfx::SheetRect* r =
          linker.GetSheet(gfx::SpriteType::kEntity, sheet_key);
      Check(r != nullptr && r->valid,
            ("Entity sheet exists: " + std::string(sheet_key)).c_str());
      s_scr.Clear(0);
      e.Render(&s_scr);
      s_scr.Flush(&sprite_backend, false);
      Check(!sprite_backend.last_commands.empty(),
            ("Entity Render emitted commands: " + label).c_str());
      for (const auto& c : sprite_backend.last_commands) {
        if (c.type == platform::DrawCmdType::kSprite) {
          Check(!cmd_in_rect(c, miss_ent),
                ("No missing_entity for: " + label).c_str());
          Check(cmd_in_rect(c, r),
                ("Entity sprite within sheet bounds: " + label).c_str());
        }
      }
    };

    const auto dirs = {entity::Direction::kDown, entity::Direction::kUp,
                       entity::Direction::kLeft, entity::Direction::kRight};
    auto check_mob_all_frames = [&](const char* name, int32_t lvl,
                                    const char* sheet_key) {
      auto m = entity::Mob::CreateByName(name, lvl);
      Check(m != nullptr, ("CreateByName: " + std::string(name)).c_str());
      if (!m) return;
      for (auto d : dirs) {
        for (int32_t wd : {0, 8}) {
          m->dir = d;
          m->walk_dist = wd;
          check_entity_sheet(*m, sheet_key,
                             std::string(name) + " lvl=" + std::to_string(lvl));
        }
      }
    };

    check_mob_all_frames("Cow", 1, "cow");
    check_mob_all_frames("Pig", 1, "pig");
    {
      entity::Pig left_pig;
      left_pig.x = 64;
      left_pig.y = 64;
      left_pig.dir = entity::Direction::kLeft;
      left_pig.walk_dist = 0;  // xt = 4, mirror = 1
      const gfx::SheetRect* pig_rect =
          linker.GetSheet(gfx::SpriteType::kEntity, "pig");
      s_scr.Clear(0);
      left_pig.Render(&s_scr);
      s_scr.Flush(&sprite_backend, false);
      int32_t tl = -1, tr = -1;
      for (const auto& c : sprite_backend.last_commands) {
        if (c.type == platform::DrawCmdType::kSprite &&
            c.sprite.y == left_pig.y - 11) {
          if (c.sprite.x == left_pig.x - 8) tl = c.sprite.src_x;
          if (c.sprite.x == left_pig.x) tr = c.sprite.src_x;
          Check((c.flags & platform::kFlagMirrorX) != 0,
                "Mob mirrored frame sets kFlagMirrorX");
        }
      }
      Check(tl == pig_rect->atlas_x + 5 * 8 && tr == pig_rect->atlas_x + 4 * 8,
            "Mob mirrored 2x2 swaps left and right tile columns");
    }
    check_mob_all_frames("Sheep", 1, "sheep");
    {
      entity::Sheep shorn;
      shorn.cut = true;
      for (auto d : dirs) {
        shorn.dir = d;
        shorn.walk_dist = 8;
        check_entity_sheet(shorn, "sheep", "Sheep (cut)");
      }
    }
    for (int32_t lvl = 1; lvl <= 4; ++lvl) {
      check_mob_all_frames("Slime", lvl, "slime");
      check_mob_all_frames("Zombie", lvl, "zombie");
      check_mob_all_frames("Skeleton", lvl, "skeleton");
      check_mob_all_frames("Creeper", lvl, "creeper");
      check_mob_all_frames("Knight", lvl, "knight");
      check_mob_all_frames("Snake", lvl, "snake");
    }
    check_mob_all_frames("AirWizard", 1, "air_wizard");
    check_mob_all_frames("AirWizard", 2, "air_wizard");
    check_mob_all_frames("ObsidianKnight", 1, "obsidian_knight_armored");

    for (auto d : dirs) {
      entity::Boat boat(d);
      check_entity_sheet(boat, "boat", "Boat");
    }

    {
      entity::Composter comp;
      check_entity_sheet(comp, "composter", "Composter (empty)");
      comp.compost = 1;
      auto comp_filled = comp.Clone();
      check_entity_sheet(*comp_filled, "compostor_filled", "Composter (filled)");
      comp.compost = entity::Composter::kMaxCompost;
      auto comp_full = comp.Clone();
      check_entity_sheet(*comp_full, "compostor_full", "Composter (full)");
    }
    check_entity_sheet(entity::WaterParticle(32, 32), "splash_0",
                       "WaterParticle");
    check_entity_sheet(entity::SandParticle(32, 32), "sand_footsteps",
                       "SandParticle");
    check_entity_sheet(entity::SmashParticle(32, 32), "smash", "SmashParticle");
    check_entity_sheet(entity::FireParticle(32, 32), "spark", "FireParticle");
    check_entity_sheet(
        entity::Arrow(nullptr, 32, 32, entity::Direction::kRight, 3), "arrow",
        "Arrow");
  }

  // 7. WorldGenDisplay Layout & Visibility Regression
  {
    util::Localization::Instance().ChangeLanguage("en-us");
    io::Settings::Set("mode", "minicraft.settings.mode.survival");
    screen::WorldGenDisplay wg;
    Check(wg.Menus().size() == 1, "WorldGenDisplay has 1 menu");
    const auto* m = wg.Menus()[0].get();
    Check(m->GetNumOptions() == 11,
          "WorldGenDisplay includes nameHelp and all 11 entries");
    Check(m->GetEntry(0)->IsVisible() && m->GetEntry(0)->IsSelectable(),
          "WorldGenDisplay nameField visible & selectable");
    Check(!m->GetEntry(1)->IsVisible() && !m->GetEntry(1)->IsSelectable(),
          "WorldGenDisplay nameHelp hidden by default");
    Check(m->GetEntry(2)->IsVisible() && m->GetEntry(2)->IsSelectable(),
          "WorldGenDisplay mode visible & selectable");
    Check(!m->GetEntry(3)->IsVisible() && !m->GetEntry(3)->IsSelectable(),
          "WorldGenDisplay scoretime hidden when mode != score");
    Check(m->GetEntry(4)->IsVisible() && m->GetEntry(4)->IsSelectable(),
          "WorldGenDisplay create_world visible & selectable");

    Check(m->GetBounds().Left() == 48 && m->GetBounds().Top() == 48 &&
              m->GetBounds().Width() == 192 && m->GetBounds().Height() == 96,
          "WorldGenDisplay bounds match Java (48, 48, 192x96)");

    DummyRenderBackend wg_backend;
    gfx::Screen wg_screen(&linker);
    wg.Render(&wg_screen);
    wg_screen.Flush(&wg_backend, false);
    auto has_y = [&](int32_t y) {
      for (const auto& c : wg_backend.last_commands) {
        if (c.type == platform::DrawCmdType::kSprite && c.sprite.y == y) {
          return true;
        }
      }
      return false;
    };
    auto min_x_at_y = [&](int32_t y) {
      int32_t mx = 9999;
      for (const auto& c : wg_backend.last_commands) {
        if (c.type == platform::DrawCmdType::kSprite && c.sprite.y == y &&
            c.sprite.x < mx) {
          mx = c.sprite.x;
        }
      }
      return mx;
    };
    Check(has_y(48), "WorldGenDisplay renders title at y=48");
    Check(has_y(64) && !has_y(82) && has_y(100) && !has_y(118) && has_y(136),
          "WorldGenDisplay renders first 3 visible rows at y=64, 100, 136 (36px step)");
    Check(min_x_at_y(64) == 48 && min_x_at_y(100) == 64 && min_x_at_y(136) == 64,
          "WorldGenDisplay entries aligned at x=64 with '> ' cursor at x=48");

    io::Settings::Set("mode", "minicraft.settings.mode.score");
    Check(m->GetEntry(3)->IsVisible() && m->GetEntry(3)->IsSelectable(),
          "WorldGenDisplay scoretime becomes visible when mode == score");
    io::Settings::Set("mode", "minicraft.settings.mode.survival");
    Check(!m->GetEntry(3)->IsVisible() && !m->GetEntry(3)->IsSelectable(),
          "WorldGenDisplay scoretime hides again when mode == survival");
  }

  // 10. Pixel-exact entity and tile collision bounds (not tile-padded)
  {
    level::Level lvl(64, 64, 1, 0, nullptr, /*skip_gen=*/true);
    auto& cm = lvl.GetChunkManager();
    for (int32_t ty = 0; ty < 16; ++ty) {
      for (int32_t tx = 0; tx < 16; ++tx) {
        cm.SetTile(tx, ty, level::Tiles::kGrass, 0);
      }
    }
    for (int32_t cy = 0; cy <= 1; ++cy) {
      for (int32_t cx = 0; cx <= 1; ++cx) {
        cm.SetChunkStage(cx, cy, level::Chunk::kStageFinished);
      }
    }

    auto chest = std::make_shared<entity::Chest>();
    lvl.Add(chest, 88, 88, false);  // Chest xr=3, yr=3 -> x bounds [85..91]

    auto p = std::make_shared<entity::Player>(nullptr, &input);
    lvl.Add(p, 70, 88, false);  // Player xr=4, yr=3 -> x bounds [66..74]
    lvl.Tick(false);

    // Must not collide prematurely across tile padding (previously stopped ~x=67)
    for (int32_t step = 0; step < 15; ++step) {
      p->Move(1, 0);
    }
    Check(p->x == 80,
          "Player walks up to Chest pixel hitbox edge (x=80, x+xr=84 vs chest 85)");

    // Multi-pixel movement stops flush against blocking Chest
    p->x = 78;
    p->Move(5, 0);
    Check(p->x == 80, "Multi-pixel Move(5,0) stops flush against Chest at x=80");

    // Solid tile multi-pixel movement stops flush against tile edge (tile x=7 -> 112)
    lvl.SetTile(7, 5, level::Tiles::kRock, 0);
    p->x = 104;
    p->y = 88;
    p->Move(6, 0);
    Check(p->x == 107,
          "Multi-pixel Move(6,0) stops flush at rock tile edge (x=107, x+xr=111)");
  }

  // 11. Continuous-hurt sound throttling & distance filtering match Java
  {
    struct CountingAudioBackend final : public platform::IAudioBackend {
      int32_t monsterhurt_count = 0;
      int32_t playerhurt_count = 0;
      bool Init() override { return true; }
      void Shutdown() override {}
      bool LoadWavFromMemory(std::string_view, const uint8_t*, size_t) override {
        return true;
      }
      void UnloadAllSounds() override {}
      void PlayClip(std::string_view key) override {
        if (key == "monsterhurt") ++monsterhurt_count;
        if (key == "playerhurt") ++playerhurt_count;
      }
    } audio;

    const bool prev_sound = io::Settings::GetBool("sound");
    io::Settings::Set("sound", true);
    pak::PakReader empty_pak;
    io::Sound::Init(&audio, empty_pak);

    level::Level lvl(64, 64, 1, 0, nullptr, /*skip_gen=*/true);
    auto& cm = lvl.GetChunkManager();
    for (int32_t ty = 0; ty < 32; ++ty) {
      for (int32_t tx = 0; tx < 32; ++tx) {
        cm.SetTile(tx, ty, level::Tiles::kGrass, 0);
      }
    }
    for (int32_t cy = 0; cy <= 2; ++cy) {
      for (int32_t cx = 0; cx <= 2; ++cx) {
        cm.SetChunkStage(cx, cy, level::Chunk::kStageFinished);
      }
    }

    auto p = std::make_shared<entity::Player>(nullptr, &input);
    lvl.Add(p, 88, 88, false);

    auto near_slime = std::make_shared<entity::Slime>(1);
    near_slime->health = 100;
    lvl.Add(near_slime, 100, 88, false);  // 12px away (< 80px)

    auto far_slime = std::make_shared<entity::Slime>(1);
    far_slime->health = 100;
    lvl.Add(far_slime, 200, 88, false);  // 112px away (>= 80px)

    lvl.Tick(false);  // Flush pending entities into level/players_
    p->hurt_time = 0;
    p->health = 20;
    near_slime->x = 100;
    near_slime->y = 88;
    near_slime->hurt_time = 0;
    far_slime->x = 200;
    far_slime->y = 88;
    far_slime->hurt_time = 0;

    // Repeated hurt calls across 10 ticks while hurt_time > 0 must play monsterhurt once.
    for (int32_t t = 0; t < 10; ++t) {
      near_slime->DoHurt(1, entity::Direction::kDown);
    }
    Check(audio.monsterhurt_count == 1,
          "MobAi::DoHurt does not machine-gun monsterhurt while hurt_time > 0");

    // Distant mob beyond 5 tiles (80px) must stay silent.
    far_slime->DoHurt(1, entity::Direction::kDown);
    Check(audio.monsterhurt_count == 1,
          "MobAi::DoHurt does not play monsterhurt beyond 5 tiles");

    // Player hurt cooldown is 30 ticks (matching Java playerHurtTime=30).
    const size_t prev_mode = io::Settings::GetIdx("mode");
    io::Settings::SetIdx("mode", 0);  // Survival
    p->DoHurt(1, entity::Direction::kDown);
    Check(p->hurt_time == 30, "Player::DoHurt sets hurt_time == 30 ticks");
    for (int32_t t = 0; t < 29; ++t) {
      p->Tick();
      p->DoHurt(1, entity::Direction::kDown);
    }
    Check(audio.playerhurt_count == 1,
          "Player::DoHurt does not replay playerhurt during 30-tick cooldown");
    p->Tick();  // hurt_time reaches 0
    p->DoHurt(1, entity::Direction::kDown);
    Check(audio.playerhurt_count == 2,
          "Player::DoHurt plays playerhurt again once 30-tick cooldown finishes");

    io::Settings::SetIdx("mode", prev_mode);
    io::Sound::Shutdown();
    io::Settings::Set("sound", prev_sound);
  }

  // 12. Entity pushing weight & cooldown match Java (1px per push + delay ticks)
  {
    level::Level lvl(64, 64, 1, 0, nullptr, /*skip_gen=*/true);
    auto& cm = lvl.GetChunkManager();
    for (int32_t ty = 0; ty < 16; ++ty) {
      for (int32_t tx = 0; tx < 16; ++tx) {
        cm.SetTile(tx, ty, level::Tiles::kGrass, 0);
      }
    }
    cm.SetTile(10, 5, level::Tiles::kWater, 0);
    for (int32_t cy = 0; cy <= 1; ++cy) {
      for (int32_t cx = 0; cx <= 1; ++cx) {
        cm.SetChunkStage(cx, cy, level::Chunk::kStageFinished);
      }
    }

    auto p = std::make_shared<entity::Player>(nullptr, &input);
    p->dir = entity::Direction::kRight;
    lvl.Add(p, 70, 88, false);

    auto chest = std::make_shared<entity::Chest>();
    lvl.Add(chest, 88, 88, false);

    auto statue = std::make_shared<entity::KnightStatue>(500);
    lvl.Add(statue, 110, 88, false);

    auto locked_chest = std::make_shared<entity::DungeonChest>(false, true);
    lvl.Add(locked_chest, 130, 88, false);

    auto boat = std::make_shared<entity::Boat>(entity::Direction::kRight);
    lvl.Add(boat, 150, 88, false);  // Land tile (x=150 -> tile 9)

    lvl.Tick(false);
    const int32_t chest_start_x = chest->x;
    const int32_t statue_start_x = statue->x;
    const int32_t locked_start_x = locked_chest->x;

    // Single push moves Chest 1px, and 10 ticks of cooldown block another push until tick 10.
    chest->TouchedBy(*p);
    chest->Tick();
    Check(chest->x == chest_start_x + 1,
          "Furniture moves exactly 1px on push (not 10px ice-slide)");
    for (int32_t t = 0; t < 9; ++t) {
      chest->TouchedBy(*p);
      chest->Tick();
    }
    Check(chest->x == chest_start_x + 1,
          "Furniture ignores pushes during remaining 9 cooldown ticks");
    chest->TouchedBy(*p);  // Now push_time_ == 0 -> accepts next push
    chest->Tick();
    Check(chest->x == chest_start_x + 2,
          "Furniture accepts next 1px push once 10-tick cooldown finishes");

    // Unpushable furniture
    statue->TouchedBy(*p);
    statue->Tick();
    Check(statue->x == statue_start_x, "KnightStatue cannot be pushed");

    locked_chest->TouchedBy(*p);
    locked_chest->Tick();
    Check(locked_chest->x == locked_start_x,
          "Locked DungeonChest cannot be pushed");

    // Boat push weight: 6 ticks on land, 2 ticks in water
    Check(boat->GetPushTimeDelay() == 6,
          "Boat push delay on land matches Java (6 ticks)");
    boat->TouchedBy(*p);
    boat->Tick();
    Check(boat->x == 151, "Boat moves 1px when pushed on land");
    boat->x = 168;  // Tile 10 (Water)
    Check(boat->GetPushTimeDelay() == 2,
          "Boat push delay in water matches Java (2 ticks)");
  }

  // 13. Java-matched mob cap, monster_density crowding radius, and torch light exclusion
  {
    const size_t prev_diff = io::Settings::GetIdx("diff");
    const size_t prev_mode = io::Settings::GetIdx("mode");
    io::Settings::SetIdx("diff", 1);  // Normal -> base 300
    io::Settings::SetIdx("mode", 0);  // Survival

    level::Level surface(128, 128, 1, 0, nullptr, /*skip_gen=*/true);
    level::Level cave(128, 128, 1, -1, nullptr, /*skip_gen=*/true);
    level::Level sky(128, 128, 1, 1, nullptr, /*skip_gen=*/true);

    Check(surface.max_mob_count == 200, "Surface Normal max_mob_count == 200");
    Check(surface.monster_density == 16, "Surface monster_density == 16");
    Check(cave.max_mob_count == 300, "Cave Normal max_mob_count == 300");
    Check(cave.monster_density == 8, "Cave monster_density == 8");
    Check(sky.max_mob_count == 150, "Sky Normal max_mob_count == 150");

    auto& cm = surface.GetChunkManager();
    for (int32_t ty = 0; ty < 64; ++ty) {
      for (int32_t tx = 0; tx < 64; ++tx) {
        cm.SetTile(tx, ty, level::Tiles::kGrass, 0);
      }
    }
    for (int32_t cy = 0; cy <= 1; ++cy) {
      for (int32_t cx = 0; cx <= 1; ++cx) {
        cm.SetChunkStage(cx, cy, level::Chunk::kStageFinished);
      }
    }

    auto p = std::make_shared<entity::Player>(nullptr, &input);
    surface.Add(p, 80, 80, false);
    surface.Tick(false);

    auto& gs = game::GameState::Instance();
    const auto prev_tod = gs.GetTimeOfDay();
    gs.SetTimeOfDay(game::TimeOfDay::kDay);

    // PassiveMob solo_radius * monster_density = 20 * 16 = 320px -> blocks at 200px, allows at 500px
    Check(!entity::PassiveMob::CheckStartPos(surface, 280, 80),
          "PassiveMob::CheckStartPos blocks crowded spawn within 320px");
    Check(entity::PassiveMob::CheckStartPos(surface, 580, 80),
          "PassiveMob::CheckStartPos allows isolated Grass spawn at 500px");
    surface.SetTile(36, 5, level::Tiles::kDirt, 0);  // 580 >> 4 == 36, 80 >> 4 == 5
    Check(!entity::PassiveMob::CheckStartPos(surface, 580, 80),
          "PassiveMob::CheckStartPos blocks Dirt tile (Java: only Grass/Flower)");
    surface.SetTile(36, 5, level::Tiles::kGrass, 0);

    // Torch blocks EnemyMob spawn within 3 tiles, allows unlit dark tile
    surface.SetTile(36, 5, level::Tiles::kTorch, 0);
    Check(!entity::EnemyMob::CheckStartPos(surface, 592, 80),
          "EnemyMob::CheckStartPos blocks lit tile near Torch");
    surface.SetTile(36, 5, level::Tiles::kGrass, 0);
    Check(entity::EnemyMob::CheckStartPos(surface, 592, 80),
          "EnemyMob::CheckStartPos allows dark uncrowded tile");

    gs.SetTimeOfDay(prev_tod);
    io::Settings::SetIdx("diff", prev_diff);
    io::Settings::SetIdx("mode", prev_mode);
  }

  // 13. ResourcePackDisplay: built-in Default pack is always shown last, enabled, and locked
  {
    auto& host = screen::DisplayHost::Instance();
    const auto prev_fs = host.fs;
    const auto prev_pak = host.pak;
    const auto prev_game_dir = host.game_dir;
    const auto prev_active = host.active_resource_packs;
    const auto prev_input = host.input;
    const auto prev_reload = host.reload_resource_packs;

    const std::string rp_game_dir = temp_dir + "/rp_test";
    const std::string rp_root = rp_game_dir + "/resourcepacks";
    fs.CreateDirectories(rp_root);

    int32_t reload_calls = 0;
    host.fs = &fs;
    host.pak = &pak;
    host.game_dir = rp_game_dir;
    host.active_resource_packs.clear();
    host.input = &input;
    host.reload_resource_packs = [&reload_calls] { ++reload_calls; };

    // Empty resourcepacks/ directory: Default must still appear as a locked enabled pack.
    screen::ResourcePackDisplay empty_rp;
    Check(empty_rp.Packs().size() == 1,
          "ResourcePackDisplay shows Default when resourcepacks/ is empty");
    if (!empty_rp.Packs().empty()) {
      Check(empty_rp.Packs()[0].name == "Default", "Default pack name matches");
      Check(empty_rp.Packs()[0].enabled && empty_rp.Packs()[0].locked,
            "Default pack is enabled and locked");
    }
    Check(empty_rp.Menus().size() == 2,
          "ResourcePackDisplay uses 2-column indicator + entry menus");

    // Trying to toggle Default must do nothing.
    input.ReleaseAll();
    input.PushKeyEvents({{"ENTER", true}});
    input.Tick();
    empty_rp.Tick(input);
    input.PushKeyEvents({{"ENTER", false}});
    input.Tick();
    Check(host.active_resource_packs.empty(),
          "Toggling locked Default leaves active_resource_packs empty");
    Check(reload_calls == 0, "Toggling locked Default does not trigger reload");

    // With custom packs present: custom packs appear above locked Default.
    fs.CreateDirectories(rp_root + "/alpha_pack");
    fs.WriteTextFile(rp_root + "/alpha_pack/pack.json",
                     R"({"name":"Alpha Pack","description":"First custom pack"})");
    fs.CreateDirectories(rp_root + "/beta_pack");

    screen::ResourcePackDisplay custom_rp;
    Check(custom_rp.Packs().size() == 3,
          "ResourcePackDisplay lists 2 custom packs + Default");
    if (custom_rp.Packs().size() == 3) {
      Check(custom_rp.Packs()[0].name == "Alpha Pack",
            "Reads pack.json name for custom pack");
      Check(custom_rp.Packs()[1].name == "beta_pack",
            "Falls back to folder name without pack.json");
      Check(custom_rp.Packs()[2].name == "Default" &&
                custom_rp.Packs()[2].locked && custom_rp.Packs()[2].enabled,
            "Default stays locked and enabled at bottom of custom list");
    }

    // Selecting first custom pack toggles it active without touching Default.
    input.PushKeyEvents({{"ENTER", true}});
    input.Tick();
    custom_rp.Tick(input);
    input.PushKeyEvents({{"ENTER", false}});
    input.Tick();
    Check(host.active_resource_packs.size() == 1 &&
              host.active_resource_packs[0] == "alpha_pack",
          "Selecting custom pack enables it in active_resource_packs");

    // Cursor-right swaps custom packs above Default; Default remains at bottom.
    input.PushKeyEvents({{"RIGHT", true}});
    input.Tick();
    custom_rp.Tick(input);
    input.PushKeyEvents({{"RIGHT", false}});
    input.Tick();
    Check(custom_rp.Packs().size() == 3 &&
              custom_rp.Packs()[0].folder == "beta_pack" &&
              custom_rp.Packs()[1].folder == "alpha_pack" &&
              custom_rp.Packs()[2].name == "Default",
          "Reordering custom packs keeps Default locked at the end");

    // Cursor-down past last custom pack skips locked Default.
    input.PushKeyEvents({{"DOWN", true}});
    input.Tick();
    custom_rp.Tick(input);
    input.PushKeyEvents({{"DOWN", false}});
    input.Tick();
    Check(custom_rp.Menus()[1]->GetSelection() == 0,
          "Cursor skips locked Default entry when custom packs exist");

    host.fs = prev_fs;
    host.pak = prev_pak;
    host.game_dir = prev_game_dir;
    host.active_resource_packs = prev_active;
    host.input = prev_input;
    host.reload_resource_packs = prev_reload;
  }

  // 14. Java-matched entity AI behaviors (walk_time, Creative/sleep ignore,
  //     Creeper, Skeleton, AirWizard, ObsidianKnight, Time potion & despawn)
  {
    auto& gs = game::GameState::Instance();
    const int32_t prev_mode = io::Settings::GetIdx("mode");
    const bool prev_sleep = gs.bed_sleeping;
    const int32_t prev_gt = gs.game_time;
    const bool prev_summoned = gs.obsidian_knight_summoned;

    io::Settings::SetIdx("mode", 0);  // Survival
    gs.bed_sleeping = false;
    gs.game_time = 55000;  // Night

    level::Level lvl(64, 64, 1, 0, nullptr, /*skip_gen=*/true);
    auto& cm = lvl.GetChunkManager();
    for (int32_t ty = 0; ty < 64; ++ty) {
      for (int32_t tx = 0; tx < 64; ++tx) {
        cm.SetTile(tx, ty, level::Tiles::kGrass, 0);
      }
    }
    for (int32_t cy = 0; cy <= 1; ++cy) {
      for (int32_t cx = 0; cx <= 1; ++cx) {
        cm.SetChunkStage(cx, cy, level::Chunk::kStageFinished);
      }
    }

    auto p = std::make_shared<entity::Player>(nullptr, &input);
    lvl.Add(p, 160, 160, false);

    auto zombie = std::make_shared<entity::Zombie>(1);
    lvl.Add(zombie, 120, 160, false);
    Check(zombie->WalkTime() == 2, "MobAi sets walk_time = 2 (half player speed)");

    // Tick Zombie multiple frames and ensure it advances roughly half speed (not 1px/tick)
    const int32_t z_start_x = zombie->x;
    for (int32_t t = 0; t < 10; ++t) {
      lvl.Tick(false);  // flush first
      zombie->Tick();
    }
    const int32_t z_moved = zombie->x - z_start_x;
    Check(z_moved > 0 && z_moved <= 5,
          "Zombie walks at half speed (<= 5px in 10 ticks via walk_time = 2)");

    // Creative mode prevents Creeper fuse and EnemyMob aggro
    io::Settings::SetIdx("mode", 1);  // Creative
    auto creeper = std::make_shared<entity::Creeper>(1);
    lvl.Add(creeper, 162, 160, false);
    lvl.Tick(false);
    creeper->TouchedBy(*p);
    Check(!creeper->IsFuseLit(), "Creeper does not light fuse in Creative mode");

    // In Survival, touching Creeper lights 60-tick fuse and stops Creeper movement
    io::Settings::SetIdx("mode", 0);  // Survival
    creeper->TouchedBy(*p);
    Check(creeper->IsFuseLit() && creeper->FuseTime() == 60,
          "Creeper lights 60-tick fuse when touching Survival player");
    const int32_t cx_lit = creeper->x;
    creeper->Tick();
    Check(creeper->x == cx_lit && creeper->FuseTime() == 59,
          "Creeper halts movement while fuse burns down");

    // Creeper defuses if player moves outside 64px before detonation
    p->x = 400;
    for (int32_t t = 0; t < 60; ++t) creeper->Tick();
    Check(!creeper->removed && !creeper->IsFuseLit(),
          "Creeper defuses if player exits 64px trigger radius");
    p->x = 160;

    // AirWizard retreats when player is within 2 tiles (< 32px) and hurt sets attack_delay = 120
    auto wizard = std::make_shared<entity::AirWizard>();
    lvl.Add(wizard, 180, 160, false);  // 20px right of player
    lvl.Tick(false);
    const int32_t wx_before = wizard->x;
    for (int32_t t = 0; t < 4; ++t) wizard->Tick();
    Check(wizard->x >= wx_before,
          "AirWizard backs away or holds distance when player < 32px");
    auto wizard_far = std::make_shared<entity::AirWizard>();
    lvl.Add(wizard_far, 220, 160, false);  // 60px away (>= 50px) so random attack doesn't pre-trigger
    lvl.Tick(false);
    wizard_far->Tick();
    wizard_far->DoHurt(1, entity::Direction::kRight);
    Check(wizard_far->AttackDelay() == 120,
          "AirWizard hurt sets 120-tick attack telegraph delay");

    // ObsidianKnight: walk_time = 3, breaks armor at <= 50% HP, reverts to KnightStatue if no player
    level::Level boss_lvl(64, 64, 1, -4, nullptr, /*skip_gen=*/true);
    for (int32_t ty = 0; ty < 32; ++ty) {
      for (int32_t tx = 0; tx < 32; ++tx) {
        boss_lvl.GetChunkManager().SetTile(tx, ty, level::Tiles::kObsidian, 0);
      }
    }
    auto knight = std::make_shared<entity::ObsidianKnight>(500);
    Check(knight->WalkTime() == 3, "ObsidianKnight sets walk_time = 3");
    auto bp = std::make_shared<entity::Player>(nullptr, &input);
    boss_lvl.Add(bp, 160, 160, false);
    boss_lvl.Add(knight, 200, 160, false);
    boss_lvl.Tick(false);
    knight->health = 200;  // <= 50% of 500
    knight->Tick();
    Check(knight->Phase() == 1, "ObsidianKnight enters phase 1 at <= 50% HP");

    // Removing player causes ObsidianKnight to revert back into KnightStatue
    bp->Remove();
    boss_lvl.PurgeRemovedEntities();
    knight->Tick();
    boss_lvl.Tick(false);
    bool found_statue = false;
    for (const auto& e : boss_lvl.Entities()) {
      if (e && !e->removed && e->GetEntityTypeName() == "KnightStatue") {
        found_statue = true;
      }
    }
    Check(knight->removed && found_statue,
          "ObsidianKnight reverts to KnightStatue when no player remains");

    gs.bed_sleeping = prev_sleep;
    gs.game_time = prev_gt;
    gs.obsidian_knight_summoned = prev_summoned;
    io::Settings::SetIdx("mode", prev_mode);
  }

  // 15. Non-mob entity parity with Java (Arrow, Spark/FireSpark, Boat, DeathChest, Tnt, Composter, Spawner)
  {
    auto& gs = game::GameState::Instance();
    const size_t prev_diff = io::Settings::GetIdx("diff");
    const size_t prev_mode = io::Settings::GetIdx("mode");
    const int32_t prev_gt = gs.game_time;

    io::Settings::SetIdx("diff", 1);  // Normal
    io::Settings::SetIdx("mode", 0);  // Survival

    level::Level lvl(64, 64, 1, 0, nullptr, /*skip_gen=*/true);
    for (int32_t ty = 0; ty < 32; ++ty) {
      for (int32_t tx = 0; tx < 32; ++tx) {
        lvl.GetChunkManager().SetTile(tx, ty, level::Tiles::kGrass, 0);
      }
    }
    // Water strip at tx=11..15, ty=10; Lava at tx=16, ty=10; Rock wall at tx=18, ty=10
    for (int32_t tx = 11; tx <= 15; ++tx) {
      lvl.GetChunkManager().SetTile(tx, 10, level::Tiles::kWater, 0);
    }
    lvl.GetChunkManager().SetTile(16, 10, level::Tiles::kLava, 0);
    lvl.GetChunkManager().SetTile(18, 10, level::Tiles::kRock, 0);

    io::InputHandler input;
    auto p = std::make_shared<entity::Player>(nullptr, &input);
    lvl.Add(p, 160, 160, false);  // Tile (10, 10)
    lvl.Tick(false);

    // Arrow: Java speed (7 for dmg 0..3, 8 for dmg > 3), hitbox, flies across water
    auto skel = std::make_shared<entity::Skeleton>(1);
    lvl.Add(skel, 168, 168, false);
    auto arrow_mid = std::make_shared<entity::Arrow>(
        skel.get(), 170, 168, entity::Direction::kRight, 2);
    Check(arrow_mid->xr == 2 && arrow_mid->yr == 1 && arrow_mid->CanSwim(),
          "Arrow uses directional hitbox (2,1) and CanSwim()==true");
    lvl.Add(arrow_mid, 170, 168, false);
    arrow_mid->Tick();
    Check(arrow_mid->x == 177 && !arrow_mid->removed,
          "Arrow (dmg=2) travels 7px/tick and crosses water without despawning");
    for (int32_t t = 0; t < 5; ++t) arrow_mid->Tick();
    Check(!arrow_mid->removed, "Arrow continues across water corridor");

    auto arrow_fast = std::make_shared<entity::Arrow>(
        skel.get(), 170, 168, entity::Direction::kRight, 5);
    lvl.Add(arrow_fast, 170, 168, false);
    arrow_fast->Tick();
    Check(arrow_fast->x == 178, "Arrow (dmg>3) travels at Java speed 8px/tick");

    // Spark & FireSpark: Java lifetimes, FireSpark wall stop + burn(5)
    auto wizard = std::make_shared<entity::AirWizard>();
    lvl.Add(wizard, 80, 80, false);
    entity::Spark spark(wizard.get(), 1.0, 0.0);
    Check(spark.LifeTime() >= 360 && spark.LifeTime() <= 389,
          "Spark lifetime matches Java (360..389 ticks)");

    auto boss = std::make_shared<entity::ObsidianKnight>(500);
    lvl.Add(boss, 160, 160, false);
    auto fspark = std::make_shared<entity::FireSpark>(boss.get(), 1.0, 0.0);
    Check(fspark->LifeTime() >= 180 && fspark->LifeTime() <= 199,
          "FireSpark lifetime matches Java (180..199 ticks)");
    lvl.Add(fspark, 160, 160, false);
    fspark->Tick();
    Check(p->burning_duration == 300 && !fspark->removed,
          "FireSpark applies burn(5) = 300 ticks instead of self-removing");
    p->burning_duration = 0;

    // Place FireSpark right against Rock wall at tx=18 (x=288)
    auto fspark_wall = std::make_shared<entity::FireSpark>(boss.get(), 4.0, 0.0);
    lvl.Add(fspark_wall, 287, 168, false);
    lvl.Tick(false);
    fspark_wall->Tick();
    Check(fspark_wall->IsStopped(),
          "FireSpark stops when hitting solid rock tile");
    for (int32_t t = 0; t < 49; ++t) fspark_wall->Tick();
    Check(!fspark_wall->removed, "FireSpark remains 49 stopped ticks");
    fspark_wall->Tick();
    Check(fspark_wall->removed, "FireSpark removes after 50 stopped ticks");

    // Boat: (6,6) hitbox, MOVE_SPEED=1 + stamina drain per 45 moves, land 1/4 throttle, lava break
    auto boat = std::make_shared<entity::Boat>(entity::Direction::kRight);
    Check(boat->xr == 6 && boat->yr == 6, "Boat uses Java (6,6) hitbox");
    lvl.Add(boat, 184, 168, false);  // Water tile (11, 10)
    Check(boat->Use(*p), "Player boards Boat");
    p->stamina = 10;
    const int32_t boat_x0 = boat->x;
    for (int32_t t = 0; t < 46; ++t) {
      boat->Tick();
      boat->Drive(1, 0, entity::Direction::kRight);
    }
    Check(boat->x > boat_x0 && boat->x <= boat_x0 + 46,
          "Boat moves at Java MOVE_SPEED=1 in water");
    Check(p->stamina < 10, "Boat consumes player stamina every 45 move ticks");
    p->stamina = 0;
    const int32_t boat_x_exhausted = boat->x;
    boat->Tick();
    boat->Drive(1, 0, entity::Direction::kRight);
    Check(boat->x == boat_x_exhausted, "Boat stops moving when player stamina == 0");

    // Land throttle: move boat to grass tile (10,10)
    p->stamina = 10;
    boat->x = 160;
    boat->y = 160;
    const int32_t land_start_x = boat->x;
    for (int32_t t = 0; t < 8; ++t) {
      boat->Tick();
      boat->Drive(1, 0, entity::Direction::kRight);
    }
    Check(boat->x == land_start_x + 2,
          "Boat moves on land only every 4th tick (8 ticks -> 2px)");

    // Lava destroys boat on tick and drops Boat item
    boat->x = 16 * 16 + 8;
    boat->y = 10 * 16 + 8;
    boat->Tick();
    Check(boat->removed && p->GetBoat() == nullptr,
          "Boat breaks and dismounts rider on lava tile");

    // DeathChest: difficulty-based time, Use()==false, TouchedBy() retrieves items
    io::Settings::SetIdx("diff", 0);  // Easy -> 450s
    entity::DeathChest dc_easy;
    Check(dc_easy.Time() == 450 * game::GameState::kNormSpeed,
          "DeathChest Easy timer matches Java (450s)");
    io::Settings::SetIdx("diff", 2);  // Hard -> 150s
    entity::DeathChest dc_hard;
    Check(dc_hard.Time() == 150 * game::GameState::kNormSpeed,
          "DeathChest Hard timer matches Java (150s)");
    io::Settings::SetIdx("diff", 1);  // Normal -> 300s
    p->GetInventory().ClearInv();
    p->AddItemToInventory(item::Items::Get("Wood"));
    auto dc = std::make_shared<entity::DeathChest>(*p);
    Check(dc->Time() == 300 * game::GameState::kNormSpeed &&
              p->GetInventory().InvSize() == 0,
          "DeathChest Normal timer is 300s and empties player inventory");
    lvl.Add(dc, 160, 160, false);
    Check(!dc->Use(*p), "DeathChest::Use returns false (cannot open chest GUI)");
    dc->TouchedBy(*p);
    Check(dc->removed && p->GetInventory().InvSize() == 1,
          "DeathChest::TouchedBy retrieves items into player inventory and removes chest");

    // Tnt: Hurt() doesn't ignite, Interact() sets fuse, 90-tick fuse + chain reaction (60)
    auto tnt1 = std::make_shared<entity::Tnt>();
    auto tnt2 = std::make_shared<entity::Tnt>();
    lvl.Add(tnt1, 200, 200, false);
    lvl.Add(tnt2, 212, 200, false);  // Within 32px blast radius
    lvl.Tick(false);
    tnt1->Hurt(*p, 5, entity::Direction::kDown);
    Check(!tnt1->fuse_lit, "Tnt::Hurt does not light fuse in Java");
    Check(tnt1->Interact(*p, nullptr, entity::Direction::kDown) && tnt1->fuse_lit,
          "Tnt::Interact lights fuse");
    for (int32_t t = 0; t < 89; ++t) tnt1->Tick();
    Check(!tnt1->removed, "Tnt has not exploded at 89 ticks");
    tnt1->Tick();
    Check(tnt1->removed, "Tnt explodes at tick 90 (FUSE_TIME=90)");
    Check(tnt2->fuse_lit && tnt2->Ftik() == 60,
          "Chained Tnt lights with ftik = 60 (90 * 2 / 3)");
    const auto* demo_ach =
        util::AchievementManager::Instance().Find("minicraft.achievement.demolition");
    Check(demo_ach != nullptr && demo_ach->unlocked,
          "Tnt explosion unlocks demolition achievement");

    // Composter: kMaxCompost=7, Bread 100%, full Interact drops Fertilizer
    Check(entity::Composter::kMaxCompost == 7, "Composter kMaxCompost matches Java (7)");
    auto comp = std::make_shared<entity::Composter>();
    lvl.Add(comp, 128, 128, false);
    auto bread = item::Items::Get("Bread");
    auto* bread_st = dynamic_cast<item::StackableItem*>(bread.get());
    bread_st->count = 7;
    for (int32_t i = 0; i < 7; ++i) {
      Check(comp->Interact(*p, bread.get(), entity::Direction::kDown),
            "Composter accepts Bread 100% of the time");
    }
    Check(comp->compost == 7 && bread_st->count == 0,
          "Composter reaches max compost 7 and consumes 7 Bread");
    Check(comp->Interact(*p, nullptr, entity::Direction::kDown) &&
              comp->compost == 0,
          "Full Composter drops Fertilizer and resets compost to 0");

    // Spawner: daytime surface block, active radius 128px, dark night spawn
    gs.game_time = 20000;  // Daytime (> 8100 and < 40500)
    auto spawner = std::make_shared<entity::Spawner>("Zombie", 1);
    lvl.Add(spawner, 96, 96, false);
    p->x = 96;
    p->y = 96;
    lvl.Tick(false);
    const int32_t mobs_before_day = lvl.mob_count;
    spawner->SetSpawnTick(1);
    spawner->Tick();
    Check(lvl.mob_count == mobs_before_day,
          "Enemy Spawner blocks spawning on surface during daytime");

    gs.game_time = 56000;  // Night
    p->x = 400;            // > 128px away from (96, 96)
    spawner->SetSpawnTick(1);
    spawner->Tick();
    Check(lvl.mob_count == mobs_before_day,
          "Spawner blocks spawning when closest player is outside 128px radius");

    p->x = 112;
    p->y = 96;  // Within 128px
    spawner->SetSpawnTick(1);
    spawner->Tick();
    lvl.Tick(false);
    bool spawned_zombie = false;
    for (const auto& e : lvl.Entities()) {
      if (e && !e->removed && e->GetEntityTypeName() == "Zombie") {
        spawned_zombie = true;
      }
    }
    Check(spawned_zombie,
          "Enemy Spawner spawns Zombie on surface at night when player is within 128px");

    gs.game_time = prev_gt;
    io::Settings::SetIdx("diff", prev_diff);
    io::Settings::SetIdx("mode", prev_mode);
  }

  // 16. GFX parity checks (lightness, white_tint priority, newline wrapping, day quarters)
  {
    std::cout << "[16/16] Testing GFX parity fixes...\n";
    Check(gfx::color::GetLightnessFromRgb(0x808080) >= 0.5f,
          "Color lightness >= 0.5 for mid-light gray (0x808080)");
    Check(gfx::color::GetLightnessFromRgb(0x404040) < 0.5f,
          "Color lightness < 0.5 for dark gray (0x404040)");

    gfx::Screen test_screen(nullptr);
    DummyRenderBackend backend;
    gfx::SheetRect sheet{};
    sheet.width = 16;
    sheet.height = 16;
    sheet.valid = true;
    test_screen.RenderSub(10, 20, 0, 0, 8, 8, &sheet, 0, 0xFF0000, true,
                          0x00FF00);
    test_screen.RenderSub(20, 20, 0, 0, 8, 8, &sheet, 0, 0xFF0000, false,
                          0x00FF00);
    test_screen.Flush(&backend);
    Check(backend.last_commands.size() == 2,
          "Screen::RenderSub emits 2 DrawCmds");
    if (backend.last_commands.size() == 2) {
      const auto& cmd0 = backend.last_commands[0];
      Check((cmd0.flags & platform::kFlagUseWhiteTint) != 0 &&
                (cmd0.flags & platform::kFlagFullBright) != 0 &&
                (cmd0.flags & platform::kFlagUseSolidColor) == 0,
            "RenderSub preserves white_tint alongside fullbright (and fullbright takes priority over solid_color)");
      Check(cmd0.sprite.tint_rgba == gfx::color::ToRgba(0xFF0000),
            "RenderSub stores white_tint in tint_rgba with fullbright");

      const auto& cmd1 = backend.last_commands[1];
      Check((cmd1.flags & platform::kFlagUseWhiteTint) != 0 &&
                (cmd1.flags & platform::kFlagUseSolidColor) != 0 &&
                (cmd1.flags & platform::kFlagFullBright) == 0,
            "RenderSub preserves white_tint alongside solid_color");
      Check(cmd1.sprite.tint_rgba == gfx::color::ToRgba(0xFF0000),
            "RenderSub stores white_tint in tint_rgba with solid_color");
      Check(cmd1.sprite.color_rgba == gfx::color::ToRgba(0x00FF00),
            "RenderSub stores solid_color in color_rgba");
    }

    // Verify POD anonymous union arms and strongly-typed gfx::Axis commands
    {
      test_screen.Clear(0);
      test_screen.DrawAxisLine(10, 12, gfx::Axis::kHorizontal, 25, 0x112233);
      test_screen.DrawAxisLine(10, 12, gfx::Axis::kVertical, 30, 0x445566);
      test_screen.DrawLineSpecial(5, 7, gfx::Axis::kHorizontal, 8);
      test_screen.DrawLineSpecial(5, 7, gfx::Axis::kVertical, 8);
      test_screen.Overlay(3, 14, 18, 80.0);
      test_screen.Flush(&backend);
      Check(backend.last_commands.size() == 5 &&
                backend.last_commands[0].type == platform::DrawCmdType::kDrawLine &&
                backend.last_commands[0].line.x0 == 10 &&
                backend.last_commands[0].line.y0 == 12 &&
                backend.last_commands[0].line.x1 == 35 &&
                backend.last_commands[0].line.y1 == 12 &&
                backend.last_commands[1].line.x1 == 10 &&
                backend.last_commands[1].line.y1 == 42 &&
                backend.last_commands[2].type == platform::DrawCmdType::kInvertLine &&
                backend.last_commands[2].rect.w == 8 &&
                backend.last_commands[2].rect.h == 1 &&
                backend.last_commands[3].rect.w == 1 &&
                backend.last_commands[3].rect.h == 8 &&
                backend.last_commands[4].type ==
                    platform::DrawCmdType::kApplyLightOverlay &&
                backend.last_commands[4].overlay.offset_x == 14 &&
                backend.last_commands[4].overlay.offset_y == 18 &&
                std::abs(backend.last_commands[4].overlay.dark_alpha - 0.5f) <
                    1e-5f,
            "DrawCmd POD union arms and gfx::Axis lines emit clean typed payloads");
    }

    // Verify SpriteAnimation Rule of 5 move semantics & px_at bounds safety on undersized/missing border sheets
    {
      gfx::SpriteLinker anim_linker;
      anim_linker.SetSheet(gfx::SpriteType::kTile, "missing_tile",
                           {0, 0, 0, 16, 16, true});
      anim_linker.SetSheet(gfx::SpriteType::kTile, "tiny_base",
                           {0, 16, 0, 8, 8, true});  // 1x1 (smaller than 2x2)
      anim_linker.SetSheet(gfx::SpriteType::kTile, "norm_base",
                           {0, 32, 0, 16, 16, true});  // 2x2
      anim_linker.SetSheet(gfx::SpriteType::kTile, "tiny_border",
                           {0, 48, 0, 16, 16, true});  // 2x2 (smaller than required 3x3!)
      gfx::SpriteMeta bad_meta;
      bad_meta.frames = 2;
      bad_meta.frametime = 5;
      bad_meta.border = "tiny_border";  // Previously caused OOB read at sparse(2,2)!
      bad_meta.corner = "nonexistent_corner";
      anim_linker.SetMetadata("norm_base", bad_meta);

      gfx::SpriteAnimation a1(gfx::SpriteType::kTile, "norm_base", &anim_linker);
      a1.SetConnectionChecker(
          [](const level::Level&, int32_t, int32_t, const level::Tile&, bool) {
            return false;
          });
      // Move construction & move assignment must keep Registry valid without dangling pointers
      gfx::SpriteAnimation a2(std::move(a1));
      gfx::SpriteAnimation a3(gfx::SpriteType::kTile, "tiny_base", &anim_linker);
      a3 = std::move(a2);
      gfx::SpriteAnimation::RefreshAll(&anim_linker);

      level::Level anim_lvl(8, 8, 1, 0, nullptr, /*skip_gen=*/true);
      gfx::Screen anim_scr(&anim_linker);
      anim_scr.SetOffset(0, 0);
      backend.last_commands.clear();
      a3.Render(&anim_scr, anim_lvl, 1, 1, 10, 0, 100);
      anim_scr.Flush(&backend);
      Check(!backend.last_commands.empty(),
            "SpriteAnimation move-assigned instance renders safely without OOB when border < 3x3");
    }

    auto lines = gfx::Font::GetLines("hello \nworld", 200, 100, 1);
    Check(lines.size() == 2 && lines[0] == "hello" && lines[1] == "world",
          "Font::GetLines splits \"hello \\nworld\" cleanly without trailing space or blank line");

    auto& gs = game::GameState::Instance();
    int32_t prev_gt = gs.game_time;
    gs.game_time = 16199;
    Check(gs.GetTimeOfDay() == game::TimeOfDay::kMorning,
          "GameState::GetTimeOfDay is Morning at tick 16199");
    gs.game_time = 16200;
    Check(gs.GetTimeOfDay() == game::TimeOfDay::kDay,
          "GameState::GetTimeOfDay transitions to Day at tick 16200 (dayLength/4)");
    gs.game_time = 32400;
    Check(gs.GetTimeOfDay() == game::TimeOfDay::kEvening,
          "GameState::GetTimeOfDay transitions to Evening at tick 32400 (dayLength/2)");
    gs.game_time = 48600;
    Check(gs.GetTimeOfDay() == game::TimeOfDay::kNight,
          "GameState::GetTimeOfDay transitions to Night at tick 48600 (3*dayLength/4)");
    gs.game_time = prev_gt;
  }

  // 17. IO parity fixes (defaults, scoretime option visibility, gamepad edge latch, repeat backspace, last input device)
  {
    std::cout << "[17/17] Testing IO parity fixes...\n";
    io::Settings::Instance().InitDefaults();
    Check(!io::Settings::GetBool("tutorials") &&
              !io::Settings::GetBool("quests") &&
              io::Settings::GetBool("showquests"),
          "Settings defaults for tutorials/quests are false and showquests is true");

    auto* score_entry = io::Settings::GetEntry("scoretime");
    Check(score_entry != nullptr && io::Settings::GetInt("scoretime") == 20,
          "scoretime defaults to 20");
    if (score_entry) {
      screen::SettingListEntry sle(score_entry);
      io::InputHandler dummy_in;
      dummy_in.PushKeyEvents({{"RIGHT", true, false}});
      dummy_in.Tick(100);
      sle.Tick(dummy_in);
      int32_t v1 = io::Settings::GetInt("scoretime");
      dummy_in.PushKeyEvents({{"RIGHT", false, false}, {"RIGHT", true, false}});
      dummy_in.Tick(200);
      sle.Tick(dummy_in);
      int32_t v2 = io::Settings::GetInt("scoretime");
      dummy_in.PushKeyEvents({{"RIGHT", false, false}, {"RIGHT", true, false}});
      dummy_in.Tick(300);
      sle.Tick(dummy_in);
      int32_t v3 = io::Settings::GetInt("scoretime");
      Check(v1 == 40 && v2 == 60 && v3 == 20,
            "scoretime skips hidden 10 and 120 options (20 -> 40 -> 60 -> 20)");
    }

    io::InputHandler ih;
    platform::GamepadState gp{};
    gp.connected = true;
    gp.buttons[static_cast<size_t>(platform::GamepadButton::kA)] = true;
    ih.UpdateGamepad(gp, 1000);
    ih.UpdateGamepad(gp, 1005);  // Second poll before Tick()
    ih.Tick(1010);
    Check(ih.GetLastInputType() == 1,
          "GetLastInputType reports controller (1) after gamepad button press");
    Check(ih.ButtonPressed(platform::GamepadButton::kA),
          "Gamepad ButtonPressed survives multiple polls prior to Tick()");
    ih.Tick(1026);
    Check(!ih.ButtonPressed(platform::GamepadButton::kA),
          "Gamepad ButtonPressed clears on subsequent Tick()");

    gp.buttons[static_cast<size_t>(platform::GamepadButton::kA)] = false;
    ih.UpdateGamepad(gp, 1500);
    ih.PushKeyEvents({{"W", true, false}});
    ih.Tick(2000);
    Check(ih.GetLastInputType() == 0,
          "GetLastInputType switches back to keyboard (0) when key pressed later");

    ih.PushCharEvents({{'a'}, {'b'}, {'c'}});
    ih.Tick(2050);
    std::string typed = ih.AddKeyTyped("");
    Check(typed == "abc", "Typed text captured as 'abc'");

    ih.PushKeyEvents({{"BACKSPACE", true, true}});
    ih.Tick(2100);
    Check(!ih.PhysicalKeyPressed("BACKSPACE"),
          "Repeat BACKSPACE key event does not trigger PhysicalKeyPressed");
    typed = ih.AddKeyTyped(typed);
    Check(typed == "ab",
          "Repeat BACKSPACE key event erases character cleanly");
  }

  // 18. Item module Java parity fixes
  {
    std::cout << "[18/18] Testing item module Java parity fixes...\n";

    // 18a. Recipe/item registry lookups, aliases, and serialization round-trips
    const char* kJavaItemNames[] = {
        "Steak",           "Gold Apple",     "Raw Obsidian",
        "Totem of Air",    "Obsidian Poppet","Wood Bow",
        "Iron Bow",        "Gold Bow",       "Gem Bow",
        "Wood Fishing Rod","Iron Fishing Rod","Gold Fishing Rod",
        "Gem Fishing Rod", "Black Clothes",  "Reg Clothes",
        "Oxeye Daisy",     "Pink Lily",      "White Lily",
        "Antidious",       "Book",           "Obsidian Heart",
        "Wheat Seeds",     "Grass Seeds",    "Stone",
        "Natural Rock",
    };
    for (const char* name : kJavaItemNames) {
      auto it = item::Items::Get(name, true);
      std::string msg = std::string("Items::Get resolves Java item: ") + name;
      Check(it != nullptr && it->Kind() != item::ItemKind::kUnknown &&
                it->Name() == name,
            msg.c_str());
    }

    auto null_it = item::Items::Get("NULL", true);
    auto unk_it = item::Items::Get("UNKNOWN", true);
    Check(null_it == nullptr && unk_it != nullptr &&
              unk_it->Kind() == item::ItemKind::kUnknown,
          "Items::Get handles NULL and UNKNOWN");

    auto sword_u = item::Items::Get("Wood Sword_5");
    auto pick_s = item::Items::Get("Iron Pickaxe;3");
    auto can_u = item::Items::Get("Watering Can_900");
    auto wood_u = item::Items::Get("Wood_42");
    auto wood_s = item::Items::Get("Wood;17");
    Check(dynamic_cast<item::ToolItem*>(sword_u.get()) != nullptr &&
              dynamic_cast<item::ToolItem*>(sword_u.get())->dur == 5,
          "Items::Get restores ToolItem::dur via underscore");
    Check(dynamic_cast<item::ToolItem*>(pick_s.get()) != nullptr &&
              dynamic_cast<item::ToolItem*>(pick_s.get())->dur == 3,
          "Items::Get restores ToolItem::dur via semicolon");
    Check(dynamic_cast<item::WateringCanItem*>(can_u.get()) != nullptr &&
              dynamic_cast<item::WateringCanItem*>(can_u.get())->content == 900,
          "Items::Get restores WateringCanItem::content");
    Check(dynamic_cast<item::StackableItem*>(wood_u.get()) != nullptr &&
              dynamic_cast<item::StackableItem*>(wood_u.get())->count == 42 &&
              dynamic_cast<item::StackableItem*>(wood_s.get()) != nullptr &&
              dynamic_cast<item::StackableItem*>(wood_s.get())->count == 17,
          "Items::Get restores StackableItem::count via _ and ;");

    auto creative_inv = item::Items::CreateCreativeModeInventory();
    bool has_power_glove = false;
    for (const auto& entry : creative_inv->ItemsList()) {
      if (entry && entry->Kind() == item::ItemKind::kPowerGlove) {
        has_power_glove = true;
      }
    }
    Check(!has_power_glove,
          "CreateCreativeModeInventory excludes PowerGloveItem");

    // 18b. ToolItem durability, specs, and attack damage
    Check(item::GetToolSpec(item::ToolType::kShovel).durability == 34 &&
              item::GetToolSpec(item::ToolType::kHoe).durability == 30 &&
              item::GetToolSpec(item::ToolType::kSword).durability == 52 &&
              item::GetToolSpec(item::ToolType::kPickaxe).durability == 38 &&
              item::GetToolSpec(item::ToolType::kAxe).durability == 34 &&
              item::GetToolSpec(item::ToolType::kBow).durability == 30 &&
              item::GetToolSpec(item::ToolType::kClaymore).durability == 44 &&
              item::GetToolSpec(item::ToolType::kShears).durability == 42,
          "ToolSpec durabilities match Java");
    Check(item::GetToolSpec(item::ToolType::kShears).y_pos == 12 &&
              item::GetToolSpec(item::ToolType::kShears).no_level &&
              !item::GetToolSpec(item::ToolType::kBow).no_level,
          "Shears sprite row/no_level and Bow no_level match Java");

    item::ToolItem iron_pick(item::ToolType::kPickaxe, 2);
    Check(iron_pick.damage == 20 && iron_pick.GetDamage() >= 20,
          "ToolItem::damage and GetDamage match Java formula");
    item::ToolItem shears_tool(item::ToolType::kShears, 0);
    Check(!shears_tool.CanAttack(), "Shears CanAttack() is false");

    auto zombie_target = std::make_shared<entity::Zombie>(1);
    Check(iron_pick.GetAttackDamageBonus(nullptr) == 0,
          "GetAttackDamageBonus(nullptr) returns 0");
    const int32_t prev_dur = iron_pick.dur;
    const int32_t pick_bonus = iron_pick.GetAttackDamageBonus(zombie_target.get());
    Check(pick_bonus > 0 && iron_pick.dur == prev_dur - 1,
          "Pickaxe GetAttackDamageBonus deals bonus damage and consumes 1 dur");

    // 18c. PotionItem specs, bottle return, Escape potion
    Check(item::GetPotionSpec(item::PotionType::kSpeed).duration == 4200 &&
              item::GetPotionSpec(item::PotionType::kLight).duration == 6000 &&
              item::GetPotionSpec(item::PotionType::kSwim).duration == 4800 &&
              item::GetPotionSpec(item::PotionType::kEnergy).duration == 8400 &&
              item::GetPotionSpec(item::PotionType::kRegen).duration == 1800 &&
              item::GetPotionSpec(item::PotionType::kHealth).duration == 0 &&
              item::GetPotionSpec(item::PotionType::kTime).duration == 1800 &&
              item::GetPotionSpec(item::PotionType::kLava).duration == 7200 &&
              item::GetPotionSpec(item::PotionType::kShield).duration == 5400 &&
              item::GetPotionSpec(item::PotionType::kHaste).duration == 4800 &&
              item::GetPotionSpec(item::PotionType::kEscape).duration == 0,
          "PotionSpec durations match Java");

    auto& gs = game::GameState::Instance();
    const size_t prev_mode = io::Settings::GetIdx("mode");
    io::Settings::SetIdx("mode", 0);  // Survival

    level::Level surface_lvl(64, 64, 1, 0, nullptr, /*skip_gen=*/true);
    level::Level cave_lvl(64, 64, 1, -1, nullptr, /*skip_gen=*/true);
    for (int32_t y = 0; y < 32; ++y) {
      for (int32_t x = 0; x < 32; ++x) {
        surface_lvl.SetTile(x, y, level::Tiles::kGrass, 0);
        cave_lvl.SetTile(x, y, level::Tiles::kDirt, 0);
      }
    }

    auto p = std::make_shared<entity::Player>(nullptr, &input);
    p->GetInventory().ClearInv();
    surface_lvl.Add(p, 160, 160, false);

    item::PotionItem speed_pot(item::PotionType::kSpeed, 1);
    Check(speed_pot.InteractOn(nullptr, &surface_lvl, 10, 10, p.get(),
                               entity::Direction::kDown),
          "Speed Potion InteractOn succeeds");
    Check(speed_pot.count == 0 && p->potion_effects[0] > 0 &&
              p->GetInventory().Count(
                  *item::Items::Get("Glass Bottle", true)) == 1,
          "Drinking Speed Potion sets effect, consumes item, returns Glass Bottle");

    // Escape Potion on surface fails with message and does not consume
    std::vector<std::string> notif_log;
    const auto prev_notif = gs.on_add_notification;
    gs.on_add_notification = [&notif_log](const std::string& msg, int32_t) {
      notif_log.push_back(msg);
    };
    item::PotionItem escape_pot(item::PotionType::kEscape, 1);
    Check(!escape_pot.InteractOn(nullptr, &surface_lvl, 10, 10, p.get(),
                                 entity::Direction::kDown),
          "Escape Potion returns false on surface (depth 0)");
    Check(escape_pot.count == 1 && !notif_log.empty() &&
              notif_log.back() == "You can't escape from here!",
          "Escape Potion on surface notifies and preserves item");

    // Escape Potion in cave schedules level change +1
    int32_t scheduled_change = 0;
    const auto prev_sched = gs.on_schedule_level_change;
    gs.on_schedule_level_change = [&scheduled_change](int32_t d,
                                                      std::function<void()>) {
      scheduled_change = d;
    };
    p->Remove();
    cave_lvl.Add(p, 160, 160, false);
    Check(escape_pot.InteractOn(nullptr, &cave_lvl, 10, 10, p.get(),
                                entity::Direction::kDown),
          "Escape Potion succeeds in cave (depth -1)");
    Check(scheduled_change == 1 && escape_pot.count == 0,
          "Escape Potion in cave schedules +1 level change and consumes item");
    gs.on_schedule_level_change = prev_sched;

    // 18d. HeartItem increases max_health up to 30
    p->Remove();
    surface_lvl.Add(p, 160, 160, false);
    p->max_health = entity::Player::kMaxHealth;
    p->health = 10;
    auto heart_ptr = item::Items::Get("Obsidian Heart", true);
    auto* heart = dynamic_cast<item::HeartItem*>(heart_ptr.get());
    Check(heart != nullptr, "Obsidian Heart is a HeartItem");
    if (heart) {
      Check(heart->InteractOn(nullptr, &surface_lvl, 10, 10, p.get(),
                              entity::Direction::kDown),
            "Obsidian Heart consumes when below kMaxTotalHealth");
      Check(p->max_health == entity::Player::kMaxHealth + 5 &&
                p->health == 15 && heart->count == 0,
            "Obsidian Heart boosts max_health and heals");

      p->max_health = entity::Player::kMaxTotalHealth;
      heart->count = 1;
      notif_log.clear();
      Check(!heart->InteractOn(nullptr, &surface_lvl, 10, 10, p.get(),
                               entity::Direction::kDown),
            "Obsidian Heart fails cleanly at kMaxTotalHealth");
      Check(heart->count == 1 && !notif_log.empty() &&
                notif_log.back() == "Health increase is at max!",
            "Obsidian Heart notifies at max without consuming");
    }
    gs.on_add_notification = prev_notif;

    // 18e. BucketItem: Water + Lava -> Obsidian, Grass blocked, Hole filled
    surface_lvl.SetTile(5, 5, level::Tiles::kLava, 0);
    auto water_bucket =
        std::make_shared<item::BucketItem>(item::BucketFill::kWater, 1);
    p->GetInventory().ClearInv();
    p->active_item = water_bucket;
    auto* active_bkt = dynamic_cast<item::BucketItem*>(p->active_item.get());
    Check(active_bkt->InteractOn(
              const_cast<level::Tile*>(level::Tiles::Get(level::Tiles::kLava)),
              &surface_lvl, 5, 5, p.get(), entity::Direction::kDown),
          "Water Bucket interacts with Lava tile");
    Check(surface_lvl.GetTile(5, 5)->id == level::Tiles::kObsidian &&
              active_bkt->filling == item::BucketFill::kEmpty &&
              active_bkt->Name() == "Empty Bucket",
          "Water Bucket turns Lava into Obsidian and becomes Empty Bucket");

    item::BucketItem water_bkt2(item::BucketFill::kWater, 2);
    surface_lvl.SetTile(6, 5, level::Tiles::kGrass, 0);
    Check(!water_bkt2.InteractOn(
              const_cast<level::Tile*>(level::Tiles::Get(level::Tiles::kGrass)),
              &surface_lvl, 6, 5, p.get(), entity::Direction::kDown),
          "Water Bucket does not empty onto Grass");
    surface_lvl.SetTile(6, 5, level::Tiles::kHole, 0);
    Check(water_bkt2.InteractOn(
              const_cast<level::Tile*>(level::Tiles::Get(level::Tiles::kHole)),
              &surface_lvl, 6, 5, p.get(), entity::Direction::kDown) &&
              surface_lvl.GetTile(6, 5)->id == level::Tiles::kWater &&
              water_bkt2.count == 1 &&
              p->GetInventory().Count(
                  *item::Items::Get("Empty Bucket", true)) == 1,
          "Stacked Water Bucket empties onto Hole and adds Empty Bucket to inventory");

    // 18f. WateringCanItem fills at water, decrements content, hydrates farmland
    item::WateringCanItem can;
    surface_lvl.SetTile(7, 5, level::Tiles::kWater, 0);
    Check(can.InteractOn(
              const_cast<level::Tile*>(level::Tiles::Get(level::Tiles::kWater)),
              &surface_lvl, 7, 5, p.get(), entity::Direction::kDown),
          "WateringCanItem refills at Water");
    Check(can.content == item::WateringCanItem::kCapacity,
          "WateringCanItem content equals kCapacity after refill");
    surface_lvl.SetTile(8, 5, level::Tiles::kFarmland, 0);
    Check(can.InteractOn(const_cast<level::Tile*>(
                             level::Tiles::Get(level::Tiles::kFarmland)),
                         &surface_lvl, 8, 5, p.get(), entity::Direction::kDown),
          "WateringCanItem waters Farmland");
    Check(can.content == item::WateringCanItem::kCapacity - 1 &&
              surface_lvl.GetData(8, 5) > 0,
          "WateringCanItem decrements content and hydrates Farmland");

    // 18g. FishingRodItem starts player fishing and depletes past threshold
    item::FishingRodItem rod(0);
    Check(!rod.CanAttack(), "FishingRodItem CanAttack() is false");
    surface_lvl.SetTile(9, 5, level::Tiles::kWater, 0);
    Check(rod.InteractOn(
              const_cast<level::Tile*>(level::Tiles::Get(level::Tiles::kWater)),
              &surface_lvl, 9, 5, p.get(), entity::Direction::kDown),
          "FishingRodItem starts fishing on Water when not swimming");
    Check(p->is_fishing && p->fishing_level == 0,
          "Player enters fishing state with rod level");
    rod.uses = 250;
    Check(rod.IsDepleted(), "FishingRodItem IsDepleted() true when uses=250");

    // 18h. ArmorItem stamina gate and ClothingItem previous swap
    p->GetInventory().ClearInv();
    p->cur_armor.reset();
    p->stamina = 0;
    auto leather_it = item::Items::Get("Leather Armor", true);
    auto* leather = dynamic_cast<item::ArmorItem*>(leather_it.get());
    Check(leather != nullptr &&
              !leather->InteractOn(nullptr, &surface_lvl, 10, 10, p.get(),
                                   entity::Direction::kDown),
          "ArmorItem requires positive stamina to PayStamina(9)");
    p->stamina = 10;
    Check(leather->InteractOn(nullptr, &surface_lvl, 10, 10, p.get(),
                              entity::Direction::kDown) &&
              p->cur_armor != nullptr && p->stamina == 1,
          "ArmorItem equips and deducts 9 stamina");

    p->shirt_color = gfx::color::Get(1, 51, 51, 0);  // Reg Clothes
    auto black_clothes = item::Items::Get("Black Clothes", true);
    auto* cc = dynamic_cast<item::ClothingItem*>(black_clothes.get());
    Check(cc != nullptr &&
              cc->InteractOn(nullptr, &surface_lvl, 10, 10, p.get(),
                             entity::Direction::kDown),
          "ClothingItem equips Black Clothes");
    Check(p->GetInventory().Count(*item::Items::Get("Reg Clothes", true)) == 1,
          "ClothingItem returns previous Reg Clothes to inventory");

    // 18i. Inventory PowerGlove guard, max_count splitting, and TryAdd partial chance
    item::Inventory inv(27, false);
    inv.Add(std::make_unique<item::PowerGloveItem>());
    Check(inv.InvSize() == 0, "Inventory::Add ignores PowerGloveItem");

    auto stack_250 = std::make_unique<item::StackableItem>(
        "Wood", gfx::LinkedSprite(gfx::SpriteType::kItem, "wood"), 250);
    inv.Add(std::move(stack_250));
    Check(inv.InvSize() == 3 &&
              inv.Count(*item::Items::Get("Wood", true)) == 250,
          "Inventory::Add splits stacks > 100 across slots");

    item::Inventory prob_inv(27, false);
    util::Random det_rng;
    det_rng.SetSeed(12345);
    prob_inv.TryAdd(det_rng, 2, *item::Items::Get("Wood", true), 100, false);
    const int32_t wood_cnt = prob_inv.Count(*item::Items::Get("Wood", true));
    Check(wood_cnt > 10 && wood_cnt < 90,
          "Inventory::TryAdd(all_or_nothing=false) rolls per-item chance");

    io::Settings::SetIdx("mode", prev_mode);
  }

  // [19] Level module Java parity regression suite
  {
    std::cout << "[19/19] Testing level module Java parity fixes...\n";
    using namespace minicraft::level;
    auto& gs = game::GameState::Instance();
    const int32_t prev_mode = io::Settings::GetIdx("mode");
    const bool prev_aw = gs.air_wizard_beaten;
    const bool prev_ok = gs.obsidian_knight_beaten;
    const bool prev_ok_sum = gs.obsidian_knight_summoned;
    io::Settings::SetIdx("mode", 0);  // Normal (Survival)
    gs.air_wizard_beaten = false;
    gs.obsidian_knight_beaten = false;
    gs.obsidian_knight_summoned = false;

    // 19a. LevelNoise 3D scale-layer independence & determinism
    {
      LevelNoise n1(987654321LL, 0, 0, 0, 64, 64);
      LevelNoise n2(987654321LL, 0, 0, 0, 64, 64);
      Check(n1.Sample(0, 12, 18, 0) != n1.Sample(0, 12, 18, 1),
            "LevelNoise different layers at same scale/xy are independent");
      Check(n1.Sample(0, 12, 18, 0) != n1.Sample(3, 12, 18, 0),
            "LevelNoise different scales at same layer/xy are independent");
      Check(n1.GetTemperature(7, 9) == n2.GetTemperature(7, 9) &&
                n1.GetHeight(7, 9) == n2.GetHeight(7, 9) &&
                n1.GetHumidity(7, 9) == n2.GetHumidity(7, 9),
            "LevelNoise deterministic across same seed");
    }

    // 19b. Biome registry & tile generation basics
    {
      Biomes::Init();
      util::Random rng(42);
      LevelNoise n(42, 0, 0, 0, 64, 64);
      Check(Biomes::Get(BiomeId::kOcean).GenerateTile(n, 4, 4, rng).tile_id ==
                Tiles::kWater,
            "Ocean biome generates Water tile");
      Check(Biomes::Get(BiomeId::kRiverBank).GenerateTile(n, 4, 4, rng).tile_id ==
                Tiles::kSand,
            "RiverBank biome generates Sand tile");
    }

    // 19c. Structure blueprints (DungeonBossRoom, DungeonGarden, AirWizardHouse)
    {
      Level boss_lvl(64, 64, 0, -4, nullptr, /*skip_gen=*/true);
      Structure::DungeonBossRoom().Draw(boss_lvl, 32, 32);
      boss_lvl.Tick(false);
      int32_t door_count = 0;
      for (int32_t y = 26; y <= 38; ++y) {
        for (int32_t x = 26; x <= 38; ++x) {
          if (boss_lvl.GetTile(x, y)->id == Tiles::kBossDoor) ++door_count;
        }
      }
      Check(door_count == 4, "DungeonBossRoom places 4 Obsidian Boss Doors");
      Check(boss_lvl.GetTile(32, 32)->id == Tiles::kBossFloor,
            "DungeonBossRoom center tile is Obsidian Boss Floor");
      Check(boss_lvl.GetTile(28, 28)->id == Tiles::kBossWall,
            "DungeonBossRoom corners are Obsidian Boss Wall");
      int32_t statue_hp = -1;
      for (const auto& e : boss_lvl.Entities()) {
        if (auto* ks = dynamic_cast<entity::KnightStatue*>(e.get())) {
          statue_hp = ks->BossHp();
        }
      }
      Check(statue_hp == 5000,
            "DungeonBossRoom spawns KnightStatue with 5000 boss HP");

      Level garden_lvl(32, 32, 0, -4, nullptr, /*skip_gen=*/true);
      Structure::DungeonGarden().Draw(garden_lvl, 16, 16);
      int32_t flowers = 0;
      for (int32_t y = 10; y <= 22; ++y) {
        for (int32_t x = 10; x <= 22; ++x) {
          if (garden_lvl.GetTile(x, y)->id == Tiles::kFlower) ++flowers;
        }
      }
      Check(flowers > 0, "DungeonGarden places Flower tiles");

      Level sky_lvl(32, 32, 0, 1, nullptr, /*skip_gen=*/true);
      Structure::AirWizardHouse().Draw(sky_lvl, 16, 16);
      sky_lvl.Tick(false);
      bool has_gold_lantern = false;
      bool has_enchanter = false;
      for (const auto& e : sky_lvl.Entities()) {
        if (auto* lan = dynamic_cast<entity::Lantern*>(e.get())) {
          if (lan->GetType() == entity::Lantern::Type::kGold) {
            has_gold_lantern = true;
          }
        }
        if (auto* cr = dynamic_cast<entity::Crafter*>(e.get())) {
          if (cr->GetType() == entity::Crafter::Type::kEnchanter) {
            has_enchanter = true;
          }
        }
      }
      Check(has_gold_lantern && has_enchanter,
            "AirWizardHouse spawns Gold Lantern and Enchanter");
    }

    // 19d. Level::Tick(!full_tick vs full_tick), GetEntitiesInTiles, CheckChestCount
    {
      io::InputHandler dummy_input;
      Level tick_lvl(32, 32, 0, 0, nullptr, /*skip_gen=*/true);
      for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
          tick_lvl.SetTile(x, y, Tiles::kTreeSapling, 0);
        }
      }
      auto p = std::make_shared<entity::Player>(nullptr, &dummy_input);
      p->x = 16 * 16 + 8;
      p->y = 16 * 16 + 8;
      tick_lvl.Add(p);
      tick_lvl.Tick(false);
      Check(tick_lvl.Entities().size() == 1,
            "Level::Tick(false) flushes pending entities");
      int32_t aged_before = 0;
      for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
          if (tick_lvl.GetData(x, y) > 0) ++aged_before;
        }
      }
      Check(aged_before == 0,
            "Level::Tick(false) skips random tile ticks and world updates");
      tick_lvl.Tick(true);
      int32_t aged_after = 0;
      for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
          aged_after += tick_lvl.GetData(x, y);
        }
      }
      Check(aged_after == 10,
            "Level::Tick(true) runs 10 random tile ticks around Player");

      // Manual ~3 Hz bare-hand punching breaks a TreeTile under Level::Tick(true).
      Level tree_lvl(32, 32, 0, 0, nullptr, /*skip_gen=*/true);
      for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
          tree_lvl.SetTile(x, y, Tiles::kGrass, 0);
        }
      }
      tree_lvl.SetTile(16, 15, Tiles::kTree, 0);
      io::InputHandler tree_input;
      auto tp = std::make_shared<entity::Player>(nullptr, &tree_input);
      tp->SetLevel(&tree_lvl, 16 * 16 + 8, 16 * 16 + 3);
      tp->dir = entity::Direction::kUp;
      tree_lvl.Add(tp);
      tree_lvl.Tick(false);
      for (int32_t frame = 0; frame < 600; ++frame) {
        if (tree_lvl.GetTile(16, 15)->id == Tiles::kGrass) break;
        if (frame % 20 == 0) {
          tree_input.PushKeyEvents({{"C", true, false}});
        } else if (frame % 20 == 1) {
          tree_input.PushKeyEvents({{"C", false, false}});
        }
        tree_input.Tick(static_cast<uint64_t>(frame) * 16);
        tree_lvl.Tick(true);
      }
      Check(tree_lvl.GetTile(16, 15)->id == Tiles::kGrass,
            "Manual bare-hand hits break TreeTile despite background tile ticks");

      auto nearby = tick_lvl.GetEntitiesInTiles(16, 16, 16, 16);
      auto far_away = tick_lvl.GetEntitiesInTiles(0, 0, 4, 4);
      Check(nearby.size() == 1 && far_away.empty(),
            "GetEntitiesInTiles filters by center tile coordinate");

      Level d4_lvl(32, 32, 0, -4, nullptr, /*skip_gen=*/true);
      for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
          d4_lvl.SetTile(x, y, Tiles::kDirt, 0);
        }
      }
      auto dc1 = std::make_shared<entity::DungeonChest>(false, true);
      auto dc2 = std::make_shared<entity::DungeonChest>(false, false);
      dc1->x = 8 * 16 + 8;
      dc1->y = 8 * 16 + 8;
      dc2->x = 10 * 16 + 8;
      dc2->y = 10 * 16 + 8;
      d4_lvl.Add(dc1);
      d4_lvl.Add(dc2);
      d4_lvl.Tick(false);
      d4_lvl.CheckChestCount(true);
      Check(d4_lvl.chest_count == 1,
            "CheckChestCount counts only locked DungeonChests on depth -4");
      p->GetInventory().ClearInv();
      p->GetInventory().Add(item::Items::Get("Dungeon Key", true));
      dc1->Use(*p);
      d4_lvl.Tick(false);
      int32_t gold_apples = 0;
      for (const auto& e : d4_lvl.Entities()) {
        if (auto* ie = dynamic_cast<entity::ItemEntity*>(e.get())) {
          if (ie->item && ie->item->Name() == "Gold Apple") {
            ++gold_apples;
          }
        }
      }
      Check(d4_lvl.chest_count == 0 && gold_apples == 5,
            "Unlocking last DungeonChest drops 5 Gold Apples");
    }

    // 19e. Tiles::Get / Tiles::Find / helpers parity
    {
      Check(Tiles::Get("Wheat_99") == Tiles::Get(Tiles::kWheat),
            "Tiles::Get strips legacy underscore metadata");
      Check(Tiles::Get("Lapis Ore") == Tiles::Get(Tiles::kLapis) &&
                Tiles::Get("Wool") == Tiles::Get(Tiles::kWhiteWool),
            "Tiles::Get resolves Lapis Ore and Wool aliases");
      Check(Tiles::Get("NonExistentTileXYZ") == Tiles::Get(Tiles::kGrass),
            "Tiles::Get falls back to Grass for unknown names");
      Check(Tiles::Find("NonExistentTileXYZ") == nullptr,
            "Tiles::Find returns nullptr for unknown names");
      Check(Tiles::IsExplosionResistant(Tiles::kHardRock) &&
                Tiles::IsExplosionResistant(Tiles::kBossDoor) &&
                !Tiles::IsExplosionResistant(Tiles::kRock),
            "Tiles::IsExplosionResistant matches Java explosion blacklist");
      Check(Tiles::IsWool(Tiles::kWhiteWool) &&
                Tiles::IsWool(Tiles::kOrangeWool) &&
                !Tiles::IsWool(Tiles::kDirt),
            "Tiles::IsWool identifies all wool tile IDs");
    }

    // 19f. Tile physics, interactions & farming bit-packing parity
    {
      io::InputHandler dummy_input;
      Level t_lvl(32, 32, 0, 0, nullptr, /*skip_gen=*/true);
      auto p = std::make_shared<entity::Player>(nullptr, &dummy_input);
      p->x = 5 * 16 + 8;
      p->y = 5 * 16 + 8;
      p->stamina = p->max_stamina;
      p->shirt_color = 0;
      p->cur_armor.reset();
      t_lvl.Add(p);
      t_lvl.Tick(false);

      // RawStone & RawObsidian passable + harvestable
      t_lvl.SetTile(6, 5, Tiles::kRawStone, 0);
      Check(t_lvl.GetTile(6, 5)->MayPass(t_lvl, 6, 5, *p),
            "RawStone (MaterialTile) is passable");
      auto wood_pick = std::make_unique<item::ToolItem>(item::ToolType::kPickaxe, 0);
      Check(t_lvl.GetTile(6, 5)->Interact(t_lvl, 6, 5, *p, wood_pick.get(),
                                          entity::Direction::kRight) &&
                t_lvl.GetTile(6, 5)->id == Tiles::kHole,
            "Pickaxe harvests RawStone into Hole on depth 0");

      // DoorTile Hurt toggles open/closed; Interact breaks; BossDoor locked
      t_lvl.SetTile(7, 5, Tiles::kWoodDoor, 0);
      t_lvl.GetTile(7, 5)->Hurt(t_lvl, 7, 5, *p, 1, entity::Direction::kRight);
      Check(t_lvl.GetData(7, 5) == 1 &&
                t_lvl.GetTile(7, 5)->MayPass(t_lvl, 7, 5, *p),
            "DoorTile Hurt by Player toggles door open");
      auto wood_axe = std::make_unique<item::ToolItem>(item::ToolType::kAxe, 0);
      Check(t_lvl.GetTile(7, 5)->Interact(t_lvl, 7, 5, *p, wood_axe.get(),
                                          entity::Direction::kRight) &&
                t_lvl.GetTile(7, 5)->id == Tiles::kWoodPlanks,
            "Axe Interact breaks Wood Door to Wood Planks");

      t_lvl.SetTile(8, 5, Tiles::kBossDoor, 0);
      auto gem_pick = std::make_unique<item::ToolItem>(item::ToolType::kPickaxe, 4);
      t_lvl.GetTile(8, 5)->Interact(t_lvl, 8, 5, *p, gem_pick.get(),
                                    entity::Direction::kRight);
      Check(t_lvl.GetTile(8, 5)->id == Tiles::kBossDoor,
            "BossDoor stays locked before Obsidian Knight is beaten");

      // SignTile empty-hand Interact opens editor; Axe removes sign
      bool opened_sign = false;
      auto prev_sign_cb = gs.on_open_sign_editor;
      gs.on_open_sign_editor = [&](int32_t, int32_t, int32_t) {
        opened_sign = true;
      };
      t_lvl.SetTile(9, 5, Tiles::kSign, Tiles::kGrass);
      t_lvl.GetTile(9, 5)->Interact(t_lvl, 9, 5, *p, nullptr,
                                    entity::Direction::kRight);
      Check(opened_sign, "SignTile empty-hand Interact opens sign editor");
      t_lvl.GetTile(9, 5)->Interact(t_lvl, 9, 5, *p, wood_axe.get(),
                                    entity::Direction::kRight);
      Check(t_lvl.GetTile(9, 5)->id == Tiles::kGrass,
            "SignTile Axe Interact restores underlying Grass tile");
      gs.on_open_sign_editor = prev_sign_cb;

      // HardRock Gem Pickaxe gate
      bool notified = false;
      auto prev_notif_cb = gs.on_add_notification;
      gs.on_add_notification = [&](const std::string&, int32_t) {
        notified = true;
      };
      t_lvl.SetTile(10, 5, Tiles::kHardRock, 0);
      Check(!t_lvl.GetTile(10, 5)->Interact(t_lvl, 10, 5, *p, wood_pick.get(),
                                            entity::Direction::kRight) &&
                notified && t_lvl.GetData(10, 5) == 0,
            "HardRock rejects non-gem pickaxe and notifies player");
      Check(t_lvl.GetTile(10, 5)->Interact(t_lvl, 10, 5, *p, gem_pick.get(),
                                           entity::Direction::kRight) &&
                t_lvl.GetData(10, 5) > 0,
            "HardRock accepts Gem Pickaxe and takes damage");
      gs.on_add_notification = prev_notif_cb;

      // Water cooling Lava -> Raw Obsidian
      t_lvl.SetTile(12, 5, Tiles::kWater, 0);
      t_lvl.SetTile(13, 5, Tiles::kLava, 0);
      t_lvl.GetTile(12, 5)->Tick(t_lvl, 12, 5);
      Check(t_lvl.GetTile(13, 5)->id == Tiles::kRawObsidian,
            "WaterTile::Tick cools adjacent Lava into Raw Obsidian");

      // Wool, Hole, InfiniteFall, LavaBrick MayPass
      t_lvl.SetTile(14, 5, Tiles::kWhiteWool, 0);
      t_lvl.SetTile(15, 5, Tiles::kHole, 0);
      t_lvl.SetTile(16, 5, Tiles::kInfiniteFall, 0);
      t_lvl.SetTile(17, 5, Tiles::kLavaBrick, 0);
      auto air_wiz = std::make_shared<entity::AirWizard>();
      entity::Arrow arrow(nullptr, entity::Direction::kDown, 1);
      Check(t_lvl.GetTile(16, 5)->MayPass(t_lvl, 16, 5, *air_wiz) &&
                t_lvl.GetTile(16, 5)->MayPass(t_lvl, 16, 5, arrow) &&
                !t_lvl.GetTile(16, 5)->MayPass(t_lvl, 16, 5, *p),
            "InfiniteFall allows AirWizard/Arrow and blocks Survival Player");
      Check(t_lvl.GetTile(14, 5)->ConnectsToSand(t_lvl, 14, 5) == false &&
                t_lvl.GetTile(15, 5)->ConnectsToSand(t_lvl, 15, 5) &&
                t_lvl.GetTile(15, 5)->ConnectsToFluid(t_lvl, 15, 5),
            "HoleTile connects to sand and fluid");
      p->stamina = p->max_stamina;
      auto shears = std::make_unique<item::ToolItem>(item::ToolType::kShears, 2);
      t_lvl.GetTile(14, 5)->Interact(t_lvl, 14, 5, *p, shears.get(),
                                     entity::Direction::kRight);
      Check(t_lvl.GetTile(14, 5)->id == Tiles::kHole,
            "WoolTile Shears Interact leaves Hole tile");

      // Farmland & CropTile Java bit-packing + harvest to Farmland
      const int16_t crop_data = static_cast<int16_t>((7 << 3) | 5);
      t_lvl.SetTile(18, 5, Tiles::kWheat, crop_data);
      auto fert = item::Items::Get("Fertilizer", true);
      t_lvl.GetTile(18, 5)->Interact(t_lvl, 18, 5, *p, fert.get(),
                                     entity::Direction::kRight);
      Check((static_cast<uint16_t>(t_lvl.GetData(18, 5)) >> 7) == 40,
            "CropTile Fertilizer boosts high-bit fertilization by 40");
      t_lvl.GetTile(18, 5)->Hurt(t_lvl, 18, 5, *p, 1, entity::Direction::kDown);
      Check(t_lvl.GetTile(18, 5)->id == Tiles::kFarmland &&
                t_lvl.GetData(18, 5) == 5,
            "CropTile::Hurt harvests to Farmland preserving moisture bits");
    }

    gs.air_wizard_beaten = prev_aw;
    gs.obsidian_knight_beaten = prev_ok;
    gs.obsidian_knight_summoned = prev_ok_sum;
    io::Settings::SetIdx("mode", prev_mode);
  }

  // 20. Screen module Java parity fixes
  {
    std::cout << "[20/20] Testing screen module Java parity fixes...\n";

    auto& host = screen::DisplayHost::Instance();
    auto prev_get_player = host.get_player;
    auto prev_save_prefs = host.save_prefs;
    auto prev_exit_display = host.exit_display;
    auto prev_set_display = host.set_display;
    auto prev_get_display = host.get_display;
    auto prev_add_notification = host.add_notification;
    auto prev_game_dir = host.game_dir;
    auto prev_input = host.input;
    auto prev_fs = host.fs;
    host.add_notification = nullptr;

    // 20a. InputHandler compound shift+action & virtual chars
    {
      io::InputHandler ih;
      ih.Tick(1000);
      ih.PushKeyEvents({{"SHIFT", true, false}, {"DOWN", true, false}});
      ih.Tick(1020);
      Check(ih.InputPressed("shift+cursor-down"),
            "InputHandler evaluates compound shift+cursor-down");
      ih.Tick(1040);
      ih.PushKeyEvents({{"ENTER", true, false}});
      ih.Tick(1060);
      Check(ih.InputPressed("shift+select"),
            "InputHandler evaluates compound shift+select");

      ih.PushVirtualChar('Z');
      Check(ih.GetKeysTyped("") == "Z",
            "InputHandler::PushVirtualChar delivers virtual char to buffer");

      io::InputHandler ih_hold;
      ih_hold.PushKeyEvents({{"C", true, false}});
      ih_hold.Tick(1100);
      const bool first_tick = ih_hold.InputPressed("attack");
      ih_hold.Tick(1120);
      const bool second_tick = !ih_hold.InputPressed("attack");
      for (int32_t t = 0; t < 31; ++t) {
        ih_hold.Tick(1140 + static_cast<uint64_t>(t) * 16);
      }
      const bool sticky_tick = ih_hold.InputPressed("attack");
      ih_hold.PushKeyEvents({{"C", false, false}});
      ih_hold.Tick(1800);
      Check(first_tick && second_tick && sticky_tick &&
                !ih_hold.InputPressed("attack"),
            "InputHandler held attack activates sticky repeat until released");
    }

    // 20b. Menu::SetEntries display_length clamp & SettingListEntry ALT/wrap
    {
      screen::Menu::Builder mb(true, 0, gfx::RelPos::kCenter);
      mb.SetDisplayLength(3);
      mb.AddEntry(std::make_unique<screen::StringEntry>("A", false));
      mb.AddEntry(std::make_unique<screen::StringEntry>("B", false));
      mb.AddEntry(std::make_unique<screen::StringEntry>("C", false));
      mb.AddEntry(std::make_unique<screen::StringEntry>("D", false));
      auto menu = mb.CreateMenu();
      std::vector<std::unique_ptr<screen::ListEntry>> small;
      small.push_back(std::make_unique<screen::StringEntry>("One", false));
      menu->SetEntries(std::move(small));
      Check(menu->GetNumOptions() == 1,
            "Menu::SetEntries preserves entry count after shrinking");

      // Centering of entries wider than a fixed menu size (AchievementsDisplay)
      struct CenterProbeEntry : screen::SelectEntry {
        mutable int32_t rendered_x = -1;
        CenterProbeEntry() : screen::SelectEntry("Very Wide Achievement", nullptr, false) {}
        void Render(gfx::Screen* screen, int32_t x, int32_t y,
                    bool is_selected) const override {
          rendered_x = x;
          screen::SelectEntry::Render(screen, x, y, is_selected);
        }
      };
      screen::Menu::Builder center_b(false, 2, gfx::RelPos::kCenter);
      center_b.SetSize(48, 48);
      auto probe = std::make_unique<CenterProbeEntry>();
      const CenterProbeEntry* probe_ptr = probe.get();
      const int32_t probe_w = probe_ptr->GetWidth();
      center_b.AddEntry(std::move(probe));
      auto center_menu = center_b.CreateMenu();
      gfx::Screen dummy_screen(nullptr);
      center_menu->Render(&dummy_screen);
      Check(probe_ptr->rendered_x == gfx::Screen::w / 2 - probe_w / 2,
            "Menu centers wide entries around menu center like AchievementsDisplay");

      int32_t prev_fps = io::Settings::GetMaxFps();
      if (auto* fps_entry = io::Settings::GetEntry("fps")) {
        Check(!fps_entry->wrap && !fps_entry->localize_options,
              "Settings 'fps' disables wrap and option localization");
        screen::SettingListEntry sle(fps_entry);
        io::Settings::SetMaxFps(50);
        io::InputHandler ih;
        ih.Tick(100);
        ih.PushKeyEvents({{"ALT", true, false}, {"RIGHT", true, false}});
        ih.Tick(120);
        sle.Tick(ih);
        Check(io::Settings::GetMaxFps() == 60,
              "SettingListEntry steps IntRange by 10 when ALT is held");
      }
      io::Settings::SetMaxFps(prev_fps);
    }

    // 20c. InputEntry CharFilter + CTRL-C/X/V clipboard & OnScreenKeyboardMenu
    {
      screen::InputEntry ie(
          "Filter", 10, "", nullptr, false, false,
          [](char32_t cp) { return cp >= U'0' && cp <= U'9'; });
      io::InputHandler ih;
      ih.Tick(200);
      ih.PushCharEvents({{'a'}, {'4'}, {'2'}, {'b'}});
      ih.Tick(220);
      ie.Tick(ih);
      Check(ie.GetUserInput() == "42",
            "InputEntry CharFilter rejects non-matching characters");

      ih.Tick(240);
      ih.PushKeyEvents({{"CTRL", true, false}, {"X", true, false}});
      ih.Tick(260);
      ie.Tick(ih);
      Check(ie.GetUserInput().empty() &&
                screen::DisplayHost::ClipboardText() == "42",
            "InputEntry CTRL-X cuts text to clipboard");

      ih.PushKeyEvents({{"CTRL", false, false}, {"X", false, false}});
      ih.Tick(280);
      ih.PushKeyEvents({{"CTRL", true, false}, {"V", true, false}});
      ih.Tick(300);
      ie.Tick(ih);
      Check(ie.GetUserInput() == "42",
            "InputEntry CTRL-V pastes text from clipboard");

      screen::OnScreenKeyboardMenu osk;
      osk.SetVisible(true);
      io::InputHandler ih_pad;
      ih_pad.Tick(400);
      platform::GamepadState gp;
      gp.connected = true;
      gp.buttons[static_cast<size_t>(platform::GamepadButton::kA)] = true;
      ih_pad.UpdateGamepad(gp, 420);
      ih_pad.Tick(420);
      auto tres = osk.TickKeyboard(ih_pad);
      Check(tres == screen::OnScreenKeyboardMenu::TickResult::kNone &&
                ih_pad.GetKeysTyped("") == "Q",
            "OnScreenKeyboardMenu A press emits virtual character");
    }

    // 20d. WorldGenDisplay Windows reserved names & WorldSelectDisplay Copy/Delete
    {
      auto con_entry = screen::WorldGenDisplay::MakeWorldNameInput(
          "Name", {}, "CON", true);
      Check(!con_entry->IsValid(),
            "WorldGenDisplay rejects Windows reserved folder name CON");
      auto nul_entry = screen::WorldGenDisplay::MakeWorldNameInput(
          "Name", {}, "nul.txt", true);
      Check(!nul_entry->IsValid(),
            "WorldGenDisplay rejects Windows reserved stem nul.txt");
      auto ok_entry = screen::WorldGenDisplay::MakeWorldNameInput(
          "Name", {}, "NormalWorld", true);
      Check(ok_entry->IsValid(),
            "WorldGenDisplay accepts normal valid world name");

      const std::string ws_dir = temp_dir + "/screen_ws_test";
      const std::string saves_dir = ws_dir + "/saves";
      fs.CreateDirectories(saves_dir + "/Alpha");
      fs.WriteTextFile(saves_dir + "/Alpha/Level0.miniplussave", "data");
      fs.WriteTextFile(saves_dir + "/Alpha/Game.miniplussave", "2.2.0\n");

      host.game_dir = ws_dir;
      host.fs = &fs;
      std::vector<std::unique_ptr<screen::Display>> pushed;
      int32_t exit_calls = 0;
      host.set_display = [&](std::unique_ptr<screen::Display> d) {
        pushed.push_back(std::move(d));
      };
      host.exit_display = [&](int32_t cnt) { exit_calls += cnt; };

      screen::WorldSelectDisplay ws;
      ws.Init(nullptr);
      io::InputHandler ih;
      ih.Tick(500);
      ih.PushKeyEvents({{"SHIFT", true, false}, {"D", true, false}});
      ih.Tick(520);
      ws.Tick(ih);
      Check(!pushed.empty(),
            "WorldSelectDisplay SHIFT-D opens delete confirmation popup");
      if (!pushed.empty()) {
        ih.Tick(540);
        ih.PushKeyEvents({{"SHIFT", false, false},
                          {"D", false, false},
                          {"ENTER", true, false}});
        ih.Tick(560);
        pushed.back()->Tick(ih);
        Check(!fs.Exists(saves_dir + "/Alpha") && exit_calls >= 3 &&
                  dynamic_cast<screen::WorldGenDisplay*>(pushed.back().get()) !=
                      nullptr,
              "Deleting last world removes folder and transitions to "
              "WorldGenDisplay");
      }
      fs.RemoveDirectoryAll(ws_dir);
    }

    // 20e. EndGameDisplay scoretime unlocks & SavePrefs/LoadPrefs persistence
    {
      const std::string prefs_dir = temp_dir + "/screen_prefs_test";
      fs.CreateDirectories(prefs_dir);
      const size_t prev_st = io::Settings::GetIdx("scoretime");
      const bool prev_auto = io::Settings::GetBool("autosave");
      const bool prev_sq = io::Settings::GetBool("showquests");
      const std::string prev_skin = game::GameState::Instance().selected_skin_name;

      Check(io::Settings::GetEntry("hwa") == nullptr,
            "Hardware acceleration (hwa) option is removed from Settings");

      if (auto* st = io::Settings::GetEntry("scoretime")) {
        for (size_t i = 0; i < st->int_options.size(); ++i) {
          if (st->int_options[i] == 10 && i < st->option_visible.size()) {
            st->option_visible[i] = false;
          }
          if (st->int_options[i] == 20) st->selected_idx = i;
        }
      }
      io::Settings::Set("autosave", false);
      io::Settings::Set("showquests", false);
      game::GameState::Instance().selected_skin_name = "custom_skin_test";

      io::InputHandler ih;
      entity::Player score_player(nullptr, &ih);
      score_player.AddScore(2000);
      host.get_player = [&]() -> entity::Player* { return &score_player; };
      bool prefs_saved = false;
      host.save_prefs = [&]() {
        prefs_saved = true;
        saveload::Save::SavePrefs(fs, prefs_dir, &ih);
      };

      screen::EndGameDisplay eg;
      bool opt10_unlocked = false;
      if (auto* st = io::Settings::GetEntry("scoretime")) {
        for (size_t i = 0; i < st->int_options.size(); ++i) {
          if (st->int_options[i] == 10 && i < st->option_visible.size()) {
            opt10_unlocked = st->option_visible[i];
          }
        }
      }
      Check(prefs_saved && opt10_unlocked,
            "EndGameDisplay unlocks 10-min scoretime when >1000 points and "
            "saves prefs");

      // Clear in-memory and reload from disk to verify persistence.
      io::Settings::Set("autosave", true);
      io::Settings::Set("showquests", true);
      game::GameState::Instance().selected_skin_name = "minicraft.skin.norman";
      if (auto* st = io::Settings::GetEntry("scoretime")) {
        for (size_t i = 0; i < st->int_options.size(); ++i) {
          if (st->int_options[i] == 10 && i < st->option_visible.size()) {
            st->option_visible[i] = false;
          }
        }
      }
      saveload::Load::LoadPrefs(fs, prefs_dir, &ih);
      bool reloaded_opt10 = false;
      if (auto* st = io::Settings::GetEntry("scoretime")) {
        for (size_t i = 0; i < st->int_options.size(); ++i) {
          if (st->int_options[i] == 10 && i < st->option_visible.size()) {
            reloaded_opt10 = st->option_visible[i];
          }
        }
      }
      Check(!io::Settings::GetBool("autosave") && !io::Settings::GetBool("showquests") &&
                game::GameState::Instance().selected_skin_name ==
                    "custom_skin_test" &&
                reloaded_opt10,
            "SavePrefs/LoadPrefs persists autosave, showquests, skin, and "
            "visibleScoreTimes");

      io::Settings::SetIdx("scoretime", prev_st);
      io::Settings::Set("autosave", prev_auto);
      io::Settings::Set("showquests", prev_sq);
      game::GameState::Instance().selected_skin_name = prev_skin;
      fs.RemoveDirectoryAll(prefs_dir);
    }

    // 20f. CraftingDisplay gating & in-place craft order preservation
    {
      const size_t prev_mode = io::Settings::GetIdx("mode");
      io::Settings::SetIdx("mode", 0);  // Survival
      screen::CraftingDisplay::ResetRecipeUnlocks();

      io::InputHandler ih;
      entity::Player p(nullptr, &ih);
      p.GetInventory().ClearInv();
      p.GetInventory().Add(item::Items::Get("Wood_20"));

      item::Recipe r_torch("Torch_1", {"Wood_1"});
      item::Recipe r_plank("Plank_1", {"Wood_10"});
      {
        screen::CraftingDisplay locked_cd({r_plank}, "Bench", &p, false);
        Check(locked_cd.Menus()[0]->GetNumOptions() == 0,
              "CraftingDisplay hides un-unlocked recipes in Survival");
      }

      screen::CraftingDisplay::UnlockRecipe(r_plank);
      screen::CraftingDisplay::UnlockRecipe(r_torch);
      screen::CraftingDisplay cd({r_plank, r_torch}, "Bench", &p, false);
      Check(cd.Menus()[0]->GetNumOptions() == 2,
            "CraftingDisplay displays unlocked recipes");

      // Craft r_plank until player no longer has 10 Wood: order should NOT reshuffle.
      ih.Tick(600);
      ih.PushKeyEvents({{"ENTER", true, false}});
      ih.Tick(620);
      cd.Tick(ih);
      Check(p.GetInventory().Count(*item::Items::Get("Wood", true)) == 10,
            "CraftingDisplay crafts selected recipe");
      ih.Tick(640);
      ih.PushKeyEvents({{"ENTER", true, false}});
      ih.Tick(660);
      cd.Tick(ih);
      // Now Wood is 0: r_plank cannot craft and r_torch cannot craft, order kept.
      Check(cd.Menus()[0]->GetSelection() == 0,
            "CraftingDisplay updates craftability in-place without resetting "
            "menu selection");

      io::Settings::SetIdx("mode", prev_mode);
      screen::CraftingDisplay::ResetRecipeUnlocks();
    }

    // 20g. ContainerDisplay full-destination safety & PauseDisplay single exit
    {
      io::InputHandler ih;
      entity::Player p(nullptr, &ih);
      entity::Chest chest;
      p.GetInventory().ClearInv();
      p.GetInventory().Add(item::Items::Get("Wood Sword"));
      p.GetInventory().Add(item::Items::Get("Stone_5"));

      // Fill chest completely with distinct unstackable items so Add() fails.
      const int32_t chest_cap = chest.GetInventory()->GetMaxSlots();
      for (int32_t i = 0; i < chest_cap; ++i) {
        chest.GetInventory()->Add(item::Items::Get("Wood Shovel"));
      }
      screen::ContainerDisplay cd(&p, &chest);
      // Focus player inventory (menu 0), slot 0 ("Wood Sword").
      ih.Tick(700);
      ih.PushKeyEvents({{"C", true, false}});  // default attack = C
      ih.Tick(720);
      cd.Tick(ih);
      Check(p.GetInventory().InvSize() == 2 &&
                p.GetInventory().Get(0) &&
                p.GetInventory().Get(0)->Name() == "Wood Sword",
            "ContainerDisplay leaves source item at original slot if "
            "destination chest is full");

      int32_t pause_exits = 0;
      host.exit_display = [&](int32_t cnt) { pause_exits += cnt; };
      screen::PauseDisplay pd;
      ih.Tick(740);
      ih.PushKeyEvents({{"ESCAPE", true, false}});
      ih.Tick(760);
      pd.Tick(ih);
      Check(pause_exits == 1,
            "PauseDisplay calls ExitDisplay once when pause/exit pressed");
    }

    // 20h. SignDisplay cursor/clipboard/SignVersion + SignDisplayMenu invalidation
    {
      screen::SignDisplay::ResetSignTexts();
      const uint32_t v0 = screen::SignDisplay::SignVersion();
      screen::SignDisplay::UpdateSign(0, 3, 4, {"ac"});
      const uint32_t v1 = screen::SignDisplay::SignVersion();
      screen::SignDisplayMenu overlay(0, 3, 4);
      Check(v1 > v0 && overlay.Matches(0, 3, 4),
            "SignDisplay::UpdateSign increments SignVersion and matches overlay");

      int32_t sign_exits = 0;
      host.exit_display = [&](int32_t cnt) { sign_exits += cnt; };
      screen::SignDisplay sd(0, 3, 4);
      io::InputHandler ih;
      ih.Tick(800);
      ih.PushKeyEvents({{"RIGHT", true, false}});
      ih.Tick(820);
      sd.Tick(ih);
      ih.Tick(840);
      ih.PushCharEvents({{'b'}});
      ih.Tick(860);
      sd.Tick(ih);
      ih.Tick(880);
      ih.PushKeyEvents({{"SHIFT", true, false}, {"ENTER", true, false}});
      ih.Tick(900);
      sd.Tick(ih);
      const auto* edited = screen::SignDisplay::GetSign(0, 3, 4);
      Check(sign_exits == 1 && edited != nullptr && !edited->empty() &&
                (*edited)[0] == "abc" && !overlay.Matches(0, 3, 4),
            "SignDisplay inserts mid-line from cursor_x=0, confirms on "
            "SHIFT-ENTER, and invalidates stale SignDisplayMenu");
    }

    // 20i. QuestsDisplay tabs/filtering & TutorialDisplayHandler order/resume
    {
      auto& adv = util::AdvancementSystem::Instance();
      const auto& tuts = adv.Tutorials();
      Check(!tuts.empty() && tuts.front()->key ==
                                 "minicraft.tutorial.start_getting_wood",
            "AdvancementSystem::Tutorials sorted by JSON index");

      const bool prev_tut = io::Settings::GetBool("tutorials");
      const bool prev_qst = io::Settings::GetBool("quests");
      io::Settings::Set("tutorials", true);
      io::Settings::Set("quests", true);

      adv.ResetAll();
      screen::TutorialDisplayHandler::Reset(true);
      Check(screen::TutorialDisplayHandler::InGuide() &&
                !screen::TutorialDisplayHandler::InQuests(),
            "TutorialDisplayHandler suppresses InQuests while InGuide active");

      adv.SetCurrentOngoingTutorial("minicraft.tutorial.getting_wood");
      screen::TutorialDisplayHandler::Reset(false);
      Check(!screen::TutorialDisplayHandler::InGuide() &&
                screen::TutorialDisplayHandler::InTutorial() &&
                !screen::TutorialDisplayHandler::InQuests(),
            "TutorialDisplayHandler::Reset(false) resumes ongoing tutorial "
            "without control guide");

      screen::QuestsDisplay qd;
      io::InputHandler ih;
      ih.Tick(920);
      ih.PushKeyEvents({{"RIGHT", true, false}});
      ih.Tick(940);
      qd.Tick(ih);
      Check(qd.Menus().size() == 5,
            "QuestsDisplay constructs full 5-menu tabbed layout");

      screen::TutorialDisplayHandler::TurnOffTutorials();
      io::Settings::Set("tutorials", prev_tut);
      io::Settings::Set("quests", prev_qst);
    }

    host.get_player = prev_get_player;
    host.save_prefs = prev_save_prefs;
    host.exit_display = prev_exit_display;
    host.set_display = prev_set_display;
    host.get_display = prev_get_display;
    host.add_notification = prev_add_notification;
    host.game_dir = prev_game_dir;
    host.input = prev_input;
    host.fs = prev_fs;
  }

  // 21. Player-object & furniture interaction parity (UseNearby, Pickup, Attack)
  {
    std::cout << "[21/21] Testing player-object interaction parity...\n";

    io::InputHandler io_int;
    game::World world;
    game::Updater updater;
    updater.WireHostBridges(world, io_int, plat, pak, linker, temp_dir, []() {},
                            []() {});
    Check(world.InitWorld(&fs, temp_dir, "InteractWorld", true, int64_t{987654},
                          &io_int, &updater),
          "Init InteractWorld");

    entity::Player* p = world.GetPlayer();
    level::Level* lvl = world.CurrentLevel();
    Check(p != nullptr && lvl != nullptr, "Player and surface level ready");
    if (p && lvl) {
      // Clear a flat grass strip ahead of player
      const int32_t tx = p->x >> 4;
      const int32_t ty = p->y >> 4;
      for (int32_t dx = -2; dx <= 2; ++dx) {
        for (int32_t dy = -2; dy <= 2; ++dy) {
          lvl->SetTile(tx + dx, ty + dy, level::Tiles::kGrass, 0);
        }
      }
      p->x = tx * 16 + 8;
      p->y = ty * 16 + 8;
      p->dir = entity::Direction::kRight;
      p->stamina = p->max_stamina;

      // 21a. Holding FurnitureItem + Attack places Workbench on tile
      auto wb_item = item::Items::Get("Workbench");
      p->active_item = std::shared_ptr<item::Item>(std::move(wb_item));
      io_int.PushKeyEvents({{"C", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"C", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);

      entity::Crafter* placed_wb = nullptr;
      for (const auto& e : lvl->Entities()) {
        if (auto* cr = dynamic_cast<entity::Crafter*>(e.get())) {
          if (cr->GetType() == entity::Crafter::Type::kWorkbench) {
            placed_wb = cr;
            break;
          }
        }
      }
      Check(placed_wb != nullptr && p->active_item == nullptr,
            "Attack with Workbench FurnitureItem places Crafter and clears held item");

      // 21b. Pressing menu/craft in front of Workbench opens Workbench CraftingDisplay (not PlayerInvDisplay)
      // Also returns held active_item back to inventory instead of keeping it outside
      p->AddItemToInventory(item::Items::Get("Wood"));
      Check(p->active_item == nullptr,
            "AddItemToInventory does not duplicate item into active_item");
      p->active_item =
          std::shared_ptr<item::Item>(item::Items::Get("Apple"));
      io_int.PushKeyEvents({{"E", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"E", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      Check(dynamic_cast<screen::CraftingDisplay*>(updater.CurrentDisplay()) !=
                nullptr &&
                p->active_item == nullptr,
            "Menu key in front of Workbench returns active_item and opens Workbench CraftingDisplay");
      updater.SetDisplay(nullptr);
      updater.Tick(world, io_int, plat, temp_dir);

      // 21c. Pressing pickup (V) picks up Workbench into active_item without duplicating
      const size_t inv_before_pickup = p->GetInventory().InvSize();
      io_int.PushKeyEvents({{"V", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"V", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      Check(dynamic_cast<item::FurnitureItem*>(p->active_item.get()) != nullptr &&
                p->GetInventory().InvSize() == inv_before_pickup,
            "Pickup key (V) picks up Workbench into active_item without duplicating in inventory");

      // 21d. Non-empty Chest cannot be picked up with V; E opens ContainerDisplay
      auto chest = std::make_shared<entity::Chest>();
      chest->GetInventory()->Add(item::Items::Get("Coal"));
      lvl->Add(chest, (tx + 1) * 16 + 8, ty * 16 + 8, false);
      lvl->Tick(false);
      io_int.PushKeyEvents({{"V", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"V", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      Check(!chest->removed,
            "Pickup key (V) refuses to pick up non-empty Chest");
      io_int.PushKeyEvents({{"X", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"X", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      Check(dynamic_cast<screen::ContainerDisplay*>(updater.CurrentDisplay()) !=
                nullptr,
            "Menu key (X) in front of Chest opens ContainerDisplay");
      updater.SetDisplay(nullptr);
      updater.Tick(world, io_int, plat, temp_dir);
      chest->Remove();
      lvl->Tick(false);

      // 21e. Food (InteractsWithWorld==false) near Tnt eats food without lighting fuse;
      // bare-handed Attack lights Tnt fuse and breaks unoccupied Boat
      auto press_attack = [&]() {
        io_int.PushKeyEvents({{"C", true, false}});
        updater.Tick(world, io_int, plat, temp_dir);
        io_int.PushKeyEvents({{"C", false, false}});
        updater.Tick(world, io_int, plat, temp_dir);
      };
      auto tnt = std::make_shared<entity::Tnt>();
      lvl->Add(tnt, (tx + 1) * 16 + 8, ty * 16 + 8, false);
      lvl->Tick(false);
      p->hunger = 5;
      p->active_item =
          std::shared_ptr<item::Item>(item::Items::Get("Apple"));
      press_attack();
      Check(p->hunger > 5 && !tnt->fuse_lit,
            "Attack with non-world item (Apple) consumes food without igniting Tnt");
      p->active_item.reset();
      press_attack();
      Check(tnt->fuse_lit,
            "Bare-handed Attack ignites Tnt via Furniture::Interact");
      tnt->Remove();
      lvl->Tick(false);

      lvl->SetTile(tx + 1, ty, level::Tiles::kWater, 0);
      auto boat = std::make_shared<entity::Boat>(entity::Direction::kRight);
      lvl->Add(boat, (tx + 1) * 16 + 8, ty * 16 + 8, false);
      lvl->Tick(false);
      press_attack();
      Check(boat->removed && p->GetBoat() == nullptr,
            "Bare-handed Attack breaks unoccupied Boat instead of mounting it");
    }

    // 22. Player swing/slash render effect parity
    {
      std::cout << "[22/22] Testing player attack slash render effect...\n";
      auto* p = world.GetPlayer();
      p->active_item.reset();
      p->dir = entity::Direction::kDown;
      p->stamina = p->max_stamina;
      while (p->IsAttacking()) {
        p->Tick();
      }

      DummyRenderBackend slash_backend;
      gfx::Screen slash_scr(&linker);
      slash_scr.SetOffset(p->x - gfx::Screen::w / 2,
                          p->y - (gfx::Screen::h - 8) / 2);
      slash_scr.Clear(0);
      p->Render(&slash_scr);
      slash_scr.Flush(&slash_backend);
      const size_t idle_cmds = slash_backend.last_command_count;

      const int32_t walk_before = p->walk_dist;
      io_int.PushKeyEvents({{"C", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"C", false, false}});
      io_int.Tick();
      Check(p->IsAttacking() && p->walk_dist == walk_before + 8,
            "Attack starts slash timer and increments walk_dist by 8");

      slash_scr.Clear(0);
      p->Render(&slash_scr);
      slash_scr.Flush(&slash_backend);
      Check(slash_backend.last_command_count == idle_cmds + 2,
            "Bare-handed Attack renders 2 slash HUD sprites");

      const gfx::SheetRect* hud_rect =
          linker.GetSheet(gfx::SpriteType::kGui, "hud");
      const int32_t xo = p->x - 8 - (p->x - gfx::Screen::w / 2);
      const int32_t yo = p->y - 11 - (p->y - (gfx::Screen::h - 8) / 2);
      int slash_hits = 0;
      for (const auto& cmd : slash_backend.last_commands) {
        if (hud_rect && cmd.type == platform::DrawCmdType::kSprite &&
            cmd.sprite.src_x == hud_rect->atlas_x + 3 * 8 &&
            cmd.sprite.src_y == hud_rect->atlas_y &&
            cmd.sprite.y == yo + 12 &&
            (cmd.sprite.x == xo || cmd.sprite.x == xo + 8)) {
          ++slash_hits;
        }
      }
      Check(slash_hits == 2,
            "Downward attack slash renders left/right hud(3,0) halves at yo+12");

      while (p->IsAttacking()) {
        p->Tick();
      }
      p->active_item =
          std::shared_ptr<item::Item>(item::Items::Get("Wood Sword"));
      io_int.PushKeyEvents({{"C", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"C", false, false}});
      slash_scr.Clear(0);
      p->Render(&slash_scr);
      slash_scr.Flush(&slash_backend);
      Check(p->IsAttacking() &&
                slash_backend.last_command_count == idle_cmds + 3,
            "Sword Attack renders 2 slash HUD sprites + held item sprite");

      while (p->IsAttacking()) {
        p->Tick();
      }
      p->active_item =
          std::shared_ptr<item::Item>(item::Items::Get("Apple"));
      p->hunger = 5;
      io_int.PushKeyEvents({{"C", true, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      io_int.PushKeyEvents({{"C", false, false}});
      io_int.Tick();
      Check(!p->IsAttacking(),
            "Non-world item (Apple) does not trigger attack slash");
    }

    // 23. FurnitureItem 8x8 itemSprite parity in inventory and crafting menus
    {
      std::cout << "[23/23] Testing FurnitureItem 8x8 menu icon parity...\n";
      const char* kFurnitureNames[] = {
          "Workbench", "Furnace", "Oven",      "Anvil",         "Enchanter",
          "Loom",      "Dye Vat", "Chest",     "Dungeon Chest", "Lantern",
          "Iron Lantern", "Gold Lantern", "Composter", "Tnt", "Red Bed",
      };
      DummyRenderBackend icon_backend;
      gfx::Screen icon_scr(&linker);
      for (const char* fname : kFurnitureNames) {
        auto fit = item::Items::Get(fname);
        std::string label =
            std::string("FurnitureItem uses 8x8 kItem sprite: ") + fname;
        Check(fit != nullptr &&
                  fit->sprite.Type() == gfx::SpriteType::kItem &&
                  fit->sprite.GetSheet(linker) != nullptr,
              label.c_str());

        screen::ItemEntry entry(std::shared_ptr<item::Item>(fit->Clone()));
        icon_scr.Clear(0);
        entry.Render(&icon_scr, 16, 24, true);
        icon_scr.Flush(&icon_backend);

        const gfx::SheetRect* expected_sheet = fit->sprite.GetSheet(linker);
        int icon_hits = 0;
        for (const auto& cmd : icon_backend.last_commands) {
          if (expected_sheet && cmd.type == platform::DrawCmdType::kSprite &&
              cmd.sprite.x == 16 && cmd.sprite.y == 24 &&
              cmd.sprite.w == 8 && cmd.sprite.h == 8 &&
              cmd.sprite.src_x == expected_sheet->atlas_x &&
              cmd.sprite.src_y == expected_sheet->atlas_y) {
            ++icon_hits;
          }
        }
        std::string render_label =
            std::string("ItemEntry renders 8x8 icon at (16,24) for ") + fname;
        Check(icon_hits == 1, render_label.c_str());
      }

      item::Recipe wb_rec("Workbench_1", {"Wood_10"});
      screen::RecipeEntry r_entry(&wb_rec);
      icon_scr.Clear(0);
      r_entry.Render(&icon_scr, 20, 32, false);
      icon_scr.Flush(&icon_backend);
      auto wb_item = item::Items::Get("Workbench");
      const gfx::SheetRect* wb_sheet = wb_item->sprite.GetSheet(linker);
      int wb_hits = 0;
      for (const auto& cmd : icon_backend.last_commands) {
        if (wb_sheet && cmd.type == platform::DrawCmdType::kSprite &&
            cmd.sprite.x == 20 && cmd.sprite.y == 32 &&
            cmd.sprite.w == 8 && cmd.sprite.h == 8 &&
            cmd.sprite.src_x == wb_sheet->atlas_x &&
            cmd.sprite.src_y == wb_sheet->atlas_y) {
          ++wb_hits;
        }
      }
      Check(wb_hits == 1,
            "RecipeEntry renders Workbench using 8x8 kItem sprite at (20,32)");
    }

    // 24. Craft input (Z / SHIFT-E fast tap), Workbench menu entries, and recipe unlock save/load
    if (p && lvl) {
      std::cout << "[24/24] Testing craft key input, Workbench recipes, and save/load persistence...\n";

      // Clear all nearby furniture and restore walkable/buildable grass ahead of player.
      for (const auto& e : lvl->Entities()) {
        if (dynamic_cast<entity::Furniture*>(e.get()) != nullptr) {
          e->Remove();
        }
      }
      lvl->PurgeRemovedEntities();
      const int32_t tx24 = p->x >> 4;
      const int32_t ty24 = p->y >> 4;
      for (int32_t dx = -1; dx <= 1; ++dx) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
          lvl->SetTile(tx24 + dx, ty24 + dy, level::Tiles::kGrass, 0);
        }
      }
      updater.SetDisplay(nullptr);
      p->active_item.reset();
      p->stamina = p->max_stamina;

      // 24a. Single-frame Z tap opens personal CraftingDisplay (not PlayerInvDisplay)
      io_int.ReleaseAll();
      io_int.PushKeyEvents({{"Z", true, false}, {"Z", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      auto* personal_craft_z =
          dynamic_cast<screen::CraftingDisplay*>(updater.CurrentDisplay());
      Check(personal_craft_z != nullptr &&
                dynamic_cast<screen::PlayerInvDisplay*>(
                    updater.CurrentDisplay()) == nullptr &&
                !personal_craft_z->Menus().empty() &&
                personal_craft_z->Menus()[0]->GetTitle() ==
                    util::Localization::GetLocalized(
                        "minicraft.displays.crafting"),
            "Fast Z tap opens localized personal CraftingDisplay instead of PlayerInvDisplay");
      updater.SetDisplay(nullptr);
      io_int.ReleaseAll();
      updater.Tick(world, io_int, plat, temp_dir);

      // 24b. Fast SHIFT+E tap where SHIFT releases in same batch still opens CraftingDisplay
      io_int.PushKeyEvents({{"SHIFT", true, false},
                            {"E", true, false},
                            {"SHIFT", false, false},
                            {"E", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      Check(dynamic_cast<screen::CraftingDisplay*>(updater.CurrentDisplay()) !=
                nullptr &&
                dynamic_cast<screen::PlayerInvDisplay*>(
                    updater.CurrentDisplay()) == nullptr,
            "Fast SHIFT+E tap opens personal CraftingDisplay instead of PlayerInvDisplay");
      updater.SetDisplay(nullptr);
      io_int.ReleaseAll();
      updater.Tick(world, io_int, plat, temp_dir);

      // 24c. Place Workbench, unlock Wood recipes, verify Workbench CraftingDisplay has recipes
      p->AddItemToInventory(item::Items::Get("Wood"));
      p->dir = entity::Direction::kRight;
      p->active_item =
          std::shared_ptr<item::Item>(item::Items::Get("Workbench"));
      io_int.PushKeyEvents({{"C", true, false}, {"C", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      updater.Tick(world, io_int, plat, temp_dir);
      Check(!screen::CraftingDisplay::UnlockedRecipes().empty(),
            "Collecting Wood unlocks recipes before saving");

      io_int.PushKeyEvents({{"E", true, false}, {"E", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      auto* wb_disp =
          dynamic_cast<screen::CraftingDisplay*>(updater.CurrentDisplay());
      Check(wb_disp != nullptr && wb_disp->Menus().size() == 3 &&
                wb_disp->Menus()[0]->GetNumOptions() > 0,
            "Interacting with Workbench renders non-empty recipe list");
      const size_t saved_unlock_count =
          screen::CraftingDisplay::UnlockedRecipes().size();
      const int32_t wb_entry_count =
          (wb_disp && !wb_disp->Menus().empty())
              ? wb_disp->Menus()[0]->GetNumOptions()
              : -1;
      updater.SetDisplay(nullptr);
      io_int.ReleaseAll();
      updater.Tick(world, io_int, plat, temp_dir);

      // 24d. Save world, wipe in-memory unlocks, reload world, verify Workbench still shows recipes
      Check(world.SaveCurrentWorld(fs, temp_dir, "InteractWorld"),
            "SaveCurrentWorld succeeds with unlocked recipes");
      world.UnloadWorld(&clock);
      screen::CraftingDisplay::ResetRecipeUnlocks();
      util::AdvancementSystem::Instance().ResetAll();
      Check(screen::CraftingDisplay::UnlockedRecipes().empty(),
            "ResetRecipeUnlocks clears unlocks prior to reload");

      Check(world.InitWorld(&fs, temp_dir, "InteractWorld", false, std::nullopt,
                            &io_int, &updater),
            "Reload InteractWorld succeeds");
      Check(screen::CraftingDisplay::UnlockedRecipes().size() >=
                saved_unlock_count,
            "Reloaded world restores unlocked recipes");

      if (entity::Player* rp = world.GetPlayer()) {
        rp->dir = entity::Direction::kRight;
      }
      io_int.ReleaseAll();
      io_int.PushKeyEvents({{"E", true, false}, {"E", false, false}});
      updater.Tick(world, io_int, plat, temp_dir);
      auto* reloaded_wb =
          dynamic_cast<screen::CraftingDisplay*>(updater.CurrentDisplay());
      Check(reloaded_wb != nullptr && reloaded_wb->Menus().size() == 3 &&
                wb_entry_count > 0 &&
                reloaded_wb->Menus()[0]->GetNumOptions() == wb_entry_count,
            "Workbench in reloaded world displays all unlocked recipes");
      updater.SetDisplay(nullptr);
    }

    // 25. Plantable/TileItem placement guide, tool stamina cost parity, and es-es tool translation
    {
      std::cout << "[25/25] Testing placement guide, tool stamina cost, and item translation...\n";
      entity::Player* p25 = world.GetPlayer();
      level::Level* lvl25 = world.CurrentLevel();
      Check(p25 != nullptr && lvl25 != nullptr, "Player and level active for test 25");
      if (p25 && lvl25) {
        const int prev_mode25 = io::Settings::GetIdx("mode");
        io::Settings::SetIdx("mode", 0);  // Normal mode (non-creative)
        p25->potion_effects.clear();
        for (const auto& e : lvl25->Entities()) {
          if (e && e.get() != p25) {
            e->Remove();
          }
        }
        lvl25->PurgeRemovedEntities();
        p25->dir = entity::Direction::kRight;
        const int32_t cx = p25->x >> 4;
        const int32_t cy = p25->y >> 4;
        lvl25->SetTile(cx, cy, level::Tiles::kGrass, 0);
        lvl25->SetTile(cx + 1, cy, level::Tiles::kGrass, 0);

        // 25a. TileItem target-tile HUD placement guide renders 4 hud(3,2) tiles when not swimming
        DummyRenderBackend guide_backend;
        gfx::Screen guide_scr(&linker);
        const int32_t cam_x = p25->x - gfx::Screen::w / 2;
        const int32_t cam_y = p25->y - (gfx::Screen::h - 8) / 2;
        guide_scr.SetOffset(cam_x, cam_y);
        const gfx::SheetRect* hud_rect =
            linker.GetSheet(gfx::SpriteType::kGui, "hud");

        p25->active_item.reset();
        guide_scr.Clear(0);
        p25->Render(&guide_scr);
        guide_scr.Flush(&guide_backend);
        const int32_t itx = (p25->x + entity::DirX(p25->dir) * 12) >> 4;
        const int32_t ity = (p25->y - 2 + entity::DirY(p25->dir) * 12) >> 4;
        auto count_guide_quads = [&]() {
          int hits = 0;
          for (const auto& cmd : guide_backend.last_commands) {
            if (hud_rect && cmd.type == platform::DrawCmdType::kSprite &&
                cmd.sprite.src_x == hud_rect->atlas_x + 3 * 8 &&
                cmd.sprite.src_y == hud_rect->atlas_y + 2 * 8 &&
                cmd.sprite.w == 8 && cmd.sprite.h == 8 &&
                (cmd.sprite.x == itx * 16 - cam_x ||
                 cmd.sprite.x == itx * 16 + 8 - cam_x) &&
                (cmd.sprite.y == ity * 16 - cam_y ||
                 cmd.sprite.y == ity * 16 + 8 - cam_y)) {
              ++hits;
            }
          }
          return hits;
        };
        Check(count_guide_quads() == 0,
              "Unarmed player does not render TileItem placement guide");

        p25->active_item =
            std::shared_ptr<item::Item>(item::Items::Get("Wheat Seeds"));
        guide_scr.Clear(0);
        p25->Render(&guide_scr);
        guide_scr.Flush(&guide_backend);
        Check(count_guide_quads() == 4,
              "Holding Wheat Seeds (TileItem) renders 4 hud(3,2) guide sprites at interaction tile");

        // Swimming suppresses placement guide
        lvl25->SetTile(cx, cy, level::Tiles::kWater, 0);
        guide_scr.Clear(0);
        p25->Render(&guide_scr);
        guide_scr.Flush(&guide_backend);
        Check(count_guide_quads() == 0,
              "Swimming player suppresses TileItem placement guide");
        lvl25->SetTile(cx, cy, level::Tiles::kGrass, 0);

        // 25b. Tool stamina costs match Java (base 1 + tile interact cost)
        const int32_t tx = (p25->x + 12) >> 4;
        const int32_t ty = (p25->y - 2) >> 4;
        auto do_attack = [&]() {
          io_int.ReleaseAll();
          io_int.PushKeyEvents({{"C", true, false}, {"C", false, false}});
          updater.Tick(world, io_int, plat, temp_dir);
        };

        p25->active_item.reset();
        p25->stamina = 10;
        lvl25->SetTile(tx, ty, level::Tiles::kGrass, 0);
        do_attack();
        Check(p25->stamina == 9, "Bare-hand Attack deducts 1 stamina");

        auto wood_axe =
            std::make_shared<item::ToolItem>(item::ToolType::kAxe, 0);
        p25->active_item = wood_axe;
        p25->stamina = 10;
        lvl25->SetTile(tx, ty, level::Tiles::kTree, 0);
        do_attack();
        Check(p25->stamina == 5,
              "Wood Axe on TreeTile deducts 5 stamina (1 base + 4 tile)");

        auto gem_axe =
            std::make_shared<item::ToolItem>(item::ToolType::kAxe, 4);
        p25->active_item = gem_axe;
        p25->stamina = 10;
        lvl25->SetTile(tx, ty, level::Tiles::kTree, 0);
        do_attack();
        Check(p25->stamina == 9,
              "Gem Axe on TreeTile deducts 1 stamina (1 base + 0 tile)");

        auto wood_pick =
            std::make_shared<item::ToolItem>(item::ToolType::kPickaxe, 0);
        p25->active_item = wood_pick;
        p25->stamina = 10;
        lvl25->SetTile(tx, ty, level::Tiles::kRock, 0);
        do_attack();
        Check(p25->stamina == 4,
              "Wood Pickaxe on RockTile deducts 6 stamina (1 base + 5 tile)");

        auto wood_shovel =
            std::make_shared<item::ToolItem>(item::ToolType::kShovel, 0);
        const int32_t shovel_dur_before = wood_shovel->dur;
        p25->active_item = wood_shovel;
        p25->stamina = 10;
        lvl25->SetTile(tx, ty, level::Tiles::kFarmland, 0);
        do_attack();
        Check(p25->stamina == 5 &&
                  lvl25->GetTile(tx, ty)->id == level::Tiles::kDirt &&
                  wood_shovel->dur == shovel_dur_before - 1,
              "Wood Shovel on Farmland deducts 5 stamina and 1 durability");

        // 25c. Spanish localization translates tools, stackables, and regular items
        util::Localization::Instance().ChangeLanguage("es-es");
        auto w_pick =
            std::make_unique<item::ToolItem>(item::ToolType::kPickaxe, 0);
        auto i_sword =
            std::make_unique<item::ToolItem>(item::ToolType::kSword, 2);
        auto shears_it =
            std::make_unique<item::ToolItem>(item::ToolType::kShears, 2);
        auto apple_it = item::Items::Get("Apple");
        auto seeds_it = item::Items::Get("Wheat Seeds_5");
        Check(w_pick->GetDisplayName() == " Madera Pico" &&
                  i_sword->GetDisplayName() == " Hierro Espada" &&
                  shears_it->GetDisplayName() == " Tijeras" &&
                  apple_it && apple_it->GetDisplayName() == " 1 Manzana" &&
                  seeds_it &&
                  seeds_it->GetDisplayName() == " 5 Semillas de Trigo",
              "ToolItem and item GetDisplayName translate into es-es");
        util::Localization::Instance().ChangeLanguage("en-us");

        io::Settings::SetIdx("mode", prev_mode25);
      }
    }

    // 26. Connected tile quadrant, corner mirror, and singleton parity
    {
      std::cout << "[26/26] Testing connected tile quadrant and corner mirror parity...\n";
      auto& gs = game::GameState::Instance();
      gfx::SpriteLinker* prev_linker = gs.sprite_linker;

      gfx::SpriteLinker tile_linker;
      tile_linker.SetSheet(gfx::SpriteType::kTile, "dirt",
                           {0, 0, 0, 16, 16, true});
      tile_linker.SetSheet(gfx::SpriteType::kTile, "rock",
                           {0, 100, 0, 16, 16, true});
      tile_linker.SetSheet(gfx::SpriteType::kTile, "rock_border",
                           {0, 200, 0, 24, 24, true});
      tile_linker.SetSheet(gfx::SpriteType::kTile, "rock_corner",
                           {0, 300, 0, 16, 16, true});
      tile_linker.SetSheet(gfx::SpriteType::kTile, "stone_wall",
                           {0, 400, 0, 16, 16, true});
      tile_linker.SetSheet(gfx::SpriteType::kTile, "stone_wall_border",
                           {0, 500, 0, 24, 24, true});
      tile_linker.SetSheet(gfx::SpriteType::kTile, "exploded",
                           {0, 600, 0, 16, 16, true});
      tile_linker.SetSheet(gfx::SpriteType::kTile, "exploded_border",
                           {0, 700, 0, 24, 24, true});
      gs.sprite_linker = &tile_linker;

      DummyRenderBackend tile_backend;
      gfx::Screen tile_screen(&tile_linker);
      level::Level t_lvl(32, 32, 0, 0, nullptr, /*skip_gen=*/true);
      auto& t_cm = t_lvl.GetChunkManager();
      for (int32_t ty = 0; ty < 32; ++ty) {
        for (int32_t tx = 0; tx < 32; ++tx) {
          t_cm.SetTile(tx, ty, level::Tiles::kDirt, 0);
        }
      }
      for (int32_t cy = 0; cy <= 1; ++cy) {
        for (int32_t cx = 0; cx <= 1; ++cx) {
          t_cm.SetChunkStage(cx, cy, level::Chunk::kStageFinished);
        }
      }

      // 26a. Outer corner Rock emits 4 dirt bg + 4 exclusive 8x8 quadrants (no base protrusion)
      t_lvl.SetTile(10, 10, level::Tiles::kRock, 0);
      t_lvl.SetTile(11, 10, level::Tiles::kRock, 0);
      t_lvl.SetTile(10, 11, level::Tiles::kRock, 0);
      t_lvl.SetTile(11, 11, level::Tiles::kRock, 0);
      tile_screen.Clear(0);
      tile_screen.SetOffset(10 * 16, 10 * 16);
      level::Tiles::Get(level::Tiles::kRock)->Render(&tile_screen, t_lvl, 10, 10);
      tile_screen.Flush(&tile_backend);
      Check(tile_backend.last_commands.size() == 8 &&
                tile_backend.last_commands[4].sprite.src_x == 200 &&
                tile_backend.last_commands[4].sprite.src_y == 0 &&
                tile_backend.last_commands[4].sprite.w == 8 &&
                tile_backend.last_commands[5].sprite.src_x == 208 &&
                tile_backend.last_commands[5].sprite.src_y == 0 &&
                tile_backend.last_commands[6].sprite.src_x == 200 &&
                tile_backend.last_commands[6].sprite.src_y == 8 &&
                tile_backend.last_commands[7].sprite.src_x == 100 &&
                tile_backend.last_commands[7].sprite.src_y == 0,
            "Outer-corner Rock renders 4 exclusive 8x8 quadrants without base protrusion");

      // 26b. Inner corners render corner_rect mirrored on both X and Y (bits = 3)
      t_lvl.SetTile(15, 14, level::Tiles::kRock, 0);
      t_lvl.SetTile(15, 16, level::Tiles::kRock, 0);
      t_lvl.SetTile(14, 15, level::Tiles::kRock, 0);
      t_lvl.SetTile(16, 15, level::Tiles::kRock, 0);
      t_lvl.SetTile(15, 15, level::Tiles::kRock, 0);
      tile_screen.Clear(0);
      tile_screen.SetOffset(15 * 16, 15 * 16);
      level::Tiles::Get(level::Tiles::kRock)->Render(&tile_screen, t_lvl, 15, 15);
      tile_screen.Flush(&tile_backend);
      const uint8_t kBothMirrors =
          platform::kFlagMirrorX | platform::kFlagMirrorY;
      Check(tile_backend.last_commands.size() == 8 &&
                tile_backend.last_commands[4].sprite.src_x == 300 &&
                tile_backend.last_commands[4].sprite.src_y == 0 &&
                (tile_backend.last_commands[4].flags & kBothMirrors) == kBothMirrors &&
                tile_backend.last_commands[5].sprite.src_x == 308 &&
                tile_backend.last_commands[5].sprite.src_y == 0 &&
                (tile_backend.last_commands[5].flags & kBothMirrors) == kBothMirrors &&
                tile_backend.last_commands[6].sprite.src_x == 300 &&
                tile_backend.last_commands[6].sprite.src_y == 8 &&
                (tile_backend.last_commands[6].flags & kBothMirrors) == kBothMirrors &&
                tile_backend.last_commands[7].sprite.src_x == 308 &&
                tile_backend.last_commands[7].sprite.src_y == 8 &&
                (tile_backend.last_commands[7].flags & kBothMirrors) == kBothMirrors,
            "Inner-corner Rock quadrants render rock_corner mirrored X|Y");

      // 26c. Surrounded singleton Rock swaps 8x8 base quadrants matching SpriteAnimation
      t_lvl.SetTile(14, 14, level::Tiles::kRock, 0);
      t_lvl.SetTile(16, 14, level::Tiles::kRock, 0);
      t_lvl.SetTile(14, 16, level::Tiles::kRock, 0);
      t_lvl.SetTile(16, 16, level::Tiles::kRock, 0);
      tile_screen.Clear(0);
      tile_screen.SetOffset(15 * 16, 15 * 16);
      level::Tiles::Get(level::Tiles::kRock)->Render(&tile_screen, t_lvl, 15, 15);
      tile_screen.Flush(&tile_backend);
      Check(tile_backend.last_commands.size() == 8 &&
                tile_backend.last_commands[4].sprite.src_x == 108 &&
                tile_backend.last_commands[4].sprite.src_y == 8 &&
                tile_backend.last_commands[5].sprite.src_x == 100 &&
                tile_backend.last_commands[5].sprite.src_y == 8 &&
                tile_backend.last_commands[6].sprite.src_x == 108 &&
                tile_backend.last_commands[6].sprite.src_y == 0 &&
                tile_backend.last_commands[7].sprite.src_x == 100 &&
                tile_backend.last_commands[7].sprite.src_y == 0,
            "Surrounded singleton Rock renders swapped 8x8 base quadrants");

      // 26d. Non-singleton connected WallTile and ExplodedTile use border_rect (1,1)
      for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
          t_lvl.SetTile(20 + dx, 20 + dy, level::Tiles::kStoneWall, 0);
          t_lvl.SetTile(25 + dx, 25 + dy, level::Tiles::kExplode, 0);
        }
      }
      tile_screen.Clear(0);
      tile_screen.SetOffset(20 * 16, 20 * 16);
      level::Tiles::Get(level::Tiles::kStoneWall)
          ->Render(&tile_screen, t_lvl, 20, 20);
      tile_screen.Flush(&tile_backend);
      const size_t n_wall = tile_backend.last_commands.size();
      Check(n_wall >= 4 &&
                tile_backend.last_commands[n_wall - 4].sprite.src_x == 508 &&
                tile_backend.last_commands[n_wall - 4].sprite.src_y == 8 &&
                tile_backend.last_commands[n_wall - 1].sprite.src_x == 508 &&
                tile_backend.last_commands[n_wall - 1].sprite.src_y == 8,
            "Connected StoneWall uses stone_wall_border (1,1) interior");

      tile_screen.Clear(0);
      tile_screen.SetOffset(25 * 16, 25 * 16);
      level::Tiles::Get(level::Tiles::kExplode)
          ->Render(&tile_screen, t_lvl, 25, 25);
      tile_screen.Flush(&tile_backend);
      Check(tile_backend.last_commands.size() == 4 &&
                tile_backend.last_commands[0].sprite.src_x == 708 &&
                tile_backend.last_commands[0].sprite.src_y == 8 &&
                tile_backend.last_commands[3].sprite.src_x == 708 &&
                tile_backend.last_commands[3].sprite.src_y == 8,
            "Connected ExplodedTile uses exploded_border (1,1) interior");

      // 26e. Java ConnectsToSand parity (Lava/Sand/Cactus/Hole/Exploded = true; Water/Path/Farmland/Grass = false)
      Check(level::Tiles::Get(level::Tiles::kSand)->ConnectsToSand(t_lvl, 0, 0) &&
                level::Tiles::Get(level::Tiles::kLava)->ConnectsToSand(t_lvl, 0, 0) &&
                level::Tiles::Get(level::Tiles::kCactus)->ConnectsToSand(t_lvl, 0, 0) &&
                level::Tiles::Get(level::Tiles::kHole)->ConnectsToSand(t_lvl, 0, 0) &&
                level::Tiles::Get(level::Tiles::kExplode)->ConnectsToSand(t_lvl, 0, 0) &&
                !level::Tiles::Get(level::Tiles::kWater)->ConnectsToSand(t_lvl, 0, 0) &&
                !level::Tiles::Get(level::Tiles::kPath)->ConnectsToSand(t_lvl, 0, 0) &&
                !level::Tiles::Get(level::Tiles::kFarmland)->ConnectsToSand(t_lvl, 0, 0) &&
                !level::Tiles::Get(level::Tiles::kGrass)->ConnectsToSand(t_lvl, 0, 0),
            "ConnectsToSand matches Java Minicraft+ across all tile types");

      gs.sprite_linker = prev_linker;
    }

    // 27. Precompiled SPIR-V shaders & GLSL fallbacks exist with valid SPIR-V magic
    {
      std::cout << "[27/27] Testing precompiled SPIR-V shader blobs...\n";
      static constexpr const char* kExpectedShaders[] = {
          "sprite.vert", "sprite.frag", "light.vert", "light.frag",
          "quad.vert",   "dither.frag", "blit.frag"};
      size_t count = 0;
      const auto* all = platform::pc::AllPrecompiledShaders(&count);
      Check(all != nullptr && count == 7,
            "All 7 shaders are embedded from build-time precompilation");
      for (const char* name : kExpectedShaders) {
        const auto* s = platform::pc::FindPrecompiledShader(name);
        const bool valid_spv =
            s != nullptr && s->spv_data != nullptr && s->spv_size >= 20 &&
            (s->spv_size % 4) == 0 && s->spv_data[0] == 0x03 &&
            s->spv_data[1] == 0x02 && s->spv_data[2] == 0x23 &&
            s->spv_data[3] == 0x07;
        const bool valid_glsl =
            s != nullptr && s->glsl_fallback != nullptr &&
            std::string_view(s->glsl_fallback).find("#version 330 core") !=
                std::string_view::npos &&
            std::string_view(s->glsl_fallback).find("void main()") !=
                std::string_view::npos;
        Check(valid_spv && valid_glsl,
              "Shader has valid SPIR-V binary magic (0x07230203) and GLSL fallback");
      }
    }

    // 28. TileItem invalid placement notifications (walls, doors, floors/bricks/planks)
    {
      std::cout << "[28/28] Testing TileItem invalid placement notifications...\n";
      auto& gs = game::GameState::Instance();
      level::Level p_lvl(64, 64, 0, 0, nullptr);
      p_lvl.SetTile(10, 10, level::Tiles::kGrass, 0);
      auto p = std::make_shared<entity::Player>(nullptr, nullptr);
      p->SetLevel(&p_lvl, 10 * 16 + 8, 10 * 16 + 8);

      std::vector<std::string> notifs;
      const auto prev_cb = gs.on_add_notification;
      gs.on_add_notification = [&notifs](const std::string& msg, int32_t) {
        notifs.push_back(msg);
      };

      const std::string prev_lang =
          util::Localization::Instance().SelectedLocaleTag();

      // 28a. English (en-us) messages
      util::Localization::Instance().ChangeLanguage("en-us");
      notifs.clear();
      auto plank_wall = item::Items::Get("Plank Wall");
      auto stone_door = item::Items::Get("Stone Door");
      auto obsidian_wall = item::Items::Get("Obsidian Wall");
      auto plank = item::Items::Get("Plank");
      auto stone_brick = item::Items::Get("Stone Brick");
      auto obsidian_brick = item::Items::Get("Obsidian Brick");
      auto ornate_wood = item::Items::Get("Ornate Wood");
      auto stone_block = item::Items::Get("Stone");
      level::Tile* grass = const_cast<level::Tile*>(level::Tiles::Get(level::Tiles::kGrass));

      Check(plank_wall &&
                !plank_wall->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                        entity::Direction::kDown),
            "Plank Wall fails to place on Grass");
      Check(stone_door &&
                !stone_door->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                        entity::Direction::kDown),
            "Stone Door fails to place on Grass");
      Check(obsidian_wall &&
                !obsidian_wall->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                           entity::Direction::kDown),
            "Obsidian Wall fails to place on Grass");
      Check(plank && !plank->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                        entity::Direction::kDown),
            "Plank fails to place on Grass");
      Check(stone_brick &&
                !stone_brick->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                         entity::Direction::kDown),
            "Stone Brick fails to place on Grass");
      Check(obsidian_brick &&
                !obsidian_brick->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                            entity::Direction::kDown),
            "Obsidian Brick fails to place on Grass");
      Check(ornate_wood &&
                !ornate_wood->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                         entity::Direction::kDown),
            "Ornate Wood fails to place on Grass");
      Check(stone_block &&
                !stone_block->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                         entity::Direction::kDown),
            "Stone fails to place on Grass");

      Check(notifs.size() == 8 &&
                notifs[0] == "Can only be placed on Wood Planks!" &&
                notifs[1] == "Can only be placed on Stone Bricks!" &&
                notifs[2] == "Can only be placed on Obsidian!" &&
                notifs[3] == "Dig a hole first!" &&
                notifs[4] == "Dig a hole first!" &&
                notifs[5] == "Dig a hole first!" &&
                notifs[6] == "Dig a hole first!" &&
                notifs[7] == "Dig a hole first!",
            "Invalid placement notifications match en-us strings");

      // 28b. Spanish (es-es) localized required tile names & prompts
      util::Localization::Instance().ChangeLanguage("es-es");
      notifs.clear();
      Check(!plank_wall->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                    entity::Direction::kDown) &&
                !stone_door->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                        entity::Direction::kDown) &&
                !obsidian_wall->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                           entity::Direction::kDown) &&
                !plank->InteractOn(grass, &p_lvl, 10, 10, p.get(),
                                   entity::Direction::kDown),
            "es-es invalid placements return false and notify");
      Check(notifs.size() == 4 &&
                notifs[0].find("Tablas de Madera") != std::string::npos &&
                notifs[1].find("Ladrillo de piedra") != std::string::npos &&
                notifs[2].find("Obsidiana") != std::string::npos &&
                notifs[3].find("hoyo") != std::string::npos,
            "es-es invalid placement notifications translate required tile and prompt");

      util::Localization::Instance().ChangeLanguage(prev_lang);
      gs.on_add_notification = prev_cb;
    }

    // 29. Save, Sleep, Boss/Statue, DeathChest notifications & 3-notif cap
    {
      std::cout << "[29/29] Testing world save, sleep, boss, and DeathChest notifications...\n";
      auto& gs = game::GameState::Instance();
      const auto prev_add_cb = gs.on_add_notification;
      const auto prev_notify_cb = gs.on_notify_all;
      const std::string prev_lang =
          util::Localization::Instance().SelectedLocaleTag();

      std::vector<std::pair<std::string, int32_t>> events;
      gs.on_notify_all = [&events](const std::string& msg, int32_t delay) {
        events.emplace_back(util::Localization::GetLocalized(msg), delay);
      };
      gs.on_add_notification = [&events](const std::string& msg, int32_t) {
        events.emplace_back(msg, 0);
      };

      // 29a. Save::SaveWorld notifies "World Saved!" / "¡Mundo guardado!"
      level::Level s_lvl(64, 64, 0, 0, nullptr);
      auto sp = std::make_shared<entity::Player>(nullptr, nullptr);
      sp->SetLevel(&s_lvl, 32 * 16 + 8, 32 * 16 + 8);
      std::array<std::unique_ptr<level::Level>, 6> lvls;
      lvls[3] = std::make_unique<level::Level>(64, 64, 0, 0, nullptr);
      int32_t cur_idx = 3;
      saveload::WorldSaveContext sctx;
      sctx.world_dir = temp_dir + "/NotifSaveWorld";
      sctx.levels = &lvls;
      sctx.player = &sp;
      sctx.current_level_idx = &cur_idx;

      util::Localization::Instance().ChangeLanguage("es-es");
      events.clear();
      Check(saveload::Save::SaveWorld(fs, sctx) && !gs.saving,
            "SaveWorld succeeds and clears saving flag");
      Check(!events.empty() && events.back().first == "¡Mundo guardado!",
            "SaveWorld emits es-es world_saved notification");

      // 29b. Bed::Use during morning emits cannot_sleep with Min/Sec
      util::Localization::Instance().ChangeLanguage("en-us");
      const int32_t prev_time = gs.game_time;
      const bool prev_aw = gs.air_wizard_beaten;
      const bool prev_obk_sum = gs.obsidian_knight_summoned;
      gs.SetTimeOfDay(game::TimeOfDay::kMorning);
      entity::Bed bed("Red");
      bed.level = &s_lvl;
      bed.x = 32 * 16 + 8;
      bed.y = 32 * 16 + 8;
      events.clear();
      Check(!bed.Use(*sp) && !events.empty() &&
                events.back().first.find("Can't sleep!") != std::string::npos,
            "Bed::Use during daytime emits localized cannot_sleep");
      gs.game_time = prev_time;

      // 29c. KnightStatue 3-touch sequence & boss_limit when active
      gs.obsidian_knight_summoned = false;
      auto ks = std::make_shared<entity::KnightStatue>(500);
      s_lvl.Add(ks, 20 * 16, 20 * 16, false);
      events.clear();
      Check(ks->Use(*sp) && ks->Use(*sp) && ks->Use(*sp),
            "KnightStatue takes 3 touches to awaken ObsidianKnight");
      Check(events.size() == 3 &&
                events[0].first == "You hear echoed whispers..." &&
                events[1].first == "You hear the statue vibrating..." &&
                events[2].first == "The Obsidian Knight has awoken!",
            "KnightStatue emits statue_tapped, statue_touched, obsidian_knight_awoken");
      entity::KnightStatue ks2(500);
      ks2.level = &s_lvl;
      events.clear();
      Check(!ks2.Use(*sp) && !events.empty() &&
                events[0].first == "No more bosses can be spawned",
            "KnightStatue emits boss_limit when ObsidianKnight already active");
      gs.obsidian_knight_summoned = prev_obk_sum;

      // 29d. AirWizard first defeat emits dungeon_opened (-400); ObsidianKnight::Die localizes
      gs.air_wizard_beaten = false;
      auto aw = std::make_shared<entity::AirWizard>();
      s_lvl.Add(aw, 22 * 16, 22 * 16, false);
      events.clear();
      aw->Die();
      Check(events.size() == 2 &&
                events[0].first == "Air Wizard Defeated!" &&
                events[1].first == "The Dungeon is now open!" &&
                events[1].second == -400,
            "First AirWizard defeat emits air_wizard_defeated + dungeon_opened (-400)");
      auto obk = std::make_shared<entity::ObsidianKnight>(500);
      s_lvl.Add(obk, 23 * 16, 23 * 16, false);
      events.clear();
      obk->Die();
      Check(!events.empty() && events.back().first == "Obsidian Knight: Defeated!",
            "ObsidianKnight defeat emits localized obsidian_knight_defeated");
      gs.air_wizard_beaten = prev_aw;

      // 29e. Updater::AddNotification caps queue at 3
      game::Updater test_updater;
      test_updater.AddNotification("n1");
      test_updater.AddNotification("n2");
      test_updater.AddNotification("n3");
      test_updater.AddNotification("n4");
      Check(test_updater.Notifications().size() == 3 &&
                test_updater.Notifications()[0] == "n2" &&
                test_updater.Notifications()[2] == "n4",
            "Updater keeps at most 3 queued notifications like Java");

      util::Localization::Instance().ChangeLanguage(prev_lang);
      gs.on_add_notification = prev_add_cb;
      gs.on_notify_all = prev_notify_cb;
    }

    // 30. TitleDisplay splash selection uses non-fixed GlobalRandom seed
    {
      std::cout << "[30/30] Testing TitleDisplay splash randomization...\n";
      std::vector<int32_t> picks;
      for (int i = 0; i < 10; ++i) {
        screen::TitleDisplay td;
        td.Init(nullptr);
        // Verify repeated GlobalRandom sequence is not stuck at the old fixed 0x5EEDBEEFCAFELL start.
        picks.push_back(util::GlobalRandom().NextInt(1000000));
      }
      bool all_equal = true;
      for (size_t i = 1; i < picks.size(); ++i) {
        if (picks[i] != picks[0]) {
          all_equal = false;
          break;
        }
      }
      util::Random legacy_fixed(0x5EEDBEEFCAFELL);
      const int32_t first_fixed = legacy_fixed.NextInt(91) + 2;
      (void)first_fixed;
      Check(!all_equal, "GlobalRandom produces varied values across TitleDisplay launches");
    }

    // 31. Time-of-Day, cumulative playtime, and past_day1 Save/Load parity with Java
    {
      std::cout << "[31/31] Testing Time-of-Day, tick_count, and past_day1 Save/Load...\n";
      auto& gs = game::GameState::Instance();
      const int32_t saved_gt = gs.game_time;
      const int32_t saved_tc = gs.tick_count;
      const bool saved_pd1 = gs.past_day1;

      // 31a. C++ save/load preserves Evening/Night time-of-day AND past_day1 after Day 1
      gs.game_time = 51234;  // Night
      gs.tick_count = 72000;  // 20 minutes played (> 65000)
      gs.past_day1 = true;
      const std::string time_world_dir = temp_dir + "/saves/TimeParityWorld";
      fs.CreateDirectories(time_world_dir);
      saveload::WorldSaveContext sctx;
      sctx.world_dir = time_world_dir;
      sctx.levels = &world.Levels();
      auto player_sp = world.Levels()[3]->Players().front();
      sctx.player = &player_sp;
      int32_t cur_lvl = world.CurrentLevelIdx();
      sctx.current_level_idx = &cur_lvl;
      Check(saveload::Save::SaveWorld(fs, sctx), "Save TimeParityWorld");

      // Scrub live state before reload to ensure LoadWorld truly restores it.
      gs.game_time = 0;
      gs.tick_count = 0;
      gs.past_day1 = false;
      std::array<std::unique_ptr<level::Level>, 6> loaded_lvls;
      std::shared_ptr<entity::Player> loaded_player;
      int32_t loaded_lvl_idx = 0;
      saveload::WorldSaveContext lctx;
      lctx.world_dir = time_world_dir;
      lctx.levels = &loaded_lvls;
      lctx.player = &loaded_player;
      lctx.current_level_idx = &loaded_lvl_idx;
      Check(saveload::Load::LoadWorld(fs, lctx), "Load TimeParityWorld");
      Check(gs.game_time == 51234 && gs.GetTimeOfDay() == game::TimeOfDay::kNight,
            "Reload preserves exact time-of-day tick (Night 51234)");
      Check(gs.tick_count == 72000,
            "Reload preserves cumulative tick_count (72000)");
      Check(gs.past_day1,
            "Reload preserves past_day1=true instead of resetting after wrapped Day 1");
      Check(screen::InfoDisplay::GetTimeString() == "20m 00s",
            "InfoDisplay::GetTimeString uses cumulative tick_count like Java gameTime");

      // 31b. Sleeping on Day 1 (past_day1=true while tick_count < 65000) also persists across save/load
      gs.game_time = 1000;
      gs.tick_count = 40000;
      gs.past_day1 = true;
      Check(saveload::Save::SaveWorld(fs, sctx), "Save TimeParityWorld after Day 1 sleep");
      gs.past_day1 = false;
      Check(saveload::Load::LoadWorld(fs, lctx), "Load TimeParityWorld after Day 1 sleep");
      Check(gs.game_time == 1000 && gs.tick_count == 40000 && gs.past_day1,
            "Reload after Day 1 sleep preserves Morning tick, playtime, and past_day1=true");

      // 31c. Java save at Night (54321) with total gameTime=90000 (>65000) preserves both independently
      const std::string time_java_dir = temp_dir + "/saves/TimeJavaWorld";
      fs.CreateDirectories(time_java_dir);
      fs.WriteTextFile(time_java_dir + "/Game.miniplussave",
                       "2.2.4,123456,0,54321,90000,1,false,false,false,false,");
      lctx.world_dir = time_java_dir;
      Check(saveload::Load::LoadWorld(fs, lctx), "Load Java-style Game.miniplussave at Night");
      Check(gs.game_time == 54321 && gs.GetTimeOfDay() == game::TimeOfDay::kNight,
            "Java save Night tickCount (54321) preserved without gameTime overwrite");
      Check(gs.tick_count == 90000 && gs.past_day1,
            "Java save gameTime (90000) restores tick_count and past_day1=true");

      gs.game_time = saved_gt;
      gs.tick_count = saved_tc;
      gs.past_day1 = saved_pd1;
    }

    // 32. Bed sleep fast-forward to Morning (kSleepEndTime = 8100) and ESC cancel parity with Java
    {
      std::cout << "[32/32] Testing Bed sleep fast-forward to morning and cancel parity...\n";
      auto& gs = game::GameState::Instance();
      const int32_t saved_gt = gs.game_time;
      const bool saved_pd1 = gs.past_day1;
      const bool saved_sleep = gs.bed_sleeping;
      const int32_t saved_sp = gs.bed_sleep_players;
      const float saved_spd = gs.game_speed;
      const bool saved_paused = gs.paused;
      const std::string prev_mode = io::Settings::GetString("mode");

      io::Settings::Set("mode", std::string("minicraft.settings.mode.survival"));
      entity::Player* wp = world.GetPlayer();
      level::Level* wl = world.CurrentLevel();
      Check(wp != nullptr && wl != nullptr, "World has active player and surface level");

      entity::Bed bed("Red");
      bed.level = wl;
      bed.x = wp->x;
      bed.y = wp->y;

      // 32a. Early Evening before kSleepStartTime (35000 < 40500) cannot sleep in Survival
      gs.game_time = 35000;
      gs.past_day1 = false;
      Check(!bed.Use(*wp) && !gs.bed_sleeping,
            "Bed::Use before kSleepStartTime (40500) blocks sleep like Java");

      // 32b. Night (50000 >= 40500) enters sleep at 20x speed and fast-forwards to kSleepEndTime (8100)
      gs.game_time = 50000;
      gs.past_day1 = false;
      gs.paused = false;
      Check(bed.Use(*wp) && gs.bed_sleeping && gs.game_speed == 20.0f,
            "Bed::Use at Night starts sleeping at 20x game_speed");

      // Player does not take damage while asleep in bed
      const int32_t hp_before = wp->health;
      wp->DoHurt(2, entity::Direction::kDown);
      Check(wp->health == hp_before, "Player in bed is invulnerable like Java Bed.inBed");

      // Run ticks until waking up (1 tick to pass midnight -> 8100 ticks to morning)
      for (int32_t i = 0; i < 8200 && gs.bed_sleeping; ++i) {
        updater.Tick(world, input, plat, temp_dir);
      }
      Check(!gs.bed_sleeping && gs.game_speed == 1.0f && gs.past_day1,
            "Updater::Tick wakes player at normal speed with past_day1=true");
      Check(gs.game_time >= game::GameState::kSleepEndTime &&
                gs.game_time <= game::GameState::kSleepEndTime + 2 &&
                gs.GetTimeOfDay() == game::TimeOfDay::kMorning,
            "Sleeping fast-forwards to kSleepEndTime (8100) in bright Morning like Java");

      // 32c. Early pre-dawn Morning (< 8100 when past_day1) can sleep, and ESC cancels without PauseDisplay
      gs.game_time = 2000;
      gs.past_day1 = true;
      Check(bed.Use(*wp) && gs.bed_sleeping && gs.game_speed == 20.0f,
            "Bed::Use after midnight (< 8100 with past_day1) enters sleep like Java");
      input.PushKeyEvents({{"ESCAPE", true}});
      updater.Tick(world, input, plat, temp_dir);
      input.PushKeyEvents({{"ESCAPE", false}});
      Check(!gs.bed_sleeping && gs.game_speed == 1.0f && !updater.HasActiveDisplay(),
            "Pressing ESC cancels bed sleep without opening PauseDisplay");

      io::Settings::Set("mode", prev_mode);
      gs.game_time = saved_gt;
      gs.past_day1 = saved_pd1;
      gs.bed_sleeping = saved_sleep;
      gs.bed_sleep_players = saved_sp;
      gs.game_speed = saved_spd;
      gs.paused = saved_paused;
    }

    // 33. Background-threaded world loading in LoadingDisplay
    {
      std::cout << "[33/33] Testing background-threaded world loading in LoadingDisplay...\n";
      auto& host = screen::DisplayHost::Instance();
      const auto prev_start = host.start_world;
      const auto prev_save = host.save_world;

      const std::thread::id main_tid = std::this_thread::get_id();
      std::thread::id worker_tid;
      std::atomic<bool> worker_started{false};
      std::atomic<bool> allow_finish{false};
      int32_t save_calls = 0;

      host.start_world = [&](const std::string& /*world_name*/, bool /*is_new*/,
                             std::optional<int64_t> /*seed*/) -> bool {
        worker_tid = std::this_thread::get_id();
        screen::LoadingDisplay::SetPercentage(42.0f);
        worker_started.store(true, std::memory_order_release);
        while (!allow_finish.load(std::memory_order_acquire)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        screen::LoadingDisplay::SetPercentage(100.0f);
        return true;
      };
      host.save_world = [&]() { ++save_calls; };

      updater.SetDisplay(std::make_unique<screen::LoadingDisplay>("AsyncWorld", true));
      // Process Push + 2 delay_ticks_ + worker launch tick; main thread must return immediately.
      for (int32_t i = 0; i < 4; ++i) {
        updater.Tick(world, input, plat, temp_dir);
      }
      for (int32_t w = 0; w < 200 && !worker_started.load(std::memory_order_acquire); ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Check(worker_started.load(std::memory_order_acquire),
            "LoadingDisplay starts start_world in background thread");
      Check(worker_tid != main_tid,
            "start_world executes off the main UI thread");
      Check(updater.HasActiveDisplay(),
            "LoadingDisplay remains open and main thread keeps ticking while worker runs");
      gfx::Screen ld_screen(&linker);
      if (const screen::Display* cur = updater.CurrentDisplay()) {
        cur->Render(&ld_screen);
      }
      Check(screen::LoadingDisplay::GetPercentage() >= 42.0f,
            "Progress percentage is safely readable on main thread during load");

      allow_finish.store(true, std::memory_order_release);
      for (int32_t w = 0; w < 500 && updater.HasActiveDisplay(); ++w) {
        updater.Tick(world, input, plat, temp_dir);
        if (updater.HasActiveDisplay()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
      Check(!updater.HasActiveDisplay() && save_calls == 1,
            "LoadingDisplay closes and saves newly generated world once worker finishes");

      // Also verify failed load exits cleanly without calling save_world
      host.start_world = [&](const std::string&, bool, std::optional<int64_t>) -> bool {
        return false;
      };
      updater.SetDisplay(std::make_unique<screen::TitleDisplay>());
      updater.Tick(world, input, plat, temp_dir);
      updater.SetDisplay(std::make_unique<screen::LoadingDisplay>("BrokenWorld", false));
      for (int32_t w = 0; w < 500; ++w) {
        updater.Tick(world, input, plat, temp_dir);
        if (dynamic_cast<screen::TitleDisplay*>(updater.CurrentDisplay()) != nullptr && w >= 3) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Check(dynamic_cast<screen::TitleDisplay*>(updater.CurrentDisplay()) != nullptr &&
                save_calls == 1,
            "Failed background load pops LoadingDisplay back to parent without saving");
      updater.ClearDisplays();

      host.start_world = prev_start;
      host.save_world = prev_save;
    }

    // 34. Background-threaded world saving + flush on UnloadWorld
    {
      std::cout << "[34/34] Testing background-threaded world saving...\n";

      class DelayedTestFileSystem final : public platform::IFileSystem {
       public:
        explicit DelayedTestFileSystem(const platform::IFileSystem& inner)
            : inner_(inner) {}

        std::string GetDefaultGameDir() const override {
          return inner_.GetDefaultGameDir();
        }
        bool Exists(std::string_view p) const override {
          return inner_.Exists(p);
        }
        bool IsDirectory(std::string_view p) const override {
          return inner_.IsDirectory(p);
        }
        bool CreateDirectories(std::string_view p) const override {
          return inner_.CreateDirectories(p);
        }
        bool RemoveFile(std::string_view p) const override {
          return inner_.RemoveFile(p);
        }
        bool RemoveDirectoryAll(std::string_view p) const override {
          return inner_.RemoveDirectoryAll(p);
        }
        bool Rename(std::string_view a, std::string_view b) const override {
          return inner_.Rename(a, b);
        }
        bool ReadBinaryFile(std::string_view p,
                            std::vector<uint8_t>* out) const override {
          return inner_.ReadBinaryFile(p, out);
        }
        bool WriteBinaryFile(std::string_view p, const uint8_t* data,
                             size_t size) const override {
          if (gate_enabled.load(std::memory_order_acquire) &&
              p.find("world.mcbin") != std::string_view::npos) {
            write_thread_id = std::this_thread::get_id();
            write_entered.store(true, std::memory_order_release);
            while (!allow_write.load(std::memory_order_acquire)) {
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
          }
          return inner_.WriteBinaryFile(p, data, size);
        }
        bool ReadTextFile(std::string_view p, std::string* out) const override {
          return inner_.ReadTextFile(p, out);
        }
        bool WriteTextFile(std::string_view p,
                           std::string_view text) const override {
          return inner_.WriteTextFile(p, text);
        }
        std::vector<std::string> ListDirectory(
            std::string_view p) const override {
          return inner_.ListDirectory(p);
        }
        bool SaveScreenshotPng(std::string_view p, int32_t w, int32_t h,
                               const uint8_t* rgba) const override {
          return inner_.SaveScreenshotPng(p, w, h, rgba);
        }
        bool DecodePngFromMemory(
            const uint8_t* bytes, size_t size, int32_t* w, int32_t* h,
            std::vector<uint8_t>* rgba) const override {
          return inner_.DecodePngFromMemory(bytes, size, w, h, rgba);
        }

        const platform::IFileSystem& inner_;
        mutable std::atomic<bool> gate_enabled{true};
        mutable std::atomic<bool> write_entered{false};
        mutable std::atomic<bool> allow_write{false};
        mutable std::thread::id write_thread_id{};
      };

      auto& gs = game::GameState::Instance();
      const std::thread::id main_tid = std::this_thread::get_id();
      Check(world.InitWorld(&fs, temp_dir, "AsyncSaveWorld", true,
                            int64_t{12345}, &input, &updater),
            "InitWorld succeeds for AsyncSaveWorld");
      updater.ResetAutosaveAndNotifications();

      DelayedTestFileSystem delayed_fs(fs);
      Check(world.SaveCurrentWorldAsync(delayed_fs, temp_dir,
                                        "AsyncSaveWorld", true),
            "SaveCurrentWorldAsync launches background save");
      Check(world.IsSaving() && gs.saving.load(),
            "World::IsSaving and GameState::saving are true during async save");
      Check(!world.SaveCurrentWorldAsync(delayed_fs, temp_dir,
                                         "AsyncSaveWorld", true),
            "Overlapping SaveCurrentWorldAsync call is ignored cleanly");

      for (int32_t w = 0;
           w < 200 && !delayed_fs.write_entered.load(std::memory_order_acquire);
           ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Check(delayed_fs.write_entered.load(std::memory_order_acquire) &&
                delayed_fs.write_thread_id != main_tid,
            "Save worker writes files off the main thread");

      delayed_fs.allow_write.store(true, std::memory_order_release);
      world.WaitForPendingSave();
      Check(!world.IsSaving() && !gs.saving.load(),
            "Saving flags reset cleanly when background save finishes");
      Check(screen::LoadingDisplay::GetPercentage() >= 99.9f,
            "Async save updates completion percentage to 100%");
      Check(!updater.Notifications().empty(),
            "Async save emits World Saved notification on completion");

      // Silent background save (used on new-world initial save) skips notification.
      updater.ResetAutosaveAndNotifications();
      Check(world.SaveCurrentWorldAsync(fs, temp_dir, "AsyncSaveWorld", false),
            "Silent SaveCurrentWorldAsync succeeds");
      world.WaitForPendingSave();
      Check(updater.Notifications().empty(),
            "Silent SaveCurrentWorldAsync does not queue save notification");

      // Trigger async save right before UnloadWorld (save_quit pattern):
      // UnloadWorld must flush/join background save cleanly before unloading.
      delayed_fs.write_entered.store(false, std::memory_order_release);
      delayed_fs.allow_write.store(false, std::memory_order_release);
      Check(world.SaveCurrentWorldAsync(delayed_fs, temp_dir,
                                        "AsyncSaveWorld", false),
            "SaveCurrentWorldAsync starts before immediate UnloadWorld");
      std::thread releaser([&delayed_fs]() {
        for (int32_t w = 0;
             w < 200 && !delayed_fs.write_entered.load(std::memory_order_acquire);
             ++w) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        delayed_fs.allow_write.store(true, std::memory_order_release);
      });
      world.UnloadWorld(&clock);
      releaser.join();
      Check(!world.IsSaving() &&
                fs.Exists(saveload::Save::JoinWorldPath(
                    saveload::Save::JoinWorldPath(temp_dir, "saves/AsyncSaveWorld"),
                    "world.mcbin")),
            "UnloadWorld joins pending background save cleanly before clearing world");
    }

    // 35. Passive mobs cannot walk/swim onto water (Java PassiveMob parity)
    {
      std::cout << "[35/35] Testing passive mob water/lava/hole blocking parity...\n";
      level::Level test_lvl(32, 32, 4242, 0, nullptr, true);
      for (int32_t ty = 8; ty <= 14; ++ty) {
        for (int32_t tx = 8; tx <= 14; ++tx) {
          test_lvl.SetTile(tx, ty, level::Tiles::kGrass, 0);
        }
      }
      // Wall off tx=11 with water, lava, and hole rows.
      test_lvl.SetTile(11, 10, level::Tiles::kWater, 0);
      test_lvl.SetTile(11, 11, level::Tiles::kLava, 0);
      test_lvl.SetTile(11, 12, level::Tiles::kHole, 0);

      auto cow = std::make_shared<entity::Cow>();
      auto pig = std::make_shared<entity::Pig>();
      auto sheep = std::make_shared<entity::Sheep>();
      Check(!cow->CanSwim() && !pig->CanSwim() && !sheep->CanSwim(),
            "Cow, Pig, and Sheep all report CanSwim()==false like Java");

      // Place Cow just left of Water tile (tile 10 ends at px 175, radius xr=4).
      cow->x = 10 * 16 + 11;  // 171 -> right hitbox is 175 (edge of grass)
      cow->y = 10 * 16 + 8;   // tile 10
      test_lvl.Add(cow);
      test_lvl.FlushPendingEntities();
      for (int32_t step = 0; step < 16; ++step) {
        cow->Move(1, 0);
      }
      Check((cow->x + cow->xr) < 11 * 16,
            "Cow cannot step into adjacent WaterTile");

      // Place Pig just left of LavaTile and Sheep just left of HoleTile.
      pig->x = 10 * 16 + 11;
      pig->y = 11 * 16 + 8;
      sheep->x = 10 * 16 + 11;
      sheep->y = 12 * 16 + 8;
      test_lvl.Add(pig);
      test_lvl.Add(sheep);
      test_lvl.FlushPendingEntities();
      for (int32_t step = 0; step < 16; ++step) {
        pig->Move(1, 0);
        sheep->Move(1, 0);
      }
      Check((pig->x + pig->xr) < 11 * 16 && (sheep->x + sheep->xr) < 11 * 16,
            "Passive mobs also cannot step into LavaTile or HoleTile");
    }

    // 36. Java parity audit fixes: crops, shears, potions, combat, boat, bosses
    {
      std::cout << "[36/37] Testing Java parity fixes (items, combat, boat, bosses)...\n";
      const std::string prev_mode = io::Settings::GetString("mode");
      const size_t prev_diff = io::Settings::GetIdx("diff");
      io::Settings::Set("mode", std::string("minicraft.settings.mode.survival"));
      io::Settings::SetIdx("diff", 1);

      level::Level plvl(64, 64, 1, 0, nullptr, /*skip_gen=*/true);
      for (int32_t ty = 0; ty < 32; ++ty) {
        for (int32_t tx = 0; tx < 32; ++tx) {
          plvl.SetTile(tx, ty, level::Tiles::kGrass, 0);
        }
      }
      io::InputHandler p_in;
      auto tp = std::make_shared<entity::Player>(nullptr, &p_in);
      plvl.Add(tp, 160, 160, false);
      plvl.Tick(false);

      // 36a. Crop planting preserves Farmland moisture and starts at stage 0
      plvl.SetTile(12, 10, level::Tiles::kFarmland, 5);
      plvl.SetTile(13, 10, level::Tiles::kFarmland, 5);
      auto wheat_seeds = item::Items::Get("Wheat Seeds");
      auto potato = item::Items::Get("Potato");
      Check(wheat_seeds->InteractOn(
                const_cast<level::Tile*>(plvl.GetTile(12, 10)), &plvl, 12, 10,
                tp.get(), entity::Direction::kRight) &&
                plvl.GetTile(12, 10)->id == level::Tiles::kWheat &&
                plvl.GetData(12, 10) == 5,
            "Wheat Seeds planting preserves Farmland moisture and starts at age 0");
      Check(potato->InteractOn(
                const_cast<level::Tile*>(plvl.GetTile(13, 10)), &plvl, 13, 10,
                tp.get(), entity::Direction::kRight) &&
                plvl.GetTile(13, 10)->id == level::Tiles::kPotato &&
                plvl.GetData(13, 10) == 5,
            "Potato planting (keep_data) preserves Farmland moisture and starts at age 0");

      // 36b. Shears dur=42/level=0, Sheep shear durability & DyeItem, 9 spawners & 5 creative tiles
      auto shears_reg = item::Items::Get("Shears");
      auto* shears_ptr = dynamic_cast<item::ToolItem*>(shears_reg.get());
      Check(shears_ptr != nullptr && shears_ptr->level == 0 && shears_ptr->dur == 42,
            "Registered Shears uses level=0 and durability=42");
      auto sheep = std::make_shared<entity::Sheep>(item::DyeColor::kWhite);
      plvl.Add(sheep, 180, 160, false);
      plvl.Tick(false);
      Check(sheep->Interact(*tp, shears_ptr, entity::Direction::kRight) &&
                sheep->cut && shears_ptr->dur == 41,
            "Shearing Sheep consumes 1 Shears durability");
      auto blue_dye = item::Items::Get("Blue Dye");
      auto* dye_st = dynamic_cast<item::StackableItem*>(blue_dye.get());
      dye_st->count = 2;
      Check(sheep->Interact(*tp, blue_dye.get(), entity::Direction::kRight) &&
                sheep->color == item::DyeColor::kBlue && dye_st->count == 1,
            "DyeItem dyes Sheep and decrements dye stack count");
      Check(item::Items::Get("Cow Spawner", true) != nullptr &&
                item::Items::Get("Knight Spawner", true) != nullptr &&
                item::Items::Get("Farmland", true) != nullptr &&
                item::Items::Get("Water", true) != nullptr,
            "9 Mob Spawner items and 5 Creative TileItems are registered");

      // 36c. Potion remove matches Player::PotionEffect indices (no off-by-1)
      tp->AddPotionEffect(entity::Player::PotionEffect::kSpeed, 100);
      tp->AddPotionEffect(entity::Player::PotionEffect::kLight, 100);
      item::ApplyPotionEffect(tp.get(), item::PotionType::kSpeed, false);
      Check(tp->potion_effects.count(0) == 0 && tp->potion_effects.count(1) == 1,
            "ApplyPotionEffect(kSpeed, false) removes kSpeed(0) leaving kLight(1)");

      // 36d. Player can still move while hurt_time > 0, and Mob takes Lava damage
      tp->x = 160;
      tp->y = 160;
      tp->DoHurt(1, entity::Direction::kNone);
      const int32_t hurt_x0 = tp->x;
      Check(tp->hurt_time == 30 && tp->Move(1, 0) && tp->x > hurt_x0,
            "Player is not frozen while hurt_time > 0");

      auto zom = std::make_shared<entity::Zombie>(1);
      plvl.SetTile(15, 10, level::Tiles::kLava, 0);
      plvl.Add(zom, 15 * 16 + 8, 10 * 16 + 8, false);
      plvl.Tick(false);
      const int32_t z_hp0 = zom->health;
      zom->Tick();
      Check(zom->health == z_hp0 - 4, "EnemyMob standing on Lava takes 4 damage per Mob::Tick");

      // 36e. Armor absorption uses cur_armor->level & DeathChest preserves active_item + cur_armor
      tp->hurt_time = 0;
      tp->health = 10;
      tp->armor = 50;
      tp->armor_damage_buffer = 0;
      auto gem_arm = item::Items::Get("Gem Armor");
      tp->cur_armor.reset(dynamic_cast<item::ArmorItem*>(gem_arm.release()));
      tp->DoHurt(2, entity::Direction::kNone);
      Check(tp->armor == 48 && tp->armor_damage_buffer == 2 && tp->health == 10,
            "Gem Armor (level=5) absorbs 2 damage cleanly without forced 1 HP loss");

      tp->GetInventory().ClearInv();
      tp->active_item = std::shared_ptr<item::Item>(item::Items::Get("Wood Sword"));
      tp->SetScore(300);
      tp->Die();
      plvl.Tick(false);
      bool found_dc = false;
      bool dc_has_sword = false;
      bool dc_has_armor = false;
      for (const auto& e : plvl.Entities()) {
        if (auto* dc = dynamic_cast<entity::DeathChest*>(e.get())) {
          found_dc = true;
          auto ws = item::Items::Get("Wood Sword");
          auto ga = item::Items::Get("Gem Armor");
          dc_has_sword = dc->GetInventory()->Count(*ws) >= 1;
          dc_has_armor = dc->GetInventory()->Count(*ga) >= 1;
        }
      }
      Check(found_dc && dc_has_sword && dc_has_armor && tp->GetScore() == 200,
            "Player::Die drops active_item+cur_armor into DeathChest and deducts score/3");

      // 36f. Boat dismounts to adjacent safe land instead of dropping into water
      auto bp = std::make_shared<entity::Player>(nullptr, &p_in);
      plvl.SetTile(18, 10, level::Tiles::kWater, 0);
      plvl.SetTile(17, 10, level::Tiles::kWater, 0);
      plvl.SetTile(19, 10, level::Tiles::kWater, 0);
      plvl.SetTile(18, 11, level::Tiles::kWater, 0);
      plvl.SetTile(18, 9, level::Tiles::kGrass, 0);
      auto boat = std::make_shared<entity::Boat>(entity::Direction::kRight);
      plvl.Add(bp, 18 * 16 + 8, 10 * 16 + 8, false);
      plvl.Add(boat, 18 * 16 + 8, 10 * 16 + 8, false);
      plvl.Tick(false);
      boat->Use(*bp);
      bp->ExitBoat();
      Check((bp->y >> 4) != 10 && plvl.GetTile(bp->x >> 4, bp->y >> 4)->id == level::Tiles::kGrass,
            "Player::ExitBoat places player onto adjacent land tile");

      // 36g. ObsidianKnight default HP=5000, Phase 0 blocks Arrows, BossRoom regen avoids duplicate statue, Lantern radii
      auto boss = std::make_shared<entity::ObsidianKnight>();
      Check(boss->max_health == 5000, "ObsidianKnight default max_health is 5000");
      entity::Arrow p_arr(bp.get(), entity::Direction::kRight, 4);
      Check(boss->CalculateEntityDamage(&p_arr, 10) == 0 && p_arr.removed,
            "ObsidianKnight Phase 0 deflects and removes Arrows");
      Check(entity::Lantern(entity::Lantern::Type::kNorm).GetLightRadius() == 9 &&
                entity::Lantern(entity::Lantern::Type::kIron).GetLightRadius() == 12 &&
                entity::Lantern(entity::Lantern::Type::kGold).GetLightRadius() == 15,
            "Lantern light radii match Java 9/12/15");
      level::Level d4(64, 64, 42, -4, nullptr, /*skip_gen=*/true);
      level::Structure::DungeonBossRoom().Draw(d4, 32, 32);
      d4.Tick(false);
      d4.RegenerateBossRoom();
      d4.Tick(false);
      int32_t statue_cnt = 0;
      for (const auto& e : d4.Entities()) {
        if (e && !e->removed && dynamic_cast<entity::KnightStatue*>(e.get())) {
          ++statue_cnt;
        }
      }
      Check(statue_cnt == 1, "RegenerateBossRoom does not spawn duplicate KnightStatue");

      io::Settings::Set("mode", prev_mode);
      io::Settings::SetIdx("diff", prev_diff);
    }

    // 37. Tile, HUD/Updater & Java 2.3 chunked save compatibility tests
    {
      std::cout << "[37/37] Testing tiles, HUD toggles, and Java save/load parity...\n";
      auto& gs = game::GameState::Instance();

      // 37a. Surface explosion turns ExplodedTile to HoleTile; Stairs blocks Furniture
      level::Level surf(32, 32, 1, 0, nullptr, /*skip_gen=*/true);
      surf.SetTile(10, 10, level::Tiles::kExplode, 0);
      auto ticker = std::make_shared<entity::ExplosionTileTicker>(10, 10, 1);
      surf.Add(ticker);
      surf.Tick(false);
      ticker->Tick();
      Check(surf.GetTile(10, 10)->id == level::Tiles::kHole,
            "Surface ExplosionTileTicker turns kExplode into kHole");
      surf.SetTile(11, 10, level::Tiles::kStairsUp, 0);
      entity::Chest ch;
      Check(!surf.GetTile(11, 10)->MayPass(surf, 11, 10, ch),
            "StairsTile::MayPass blocks Furniture");

      // 37b. F1/P/O HUD toggles & paused Updater menu-open tick order
      Check(world.InitWorld(&fs, temp_dir, "HudJavaWorld", true, int64_t{99}, &input, &updater),
            "InitWorld succeeds for HudJavaWorld");
      auto* hp = world.GetPlayer();
      Check(hp != nullptr && hp->render_gui && hp->show_potion_effects && !hp->simp_potion_effects,
            "Player HUD defaults match Java");
      updater.ClearDisplays();
      updater.Tick(world, input, plat, temp_dir);
      input.PushKeyEvents({{"F1", true}, {"P", true}});
      updater.Tick(world, input, plat, temp_dir);
      input.PushKeyEvents({{"F1", false}, {"P", false}});
      for (int i = 0; i < 11; ++i) {
        updater.Tick(world, input, plat, temp_dir);
      }
      input.PushKeyEvents({{"O", true}});
      updater.Tick(world, input, plat, temp_dir);
      input.PushKeyEvents({{"O", false}});
      Check(!hp->render_gui && !hp->show_potion_effects && hp->simp_potion_effects,
            "F1, P, and O hotkeys toggle render_gui, show_potion_effects, and simp_potion_effects");

      const int32_t gt_before = gs.game_time;
      const int32_t tc_before = gs.tick_count;
      updater.SetDisplay(std::make_unique<screen::PauseDisplay>());
      updater.Tick(world, input, plat, temp_dir);
      Check(gs.game_time == gt_before && gs.tick_count == tc_before && gs.paused,
            "Updater::Tick with active display does not advance world time/ticks");
      updater.ClearDisplays();
      updater.Tick(world, input, plat, temp_dir);

      // 37c. Java 2.3.0-dev2+ chunked Level load + Boat RIGHT(-1..3->0..4) + TitleCase Potion + Prefs string coercion
      const std::string j23_dir = temp_dir + "/saves/Java23Chunked";
      fs.CreateDirectories(j23_dir);
      fs.WriteTextFile(j23_dir + "/Game.miniplussave",
                       "2.3.0-dev2,123456,0,4000,10000,1,false,true,true,");
      fs.WriteTextFile(
          j23_dir + "/Player.miniplussave",
          "160,160,10,10,10,10,10,0,0,NULL,42,3,PotionEffects[Speed;4200:Light;3000],20132608,{},");
      fs.WriteTextFile(j23_dir + "/Inventory.miniplussave", "Wood_4,");
      fs.WriteTextFile(j23_dir + "/Entities.miniplussave", "Boat[128:128:3:3],");
      for (int32_t li = 0; li < 6; ++li) {
        const std::string ldir = j23_dir + "/Level" + std::to_string(li);
        fs.CreateDirectories(ldir);
        fs.WriteTextFile(ldir + "/index.miniplussave", "0,0,");
        std::string ct;
        std::string cd;
        ct.reserve(64 * 64 * 6);
        cd.reserve(64 * 64 * 2);
        for (int32_t cx = 0; cx < 64; ++cx) {
          for (int32_t cy = 0; cy < 64; ++cy) {
            ct += (li == 3 && cx == 2 && cy == 5) ? "SAND," : "GRASS,";
            cd += (li == 3 && cx == 2 && cy == 5) ? "4," : "0,";
          }
        }
        fs.WriteTextFile(ldir + "/t.0.0.miniplussave", ct);
        fs.WriteTextFile(ldir + "/d.0.0.miniplussave", cd);
      }

      std::array<std::unique_ptr<level::Level>, 6> j23_lvls;
      std::shared_ptr<entity::Player> j23_p;
      int32_t j23_cur_lvl = 3;
      saveload::SignMap j23_signs;
      saveload::WorldSaveContext j23_ctx;
      j23_ctx.world_dir = j23_dir;
      j23_ctx.levels = &j23_lvls;
      j23_ctx.player = &j23_p;
      j23_ctx.current_level_idx = &j23_cur_lvl;
      j23_ctx.signs = &j23_signs;
      Check(saveload::Load::LoadWorld(fs, j23_ctx),
            "LoadWorld imports Java 2.3.0-dev2+ chunked world");
      Check(j23_lvls[3] && j23_lvls[3]->GetTile(2, 5)->id == level::Tiles::kSand &&
                j23_lvls[3]->GetData(2, 5) == 4,
            "Java 2.3 chunked column-major tile and data loaded accurately");
      Check(j23_p && j23_p->potion_effects[0] == 4200 && j23_p->potion_effects[1] == 3000,
            "Java TitleCase PotionEffects[Speed;...:Light;...] loaded");
      entity::Direction loaded_boat_dir = entity::Direction::kNone;
      for (const auto& e : j23_lvls[3]->Entities()) {
        if (auto* b = dynamic_cast<entity::Boat*>(e.get())) {
          loaded_boat_dir = b->Dir();
        }
      }
      Check(loaded_boat_dir == entity::Direction::kRight,
            "Java Boat dir 3 correctly maps to Direction::kRight");

      // C++ kRight (4) round-trip
      j23_ctx.world_dir = temp_dir + "/saves/CppBoatRoundTrip";
      Check(saveload::Save::SaveWorld(fs, j23_ctx), "SaveWorld with kRight Boat");
      std::array<std::unique_ptr<level::Level>, 6> cpp_lvls;
      std::shared_ptr<entity::Player> cpp_p;
      int32_t cpp_cur_lvl = 3;
      saveload::SignMap cpp_signs;
      saveload::WorldSaveContext cpp_ctx;
      cpp_ctx.world_dir = j23_ctx.world_dir;
      cpp_ctx.levels = &cpp_lvls;
      cpp_ctx.player = &cpp_p;
      cpp_ctx.current_level_idx = &cpp_cur_lvl;
      cpp_ctx.signs = &cpp_signs;
      Check(saveload::Load::LoadWorld(fs, cpp_ctx), "Load C++ Boat round-trip world");
      entity::Direction cpp_boat_dir = entity::Direction::kNone;
      for (const auto& e : cpp_lvls[3]->Entities()) {
        if (auto* b = dynamic_cast<entity::Boat*>(e.get())) {
          cpp_boat_dir = b->Dir();
        }
      }
      Check(cpp_boat_dir == entity::Direction::kRight,
            "C++ Boat Direction::kRight (4) round-trips without clamping to kLeft");

      // Preferences string coercion & unlockedAchievements key
      const std::string pdir = temp_dir + "/java_prefs";
      fs.CreateDirectories(pdir);
      fs.WriteTextFile(pdir + "/Preferences.json",
                       R"({"sound":"false","fps":"75","tutorials":"true"})");
      fs.WriteTextFile(pdir + "/Unlocks.json",
                       R"({"unlockedAchievements":["minicraft.achievement.find_gem"]})");
      const bool prev_sound = io::Settings::GetBool("sound");
      const int32_t prev_fps = io::Settings::GetMaxFps();
      util::AchievementManager::Instance().Reset();
      Check(saveload::Load::LoadPrefs(fs, pdir, nullptr, nullptr),
            "LoadPrefs reads Java string prefs and unlockedAchievements");
      const auto* gem_ach =
          util::AchievementManager::Instance().Find("minicraft.achievement.find_gem");
      Check(!io::Settings::GetBool("sound") && io::Settings::GetMaxFps() == 75 &&
                gem_ach != nullptr && gem_ach->unlocked,
            "Java string Preferences and unlockedAchievements applied");
      io::Settings::Set("sound", prev_sound);
      io::Settings::SetMaxFps(prev_fps);

      // 38. Native C++ binary save format, efficiency, CRC32 integrity, backup recovery & fuzz safety
      std::cout << "[38/38] Testing native binary save format, CRC32 integrity, and backup recovery...\n";
      {
        const std::string bin_world_dir = temp_dir + "/saves/NativeBinWorld";
        std::array<std::unique_ptr<level::Level>, 6> b_lvls;
        static constexpr int32_t kDepths[6] = {-3, -2, -1, 0, 1, -4};
        for (int32_t i = 0; i < 6; ++i) {
          b_lvls[i] = std::make_unique<level::Level>(
              128, 128, 1000 + i, kDepths[i],
              (i < 4 ? nullptr : nullptr), /*skip_gen=*/true);
          // Populate two chunks: one uniform (Grass) and one customized
          b_lvls[i]->SetTile(2, 2, level::Tiles::kGrass, 0);
          if (i == 3) {
            b_lvls[i]->SetTile(70, 70, level::Tiles::kFarmland, 6);
          }
        }
        auto b_player = std::make_shared<entity::Player>(nullptr, nullptr);
        b_player->GetInventory().ClearInv();
        auto wc = std::make_unique<item::WateringCanItem>();
        wc->content = 1234;
        wc->UpdateSprite();
        b_player->GetInventory().Add(std::move(wc));
        auto rod = std::make_unique<item::FishingRodItem>(2);
        rod->uses = 7;
        b_player->GetInventory().Add(std::move(rod));
        auto pick = item::Items::Get("Gem Pickaxe");
        dynamic_cast<item::ToolItem*>(pick.get())->dur = 19;
        b_player->GetInventory().Add(std::move(pick));
        b_player->active_item =
            std::shared_ptr<item::Item>(item::Items::Get("Gold Sword"));
        b_player->SetScore(777);
        b_lvls[3]->Add(b_player, 200, 220, false);

        auto magenta_sheep =
            std::make_shared<entity::Sheep>(item::DyeColor::kMagenta);
        magenta_sheep->cut = true;
        magenta_sheep->health = 4;
        b_lvls[3]->Add(magenta_sheep, 240, 220, false);

        auto comp = std::make_shared<entity::Composter>();
        comp->compost = 5;
        comp->UpdateVisual();
        b_lvls[3]->Add(comp, 260, 220, false);

        auto statue = std::make_shared<entity::KnightStatue>(3456);
        b_lvls[5]->Add(statue, 160, 160, false);
        b_lvls[3]->FlushPendingEntities();
        b_lvls[5]->FlushPendingEntities();

        int32_t b_cur_lvl = 3;
        saveload::SignMap b_signs;
        b_signs[{3, 12, 14}] = {"Binario", "Seguro", "C++", "OK"};
        saveload::WorldSaveContext b_sctx;
        b_sctx.world_dir = bin_world_dir;
        b_sctx.levels = &b_lvls;
        b_sctx.player = &b_player;
        b_sctx.current_level_idx = &b_cur_lvl;
        b_sctx.signs = &b_signs;

        // First save (score = 777)
        Check(saveload::Save::SaveWorld(fs, b_sctx),
              "SaveWorld writes initial native binary world.mcbin");
        const std::string mcbin_path =
            bin_world_dir + "/" + std::string(saveload::kBinaryWorldFileName);
        const std::string mcbak_path =
            bin_world_dir + "/" + std::string(saveload::kBinaryWorldBackupFileName);
        const std::string mctmp_path =
            bin_world_dir + "/" + std::string(saveload::kBinaryWorldTempFileName);
        std::vector<uint8_t> mcbin_bytes;
        Check(fs.ReadBinaryFile(mcbin_path, &mcbin_bytes) &&
                  mcbin_bytes.size() >= saveload::kBinaryPreambleSize &&
                  mcbin_bytes.size() < 65536 &&
                  std::memcmp(mcbin_bytes.data(), saveload::kBinaryMagic.data(),
                              saveload::kBinaryMagic.size()) == 0 &&
                  !fs.Exists(bin_world_dir + "/Game.miniplussave") &&
                  !fs.Exists(mctmp_path),
              "world.mcbin uses compact MCPBSAVE binary format without text clutter or leftover .tmp");

        // Second save (score = 999) rotates first save into world.mcbin.bak
        b_player->SetScore(999);
        Check(saveload::Save::SaveWorld(fs, b_sctx) && fs.Exists(mcbak_path),
              "Second SaveWorld atomically rotates previous healthy save to world.mcbin.bak");

        const auto listed = saveload::Load::ListWorlds(fs, temp_dir + "/saves");
        bool found_bin_meta = false;
        for (const auto& wm : listed) {
          if (wm.folder_name == "NativeBinWorld" && wm.valid && wm.score == 999) {
            found_bin_meta = true;
          }
        }
        Check(found_bin_meta,
              "ListWorlds reads binary world metadata header and exact score (999)");

        // Full round-trip load verification
        std::array<std::unique_ptr<level::Level>, 6> r_lvls;
        std::shared_ptr<entity::Player> r_player;
        int32_t r_cur_lvl = 0;
        saveload::SignMap r_signs;
        saveload::WorldSaveContext r_lctx;
        r_lctx.world_dir = bin_world_dir;
        r_lctx.levels = &r_lvls;
        r_lctx.player = &r_player;
        r_lctx.current_level_idx = &r_cur_lvl;
        r_lctx.signs = &r_signs;
        Check(saveload::Load::LoadWorld(fs, r_lctx) && r_player != nullptr &&
                  r_player->GetScore() == 999 &&
                  r_lvls[3]->GetTile(70, 70)->id == level::Tiles::kFarmland &&
                  r_lvls[3]->GetData(70, 70) == 6,
              "LoadWorld restores binary chunks, tile data, and player score");

        bool ok_wc = false;
        bool ok_rod = false;
        bool ok_pick = false;
        for (size_t ii = 0; ii < r_player->GetInventory().InvSize(); ++ii) {
          const item::Item* it = r_player->GetInventory().Get(ii);
          if (const auto* wci = dynamic_cast<const item::WateringCanItem*>(it)) {
            ok_wc = (wci->content == 1234);
          } else if (const auto* fri =
                         dynamic_cast<const item::FishingRodItem*>(it)) {
            ok_rod = (fri->uses == 7 && fri->level == 2);
          } else if (const auto* ti = dynamic_cast<const item::ToolItem*>(it)) {
            if (ti->Name() == "Gem Pickaxe") ok_pick = (ti->dur == 19);
          }
        }
        bool ok_sheep = false;
        bool ok_comp = false;
        bool ok_statue = false;
        for (const auto& e : r_lvls[3]->Entities()) {
          if (const auto* sh = dynamic_cast<const entity::Sheep*>(e.get())) {
            ok_sheep = (sh->color == item::DyeColor::kMagenta && sh->cut &&
                        sh->health == 4);
          } else if (const auto* cp =
                         dynamic_cast<const entity::Composter*>(e.get())) {
            ok_comp = (cp->compost == 5);
          }
        }
        for (const auto& e : r_lvls[5]->Entities()) {
          if (const auto* ks =
                  dynamic_cast<const entity::KnightStatue*>(e.get())) {
            ok_statue = (ks->BossHp() == 3456);
          }
        }
        Check(ok_wc && ok_rod && ok_pick && ok_sheep && ok_comp && ok_statue &&
                  r_signs[{3, 12, 14}].size() == 4 &&
                  r_signs[{3, 12, 14}][1] == "Seguro",
              "Binary format losslessly preserves item states, dyed Sheep, Composter, KnightStatue, and signs");

        // Bit-flip corruption in primary world.mcbin payload -> automatic recovery from world.mcbin.bak (score=777)
        Check(fs.ReadBinaryFile(mcbin_path, &mcbin_bytes), "Read world.mcbin for corruption test");
        std::vector<uint8_t> bad_payload = mcbin_bytes;
        bad_payload.back() ^= 0x5Au;
        fs.WriteBinaryFile(mcbin_path, bad_payload.data(), bad_payload.size());
        Check(saveload::Load::LoadWorld(fs, r_lctx) && r_player != nullptr &&
                  r_player->GetScore() == 777,
              "CRC32 detects payload bit-flip in world.mcbin and recovers world.mcbin.bak (score=777)");

        // Bit-flip corruption in primary world.mcbin header -> ListWorlds also falls back to world.mcbin.bak
        std::vector<uint8_t> bad_hdr = mcbin_bytes;
        bad_hdr[saveload::kBinaryPreambleSize + 2] ^= 0xFFu;
        fs.WriteBinaryFile(mcbin_path, bad_hdr.data(), bad_hdr.size());
        bool listed_from_bak = false;
        for (const auto& wm : saveload::Load::ListWorlds(fs, temp_dir + "/saves")) {
          if (wm.folder_name == "NativeBinWorld" && wm.valid && wm.score == 777) {
            listed_from_bak = true;
          }
        }
        Check(listed_from_bak,
              "ListWorlds detects header CRC32 corruption and falls back to world.mcbin.bak");

        // Total corruption / truncation / crafted OOM payload -> rejected safely with zero live state mutation
        const std::string corrupt_dir = temp_dir + "/saves/CorruptBinWorld";
        fs.CreateDirectories(corrupt_dir);
        const std::string corrupt_bin =
            corrupt_dir + "/" + std::string(saveload::kBinaryWorldFileName);
        fs.WriteBinaryFile(corrupt_bin, mcbin_bytes.data(), 24);  // truncated preamble
        gs.game_time = 23456;
        saveload::WorldSaveContext bad_ctx = r_lctx;
        bad_ctx.world_dir = corrupt_dir;
        Check(!saveload::Load::LoadWorld(fs, bad_ctx) && gs.game_time == 23456,
              "Truncated binary file is rejected cleanly without mutating GameState");

        // Forged container with valid header & payload CRCs but maliciously huge chunk count (0x3FFFFFFF)
        saveload::BinaryWorldHeader forged_hdr;
        forged_hdr.version_str = std::string(saveload::Version::kCurrentVersion);
        forged_hdr.mode_str = "minicraft.settings.mode.survival";
        forged_hdr.diff_str = "minicraft.settings.diff.normal";
        const auto fhdr_bytes = saveload::SerializeBinaryWorldHeader(forged_hdr);
        saveload::BinaryWriter forged_payload;
        forged_payload.WriteU16(0);        // empty palette
        forged_payload.WriteU8(1);         // level 0 present
        forged_payload.WriteI32(128);      // w
        forged_payload.WriteI32(128);      // h
        forged_payload.WriteI64(1);        // seed
        forged_payload.WriteI32(-3);       // depth
        forged_payload.WriteU32(0x3FFFFFFFu);  // malicious chunk count -> anti-OOM check
        const auto fpay_bytes = forged_payload.TakeBuffer();
        const uint32_t fpay_crc =
            saveload::ComputeCrc32(fpay_bytes.data(), fpay_bytes.size());
        saveload::BinaryWriter forged_file;
        forged_file.WriteBytes(saveload::kBinaryMagic.data(),
                               saveload::kBinaryMagic.size());
        forged_file.WriteU32(saveload::kBinaryFormatVersion);
        forged_file.WriteU32(0u);  // uncompressed
        forged_file.WriteU32(static_cast<uint32_t>(fhdr_bytes.size()));
        forged_file.WriteU32(
            saveload::ComputeCrc32(fhdr_bytes.data(), fhdr_bytes.size()));
        forged_file.WriteU32(static_cast<uint32_t>(fpay_bytes.size()));
        forged_file.WriteU32(static_cast<uint32_t>(fpay_bytes.size()));
        forged_file.WriteU32(fpay_crc);
        forged_file.WriteU32(fpay_crc);
        forged_file.WriteBytes(fhdr_bytes.data(), fhdr_bytes.size());
        forged_file.WriteBytes(fpay_bytes.data(), fpay_bytes.size());
        const auto forged_all = forged_file.TakeBuffer();
        fs.WriteBinaryFile(corrupt_bin, forged_all.data(), forged_all.size());
        Check(!saveload::Load::LoadWorld(fs, bad_ctx) && gs.game_time == 23456,
              "Crafted binary file with malicious counts is rejected without OOM or state mutation");

        // Java folder migration -> saving writes world.mcbin and cleans up legacy .miniplussave files
        j23_ctx.world_dir = j23_dir;
        Check(saveload::Save::SaveWorld(fs, j23_ctx) &&
                  fs.Exists(j23_dir + "/world.mcbin") &&
                  !fs.Exists(j23_dir + "/Game.miniplussave") &&
                  !fs.IsDirectory(j23_dir + "/Level3"),
              "Saving an imported Java world migrates it to world.mcbin and removes legacy text files");
        std::array<std::unique_ptr<level::Level>, 6> mig_lvls;
        std::shared_ptr<entity::Player> mig_p;
        int32_t mig_cur = 0;
        saveload::WorldSaveContext mig_ctx;
        mig_ctx.world_dir = j23_dir;
        mig_ctx.levels = &mig_lvls;
        mig_ctx.player = &mig_p;
        mig_ctx.current_level_idx = &mig_cur;
        Check(saveload::Load::LoadWorld(fs, mig_ctx) &&
                  mig_lvls[3]->GetTile(2, 5)->id == level::Tiles::kSand &&
                  mig_lvls[3]->GetData(2, 5) == 4,
              "Migrated Java -> binary C++ world reloads accurately");

        // Hit-effect SmashParticle centering on tiles (x*16, y*16) and furniture (x-8, y-8)
        level::Level hit_lvl(32, 32, 7, 0, nullptr, /*skip_gen=*/true);
        hit_lvl.SetTile(5, 6, level::Tiles::kTree, 0);
        hit_lvl.GetTile(5, 6)->Hurt(hit_lvl, 5, 6, 3);
        auto hit_statue = std::make_shared<entity::KnightStatue>(5000);
        hit_lvl.Add(hit_statue, 160, 176, false);
        hit_lvl.FlushPendingEntities();
        hit_statue->Hurt(*b_player, 2, entity::Direction::kRight);
        hit_lvl.FlushPendingEntities();
        bool tile_smash_centered = false;
        bool furn_smash_centered = false;
        for (const auto& e : hit_lvl.Entities()) {
          if (dynamic_cast<const entity::SmashParticle*>(e.get())) {
            if (e->x == 5 * 16 && e->y == 6 * 16) tile_smash_centered = true;
            if (e->x == 160 - 8 && e->y == 176 - 8) furn_smash_centered = true;
          }
        }
        Check(tile_smash_centered && furn_smash_centered,
              "SmashParticle spawns aligned to 16x16 tile (x*16, y*16) and furniture (x-8, y-8)");

        // LoadChunk on new chunk (prev_stage == -1) populates parent_level ornate pools and does not overwrite DungeonBossRoom
        level::Level parent_d3(128, 128, 777, -3, nullptr, /*skip_gen=*/true);
        level::Level child_d4(128, 128, 888, -4, &parent_d3, /*skip_gen=*/false);
        // Queue a pending ornate pool in parent_d3 chunk (4, 4) at kStageTerrain (or let LoadChunk generate new chunk)
        level::Chunk* p_chunk44 = parent_d3.GetChunkManager().GetChunk(4, 4);
        p_chunk44->pending_ornate_pools.clear();
        p_chunk44->pending_ornate_pools.emplace_back(4 * 64 + 20, 4 * 64 + 20);
        parent_d3.SetTile(4 * 64 + 10, 4 * 64 + 10, level::Tiles::kStairsDown, 0);
        Check(parent_d3.GetChunkManager().GetChunkStage(4, 4) ==
                  level::Chunk::kStageTerrain &&
                  child_d4.GetChunkManager().GetChunkStage(4, 4) == -1,
              "Chunk (4,4) starts at kStageTerrain in parent and -1 in child");

        // Player modifies central DungeonBossRoom and removes KnightStatue
        child_d4.FlushPendingEntities();
        for (const auto& e : child_d4.Entities()) {
          if (e && dynamic_cast<entity::KnightStatue*>(e.get())) e->Remove();
        }
        child_d4.PurgeRemovedEntities();
        child_d4.SetTile(64, 64, level::Tiles::kWoodPlanks, 0);

        // LoadChunk(4, 4) with prev_stage == -1 on child_d4 must:
        // 1) Populate parent_d3(4, 4) (drawing OrnateLavaPool at (276, 276) and reaching kStageFinished)
        // 2) Link StairsDown -> DungeonGate + StairsUp at (266, 266)
        // 3) Preserve modified BossRoom at (64, 64) without respawning KnightStatue
        child_d4.LoadChunk(4, 4);
        child_d4.FlushPendingEntities();
        int32_t statues_after = 0;
        for (const auto& e : child_d4.Entities()) {
          if (e && !e->removed && dynamic_cast<entity::KnightStatue*>(e.get())) {
            ++statues_after;
          }
        }
        Check(parent_d3.GetChunkManager().GetChunkStage(4, 4) ==
                  level::Chunk::kStageFinished &&
                  p_chunk44->pending_ornate_pools.empty() &&
                  parent_d3.GetTile(4 * 64 + 20, 4 * 64 + 20)->id ==
                      level::Tiles::kLava,
              "LoadChunk(prev_stage==-1) populates parent_level pending OrnateLavaPool");
        Check(child_d4.GetTile(4 * 64 + 10, 4 * 64 + 10)->id ==
                  level::Tiles::kStairsUp &&
                  child_d4.GetTile(64, 64)->id == level::Tiles::kWoodPlanks &&
                  statues_after == 0,
              "LoadChunk links new dungeon stairs without overwriting existing DungeonBossRoom");
      }
    }

    world.UnloadWorld(&clock);
  }

  fs.RemoveDirectoryAll(temp_dir);

  if (g_failures == 0) {
    std::cout << "ALL MINICRAFT CORE TESTS PASSED!\n";
    return 0;
  }
  std::cerr << g_failures << " TEST(S) FAILED!\n";
  return 1;
}
