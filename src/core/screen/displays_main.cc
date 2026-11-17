#include "core/screen/displays.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <unordered_map>
#include <unordered_set>

#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/io/sound.h"
#include "core/pak/pak_reader.h"
#include "core/platform/file_system.h"
#include "core/saveload/load.h"
#include "core/saveload/save.h"
#include "core/saveload/version.h"
#include "core/screen/entries.h"
#include "core/util/advancements.h"
#include "core/util/book_data.h"
#include "core/util/localization.h"
#include "core/util/random.h"
#include "json.hpp"

namespace minicraft::screen {

namespace {

constexpr const char *kSplashes[] = {
    "Secret Splash!",
    "Happy birthday Minicraft!",
    "Now with skins!",
    "Now with better fishing!",
    "Now with better tools!",
    "Now with better chests!",
    "Now with better dungeons!",
    "Only on PlayMinicraft.com!",
    "Playminicraft.com is the bomb!",
    "The Wiki is weak! Help it!",
    "Notch is Awesome!",
    "Dillyg10 is cool as Ice!",
    "Shylor is the man!",
    "Chris J is great with portals!",
    "AntVenom loves cows! Honest!",
    "You should read Antidious Venomi!",
    "Keep calm!",
    "Get him, Paul!",
    "Forty-Two!",
    "Kill Creeper, get Gunpowder!",
    "Kill Cow, get Beef!",
    "Kill Zombie, get Cloth!",
    "Kill Slime, get Slime!",
    "Kill Skeleton, get Bones!",
    "Kill Sheep, get Wool!",
    "Kill Pig, get Porkchop!",
    "Gold > Iron",
    "Gem > Gold",
    "Story? Uhh...",
    "Infinite terrain? What's that?",
    "Windows? I prefer Doors!",
    "2.5D FTW!",
    "3rd dimension not included!",
    "Null not included",
    "Mouse not included!",
    "No spiders included!",
    "No chickens included!",
    "Grab your friends!",
    "Creepers included!",
    "Skeletons included!",
    "Knights included!",
    "Snakes included!",
    "Cows included!",
    "Sheep included!",
    "Pigs included!",
    "Bigger Worlds!",
    "World types!",
    "World themes!",
    "Milk is an idea!",
    "So we back in the mine,",
    "Pickaxe swinging from side to side",
    "In search of Gems!",
    "Life itself suspended by a thread",
    "Gimmie a bucket!",
    "Farming with water!",
    "Get the High-Score!",
    "Potions ftw!",
    "Beds ftw!",
    "Defeat the Air Wizard!",
    "Conquer the Dungeon!",
    "One down, one to go...",
    "Loom + Wool = String!",
    "String + Wood = Rod!",
    "Sand + Gunpowder = TNT!",
    "Sleep at Night!",
    "Farm at Day!",
    "Explanation Mark!",
    "!sdrawkcab si sihT",
    "This is forwards!",
    "Why is this blue?",
    "Green is a nice color!",
    "Red is my favorite color!",
    "Punch the Moon!",
    "This is String qq!",
    "Why?",
    "hello down there!",
    "That guy is such a sly fox!",
    "Hola se\xC3\xB1or!",
    "Sonic Boom!",
    "Hakuna Matata!",
    "One truth prevails!",
    "Awesome!",
    "Sweet!",
    "Great!",
    "Cool!",
    "Radical!",
    "011011000110111101101100!",
    "001100010011000000110001!",
    "011010000110110101101101?",
    "...zzz...",
    "They were too redundant",
    "Echoed whispers...",
    "Of knights and men",
    "Death before dishonor!",
};
constexpr int32_t kNumSplashes =
    static_cast<int32_t>(sizeof(kSplashes) / sizeof(kSplashes[0]));

std::string SavesDir() {
  return saveload::Save::JoinWorldPath(DisplayHost::Instance().game_dir,
                                       "saves");
}

bool IsWindowsReservedStem(std::string_view stem) {
  std::string up;
  up.reserve(stem.size());
  for (char c : stem) {
    up.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  if (up == "CON" || up == "PRN" || up == "AUX" || up == "NUL")
    return true;
  if (up.size() == 4 &&
      (up.compare(0, 3, "COM") == 0 || up.compare(0, 3, "LPT") == 0) &&
      up[3] >= '1' && up[3] <= '9') {
    return true;
  }
  return false;
}

bool IsValidWorldFolderName(std::string_view name) {
  if (name.empty() || name.size() > 255)
    return false;
  if (name.back() == ' ' || name.back() == '.')
    return false;
  for (unsigned char ch : name) {
    if (ch < 0x20 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
        ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
      return false;
    }
  }
  size_t dot = name.find('.');
  std::string_view stem =
      (dot == std::string_view::npos) ? name : name.substr(0, dot);
  if (IsWindowsReservedStem(stem))
    return false;
  return true;
}

bool CopyDirRecursive(const platform::IFileSystem &fs, const std::string &src,
                      const std::string &dst) {
  if (!fs.IsDirectory(src))
    return false;
  if (!fs.CreateDirectories(dst))
    return false;
  for (const auto &entry : fs.ListDirectory(src)) {
    const std::string sp = saveload::Save::JoinWorldPath(src, entry);
    const std::string dp = saveload::Save::JoinWorldPath(dst, entry);
    if (fs.IsDirectory(sp)) {
      if (!CopyDirRecursive(fs, sp, dp))
        return false;
    } else {
      std::vector<uint8_t> bytes;
      if (!fs.ReadBinaryFile(sp, &bytes))
        return false;
      if (!fs.WriteBinaryFile(dp, bytes.data(), bytes.size()))
        return false;
    }
  }
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// PopupDisplay
// ---------------------------------------------------------------------------
PopupDisplay::PopupDisplay(std::optional<PopupConfig> config,
                           const std::vector<std::string> &messages,
                           bool clear_screen, bool menu_frame)
    : PopupDisplay(std::move(config),
                   StringEntry::UseLines(messages, gfx::color::kWhite, true),
                   clear_screen, menu_frame) {}

PopupDisplay::PopupDisplay(std::optional<PopupConfig> config,
                           std::vector<std::unique_ptr<ListEntry>> entries,
                           bool clear_screen, bool menu_frame)
    : Display(clear_screen, true) {
  const int32_t spacing = config ? config->entry_spacing : 0;
  bool has_input = false;
  for (const auto &e : entries) {
    if (dynamic_cast<const InputEntry *>(e.get())) {
      has_input = true;
      break;
    }
  }
  Menu::Builder builder(menu_frame, spacing, gfx::RelPos::kCenter,
                        std::move(entries));
  if (config) {
    if (!config->title.empty()) {
      std::string title = config->localize_title
                              ? util::Localization::GetLocalized(config->title)
                              : config->title;
      builder.SetTitle(std::move(title));
    }
    callbacks_ = std::move(config->callbacks);
  }
  menus_.push_back(builder.CreateMenu());
  if (has_input && DisplayHost::Instance().input) {
    osk_ = OnScreenKeyboardMenu::CheckAndCreateMenu(
        *DisplayHost::Instance().input);
  }
}

bool PopupDisplay::TickCallbacks(io::InputHandler &input) {
  Menu *m = menus_.empty() ? nullptr : menus_[0].get();
  for (auto &cb : callbacks_) {
    if (cb.key.empty() || input.InputPressed(cb.key)) {
      if (cb.callback && cb.callback(m))
        return true;
    }
  }
  return false;
}

void PopupDisplay::Tick(io::InputHandler &input) {
  if (!osk_) {
    if (!TickCallbacks(input)) {
      Display::Tick(input);
    }
    return;
  }

  using TickRes = OnScreenKeyboardMenu::TickResult;
  TickRes res = osk_->TickKeyboard(input);
  bool acted = (res != TickRes::kNone);
  bool take_exit = (res != TickRes::kBackspaceActed);
  if (take_exit && input.InputPressed("exit")) {
    DisplayHost::ExitDisplay();
    return;
  }

  Menu *m = menus_.empty() ? nullptr : menus_[0].get();
  if (m && dynamic_cast<InputEntry *>(m->GetCurEntry())) {
    if (input.ButtonPressed(platform::GamepadButton::kX)) {
      osk_->SetVisible(!osk_->IsVisible());
    }
    if (!acted && !osk_->IsVisible()) {
      if (!TickCallbacks(input))
        m->Tick(input);
    }
  } else {
    if (osk_->IsVisible())
      osk_->SetVisible(false);
    if (!TickCallbacks(input) && m) {
      m->Tick(input);
    }
  }
}

void PopupDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (osk_)
    osk_->Render(screen);
}

// ---------------------------------------------------------------------------
// TitleDisplay
// ---------------------------------------------------------------------------
TitleDisplay::TitleDisplay() : Display(true, false) {
  Menu::Builder b(false, 2, gfx::RelPos::kCenter);
  b.SetPositioning({gfx::Screen::w / 2, gfx::Screen::h * 3 / 5},
                   gfx::RelPos::kCenter);

  b.AddEntry(std::make_unique<StringEntry>(
      "minicraft.displays.title.display.latest_already", gfx::color::kDarkGray,
      true));
  b.AddEntry(std::make_unique<BlankEntry>());

  b.AddEntry(std::make_unique<SelectEntry>("minicraft.displays.title.play", [] {
    if (!WorldSelectDisplay::GetWorldNames().empty()) {
      Menu::Builder pb(false, 2, gfx::RelPos::kCenter);
      pb.AddEntry(std::make_unique<SelectEntry>(
          "minicraft.displays.title.play.load_world", [] {
            DisplayHost::SetDisplay(std::make_unique<WorldSelectDisplay>());
          }));
      pb.AddEntry(std::make_unique<SelectEntry>(
          "minicraft.displays.title.play.new_world", [] {
            DisplayHost::SetDisplay(std::make_unique<WorldGenDisplay>());
          }));
      std::vector<std::unique_ptr<Menu>> pm;
      pm.push_back(pb.CreateMenu());
      DisplayHost::SetDisplay(std::make_unique<Display>(true, std::move(pm)));
    } else {
      DisplayHost::SetDisplay(std::make_unique<WorldGenDisplay>());
    }
  }));

  b.AddEntry(
      std::make_unique<SelectEntry>("minicraft.display.options_display", [] {
        DisplayHost::SetDisplay(std::make_unique<OptionsMainMenuDisplay>());
      }));
  b.AddEntry(std::make_unique<SelectEntry>("minicraft.displays.skin", [] {
    DisplayHost::SetDisplay(std::make_unique<SkinDisplay>());
  }));
  b.AddEntry(
      std::make_unique<SelectEntry>("minicraft.displays.achievements", [] {
        DisplayHost::SetDisplay(std::make_unique<AchievementsDisplay>());
      }));
  b.AddEntry(std::make_unique<SelectEntry>("minicraft.displays.title.help", [] {
    Menu::Builder hb(false, 1, gfx::RelPos::kCenter);
    hb.SetTitle("minicraft.displays.title.help");
    hb.AddEntry(std::make_unique<BlankEntry>());
    hb.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.title.help.instructions", [] {
          DisplayHost::SetDisplay(std::make_unique<BookDisplay>(
              util::BookData::Instance().Instructions()));
        }));
    hb.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.title.help.storyline_guide", [] {
          DisplayHost::SetDisplay(std::make_unique<BookDisplay>(
              util::BookData::Instance().StorylineGuide()));
        }));
    hb.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.title.help.about", [] {
          DisplayHost::SetDisplay(std::make_unique<BookDisplay>(
              util::BookData::Instance().About()));
        }));
    hb.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.title.help.credits", [] {
          DisplayHost::SetDisplay(std::make_unique<BookDisplay>(
              util::BookData::Instance().Credits()));
        }));
    std::vector<std::unique_ptr<Menu>> hm;
    hm.push_back(hb.CreateMenu());
    DisplayHost::SetDisplay(std::make_unique<Display>(true, std::move(hm)));
  }));
  b.AddEntry(std::make_unique<SelectEntry>("minicraft.displays.title.quit", [] {
    if (DisplayHost::Instance().quit_game)
      DisplayHost::Instance().quit_game();
  }));

  menus_.push_back(b.CreateMenu());
}

