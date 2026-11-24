#ifndef MINICRAFT_CORE_SAVELOAD_BINARY_CODEC_H_
#define MINICRAFT_CORE_SAVELOAD_BINARY_CODEC_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace minicraft::saveload {

inline constexpr std::string_view kBinaryWorldFileName = "world.mcbin";
inline constexpr std::string_view kBinaryWorldBackupFileName = "world.mcbin.bak";
inline constexpr std::string_view kBinaryWorldTempFileName = "world.mcbin.tmp";

inline constexpr std::array<uint8_t, 8> kBinaryMagic = {
    'M', 'C', 'P', 'B', 'S', 'A', 'V', 'E'};
inline constexpr uint32_t kBinaryFormatVersion = 1;
inline constexpr uint32_t kContainerFlagCompressed = 0x1u;
inline constexpr size_t kBinaryPreambleSize = 40;

// Strict bounds caps to reject corrupted or malicious binary inputs safely.
inline constexpr uint32_t kMaxBinaryHeaderSize = 64 * 1024;
inline constexpr uint32_t kMaxBinaryPayloadSize = 256 * 1024 * 1024;
inline constexpr uint32_t kMaxString32Len = 4 * 1024 * 1024;
inline constexpr uint32_t kMaxChunksPerLevel = 65536;
inline constexpr uint32_t kMaxInventoryItems = 16384;
inline constexpr uint32_t kMaxWorldEntities = 262144;
inline constexpr uint32_t kMaxWorldSigns = 65536;
inline constexpr uint8_t kMaxSignLines = 16;
inline constexpr int32_t kMaxNestedItemDepth = 4;

// Chunk encoding bitflags.
inline constexpr uint8_t kChunkFlagTilesUniform = 0x1u;
inline constexpr uint8_t kChunkFlagDataAllZero = 0x2u;

// World header boolean bitflags.
inline constexpr uint8_t kWorldFlagAirWizardBeaten = 1u << 0;
inline constexpr uint8_t kWorldFlagObsidianKnightBeaten = 1u << 1;
inline constexpr uint8_t kWorldFlagObsidianKnightSummoned = 1u << 2;
inline constexpr uint8_t kWorldFlagPastDay1 = 1u << 3;
inline constexpr uint8_t kWorldFlagQuests = 1u << 4;
inline constexpr uint8_t kWorldFlagTutorials = 1u << 5;

enum class BinaryEntityKind : uint8_t {
  kCow = 1,
  kPig = 2,
  kSheep = 3,
  kZombie = 4,
  kSlime = 5,
  kCreeper = 6,
  kSkeleton = 7,
  kKnight = 8,
  kSnake = 9,
  kAirWizard = 10,
  kObsidianKnight = 11,
  kSpawner = 12,
  kWorkbench = 13,
  kAnvil = 14,
  kEnchanter = 15,
  kLoom = 16,
  kFurnace = 17,
  kOven = 18,
  kDyeVat = 19,
  kComposter = 20,
  kChest = 21,
  kDeathChest = 22,
  kDungeonChest = 23,
  kBed = 24,
  kTnt = 25,
  kLantern = 26,
  kKnightStatue = 27,
  kBoat = 28,
};

namespace detail {

constexpr std::array<uint32_t, 256> MakeCrc32Table() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int bit = 0; bit < 8; ++bit) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    table[i] = c;
  }
  return table;
}

inline constexpr std::array<uint32_t, 256> kCrc32Table = MakeCrc32Table();

}  // namespace detail

