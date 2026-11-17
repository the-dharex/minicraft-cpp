#ifndef MINICRAFT_CORE_IO_SOUND_H_
#define MINICRAFT_CORE_IO_SOUND_H_

#include <string_view>

#include "core/pak/pak_reader.h"
#include "core/platform/audio_backend.h"

namespace minicraft {
namespace io {

class Sound {
 public:
  static void Init(platform::IAudioBackend* backend,
                   const pak::PakReader& pak);
  static void ReloadFromPak(const pak::PakReader& pak);
  static void Shutdown();

  static void Play(std::string_view name);

 private:
  static platform::IAudioBackend* backend_;
};

}  // namespace io
}  // namespace minicraft

#endif  // MINICRAFT_CORE_IO_SOUND_H_
