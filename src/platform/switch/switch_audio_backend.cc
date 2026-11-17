#include "platform/switch/switch_audio_backend.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace minicraft::platform::nx {

namespace {

constexpr uint64_t kCooldownMs = 22;

uint16_t ReadLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

std::size_t AlignUp(std::size_t value, std::size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

SwitchAudioBackend::~SwitchAudioBackend() {
  Shutdown();
}

bool SwitchAudioBackend::Init() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) return true;

  static const AudioRendererConfig config = {
      .output_rate = AudioRendererOutputRate_48kHz,
      .num_voices = kMaxVoices,
      .num_effects = 0,
      .num_sinks = 1,
      .num_mix_objs = 1,
      .num_mix_buffers = 2,
  };

  if (R_FAILED(audrenInitialize(&config))) {
    return false;
  }

  if (R_FAILED(audrvCreate(&driver_, &config, 2))) {
    audrenExit();
    return false;
  }

  static constexpr u8 kChannelIds[] = {0, 1};
  audrvDeviceSinkAdd(&driver_, AUDREN_DEFAULT_DEVICE_NAME, 2, kChannelIds);

  if (R_FAILED(audrvUpdate(&driver_))) {
    audrvClose(&driver_);
    audrenExit();
    return false;
  }

  if (R_FAILED(audrenStartAudioRenderer())) {
    audrvClose(&driver_);
    audrenExit();
    return false;
  }

  initialized_ = true;
  paused_ = false;
  return true;
}

void SwitchAudioBackend::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) return;

  for (int i = 0; i < kMaxVoices; ++i) {
    if (voices_[i].active) {
      audrvVoiceStop(&driver_, i);
      voices_[i].active = false;
      voices_[i].paused_by_focus = false;
    }
  }
  audrvUpdate(&driver_);

  for (auto& kv : clips_) {
    ReleaseClip(&kv.second);
  }
  clips_.clear();
  audrvUpdate(&driver_);

  audrvClose(&driver_);
  audrenExit();
  initialized_ = false;
}

void SwitchAudioBackend::ReleaseClip(SoundClip* clip) {
  if (!clip) return;
  if (clip->mempool_id >= 0) {
    audrvMemPoolDetach(&driver_, clip->mempool_id);
    audrvMemPoolRemove(&driver_, clip->mempool_id);
    clip->mempool_id = -1;
  }
  if (clip->mempool_buf) {
    std::free(clip->mempool_buf);
    clip->mempool_buf = nullptr;
    clip->mempool_size = 0;
  }
}