void TitleDisplay::Init(Display * /*parent*/) {
  Display::Init(nullptr);
  std::time_t now = std::time(nullptr);
  std::tm local_tm{};
#ifdef _WIN32
  localtime_s(&local_tm, &now);
#else
  localtime_r(&now, &local_tm);
#endif
  rand_idx_ = 0;
  if (local_tm.tm_mon == 11) {
    if (local_tm.tm_mday == 19)
      rand_idx_ = 1;
  } else {
    rand_idx_ = util::GlobalRandom().NextInt(kNumSplashes - 2) + 2;
  }
}

void TitleDisplay::Tick(io::InputHandler &input) {
  if (input.InputPressed("F3-r")) {
    rand_idx_ = util::GlobalRandom().NextInt(kNumSplashes - 3) + 3;
  }
  Display::Tick(input);
}

void TitleDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (!screen)
    return;

  if (const gfx::SheetRect *sheet =
          screen->Linker() ? title_sheet_.GetSheet(*screen->Linker())
                           : nullptr) {
    const int32_t th = sheet->height / 8;
    const int32_t tw = sheet->width / 8;
    const int32_t xo = (gfx::Screen::w - sheet->width) / 2;
    const int32_t yo = 26 - sheet->height / 2;
    for (int32_t y = 0; y < th; ++y) {
      for (int32_t x = 0; x < tw; ++x) {
        screen->Render(xo + x * 8, yo + y * 8, x, y, 0, sheet);
      }
    }
  }

  std::string_view splash = kSplashes[rand_idx_];
  const bool is_blue = splash.find("blue") != std::string_view::npos;
  const bool is_green = splash.find("Green") != std::string_view::npos;
  const bool is_red = splash.find("Red") != std::string_view::npos;

  if (reverse_) {
    if (--count_ <= 0)
      reverse_ = false;
  } else {
    if (++count_ >= 25)
      reverse_ = true;
  }
  const int32_t bcol = 5 - count_ / 5;
  const int32_t splash_col =
      is_blue    ? gfx::color::kBlue
      : is_red   ? gfx::color::kRed
      : is_green ? gfx::color::kGreen
                 : gfx::color::Get(1, bcol * 51, bcol * 51, bcol * 25);

  gfx::Font::DrawCentered(splash, screen, (gfx::Screen::h / 2) - 44,
                          splash_col);

  gfx::Font::Draw(util::Localization::GetLocalized(
                      "minicraft.displays.title.display.version",
                      std::string(saveload::Version::kCurrentVersion)),
                  screen, 1, 1, gfx::color::Get(1, 51));

  if (auto *input = DisplayHost::Instance().input) {
    gfx::Font::DrawCentered(util::Localization::GetLocalized(
                                "minicraft.displays.title.display.help.0",
                                input->GetMapping("cursor-up"),
                                input->GetMapping("cursor-down")),
                            screen, gfx::Screen::h - 30, gfx::color::kDarkGray);
    gfx::Font::DrawCentered(util::Localization::GetLocalized(
                                "minicraft.displays.title.display.help.1",
                                input->GetMapping("select")),
                            screen, gfx::Screen::h - 20, gfx::color::kDarkGray);
    gfx::Font::DrawCentered(util::Localization::GetLocalized(
                                "minicraft.displays.title.display.help.2",
                                input->GetMapping("exit")),
                            screen, gfx::Screen::h - 10, gfx::color::kDarkGray);
  }
}

// ---------------------------------------------------------------------------
// WorldSelectDisplay
// ---------------------------------------------------------------------------
std::string WorldSelectDisplay::world_name_;
bool WorldSelectDisplay::loaded_world_ = true;

std::vector<std::string> WorldSelectDisplay::GetWorldNames() {
  std::vector<std::string> names;
  const auto *fs = DisplayHost::Instance().fs;
  if (!fs)
    return names;
  for (const auto &meta : saveload::Load::ListWorlds(*fs, SavesDir())) {
    names.push_back(meta.folder_name);
  }
  return names;
}

WorldSelectDisplay::WorldSelectDisplay() : Display(true) {}

void WorldSelectDisplay::Init(Display *parent) {
  Display::Init(parent);
  world_name_.clear();
  loaded_world_ = true;
  RefreshWorlds();
  RebuildEntries();
}

void WorldSelectDisplay::RefreshWorlds() {
  world_names_.clear();
  world_versions_.clear();
  world_compatible_.clear();
  const auto *fs = DisplayHost::Instance().fs;
  if (!fs)
    return;
  const auto current_ver = saveload::Version::Current();
  for (const auto &meta : saveload::Load::ListWorlds(*fs, SavesDir())) {
    world_names_.push_back(meta.folder_name);
    world_versions_.push_back(meta.version.ToString());
    world_compatible_.push_back(meta.version <= current_ver);
  }
}

void WorldSelectDisplay::RebuildEntries() {
  menus_.clear();
  Menu::Builder b(false, 0, gfx::RelPos::kCenter);
  b.SetDisplayLength(5).SetScrollPolicies(1.0f, true);
  for (size_t i = 0; i < world_names_.size(); ++i) {
    const std::string name = world_names_[i];
    const bool compat = world_compatible_[i];
    b.AddEntry(std::make_unique<SelectEntry>(
        name,
        [name, compat] {
          if (!compat)
            return;
          SetWorldName(name, true);
          DisplayHost::SetDisplay(
              std::make_unique<LoadingDisplay>(name, false));
        },
        false));
  }
  menus_.push_back(b.CreateMenu());
}

