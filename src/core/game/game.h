#ifndef MINICRAFT_CORE_GAME_GAME_H_
#define MINICRAFT_CORE_GAME_GAME_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core/game/initializer.h"
#include "core/game/renderer.h"
#include "core/game/updater.h"
#include "core/game/world.h"
#include "core/gfx/screen.h"
#include "core/gfx/sprite_atlas.h"
#include "core/io/input_handler.h"
#include "core/pak/pak_reader.h"
#include "core/platform/platform_context.h"

namespace minicraft::game {

// Top-level platform-agnostic engine coordinator owning the main fixed-timestep
// game loop, PakReader, InputHandler, Screen, World, Updater, and Renderer.
class Game {
 public:
  static constexpr const char* kName = "Minicraft Plus";

  Game() = default;
  ~Game() = default;

  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  bool Init(const platform::PlatformContext& platform,
            std::vector<uint8_t> pak_bytes, const LaunchOptions& opts);
  void RunMainLoop(int32_t max_frames = -1);
  void Shutdown();

  void Quit() { running_ = false; }

  World& GetWorld() { return world_; }
  Updater& GetUpdater() { return updater_; }
  io::InputHandler& GetInput() { return input_; }
  gfx::SpriteLinker& GetLinker() { return linker_; }
  const pak::PakReader& GetPak() const { return pak_; }

 private:
  platform::PlatformContext platform_{};
  std::string game_dir_;
  pak::PakReader pak_;
  gfx::SpriteLinker linker_;
  gfx::Screen screen_{&linker_};
  io::InputHandler input_;
  World world_;
  Updater updater_;
  Renderer renderer_;
  std::vector<uint16_t> custom_texture_ids_;
  bool running_ = false;
};

}  // namespace minicraft::game

#endif  // MINICRAFT_CORE_GAME_GAME_H_
