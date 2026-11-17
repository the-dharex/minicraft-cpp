#ifndef MINICRAFT_PLATFORM_PC_PRECOMPILED_SHADERS_H_
#define MINICRAFT_PLATFORM_PC_PRECOMPILED_SHADERS_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace minicraft {
namespace platform {
namespace pc {

struct PrecompiledShader {
  std::string_view name;
  const uint8_t* spv_data;
  size_t spv_size;
  const char* glsl_fallback;
};

const PrecompiledShader* FindPrecompiledShader(std::string_view name);
const PrecompiledShader* AllPrecompiledShaders(size_t* out_count);

}  // namespace pc
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_PC_PRECOMPILED_SHADERS_H_