void WorldSelectDisplay::Tick(io::InputHandler &input) {
  Display::Tick(input);
  if (world_names_.empty() || menus_.empty())
    return;
  const int32_t sel = menus_[0]->GetSelection();
  if (sel < 0 || sel >= static_cast<int32_t>(world_names_.size()))
    return;
  const std::string selected_world = world_names_[sel];

  auto open_name_popup = [&](bool is_copy) {
    auto taken = world_names_;
    if (!is_copy) {
      taken.erase(std::remove(taken.begin(), taken.end(), selected_world),
                  taken.end());
    }
    auto input_entry = WorldGenDisplay::MakeWorldNameInput(
        "", std::move(taken), selected_world, false);
    InputEntry *raw_input = input_entry.get();

    std::vector<std::unique_ptr<ListEntry>> items;
    items.push_back(std::make_unique<StringEntry>(
        "minicraft.displays.world_select.popups.display.change",
        is_copy ? gfx::color::kBlue : gfx::color::kGreen, true));
    items.push_back(std::move(input_entry));
    for (auto &line : StringEntry::UseLines(
             {"",
              util::Localization::GetLocalized(
                  "minicraft.displays.world_select.popups.display.confirm",
                  input.GetMapping("select")),
              util::Localization::GetLocalized(
                  "minicraft.displays.world_select.popups.display.cancel",
                  input.GetMapping("exit"))},
             gfx::color::kWhite, false)) {
      items.push_back(std::move(line));
    }

    PopupConfig cfg;
    cfg.entry_spacing = 0;
    cfg.callbacks.push_back(
        {"select", [this, selected_world, raw_input, is_copy, sel](Menu *) {
           if (!raw_input || !raw_input->IsValid())
             return false;
           const std::string new_name = raw_input->GetUserInput();
           if (const auto *fs = DisplayHost::Instance().fs) {
             const std::string src =
                 saveload::Save::JoinWorldPath(SavesDir(), selected_world);
             const std::string dst =
                 saveload::Save::JoinWorldPath(SavesDir(), new_name);
             if (is_copy) {
               CopyDirRecursive(*fs, src, dst);
             } else {
               fs->Rename(src, dst);
             }
           }
           io::Sound::Play("confirm");
           RefreshWorlds();
           RebuildEntries();
           if (!world_names_.empty()) {
             if (!menus_.empty() && menus_[0]) {
               menus_[0]->SetSelection(std::min<int32_t>(
                   sel, static_cast<int32_t>(world_names_.size()) - 1));
             }
             DisplayHost::ExitDisplay();
           } else {
             DisplayHost::ExitDisplay(3);
             DisplayHost::SetDisplay(std::make_unique<WorldGenDisplay>());
           }
           return true;
         }});
    DisplayHost::SetDisplay(
        std::make_unique<PopupDisplay>(std::move(cfg), std::move(items)));
  };

  if (input.InputPressed("SHIFT-C") ||
      input.ButtonPressed(platform::GamepadButton::kLeftBumper)) {
    open_name_popup(true);
  } else if (input.InputPressed("SHIFT-R") ||
             input.ButtonPressed(platform::GamepadButton::kRightBumper)) {
    open_name_popup(false);
  } else if (input.InputPressed("SHIFT-D") ||
             (input.LeftTriggerPressed() && input.RightTriggerPressed())) {
    std::vector<std::unique_ptr<ListEntry>> items;
    items.push_back(std::make_unique<StringEntry>(
        util::Localization::GetLocalized(
            "minicraft.displays.world_select.popups.display.delete",
            std::string_view(""), selected_world, std::string_view("")),
        gfx::color::kRed, false));
    for (auto &line : StringEntry::UseLines(
             {"",
              util::Localization::GetLocalized(
                  "minicraft.displays.world_select.popups.display.confirm",
                  input.GetMapping("select")),
              util::Localization::GetLocalized(
                  "minicraft.displays.world_select.popups.display.cancel",
                  input.GetMapping("exit"))},
             gfx::color::kWhite, false)) {
      items.push_back(std::move(line));
    }
    PopupConfig cfg;
    cfg.callbacks.push_back(
        {"select", [this, selected_world, sel](Menu *) {
           if (const auto *fs = DisplayHost::Instance().fs) {
             fs->RemoveDirectoryAll(
                 saveload::Save::JoinWorldPath(SavesDir(), selected_world));
           }
           io::Sound::Play("confirm");
           RefreshWorlds();
           RebuildEntries();
           if (!world_names_.empty()) {
             DisplayHost::ExitDisplay();
             if (!menus_.empty() && menus_[0] &&
                 sel >= static_cast<int32_t>(world_names_.size())) {
               menus_[0]->SetSelection(
                   static_cast<int32_t>(world_names_.size()) - 1);
             }
           } else {
             DisplayHost::ExitDisplay(3);
             DisplayHost::SetDisplay(std::make_unique<WorldGenDisplay>());
           }
           return true;
         }});
    DisplayHost::SetDisplay(
        std::make_unique<PopupDisplay>(std::move(cfg), std::move(items)));
  }
}

void WorldSelectDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (!screen)
    return;
  gfx::Font::DrawCentered(util::Localization::GetLocalized(
                              "minicraft.displays.world_select.select_world"),
                          screen, 0, gfx::color::kWhite);
  if (!menus_.empty() && !world_names_.empty()) {
    const int32_t sel = menus_[0]->GetSelection();
    if (sel >= 0 && sel < static_cast<int32_t>(world_versions_.size())) {
      const int32_t ver_col =
          world_compatible_[sel] ? gfx::color::kWhite : gfx::color::kRed;
      if (!world_compatible_[sel]) {
        gfx::Font::DrawCentered(
            util::Localization::GetLocalized(
                "minicraft.displays.world_select.display.world_too_new"),
            screen, gfx::Font::TextHeight() * 5, ver_col);
      }
      saveload::Version parsed(world_versions_[sel], false);
      std::string ver_label = world_versions_[sel];
      if (parsed.IsValid() && parsed <= saveload::Version(1, 9, 2)) {
        ver_label = "~" + ver_label;
      }
      gfx::Font::DrawCentered(
          util::Localization::GetLocalized(
              "minicraft.displays.world_select.display.world_version",
              ver_label),
          screen, gfx::Font::TextHeight() * 7 / 2, ver_col);
    }
  }
  if (auto *input = DisplayHost::Instance().input) {
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.world_select.display.help.0",
            input->GetMapping("select")),
        screen, gfx::Screen::h - 60, gfx::color::kGray);
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.world_select.display.help.1",
            input->GetMapping("exit")),
        screen, gfx::Screen::h - 40, gfx::color::kGray);
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.world_select.display.help.2",
            input->SelectMapping("SHIFT-C", "LEFTBUMPER")),
        screen, gfx::Screen::h - 24, gfx::color::kBlue);
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.world_select.display.help.3",
            input->SelectMapping("SHIFT-R", "RIGHTBUMPER")),
        screen, gfx::Screen::h - 16, gfx::color::kGreen);
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.world_select.display.help.4",
            input->SelectMapping("SHIFT-D", "LEFTRIGHTTRIGGER")),
        screen, gfx::Screen::h - 8, gfx::color::kRed);
  }
}

// ---------------------------------------------------------------------------
// WorldGenDisplay
// ---------------------------------------------------------------------------
std::unique_ptr<InputEntry>
WorldGenDisplay::MakeWorldNameInput(std::string prompt,
                                    std::vector<std::string> taken_names,
                                    std::string init_value, bool is_gen) {
  auto to_lower = [](std::string_view sv) {
    std::string s(sv);
    for (char &c : s) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
  };
  auto char_filter = [](char32_t cp) -> bool {
    if (cp < 0x20 || cp == '<' || cp == '>' || cp == ':' || cp == '"' ||
        cp == '/' || cp == '\\' || cp == '|' || cp == '?' || cp == '*') {
      return false;
    }
    return true;
  };
  return std::make_unique<InputEntry>(
      std::move(prompt), 36, std::move(init_value),
      [taken = std::move(taken_names), to_lower](std::string_view v) {
        std::string lv = to_lower(v);
        if (!IsValidWorldFolderName(lv))
          return false;
        for (const auto &t : taken) {
          if (to_lower(t) == lv)
            return false;
        }
        return true;
      },
      true, is_gen, char_filter);
}

std::optional<int64_t> WorldGenDisplay::ParseSeed(std::string_view seed_str) {
  if (seed_str.empty())
    return std::nullopt;
  bool all_digits = true;
  for (size_t i = 0; i < seed_str.size(); ++i) {
    if (i == 0 && seed_str[i] == '-')
      continue;
    if (!std::isdigit(static_cast<unsigned char>(seed_str[i]))) {
      all_digits = false;
      break;
    }
  }
  if (all_digits && seed_str.size() < 20) {
    try {
      return std::stoll(std::string(seed_str));
    } catch (...) {
    }
  }
  int64_t seed = 1125899906842597LL;
  for (char c : seed_str) {
    seed = 31 * seed + static_cast<unsigned char>(c);
  }
  return seed;
}

