#include "platform/pc/miniaudio_backend.h"

#include <array>
#include <chrono>
#include <cstring>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_MP3
#include "miniaudio.h"

namespace minicraft {
namespace platform {
namespace pc {

namespace {

constexpr size_t kVoicePoolSize = 24;
constexpr auto kSameClipCooldown = std::chrono::milliseconds(22);

struct PcmClip {
  ma_format format = ma_format_s16;
  ma_uint32 channels = 2;
  ma_uint32 sample_rate = 44100;
  std::vector<int16_t> samples;
  bool has_played = false;
  std::chrono::steady_clock::time_point last_played_at{};
};

struct VoiceSlot {
  bool in_use = false;
  ma_audio_buffer buffer{};
  ma_sound sound{};
};

}  // namespace

struct MiniaudioBackend::Impl {
  bool initialized = false;
  ma_engine engine{};
  std::unordered_map<std::string, PcmClip> clips;
  std::array<VoiceSlot, kVoicePoolSize> voices{};
  size_t next_voice = 0;

  void ReleaseVoice(VoiceSlot& slot) {
    if (slot.in_use) {
      ma_sound_uninit(&slot.sound);
      ma_audio_buffer_uninit(&slot.buffer);
      slot.in_use = false;
    }
  }
};

MiniaudioBackend::MiniaudioBackend() : impl_(std::make_unique<Impl>()) {}

MiniaudioBackend::~MiniaudioBackend() { Shutdown(); }

bool MiniaudioBackend::Init() {
  if (impl_->initialized) {
    return true;
  }
  ma_engine_config cfg = ma_engine_config_init();
  cfg.channels = 2;
  cfg.sampleRate = 44100;
  if (ma_engine_init(&cfg, &impl_->engine) != MA_SUCCESS) {
    return false;
  }
  impl_->initialized = true;
  return true;
}

void MiniaudioBackend::Shutdown() {
  if (!impl_ || !impl_->initialized) {
    return;
  }
  for (VoiceSlot& slot : impl_->voices) {
    impl_->ReleaseVoice(slot);
  }
  impl_->clips.clear();
  ma_engine_uninit(&impl_->engine);
  impl_->initialized = false;
}

bool MiniaudioBackend::LoadWavFromMemory(std::string_view key,
                                         const uint8_t* data, size_t size) {
  if (!impl_->initialized || data == nullptr || size == 0) {
    return false;
  }

  ma_decoder_config dec_cfg =
      ma_decoder_config_init(ma_format_s16, 2, 44100);
  ma_decoder decoder{};
  if (ma_decoder_init_memory(data, size, &dec_cfg, &decoder) != MA_SUCCESS) {
    return false;
  }

  PcmClip clip;
  clip.format = ma_format_s16;
  clip.channels = 2;
  clip.sample_rate = 44100;

  constexpr ma_uint64 kChunkFrames = 4096;
  std::vector<int16_t> temp(kChunkFrames * clip.channels);
  for (;;) {
    ma_uint64 frames_read = 0;
    ma_result res = ma_decoder_read_pcm_frames(
        &decoder, temp.data(), kChunkFrames, &frames_read);
    if (frames_read > 0) {
      clip.samples.insert(clip.samples.end(), temp.begin(),
                          temp.begin() + frames_read * clip.channels);
    }
    if (res != MA_SUCCESS || frames_read < kChunkFrames) {
      break;
    }
  }
  ma_decoder_uninit(&decoder);

  if (clip.samples.empty()) {
    return false;
  }
  impl_->clips[std::string(key)] = std::move(clip);
  return true;
}

void MiniaudioBackend::UnloadAllSounds() {
  if (!impl_) return;
  for (VoiceSlot& slot : impl_->voices) {
    impl_->ReleaseVoice(slot);
  }
  impl_->clips.clear();
}

void MiniaudioBackend::PlayClip(std::string_view key) {
  if (!impl_->initialized) {
    return;
  }
  auto it = impl_->clips.find(std::string(key));
  if (it == impl_->clips.end()) {
    return;
  }
  PcmClip& clip = it->second;
  if (clip.samples.empty()) {
    return;
  }

  // Collapse same-tick / 60Hz identical clip retriggers to avoid machine-gun stacking.
  const auto now = std::chrono::steady_clock::now();
  if (clip.has_played && (now - clip.last_played_at) < kSameClipCooldown) {
    return;
  }
  clip.has_played = true;
  clip.last_played_at = now;

  // Prefer an already-finished or free voice slot.
  size_t chosen = impl_->next_voice;
  for (size_t i = 0; i < kVoicePoolSize; ++i) {
    size_t idx = (impl_->next_voice + i) % kVoicePoolSize;
    VoiceSlot& v = impl_->voices[idx];
    if (!v.in_use || ma_sound_at_end(&v.sound)) {
      chosen = idx;
      break;
    }
  }
  impl_->next_voice = (chosen + 1) % kVoicePoolSize;

  VoiceSlot& slot = impl_->voices[chosen];
  impl_->ReleaseVoice(slot);

  ma_uint64 frame_count = clip.samples.size() / clip.channels;
  ma_audio_buffer_config buf_cfg = ma_audio_buffer_config_init(
      clip.format, clip.channels, frame_count, clip.samples.data(), nullptr);
  buf_cfg.sampleRate = clip.sample_rate;
  if (ma_audio_buffer_init(&buf_cfg, &slot.buffer) != MA_SUCCESS) {
    return;
  }
  if (ma_sound_init_from_data_source(&impl_->engine, &slot.buffer,
                                     MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                     &slot.sound) != MA_SUCCESS) {
    ma_audio_buffer_uninit(&slot.buffer);
    return;
  }
  slot.in_use = true;
  ma_sound_start(&slot.sound);
}

}  // namespace pc
}  // namespace platform
}  // namespace minicraft
