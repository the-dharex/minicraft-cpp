#ifndef MINICRAFT_PLATFORM_PC_MINIAUDIO_BACKEND_H_
#define MINICRAFT_PLATFORM_PC_MINIAUDIO_BACKEND_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/platform/audio_backend.h"

namespace minicraft {
namespace platform {
namespace pc {

class MiniaudioBackend final : public IAudioBackend {
 public:
  MiniaudioBackend();
  ~MiniaudioBackend() override;

  MiniaudioBackend(const MiniaudioBackend&) = delete;
  MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;

  bool Init() override;
  void Shutdown() override;

  bool LoadWavFromMemory(std::string_view key, const uint8_t* data,
                         size_t size) override;
  void UnloadAllSounds() override;
  void PlayClip(std::string_view key) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pc
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_PC_MINIAUDIO_BACKEND_H_
