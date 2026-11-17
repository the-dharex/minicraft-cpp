#include <switch.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/game/game.h"
#include "core/pak/pak_reader.h"
#include "core/platform/platform_context.h"
#include "platform/switch/switch_audio_backend.h"
#include "platform/switch/switch_clock.h"
#include "platform/switch/switch_file_system.h"
#include "platform/switch/switch_render_backend.h"
#include "platform/switch/switch_splash.h"
#include "platform/switch/switch_system.h"
#include "platform/switch/switch_window.h"

namespace {

bool LoadPakBytes(const minicraft::platform::nx::SwitchFileSystem& fs,
                  std::vector<uint8_t>* out_bytes) {
  return fs.ReadBinaryFile("romfs:/assets.pak", out_bytes) && !out_bytes->empty();
}

void SeedInitialLanguageIfMissing(
    const minicraft::platform::nx::SwitchFileSystem& fs) {
  const std::string prefs_path = fs.GetDefaultGameDir() + "/Preferences.json";
  if (fs.Exists(prefs_path)) {
    return;
  }
  const std::string locale =
      minicraft::platform::nx::DetectSystemLocaleTag();
  if (!locale.empty() && locale != "en-us") {
    fs.WriteTextFile(prefs_path, "{\n  \"lang\": \"" + locale + "\"\n}\n");
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  using namespace minicraft;
  using namespace minicraft::platform::nx;

  const bool romfs_ok = R_SUCCEEDED(romfsInit());

  SwitchClock clock;
  SwitchFileSystem fs;
  SwitchAudioBackend audio;

  if (!fs.InitializeStorage()) {
    ShowFatalErrorApplet(
        "Unable to access Nintendo Switch save storage.",
        fs.LastStorageError(), 2001);
    if (romfs_ok) romfsExit();
    return 1;
  }

  SeedInitialLanguageIfMissing(fs);

  std::vector<uint8_t> pak_bytes;
  if (!romfs_ok || !LoadPakBytes(fs, &pak_bytes)) {
    ShowFatalErrorApplet(
        "Required game data archive (assets.pak) could not be loaded.",
        "Expected romfs:/assets.pak inside the application package.", 2002);
    fs.ShutdownStorage();
    if (romfs_ok) romfsExit();
    return 1;
  }

  SwitchWindow window(&clock, &audio, &fs);
  if (!window.Create(1280, 720, game::Game::kName)) {
    ShowFatalErrorApplet(
        "Failed to initialize Nintendo Switch graphics display.",
        "EGL / OpenGL Core context initialization failed.", 2003);
    fs.ShutdownStorage();
    if (romfs_ok) romfsExit();
    return 1;
  }

  SwitchSplashScreen splash(&window, &clock);
  {
    pak::PakReader splash_pak;
    std::vector<uint8_t> splash_png;
    if (splash_pak.LoadFromMemory(pak_bytes) &&
        splash_pak.ReadEntry("Minicraft_Splash_Screen_3.png", &splash_png)) {
      splash.Show(splash_png);
    }
  }

  if (!audio.Init()) {
    // Non-fatal: keep running silently if audio renderer cannot be acquired.
  }

  SwitchRenderBackend renderer(&window);
  if (!renderer.Init()) {
    splash.Destroy();
    ShowFatalErrorApplet(
        "Failed to initialize OpenGL shaders and framebuffers.",
        "SwitchRenderBackend::Init returned false.", 2004);
    audio.Shutdown();
    window.Destroy();
    fs.ShutdownStorage();
    if (romfs_ok) romfsExit();
    return 1;
  }

  platform::PlatformContext ctx;
  ctx.window = &window;
  ctx.renderer = &renderer;
  ctx.audio = &audio;
  ctx.file_system = &fs;
  ctx.clock = &clock;

  game::LaunchOptions opts = game::Initializer::ParseArgs(argc, argv);
  opts.save_dir = fs.GetDefaultGameDir();
  opts.fullscreen = true;

  game::Game game;
  if (!game.Init(ctx, std::move(pak_bytes), opts)) {
    splash.Destroy();
    ShowFatalErrorApplet(
        "Failed to initialize Minicraft+ game engine.",
        "Check save data integrity and assets.pak contents.", 2005);
    renderer.Shutdown();
    audio.Shutdown();
    window.Destroy();
    fs.ShutdownStorage();
    if (romfs_ok) romfsExit();
    return 1;
  }

  if (splash.IsActive()) {
    splash.WaitRemaining(1200);
    splash.Destroy();
  }

  game.RunMainLoop();
  game.Shutdown();

  renderer.Shutdown();
  audio.Shutdown();
  window.Destroy();
  fs.ShutdownStorage();
  if (romfs_ok) romfsExit();
  return 0;
}