WorldGenDisplay::WorldGenDisplay() : Display(true) {
  auto name_input =
      MakeWorldNameInput("minicraft.displays.world_gen.enter_world",
                         WorldSelectDisplay::GetWorldNames(), "", true);
  name_field_ = name_input.get();

  auto name_help = std::make_unique<SelectEntry>(
      "minicraft.displays.world_gen.troublesome_input",
      [] {
        DisplayHost::SetDisplay(std::make_unique<PopupDisplay>(
            std::nullopt,
            std::vector<std::string>{
                "minicraft.displays.world_gen.troublesome_input.msg"}));
      },
      true, [](bool) { return gfx::color::Get(1, 204); });
  name_help->SetVisible(false);
  if (auto *input = DisplayHost::Instance().input) {
    auto has_single_word_key = [](std::string_view mapping) {
      size_t pos = 0;
      while (pos <= mapping.size()) {
        size_t sep = mapping.find_first_of("/|", pos);
        std::string_view tok = (sep == std::string_view::npos)
                                   ? mapping.substr(pos)
                                   : mapping.substr(pos, sep - pos);
        if (tok.size() == 1 &&
            (std::isalnum(static_cast<unsigned char>(tok[0])) ||
             tok[0] == '_')) {
          return true;
        }
        if (sep == std::string_view::npos)
          break;
        pos = sep + 1;
      }
      return false;
    };
    if (has_single_word_key(input->GetMapping("cursor-up")) ||
        has_single_word_key(input->GetMapping("cursor-down"))) {
      name_help->SetVisible(true);
    }
  }

  auto seed_filter = [](char32_t cp) -> bool {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
        (cp >= '0' && cp <= '9')) {
      return true;
    }
    switch (cp) {
    case '-':
    case '!':
    case '"':
    case '#':
    case '%':
    case '/':
    case '(':
    case ')':
    case '=':
    case '+':
    case ',':
      return true;
    default:
      return false;
    }
  };
  auto seed_input =
      std::make_unique<InputEntry>("minicraft.displays.world_gen.world_seed",
                                   20, "", nullptr, true, false, seed_filter);
  seed_field_ = seed_input.get();

  Menu::Builder b(false, 10, gfx::RelPos::kLeft);
  b.SetDisplayLength(5)
      .SetScrollPolicies(0.8f, false)
      .SetTitle("minicraft.displays.world_gen.title");

  b.AddEntry(std::move(name_input));
  b.AddEntry(std::move(name_help));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("mode")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("scoretime")));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.displays.world_gen.create_world",
      [this] {
        if (!name_field_ || !name_field_->IsValid())
          return;
        std::string wname = name_field_->GetUserInput();
        auto seed =
            seed_field_ ? ParseSeed(seed_field_->GetUserInput()) : std::nullopt;
        WorldSelectDisplay::SetWorldName(wname, false);
        DisplayHost::SetDisplay(
            std::make_unique<LoadingDisplay>(wname, true, seed));
      },
      true, [](bool) { return gfx::color::kCyan; }));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("size")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("theme")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("type")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("quests")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("tutorials")));
  b.AddEntry(std::move(seed_input));

  menus_.push_back(b.CreateMenu());
  if (DisplayHost::Instance().input) {
    osk_ = OnScreenKeyboardMenu::CheckAndCreateMenu(
        *DisplayHost::Instance().input);
  }
}

void WorldGenDisplay::Tick(io::InputHandler &input) {
  if (!osk_) {
    Display::Tick(input);
    return;
  }

  using TickRes = OnScreenKeyboardMenu::TickResult;
  TickRes res = osk_->TickKeyboard(input);
  bool acted = (res != TickRes::kNone);
  bool take_exit = (res != TickRes::kBackspaceActed);
  if (take_exit && input.InputPressed("exit")) {
    DisplayHost::ExitDisplay();
    return;
  }

  Menu *m = menus_.empty() ? nullptr : menus_[0].get();
  const bool on_input =
      m && dynamic_cast<InputEntry *>(m->GetCurEntry()) != nullptr;
  if (on_input) {
    if (input.ButtonPressed(platform::GamepadButton::kX)) {
      osk_->SetVisible(!osk_->IsVisible());
    }
    if (!acted && !osk_->IsVisible() && m) {
      m->Tick(input);
    }
  } else {
    if (osk_->IsVisible())
      osk_->SetVisible(false);
    if (m)
      m->Tick(input);
  }
}

void WorldGenDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (osk_)
    osk_->Render(screen);
}

// ---------------------------------------------------------------------------
// LoadingDisplay
// ---------------------------------------------------------------------------
std::mutex LoadingDisplay::progress_mutex_;
float LoadingDisplay::percentage_ = 0.0f;
std::string LoadingDisplay::progress_type_;
bool LoadingDisplay::localize_progress_ = true;

LoadingDisplay::LoadingDisplay(std::string world_name, bool is_new_world,
                               std::optional<int64_t> seed)
    : Display(true, false), world_name_(std::move(world_name)),
      is_new_world_(is_new_world), seed_(seed),
      ellipsis_(std::make_unique<gfx::TimeUpdater>(nullptr, 750)) {}

LoadingDisplay::~LoadingDisplay() { JoinWorker(); }

void LoadingDisplay::JoinWorker() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void LoadingDisplay::Init(Display *parent) {
  if (parent && (parent == this || parent->GetParent() == this))
    return;
  Display::Init(parent);
  JoinWorker();
  {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    percentage_ = 0.0f;
    progress_type_ = "minicraft.displays.loading.message.world";
    localize_progress_ = true;
  }
  msg_ = is_new_world_ ? "minicraft.displays.loading.message.generating"
                       : "minicraft.displays.loading.message.loading";
  started_ = false;
  completed_.store(false, std::memory_order_release);
  load_ok_.store(false, std::memory_order_release);
  delay_ticks_ = 2;
}

void LoadingDisplay::OnExit() {
  JoinWorker();
  SetPercentage(0.0f);
  if (is_new_world_ && load_ok_.load(std::memory_order_acquire) &&
      DisplayHost::Instance().save_world) {
    DisplayHost::Instance().save_world_silent_once = true;
    DisplayHost::Instance().save_world();
    if (DisplayHost::Instance().clear_notifications) {
      DisplayHost::Instance().clear_notifications();
    }
  }
}

void LoadingDisplay::SetPercentage(float percent) {
  std::lock_guard<std::mutex> lock(progress_mutex_);
  percentage_ = percent;
}
float LoadingDisplay::GetPercentage() {
  std::lock_guard<std::mutex> lock(progress_mutex_);
  return percentage_;
}
void LoadingDisplay::SetMessage(std::string_view progress_type, bool localize) {
  std::lock_guard<std::mutex> lock(progress_mutex_);
  progress_type_ = std::string(progress_type);
  localize_progress_ = localize;
}
void LoadingDisplay::Progress(float amt) {
  std::lock_guard<std::mutex> lock(progress_mutex_);
  percentage_ = std::min(100.0f, percentage_ + amt);
}

void LoadingDisplay::Tick(io::InputHandler & /*input*/) {
  if (!started_) {
    if (delay_ticks_ > 0) {
      --delay_ticks_;
      return;
    }
    started_ = true;
    completed_.store(false, std::memory_order_release);
    load_ok_.store(false, std::memory_order_release);
    auto start_fn = DisplayHost::Instance().start_world;
    if (!start_fn) {
      load_ok_.store(true, std::memory_order_release);
      completed_.store(true, std::memory_order_release);
    } else {
      worker_ = std::thread([this, start_fn = std::move(start_fn)]() {
        const bool ok = start_fn(world_name_, is_new_world_, seed_);
        load_ok_.store(ok, std::memory_order_release);
        completed_.store(true, std::memory_order_release);
      });
    }
    return;
  }

  if (!completed_.load(std::memory_order_acquire)) {
    return;
  }

  JoinWorker();
  if (load_ok_.load(std::memory_order_acquire)) {
    DisplayHost::SetDisplay(nullptr);
  } else {
    DisplayHost::ExitDisplay();
  }
}

void LoadingDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (!screen)
    return;
  std::string progress_copy;
  bool localize_copy = true;
  float pct_copy = 0.0f;
  {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    progress_copy = progress_type_;
    localize_copy = localize_progress_;
    pct_copy = percentage_;
  }
  std::string line = util::Localization::GetLocalized(msg_);
  if (!progress_copy.empty()) {
    line += " ";
    line += localize_copy
                ? util::Localization::GetLocalized(progress_copy)
                : progress_copy;
  }
  line += ellipsis_.UpdateAndGet();
  gfx::Font::DrawCentered(line, screen, gfx::Screen::h / 2 - 12,
                          gfx::color::kRed);
  const int32_t pct = std::clamp(static_cast<int32_t>(pct_copy), 0, 100);
  gfx::Font::DrawCentered(std::to_string(pct) + "%", screen,
                          gfx::Screen::h / 2 + 4, gfx::color::kWhite);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// OptionsMainMenuDisplay & OptionsWorldDisplay