// Computes IEEE 802.3 CRC-32 checksum. Thread-safe and constexpr-initialized.
inline uint32_t ComputeCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  if (data != nullptr) {
    for (size_t i = 0; i < size; ++i) {
      crc = detail::kCrc32Table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

// Explicit little-endian binary writer with zero struct-padding assumptions.
class BinaryWriter {
 public:
  explicit BinaryWriter(size_t reserve_bytes = 0) {
    if (reserve_bytes > 0) buf_.reserve(reserve_bytes);
  }

  const std::vector<uint8_t>& Buffer() const { return buf_; }
  std::vector<uint8_t> TakeBuffer() { return std::move(buf_); }
  size_t Size() const { return buf_.size(); }

  void WriteU8(uint8_t v) { buf_.push_back(v); }

  void WriteU16(uint16_t v) {
    buf_.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
  }

  void WriteI16(int16_t v) { WriteU16(static_cast<uint16_t>(v)); }

  void WriteU32(uint32_t v) {
    buf_.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
  }

  void WriteI32(int32_t v) { WriteU32(static_cast<uint32_t>(v)); }

  void WriteU64(uint64_t v) {
    WriteU32(static_cast<uint32_t>(v & 0xFFFFFFFFULL));
    WriteU32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFULL));
  }

  void WriteI64(int64_t v) { WriteU64(static_cast<uint64_t>(v)); }

  void WriteBytes(const uint8_t* data, size_t len) {
    if (len > 0 && data != nullptr) {
      buf_.insert(buf_.end(), data, data + len);
    }
  }

  void WriteStr16(std::string_view s) {
    const size_t n = s.size() > 65535u ? 65535u : s.size();
    WriteU16(static_cast<uint16_t>(n));
    WriteBytes(reinterpret_cast<const uint8_t*>(s.data()), n);
  }

  void WriteStr32(std::string_view s) {
    const size_t n = s.size() > kMaxString32Len ? kMaxString32Len : s.size();
    WriteU32(static_cast<uint32_t>(n));
    WriteBytes(reinterpret_cast<const uint8_t*>(s.data()), n);
  }

 private:
  std::vector<uint8_t> buf_;
};

// Bounds-checked explicit little-endian binary reader. Any out-of-range or
// cap-violating read latches `ok_ = false` without throwing or over-allocating.
class BinaryReader {
 public:
  BinaryReader(const uint8_t* data, size_t size)
      : data_(data), size_(data ? size : 0) {}

  bool Ok() const { return ok_; }
  size_t Pos() const { return pos_; }
  size_t Remaining() const { return ok_ && pos_ <= size_ ? size_ - pos_ : 0; }
  void Fail() { ok_ = false; }

  uint8_t ReadU8() {
    if (!EnsureAvailable(1)) return 0;
    return data_[pos_++];
  }