bool SwitchAudioBackend::ParseWavToPcm16(const uint8_t* data, size_t size,
                                         int* out_channels,
                                         int* out_sample_rate,
                                         std::vector<int16_t>* out_pcm) {
  if (!data || size < 44 || !out_channels || !out_sample_rate || !out_pcm) {
    return false;
  }
  if (std::memcmp(data, "RIFF", 4) != 0 ||
      std::memcmp(data + 8, "WAVE", 4) != 0) {
    return false;
  }

  bool has_fmt = false;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;

  const uint8_t* pcm_data = nullptr;
  uint32_t pcm_bytes = 0;

  std::size_t offset = 12;
  while (offset + 8 <= size) {
    const uint8_t* chunk_id = data + offset;
    const uint32_t chunk_size = ReadLe32(data + offset + 4);
    offset += 8;
    if (offset + chunk_size > size) break;

    if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
      audio_format = ReadLe16(data + offset);
      channels = ReadLe16(data + offset + 2);
      sample_rate = ReadLe32(data + offset + 4);
      bits_per_sample = ReadLe16(data + offset + 14);
      has_fmt = true;
    } else if (std::memcmp(chunk_id, "data", 4) == 0) {
      pcm_data = data + offset;
      pcm_bytes = chunk_size;
      break;
    }

    offset += chunk_size + (chunk_size & 1u);
  }

  if (!has_fmt || !pcm_data || pcm_bytes == 0 || sample_rate == 0 ||
      (channels != 1 && channels != 2)) {
    return false;
  }

  *out_channels = static_cast<int>(channels);
  *out_sample_rate = static_cast<int>(sample_rate);

  if (audio_format == 1 && bits_per_sample == 16) {
    const std::size_t num_samples = pcm_bytes / 2;
    out_pcm->resize(num_samples);
    std::memcpy(out_pcm->data(), pcm_data, num_samples * sizeof(int16_t));
    return num_samples > 0;
  }

  if (audio_format == 1 && bits_per_sample == 8) {
    out_pcm->resize(pcm_bytes);
    for (std::size_t i = 0; i < pcm_bytes; ++i) {
      (*out_pcm)[i] =
          static_cast<int16_t>((static_cast<int>(pcm_data[i]) - 128) << 8);
    }
    return !out_pcm->empty();
  }

  if (audio_format == 1 && bits_per_sample == 24) {
    const std::size_t num_samples = pcm_bytes / 3;
    out_pcm->resize(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
      const uint8_t* s = pcm_data + i * 3;
      const int32_t v = (static_cast<int32_t>(static_cast<int8_t>(s[2])) << 16) |
                        (static_cast<int32_t>(s[1]) << 8) |
                        static_cast<int32_t>(s[0]);
      (*out_pcm)[i] = static_cast<int16_t>(v >> 8);
    }
    return !out_pcm->empty();
  }

  if (audio_format == 1 && bits_per_sample == 32) {
    const std::size_t num_samples = pcm_bytes / 4;
    out_pcm->resize(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
      int32_t v = 0;
      std::memcpy(&v, pcm_data + i * 4, sizeof(v));
      (*out_pcm)[i] = static_cast<int16_t>(v >> 16);
    }
    return !out_pcm->empty();
  }

  if (audio_format == 3 && bits_per_sample == 32) {
    const std::size_t num_samples = pcm_bytes / 4;
    out_pcm->resize(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
      float f = 0.0f;
      std::memcpy(&f, pcm_data + i * 4, sizeof(f));
      f = std::clamp(f, -1.0f, 1.0f);
      (*out_pcm)[i] = static_cast<int16_t>(f * 32767.0f);
    }
    return !out_pcm->empty();
  }

  return false;
}

bool SwitchAudioBackend::LoadWavFromMemory(std::string_view key,
                                           const uint8_t* data, size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) return false;

  int channels = 0;
  int sample_rate = 0;
  std::vector<int16_t> pcm;
  if (!ParseWavToPcm16(data, size, &channels, &sample_rate, &pcm)) {
    return false;
  }

  const std::string key_str(key);
  auto existing = clips_.find(key_str);
  if (existing != clips_.end()) {
    ReleaseClip(&existing->second);
    clips_.erase(existing);
  }

  const std::size_t pcm_bytes = pcm.size() * sizeof(int16_t);
  const std::size_t pool_size =
      AlignUp(pcm_bytes, AUDREN_MEMPOOL_ALIGNMENT);
  void* pool_buf = std::aligned_alloc(AUDREN_MEMPOOL_ALIGNMENT, pool_size);
  if (!pool_buf) {
    return false;
  }
  std::memset(pool_buf, 0, pool_size);
  std::memcpy(pool_buf, pcm.data(), pcm_bytes);
  armDCacheFlush(pool_buf, pool_size);

  const int mpid = audrvMemPoolAdd(&driver_, pool_buf, pool_size);
  if (mpid < 0 || !audrvMemPoolAttach(&driver_, mpid)) {
    if (mpid >= 0) audrvMemPoolRemove(&driver_, mpid);
    std::free(pool_buf);
    return false;
  }

  audrvUpdate(&driver_);

  SoundClip clip;
  clip.mempool_buf = pool_buf;
  clip.mempool_size = pool_size;
  clip.pcm_bytes = pcm_bytes;
  clip.mempool_id = mpid;
  clip.channels = channels;
  clip.sample_rate = sample_rate;
  clip.sample_count = static_cast<uint32_t>(pcm.size() / channels);
  clips_.emplace(key_str, clip);
  return true;
}

void SwitchAudioBackend::UnloadAllSounds() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) return;

  for (int i = 0; i < kMaxVoices; ++i) {
    if (voices_[i].active) {
      audrvVoiceStop(&driver_, i);
      voices_[i].active = false;
      voices_[i].paused_by_focus = false;
    }
  }
  audrvUpdate(&driver_);

  for (auto& kv : clips_) {
    ReleaseClip(&kv.second);
  }
  clips_.clear();
  audrvUpdate(&driver_);
}