// ---------------------------------------------------------------------------
OptionsMainMenuDisplay::OptionsMainMenuDisplay() : Display(true) {
  Menu::Builder b(false, 6, gfx::RelPos::kLeft);
  b.SetTitle("minicraft.displays.options_main_menu");
  b.AddEntry(std::make_unique<SettingListEntry>(io::Settings::GetEntry("fps")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("sound")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("showquests")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("fullscreen")));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.display.options_display.change_key_bindings",
      [] { DisplayHost::SetDisplay(std::make_unique<KeyInputDisplay>()); }));
  b.AddEntry(std::make_unique<SelectEntry>("minicraft.displays.controls", [] {
    DisplayHost::SetDisplay(std::make_unique<ControlsDisplay>());
  }));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.display.options_display.language", [] {
        DisplayHost::SetDisplay(std::make_unique<LanguageSettingsDisplay>());
      }));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.display.options_display.resource_packs", [] {
        DisplayHost::SetDisplay(std::make_unique<ResourcePackDisplay>());
      }));
  menus_.push_back(b.CreateMenu());
}

void OptionsMainMenuDisplay::OnExit() {
  if (DisplayHost::Instance().save_prefs)
    DisplayHost::Instance().save_prefs();
}

OptionsWorldDisplay::OptionsWorldDisplay() : Display(true) {
  Menu::Builder b(false, 6, gfx::RelPos::kLeft);
  b.SetTitle("minicraft.displays.options_world");
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("diff")));
  b.AddEntry(std::make_unique<SettingListEntry>(io::Settings::GetEntry("fps")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("sound")));
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("autosave")));
  if (TutorialDisplayHandler::InQuests()) {
    b.AddEntry(std::make_unique<SettingListEntry>(
        io::Settings::GetEntry("showquests")));
  }
  b.AddEntry(
      std::make_unique<SettingListEntry>(io::Settings::GetEntry("fullscreen")));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.display.options_display.change_key_bindings",
      [] { DisplayHost::SetDisplay(std::make_unique<KeyInputDisplay>()); }));
  b.AddEntry(std::make_unique<SelectEntry>("minicraft.displays.controls", [] {
    DisplayHost::SetDisplay(std::make_unique<ControlsDisplay>());
  }));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.display.options_display.language", [] {
        DisplayHost::SetDisplay(std::make_unique<LanguageSettingsDisplay>());
      }));
  b.AddEntry(std::make_unique<SelectEntry>(
      "minicraft.display.options_display.resource_packs", [] {
        DisplayHost::SetDisplay(std::make_unique<ResourcePackDisplay>());
      }));
  if (TutorialDisplayHandler::InTutorial()) {
    b.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.options_world.skip_current_tutorial", [] {
          TutorialDisplayHandler::SkipCurrent();
          DisplayHost::ExitDisplay();
        }));
    b.AddEntry(std::make_unique<BlankEntry>());
    b.AddEntry(std::make_unique<SelectEntry>(
        "minicraft.displays.options_world.turn_off_tutorials", [] {
          PopupConfig cfg;
          cfg.title = "minicraft.display.popup.title_confirm";
          cfg.entry_spacing = 4;
          cfg.callbacks.push_back({"select", [](Menu *) {
                                     TutorialDisplayHandler::TurnOffTutorials();
                                     DisplayHost::ExitDisplay(2);
                                     return true;
                                   }});
          auto lines = StringEntry::UseLines(
              {"minicraft.displays.options_world.off_tutorials_confirm_popup",
               "minicraft.display.popup.enter_confirm",
               "minicraft.display.popup.escape_cancel"},
              gfx::color::kRed, true);
          DisplayHost::SetDisplay(
              std::make_unique<PopupDisplay>(std::move(cfg), std::move(lines)));
        }));
  }
  menus_.push_back(b.CreateMenu());
}

void OptionsWorldDisplay::OnExit() {
  if (DisplayHost::Instance().save_prefs)
    DisplayHost::Instance().save_prefs();
}

// ---------------------------------------------------------------------------
// ControlsDisplay
// ---------------------------------------------------------------------------
ControlsDisplay::ControlsDisplay() : Display(true, true) {
  displaying_ = DisplayHost::Instance().input
                    ? DisplayHost::Instance().input->GetLastInputType()
                    : 0;
  SwitchMode(displaying_);
}

void ControlsDisplay::SwitchMode(int32_t mode) {
  displaying_ = mode;
  menus_.clear();
  Menu::Builder b(false, 0, gfx::RelPos::kCenter);
  b.SetSelectable(true)
      .SetPositioning({gfx::Screen::w / 2, 20}, gfx::RelPos::kBottom)
      .SetDisplayLength(17);

  const int32_t count = (mode == 0) ? 23 : 16;
  const char *prefix = (mode == 0)
                           ? "minicraft.displays.controls.display.keyboard."
                           : "minicraft.displays.controls.display.controller.";
  for (int32_t i = 0; i < count; ++i) {
    char num[8];
    snprintf(num, sizeof(num), "%02d", i);
    for (auto &line : StringEntry::UseLines({std::string(prefix) + num})) {
      line->SetSelectable(true);
      b.AddEntry(std::move(line));
    }
  }
  menus_.push_back(b.CreateMenu());
}

void ControlsDisplay::Tick(io::InputHandler &input) {
  Display::Tick(input);
  if (input.InputPressed("cursor-left") || input.InputPressed("cursor-right")) {
    SwitchMode(displaying_ ^ 1);
  }
}

void ControlsDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (!screen)
    return;
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized("minicraft.displays.controls"), screen,
      0, gfx::color::kWhite);
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          displaying_ == 0 ? "minicraft.displays.controls.display.keyboard"
                           : "minicraft.displays.controls.display.controller"),
      screen, 10, gfx::color::kWhite);
  if (displaying_ == 0) {
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.controls.display.keyboard.desc"),
        screen, gfx::Screen::h - 16, gfx::color::kGray);
  } else {
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.controls.display.controller.desc.0"),
        screen, gfx::Screen::h - 24, gfx::color::kGray);
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.controls.display.controller.desc.1"),
        screen, gfx::Screen::h - 16, gfx::color::kGray);
  }
  if (auto *input = DisplayHost::Instance().input) {
    gfx::Font::DrawCentered(util::Localization::GetLocalized(
                                "minicraft.displays.controls.display.help.0",
                                input->GetMapping("cursor-left"),
                                input->GetMapping("cursor-right")),
                            screen, gfx::Screen::h - 8, gfx::color::kGray);
  }
}

// ---------------------------------------------------------------------------
// KeyInputDisplay
// ---------------------------------------------------------------------------
KeyInputDisplay::KeyInputDisplay() : Display(true) { RebuildEntries(); }

void KeyInputDisplay::RebuildEntries(int32_t keep_sel, int32_t keep_disp) {
  menus_.clear();
  Menu::Builder b(false, 0, gfx::RelPos::kCenter);
  b.SetTitle("minicraft.displays.key_input.title")
      .SetPositioning(
          {gfx::Screen::w / 2, gfx::Screen::h - gfx::Font::TextHeight() * 5},
          gfx::RelPos::kTop);

  if (auto *input = DisplayHost::Instance().input) {
    auto prefs = input->GetKeyPrefs();
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> duplicated;
    for (const auto &p : prefs) {
      size_t semi = p.find(';');
      if (semi == std::string::npos)
        continue;
      std::string_view rhs(p.data() + semi + 1, p.size() - semi - 1);
      size_t pos = 0;
      while (pos <= rhs.size()) {
        size_t bar = rhs.find('|', pos);
        std::string tok(rhs.substr(
            pos, bar == std::string_view::npos ? rhs.size() - pos : bar - pos));
        if (!seen.insert(tok).second)
          duplicated.insert(tok);
        if (bar == std::string_view::npos)
          break;
        pos = bar + 1;
      }
    }
    for (const auto &p : prefs) {
      b.AddEntry(std::make_unique<KeyInputEntry>(p, duplicated));
    }
  }
  if (keep_sel >= 0) {
    b.SetSelection(keep_sel, std::max<int32_t>(0, keep_disp));
  }
  menus_.push_back(b.CreateMenu());
}

void KeyInputDisplay::Tick(io::InputHandler &input) {
  if (waiting_popup_) {
    waiting_popup_ = false;
    const int32_t sel = menus_.empty() ? 0 : menus_[0]->GetSelection();
    const int32_t dsel = menus_.empty() ? 0 : menus_[0]->GetDispSelection();
    RebuildEntries(sel, dsel);
  }
  Display::Tick(input);
  if (input.IsListeningForKey() && !waiting_popup_) {
    waiting_popup_ = true;
    PopupConfig cfg;
    cfg.entry_spacing = 4;
    cfg.callbacks.push_back(
        {"", [this](Menu *) {
           auto *inp = DisplayHost::Instance().input;
           if (inp && !inp->IsListeningForKey()) {
             waiting_popup_ = false;
             const int32_t sel = menus_.empty() ? 0 : menus_[0]->GetSelection();
             const int32_t dsel =
                 menus_.empty() ? 0 : menus_[0]->GetDispSelection();
             RebuildEntries(sel, dsel);
             DisplayHost::ExitDisplay();
             return true;
           }
           return false;
         }});
    auto lines = StringEntry::UseLines(
        {"minicraft.displays.key_input.popup_display.press_key_sequence"},
        gfx::color::kYellow, true);
    DisplayHost::SetDisplay(
        std::make_unique<PopupDisplay>(std::move(cfg), std::move(lines)));
  } else if (input.InputPressed("shift-d")) {
    PopupConfig cfg;
    cfg.title = "minicraft.display.popup.title_confirm";
    cfg.entry_spacing = 4;
    cfg.callbacks.push_back(
        {"select", [this](Menu *) {
           if (auto *inp = DisplayHost::Instance().input)
             inp->ResetKeyBindings();
           const int32_t sel = menus_.empty() ? 0 : menus_[0]->GetSelection();
           const int32_t dsel =
               menus_.empty() ? 0 : menus_[0]->GetDispSelection();
           RebuildEntries(sel, dsel);
           DisplayHost::ExitDisplay();
           return true;
         }});
    auto lines = StringEntry::UseLines(
        {"minicraft.displays.key_input.popup_display.confirm_reset",
         "minicraft.display.popup.enter_confirm",
         "minicraft.display.popup.escape_cancel"},
        gfx::color::kRed, true);
    DisplayHost::SetDisplay(
        std::make_unique<PopupDisplay>(std::move(cfg), std::move(lines)));
  }
}

