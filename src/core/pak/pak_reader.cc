#include "core/pak/pak_reader.h"

#include <algorithm>
#include <cstring>

#include "core/pak/fast_compress.h"

namespace minicraft {
namespace pak {

bool PakReader::LoadFromMemory(std::vector<uint8_t> pak_bytes) {
  loaded_ = false;
  entries_.clear();
  string_table_.clear();
  archive_bytes_ = std::move(pak_bytes);

  if (archive_bytes_.size() < sizeof(PakHeader)) {
    return false;
  }

  PakHeader header{};
  std::memcpy(&header, archive_bytes_.data(), sizeof(PakHeader));
  if (std::memcmp(header.magic, kPakMagic, 4) != 0 ||
      header.version != kPakVersion) {
    return false;
  }

  uint64_t toc_bytes =
      static_cast<uint64_t>(header.entry_count) * sizeof(PakTocEntry);
  if (header.toc_offset + toc_bytes > archive_bytes_.size()) {
    return false;
  }
  if (header.string_table_offset + header.string_table_size >
      archive_bytes_.size()) {
    return false;
  }

  entries_.resize(header.entry_count);
  if (toc_bytes > 0) {
    std::memcpy(entries_.data(), archive_bytes_.data() + header.toc_offset,
                static_cast<size_t>(toc_bytes));
  }

  string_table_.resize(static_cast<size_t>(header.string_table_size));
  if (header.string_table_size > 0) {
    std::memcpy(string_table_.data(),
                archive_bytes_.data() + header.string_table_offset,
                static_cast<size_t>(header.string_table_size));
  }

  // Validate bounds of every entry upfront so runtime reads are fast and safe.
  for (const PakTocEntry& entry : entries_) {
    if (static_cast<uint64_t>(entry.name_offset) + entry.name_length >
        string_table_.size()) {
      entries_.clear();
      return false;
    }
    if (entry.data_offset + entry.packed_size > archive_bytes_.size()) {
      entries_.clear();
      return false;
    }
  }

  loaded_ = true;
  return true;
}

std::string_view PakReader::EntryName(const PakTocEntry& entry) const {
  return std::string_view(string_table_.data() + entry.name_offset,
                          entry.name_length);
}

const PakTocEntry* PakReader::FindEntry(std::string_view path) const {
  if (!loaded_ || entries_.empty()) {
    return nullptr;
  }
  uint64_t target_hash = Fnv1a64(path);
  auto it = std::lower_bound(
      entries_.begin(), entries_.end(), target_hash,
      [](const PakTocEntry& e, uint64_t h) { return e.name_hash < h; });

  // Handle theoretical hash collisions linearly within equal hashes.
  while (it != entries_.end() && it->name_hash == target_hash) {
    if (EntryName(*it) == path) {
      return &(*it);
    }
    ++it;
  }
  return nullptr;
}

bool PakReader::HasEntry(std::string_view path) const {
  return FindEntry(path) != nullptr;
}

bool PakReader::ReadEntry(std::string_view path,
                          std::vector<uint8_t>* out_data) const {
  if (out_data == nullptr) {
    return false;
  }
  const PakTocEntry* entry = FindEntry(path);
  if (entry == nullptr) {
    return false;
  }

  const uint8_t* src = archive_bytes_.data() + entry->data_offset;
  if (entry->codec == static_cast<uint8_t>(CompressionCodec::kNone)) {
    if (entry->packed_size != entry->raw_size) {
      return false;
    }
    out_data->assign(src, src + entry->raw_size);
    return true;
  }
  if (entry->codec == static_cast<uint8_t>(CompressionCodec::kFastLz)) {
    return DecompressFastLz(src, entry->packed_size, entry->raw_size, out_data);
  }
  return false;
}

bool PakReader::ReadTextEntry(std::string_view path,
                              std::string* out_text) const {
  if (out_text == nullptr) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!ReadEntry(path, &bytes)) {
    return false;
  }
  out_text->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

std::vector<std::string> PakReader::ListEntries() const {
  std::vector<std::string> result;
  result.reserve(entries_.size());
  for (const PakTocEntry& entry : entries_) {
    result.emplace_back(EntryName(entry));
  }
  return result;
}

}  // namespace pak
}  // namespace minicraft
