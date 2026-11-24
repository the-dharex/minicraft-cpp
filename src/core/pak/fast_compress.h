#ifndef MINICRAFT_CORE_PAK_FAST_COMPRESS_H_
#define MINICRAFT_CORE_PAK_FAST_COMPRESS_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace minicraft {
namespace pak {

// Compresses |input| using a fast LZ77 codec with 64 KB sliding window.
std::vector<uint8_t> CompressFastLz(const uint8_t* input, size_t size);

// Decompresses |input| into |output| knowing exact |expected_raw_size|.
// Returns false if the stream is malformed.
bool DecompressFastLz(const uint8_t* input, size_t packed_size,
                      size_t expected_raw_size, std::vector<uint8_t>* output);

}  // namespace pak
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PAK_FAST_COMPRESS_H_
