#ifndef MINICRAFT_CORE_PAK_PAK_FORMAT_H_
#define MINICRAFT_CORE_PAK_PAK_FORMAT_H_

#include <cstdint>
#include <string_view>

namespace minicraft {
namespace pak {

inline constexpr char kPakMagic[4] = {'M', 'P', 'A', 'K'};
inline constexpr uint16_t kPakVersion = 1;

enum class CompressionCodec : uint8_t {
  kNone = 0,
  kFastLz = 1,
};

#pragma pack(push, 1)
struct PakHeader {
  char magic[4];
  uint16_t version;
  uint16_t flags;
  uint32_t entry_count;
  uint32_t reserved;
  uint64_t toc_offset;
  uint64_t string_table_offset;
  uint64_t string_table_size;
};

struct PakTocEntry {
  uint64_t name_hash;
  uint32_t name_offset;
  uint32_t name_length;
  uint64_t data_offset;
  uint32_t packed_size;
  uint32_t raw_size;
  uint8_t codec;
  uint8_t reserved[3];
};

// Pre-baked atlas binary header stored at "__atlas__/atlas.bin".
struct AtlasHeader {
  char magic[4];  // "MATL"
  uint16_t width;
  uint16_t height;
  uint32_t sheet_count;
};

struct AtlasSheetEntry {
  uint8_t category;  // 0=Entity, 1=Gui, 2=Item, 3=Tile, 4=Misc
  uint8_t reserved;
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
  uint16_t name_length;
};
#pragma pack(pop)

inline constexpr uint64_t Fnv1a64(std::string_view text) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

}  // namespace pak
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PAK_PAK_FORMAT_H_
