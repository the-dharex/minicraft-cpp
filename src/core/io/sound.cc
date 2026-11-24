#include "core/io/sound.h"

#include <vector>

#include "core/io/settings.h"
#include "core/util/logging.h"

namespace minicraft {
namespace io {

platform::IAudioBackend* Sound::backend_ = nullptr;

namespace {

constexpr const char* kSoundKeys[] = {
    "bossdeath", "confirm", "craft", "death",      "explode",
    "fuse",      "monsterhurt", "pickup", "playerhurt", "select",
};

}  // namespace

void Sound::Init(platform::IAudioBackend* backend, const pak::PakReader& pak) {
  backend_ = backend;
  ReloadFromPak(pak);
}

void Sound::ReloadFromPak(const pak::PakReader& pak) {
  if (!backend_) return;
  backend_->UnloadAllSounds();
  std::vector<uint8_t> wav_data;
  for (const char* key : kSoundKeys) {
    std::string path = std::string("assets/sound/") + key + ".wav";
    if (pak.ReadEntry(path, &wav_data)) {
      if (!backend_->LoadWavFromMemory(key, wav_data.data(), wav_data.size())) {
        util::log::kSound.Warn(std::string("Failed decoding wav: ") + path);
      }
    } else {
      util::log::kSound.Warn(std::string("Missing sound in pak: ") + path);
    }
  }
}

void Sound::Shutdown() {
  if (backend_) {
    backend_->UnloadAllSounds();
  }
  backend_ = nullptr;
}

void Sound::Play(std::string_view name) {
  if (!backend_) return;
  if (!Settings::GetBool("sound")) return;
  backend_->PlayClip(name);
}

}  // namespace io
}  // namespace minicraft
