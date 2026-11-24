#ifndef MINICRAFT_CORE_PLATFORM_FILE_SYSTEM_H_
#define MINICRAFT_CORE_PLATFORM_FILE_SYSTEM_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace minicraft {
namespace platform {

// platform-agnostic filesystem and image I/O interface.
class IFileSystem {
 public:
  virtual ~IFileSystem() = default;

  virtual std::string GetDefaultGameDir() const = 0;
  virtual bool Exists(std::string_view path) const = 0;
  virtual bool IsDirectory(std::string_view path) const = 0;
  virtual bool CreateDirectories(std::string_view path) const = 0;
  virtual bool RemoveFile(std::string_view path) const = 0;
  virtual bool RemoveDirectoryAll(std::string_view path) const = 0;
  virtual bool Rename(std::string_view from, std::string_view to) const = 0;

  virtual bool ReadBinaryFile(std::string_view path,
                              std::vector<uint8_t>* out) const = 0;
  virtual bool ReadBinaryHeader(std::string_view path, uint8_t* out,
                                size_t max_bytes,
                                size_t* out_bytes_read) const {
    if (!out || !out_bytes_read) return false;
    *out_bytes_read = 0;
    std::vector<uint8_t> full;
    if (!ReadBinaryFile(path, &full)) return false;
    const size_t n = full.size() < max_bytes ? full.size() : max_bytes;
    for (size_t i = 0; i < n; ++i) out[i] = full[i];
    *out_bytes_read = n;
    return true;
  }
  virtual bool WriteBinaryFile(std::string_view path, const uint8_t* data,
                               size_t size) const = 0;
  virtual bool ReadTextFile(std::string_view path, std::string* out) const = 0;
  virtual bool WriteTextFile(std::string_view path,
                             std::string_view text) const = 0;
  virtual std::vector<std::string> ListDirectory(
      std::string_view path) const = 0;

  virtual bool SaveScreenshotPng(std::string_view path, int32_t width,
                                 int32_t height, const uint8_t* rgba) const = 0;
  virtual bool DecodePngFromMemory(const uint8_t* png_bytes, size_t size,
                                   int32_t* out_w, int32_t* out_h,
                                   std::vector<uint8_t>* out_rgba) const = 0;
};

}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PLATFORM_FILE_SYSTEM_H_
