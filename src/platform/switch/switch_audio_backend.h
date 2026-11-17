#pragma once

#include <switch.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/platform/audio_backend.h"

namespace minicraft::platform::nx {

// Native Nintendo Switch audio mixer backend using `audren` and `audrv`
// with a 24-voice polyphonic pool, hardware sample-rate conversion, aligned
// audio mempools, per-clip cooldown, and focus/sleep pause-resume handling.
class SwitchAudioBackend final : public IAudioBackend {
 public:
  static constexpr int kMaxVoices = 24;

  SwitchAudioBackend() = default;
  ~SwitchAudioBackend() override;

  bool Init() override;
  void Shutdown() override;

  bool LoadWavFromMemory(std::string_view key, const uint8_t* data,
                         size_t size) override;
  void UnloadAllSounds() override;
  void PlayClip(std::string_view key) override;

  // Advances audio driver command lists once per frame.
  void Update();

  // Pauses or resumes active voices when the application loses/regains focus.
  void SetPaused(bool paused);

 private:
  struct SoundClip {
    void* mempool_buf = nullptr;
    std::size_t mempool_size = 0;
    std::size_t pcm_bytes = 0;
    int mempool_id = -1;
    int channels = 0;
    int sample_rate = 48000;
    uint32_t sample_count = 0;
    uint64_t last_played_ms = 0;
  };

  struct VoiceSlot {
    AudioDriverWaveBuf wavebuf{};
    bool active = false;
    bool paused_by_focus = false;
    uint64_t started_seq = 0;
  };

  static bool ParseWavToPcm16(const uint8_t* data, size_t size,
                              int* out_channels, int* out_sample_rate,
                              std::vector<int16_t>* out_pcm);
  void ReleaseClip(SoundClip* clip);
  int AcquireVoiceSlot();

  AudioDriver driver_{};
  bool initialized_ = false;
  bool paused_ = false;
  uint64_t play_sequence_ = 0;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, SoundClip> clips_;
  VoiceSlot voices_[kMaxVoices]{};
};

}  // namespace minicraft::platform::nx