void KeyInputDisplay::Render(gfx::Screen *screen) const {
  if (!screen)
    return;
  screen->Clear(0);
  Display::Render(screen);
  gfx::Font::DrawCentered(util::Localization::GetLocalized(
                              "minicraft.displays.key_input.display.help.0"),
                          screen, gfx::Screen::h - 32, gfx::color::kWhite);
  gfx::Font::DrawCentered(util::Localization::GetLocalized(
                              "minicraft.displays.key_input.display.help.1"),
                          screen, gfx::Screen::h - 24, gfx::color::kWhite);
  gfx::Font::DrawCentered(util::Localization::GetLocalized(
                              "minicraft.displays.key_input.display.help.2"),
                          screen, gfx::Screen::h - 16, gfx::color::kWhite);
  auto *input = DisplayHost::Instance().input;
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          "minicraft.displays.key_input.display.help.3",
          input ? input->GetMapping("exit") : std::string("ESCAPE")),
      screen, gfx::Screen::h - 8, gfx::color::kWhite);
}

// ---------------------------------------------------------------------------
// LanguageSettingsDisplay
// ---------------------------------------------------------------------------
LanguageSettingsDisplay::LanguageSettingsDisplay() : Display(true) {
  auto locales = util::Localization::Instance().GetLocales();
  std::sort(locales.begin(), locales.end(), [](const auto &a, const auto &b) {
    if (a.tag == util::Localization::kDebugLocale)
      return true;
    if (b.tag == util::Localization::kDebugLocale)
      return false;
    return a.DisplayString() < b.DisplayString();
  });

  const std::string cur_tag =
      util::Localization::Instance().SelectedLocaleTag();
  int32_t sel_idx = 0;

  Menu::Builder b(false, 2, gfx::RelPos::kCenter);
  b.SetTitle("minicraft.displays.language_settings.title")
      .SetSelectable(true)
      .SetPositioning({gfx::Screen::w / 2, 10}, gfx::RelPos::kBottom)
      .SetSize(gfx::Screen::w, gfx::Screen::h - 30);

  for (size_t i = 0; i < locales.size(); ++i) {
    const auto loc = locales[i];
    const bool is_cur = (loc.tag == cur_tag);
    if (is_cur)
      sel_idx = static_cast<int32_t>(i);
    b.AddEntry(std::make_unique<SelectEntry>(
        loc.DisplayString(),
        [tag = loc.tag] {
          util::Localization::Instance().ChangeLanguage(tag);
          DisplayHost::ExitDisplay();
        },
        false,
        [is_cur](bool selected) {
          if (is_cur) {
            return selected ? gfx::color::kGreen : gfx::color::kGray;
          }
          return selected ? ListEntry::kColSelected : ListEntry::kColUnselected;
        }));
  }
  b.SetSelection(sel_idx);
  menus_.push_back(b.CreateMenu());
}

// ---------------------------------------------------------------------------
// SkinDisplay
// ---------------------------------------------------------------------------
const std::vector<std::string> &SkinDisplay::DefaultSkins() {
  static const std::vector<std::string> kSkins = {
      "minicraft.skin.paul",
      "minicraft.skin.paul_cape",
      "minicraft.skin.minecraft_steve",
      "minicraft.skin.minecraft_alex",
  };
  return kSkins;
}

int32_t SkinDisplay::SelectedSkinIndex() {
  return game::GameState::Instance().selected_skin_index;
}

void SkinDisplay::SetSelectedSkinIndex(int32_t idx) {
  game::GameState::Instance().selected_skin_index = idx;
}

SkinDisplay::SkinDisplay() : Display(true, true) {
  if (DisplayHost::Instance().reload_skins) {
    DisplayHost::Instance().reload_skins("");
  }
  skin_names_ = DefaultSkins();
  if (const auto *fs = DisplayHost::Instance().fs;
      fs && !DisplayHost::Instance().game_dir.empty()) {
    const std::string skins_dir = saveload::Save::JoinWorldPath(
        DisplayHost::Instance().game_dir, "skins");
    fs->CreateDirectories(skins_dir);
    std::vector<std::string> custom_skins;
    for (const auto &fname : fs->ListDirectory(skins_dir)) {
      if (fname.size() > 4 &&
          (fname.compare(fname.size() - 4, 4, ".png") == 0 ||
           fname.compare(fname.size() - 4, 4, ".PNG") == 0)) {
        custom_skins.push_back(fname.substr(0, fname.size() - 4));
      }
    }
    std::sort(custom_skins.begin(), custom_skins.end());
    for (auto &cs : custom_skins) {
      skin_names_.push_back(std::move(cs));
    }
  }

  Menu::Builder b(false, 2, gfx::RelPos::kCenter);
  b.SetDisplayLength(8).SetSelectable(true).SetPositioning(
      {gfx::Screen::w / 2, gfx::Screen::h * 3 / 5}, gfx::RelPos::kCenter);
  for (size_t i = 0; i < skin_names_.size(); ++i) {
    const int32_t idx = static_cast<int32_t>(i);
    const std::string skin = skin_names_[i];
    const bool is_default = i < DefaultSkins().size();
    b.AddEntry(std::make_unique<SelectEntry>(
        skin,
        [idx, skin] {
          SetSelectedSkinIndex(idx);
          game::GameState::Instance().selected_skin_name = skin;
          const bool creative = io::Settings::GetString("mode") == "creative";
          util::AchievementManager::Instance().SetAchievement(
              "minicraft.achievement.skin", true, creative, true);
          if (DisplayHost::Instance().reload_skins) {
            DisplayHost::Instance().reload_skins(skin);
          }
          DisplayHost::ExitDisplay();
        },
        is_default));
  }

  int32_t initial_sel = SelectedSkinIndex();
  const auto &cur_name = game::GameState::Instance().selected_skin_name;
  for (size_t i = 0; i < skin_names_.size(); ++i) {
    if (skin_names_[i] == cur_name) {
      initial_sel = static_cast<int32_t>(i);
      break;
    }
  }
  b.SetSelection(std::clamp<int32_t>(
      initial_sel, 0, static_cast<int32_t>(skin_names_.size()) - 1));
  menus_.push_back(b.CreateMenu());
}

void SkinDisplay::Tick(io::InputHandler &input) {
  Display::Tick(input);
  step_++;
}

void SkinDisplay::OnExit() {
  if (DisplayHost::Instance().save_prefs)
    DisplayHost::Instance().save_prefs();
}

void SkinDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (!screen || menus_.empty())
    return;
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized("minicraft.displays.skin"), screen, 16,
      gfx::color::kWhite);

  const int32_t sel =
      std::clamp<int32_t>(menus_[0]->GetSelection(), 0,
                          static_cast<int32_t>(skin_names_.size()) - 1);
  const int32_t sprite_idx = (step_ / 40) % 8;
  const int32_t dir_idx = sprite_idx / 2;
  const int32_t walk = sprite_idx % 2;
  const int32_t xt = (dir_idx == 0) ? 0 : (dir_idx == 1 ? 2 : 4 + 2 * walk);
  const int32_t mirror = (dir_idx < 2) ? walk : (dir_idx == 2 ? 1 : 0);

  const gfx::SheetRect *sheet = nullptr;
  int32_t sy_base = 0;
  if (screen->Linker()) {
    if (sel < static_cast<int32_t>(DefaultSkins().size())) {
      sheet =
          screen->Linker()->GetSheet(gfx::SpriteType::kMisc, "default_skins");
      sy_base = sel * 4;
    } else if (sel >= 0 && sel < static_cast<int32_t>(skin_names_.size())) {
      sheet = screen->Linker()->GetSheet(gfx::SpriteType::kEntity,
                                         "skin." + skin_names_[sel]);
      sy_base = 0;
    }
  }
  if (sheet) {
    const int32_t px = gfx::Screen::w / 2 - 8;
    const int32_t py = 40;
    const int32_t left_col = mirror ? xt + 1 : xt;
    const int32_t right_col = mirror ? xt : xt + 1;
    screen->Render(px, py, left_col, sy_base, mirror, sheet);
    screen->Render(px + 8, py, right_col, sy_base, mirror, sheet);
    screen->Render(px, py + 8, left_col, sy_base + 1, mirror, sheet);
    screen->Render(px + 8, py + 8, right_col, sy_base + 1, mirror, sheet);
  }

  if (auto *input = DisplayHost::Instance().input) {
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.resource_packs.display.help.move",
            input->GetMapping("cursor-down"), input->GetMapping("cursor-up")),
        screen, gfx::Screen::h - 17, gfx::color::kDarkGray);
    gfx::Font::DrawCentered(util::Localization::GetLocalized(
                                "minicraft.displays.skin.display.help.select",
                                input->GetMapping("SELECT"),
                                input->GetMapping("EXIT")),
                            screen, gfx::Screen::h - 9, gfx::color::kDarkGray);
  }
}