  uint16_t ReadU16() {
    if (!EnsureAvailable(2)) return 0;
    uint16_t v = static_cast<uint16_t>(data_[pos_]) |
                 (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
    pos_ += 2;
    return v;
  }

  int16_t ReadI16() { return static_cast<int16_t>(ReadU16()); }

  uint32_t ReadU32() {
    if (!EnsureAvailable(4)) return 0;
    uint32_t v = static_cast<uint32_t>(data_[pos_]) |
                 (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                 (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                 (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
  }

  int32_t ReadI32() { return static_cast<int32_t>(ReadU32()); }

  uint64_t ReadU64() {
    uint64_t lo = ReadU32();
    uint64_t hi = ReadU32();
    return lo | (hi << 32);
  }

  int64_t ReadI64() { return static_cast<int64_t>(ReadU64()); }

  bool ReadBytes(uint8_t* dst, size_t len) {
    if (!EnsureAvailable(len)) return false;
    if (len > 0 && dst != nullptr) {
      std::memcpy(dst, data_ + pos_, len);
    }
    pos_ += len;
    return true;
  }

  bool ReadStr16(std::string* out) {
    const uint16_t len = ReadU16();
    if (!EnsureAvailable(len)) return false;
    if (out) {
      out->assign(reinterpret_cast<const char*>(data_ + pos_), len);
    }
    pos_ += len;
    return true;
  }

  bool ReadStr32(std::string* out, uint32_t max_len = kMaxString32Len) {
    const uint32_t len = ReadU32();
    if (!ok_ || len > max_len || !EnsureAvailable(len)) {
      ok_ = false;
      return false;
    }
    if (out) {
      out->assign(reinterpret_cast<const char*>(data_ + pos_), len);
    }
    pos_ += len;
    return true;
  }

 private:
  bool EnsureAvailable(size_t n) {
    if (!ok_ || n > size_ - pos_) {
      ok_ = false;
      return false;
    }
    return true;
  }

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t pos_ = 0;
  bool ok_ = true;
};

struct BinaryPreamble {
  uint32_t format_version = 0;
  uint32_t container_flags = 0;
  uint32_t header_size = 0;
  uint32_t header_crc32 = 0;
  uint32_t payload_stored_size = 0;
  uint32_t payload_raw_size = 0;
  uint32_t payload_stored_crc32 = 0;
  uint32_t payload_raw_crc32 = 0;
};

struct BinaryWorldHeader {
  std::string version_str;
  std::string mode_str;
  std::string diff_str;
  int32_t score = 0;
  int32_t game_time = 0;
  int32_t tick_count = 0;
  int32_t score_time = 0;
  int32_t scoretime_setting = 20;
  uint8_t saved_lvl_idx = 3;
  uint8_t world_flags = 0;
};

inline std::vector<uint8_t> SerializeBinaryWorldHeader(
    const BinaryWorldHeader& h) {
  BinaryWriter w(160);
  w.WriteStr16(h.version_str);
  w.WriteStr16(h.mode_str);
  w.WriteStr16(h.diff_str);
  w.WriteI32(h.score);
  w.WriteI32(h.game_time);
  w.WriteI32(h.tick_count);
  w.WriteI32(h.score_time);
  w.WriteI32(h.scoretime_setting);
  w.WriteU8(h.saved_lvl_idx);
  w.WriteU8(h.world_flags);
  return w.TakeBuffer();
}

// Validates the 40-byte preamble and CRC-32 of the metadata header section.
// Works both on partial header reads (`ListWorlds`) and full file reads.
inline bool ParseBinaryPreambleAndHeader(const uint8_t* data, size_t avail_size,
                                         BinaryPreamble* out_pre,
                                         BinaryWorldHeader* out_hdr) {
  if (!data || avail_size < kBinaryPreambleSize) return false;
  if (std::memcmp(data, kBinaryMagic.data(), kBinaryMagic.size()) != 0) {
    return false;
  }

  BinaryReader pr(data + kBinaryMagic.size(),
                  kBinaryPreambleSize - kBinaryMagic.size());
  BinaryPreamble pre;
  pre.format_version = pr.ReadU32();
  pre.container_flags = pr.ReadU32();
  pre.header_size = pr.ReadU32();
  pre.header_crc32 = pr.ReadU32();
  pre.payload_stored_size = pr.ReadU32();
  pre.payload_raw_size = pr.ReadU32();
  pre.payload_stored_crc32 = pr.ReadU32();
  pre.payload_raw_crc32 = pr.ReadU32();
  if (!pr.Ok() || pre.format_version != kBinaryFormatVersion) return false;
  if ((pre.container_flags & ~kContainerFlagCompressed) != 0) return false;
  if (pre.header_size == 0 || pre.header_size > kMaxBinaryHeaderSize) {
    return false;
  }
  if (pre.payload_stored_size > kMaxBinaryPayloadSize ||
      pre.payload_raw_size > kMaxBinaryPayloadSize) {
    return false;
  }
  if ((pre.container_flags & kContainerFlagCompressed) == 0 &&
      pre.payload_stored_size != pre.payload_raw_size) {
    return false;
  }
  if (avail_size < kBinaryPreambleSize + static_cast<size_t>(pre.header_size)) {
    return false;
  }

  const uint8_t* hdr_ptr = data + kBinaryPreambleSize;
  if (ComputeCrc32(hdr_ptr, pre.header_size) != pre.header_crc32) {
    return false;
  }

  BinaryReader hr(hdr_ptr, pre.header_size);
  BinaryWorldHeader hdr;
  if (!hr.ReadStr16(&hdr.version_str) || !hr.ReadStr16(&hdr.mode_str) ||
      !hr.ReadStr16(&hdr.diff_str)) {
    return false;
  }
  hdr.score = hr.ReadI32();
  hdr.game_time = hr.ReadI32();
  hdr.tick_count = hr.ReadI32();
  hdr.score_time = hr.ReadI32();
  hdr.scoretime_setting = hr.ReadI32();
  hdr.saved_lvl_idx = hr.ReadU8();
  hdr.world_flags = hr.ReadU8();
  if (!hr.Ok() || hr.Remaining() != 0) return false;

  if (out_pre) *out_pre = pre;
  if (out_hdr) *out_hdr = std::move(hdr);
  return true;
}

}  // namespace minicraft::saveload

#endif  // MINICRAFT_CORE_SAVELOAD_BINARY_CODEC_H_
