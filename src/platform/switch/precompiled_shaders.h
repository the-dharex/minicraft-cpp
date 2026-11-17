#pragma once

#include <cstddef>
#include <cstdint>

namespace minicraft::platform::nx {

struct PrecompiledShader {
  const char* name;
  const uint8_t* spv_data;
  size_t spv_size;
  const char* glsl_fallback;
};

const PrecompiledShader* FindPrecompiledShader(const char* name);

}  // namespace minicraft::platform::nx
