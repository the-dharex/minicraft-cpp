#ifndef MINICRAFT_PLATFORM_PC_PC_FILE_SYSTEM_H_
#define MINICRAFT_PLATFORM_PC_PC_FILE_SYSTEM_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/platform/file_system.h"

namespace minicraft {
namespace platform {
namespace pc {

class PcFileSystem final : public IFileSystem {
 public:
  std::string GetDefaultGameDir() const override;
  bool Exists(std::string_view path) const override;
  bool IsDirectory(std::string_view path) const override;
  bool CreateDirectories(std::string_view path) const override;
  bool RemoveFile(std::string_view path) const override;
  bool RemoveDirectoryAll(std::string_view path) const override;
  bool Rename(std::string_view from, std::string_view to) const override;

  bool ReadBinaryFile(std::string_view path,
                      std::vector<uint8_t>* out) const override;
  bool WriteBinaryFile(std::string_view path, const uint8_t* data,
                       size_t size) const override;
  bool ReadTextFile(std::string_view path, std::string* out) const override;
  bool WriteTextFile(std::string_view path,
                     std::string_view text) const override;
  std::vector<std::string> ListDirectory(std::string_view path) const override;

  bool SaveScreenshotPng(std::string_view path, int32_t width, int32_t height,
                         const uint8_t* rgba) const override;
  bool DecodePngFromMemory(const uint8_t* png_bytes, size_t size,
                           int32_t* out_w, int32_t* out_h,
                           std::vector<uint8_t>* out_rgba) const override;
};

}  // namespace pc
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_PC_PC_FILE_SYSTEM_H_
