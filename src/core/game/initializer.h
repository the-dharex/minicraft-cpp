#ifndef MINICRAFT_CORE_GAME_INITIALIZER_H_
#define MINICRAFT_CORE_GAME_INITIALIZER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace minicraft {
namespace gfx {
class SpriteLinker;
}
namespace io {
class InputHandler;
}
namespace pak {
class PakReader;
}
namespace platform {
struct PlatformContext;
}
}  // namespace minicraft

namespace minicraft::game {

struct LaunchOptions {
  std::string save_dir;
  bool fullscreen = false;
  bool debug_locale = false;
  bool unloc_tracing = false;
};

// Bootstraps platform-agnostic game registries, localization, advancements,
// mega-atlas textures, sound bank, saved preferences, and resource packs.
class Initializer {
 public:
  static LaunchOptions ParseArgs(int argc, char* argv[]);

  static bool InitializeCore(const pak::PakReader& pak,
                             const platform::PlatformContext& platform,
                             gfx::SpriteLinker& linker,
                             io::InputHandler& input,
                             const std::string& game_dir,
                             const LaunchOptions& opts);

  static void ReloadResourcePacks(
      const pak::PakReader& pak, const platform::PlatformContext& platform,
      gfx::SpriteLinker& linker, const std::string& game_dir,
      const std::vector<std::string>& active_packs,
      std::vector<uint16_t>* custom_texture_ids);

  static void ReloadCustomSkins(
      const platform::PlatformContext& platform, gfx::SpriteLinker& linker,
      const std::string& game_dir,
      std::vector<uint16_t>* custom_skin_texture_ids);
};

}  // namespace minicraft::game

#endif  // MINICRAFT_CORE_GAME_INITIALIZER_H_