int SwitchAudioBackend::AcquireVoiceSlot() {
  for (int i = 0; i < kMaxVoices; ++i) {
    VoiceSlot& slot = voices_[i];
    if (!slot.active) return i;
    if (slot.wavebuf.state == AudioDriverWaveBufState_Done ||
        slot.wavebuf.state == AudioDriverWaveBufState_Free ||
        !audrvVoiceIsPlaying(&driver_, i)) {
      slot.active = false;
      return i;
    }
  }

  // All voices busy: steal the oldest voice.
  int victim = 0;
  for (int i = 1; i < kMaxVoices; ++i) {
    if (voices_[i].started_seq < voices_[victim].started_seq) {
      victim = i;
    }
  }
  audrvVoiceStop(&driver_, victim);
  voices_[victim].active = false;
  voices_[victim].paused_by_focus = false;
  return victim;
}

void SwitchAudioBackend::PlayClip(std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || paused_) return;

  auto it = clips_.find(std::string(key));
  if (it == clips_.end()) return;

  SoundClip& clip = it->second;
  const uint64_t now_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
  if (now_ms - clip.last_played_ms < kCooldownMs) {
    return;
  }
  clip.last_played_ms = now_ms;

  const int voice_id = AcquireVoiceSlot();
  if (!audrvVoiceInit(&driver_, voice_id, clip.channels, PcmFormat_Int16,
                      clip.sample_rate)) {
    return;
  }

  audrvVoiceSetDestinationMix(&driver_, voice_id, AUDREN_FINAL_MIX_ID);
  if (clip.channels == 1) {
    audrvVoiceSetMixFactor(&driver_, voice_id, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&driver_, voice_id, 1.0f, 0, 1);
  } else {
    audrvVoiceSetMixFactor(&driver_, voice_id, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&driver_, voice_id, 0.0f, 0, 1);
    audrvVoiceSetMixFactor(&driver_, voice_id, 0.0f, 1, 0);
    audrvVoiceSetMixFactor(&driver_, voice_id, 1.0f, 1, 1);
  }
  audrvVoiceSetVolume(&driver_, voice_id, 1.0f);

  VoiceSlot& slot = voices_[voice_id];
  std::memset(&slot.wavebuf, 0, sizeof(slot.wavebuf));
  slot.wavebuf.data_raw = clip.mempool_buf;
  slot.wavebuf.size = clip.pcm_bytes;
  slot.wavebuf.start_sample_offset = 0;
  slot.wavebuf.end_sample_offset = static_cast<s32>(clip.sample_count);
  slot.wavebuf.is_looping = false;

  if (!audrvVoiceAddWaveBuf(&driver_, voice_id, &slot.wavebuf)) {
    return;
  }

  audrvVoiceStart(&driver_, voice_id);
  slot.active = true;
  slot.paused_by_focus = false;
  slot.started_seq = ++play_sequence_;

  audrvUpdate(&driver_);
}

void SwitchAudioBackend::Update() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) return;

  for (int i = 0; i < kMaxVoices; ++i) {
    VoiceSlot& slot = voices_[i];
    if (slot.active &&
        (slot.wavebuf.state == AudioDriverWaveBufState_Done ||
         slot.wavebuf.state == AudioDriverWaveBufState_Free)) {
      slot.active = false;
      slot.paused_by_focus = false;
    }
  }
  audrvUpdate(&driver_);
}

void SwitchAudioBackend::SetPaused(bool paused) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || paused == paused_) return;
  paused_ = paused;

  for (int i = 0; i < kMaxVoices; ++i) {
    VoiceSlot& slot = voices_[i];
    if (!slot.active) continue;
    if (paused) {
      if (audrvVoiceIsPlaying(&driver_, i) && !audrvVoiceIsPaused(&driver_, i)) {
        audrvVoiceSetPaused(&driver_, i, true);
        slot.paused_by_focus = true;
      }
    } else if (slot.paused_by_focus) {
      audrvVoiceSetPaused(&driver_, i, false);
      slot.paused_by_focus = false;
    }
  }
  audrvUpdate(&driver_);
}

}  // namespace minicraft::platform::nx