// ---------------------------------------------------------------------------
// ResourcePackDisplay
// ---------------------------------------------------------------------------
namespace {

ResourcePackDisplay::PackInfo ParsePackMetadata(const std::string &json_text,
                                                std::string fallback_name,
                                                std::string fallback_desc) {
  ResourcePackDisplay::PackInfo info;
  info.name = std::move(fallback_name);
  info.description = std::move(fallback_desc);
  if (json_text.empty())
    return info;

  auto j = nlohmann::json::parse(json_text, nullptr, false);
  if (!j.is_discarded() && j.is_object()) {
    if (j.contains("name") && j["name"].is_string()) {
      std::string parsed_name = j["name"].get<std::string>();
      if (!parsed_name.empty())
        info.name = std::move(parsed_name);
    }
    if (j.contains("description") && j["description"].is_string()) {
      info.description = j["description"].get<std::string>();
    }
    if (j.contains("pack_format") && j["pack_format"].is_number_integer()) {
      info.pack_format = j["pack_format"].get<int32_t>();
    }
  }
  return info;
}

void LoadPackLogo(const platform::IFileSystem &fs, const std::string &pack_dir,
                  ResourcePackDisplay::PackInfo *out) {
  if (!out)
    return;
  std::vector<uint8_t> png_bytes, rgba;
  int32_t w = 0, h = 0;
  if (!fs.ReadBinaryFile(saveload::Save::JoinWorldPath(pack_dir, "pack.png"),
                         &png_bytes)) {
    return;
  }
  if (!fs.DecodePngFromMemory(png_bytes.data(), png_bytes.size(), &w, &h,
                              &rgba)) {
    return;
  }
  if (w > 0 && h > 0 && w <= gfx::Screen::w && h <= 64) {
    uint16_t tid = DisplayHost::CreateTexture(w, h, rgba.data());
    if (tid > 0) {
      out->logo_tex = tid;
      out->logo_w = static_cast<uint16_t>(w);
      out->logo_h = static_cast<uint16_t>(h);
    }
  }
}

} // namespace

ResourcePackDisplay::ResourcePackDisplay() : Display(true, true) {
  LoadPacks();
  RebuildMenus(0, 0);
}

ResourcePackDisplay::~ResourcePackDisplay() { FreeLogos(); }

void ResourcePackDisplay::FreeLogos() {
  for (auto &p : packs_) {
    if (p.logo_tex > 0) {
      DisplayHost::DestroyTexture(p.logo_tex);
      p.logo_tex = 0;
    }
  }
}

void ResourcePackDisplay::LoadPacks() {
  FreeLogos();
  packs_.clear();

  std::unordered_map<std::string, PackInfo> discovered;
  const auto *fs = DisplayHost::Instance().fs;
  if (fs) {
    const std::string rp_dir = saveload::Save::JoinWorldPath(
        DisplayHost::Instance().game_dir, "resourcepacks");
    fs->CreateDirectories(rp_dir);
    for (const auto &entry : fs->ListDirectory(rp_dir)) {
      const std::string pack_dir = saveload::Save::JoinWorldPath(rp_dir, entry);
      if (!fs->IsDirectory(pack_dir))
        continue;

      std::string pack_json;
      fs->ReadTextFile(saveload::Save::JoinWorldPath(pack_dir, "pack.json"),
                       &pack_json);
      PackInfo info = ParsePackMetadata(pack_json, entry, "");
      info.folder = entry;
      LoadPackLogo(*fs, pack_dir, &info);
      discovered.emplace(entry, std::move(info));
    }
  }

  std::unordered_set<std::string> added_folders;
  for (const auto &active_name :
       DisplayHost::Instance().active_resource_packs) {
    auto it = discovered.find(active_name);
    if (it != discovered.end() && added_folders.insert(active_name).second) {
      PackInfo info = it->second;
      it->second.logo_tex = 0;
      info.enabled = true;
      info.locked = false;
      packs_.push_back(std::move(info));
    }
  }

  std::vector<PackInfo> disabled_packs;
  for (auto &[folder, info] : discovered) {
    if (added_folders.find(folder) == added_folders.end()) {
      info.enabled = false;
      info.locked = false;
      disabled_packs.push_back(std::move(info));
      info.logo_tex = 0;
    }
  }
  std::sort(
      disabled_packs.begin(), disabled_packs.end(),
      [](const PackInfo &a, const PackInfo &b) { return a.folder < b.folder; });
  for (auto &info : disabled_packs) {
    packs_.push_back(std::move(info));
  }

  // Cleanup any remaining unmoved logos.
  for (auto &[folder, info] : discovered) {
    if (info.logo_tex > 0)
      DisplayHost::DestroyTexture(info.logo_tex);
  }

  // Java parity: the built-in Default pack is always shown last, enabled and
  // locked so it cannot be disabled or reordered.
  std::string default_json;
  if (const auto *pak = DisplayHost::Instance().pak) {
    pak->ReadTextEntry("pack.json", &default_json);
  }
  PackInfo def_pack = ParsePackMetadata(
      default_json, "Default", "The built-in default resources in Minicraft+.");
  def_pack.folder.clear();
  def_pack.enabled = true;
  def_pack.locked = true;
  packs_.push_back(std::move(def_pack));
}

void ResourcePackDisplay::SyncActiveResourcePacks() {
  auto &active = DisplayHost::Instance().active_resource_packs;
  active.clear();
  for (const auto &p : packs_) {
    if (!p.locked && p.enabled && !p.folder.empty()) {
      active.push_back(p.folder);
    }
  }
}

void ResourcePackDisplay::RebuildMenus(int32_t keep_selection,
                                       int32_t keep_disp) {
  menus_.clear();
  if (packs_.empty())
    return;

  const int32_t vis_len = std::max<int32_t>(
      1, std::min<int32_t>(9, static_cast<int32_t>(packs_.size())));
  const int32_t max_idx = static_cast<int32_t>(packs_.size()) - 1;
  const int32_t sel = std::clamp(keep_selection, 0, max_idx);
  const int32_t disp_sel = std::clamp(keep_disp, 0, vis_len - 1);

  Menu::Builder ind_b(false, 2, gfx::RelPos::kLeft);
  ind_b.SetSelectable(false)
      .SetDisplayLength(vis_len)
      .SetPositioning({20, 60}, gfx::RelPos::kBottomLeft)
      .SetSelection(sel, disp_sel);

  Menu::Builder ent_b(false, 2, gfx::RelPos::kLeft);
  ent_b.SetSelectable(true)
      .SetDisplayLength(vis_len)
      .SetPositioning({20, 60}, gfx::RelPos::kBottomRight)
      .SetSelection(sel, disp_sel);

  for (size_t i = 0; i < packs_.size(); ++i) {
    const auto &p = packs_[i];
    if (p.locked) {
      ind_b.AddEntry(std::make_unique<StringEntry>("\xE2\x9C\x93",
                                                   gfx::color::kGray, false));
      auto def_entry =
          std::make_unique<SelectEntry>(p.name, SelectEntry::Action{}, false);
      def_entry->SetSelectable(false);
      ent_b.AddEntry(std::move(def_entry));
    } else {
      if (p.enabled) {
        ind_b.AddEntry(std::make_unique<StringEntry>(
            "\xE2\x9C\x93", gfx::color::kGreen, false));
      } else {
        ind_b.AddEntry(std::make_unique<BlankEntry>());
      }
      ent_b.AddEntry(std::make_unique<SelectEntry>(
          p.name,
          [this, i] {
            if (i >= packs_.size() || packs_[i].locked)
              return;
            packs_[i].enabled = !packs_[i].enabled;
            SyncActiveResourcePacks();
            changed_ = true;
            const int32_t cur_sel = menus_.size() > 1
                                        ? menus_[1]->GetSelection()
                                        : static_cast<int32_t>(i);
            const int32_t cur_disp =
                menus_.size() > 1 ? menus_[1]->GetDispSelection() : 0;
            RebuildMenus(cur_sel, cur_disp);
          },
          false));
    }
  }

  auto ind_menu = ind_b.CreateMenu();
  auto ent_menu = ent_b.CreateMenu();
  const int32_t gap =
      ent_menu->GetBounds().Left() - ind_menu->GetBounds().Right();
  if (gap < 10) {
    ent_menu->Translate(10 - gap, 0);
  }
  menus_.push_back(std::move(ind_menu));
  menus_.push_back(std::move(ent_menu));
  selection_ = 1;
}

