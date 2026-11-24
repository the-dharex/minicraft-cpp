#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/game/game.h"
#include "core/game/initializer.h"
#include "core/pak/pak_reader.h"
#include "core/platform/platform_context.h"
#include "platform/pc/miniaudio_backend.h"
#include "platform/pc/opengl_render_backend.h"
#include "platform/pc/pc_clock.h"
#include "platform/pc/pc_file_system.h"
#include "platform/pc/pc_splash.h"
#include "platform/pc/pc_window.h"

namespace {

std::string DirectoryOf(std::string_view exe_path) {
  size_t pos = exe_path.find_last_of("/\\");
  if (pos == std::string_view::npos) return ".";
  return std::string(exe_path.substr(0, pos));
}

bool LoadPakBytes(const minicraft::platform::IFileSystem& fs,
                  const std::string& exe_dir, const std::string& explicit_pak,
                  std::vector<uint8_t>* out_bytes) {
  std::vector<std::string> candidates;
  if (!explicit_pak.empty()) candidates.push_back(explicit_pak);
  candidates.push_back("assets/assets.pak");
  candidates.push_back("assets.pak");
  candidates.push_back(exe_dir + "/assets.pak");
  candidates.push_back(exe_dir + "/assets/assets.pak");

  for (const auto& path : candidates) {
    if (fs.Exists(path) && fs.ReadBinaryFile(path, out_bytes)) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char* argv[]) {
  using namespace minicraft;

  std::string pak_path;
  int32_t smoke_frames = -1;
  int32_t splash_ms = -1;
  bool no_splash = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--pak" && i + 1 < argc) {
      pak_path = argv[++i];
    } else if (arg == "--no-splash") {
      no_splash = true;
    } else if (arg == "--splash-ms" && i + 1 < argc) {
      try {
        splash_ms = std::stoi(argv[++i]);
      } catch (...) {
        splash_ms = 0;
      }
    } else if (arg == "--smoke-frames" && i + 1 < argc) {
      try {
        smoke_frames = std::stoi(argv[++i]);
      } catch (...) {
        smoke_frames = 3;
      }
    }
  }

  platform::pc::PcClock clock;
  platform::pc::PcFileSystem fs;
  platform::pc::PcWindow window;
  platform::pc::OpenGLRenderBackend renderer(&window);
  platform::pc::MiniaudioBackend audio;

  const std::string exe_dir =
      (argc > 0 && argv[0] != nullptr) ? DirectoryOf(argv[0]) : ".";
  std::vector<uint8_t> pak_bytes;
  if (!LoadPakBytes(fs, exe_dir, pak_path, &pak_bytes)) {
    std::cerr << "Error: Could not locate assets.pak\n";
    return 1;
  }

  const int32_t effective_splash_ms =
      no_splash ? 0 : (splash_ms >= 0 ? splash_ms : (smoke_frames > 0 ? 0 : 1200));

  platform::pc::PcSplashScreen splash;
  if (!no_splash && (smoke_frames <= 0 || splash_ms > 0)) {
    pak::PakReader splash_pak;
    if (splash_pak.LoadFromMemory(pak_bytes)) {
      std::vector<uint8_t> splash_png;
      if (splash_pak.ReadEntry("Minicraft_Splash_Screen_3.png", &splash_png)) {
        splash.Show(splash_png);
      }
    }
  }

  const bool show_immediately = !splash.IsActive();
  if (!window.CreateWithVisibility(864, 576, game::Game::kName,
                                   show_immediately)) {
    splash.Destroy();
    std::cerr << "Error: Could not create GLFW/OpenGL window\n";
    return 1;
  }
  if (!renderer.Init()) {
    splash.Destroy();
    std::cerr << "Error: Could not initialize OpenGL renderer\n";
    window.Destroy();
    return 1;
  }
  if (!audio.Init()) {
    std::cerr << "Warning: Audio backend unavailable; continuing muted.\n";
  }

  platform::PlatformContext ctx{};
  ctx.window = &window;
  ctx.renderer = &renderer;
  ctx.audio = &audio;
  ctx.file_system = &fs;
  ctx.clock = &clock;

  game::LaunchOptions opts = game::Initializer::ParseArgs(argc, argv);
  game::Game game;
  if (!game.Init(ctx, std::move(pak_bytes), opts)) {
    splash.Destroy();
    std::cerr << "Error: Game initialization failed.\n";
    audio.Shutdown();
    renderer.Shutdown();
    window.Destroy();
    return 1;
  }

  if (splash.IsActive()) {
    splash.WaitRemaining(effective_splash_ms);
    splash.Destroy();
    window.MakeContextCurrent();
    window.Show();
  }

  game.RunMainLoop(smoke_frames);
  game.Shutdown();
  audio.Shutdown();
  renderer.Shutdown();
  window.Destroy();
  return 0;
}
