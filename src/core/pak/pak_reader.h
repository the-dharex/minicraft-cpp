#ifndef MINICRAFT_CORE_PAK_PAK_READER_H_
#define MINICRAFT_CORE_PAK_PAK_READER_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/pak/pak_format.h"

namespace minicraft {
namespace pak {

// Pure C++ platform-agnostic reader for MPAK (.pak) archives.
class PakReader {
 public:
  PakReader() = default;
  ~PakReader() = default;

  PakReader(const PakReader&) = delete;
  PakReader& operator=(const PakReader&) = delete;
  PakReader(PakReader&&) = default;
  PakReader& operator=(PakReader&&) = default;

  // Initializes the reader from raw archive bytes loaded by the platform layer.
  bool LoadFromMemory(std::vector<uint8_t> pak_bytes);

  bool IsLoaded() const { return loaded_; }
  size_t EntryCount() const { return entries_.size(); }

  bool HasEntry(std::string_view path) const;
  bool ReadEntry(std::string_view path, std::vector<uint8_t>* out_data) const;
  bool ReadTextEntry(std::string_view path, std::string* out_text) const;

  // Lists all virtual entry paths in the archive (mainly for tooling/debug).
  std::vector<std::string> ListEntries() const;

 private:
  const PakTocEntry* FindEntry(std::string_view path) const;
  std::string_view EntryName(const PakTocEntry& entry) const;

  bool loaded_ = false;
  std::vector<uint8_t> archive_bytes_;
  std::vector<PakTocEntry> entries_;
  std::string string_table_;
};

}  // namespace pak
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PAK_PAK_READER_H_