void ResourcePackDisplay::Tick(io::InputHandler &input) {
  if (input.InputPressed("exit")) {
    DisplayHost::ExitDisplay();
    return;
  }

  if (menus_.size() < 2 || packs_.empty())
    return;

  const int32_t cur_sel = menus_[1]->GetSelection();
  const int32_t cur_disp = menus_[1]->GetDispSelection();
  const int32_t n = static_cast<int32_t>(packs_.size());

  // Java parity: cursor-right/cursor-left reorder custom packs above Default.
  if (input.InputPressed("cursor-right")) {
    if (cur_sel >= 0 && cur_sel < n - 2) {
      std::swap(packs_[cur_sel], packs_[cur_sel + 1]);
      SyncActiveResourcePacks();
      changed_ = true;
      io::Sound::Play("select");
      RebuildMenus(cur_sel + 1, cur_disp + 1);
    }
    return;
  }
  if (input.InputPressed("cursor-left")) {
    if (cur_sel > 0 && cur_sel < n - 1) {
      std::swap(packs_[cur_sel], packs_[cur_sel - 1]);
      SyncActiveResourcePacks();
      changed_ = true;
      io::Sound::Play("select");
      RebuildMenus(cur_sel - 1, std::max<int32_t>(0, cur_disp - 1));
    }
    return;
  }

  if (input.InputPressed("menu") && cur_sel >= 0 && cur_sel < n) {
    const auto &p = packs_[cur_sel];
    PopupConfig cfg;
    cfg.title = p.name;
    cfg.localize_title = false;
    cfg.entry_spacing = 2;
    const std::string &desc =
        p.description.empty() ? std::string("No description.") : p.description;
    auto lines = StringEntry::UseLines({desc}, gfx::color::kWhite, false);
    DisplayHost::SetDisplay(
        std::make_unique<PopupDisplay>(std::move(cfg), std::move(lines)));
    return;
  }

  menus_[1]->Tick(input);
  if (menus_.size() >= 2 && menus_[0] && menus_[1]) {
    menus_[0]->SetSelection(menus_[1]->GetSelection());
  }
}

void ResourcePackDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (!screen)
    return;

  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          "minicraft.displays.resource_packs.display.title"),
      screen, 6, gfx::color::kWhite);

  if (menus_.size() >= 2 && !packs_.empty()) {
    const int32_t sel = std::clamp<int32_t>(
        menus_[1]->GetSelection(), 0, static_cast<int32_t>(packs_.size()) - 1);
    const auto &pack = packs_[sel];
    if (pack.logo_tex > 0) {
      gfx::SheetRect logo{pack.logo_tex, 0, 0, pack.logo_w, pack.logo_h, true};
      screen->RenderSub(gfx::Screen::w / 2 - pack.logo_w / 2,
                        36 - pack.logo_h / 2, 0, 0, pack.logo_w, pack.logo_h,
                        &logo);
    } else if (screen->Linker()) {
      if (const gfx::SheetRect *logo = screen->Linker()->GetSheet(
              gfx::SpriteType::kMisc, "default_pack_logo")) {
        if (logo->height <= 40) {
          screen->RenderSub(gfx::Screen::w / 2 - logo->width / 2,
                            36 - logo->height / 2, 0, 0, logo->width,
                            logo->height, logo);
        }
      }
    }
  }

  auto *input = DisplayHost::Instance().input;
  const auto key = [input](std::string_view action, const char *fallback) {
    return input ? input->GetMapping(action) : std::string(fallback);
  };
  if (input && input->AnyControllerConnected()) {
    gfx::Font::DrawCentered(
        util::Localization::GetLocalized(
            "minicraft.displays.resource_packs.display.help.keyboard_needed"),
        screen, gfx::Screen::h - 41, gfx::color::kDarkGray);
  }
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          "minicraft.displays.resource_packs.display.help.move",
          key("cursor-up", "UP"), key("cursor-down", "DOWN")),
      screen, gfx::Screen::h - 33, gfx::color::kDarkGray);
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          "minicraft.displays.resource_packs.display.help.reorder",
          key("cursor-left", "LEFT"), key("cursor-right", "RIGHT")),
      screen, gfx::Screen::h - 25, gfx::color::kDarkGray);
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          "minicraft.displays.resource_packs.display.help.select",
          key("select", "ENTER")),
      screen, gfx::Screen::h - 17, gfx::color::kDarkGray);
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          "minicraft.displays.resource_packs.display.help.info",
          key("menu", "X")),
      screen, gfx::Screen::h - 9, gfx::color::kDarkGray);
}

void ResourcePackDisplay::OnExit() {
  if (changed_ && DisplayHost::Instance().reload_resource_packs) {
    DisplayHost::Instance().reload_resource_packs();
  }
  if (DisplayHost::Instance().save_prefs)
    DisplayHost::Instance().save_prefs();
}

// ---------------------------------------------------------------------------
// AchievementsDisplay
// ---------------------------------------------------------------------------
AchievementsDisplay::AchievementsDisplay() : Display(true, true) {
  Menu::Builder list_b(false, 2, gfx::RelPos::kCenter);
  list_b.SetSize(48, 48);

  const auto &mgr = util::AchievementManager::Instance();
  for (const auto &id : mgr.OrderedIds()) {
    const auto *ach = mgr.Find(id);
    if (!ach)
      continue;
    const bool unlocked = ach->unlocked;
    list_b.AddEntry(
        std::make_unique<SelectEntry>(id, nullptr, true, [unlocked](bool) {
          return unlocked ? gfx::color::kGreen : gfx::color::kWhite;
        }));
  }
  menus_.push_back(list_b.CreateMenu());

  Menu::Builder desc_b(false, 2, gfx::RelPos::kBottom);
  desc_b.SetSize(200, 32)
      .SetPositioning({gfx::Screen::w / 2, gfx::Screen::h / 2 + 32},
                      gfx::RelPos::kBottom)
      .AddEntry(std::make_unique<StringEntry>("", gfx::color::kWhite, false));
  menus_.push_back(desc_b.CreateMenu());
  UpdateDescription(true);
}

void AchievementsDisplay::Init(Display *parent) {
  Display::Init(parent);
  if (util::AchievementManager::Instance().OrderedIds().empty()) {
    DisplayHost::SetDisplay(std::make_unique<TitleDisplay>());
    return;
  }
  UpdateDescription(true);
}

void AchievementsDisplay::OnExit() {
  io::Sound::Play("confirm");
  if (DisplayHost::Instance().save_prefs)
    DisplayHost::Instance().save_prefs();
}

void AchievementsDisplay::UpdateDescription(bool force) {
  if (menus_.size() < 2)
    return;
  auto *se = dynamic_cast<SelectEntry *>(menus_[0]->GetCurEntry());
  if (!se)
    return;
  if (!force && se->RawText() == selected_id_)
    return;
  selected_id_ = se->RawText();
  const auto *ach = util::AchievementManager::Instance().Find(selected_id_);
  if (ach) {
    menus_[1]->SetEntries(StringEntry::UseLines(
        {util::Localization::GetLocalized(ach->description)},
        gfx::color::kWhite, false, menus_[1]->GetBounds().Width()));
  }
}

void AchievementsDisplay::Tick(io::InputHandler &input) {
  Display::Tick(input);
  UpdateDescription(false);
}

void AchievementsDisplay::Render(gfx::Screen *screen) const {
  Display::Render(screen);
  if (!screen)
    return;

  const auto &mgr = util::AchievementManager::Instance();
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized("minicraft.displays.achievements"),
      screen, 8, gfx::color::kWhite);
  gfx::Font::DrawCentered(
      util::Localization::GetLocalized(
          "minicraft.displays.achievements.display.score", mgr.TotalScore()),
      screen, 32, gfx::color::kGray);

  if (const auto *ach = mgr.Find(selected_id_)) {
    if (ach->unlocked) {
      gfx::Font::DrawCentered(
          util::Localization::GetLocalized(
              "minicraft.displays.achievements.display.achieved"),
          screen, 48, gfx::color::kGreen);
    } else {
      gfx::Font::DrawCentered(
          util::Localization::GetLocalized(
              "minicraft.displays.achievements.display.not_achieved"),
          screen, 48, gfx::color::kRed);
    }
  }
  if (auto *input = DisplayHost::Instance().input) {
    gfx::Font::DrawCentered(util::Localization::GetLocalized(
                                "minicraft.displays.achievements.display.help",
                                input->GetMapping("cursor-down"),
                                input->GetMapping("cursor-up")),
                            screen, gfx::Screen::h - 8, gfx::color::kDarkGray);
  }
}

} // namespace minicraft::screen
