#ifndef MINICRAFT_CORE_PLATFORM_AUDIO_BACKEND_H_
#define MINICRAFT_CORE_PLATFORM_AUDIO_BACKEND_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace minicraft {
namespace platform {

// platform-agnostic audio backend interface.
class IAudioBackend {
 public:
  virtual ~IAudioBackend() = default;

  virtual bool Init() = 0;
  virtual void Shutdown() = 0;

  virtual bool LoadWavFromMemory(std::string_view key, const uint8_t* data,
                                 size_t size) = 0;
  virtual void UnloadAllSounds() = 0;
  virtual void PlayClip(std::string_view key) = 0;
};

}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PLATFORM_AUDIO_BACKEND_H_
