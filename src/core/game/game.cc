#include "core/game/game.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/game/game_state.h"
#include "core/io/settings.h"
#include "core/io/sound.h"
#include "core/platform/audio_backend.h"
#include "core/platform/file_system.h"
#include "core/platform/render_backend.h"
#include "core/platform/time_clock.h"
#include "core/platform/window_backend.h"
#include "core/screen/display.h"
#include "core/screen/displays.h"
#include "core/util/logging.h"

namespace minicraft::game {

bool Game::Init(const platform::PlatformContext& platform,
                std::vector<uint8_t> pak_bytes, const LaunchOptions& opts) {
  platform_ = platform;

  if (!pak_.LoadFromMemory(std::move(pak_bytes))) {
    util::log::kGameHandler.Error("Failed to parse assets.pak archive.");
    return false;
  }

  if (!opts.save_dir.empty()) {
    game_dir_ = opts.save_dir;
  } else if (platform_.file_system) {
    game_dir_ = platform_.file_system->GetDefaultGameDir();
  }

  if (!Initializer::InitializeCore(pak_, platform_, linker_, input_, game_dir_,
                                   opts)) {
    return false;
  }

  updater_.WireHostBridges(
      world_, input_, platform_, pak_, linker_, game_dir_,
      [this]() { Quit(); },
      [this]() {
        Initializer::ReloadResourcePacks(
            pak_, platform_, linker_, game_dir_,
            screen::DisplayHost::Instance().active_resource_packs,
            &custom_texture_ids_);
      });

  if (!screen::DisplayHost::Instance().active_resource_packs.empty()) {
    Initializer::ReloadResourcePacks(
        pak_, platform_, linker_, game_dir_,
        screen::DisplayHost::Instance().active_resource_packs,
        &custom_texture_ids_);
  }

  world_.ResetGame(&input_, false, false);
  updater_.SetDisplay(std::make_unique<screen::TitleDisplay>());
  running_ = true;
  return true;
}

void Game::RunMainLoop(int32_t max_frames) {
  if (!running_ || !platform_.clock) return;

  int64_t last_tick = platform_.clock->NowNanoseconds();
  int64_t last_render = last_tick;
  int64_t last_timer_ms = platform_.clock->NowMilliseconds();
  double unprocessed = 0.0;
  int32_t frames = 0;
  int32_t ticks = 0;
  int32_t cur_fps = 0;
  int32_t cur_tps = 0;
  int32_t total_rendered = 0;

  while (running_) {
    if (platform_.window) {
      platform_.window->PollEvents();
      if (platform_.window->ShouldClose()) {
        Quit();
        break;
      }

      std::vector<platform::KeyEvent> keys;
      std::vector<platform::CharEvent> chars;
      platform_.window->DrainInputEvents(&keys, &chars);
      input_.PushKeyEvents(keys);
      input_.PushCharEvents(chars);
      const uint64_t now_ms = platform_.clock
                                  ? static_cast<uint64_t>(
                                        platform_.clock->NowMilliseconds())
                                  : 0;
      input_.UpdateGamepad(platform_.window->PollGamepad(), now_ms);
    }

    const int64_t now = platform_.clock->NowNanoseconds();
    double ns_per_tick = 1e9 / static_cast<double>(GameState::kNormSpeed);
    if (!updater_.HasActiveDisplay()) {
      ns_per_tick /= std::max(0.05f, GameState::Instance().game_speed);
    }

    unprocessed += static_cast<double>(now - last_tick) / ns_per_tick;
    last_tick = now;

    // Prevent spiral of death while still allowing 20x sleep fast-forward.
    const double max_unprocessed = std::max(
        10.0, 2.0 * static_cast<double>(GameState::Instance().game_speed));
    if (unprocessed > max_unprocessed) unprocessed = max_unprocessed;

    while (unprocessed >= 1.0) {
      ++ticks;
      updater_.Tick(world_, input_, platform_, game_dir_);
      unprocessed -= 1.0;
    }

    const int32_t max_fps = std::clamp(io::Settings::GetMaxFps(), 10, 300);
    const double ns_per_frame = 1e9 / static_cast<double>(max_fps);
    const int64_t now_render = platform_.clock->NowNanoseconds();
    if (static_cast<double>(now_render - last_render) >= ns_per_frame / 1.01) {
      ++frames;
      ++total_rendered;
      last_render = now_render;

      renderer_.Render(world_, updater_, screen_, platform_, game_dir_, cur_fps,
                       cur_tps);
      if (platform_.window) {
        platform_.window->SwapBuffers();
      }
      if (max_frames > 0 && total_rendered >= max_frames) {
        Quit();
        break;
      }
    }

    // Smart sleep: yield CPU cleanly until next tick or frame.
    const int64_t cur_ns = platform_.clock->NowNanoseconds();
    const double until_next_tick =
        static_cast<double>(last_tick) + ns_per_tick - cur_ns;
    const double until_next_frame =
        static_cast<double>(last_render) + ns_per_frame - cur_ns;
    const double wait_ns = std::min(until_next_tick, until_next_frame) / 1.25;
    if (wait_ns > 500000.0) {  // Only sleep if >= 0.5 ms to stay crisp.
      platform_.clock->SleepNanoseconds(static_cast<int64_t>(wait_ns));
    }

    const int64_t now_ms = platform_.clock->NowMilliseconds();
    if (now_ms - last_timer_ms >= 1000) {
      const int64_t interval = now_ms - last_timer_ms;
      last_timer_ms = now_ms;
      cur_fps = static_cast<int32_t>(std::round(frames * 1000.0 / interval));
      cur_tps = static_cast<int32_t>(std::round(ticks * 1000.0 / interval));
      frames = 0;
      ticks = 0;
    }
  }
}

void Game::Shutdown() {
  updater_.ClearDisplays();
  world_.UnloadWorld(platform_.clock);
  if (platform_.renderer) {
    for (uint16_t tid : custom_texture_ids_) {
      if (tid > 0) platform_.renderer->DestroyTexture(tid);
    }
  }
  custom_texture_ids_.clear();
  io::Sound::Shutdown();
}

}  // namespace minicraft::game
