#include "core/pak/fast_compress.h"

#include <algorithm>
#include <cstring>

namespace minicraft {
namespace pak {

namespace {

constexpr size_t kWindowSize = 65535;
constexpr size_t kMaxLiteralRun = 128;
constexpr size_t kMinMatch = 3;
constexpr size_t kMaxMatch = 130;  // 127 + 3
constexpr size_t kHashBits = 15;
constexpr size_t kHashSize = 1U << kHashBits;

inline uint32_t Hash3(const uint8_t* p) {
  uint32_t v = static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16);
  return ((v * 2654435761U) >> (32 - kHashBits)) & (kHashSize - 1);
}

void EmitLiterals(const uint8_t* src, size_t count, std::vector<uint8_t>* out) {
  while (count > 0) {
    size_t chunk = std::min(count, kMaxLiteralRun);
    out->push_back(static_cast<uint8_t>(chunk - 1));
    out->insert(out->end(), src, src + chunk);
    src += chunk;
    count -= chunk;
  }
}

}  // namespace

std::vector<uint8_t> CompressFastLz(const uint8_t* input, size_t size) {
  std::vector<uint8_t> out;
  if (size == 0 || input == nullptr) {
    return out;
  }
  out.reserve(size / 2 + 64);

  std::vector<int32_t> hash_table(kHashSize, -1);

  size_t pos = 0;
  size_t literal_start = 0;

  while (pos + kMinMatch <= size) {
    uint32_t h = Hash3(input + pos);
    int32_t ref = hash_table[h];
    hash_table[h] = static_cast<int32_t>(pos);

    if (ref >= 0 && pos - static_cast<size_t>(ref) <= kWindowSize &&
        pos > static_cast<size_t>(ref)) {
      size_t dist = pos - static_cast<size_t>(ref);
      const uint8_t* a = input + ref;
      const uint8_t* b = input + pos;
      size_t max_len = std::min(kMaxMatch, size - pos);
      size_t match_len = 0;
      while (match_len < max_len && a[match_len] == b[match_len]) {
        ++match_len;
      }

      if (match_len >= 4 || (match_len == 3 && literal_start == pos)) {
        if (pos > literal_start) {
          EmitLiterals(input + literal_start, pos - literal_start, &out);
        }
        out.push_back(static_cast<uint8_t>(128 + (match_len - kMinMatch)));
        out.push_back(static_cast<uint8_t>(dist & 0xFFU));
        out.push_back(static_cast<uint8_t>((dist >> 8) & 0xFFU));

        // Update hash entries inside the match for better compression ratio.
        size_t end_pos = pos + match_len;
        for (size_t i = pos + 1; i + kMinMatch <= size && i < end_pos; ++i) {
          hash_table[Hash3(input + i)] = static_cast<int32_t>(i);
        }
        pos = end_pos;
        literal_start = pos;
        continue;
      }
    }
    ++pos;
  }

  if (literal_start < size) {
    EmitLiterals(input + literal_start, size - literal_start, &out);
  }
  return out;
}

bool DecompressFastLz(const uint8_t* input, size_t packed_size,
                      size_t expected_raw_size, std::vector<uint8_t>* output) {
  if (output == nullptr) {
    return false;
  }
  output->clear();
  if (expected_raw_size == 0) {
    return packed_size == 0;
  }
  if (input == nullptr || packed_size == 0) {
    return false;
  }

  output->resize(expected_raw_size);
  uint8_t* dst = output->data();
  size_t out_pos = 0;
  size_t in_pos = 0;

  while (in_pos < packed_size) {
    uint8_t tag = input[in_pos++];
    if (tag < 128) {
      size_t lit_len = static_cast<size_t>(tag) + 1;
      if (in_pos + lit_len > packed_size ||
          out_pos + lit_len > expected_raw_size) {
        return false;
      }
      std::memcpy(dst + out_pos, input + in_pos, lit_len);
      in_pos += lit_len;
      out_pos += lit_len;
    } else {
      if (in_pos + 2 > packed_size) {
        return false;
      }
      size_t match_len = static_cast<size_t>(tag - 128) + kMinMatch;
      size_t dist = static_cast<size_t>(input[in_pos]) |
                    (static_cast<size_t>(input[in_pos + 1]) << 8);
      in_pos += 2;
      if (dist == 0 || dist > out_pos ||
          out_pos + match_len > expected_raw_size) {
        return false;
      }
      // Byte-by-byte copy handles overlapping matches (RLE runs when dist < match_len).
      for (size_t i = 0; i < match_len; ++i) {
        dst[out_pos + i] = dst[out_pos - dist + i];
      }
      out_pos += match_len;
    }
  }

  return out_pos == expected_raw_size;
}

}  // namespace pak
}  // namespace minicraft
